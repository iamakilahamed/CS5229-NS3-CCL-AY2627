#!/bin/bash

# Absolue path to this script, default settings, do not change
SCRIPT_DIR=$(dirname "$(realpath $0)")
SCRIPT_DIR="${SCRIPT_DIR:?}"/..
ASTRA_SIM_DIR="${SCRIPT_DIR:?}"/../../astra-sim
NS3_DIR="${SCRIPT_DIR:?}"/../../extern/network_backend/ns-3
MEMORY="${SCRIPT_DIR:?}"/../../inputs/remote_memory/analytical/no_memory_expansion.json

# The workload for this test, do not change
WORKLOADS=( \
  ${SCRIPT_DIR:?}"/../../inputs/workload/resnet/resnet50_128_2D_1pass/resnet50" \
)

WORKLOAD_NAMES=( \
  "resnet50_1pass_128nodes" \
)

# The sending config for this test, do not change
SYSTEMS=( \
  "${SCRIPT_DIR:?}"/../../inputs/system/Ring_2D_1_datasplit_1_parallel.json \
)

SYSTEM_NAMES=( \
  "ring_2d_1s1p" \
)

# The logical topology for this test, do not change
LOGICAL_TOPOLOGIES=( \
  "${SCRIPT_DIR:?}"/../../inputs/network/ns3/sample_128nodes_2D_32x4.json \
)

LOGICAL_TOPO_NAMES=( \
  "32x4" \
)

# The network config for this test, do not change
# If you want to test different network configs, please edit the "solution" config file
NETWORKS=( \
  "${NS3_DIR:?}"/scratch/config/spine_leaf_128_host_100g/config_spine_leaf_4_4_128_placeholder.txt \
  "${NS3_DIR:?}"/scratch/config/spine_leaf_128_host_100g/config_spine_leaf_4_4_128_ecmp_baseline.txt \
  "${NS3_DIR:?}"/scratch/config/spine_leaf_128_host_100g/config_spine_leaf_4_4_128_solution.txt \
)

NETWORK_CONFIG_NAMES=( \
  "4_4_128_placeholder" \
  "4_4_128_ecmp_baseline" \
  "4_4_128_solution" \
)

OUTPUT_DIR="${NS3_DIR:?}"/scratch/output/
RUNNING_LOG="${NS3_DIR:?}"/scratch/output/running_log.txt

# Experiment seeds
# For quick test, you can just use one seed, but for evaluation, please use multiple seeds
# RANDOM_SEEDS=(1 2 3 4 5 6 7 8 9 10)
RANDOM_SEEDS=(1)

# Helper functions
function setup_proto {
    protoc et_def.proto \
        --proto_path ${SCRIPT_DIR}/../../extern/graph_frontend/chakra/et_def/ \
        --cpp_out ${SCRIPT_DIR}/../../extern/graph_frontend/chakra/et_def/
}

function compile_ns3 {
    cd "$NS3_DIR"
    ./ns3 configure --build-profile=debug --enable-mpi --enable-python-bindings --enable-examples --enable-tests
    ./ns3 build AstraSimNetwork -j 8
    cd "$SCRIPT_DIR"
}

function run_experiment {
    local workload_cfg="$1"
    local workload_name="$2"
    local system_cfg="$3"
    local system_name="$4"
    local topo_cfg="$5"
    local topo_name="$6"
    local network_cfg="$7"
    local network_name="$8"
    local seed="$9"

    # if ALLOVERLAPPING is true, then blocking="", else blocking="blocking"
    local blocking=""
    if [ "$ALLOWOVERLAPPING" = true ]; then
        blocking=""
    else
        blocking="_blocking"
    fi

    local report_file="${NS3_DIR:?}"/scratch/output/report_${workload_name}_${system_name}_${topo_name}_${network_name}${blocking}_${seed}.txt

    # If running log file not found, create it
    if [ ! -f "$RUNNING_LOG" ]; then
        echo "Running log file not found, creating it..."
        # create the file and add header
        # if output directory does not exist, create it
        if [ ! -d "$OUTPUT_DIR" ]; then
            mkdir -p "$OUTPUT_DIR"
        fi
        # if running log file does not exist, create it
        if [ ! -f "$RUNNING_LOG" ]; then
            touch "$RUNNING_LOG"
        fi
        echo "Timestamp, workload, system, topology, network, seed" > "$RUNNING_LOG"
    fi


    echo "Running:"
    echo "  workload=$workload_cfg, "
    echo "  system=$system_cfg, "
    echo "  memory=$MEMORY, "
    echo "  topo=$topo_cfg, "
    echo "  network=$network_cfg, "
    echo "  seed=$seed"

    cd "${NS3_DIR}/build/scratch"
    ./ns3.42-AstraSimNetwork-debug \
        --workload-configuration="$workload_cfg" \
        --workload-name="$workload_name" \
        --system-configuration="$system_cfg" \
        --system-name="$system_name" \
        --remote-memory-configuration="$MEMORY" \
        --network-configuration="$network_cfg" \
        --network-name="$network_name" \
        --logical-topology-configuration="$topo_cfg" \
        --topology-name="$topo_name" \
        --RngSeed="$seed" \
        --report-record-file="$report_file" \
        --allow-overlapping="true" \
        --comm-group-configuration="empty"
    cd "$SCRIPT_DIR"

    # Record finish timestamp and parameters
    echo "$(date +%Y-%m-%dT%H:%M:%S), $workload_name, $system_name, $topo_name, $network_name, $seed" >> "$RUNNING_LOG"
}

# Main Script
case "$1" in
  -c|--compile)
    setup_proto
    compile_ns3
    ;;

  -r|--run)
    # Go through all workload configurations
    for workload_index in "${!WORKLOADS[@]}"; do
      workload_cfg="${WORKLOADS[$workload_index]}"
      workload_name="${WORKLOAD_NAMES[$workload_index]}"
      
      # Go through all system configurations
      for system_index in "${!SYSTEMS[@]}"; do
        system_cfg="${SYSTEMS[$system_index]}"
        system_name="${SYSTEM_NAMES[$system_index]}"
        
        # Go through all logical topologies
        for topo_index in "${!LOGICAL_TOPO_NAMES[@]}"; do
          topo_cfg="${LOGICAL_TOPOLOGIES[$topo_index]}"
          topo_name="${LOGICAL_TOPO_NAMES[$topo_index]}"

          # Go through all network configurations
          for network_index in "${!NETWORK_CONFIG_NAMES[@]}"; do
            network_cfg="${NETWORKS[$network_index]}"
            network_name="${NETWORK_CONFIG_NAMES[$network_index]}"

            # Go through all random seeds
            for seed in "${RANDOM_SEEDS[@]}"; do
              run_experiment "$workload_cfg" "$workload_name" "$system_cfg" "$system_name" "$topo_cfg" "$topo_name" "$network_cfg" "$network_name" "$seed"
            done
          
          done
        
        done
      
      done
    
    done

    echo "All experiments completed. Check the report files in ${NS3_DIR:?}/scratch/output/ for results."
    echo "Running log can be found at ${RUNNING_LOG:?}."
    ;;

  --clean)
    cd "$NS3_DIR"
    ./ns3 clean
    cd "$SCRIPT_DIR"
    ;;

  -h|--help|*)
    cat <<EOF
Usage: $0 [OPTIONS]
  -c | --compile   Set up and build NS-3 + AstraSim
  -r | --run       Run experiments for each config group across all seeds
  -h | --help      Show this message
  --clean          Clean the NS-3 build
EOF
    ;;
esac
