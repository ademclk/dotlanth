# DotVM Security Model

This document describes the security architecture, threat model, and guarantees provided by DotVM.

## Design Philosophy

DotVM is designed with security as a primary concern:

1. **Defense in depth**: Multiple layers of protection
2. **Fail-safe defaults**: Errors terminate rather than continue unsafely
3. **Explicit error handling**: No silent failures
4. **Auditable**: Security events are tracked and reportable

## Threat Model

### Assumed Threats

| Threat | Description | Mitigation |
|--------|-------------|------------|
| Malicious bytecode | Crafted instructions to exploit the VM | Bounds checking, validation |
| Use-after-free | Access deallocated memory | Generation counters |
| Buffer overflow | Read/write beyond allocation | Bounds checking |
| DoS via resource exhaustion | Allocate unbounded memory | Size limits, handle table limits |
| Integer overflow | Arithmetic producing undefined behavior | Defined wrapping semantics |
| Control flow hijacking | Jump to arbitrary code | Validated PC bounds |

### Out of Scope

- Side-channel attacks (timing, cache)
- Physical attacks
- Attacks on the host runtime
- Compiler bugs

## Memory Safety

### Handle-Based Memory Management

All memory is accessed through validated handles:

```cpp
struct Handle {
    uint32_t index;       // Slot in handle table
    uint32_t generation;  // Version counter
};
```

**Validation Process:**
1. Check index bounds
2. Verify slot is active
3. Compare generation with stored generation
4. Check offset + size within allocation

### Generation Counters

Generation counters prevent use-after-free:

```
Allocation: Handle{index=5, generation=1} -> memory block A
Deallocation: slot 5 released, generation incremented to 2
Reallocation: Handle{index=5, generation=2} -> memory block B

Old handle {5,1} is now invalid (generation mismatch)
```

**Wraparound Handling:**
- Generation wraps from MAX to INITIAL (not 0)
- Wraparound events are tracked in SecurityStats
- High wraparound count may indicate attack

### Bounds Checking

All memory operations are bounds-checked:

```cpp
MemoryError validate_bounds(Handle h, size_t offset, size_t size) {
    auto err = validate_handle(h);
    if (err != MemoryError::Success) return err;

    const auto& entry = table_[h.index];

    // Check for overflow
    if (offset > entry.size || size > entry.size - offset) {
        stats_.record_bounds_violation();
        return MemoryError::BoundsViolation;
    }

    return MemoryError::Success;
}
```

## Control Flow Integrity

### PC Bounds Validation

Program counter is validated before each instruction fetch:

```cpp
if (exec_ctx_.pc >= exec_ctx_.code_size) [[unlikely]] {
    goto op_OUT_OF_BOUNDS;
}
```

### Jump Target Validation

All jump targets are validated:
- Within code bounds
- Not into middle of instruction (future)

### CFI Violation Tracking

CFI violations are recorded in SecurityStats:

```cpp
std::atomic<std::size_t> cfi_violations{0};
```

## Resource Limits

### Memory Limits

```cpp
namespace mem_config {
    // Maximum single allocation (1GB default)
    inline constexpr std::size_t MAX_ALLOCATION_SIZE = 1ULL << 30;

    // Maximum handle table entries
    inline constexpr std::uint32_t MAX_TABLE_SIZE = 1 << 20;  // 1M handles

    // Page size for alignment
    inline constexpr std::size_t PAGE_SIZE = 4096;
}
```

### Instruction Limits

Execution can be limited to prevent infinite loops:

```cpp
exec_ctx.instruction_limit = 1'000'000;  // Max 1M instructions
```

### Handle Table Exhaustion

Handle table has fixed maximum size. Exhaustion returns error (not crash):

```cpp
if (entries_.size() >= mem_config::MAX_TABLE_SIZE) {
    return mem_config::INVALID_INDEX;  // Error, not crash
}
```

## Error Handling

### std::expected Pattern

All fallible operations return `std::expected`:

```cpp
template<typename T>
using Result = std::expected<T, MemoryError>;

Result<Handle> allocate(size_t size) noexcept;
Result<T> read<T>(Handle h, size_t offset) const noexcept;
```

**Benefits:**
- Errors cannot be ignored (compile-time enforcement with `[[nodiscard]]`)
- No exceptions (predictable performance)
- Error context preserved

### Error Codes

```cpp
enum class MemoryError : uint8_t {
    Success = 0,
    InvalidSize,        // Size is 0 or exceeds limit
    AllocationFailed,   // OS allocation failed
    InvalidHandle,      // Handle not found or generation mismatch
    BoundsViolation,    // Offset + size exceeds allocation
    HandleTableFull,    // Cannot allocate more handles
    DeallocationFailed, // OS deallocation failed
    AccountingError     // Internal inconsistency detected
};
```

## Security Statistics

DotVM tracks security-relevant events:

```cpp
struct SecurityStats {
    std::atomic<std::size_t> generation_wraparounds{0};
    std::atomic<std::size_t> bounds_violations{0};
    std::atomic<std::size_t> invalid_handle_accesses{0};
    std::atomic<std::size_t> cfi_violations{0};
    std::atomic<std::size_t> allocation_limit_hits{0};
    std::atomic<std::size_t> handle_table_exhaustions{0};
    std::atomic<std::size_t> total_allocations{0};
    std::atomic<std::size_t> total_deallocations{0};
    std::atomic<std::size_t> deallocation_failures{0};
    std::atomic<std::size_t> invalid_deallocations{0};
};
```

### Event Monitoring

Register callbacks for security events:

```cpp
using SecurityEventCallback = void (*)(
    SecurityEvent event,
    const char* context,
    void* user_data
);

stats.set_event_callback(my_callback, my_data);
```

### Snapshot for Auditing

Take consistent snapshots of all counters:

```cpp
auto snapshot = stats.snapshot();
// All values read atomically
```

## Bytecode Validation

### Header Validation

Bytecode files are validated before execution:

```cpp
BytecodeError validate_header(const BytecodeHeader& header, size_t file_size) {
    // Magic number check
    if (header.magic != bytecode::MAGIC_BYTES)
        return BytecodeError::InvalidMagic;

    // Version check
    if (header.version > bytecode::CURRENT_VERSION)
        return BytecodeError::UnsupportedVersion;

    // File size limits
    if (file_size > bytecode::MAX_FILE_SIZE)
        return BytecodeError::FileTooLarge;

    // Constant pool limits
    if (header.constant_count > bytecode::MAX_CONSTANTS)
        return BytecodeError::TooManyConstants;

    return BytecodeError::Success;
}
```

### Opcode Validation

Unknown opcodes trigger defined behavior (trap to error handler), not undefined behavior.

## Integer Safety

### Defined Overflow Behavior

Integer operations have defined behavior in all cases:

- **Addition/Subtraction**: Wrap around (two's complement)
- **Multiplication**: Truncate to register width
- **Division by zero**: Returns 0, sets error flag
- **Shift out of range**: Defined per architecture

### Architecture Masking

In Arch32 mode, all results are masked to 32 bits:

```cpp
constexpr int64_t mask_int(int64_t val, Architecture arch) {
    if (arch == Architecture::Arch32) {
        return static_cast<int32_t>(val);  // Sign-extend from 32
    }
    return val;
}
```

## Secure Coding Practices

### No Raw Pointers in API

- All memory access through Handle
- Pointers only internal to MemoryManager
- Prevents dangling pointer bugs

### No Assertions for Security Checks

Security-critical checks use proper error handling, not assertions:

```cpp
// WRONG: Assertion disabled in release builds
assert(size > 0);

// RIGHT: Error return works in all builds
if (size == 0) [[unlikely]] {
    return std::unexpected{MemoryError::InvalidSize};
}
```

### Const Correctness

- Read operations are `const`
- Mutable operations clearly marked
- Prevents accidental modification

## Testing Security

### Unit Tests

- Bounds violation detection
- Use-after-free detection
- Generation wraparound handling
- Double-free detection

### Fuzz Testing

DotVM includes fuzzing targets:

```bash
cmake -DDOTVM_BUILD_FUZZERS=ON ..
./bytecode_fuzzer corpus/
./capi_fuzzer corpus/
```

### Sanitizers

Build with sanitizers for additional checking:

```bash
cmake -DDOTVM_ENABLE_SANITIZERS=ON ..
```

Enables:
- AddressSanitizer (buffer overflow, use-after-free)
- UndefinedBehaviorSanitizer (integer overflow, etc.)

## Security Recommendations

1. **Always validate bytecode** before execution
2. **Set resource limits** for untrusted code
3. **Monitor SecurityStats** for anomalies
4. **Run with sanitizers** during development
5. **Fuzz test** new opcodes and operations
6. **Review error handling** paths carefully
