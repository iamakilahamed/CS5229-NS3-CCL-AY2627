#!/bin/bash

# Absolute path to this script, default settings, do not change
SCRIPT_DIR=$(dirname "$(realpath $0)")
SCRIPT_DIR="${SCRIPT_DIR:?}"/..
ASTRA_SIM_DIR="${SCRIPT_DIR:?}"/../../astra-sim
NS3_DIR="${SCRIPT_DIR:?}"/../../extern/network_backend/ns-3
MEMORY="${SCRIPT_DIR:?}"/../../inputs/remote_memory/analytical/no_memory_expansion.json

# ============================================================================
# MULTI-TENANT (multi-job) CHALLENGE WORKLOADS
#
# Unlike the microbenchmarks, where every host takes part in one collective of
# one size, these workloads run SEVERAL INDEPENDENT JOBS CONCURRENTLY on the
# same fabric. Jobs differ in how many hosts they span, how big their messages
# are, how many passes they run, and when they start.
#
# Three input files describe a multi-job run and they must be kept consistent:
#
#   1. the workload trace directory  -- job.<npu>.et, one file per host, flat.
#      Which job a host belongs to, its message size, its pass count and its
#      start delay are all baked into its trace.
#   2. --comm-group-configuration   -- {group_id: [npu ids]}. Defines the rings.
#   3. --job-configuration          -- {job_id: [group ids]}. Defines which
#      rings belong to which job. NOT read by the simulator: it exists so that
#      you can group the per-NPU lines in the report by job afterwards.
#
# All arrays below are indexed TOGETHER by workload index -- entry i of every
# array describes the same experiment. If you add a workload you must add a
# matching entry to every array.
# ============================================================================

WORKLOADS=( \
  ${SCRIPT_DIR:?}"/../../inputs/workload/multi_job_challenge/2job_32ring_16_16_32mb_32mb_16pass_200ms/job" \
  ${SCRIPT_DIR:?}"/../../inputs/workload/multi_job_challenge/3job_32ring_16_8_8_32mb16_16mb16_4mb64_200ms_300ms/job" \
  # A small 32-host, 2-job workload. Much faster than the three above -- use it
  # to check your setup works before spending hours on a 128-host run.
  # ${SCRIPT_DIR:?}"/../../inputs/workload/multi_job_challenge/2job_8ring_2x4_32mb_32mb_nodelay/job" \
)

WORKLOAD_NAMES=( \
  "mj_2job_16_16_128nodes" \
  "mj_3job_16_8_8_128nodes" \
  # "mj_2job_2x4_32nodes" \
)

# Host count of each workload above. Selects topology and network config tier.
HOST_COUNTS=( \
  128 \
  128 \
  # 32 \
)

# Communicator groups (the rings). Indexed with the workloads above.
COMM_GROUPS=( \
  ${SCRIPT_DIR:?}"/../../inputs/comm_group/128host_32ring.json" \
  ${SCRIPT_DIR:?}"/../../inputs/comm_group/128host_32ring.json" \
  # ${SCRIPT_DIR:?}"/../../inputs/comm_group/32host_8ring.json" \
)

COMM_GROUP_NAMES=( \
  "32ring" \
  "32ring" \
  # "8ring" \
)

# Job allocation: which rings belong to which job. Label/metadata only.
# NOTE: communicator groups smaller than 3 NPUs are not usable. A group of 1 makes
# the collective degenerate and the workload never reports as finished, which hangs
# the whole run -- CheckAllFinished never fires and the simulator spins forever.
# Keep every group at 3 NPUs or more.
JOB_CONFIGS=( \
  ${SCRIPT_DIR:?}"/../../inputs/job_allocation/2_job_32_group_16_16.json" \
  ${SCRIPT_DIR:?}"/../../inputs/job_allocation/3_job_32_group_16_8_8.json" \
  # ${SCRIPT_DIR:?}"/../../inputs/job_allocation/2_job_8_group_2x4.json" \
)

JOB_CONFIG_NAMES=( \
  "2job_16_16" \
  "3job_16_8_8" \
  # "2job_2x4" \
)

# The sending config, fixed -- as in the microbenchmarks, a subset communicator
# group overrides it, so it is inert here.
SYSTEM="${SCRIPT_DIR:?}"/../../inputs/system/Ring_2D_1_datasplit_1_parallel.json
SYSTEM_NAME="ring_2D_1_datasplit_1_parallel"

# Network configs to sweep. Paths are completed from HOST_COUNTS at run time.
NETWORK_CONFIGS=( \
  "placeholder" \
  "ecmp_baseline" \
  "solution" \
)

OUTPUT_DIR="${NS3_DIR:?}"/scratch/output/
RUNNING_LOG="${NS3_DIR:?}"/scratch/output/running_log.txt

# Experiment seeds. Override from the environment to spread the work over cores:
#   for s in 1 2 3 4 5; do SEEDS="$s" ./this_script.sh -r & done
# WARNING: these workloads are heavy. A single 128-host run takes hours -- see
# the runtime table in the top-level README before launching a full sweep.
RANDOM_SEEDS=(${SEEDS:-1})

# Resolve topology / network config paths from a host count
function topo_for {
    case "$1" in
        32)  echo "${SCRIPT_DIR:?}/../../inputs/network/ns3/sample_32nodes_2D_8x4.json" ;;
        64)  echo "${SCRIPT_DIR:?}/../../inputs/network/ns3/sample_64nodes_2D_16x4.json" ;;
        128) echo "${SCRIPT_DIR:?}/../../inputs/network/ns3/sample_128nodes_2D_32x4.json" ;;
        *)   echo "UNKNOWN_HOST_COUNT_$1" ;;
    esac
}
function topo_name_for {
    case "$1" in
        32) echo "8x4" ;; 64) echo "16x4" ;; 128) echo "32x4" ;; *) echo "unknown" ;;
    esac
}
function netcfg_for {
    # $1 = host count, $2 = config name
    case "$1" in
        32)  echo "${NS3_DIR:?}/scratch/config/spine_leaf_32_host_10g/config_spine_leaf_4_4_32_$2.txt" ;;
        64)  echo "${NS3_DIR:?}/scratch/config/spine_leaf_64_host_10g/config_spine_leaf_4_4_64_$2.txt" ;;
        128) echo "${NS3_DIR:?}/scratch/config/spine_leaf_128_host_100g/config_spine_leaf_4_4_128_$2.txt" ;;
        *)   echo "UNKNOWN_HOST_COUNT_$1" ;;
    esac
}

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
    local workload_cfg="$1" workload_name="$2" hosts="$3"
    local comm_group_cfg="$4" comm_group_name="$5"
    local job_cfg="$6" job_name="$7"
    local network_cfg="$8" network_name="$9" seed="${10}"

    local topo_cfg
    topo_cfg=$(topo_for "$hosts")
    local topo_name
    topo_name=$(topo_name_for "$hosts")

    # This script always passes --allow-overlapping="true", so a run is never
    # blocking and no suffix is needed.
    local blocking=""

    local report_file="${NS3_DIR:?}"/scratch/output/report_${workload_name}_${SYSTEM_NAME}_${topo_name}_${network_name}${blocking}_${comm_group_name}_${job_name}_${seed}.txt

    if [ ! -f "$RUNNING_LOG" ]; then
        echo "Running log file not found, creating it..."
        if [ ! -d "$OUTPUT_DIR" ]; then
            mkdir -p "$OUTPUT_DIR"
        fi
        touch "$RUNNING_LOG"
        echo "Timestamp, workload, system, topology, network, comm_group, job_cfg, seed" > "$RUNNING_LOG"
    fi

    echo "Running:"
    echo "  workload=$workload_cfg,"
    echo "  hosts=$hosts,"
    echo "  comm_group=$comm_group_cfg,"
    echo "  job=$job_cfg,"
    echo "  network=$network_cfg,"
    echo "  seed=$seed"

    cd "${NS3_DIR}/build/scratch"
    ./ns3.42-AstraSimNetwork-debug \
        --workload-configuration="$workload_cfg" \
        --workload-name="$workload_name" \
        --system-configuration="$SYSTEM" \
        --system-name="$SYSTEM_NAME" \
        --remote-memory-configuration="$MEMORY" \
        --network-configuration="$network_cfg" \
        --network-name="$network_name" \
        --logical-topology-configuration="$topo_cfg" \
        --topology-name="$topo_name" \
        --RngSeed="$seed" \
        --report-record-file="$report_file" \
        --allow-overlapping="true" \
        --comm-group-configuration="$comm_group_cfg" \
        --comm-group-name="$comm_group_name" \
        --job-configuration="$job_cfg" \
        --job-name="$job_name"
    cd "$SCRIPT_DIR"

    echo "$(date +%Y-%m-%dT%H:%M:%S), $workload_name, $SYSTEM_NAME, $topo_name, $network_name, $comm_group_name, $job_name, $seed" >> "$RUNNING_LOG"
}

# Main Script
case "$1" in
  -c|--compile)
    setup_proto
    compile_ns3
    ;;

  -r|--run)
    for i in "${!WORKLOADS[@]}"; do
      for network_config in "${NETWORK_CONFIGS[@]}"; do
        network_cfg=$(netcfg_for "${HOST_COUNTS[$i]}" "$network_config")
        network_name="4_4_${HOST_COUNTS[$i]}_${network_config}"
        for seed in "${RANDOM_SEEDS[@]}"; do
          run_experiment \
            "${WORKLOADS[$i]}" "${WORKLOAD_NAMES[$i]}" "${HOST_COUNTS[$i]}" \
            "${COMM_GROUPS[$i]}" "${COMM_GROUP_NAMES[$i]}" \
            "${JOB_CONFIGS[$i]}" "${JOB_CONFIG_NAMES[$i]}" \
            "$network_cfg" "$network_name" "$seed"
        done
      done
    done

    echo "All experiments completed. Check the report files in ${NS3_DIR:?}/scratch/output/."
    echo "To get per-job completion times, group the report lines by job with"
    echo "  python3 results/multijob_report.py <report file> <comm_group json> <job_allocation json>"
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
  -r | --run       Run every multi-job workload against every network config
  -h | --help      Show this message
  --clean          Clean the NS-3 build

Environment:
  SEEDS="1 2 3"    Override the seed list (default: 1)
EOF
    ;;
esac
