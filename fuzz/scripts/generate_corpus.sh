#!/bin/bash
# TOOL-011: Seed corpus generator for fuzz targets
#
# Generates binary bytecode files for the fuzzer corpus.
# Run from the repository root: ./fuzz/scripts/generate_corpus.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORPUS_DIR="$SCRIPT_DIR/../corpus"

# Create corpus directories if they don't exist
mkdir -p "$CORPUS_DIR"/{bytecode,dsl,asm,execute}

echo "=== Generating bytecode corpus ==="

# Helper function to write little-endian bytes
write_le16() {
    local val=$1
    printf '\\x%02x\\x%02x' $((val & 0xff)) $(((val >> 8) & 0xff))
}

write_le32() {
    local val=$1
    printf '\\x%02x\\x%02x\\x%02x\\x%02x' \
        $((val & 0xff)) $(((val >> 8) & 0xff)) \
        $(((val >> 16) & 0xff)) $(((val >> 24) & 0xff))
}

write_le64() {
    local val=$1
    printf '\\x%02x\\x%02x\\x%02x\\x%02x\\x%02x\\x%02x\\x%02x\\x%02x' \
        $((val & 0xff)) $(((val >> 8) & 0xff)) \
        $(((val >> 16) & 0xff)) $(((val >> 24) & 0xff)) \
        $(((val >> 32) & 0xff)) $(((val >> 40) & 0xff)) \
        $(((val >> 48) & 0xff)) $(((val >> 56) & 0xff))
}

# Generate minimal bytecode with HALT
# Header: 48 bytes + Code: 4 bytes (HALT instruction)
generate_minimal_halt() {
    local outfile="$1"
    {
        # Magic: "DOTM" (4 bytes)
        printf 'DOTM'
        # Version: 26 (1 byte)
        printf '\\x1a'
        # Architecture: Arch64 = 1 (1 byte)
        printf '\\x01'
        # Flags: 0 (2 bytes)
        printf '\\x00\\x00'
        # Entry point: 0 (8 bytes)
        printf '\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Const pool offset: 48 (8 bytes) - right after header
        printf '\\x30\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Const pool size: 0 (8 bytes)
        printf '\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Code offset: 48 (8 bytes) - right after header
        printf '\\x30\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Code size: 4 (8 bytes) - one instruction
        printf '\\x04\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Code: HALT instruction (0x5F 00 00 00)
        printf '\\x5f\\x00\\x00\\x00'
    } > "$outfile"
    echo "  Created: $outfile"
}

# Generate bytecode with arithmetic: MOVI R0, 42; MOVI R1, 10; ADD R2, R0, R1; HALT
generate_arithmetic() {
    local outfile="$1"
    {
        # Magic: "DOTM"
        printf 'DOTM'
        # Version: 26
        printf '\\x1a'
        # Architecture: Arch64 = 1
        printf '\\x01'
        # Flags: 0
        printf '\\x00\\x00'
        # Entry point: 0
        printf '\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Const pool offset: 48
        printf '\\x30\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Const pool size: 0
        printf '\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Code offset: 48
        printf '\\x30\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # Code size: 16 (4 instructions * 4 bytes)
        printf '\\x10\\x00\\x00\\x00\\x00\\x00\\x00\\x00'
        # MOVI R0, 42 (0x11 = MOVI, Rd=0, imm16=42)
        printf '\\x11\\x00\\x2a\\x00'
        # MOVI R1, 10
        printf '\\x11\\x01\\x0a\\x00'
        # ADD R2, R0, R1 (0x02 = ADD, Rd=2, Rs1=0, Rs2=1)
        printf '\\x02\\x02\\x00\\x01'
        # HALT
        printf '\\x5f\\x00\\x00\\x00'
    } > "$outfile"
    echo "  Created: $outfile"
}

# Generate for bytecode corpus
generate_minimal_halt "$CORPUS_DIR/bytecode/minimal_halt.bin"
generate_arithmetic "$CORPUS_DIR/bytecode/arithmetic.bin"

# Copy to execute corpus (execute fuzzer uses same format)
cp "$CORPUS_DIR/bytecode/minimal_halt.bin" "$CORPUS_DIR/execute/halt_only.bin"
cp "$CORPUS_DIR/bytecode/arithmetic.bin" "$CORPUS_DIR/execute/arithmetic.bin"

echo ""
echo "=== Corpus generation complete ==="
echo "  bytecode/: $(ls -1 "$CORPUS_DIR/bytecode" | wc -l) files"
echo "  dsl/:      $(ls -1 "$CORPUS_DIR/dsl" | wc -l) files"
echo "  asm/:      $(ls -1 "$CORPUS_DIR/asm" | wc -l) files"
echo "  execute/:  $(ls -1 "$CORPUS_DIR/execute" | wc -l) files"
