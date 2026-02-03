# ADR-0001: NaN-Boxing for Value Representation

## Status

Accepted

## Context

DotVM requires a unified value type that can efficiently represent multiple data types (floats, integers, booleans, pointers, handles) in a single 64-bit word. The value type is used extensively throughout the VM—in registers, on the stack, and in memory operations. Performance is critical.

Key requirements:
- Store multiple types in a single 64-bit value
- Zero overhead for floating-point operations (the common case)
- Fast type checking (1-2 cycles)
- No heap allocation for type discrimination
- Atomic operations for concurrent access

## Decision

Use IEEE 754 NaN-boxing to encode multiple value types within a 64-bit word.

NaN-boxing exploits the fact that IEEE 754 doubles have many unused bit patterns in the "quiet NaN" (QNaN) space. A QNaN has:
- All 11 exponent bits set (0x7FF)
- The quiet bit (bit 51) set
- Remaining 51 bits can be arbitrary

We use these 51 bits to store tagged values:

```
Bits 63-52: QNaN prefix (0x7FF8)
Bits 51-48: Type tag
Bits 47-0:  Payload
```

| Type    | Tag  | Payload                         |
|---------|------|---------------------------------|
| Float   | none | Standard IEEE 754 double        |
| Integer | 0x1  | Sign-extended 48-bit integer    |
| Bool    | 0x2  | 0 (false) or 1 (true)           |
| Handle  | 0x3  | 32-bit index + 16-bit generation|
| Nil     | 0x4  | (unused)                        |
| Pointer | 0x5  | 48-bit canonical address        |

## Consequences

### Positive

- **Zero-cost floats**: Regular floating-point values pass through unchanged; no encoding/decoding overhead
- **Compact representation**: All values fit in exactly 8 bytes, enabling efficient register files and stack operations
- **Lock-free atomics**: 64-bit values are naturally atomic on modern x86-64 processors
- **Cache-friendly**: Dense value arrays with no pointer chasing
- **Fast type checks**: Single comparison with prefix mask (1-2 cycles)

### Negative

- **48-bit integer limit**: Integers are limited to 48-bit signed range (±140 trillion). Larger values require BigInt
- **16-bit handle generation**: Generation counters truncated to 16 bits when stored in Values, limiting wraparound detection
- **NaN canonicalization**: Actual NaN values from floating-point operations must be canonicalized to avoid type confusion
- **Platform-specific**: Relies on IEEE 754 semantics; may not work on exotic platforms

### Neutral

- 48-bit pointers are sufficient for canonical x86-64 addresses (current hardware only uses 48 bits)
- Slightly more complex serialization than naive tagged unions

## Alternatives Considered

### Alternative 1: Tagged Union (struct with enum)

```cpp
struct Value {
    enum Type { Float, Int, Bool, ... } type;
    union { double f; int64_t i; bool b; } data;
};
```

**Rejected because:**
- 16 bytes per value (wastes memory and cache)
- Extra memory access for type check
- Not naturally atomic

### Alternative 2: Pointer Tagging

Store type tag in low bits of pointers (assuming alignment).

**Rejected because:**
- Only works for heap-allocated values
- Requires heap allocation for every value
- Poor performance for numeric operations

### Alternative 3: SMI (Small Integer) + Heap Pointers

V8-style approach: integers in tagged immediate form, objects on heap.

**Rejected because:**
- Heap allocation overhead for non-integer values
- GC pressure
- More complex value handling

## References

- [Code: include/dotvm/core/value.hpp](../../include/dotvm/core/value.hpp)
- [Mozilla SpiderMonkey NaN-boxing](https://pernos.co/blog/nan-boxing/)
- [LuaJIT NaN-boxing implementation](https://github.com/LuaJIT/LuaJIT/blob/v2.1/src/lj_obj.h)
- [IEEE 754-2019 Standard](https://standards.ieee.org/ieee/754/6210/)
