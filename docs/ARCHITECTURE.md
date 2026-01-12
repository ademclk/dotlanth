# DotVM Architecture

This document describes the internal architecture of DotVM, a high-performance virtual machine designed for the DotLanth platform.

## Overview

DotVM is a register-based virtual machine with:
- 256 general-purpose registers (64-bit)
- NaN-boxed value representation
- Handle-based memory management with generation counters
- Computed-goto instruction dispatch
- Dual architecture support (32-bit and 64-bit modes)

## Value Representation (NaN-Boxing)

DotVM uses NaN-boxing to efficiently encode multiple value types in a single 64-bit word. This technique exploits the IEEE 754 floating-point NaN (Not-a-Number) representation.

### How NaN-Boxing Works

IEEE 754 double-precision floats use 64 bits:
- 1 sign bit
- 11 exponent bits
- 52 mantissa bits

A quiet NaN (QNaN) has:
- All exponent bits set (0x7FF)
- Bit 51 set (quiet flag)
- Remaining bits can be arbitrary

This gives us ~51 bits of "payload" space within NaN representations.

### Type Tags

```
Bits 63-52: QNaN prefix (0x7FF8)
Bits 51-48: Type tag
Bits 47-0:  Payload
```

| Type    | Tag (bits 51-48) | Payload (bits 47-0)              |
|---------|------------------|----------------------------------|
| Float   | (no tag)         | Standard IEEE 754 double         |
| Integer | 0x1              | Sign-extended 48-bit integer     |
| Bool    | 0x2              | 0 (false) or 1 (true)            |
| Handle  | 0x3              | 32-bit index + 16-bit generation |
| Nil     | 0x4              | (unused)                         |
| Pointer | 0x5              | 48-bit address                   |

### Type Detection

```cpp
bool is_float(uint64_t bits) {
    // Not a NaN, OR is canonical NaN without type tags
    return (bits & 0x7FF8000000000000) != 0x7FF8000000000000
        || (bits & 0x0007000000000000) == 0;
}

bool is_integer(uint64_t bits) {
    return (bits & 0x7FFF000000000000) == 0x7FF9000000000000;
}
```

### Performance Implications

- Float operations: Zero overhead (native representation)
- Integer operations: 1-2 cycles (mask and shift)
- Type checks: 1-2 cycles (compare with prefix)

## Register File

### Configuration

```
Architecture   | Register Count | Value Width
---------------|----------------|-------------
Arch32         | 256            | 32-bit masked
Arch64         | 256            | 64-bit native
```

### Register Conventions

| Register | Purpose              |
|----------|----------------------|
| R0       | Zero register (reads always return 0) |
| R1-R7    | Function arguments/return values |
| R8-R15   | Caller-saved temporaries |
| R16-R31  | Callee-saved registers |
| R32-R255 | General purpose |

### Implementation

The register file uses a flat `std::array<Value, 256>` with:
- Bounds checking in debug builds
- Architecture-aware masking for Arch32 mode

## Memory Model

### Handle-Based Allocation

Instead of raw pointers, DotVM uses handles for memory references:

```cpp
struct Handle {
    uint32_t index;       // Slot in handle table
    uint32_t generation;  // Version counter
};
```

**Benefits:**
1. Use-after-free detection via generation mismatch
2. Double-free prevention
3. Memory safety without garbage collection overhead

### Handle Table

```
┌─────────────────────────────────────────┐
│ HandleTable                             │
├─────────────────────────────────────────┤
│ entries_: vector<HandleEntry>           │
│   ├─ ptr: void*       (allocated memory)│
│   ├─ size: size_t     (allocation size) │
│   ├─ generation: u32  (version counter) │
│   └─ is_active: bool  (in use flag)     │
├─────────────────────────────────────────┤
│ free_list_: vector<uint32_t>            │
│   (indices of reusable slots)           │
└─────────────────────────────────────────┘
```

### Memory Operations

```cpp
// Allocation
Result<Handle> allocate(size_t size) noexcept;

// Typed read/write (bounds-checked)
Result<T> read<T>(Handle h, size_t offset) const noexcept;
MemoryError write<T>(Handle h, size_t offset, T value) noexcept;

// Bulk operations
MemoryError read_into(Handle h, size_t offset, span<T> dst) const noexcept;
MemoryError write_from(Handle h, size_t offset, span<const T> src) noexcept;
```

### Page Alignment

All allocations are rounded up to 4KB page boundaries:
- Reduces external fragmentation
- Enables future memory protection features
- Simplifies bounds checking

## Instruction Set Architecture

### Instruction Encoding

All instructions are 32 bits wide:

**Type A (Register-Register):**
```
[31:24] opcode  [23:16] Rd  [15:8] Rs1  [7:0] Rs2
```

**Type B (Register-Immediate):**
```
[31:24] opcode  [23:16] Rd  [15:0] imm16
```

**Type C (Jump/Branch):**
```
[31:24] opcode  [23:0] offset24 (sign-extended)
```

### Opcode Map

| Range       | Category     | Description              |
|-------------|--------------|--------------------------|
| 0x00-0x1F   | Arithmetic   | ADD, SUB, MUL, DIV, etc. |
| 0x20-0x2F   | Bitwise      | AND, OR, XOR, shifts     |
| 0x30-0x3F   | Comparison   | EQ, NE, LT, GT, etc.     |
| 0x40-0x5F   | Control Flow | JMP, CALL, RET, branches |
| 0x60-0x7F   | Memory       | LOAD, STORE, ALLOC, FREE |
| 0x80-0x8F   | Data Move    | MOV, MOVI, etc.          |
| 0xA0-0xAF   | State        | FLAGS, STATUS ops        |
| 0xB0-0xBF   | Crypto       | SHA256, AES ops          |
| 0xC0-0xCF   | ParaDot      | Parallel operations      |
| 0xF0-0xFF   | System       | NOP, HALT, SYSCALL       |

## Execution Engine

### Computed-Goto Dispatch

For maximum performance, DotVM uses computed-goto dispatch on GCC/Clang:

```cpp
// Dispatch table (256 label addresses)
static void* dispatch_table[256] = {
    &&op_ADD, &&op_SUB, &&op_MUL, ...
};

// Dispatch loop
DOTVM_NEXT();  // Jump to dispatch_table[opcode]

op_ADD: {
    auto d = decode_type_a(instr);
    regs.write(d.rd, alu.add(regs.read(d.rs1), regs.read(d.rs2)));
    DOTVM_NEXT();
}
```

**Performance:** ~5-10 cycles per instruction dispatch (vs ~15-20 for switch)

### Execution Context

```cpp
struct ExecutionContext {
    const uint32_t* code;        // Bytecode array
    size_t code_size;            // Number of instructions
    size_t pc;                   // Program counter
    size_t instructions_executed;
    size_t instruction_limit;    // DoS protection
    ExecState state;             // Running/Halted/Error
    ExecResult result;           // Success/Error code
};
```

## Component Interfaces (C++20 Concepts)

DotVM uses C++20 concepts for zero-overhead polymorphism:

```cpp
template<typename T>
concept RegisterFileInterface = requires(T& rf, RegIdx idx, Value val) {
    { rf.read(idx) } -> std::same_as<Value>;
    { rf.write(idx, val) } -> std::same_as<void>;
    { rf.size() } -> std::convertible_to<std::size_t>;
};

template<typename T>
concept AluInterface = requires(T& alu, Value a, Value b) {
    { alu.add(a, b) } -> std::same_as<Value>;
    { alu.sub(a, b) } -> std::same_as<Value>;
    // ... other operations
};
```

**Benefits:**
- No vtable overhead
- Full inlining capability
- Compile-time interface verification
- Easy mocking for tests

## Error Handling

DotVM uses C++23 `std::expected` for error handling:

```cpp
template<typename T>
using Result = std::expected<T, MemoryError>;

// Usage
auto result = memory.allocate(4096);
if (!result) {
    handle_error(result.error());
    return;
}
Handle h = *result;
```

**Error Types:**
- `MemoryError`: Allocation, bounds, handle validity
- `ExecResult`: Execution outcomes (success, halt, error)
- `BytecodeError`: Parsing and validation errors

## Dual Architecture Support

DotVM supports both 32-bit and 64-bit execution modes:

### Arch32 Mode
- 32-bit address space (4GB max)
- Integer operations wrap at 32 bits
- Useful for embedded/constrained environments

### Arch64 Mode
- 48-bit address space (canonical x86-64)
- Full 64-bit integer operations
- Default for modern systems

### Architecture Masking

```cpp
constexpr uint64_t mask_value(uint64_t val, Architecture arch) {
    return (arch == Architecture::Arch32)
        ? val & 0xFFFFFFFF
        : val;
}
```

## SIMD Support

DotVM includes optional SIMD operations via the `VectorRegisterFile`:

| Width | Types             | Backend        |
|-------|-------------------|----------------|
| 128   | i32x4, f32x4      | SSE/NEON       |
| 256   | i32x8, f32x8      | AVX2           |
| 512   | i32x16, f32x16    | AVX-512        |

SIMD dispatch uses runtime CPU feature detection with `if constexpr` for zero-overhead when features are disabled.
