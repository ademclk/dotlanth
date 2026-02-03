# C++ Safety Checklist for DotLanth

This checklist is based on best practices from LLVM, Chromium, V8, and RocksDB projects. Review this before submitting any PR that modifies C++ code.

---

## Memory Safety

### Ownership and Lifetime

- [ ] **No raw `new`/`delete`** - Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) or RAII containers
- [ ] **Clear ownership** - Every allocation has exactly one owner (or documented shared ownership with `shared_ptr`)
- [ ] **No dangling references** - References and pointers don't outlive their targets
- [ ] **No use-after-move** - Objects aren't used after being moved from (clang-tidy: `bugprone-use-after-move`)

### Pointer and Span Safety

- [ ] **No raw pointer arithmetic** - Use `std::span`, iterators, or range-based loops instead
- [ ] **Bounds checking at boundaries** - All `std::span` access validated at system boundaries (file I/O, network, user input)
- [ ] **Generation-based handles validated** - Before dereferencing any handle (VmContext, StateBackend), verify the generation counter matches

### Memory Operations

- [ ] **No uninitialized reads** - All variables initialized before use (run under MSan to verify)
- [ ] **Buffer sizes validated** - `memcpy`/`memmove`/`memset` sizes come from trusted sources or are validated
- [ ] **No overlapping memory copies** - Use `memmove` if source and destination may overlap

---

## Concurrency Safety

### Synchronization

- [ ] **All shared state protected** - Every variable accessed from multiple threads has either:
  - A mutex guard, OR
  - `std::atomic` with explicit memory ordering documented
- [ ] **No lock held during external calls** - Release locks before calling user callbacks or external code (prevents deadlock)
- [ ] **Condition variable predicates** - All `condition_variable::wait()` calls use the predicate form to handle spurious wakeups

### Lock-Free Code

- [ ] **Lock-free code reviewed** - Any lock-free algorithm reviewed by a second engineer
- [ ] **Memory ordering documented** - Every `std::atomic` operation has a comment explaining why that memory order is sufficient
- [ ] **ABA problem addressed** - Pointer-swapping algorithms use hazard pointers, epochs, or other ABA prevention

### Thread Safety Documentation

- [ ] **Thread safety documented** - Classes document their thread safety guarantees in the header
- [ ] **No `shared_ptr` aliasing bugs** - Remember: `shared_ptr` is thread-safe for the control block, NOT for the pointee

---

## Type Safety

### Conversions

- [ ] **No implicit narrowing** - Use `narrow_cast<>` or explicit range checks for conversions that may lose data
- [ ] **No signed/unsigned mixing** - Arithmetic operations don't mix signed and unsigned integers
- [ ] **No `reinterpret_cast`** - Except for well-defined cases (serialization, strict aliasing compliance)

### Enums and Switches

- [ ] **All enum switches exhaustive** - Either list all cases OR have a `default` that asserts/unreachable
- [ ] **Enum class preferred** - Use `enum class` over plain `enum` to prevent implicit conversions

### Casting

- [ ] **`static_cast` for safe casts** - Numeric conversions, upcasts
- [ ] **`dynamic_cast` for polymorphic downcasts** - With null/exception check
- [ ] **`const_cast` avoided** - Only when absolutely necessary and documented why

---

## Input Validation

### External Data (Bytecode, Network, Files)

- [ ] **Length fields validated before allocation** - Never allocate `input.read_u64()` bytes without checking against a maximum
- [ ] **Offsets validated before use** - Jump targets, array indices from input must be bounds-checked
- [ ] **Integer overflow checked** - Size calculations that combine untrusted values checked for overflow:
  ```cpp
  // Bad: count * sizeof(T) may overflow
  // Good: if (count > SIZE_MAX / sizeof(T)) return error;
  ```
- [ ] **Untrusted data never used as array index** - Always bounds-check first:
  ```cpp
  if (index >= array.size()) return error;
  auto value = array[index];  // Now safe
  ```

### String Handling

- [ ] **No format string injection** - User strings never passed as format string argument
- [ ] **Path traversal prevented** - File paths from input don't contain `..` or are sandboxed
- [ ] **Null terminators verified** - C strings from external sources have verified length

---

## Resource Limits

### Loop Termination

- [ ] **All loops have termination guarantee** - Either:
  - Bounded by constant, OR
  - Bounded by validated input, OR
  - Checked against instruction limit

### Recursion

- [ ] **Recursion bounded** - `SecurityContext::check_stack_depth()` called on recursive paths
- [ ] **Tail recursion used where possible** - Or explicit stack for deep recursion

### Allocation Limits

- [ ] **Allocations checked against memory limit** - `SecurityContext::check_memory_limit(bytes)` before large allocations
- [ ] **Allocation failures handled** - Either use `std::nothrow` and check, or catch `bad_alloc`

---

## Cryptographic Operations

### Algorithm Selection

- [ ] **No custom crypto implementations** - Use OpenSSL, libsodium, or other audited libraries
- [ ] **Approved algorithms only** - AES-256-GCM, SHA-256/SHA-3, Ed25519, X25519, BLAKE3

### Key Management

- [ ] **Keys zeroed after use** - `OPENSSL_cleanse()` or `explicit_bzero()` before deallocation
- [ ] **Cryptographic randomness** - Random generation uses `/dev/urandom`, `getrandom()`, or equivalent
- [ ] **No hardcoded keys/secrets** - Keys come from secure storage or environment

### Timing Safety

- [ ] **Constant-time comparisons** - Use `CRYPTO_memcmp()` or equivalent for secrets
- [ ] **No secret-dependent branches** - Control flow doesn't depend on secret data

---

## VM-Specific Safety

### Opcode Handlers

- [ ] **PC modification atomic** - Instruction pointer modifications complete before handler returns
- [ ] **No double-advance** - PC not modified multiple times per instruction
- [ ] **Handler doesn't continue after branch** - Branch/jump handlers return immediately after modifying PC

### State Transitions

- [ ] **State changes atomic or rolled back** - Failed operations don't leave partial state
- [ ] **Generation counters updated** - State modifications increment generation counter
- [ ] **Transaction boundaries respected** - State changes in transactions either all commit or all rollback

### Control Flow Integrity

- [ ] **Jump targets validated** - Bytecode jump targets point to valid instruction boundaries
- [ ] **CFI checks not bypassable** - Malicious bytecode cannot skip CFI validation
- [ ] **Return addresses protected** - Call stack returns validated against expected targets

### Register and Memory Access

- [ ] **Register indices validated** - Register access checks against `NUM_REGISTERS`
- [ ] **Memory access bounds-checked** - All memory operations validate address range
- [ ] **SIMD lane indices validated** - Vector lane access within valid range

---

## Error Handling

### Expected/Result Types

- [ ] **`std::expected` for recoverable errors** - Not exceptions
- [ ] **`[[nodiscard]]` on error-returning functions** - Caller must handle result
- [ ] **Error context preserved** - Error messages include sufficient context for debugging

### Assertions

- [ ] **Assertions for invariants** - Use `assert()` for conditions that should never be false
- [ ] **No side effects in assertions** - `assert(do_thing())` won't work in release builds
- [ ] **`std::unreachable()` for impossible cases** - After exhaustive switch or if chain

---

## Code Review Markers

When you've verified a section, add a comment in your PR:

```
// SAFETY: bounds checked on line 42
// SAFETY: lock held by caller (documented in function header)
// SAFETY: generation counter validated in get_handle()
```

---

## Quick Reference: Dangerous Patterns

| Pattern | Risk | Mitigation |
|---------|------|------------|
| `new T[n]` where n is from input | Allocation overflow | Validate n against maximum |
| `memcpy(dst, src, len)` with computed len | Buffer overflow | Validate len < dst capacity |
| `array[input.read_u32()]` | Out of bounds | Bounds check before access |
| `while (condition)` without limit | Infinite loop | Add iteration counter |
| `std::atomic<T>` with default ordering | Race condition | Specify memory order explicitly |
| `shared_ptr` passed to thread | Data race | Document thread-safety or copy |
| `catch (...)` | Silent failure | Log and re-throw or handle specifically |
| `reinterpret_cast<T*>` | Aliasing violation | Use `std::bit_cast` or `memcpy` |

---

*Last updated: 2024*
*Based on: LLVM Coding Standards, Chromium C++ Style Guide, V8 Security Guidelines, RocksDB Best Practices*
