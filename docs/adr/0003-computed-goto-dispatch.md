# ADR-0003: Computed-Goto Instruction Dispatch

## Status

Accepted

## Context

The instruction dispatch loop is the hottest path in any bytecode interpreter. DotVM targets <10 cycles per dispatch to achieve competitive performance with JIT-compiled code for short-running programs.

The dispatch mechanism must:
- Minimize branch mispredictions
- Avoid indirect call overhead
- Enable compiler optimizations
- Work with 256 possible opcodes

## Decision

Use computed-goto (labels-as-values) for instruction dispatch on GCC/Clang, with automatic fallback to switch-based dispatch on other compilers.

```cpp
// Dispatch table: array of label addresses
static void* dispatch_table[256] = {
    &&op_ADD, &&op_SUB, &&op_MUL, ...
};

// Dispatch macro
#define DOTVM_DISPATCH(opcode) goto *dispatch_table[(opcode)]

// Handler structure
op_ADD: {
    auto d = decode_type_a(instr);
    regs.write(d.rd, alu.add(regs.read(d.rs1), regs.read(d.rs2)));
    DOTVM_NEXT();  // Fetch next instruction and dispatch
}
```

The `DOTVM_NEXT()` macro performs bounds checking, fetches the next instruction, and jumps directly to the handler.

Fallback for non-GCC/Clang compilers:
```cpp
// Switch-based dispatch
switch (opcode) {
    case 0x00: /* ADD */ ...
    case 0x01: /* SUB */ ...
}
```

## Consequences

### Positive

- **~2x faster than switch**: Eliminates switch jump table overhead; each handler ends with direct jump to next
- **Better branch prediction**: Each handler has its own indirect branch, allowing CPU to learn per-opcode patterns
- **Inlining opportunities**: Handlers are inline code, not separate functions
- **Measured performance**: ~5-10 cycles per dispatch vs ~15-20 for switch

### Negative

- **Non-standard C++**: Computed-goto is a GCC/Clang extension, not ISO C++
- **Portability**: Fallback path (switch) is slower on MSVC
- **Debugging difficulty**: gdb/lldb can have trouble with label addresses
- **Code size**: Each handler duplicates the dispatch epilogue

### Neutral

- Fallback mechanism ensures code compiles everywhere, just slower on non-GCC/Clang
- Macro-based abstraction hides dispatch details from handler code
- `-Wno-pedantic` required for the dispatch file to suppress extension warning

## Alternatives Considered

### Alternative 1: Switch Statement

Standard switch-based dispatch with compiler optimization.

**Rejected as primary because:**
- Single indirect branch for all opcodes (poor branch prediction)
- Range check overhead before jump table
- ~2x slower in benchmarks

**Retained as fallback** for MSVC and other compilers.

### Alternative 2: Function Pointer Table

Array of function pointers, one per opcode.

```cpp
using Handler = void(*)(VM&, uint32_t);
Handler handlers[256] = { &add_handler, &sub_handler, ... };
```

**Rejected because:**
- Call/return overhead per instruction
- Prevents inlining
- Stack frame setup costs

### Alternative 3: Tail Call Dispatch

Each handler tail-calls the next via function pointer.

**Rejected because:**
- C++ doesn't guarantee tail call elimination
- `[[clang::musttail]]` is non-portable
- Falls back to regular calls on most compilers

### Alternative 4: Threaded Code (Direct Threading)

Store handler addresses directly in bytecode instead of opcodes.

**Rejected because:**
- Requires bytecode patching at load time
- Complicates bytecode serialization
- Address space layout randomization (ASLR) issues

## References

- [Code: include/dotvm/exec/dispatch_macros.hpp](../../include/dotvm/exec/dispatch_macros.hpp)
- [GCC Labels as Values](https://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html)
- [The Structure and Performance of Efficient Interpreters](https://www.jilp.org/vol5/v5paper12.pdf)
- [LuaJIT interpreter design](https://web.archive.org/web/20220525170225/http://lua-users.org/lists/lua-l/2010-03/msg00305.html)
