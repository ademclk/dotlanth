#!/bin/bash
# TOOL-011: Crash reproducer script
#
# Reproduces a crash from a crash artifact file.
# Useful for debugging crashes found during fuzzing.
#
# Usage: ./fuzz/scripts/reproduce_crash.sh <fuzzer_name> <crash_file>

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/ci-clang-fuzz}"

usage() {
    cat <<EOF
Usage: $(basename "$0") <fuzzer_name> <crash_file>

Reproduces a crash by running the fuzzer with a specific input file.
The fuzzer runs under the same conditions as during fuzzing (ASan enabled).

Arguments:
  fuzzer_name   Name of the fuzzer (e.g., dsl_fuzzer)
  crash_file    Path to the crash artifact file

Examples:
  $(basename "$0") dsl_fuzzer fuzz-artifacts/dsl_fuzzer/crash-abc123
  $(basename "$0") execute_fuzzer /tmp/crash-def456

Environment:
  BUILD_DIR     Build directory (default: build/ci-clang-fuzz)

Tips:
  - Use gdb for debugging: gdb --args <fuzzer> <crash_file>
  - Set ASAN_OPTIONS=abort_on_error=1 for core dumps
  - Use llvm-symbolizer for better stack traces
EOF
    exit 1
}

if [[ $# -lt 2 ]]; then
    usage
fi

FUZZER_NAME="$1"
CRASH_FILE="$2"

FUZZER_PATH="$BUILD_DIR/fuzz/$FUZZER_NAME"

# Check if fuzzer exists
if [[ ! -x "$FUZZER_PATH" ]]; then
    echo "Error: Fuzzer not found at $FUZZER_PATH"
    echo "Build with: cmake --preset ci-clang-fuzz && cmake --build --preset ci-clang-fuzz"
    exit 1
fi

# Check if crash file exists
if [[ ! -f "$CRASH_FILE" ]]; then
    echo "Error: Crash file not found: $CRASH_FILE"
    exit 1
fi

echo "=== Reproducing crash ==="
echo "  Fuzzer: $FUZZER_PATH"
echo "  Input:  $CRASH_FILE ($(wc -c < "$CRASH_FILE") bytes)"
echo ""

# Show hex dump of input (first 256 bytes)
echo "Input preview (hex):"
xxd -l 256 "$CRASH_FILE" || hexdump -C -n 256 "$CRASH_FILE" || true
echo ""

# Set ASan options for better debugging
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:print_stats=1:abort_on_error=1}"

echo "Running fuzzer..."
echo "---"

# Run the fuzzer with the crash file
# Exit code 77 means the crash was reproduced
"$FUZZER_PATH" "$CRASH_FILE" || {
    EXIT_CODE=$?
    echo "---"
    if [[ $EXIT_CODE -eq 77 ]]; then
        echo "Crash reproduced (exit code 77)"
    else
        echo "Fuzzer exited with code $EXIT_CODE"
    fi
    exit $EXIT_CODE
}

echo "---"
echo "No crash reproduced. The input may not trigger the original issue."
