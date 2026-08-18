#!/bin/bash
# Generate the Chakra traces for one multi-job workload.
#
# Usage:
#   ./generate_multi_job_workload.sh <job_setup.json> [output_dir]
#
# Example:
#   ./generate_multi_job_workload.sh job_setups/2job_8ring_2x4_32mb_32mb_nodelay.json
#
# With no output_dir, traces land in
#   ../inputs/workload/multi_job_challenge/<setup file name without .json>/
# which is where the batch script expects to find them.
#
# The setup file describes each job: which text workload body it uses, which global
# NPU ids it owns, how many passes it runs, and how long it waits before starting.
# Every NPU in the machine must be covered by exactly one job -- give idle hosts a
# job pointing at workloads_text/empty.txt, otherwise the simulator will fail when a
# rank tries to open a trace that was never written.
#
# Requires the chakra python bindings. Run this INSIDE the container:
#   docker exec -it cs5229-astra-sim bash
#   cd /app/astra-sim/workload_generator && ./generate_multi_job_workload.sh ...

set -euo pipefail

GEN_DIR=$(dirname "$(realpath "$0")")
REPO_DIR=$(realpath "${GEN_DIR}/..")
CHAKRA_DIR="${REPO_DIR}/extern/graph_frontend/chakra"

if [ $# -lt 1 ]; then
    sed -n '2,20p' "$0" | sed 's/^# \?//'
    exit 1
fi

SETUP_FILE=$(realpath "$1")
if [ ! -f "$SETUP_FILE" ]; then
    echo "ERROR: setup file not found: $SETUP_FILE" >&2
    exit 1
fi

if [ $# -ge 2 ]; then
    OUTPUT_DIR=$(realpath -m "$2")
else
    NAME=$(basename "$SETUP_FILE" .json)
    OUTPUT_DIR="${REPO_DIR}/inputs/workload/multi_job_challenge/${NAME}"
fi

# The converter needs chakra.et_def.et_def_pb2, which is generated from the .proto.
# The repo build only generates the C++ bindings, so generate the python ones here if
# they are missing.
if [ ! -f "${CHAKRA_DIR}/et_def/et_def_pb2.py" ]; then
    echo "Generating python protobuf bindings for chakra..."
    protoc et_def.proto \
        --proto_path "${CHAKRA_DIR}/et_def/" \
        --python_out "${CHAKRA_DIR}/et_def/"
fi

# chakra has no __init__.py files; import it as a namespace package.
export PYTHONPATH="${REPO_DIR}/extern/graph_frontend:${PYTHONPATH:-}"

mkdir -p "$OUTPUT_DIR"
echo "setup : $SETUP_FILE"
echo "output: $OUTPUT_DIR"

python3 "${GEN_DIR}/multi_job_converter.py" \
    --setup_filename "$SETUP_FILE" \
    --output_dir "$OUTPUT_DIR" \
    --log_filename "${OUTPUT_DIR}/convert.log"

echo
echo "Generated $(ls "$OUTPUT_DIR"/job.*.et 2>/dev/null | wc -l) trace files."
echo "Check that the NPU ids are contiguous from 0 -- every host needs a trace:"
echo "  ls ${OUTPUT_DIR} | wc -l"
