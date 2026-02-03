# ADR-0006: std::expected for Error Handling

## Status

Accepted

## Context

DotVM operations can fail in predictable ways:
- Memory allocation failures
- Invalid handle access
- Bounds violations
- Bytecode validation errors

Error handling must be:
- Explicit (callers must handle errors)
- Efficient (no exceptions in hot paths)
- Composable (errors can propagate through call chains)
- Type-safe (error types are part of the API)

The codebase targets C++26, which includes `std::expected` from C++23.

## Decision

Use `std::expected<T, E>` for operations that can fail, where `T` is the success type and `E` is the error type.

```cpp
template<typename T>
using Result = std::expected<T, MemoryError>;

// Example usage
Result<Handle> allocate(size_t size) noexcept;
Result<Value> read(Handle h, size_t offset) const noexcept;
```

Error handling at call sites:

```cpp
auto result = memory.allocate(4096);
if (!result) {
    log_error(result.error());
    return std::unexpected{result.error()};
}
Handle h = *result;
```

Monadic operations (C++23):

```cpp
auto value = memory.allocate(size)
    .and_then([&](Handle h) { return memory.read<int>(h, 0); })
    .transform([](int v) { return v * 2; })
    .value_or(-1);
```

## Consequences

### Positive

- **Explicit error handling**: Return type makes failure possibility visible; can't ignore errors
- **No exceptions**: Zero overhead when errors don't occur; predictable performance
- **Type safety**: Error type is part of the signature; compiler catches mismatches
- **Composable**: `and_then`, `transform`, `or_else` enable functional error handling
- **Monadic operations**: C++23 adds `.and_then()`, `.transform()`, `.or_else()` for chaining
- **Value semantics**: No heap allocation; `expected` is typically 1-2 words larger than `T`

### Negative

- **C++23 required**: `std::expected` is C++23; older codebases need a polyfill
- **Verbose call sites**: Every fallible call needs error checking (but this is intentional)
- **Error type proliferation**: Each domain may have its own error enum

### Neutral

- Migration from exceptions requires touching all error paths
- Some operations naturally have "success or error" semantics; others are trickier
- Error codes require good naming and documentation

## Alternatives Considered

### Alternative 1: Exceptions

```cpp
Handle allocate(size_t size);  // throws std::bad_alloc
```

**Rejected because:**
- Unpredictable performance (exception unwinding is expensive)
- Hidden control flow (any function might throw)
- Not suitable for embedded/real-time contexts
- Difficult to reason about exception safety

### Alternative 2: Error Codes (C-style)

```cpp
MemoryError allocate(size_t size, Handle* out);
```

**Rejected because:**
- Easy to ignore return value
- Output parameters are awkward
- No type safety (caller might forget to check)
- Doesn't compose well

### Alternative 3: std::optional<T>

```cpp
std::optional<Handle> allocate(size_t size);
```

**Rejected because:**
- Loses error information (only know "it failed", not why)
- Insufficient for operations with multiple failure modes
- Callers can't distinguish different error conditions

### Alternative 4: Custom Result Type

```cpp
template<typename T, typename E>
class Result { /* ... */ };
```

**Rejected because:**
- Reinventing the wheel; `std::expected` is standardized
- Third-party Result types lack standard library integration
- Different projects use incompatible Result types

### Alternative 5: Rust-style Result with Pattern Matching

Would require language extensions for proper `match` syntax.

**Rejected because:**
- C++ doesn't have pattern matching (yet)
- `std::expected` monadic operations provide similar composition

## References

- [Code: include/dotvm/core/memory.hpp](../../include/dotvm/core/memory.hpp) (Result typedef)
- [std::expected reference](https://en.cppreference.com/w/cpp/utility/expected)
- [P0323R12: std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0323r12.html)
- [Sy Brand: Functional Error Handling](https://blog.tartanllama.xyz/expected/)
