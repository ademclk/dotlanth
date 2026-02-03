#!/bin/bash
# Run clang-tidy on DotLanth source files
#
# Usage:
#   ./scripts/run-clang-tidy.sh [source_dir] [build_dir]
#
# Arguments:
#   source_dir - Project source directory (default: current directory)
#   build_dir  - Build directory with compile_commands.json (default: build)
#
# Prerequisites:
#   - clang-tidy installed
#   - compile_commands.json generated (Meson creates this automatically)
#
# Example:
#   meson setup build --native-file cross/clang-libcxx.ini
#   ./scripts/run-clang-tidy.sh . build

set -euo pipefail

SOURCE_DIR="${1:-.}"
BUILD_DIR="${2:-build}"

# Ensure clang-tidy is available
if ! command -v clang-tidy &> /dev/null; then
    echo "Error: clang-tidy not found in PATH"
    exit 1
fi

# Check for compile_commands.json
COMPILE_DB="${BUILD_DIR}/compile_commands.json"
if [[ ! -f "$COMPILE_DB" ]]; then
    echo "Error: compile_commands.json not found at $COMPILE_DB"
    echo "Run 'meson setup $BUILD_DIR' first to generate it"
    exit 1
fi

# Find source files to check
SOURCES=$(find "$SOURCE_DIR/src" "$SOURCE_DIR/include" \
    -name '*.cpp' -o -name '*.hpp' \
    2>/dev/null | sort)

if [[ -z "$SOURCES" ]]; then
    echo "Error: No source files found in $SOURCE_DIR/src or $SOURCE_DIR/include"
    exit 1
fi

echo "Running clang-tidy on DotLanth sources..."
echo "Source directory: $SOURCE_DIR"
echo "Build directory: $BUILD_DIR"
echo "---"

# Count files
FILE_COUNT=$(echo "$SOURCES" | wc -l)
echo "Checking $FILE_COUNT files..."

# Run clang-tidy
# Use run-clang-tidy if available (parallel), otherwise run sequentially
if command -v run-clang-tidy &> /dev/null; then
    run-clang-tidy -p "$BUILD_DIR" -quiet "$SOURCES"
else
    ERRORS=0
    for file in $SOURCES; do
        echo "Checking: $file"
        if ! clang-tidy -p "$BUILD_DIR" --quiet "$file"; then
            ERRORS=$((ERRORS + 1))
        fi
    done

    if [[ $ERRORS -gt 0 ]]; then
        echo "---"
        echo "clang-tidy found issues in $ERRORS files"
        exit 1
    fi
fi

echo "---"
echo "clang-tidy completed successfully"
