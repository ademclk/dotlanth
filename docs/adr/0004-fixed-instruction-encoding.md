# ADR-0004: Fixed 32-bit Instruction Encoding

## Status

Accepted

## Context

DotVM needs an instruction encoding that balances:
- Decode simplicity (fast instruction fetch and decode)
- Code density (reasonable bytecode size)
- Sufficient operand space (256 registers, meaningful immediates)
- Extensibility (room for future opcodes)

The instruction set targets a register-based VM with 256 general-purpose registers.

## Decision

Use fixed-width 32-bit instructions with three encoding formats.

All instructions are exactly 32 bits (4 bytes), with the opcode in the high byte:

**Type A (Register-Register-Register):**
```
[31:24] opcode  [23:16] Rd  [15:8] Rs1  [7:0] Rs2
```
Used for: ADD, SUB, MUL, DIV, AND, OR, comparisons

**Type B (Register-Immediate):**
```
[31:24] opcode  [23:16] Rd  [15:0] imm16
```
Used for: MOVI, ADDI, load/store with offset

**Type C (Jump/Branch):**
```
[31:24] opcode  [23:0] offset24 (sign-extended)
```
Used for: JMP, CALL (24-bit signed offset = ±8M instructions)

**Type M (Memory):**
```
[31:24] opcode  [23:16] Rd/Rs2  [15:8] Rs1  [7:0] offset8
```
Used for: LOAD8/16/32/64, STORE8/16/32/64

## Consequences

### Positive

- **Simple decode**: Fixed-width means no variable-length parsing; opcode extraction is a single shift
- **Aligned access**: 32-bit alignment enables efficient memory access
- **Fast dispatch**: `opcode = instr >> 24` extracts opcode in one operation
- **256 registers**: Full 8-bit register indices support 256 GPRs
- **16-bit immediates**: Sufficient for most constants; larger values use LOADK

### Negative

- **Code density**: Worse than variable-length encoding (e.g., x86); small operations waste bits
- **Limited immediate range**: 16-bit signed immediate (±32K); larger constants require constant pool
- **Jump range**: 24-bit offset limits single jumps to ±8M instructions (sufficient for most programs)

### Neutral

- Opcode space (256 values) is adequate for the instruction set with room for expansion
- Memory instructions use 8-bit offset, suitable for struct field access
- Three formats cover all use cases without excessive complexity

## Alternatives Considered

### Alternative 1: Variable-Length Encoding

Use 1-4 byte instructions like x86 or WebAssembly.

**Rejected because:**
- Decode complexity increases significantly
- Branch targets must scan from start to find instruction boundaries
- Harder to implement bounds checking
- Complicates parallel decode

### Alternative 2: 16-bit Instructions

Use 16-bit instructions like Thumb or RISC-V compressed.

**Rejected because:**
- Only 4-bit register indices (16 registers) or very limited immediates
- Would require frequent instruction pairs
- Code size savings don't justify complexity

### Alternative 3: 64-bit Instructions

Use 64-bit instructions for maximum operand space.

**Rejected because:**
- Wastes memory and I-cache for simple operations
- Most instructions don't need that much space
- Halves effective code cache capacity

### Alternative 4: Tagged Instruction Formats

Use high bits to indicate format, like ARM.

**Rejected because:**
- Added decode complexity
- Fewer bits available for opcode
- 32-bit with fixed high-byte opcode is simpler

## References

- [Code: include/dotvm/core/instruction.hpp](../../include/dotvm/core/instruction.hpp)
- [RISC-V Instruction Formats](https://riscv.org/specifications/)
- [ARM A64 Instruction Set](https://developer.arm.com/documentation/ddi0596)
- Related ADR: [ADR-0003](0003-computed-goto-dispatch.md) (dispatch depends on fixed opcode position)
