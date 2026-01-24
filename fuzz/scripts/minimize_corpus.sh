#!/bin/bash
# TOOL-011: Corpus minimization script
#
# Minimizes the fuzzer corpus to remove redundant inputs while maintaining
# the same code coverage.
#
# Usage: ./fuzz/scripts/minimize_corpus.sh <fuzzer_name>

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CORPUS_DIR="$SCRIPT_DIR/../corpus"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/ci-clang-fuzz}"

usage() {
    cat <<EOF
Usage: $(basename "$0") <fuzzer_name>

Minimizes the corpus for the specified fuzzer, removing inputs that
don't contribute unique coverage.

Fuzzers:
  bytecode_fuzzer  - Test bytecode header parsing
  dsl_fuzzer       - Test DSL parser
  asm_fuzzer       - Test assembly parser
  execute_fuzzer   - Test execution engine
  capi_fuzzer      - Test C API

Environment:
  BUILD_DIR        Build directory (default: build/ci-clang-fuzz)
EOF
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

FUZZER_NAME="$1"

# Determine corpus directory based on fuzzer name
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

FUZZER_PATH="$BUILD_DIR/fuzz/$FUZZER_NAME"
CORPUS_PATH="$CORPUS_DIR/$CORPUS_SUBDIR"
MINIMIZED_PATH="${CORPUS_PATH}_minimized"

# Check if fuzzer exists
if [[ ! -x "$FUZZER_PATH" ]]; then
    echo "Error: Fuzzer not found at $FUZZER_PATH"
    echo "Build with: cmake --preset ci-clang-fuzz && cmake --build --preset ci-clang-fuzz"
    exit 1
fi

echo "=== Minimizing corpus for $FUZZER_NAME ==="
echo "  Input corpus:  $CORPUS_PATH ($(ls -1 "$CORPUS_PATH" 2>/dev/null | wc -l) files)"
echo "  Output corpus: $MINIMIZED_PATH"
echo ""

# Create minimized directory
rm -rf "$MINIMIZED_PATH"
mkdir -p "$MINIMIZED_PATH"

# Run minimization
# -merge=1 tells libFuzzer to merge corpus into a minimal set
"$FUZZER_PATH" -merge=1 "$MINIMIZED_PATH" "$CORPUS_PATH"

echo ""
echo "=== Minimization complete ==="
echo "  Original: $(ls -1 "$CORPUS_PATH" 2>/dev/null | wc -l) files"
echo "  Minimized: $(ls -1 "$MINIMIZED_PATH" | wc -l) files"

# Prompt to replace original
read -p "Replace original corpus with minimized? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    # Backup original
    BACKUP_PATH="${CORPUS_PATH}_backup_$(date +%Y%m%d_%H%M%S)"
    mv "$CORPUS_PATH" "$BACKUP_PATH"
    mv "$MINIMIZED_PATH" "$CORPUS_PATH"
    echo "Done. Original backed up to: $BACKUP_PATH"
else
    echo "Minimized corpus left at: $MINIMIZED_PATH"
fi
