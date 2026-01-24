#!/bin/bash
# TOOL-011: Fuzzer coverage report generator
#
# Generates coverage reports for fuzzer runs using llvm-cov.
# Requires DOTVM_ENABLE_COVERAGE=ON during build.
#
# Usage: ./scripts/coverage/fuzz_coverage.sh <fuzzer_name> [options]
#
# Examples:
#   ./scripts/coverage/fuzz_coverage.sh dsl_fuzzer
#   ./scripts/coverage/fuzz_coverage.sh execute_fuzzer --html
#   ./scripts/coverage/fuzz_coverage.sh asm_fuzzer --json

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/ci-clang-fuzz}"
COVERAGE_DIR="$REPO_ROOT/coverage/fuzz"

# Default options
GENERATE_HTML=false
GENERATE_JSON=false
CORPUS_RUNS=0  # 0 means use default behavior (collect without generating)

usage() {
    cat <<EOF
Usage: $(basename "$0") <fuzzer_name> [options]

Generates coverage report for a fuzzer by running it against its corpus.

Options:
  --html          Generate HTML coverage report
  --json          Generate JSON coverage report
  --runs=N        Number of additional fuzzer runs (default: 0)
  --help          Show this help

Fuzzers:
  bytecode_fuzzer, dsl_fuzzer, asm_fuzzer, execute_fuzzer, capi_fuzzer

Requirements:
  - Build with DOTVM_ENABLE_COVERAGE=ON (use ci-clang-fuzz preset)
  - llvm-profdata and llvm-cov must be in PATH

Environment:
  BUILD_DIR       Build directory (default: build/ci-clang-fuzz)
  LLVM_PROFDATA   Path to llvm-profdata (default: llvm-profdata)
  LLVM_COV        Path to llvm-cov (default: llvm-cov)

Output:
  Coverage reports are written to: coverage/fuzz/<fuzzer_name>/
EOF
    exit 1
}

# Parse arguments
if [[ $# -lt 1 ]]; then
    usage
fi

FUZZER_NAME=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --html)
            GENERATE_HTML=true
            shift
            ;;
        --json)
            GENERATE_JSON=true
            shift
            ;;
        --runs=*)
            CORPUS_RUNS="${1#*=}"
            shift
            ;;
        --help)
            usage
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            ;;
        *)
            if [[ -z "$FUZZER_NAME" ]]; then
                FUZZER_NAME="$1"
            else
                echo "Unexpected argument: $1"
                usage
            fi
            shift
            ;;
    esac
done

if [[ -z "$FUZZER_NAME" ]]; then
    echo "Error: Fuzzer name required"
    usage
fi

# Determine corpus directory
case "$FUZZER_NAME" in
    bytecode_fuzzer) CORPUS_SUBDIR="bytecode" ;;
    dsl_fuzzer) CORPUS_SUBDIR="dsl" ;;
    asm_fuzzer) CORPUS_SUBDIR="asm" ;;
    execute_fuzzer) CORPUS_SUBDIR="execute" ;;
    capi_fuzzer) CORPUS_SUBDIR="bytecode" ;;
    *)
        echo "Error: Unknown fuzzer '$FUZZER_NAME'"
        usage
        ;;
esac

# Paths
FUZZER_PATH="$BUILD_DIR/fuzz/$FUZZER_NAME"
CORPUS_PATH="$REPO_ROOT/fuzz/corpus/$CORPUS_SUBDIR"
OUTPUT_DIR="$COVERAGE_DIR/$FUZZER_NAME"
PROFILE_RAW="$OUTPUT_DIR/fuzz.profraw"
PROFILE_DATA="$OUTPUT_DIR/fuzz.profdata"

# Tools
LLVM_PROFDATA="${LLVM_PROFDATA:-llvm-profdata}"
LLVM_COV="${LLVM_COV:-llvm-cov}"

# Check dependencies
check_tool() {
    if ! command -v "$1" &>/dev/null; then
        echo "Error: $1 not found. Install LLVM tools or set $2 environment variable."
        exit 1
    fi
}

check_tool "$LLVM_PROFDATA" "LLVM_PROFDATA"
check_tool "$LLVM_COV" "LLVM_COV"

# Check fuzzer
if [[ ! -x "$FUZZER_PATH" ]]; then
    echo "Error: Fuzzer not found at $FUZZER_PATH"
    echo "Build with: cmake --preset ci-clang-fuzz && cmake --build --preset ci-clang-fuzz"
    exit 1
fi

# Check corpus
if [[ ! -d "$CORPUS_PATH" ]]; then
    echo "Error: Corpus not found at $CORPUS_PATH"
    echo "Generate with: ./fuzz/scripts/generate_corpus.sh"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo "=== Generating coverage for $FUZZER_NAME ==="
echo "  Fuzzer: $FUZZER_PATH"
echo "  Corpus: $CORPUS_PATH"
echo "  Output: $OUTPUT_DIR"
echo ""

# Step 1: Run fuzzer against corpus with profiling
echo "Step 1: Running fuzzer against corpus..."
LLVM_PROFILE_FILE="$PROFILE_RAW" "$FUZZER_PATH" "$CORPUS_PATH" -runs="$CORPUS_RUNS" 2>&1 | tail -20

# Step 2: Merge profile data
echo ""
echo "Step 2: Merging profile data..."
"$LLVM_PROFDATA" merge -sparse "$PROFILE_RAW" -o "$PROFILE_DATA"

# Step 3: Generate summary report
echo ""
echo "Step 3: Generating coverage summary..."
"$LLVM_COV" report "$FUZZER_PATH" -instr-profile="$PROFILE_DATA" | tee "$OUTPUT_DIR/summary.txt"

# Step 4: Generate detailed reports if requested
if [[ "$GENERATE_HTML" == "true" ]]; then
    echo ""
    echo "Step 4a: Generating HTML report..."
    HTML_DIR="$OUTPUT_DIR/html"
    "$LLVM_COV" show "$FUZZER_PATH" \
        -instr-profile="$PROFILE_DATA" \
        -format=html \
        -output-dir="$HTML_DIR" \
        -show-line-counts \
        -show-regions
    echo "  HTML report: $HTML_DIR/index.html"
fi

if [[ "$GENERATE_JSON" == "true" ]]; then
    echo ""
    echo "Step 4b: Generating JSON report..."
    "$LLVM_COV" export "$FUZZER_PATH" \
        -instr-profile="$PROFILE_DATA" \
        -format=lcov > "$OUTPUT_DIR/coverage.lcov"
    echo "  LCOV report: $OUTPUT_DIR/coverage.lcov"
fi

echo ""
echo "=== Coverage generation complete ==="
echo "  Summary: $OUTPUT_DIR/summary.txt"
if [[ "$GENERATE_HTML" == "true" ]]; then
    echo "  HTML:    $OUTPUT_DIR/html/index.html"
fi
if [[ "$GENERATE_JSON" == "true" ]]; then
    echo "  LCOV:    $OUTPUT_DIR/coverage.lcov"
fi
