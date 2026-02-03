#!/bin/bash
# Run tests under various sanitizers
#
# Usage:
#   ./scripts/run-sanitizer-tests.sh [sanitizer]
#
# Arguments:
#   sanitizer - One of: asan, tsan, msan, all (default: all)
#
# This script:
#   1. Configures a sanitizer build using Meson native files
#   2. Builds the project
#   3. Runs tests with appropriate sanitizer environment variables
#
# Example:
#   ./scripts/run-sanitizer-tests.sh asan   # Just ASan+UBSan
#   ./scripts/run-sanitizer-tests.sh all    # All sanitizers

set -euo pipefail

SANITIZER="${1:-all}"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

run_asan() {
    log_info "Running ASan+UBSan tests..."
    BUILD_DIR="$PROJECT_ROOT/build-asan"

    # Configure
    meson setup "$BUILD_DIR" \
        --native-file "$PROJECT_ROOT/cross/clang-asan.ini" \
        --buildtype=debug \
        --wipe 2>/dev/null || \
    meson setup "$BUILD_DIR" \
        --native-file "$PROJECT_ROOT/cross/clang-asan.ini" \
        --buildtype=debug

    # Build
    meson compile -C "$BUILD_DIR"

    # Test with ASan options
    export ASAN_OPTIONS="detect_leaks=1:check_initialization_order=1:strict_init_order=1:detect_stack_use_after_return=1"
    export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

    meson test -C "$BUILD_DIR" --timeout-multiplier 3

    log_info "ASan+UBSan tests passed!"
}

run_tsan() {
    log_info "Running TSan tests..."
    BUILD_DIR="$PROJECT_ROOT/build-tsan"

    # Configure (disable JIT - incompatible with TSan)
    meson setup "$BUILD_DIR" \
        --native-file "$PROJECT_ROOT/cross/clang-tsan.ini" \
        --buildtype=debug \
        -Djit=false \
        --wipe 2>/dev/null || \
    meson setup "$BUILD_DIR" \
        --native-file "$PROJECT_ROOT/cross/clang-tsan.ini" \
        --buildtype=debug \
        -Djit=false

    # Build
    meson compile -C "$BUILD_DIR"

    # Test with TSan options
    export TSAN_OPTIONS="second_deadlock_stack=1:history_size=4"

    meson test -C "$BUILD_DIR" --timeout-multiplier 5

    log_info "TSan tests passed!"
}

run_msan() {
    log_info "Running MSan tests..."
    log_warn "MSan requires libc++ built with MSan. This may not work in all environments."
    BUILD_DIR="$PROJECT_ROOT/build-msan"

    # Configure
    meson setup "$BUILD_DIR" \
        --native-file "$PROJECT_ROOT/cross/clang-msan.ini" \
        --buildtype=debug \
        --wipe 2>/dev/null || \
    meson setup "$BUILD_DIR" \
        --native-file "$PROJECT_ROOT/cross/clang-msan.ini" \
        --buildtype=debug

    # Build
    meson compile -C "$BUILD_DIR"

    # Test with MSan options
    export MSAN_OPTIONS="poison_in_dtor=1"

    meson test -C "$BUILD_DIR" --timeout-multiplier 3

    log_info "MSan tests passed!"
}

case "$SANITIZER" in
    asan)
        run_asan
        ;;
    tsan)
        run_tsan
        ;;
    msan)
        run_msan
        ;;
    all)
        run_asan
        run_tsan
        # MSan is optional - may not work in all environments
        if [[ "${RUN_MSAN:-0}" == "1" ]]; then
            run_msan
        else
            log_warn "Skipping MSan (set RUN_MSAN=1 to enable)"
        fi
        ;;
    *)
        log_error "Unknown sanitizer: $SANITIZER"
        echo "Usage: $0 [asan|tsan|msan|all]"
        exit 1
        ;;
esac

log_info "All requested sanitizer tests completed successfully!"
