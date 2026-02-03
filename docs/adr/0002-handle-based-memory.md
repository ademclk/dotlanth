# ADR-0002: Handle-Based Memory Management

## Status

Accepted

## Context

DotVM needs a memory management strategy that provides:
- Protection against use-after-free vulnerabilities
- Prevention of double-free bugs
- Bounds checking on all memory accesses
- No garbage collection pauses
- Deterministic resource cleanup
- Memory safety without runtime overhead of full GC

The VM executes potentially untrusted code, so memory safety is a security requirement, not just a convenience.

## Decision

Implement a handle-based memory allocation system with generation counters.

Instead of exposing raw pointers, the memory manager returns `Handle` values:

```cpp
struct Handle {
    uint32_t index;       // Slot in handle table
    uint32_t generation;  // Version counter
};
```

The handle table tracks allocations:

```cpp
struct HandleEntry {
    void* ptr;            // Actual memory pointer
    size_t size;          // Allocation size
    uint32_t generation;  // Current generation
    bool is_active;       // Whether slot is in use
};
```

**Key mechanism**: When memory is deallocated, the generation counter increments. Any handle holding the old generation becomes invalid. Subsequent access attempts fail validation.

```cpp
// Allocation: returns Handle{index=5, generation=1}
auto h = mm.allocate(1024);

// Deallocation: increments generation to 2
mm.deallocate(h);

// Later access with stale handle fails
mm.read<int>(h, 0);  // Error: generation mismatch (1 != 2)
```

All memory operations are bounds-checked against the allocation size.

## Consequences

### Positive

- **Use-after-free detection**: Stale handles are detected at runtime via generation mismatch
- **Double-free prevention**: Second deallocation fails because handle is already invalid
- **Bounds checking**: Every read/write validates offset against allocation size
- **No GC pauses**: Deterministic deallocation, no stop-the-world collection
- **Debuggable**: Clear error messages identify which handle is invalid
- **NaN-box compatible**: Handle fits in 48-bit payload (32-bit index + 16-bit generation)

### Negative

- **Indirection overhead**: Every memory access requires handle table lookup
- **Generation wraparound**: After 2^32 deallocations of the same slot, generation wraps (extremely rare)
- **Memory overhead**: Handle table consumes memory proportional to peak allocation count
- **Not thread-safe by default**: MemoryManager is single-threaded; concurrent access requires external synchronization

### Neutral

- Page-aligned allocations (4KB granularity) trade memory for simpler bounds checking
- Slot reuse via free list maintains reasonable table size
- Generation wraparound is tracked in security statistics for auditing

## Alternatives Considered

### Alternative 1: Raw Pointers with Manual Tracking

Expose raw pointers and trust the bytecode to manage memory correctly.

**Rejected because:**
- No protection against use-after-free
- No bounds checking
- Unsuitable for untrusted code execution

### Alternative 2: Garbage Collection

Implement a tracing or reference-counting garbage collector.

**Rejected because:**
- GC pauses are unacceptable for real-time/deterministic workloads
- Complexity of implementing a correct GC
- Reference counting has overhead on every pointer operation
- Tracing GC requires write barriers

### Alternative 3: Arena Allocation

Allocate from arenas that are freed in bulk.

**Rejected because:**
- Doesn't support individual deallocation
- Memory stays allocated until arena is freed
- Not suitable for long-running programs with dynamic allocation patterns

### Alternative 4: Rust-style Ownership

Track ownership at compile time.

**Rejected because:**
- Requires a sophisticated type system in the bytecode verifier
- Significantly increases complexity of bytecode format
- Would require bytecode-level borrow checking

## References

- [Code: include/dotvm/core/memory.hpp](../../include/dotvm/core/memory.hpp)
- [Code: include/dotvm/core/value.hpp](../../include/dotvm/core/value.hpp) (Handle stored in Value)
- [Generation counters in game engines](https://floooh.github.io/2018/06/17/handles-vs-pointers.html)
- [Entity-Component-System handle patterns](https://skypjack.github.io/2019-05-06-ecs-baf-part-3/)
