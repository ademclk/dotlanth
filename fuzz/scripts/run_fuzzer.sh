#!/bin/bash
# TOOL-011: Fuzzer runner script
#
# Usage: ./fuzz/scripts/run_fuzzer.sh <fuzzer_name> [options]
#
# Examples:
#   ./fuzz/scripts/run_fuzzer.sh dsl_fuzzer
#   ./fuzz/scripts/run_fuzzer.sh asm_fuzzer -max_total_time=600
#   ./fuzz/scripts/run_fuzzer.sh execute_fuzzer -jobs=4

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORPUS_DIR="$SCRIPT_DIR/../corpus"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/ci-clang-fuzz}"

usage() {
    cat <<EOF
Usage: $(basename "$0") <fuzzer_name> [libfuzzer_options]

Fuzzers:
  bytecode_fuzzer  - Test bytecode header parsing
  dsl_fuzzer       - Test DSL parser
  asm_fuzzer       - Test assembly parser
  execute_fuzzer   - Test execution engine
  capi_fuzzer      - Test C API

Options are passed directly to libFuzzer.
Common options:
  -max_total_time=N     Run for N seconds (default: unlimited)
  -max_len=N            Maximum input length
  -jobs=N               Run N parallel jobs
  -workers=N            Use N parallel workers
  -dict=path            Use dictionary file
  -artifact_prefix=path  Where to store crash artifacts

Environment:
  BUILD_DIR             Build directory (default: build/ci-clang-fuzz)

Examples:
  $(basename "$0") dsl_fuzzer -max_total_time=300
  $(basename "$0") execute_fuzzer -jobs=4 -workers=4
EOF
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

FUZZER_NAME="$1"
shift

# Determine corpus directory based on fuzzer name
case "$FUZZER_NAME" in
    bytecode_fuzzer)
        CORPUS_SUBDIR="bytecode"
        ;;
    dsl_fuzzer)
        CORPUS_SUBDIR="dsl"
        ;;
    asm_fuzzer)
        CORPUS_SUBDIR="asm"
        ;;
    execute_fuzzer)
        CORPUS_SUBDIR="execute"
        ;;
    capi_fuzzer)
        CORPUS_SUBDIR="bytecode"
        ;;
    *)
        echo "Error: Unknown fuzzer '$FUZZER_NAME'"
        usage
        ;;
esac

FUZZER_PATH="$BUILD_DIR/fuzz/$FUZZER_NAME"
CORPUS_PATH="$CORPUS_DIR/$CORPUS_SUBDIR"
ARTIFACT_DIR="$REPO_ROOT/fuzz-artifacts/$FUZZER_NAME"

# Check if fuzzer exists
if [[ ! -x "$FUZZER_PATH" ]]; then
    echo "Error: Fuzzer not found at $FUZZER_PATH"
    echo ""
    echo "Build with:"
    echo "  cmake --preset ci-clang-fuzz"
    echo "  cmake --build --preset ci-clang-fuzz --target $FUZZER_NAME"
    exit 1
fi

# Create artifact directory
mkdir -p "$ARTIFACT_DIR"

echo "=== Running $FUZZER_NAME ==="
echo "  Fuzzer:    $FUZZER_PATH"
echo "  Corpus:    $CORPUS_PATH"
echo "  Artifacts: $ARTIFACT_DIR"
echo ""

# Run the fuzzer
exec "$FUZZER_PATH" "$CORPUS_PATH" \
    -artifact_prefix="$ARTIFACT_DIR/" \
    "$@"
