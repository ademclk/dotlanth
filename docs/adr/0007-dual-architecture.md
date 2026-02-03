# ADR-0007: Dual Architecture Support (32/64-bit)

## Status

Accepted

## Context

DotVM needs to support execution in different architectural contexts:
- **64-bit mode**: Full capability for modern systems
- **32-bit mode**: Constrained environments, compatibility, deterministic testing

Key requirements:
- Same bytecode runs in both modes
- Integer operations respect architecture width
- Address space is bounded appropriately
- Mode is selectable at runtime (per VM instance)

## Decision

Implement dual architecture support (Arch32/Arch64) with architecture-aware masking at runtime.

```cpp
enum class Architecture : uint8_t {
    Arch32,  // 32-bit address space, 32-bit integer operations
    Arch64   // 48-bit address space (canonical x86-64), 64-bit integers
};
```

Architecture masking is applied to:
1. **Integer operations**: Results masked to 32 or 64 bits
2. **Address calculations**: Addresses wrapped to architecture limit
3. **Memory bounds**: Maximum allocation size varies by architecture

```cpp
constexpr int64_t mask_int(int64_t val, Architecture arch) {
    if (arch == Architecture::Arch32) {
        return static_cast<int32_t>(val);  // Sign-extend from 32 bits
    }
    return val;
}

constexpr uint64_t mask_addr(uint64_t addr, Architecture arch) {
    return (arch == Architecture::Arch32)
        ? (addr & 0xFFFFFFFF)      // 4GB address space
        : (addr & 0xFFFFFFFFFFFF); // 48-bit canonical
}
```

The `Value` type includes architecture-aware factory methods:

```cpp
Value::from_int(42, Architecture::Arch32);  // Masked to 32 bits
```

## Consequences

### Positive

- **Single codebase**: One implementation handles both architectures
- **Runtime selection**: Architecture can be chosen per-VM instance
- **Deterministic testing**: 32-bit mode enables reproducible testing on 64-bit hosts
- **Embedded compatibility**: 32-bit mode suitable for constrained environments
- **Consistent semantics**: Bytecode behaves identically regardless of host architecture

### Negative

- **Runtime overhead**: Masking operations on every integer result (usually 1 cycle)
- **Complexity**: All arithmetic and memory operations must consider architecture
- **Testing burden**: Must test both modes

### Neutral

- 48-bit addresses in Arch64 match x86-64 canonical address requirements
- NaN-boxed integers already limited to 48 bits, so Arch64 has no practical limit reduction
- Arch32 address limit (4GB) is sufficient for most embedded use cases

## Alternatives Considered

### Alternative 1: Compile-Time Architecture Selection

Use `#ifdef` to select architecture at build time.

```cpp
#ifdef DOTVM_32BIT
using native_int = int32_t;
#else
using native_int = int64_t;
#endif
```

**Rejected because:**
- Requires separate builds for each architecture
- Can't run both modes in the same process (e.g., for testing)
- Less flexible for cross-compilation scenarios

### Alternative 2: Template-Based Architecture

```cpp
template<Architecture Arch>
class VM { /* ... */ };
```

**Rejected because:**
- Code bloat (entire VM duplicated for each architecture)
- Can't change architecture at runtime
- Complex to instantiate and manage

### Alternative 3: Single Architecture Only

Support only 64-bit mode; drop 32-bit support.

**Rejected because:**
- Loses embedded/constrained environment support
- Can't test 32-bit overflow behavior on 64-bit hosts
- Reduces flexibility

### Alternative 4: Abstract Integer Type

Use arbitrary-precision integers everywhere, mask only at boundaries.

**Rejected because:**
- Significant performance overhead
- Over-engineered for the actual requirement
- BigInt already handles cases that exceed 48 bits

## References

- [Code: include/dotvm/core/arch_config.hpp](../../include/dotvm/core/arch_config.hpp)
- [Code: include/dotvm/core/arch_types.hpp](../../include/dotvm/core/arch_types.hpp)
- [x86-64 Canonical Addresses](https://en.wikipedia.org/wiki/X86-64#Virtual_address_space_details)
- Related ADR: [ADR-0001](0001-nan-boxing-values.md) (48-bit integer storage in NaN-boxing)
