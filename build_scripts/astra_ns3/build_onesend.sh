#!/bin/bash

# Absolue path to this script, default settings, do not change
SCRIPT_DIR=$(dirname "$(realpath $0)")
ASTRA_SIM_DIR="${SCRIPT_DIR:?}"/../../astra-sim
NS3_DIR="${SCRIPT_DIR:?}"/../../extern/network_backend/ns-3
MEMORY="${SCRIPT_DIR:?}"/../../inputs/remote_memory/analytical/no_memory_expansion.json

# Inputs - change as necessary.
# WORKLOAD="${SCRIPT_DIR:?}"/../../inputs/workload/onesend/onesend
WORKLOAD="${SCRIPT_DIR:?}"/../../inputs/workload/onesend/onesend_2_to_2_4mb/onesend
WORKLOAD_NAME="onesend_2_to_2_4mb"

# Doesn't matter for onesend actually:
SYSTEM="${SCRIPT_DIR:?}"/../../inputs/system/Ring.json
SYSTEM_NAME="ring"
MEMORY="${SCRIPT_DIR:?}"/../../inputs/remote_memory/analytical/no_memory_expansion.json
LOGICAL_TOPOLOGY="${SCRIPT_DIR:?}"/../../inputs/network/ns3/sample_2nodes_1D.json
LOGICAL_TOPO_NAME="2nodes_1D"

# The network topology is a 1-2-4 spine-leaf, each ToR has 2 hosts connected.
NETWORK="${NS3_DIR:?}"/scratch/config/config_one_send.txt
NETWORK_NAME="one_send"

OUTPUT_DIR="${NS3_DIR:?}"/scratch/output/
RUNNING_LOG="${NS3_DIR:?}"/scratch/output/running_log.txt


# Functions
function setup {
    protoc et_def.proto\
        --proto_path ${SCRIPT_DIR}/../../extern/graph_frontend/chakra/et_def/\
        --cpp_out ${SCRIPT_DIR}/../../extern/graph_frontend/chakra/et_def/
}
function compile {
    cd "${NS3_DIR}"
    ./ns3 configure --build-profile=debug --enable-mpi --enable-python-bindings --enable-examples --enable-tests
    ./ns3 build AstraSimNetwork -j 8
    cd "${SCRIPT_DIR:?}"
}

function run {

    local report_file="${NS3_DIR:?}"/scratch/output/report_"${WORKLOAD_NAME}"_"${SYSTEM_NAME}"_"${LOGICAL_TOPO_NAME}"_"${NETWORK_NAME}".txt

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

    cd "${NS3_DIR}/build/scratch"
    ./ns3.42-AstraSimNetwork-debug \
        --workload-configuration=${WORKLOAD} \
        --workload-name=${WORKLOAD_NAME} \
        --system-configuration=${SYSTEM} \
        --system-name=${SYSTEM_NAME} \
        --network-configuration=${NETWORK} \
        --network-name=${NETWORK_NAME} \
        --remote-memory-configuration=${MEMORY} \
        --logical-topology-configuration=${LOGICAL_TOPOLOGY} \
        --topology-name=${LOGICAL_TOPO_NAME} \
        --report-record-file=${report_file} \
        --allow-overlapping="true" \
        --comm-group-configuration=\"empty\"
    cd "${SCRIPT_DIR:?}"
}

function run_ns3 {
    cd "${NS3_DIR}"
    ./ns3 run " AstraSimNetwork
        --workload-configuration=${WORKLOAD} 
        --system-configuration=${SYSTEM} 
        --network-configuration=${NETWORK} 
        --remote-memory-configuration=${MEMORY} 
        --logical-topology-configuration=${LOGICAL_TOPOLOGY} 
        --comm-group-configuration=\"empty\""
    cd "${SCRIPT_DIR:?}"
}
function run_debug {
    cd "${NS3_DIR}/build/scratch"
    ./ns3.42-AstraSimNetwork-debug \
        --workload-configuration=${WORKLOAD} \
        --system-configuration=${SYSTEM} \
        --network-configuration=${NETWORK} \
        --remote-memory-configuration=${MEMORY} \
        --logical-topology-configuration=${LOGICAL_TOPOLOGY} \
        --comm-group-configuration=\"empty\"
    cd "${SCRIPT_DIR:?}"
}
function cleanup {
    cd "${NS3_DIR}"
    ./ns3 clean
    cd "${SCRIPT_DIR:?}"
}
function debug {
    cd "${NS3_DIR}"
    ./ns3 configure --enable-mpi --build-profile debug
    ./ns3 build AstraSimNetwork -j 12 -v
    cd "${NS3_DIR}/build/scratch"
    gdb -ex=r -ex=bt --batch \
        --args "${NS3_DIR}/build/scratch/ns3.42-AstraSimNetwork-debug" \
        --workload-configuration=${WORKLOAD} \
        --system-configuration=${SYSTEM} \
        --network-configuration=${NETWORK} \
        --remote-memory-configuration=${MEMORY} \
        --logical-topology-configuration=${LOGICAL_TOPOLOGY} \
        --comm-group-configuration=\"empty\"
}
function special_debug {
    cd "${NS3_DIR}/build/scratch"
    valgrind --leak-check=yes "${NS3_DIR}/build/scratch/ns3.42-AstraSimNetwork-debug" \
        --workload-configuration=${WORKLOAD} \
        --system-configuration=${SYSTEM} \
        --network-configuration=${NETWORK} \
        --remote-memory-configuration=${MEMORY} \
        --logical-topology-configuration=${LOGICAL_TOPOLOGY} \
        --comm-group-configuration=\"empty\"
}
# Main Script
case "$1" in
--clean)
    cleanup;;
-d|--debug)
    setup
    debug;;
-c|--compile)
    setup
    compile;;
-r|--run)
    run;;
-h|--help|*)
    printf "Prints help message";;
esac
