#!/usr/bin/env python3

import logging
import argparse
import logging
import os
import sys
import traceback
import json
import random
from logging import FileHandler
from io import TextIOWrapper
from typing import Any, List
from chakra.third_party.utils.protolib import encodeMessage as encode_message
from chakra.et_def.et_def_pb2 import (
    Node as ChakraNode,
    BoolList,
    AttributeProto as ChakraAttr,
    GlobalMetadata,
    COMP_NODE,
    COMM_COLL_NODE,
    ALL_REDUCE,
    ALL_TO_ALL,
    ALL_GATHER,
    REDUCE_SCATTER,
)

random.seed(42)

class Layer:
    def __init__(
        self,
        line: str
    ) -> None:
        try:
            col = line.strip().split()
            self.name = col[0]

            # forward
            self.fwd_comp_time = int(col[2])
            self.fwd_comm_type = str(col[3])
            self.fwd_comm_size = int(col[4])
            self.fwd_comp_node = None
            self.fwd_comm_node = None

            # backward input gradient
            self.bwd_ig_comp_time = int(col[5])
            self.bwd_ig_comm_type = str(col[6])
            self.bwd_ig_comm_size = int(col[7])
            self.bwd_ig_comp_node = None
            self.bwd_ig_comm_node = None

            # backward weight gradient
            self.bwd_wg_comp_time = int(col[8])
            self.bwd_wg_comm_type = str(col[9])
            self.bwd_wg_comm_size = int(col[10])
            self.bwd_wg_update_time = str(col[11])
            self.bwd_wg_comp_node = None
            self.bwd_wg_comm_node = None
        except:
            raise ValueError(f"Cannot parse the following layer -- \"{line}\"")


class MultiJobConverter:
    def __init__(
        self,
        setup_filename: str,
        output_dir: str,
        logger: logging.Logger
    ) -> None:
        self.setup_filename = setup_filename
        self.output_dir = output_dir
        self.logger = logger
        self.next_node_id = 1

    def get_layers(
        self,
        f: TextIOWrapper,
        num_layers: int
    ) -> List[Layer]:
        layers = []
        for line in f:
            layers.append(Layer(line))
        return layers

    def get_comp_node(
        self,
        layer_name: str,
        phase: str,
        comp_time: int
    ) -> Any:
        node = ChakraNode()
        node.id = self.next_node_id
        self.next_node_id += 1
        node.name = f"{layer_name}_{phase}"
        node.type = COMP_NODE
        node.duration_micros = comp_time
        node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
        return node

    def get_comm_type(
        self,
        comm_type: str
    ) -> int:
        if comm_type == "ALLREDUCE":
            return ALL_REDUCE
        elif comm_type == "ALLTOALL":
            return ALL_TO_ALL
        elif comm_type == "ALLGATHER":
            return ALL_GATHER
        elif comm_type == "REDUCESCATTER":
            return REDUCE_SCATTER
        return 0

    def get_comm_coll_node(
        self,
        layer_name: str,
        comm_type: str,
        comm_size: int
    ) -> Any:
        node = ChakraNode()
        node.id = self.next_node_id
        self.next_node_id += 1

        node.name = f"{layer_name}_{comm_type}"
        node.type = COMM_COLL_NODE
        node.attr.append(ChakraAttr(name="is_cpu_op", bool_val=False))
        node.attr.append(ChakraAttr(name="comm_type", int64_val=self.get_comm_type(comm_type)))
        node.attr.append(ChakraAttr(name="comm_size", uint64_val=comm_size))
        return node

    def add_dependency(
        self,
        child_node: Any,
        parent_node: Any
    ) -> None:
        child_node.data_deps.append(parent_node.id)

    def convert_job(self, job_config: dict, job_id: int) -> None:
        input_filename = job_config["input_filename"]
        # Relative paths in the setup file are resolved first against the directory
        # the setup file lives in, then against this script's directory (which is
        # where workloads_text/ sits). That way the converter can be run from
        # anywhere, and setup files can live outside the generator directory.
        if not os.path.isabs(input_filename):
            candidates = [
                os.path.join(os.path.dirname(os.path.abspath(self.setup_filename)),
                             input_filename),
                os.path.join(os.path.dirname(os.path.abspath(__file__)), input_filename),
            ]
            for candidate in candidates:
                if os.path.isfile(candidate):
                    input_filename = candidate
                    break
            else:
                raise FileNotFoundError(
                    f"workload body '{input_filename}' for job {job_id} not found; "
                    f"looked in: {candidates}"
                )
        # num_npus = job_config["num_npus"] # Deprecated, use npu_indices instead
        npu_indices = job_config["npu_indices"]
        num_passes = job_config["num_passes"]
        num_dims = job_config["num_dims"]
        start_delay_us = job_config.get("start_delay_us", 0)
        # npu_id_offset = job_config.get("npu_id_offset", 0) # Deprecated
        inter_pass_delay_randomized = job_config.get("inter_pass_delay_randomized", False)
        inter_pass_delay_cap = job_config.get("inter_pass_delay_cap_us", 0)


        # start delay of each pass
        inter_pass_delay_us = []
        for i in range(num_passes):
            if inter_pass_delay_randomized:
                inter_pass_delay = random.randint(0, inter_pass_delay_cap)
                inter_pass_delay_us.append(inter_pass_delay)
            else:
                inter_pass_delay_us.append(inter_pass_delay_cap)

        with open(input_filename, "r") as f:
            print(f"Reading from file: {input_filename} for Job {job_id}")
            first_line = f.readline().strip().split()
            parallelism_type = first_line[0]
            num_layers = int(f.readline().strip())

            if parallelism_type != "DATA":
                raise ValueError(f"Only DATA parallelism is supported for now, got {parallelism_type}")

            layers = self.get_layers(f, num_layers)
            
            for global_npu_id in npu_indices:
                output_filename = f"{self.output_dir}/job.{global_npu_id}.et"
                print(f"Generating {output_filename}")
                
                with open(output_filename, "wb") as g:
                    encode_message(g, GlobalMetadata(version="0.0.4"))
                    
                    # Reset node ID for each NPU trace
                    self.next_node_id = 1

                    # Add start delay if specified
                    if start_delay_us > 0:
                        dummy_comp_node = self.get_comp_node("start_delay", "DELAY", start_delay_us)
                        encode_message(g, dummy_comp_node)
                        # The first real node will depend on this dummy node
                        first_node_dep = dummy_comp_node
                    else:
                        first_node_dep = None

                    for i in range(num_passes):
                        if i > 0 and inter_pass_delay_us[i] > 0:
                            dummy_comp_node = self.get_comp_node("inter_pass_delay", "DELAY", inter_pass_delay_us[i])
                            encode_message(g, dummy_comp_node)
                            # The first node of this pass will depend on this dummy node
                            first_node_dep = dummy_comp_node

                        fwd_comp_node = None

                        # forward pass, only computation nodes
                        for idx, layer in enumerate(layers):
                            fwd_comp_node = self.get_comp_node(
                                    layer.name, 
                                    "FWD",
                                    layer.fwd_comp_time)
                            
                            if idx == 0:
                                if first_node_dep:
                                    self.add_dependency(fwd_comp_node, first_node_dep)
                                    # Only add dependency for the very first node of the first pass
                                    if i == 0:
                                        first_node_dep = None 
                                elif i > 0: # Dependency between passes
                                     # This logic might need adjustment based on how passes are connected
                                     # For now, assuming independent passes or implicit dependency via order
                                     pass
                            
                            if idx != 0:
                                self.add_dependency(fwd_comp_node, layers[idx-1].fwd_comp_node)
                            
                            if layer.bwd_wg_comm_node != None:
                                self.add_dependency(fwd_comp_node, layer.bwd_wg_comm_node)
                            
                            layer.fwd_comp_node = fwd_comp_node
                            assert layer.fwd_comm_node == None
                            encode_message(g, fwd_comp_node)

                        # backward pass, compute first, then communication
                        for idx, layer in enumerate(reversed(layers)):
                            bwd_wg_comp_node = self.get_comp_node(
                                    layer.name, 
                                    "BWD_WG",
                                    layer.bwd_wg_comp_time)
                            
                            if idx == 0:
                                if fwd_comp_node == None:
                                    raise ValueError("fwd_comp_node is None")
                                self.add_dependency(bwd_wg_comp_node, fwd_comp_node)
                            else:
                                self.add_dependency(bwd_wg_comp_node, layers[len(layers)-idx].bwd_ig_comp_node)
                            
                            encode_message(g, bwd_wg_comp_node)

                            bwd_wg_comm_node = self.get_comm_coll_node(
                                    layer.name,
                                    layer.bwd_wg_comm_type,
                                    layer.bwd_wg_comm_size)
                            
                            attr = ChakraAttr(name="involved_dim")
                            for _ in range(num_dims):
                                attr.bool_list.values.append(True)
                            bwd_wg_comm_node.attr.append(attr)

                            self.add_dependency(bwd_wg_comm_node, bwd_wg_comp_node)
                            layer.bwd_wg_comm_node = bwd_wg_comm_node

                            encode_message(g, bwd_wg_comm_node)

                            if idx != (len(layers) - 1):
                                bwd_ig_comp_node = self.get_comp_node(
                                        layer.name, "BWD_IG",
                                        layer.bwd_ig_comp_time)
                                self.add_dependency(bwd_ig_comp_node, bwd_wg_comm_node)
                                layer.bwd_ig_comp_node = bwd_ig_comp_node
                                encode_message(g, bwd_ig_comp_node)

                    for layer in layers:
                        layer.bwd_wg_comm_node = None

    def convert(self) -> None:
        with open(self.setup_filename, "r") as f:
            setup_config = json.load(f)
        
        for job_id, job_config in setup_config.items():
            self.convert_job(job_config, int(job_id))

def get_logger(log_filename: str) -> logging.Logger:
    formatter = logging.Formatter(
            "%(levelname)s [%(asctime)s] %(message)s",
            datefmt="%m/%d/%Y %I:%M:%S %p")

    file_handler = FileHandler(log_filename, mode="w")
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(formatter)

    stream_handler = logging.StreamHandler()
    stream_handler.setLevel(logging.WARNING)
    stream_handler.setFormatter(formatter)

    logger = logging.getLogger(__file__)
    logger.setLevel(logging.DEBUG)
    logger.addHandler(file_handler)
    logger.addHandler(stream_handler)

    return logger

def main() -> None:
    parser = argparse.ArgumentParser(
            description="Multi-Job Execution Trace Converter"
    )
    parser.add_argument(
            "--setup_filename",
            type=str,
            required=True,
            help="JSON file containing job setup configuration"
    )
    parser.add_argument(
            "--output_dir",
            type=str,
            required=True,
            help="Output directory for Chakra execution traces"
    )
    parser.add_argument(
            "--log_filename",
            type=str,
            default="multi_job_converter.log",
            help="Log filename"
    )
    args = parser.parse_args()

    logger = get_logger(args.log_filename)
    logger.debug(" ".join(sys.argv))

    try:
        converter = MultiJobConverter(
            args.setup_filename,
            args.output_dir,
            logger
        )
        converter.convert()
    except Exception as e:
        traceback.print_exc()
        logger.debug(traceback.format_exc())
        sys.exit(1)

if __name__ == "__main__":
    main()
