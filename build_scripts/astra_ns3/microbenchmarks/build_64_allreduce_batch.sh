#!/bin/bash

# Absolue path to this script, default settings, do not change
SCRIPT_DIR=$(dirname "$(realpath $0)")
SCRIPT_DIR="${SCRIPT_DIR:?}"/..
ASTRA_SIM_DIR="${SCRIPT_DIR:?}"/../../astra-sim
NS3_DIR="${SCRIPT_DIR:?}"/../../extern/network_backend/ns-3
MEMORY="${SCRIPT_DIR:?}"/../../inputs/remote_memory/analytical/no_memory_expansion.json

# This script runs the "multiple 1D ring" AllReduce pattern: within each ToR pod (16 hosts/pod
# for 64 hosts), hosts are grouped by "index mod 16" into 16 independent rings (e.g. {0,16,32,48},
# {1,17,33,49}, ...), each doing its own 1D ring-AllReduce across the 4 pods. This pattern is
# driven by the --comm-group-configuration file below (see inputs/comm_group/64host_16ring.json)
# rather than by the system/logical-topology configuration, which is why those stay fixed at a
# single (unused) setting below.

# The workload for this test.
# The four uncommented sizes below (32/64/128/256 MB single pass) are the REQUIRED sweep -- you
# must report results for all four, on all three host-count tiers, averaged over the seeds below.
# The 256 MB x 4-pass entry is OPTIONAL: good supporting evidence for your report, but it
# costs hours of simulation time per config per seed. Uncomment it only if you have the budget.
WORKLOADS=( \
  ${SCRIPT_DIR:?}"/../../inputs/workload/microbenchmark_allreduce/64host_16ring_32mb_1pass/job" \
  ${SCRIPT_DIR:?}"/../../inputs/workload/microbenchmark_allreduce/64host_16ring_64mb_1pass/job" \
  ${SCRIPT_DIR:?}"/../../inputs/workload/microbenchmark_allreduce/64host_16ring_128mb_1pass/job" \
  ${SCRIPT_DIR:?}"/../../inputs/workload/microbenchmark_allreduce/64host_16ring_256mb_1pass/job" \
  #   ${SCRIPT_DIR:?}"/../../inputs/workload/microbenchmark_allreduce/64host_16ring_256mb_4pass/job" \
)

WORKLOAD_NAMES=( \
  "multiring_16ring_32mb_64nodes" \
  "multiring_16ring_64mb_64nodes" \
  "multiring_16ring_128mb_64nodes" \
  "multiring_16ring_256mb_1pass_64nodes" \
  #   "multiring_16ring_256mb_4pass_64nodes" \
)

# The communicator-group file that partitions the 64 hosts into 16 independent rings.
# This is what actually selects the "multiple 1D ring" pattern; see
# astra-sim/workload/Workload.cc (initialize_comm_group) and
# astra-sim/system/CommunicatorGroup.cc (get_collective_plan).
COMM_GROUP_CONFIG="${SCRIPT_DIR:?}"/../../inputs/comm_group/64host_16ring.json

# The sending config for this test, do not change
# We do not require test on different sending configs for microbenchmark
# but if you want to, you can uncomment the other configs
SYSTEMS=( \
  "${SCRIPT_DIR:?}"/../../inputs/system/Ring_2D_1_datasplit_1_parallel.json \
  # "${SCRIPT_DIR:?}"/../../inputs/system/Ring_2D_8_datasplit_1_parallel.json \
  # "${SCRIPT_DIR:?}"/../../inputs/system/Ring_2D_16_datasplit_1_parallel.json \
)

SYSTEM_NAMES=( \
  "ring_2D_1_datasplit_1_parallel" \
  # "ring_2D_8_datasplit_1_parallel" \
  # "ring_2D_16_datasplit_1_parallel" \
)

# The logical topology for this test, do not change
LOGICAL_TOPOLOGIES=( \
  "${SCRIPT_DIR:?}"/../../inputs/network/ns3/sample_64nodes_2D_16x4.json \
)

LOGICAL_TOPO_NAMES=( \
  "16x4" \
)

# The network config for this test, do not change
# If you want to test different network configs, please edit the "solution" config file
NETWORKS=( \
  "${NS3_DIR:?}"/scratch/config/spine_leaf_64_host_10g/config_spine_leaf_4_4_64_placeholder.txt \
  "${NS3_DIR:?}"/scratch/config/spine_leaf_64_host_10g/config_spine_leaf_4_4_64_ecmp_baseline.txt \
  "${NS3_DIR:?}"/scratch/config/spine_leaf_64_host_10g/config_spine_leaf_4_4_64_solution.txt \
)

NETWORK_CONFIG_NAMES=( \
  "4_4_64_placeholder" \
  "4_4_64_ecmp_baseline" \
  "4_4_64_solution" \
)

OUTPUT_DIR="${NS3_DIR:?}"/scratch/output/
RUNNING_LOG="${NS3_DIR:?}"/scratch/output/running_log.txt

# Experiment seeds
# For quick test, you can just use one seed, but for evaluation, please use multiple seeds
# Every number in your report must be averaged over at least these 10 seeds.
# Override from the environment to split the work across concurrent shells, e.g.
#   for s in 1 2 3 4 5 6 7 8 9 10; do SEEDS="$s" ./this_script.sh -r & done
# Each seed writes to its own report file and output directory, so this is safe.
# A single seed (SEEDS=1) is fine while developing, but not for anything you report.
RANDOM_SEEDS=(${SEEDS:-1 2 3 4 5 6 7 8 9 10})

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
    local comm_group_cfg="${10}"

    # This script always passes --allow-overlapping="true" to the simulator below, so a run is
    # never actually blocking -- no suffix needed. (Some other scripts in this repo instead check
    # an $ALLOWOVERLAPPING shell variable that is never actually assigned anywhere, so they always
    # mislabel report files with "_blocking" regardless of the real --allow-overlapping value
    # passed to the executable -- that's a pre-existing labeling bug, not a behavior difference.)
    local blocking=""

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
    echo "  comm_group=$comm_group_cfg, "
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
        --comm-group-configuration="$comm_group_cfg"
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
              run_experiment "$workload_cfg" "$workload_name" "$system_cfg" "$system_name" "$topo_cfg" "$topo_name" "$network_cfg" "$network_name" "$seed" "$COMM_GROUP_CONFIG"
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
