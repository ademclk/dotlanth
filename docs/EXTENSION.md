# Extending DotVM

This guide explains how to add new instructions, opcodes, and functionality to DotVM.

## Adding a New Opcode

### Step 1: Choose an Opcode Value

Refer to the opcode ranges in `include/dotvm/core/instruction.hpp`:

| Range       | Category     | Available |
|-------------|--------------|-----------|
| 0x09-0x1F   | Arithmetic   | Reserved  |
| 0x20-0x2F   | Bitwise      | Some used |
| 0x30-0x3F   | Comparison   | Some used |
| 0x40-0x5F   | Control Flow | Some used |
| 0x60-0x7F   | Memory       | Some used |
| 0x90-0x9F   | Reserved     | Available |
| 0xD0-0xEF   | Reserved     | Available |

### Step 2: Define the Opcode Constant

Add the constant in two places:

**`include/dotvm/core/opcode.hpp`:**
```cpp
namespace opcode {
    /// YOUR_OP: Description of what it does
    inline constexpr std::uint8_t YOUR_OP = 0xXX;
}
```

**`include/dotvm/exec/dispatch_macros.hpp`:**
```cpp
namespace opcode {
    inline constexpr std::uint8_t YOUR_OP = 0xXX;
}
```

### Step 3: Choose Instruction Format

DotVM has three instruction formats:

**Type A (3 registers):**
```
[31:24]=opcode [23:16]=Rd [15:8]=Rs1 [7:0]=Rs2
```
Use for: Binary operations (add, sub, bitwise ops)

**Type B (1 register + immediate):**
```
[31:24]=opcode [23:16]=Rd [15:0]=imm16
```
Use for: Immediate operations (addi, load constant)

**Type C (24-bit offset):**
```
[31:24]=opcode [23:0]=offset24
```
Use for: Jumps, branches, calls

### Step 4: Add Encoding/Decoding (if new format)

If using an existing format, skip this step. Otherwise add to `include/dotvm/core/instruction.hpp`:

```cpp
struct DecodedYourFormat {
    std::uint8_t opcode;
    // Your fields here

    constexpr bool operator==(const DecodedYourFormat&) const noexcept = default;
};

[[nodiscard]] constexpr DecodedYourFormat decode_your_format(std::uint32_t instr) noexcept {
    return {
        .opcode = static_cast<std::uint8_t>((instr >> 24) & 0xFF),
        // Extract your fields
    };
}

[[nodiscard]] constexpr std::uint32_t encode_your_format(
    std::uint8_t opcode, /* your params */) noexcept {
    return (static_cast<std::uint32_t>(opcode) << 24)
         | /* your encoding */;
}
```

### Step 5: Implement the Handler

Add the handler in `src/dotvm/exec/execution_engine.cpp`:

**In `dispatch_loop()`:**
```cpp
op_YOUR_OP: {
    auto d = core::decode_type_a(instr);  // or type_b/type_c

    // Your implementation here
    auto result = /* operation */;
    regs.write(d.rd, result);

    DOTVM_NEXT();
}
```

**In `execute_instruction()` (switch-based fallback):**
```cpp
case opcode::YOUR_OP: {
    auto d = core::decode_type_a(instr);
    auto result = /* operation */;
    regs.write(d.rd, result);
    return true;
}
```

### Step 6: Update the Dispatch Table

In `dispatch_loop()`, add the label to the dispatch table:

```cpp
static constexpr void* dispatch_table[256] = {
    // ... existing entries ...
    [opcode::YOUR_OP] = &&op_YOUR_OP,
    // ...
};
```

### Step 7: Add Tests

Create tests in `tests/dotvm/exec/execution_engine_test.cpp`:

```cpp
TEST_F(ExecutionEngineTest, YourOp_BasicCase) {
    std::vector<std::uint32_t> code = {
        core::encode_type_a(opcode::YOUR_OP, 1, 2, 3),
        core::encode_type_a(opcode::HALT, 0, 0, 0)
    };

    ctx.registers().write(2, Value::from_int(10));
    ctx.registers().write(3, Value::from_int(20));

    auto result = engine.execute(code, ctx);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx.registers().read(1).as_integer(), /* expected */);
}
```

## Adding ALU Operations

### Step 1: Add to ALU Interface Concept

In `include/dotvm/core/concepts/alu_concept.hpp`:

```cpp
template<typename T>
concept YourAluOp = requires(T& alu, Value a, Value b) {
    { alu.your_op(a, b) } -> std::same_as<Value>;
};
```

### Step 2: Implement in ALU

In `include/dotvm/core/alu.hpp`:

```cpp
[[nodiscard]] Value your_op(Value lhs, Value rhs) const noexcept {
    auto a = lhs.as_integer();
    auto b = rhs.as_integer();
    auto result = /* your operation */;
    return Value::from_int(arch_config::mask_int(result, arch_));
}
```

### Step 3: Add Tests

In `tests/dotvm/core/alu_test.cpp`:

```cpp
TEST_F(ALUTest, YourOp_BasicCase) {
    auto result = alu.your_op(Value::from_int(10), Value::from_int(20));
    EXPECT_EQ(result.as_integer(), /* expected */);
}
```

## Adding Memory Operations

### Step 1: Define Error Code (if needed)

In `include/dotvm/core/memory.hpp`:

```cpp
enum class MemoryError : std::uint8_t {
    // ... existing errors ...
    YourError = N,  // Description
};
```

### Step 2: Add Operation to MemoryManager

```cpp
[[nodiscard]] MemoryError your_operation(Handle h, /* params */) noexcept {
    auto err = validate_handle(h);
    if (err != MemoryError::Success) {
        return err;
    }

    // Your implementation

    return MemoryError::Success;
}
```

### Step 3: Update Concept (if interface change)

In `include/dotvm/core/concepts/memory_manager_concept.hpp`:

```cpp
template<typename T>
concept YourMemoryOp = requires(T& mm, Handle h /* params */) {
    { mm.your_operation(h /* params */) } -> std::same_as<MemoryError>;
};
```

## Adding SIMD Operations

### Step 1: Define Vector Operation

In `include/dotvm/core/simd/simd_alu.hpp`:

```cpp
template<std::size_t Width, typename Lane>
[[nodiscard]] Vector<Width, Lane> your_simd_op(
    const Vector<Width, Lane>& a,
    const Vector<Width, Lane>& b) const noexcept {

    return dispatch_binary_op<Width, Lane>(
        a, b,
        [](Lane x, Lane y) { return /* scalar op */; },
        &avx2_your_op<Width, Lane>,   // AVX2 intrinsic
        &avx512_your_op<Width, Lane>  // AVX-512 intrinsic
    );
}
```

### Step 2: Add Platform-Specific Intrinsics (Optional)

```cpp
#ifdef __AVX2__
template<std::size_t Width, typename Lane>
Vector<Width, Lane> avx2_your_op(
    const Vector<Width, Lane>& a,
    const Vector<Width, Lane>& b) {
    // Use _mm256_xxx intrinsics
}
#endif
```

## Adding a New Value Type

### Step 1: Define Tag Constant

In `include/dotvm/core/value.hpp`:

```cpp
namespace nan_box {
    inline constexpr std::uint64_t TAG_YOUR_TYPE = 0x0006ULL << 48;
    inline constexpr std::uint64_t YOUR_TYPE_PREFIX = QNAN_PREFIX | TAG_YOUR_TYPE;
}
```

### Step 2: Add to ValueType Enum

```cpp
enum class ValueType : std::uint8_t {
    // ... existing types ...
    YourType = 6
};
```

### Step 3: Add Type Check

```cpp
[[nodiscard]] constexpr bool is_your_type() const noexcept {
    return (bits_ & nan_box::TYPE_CHECK_MASK) == nan_box::YOUR_TYPE_PREFIX;
}
```

### Step 4: Add Factory and Accessor

```cpp
[[nodiscard]] static constexpr Value from_your_type(YourData data) noexcept {
    return Value{nan_box::YOUR_TYPE_PREFIX | encode_payload(data)};
}

[[nodiscard]] constexpr YourData as_your_type() const noexcept {
    return decode_payload(bits_ & nan_box::FULL_PAYLOAD);
}
```

## Testing Guidelines

1. **Unit tests**: Test individual operations in isolation
2. **Integration tests**: Test opcodes through the execution engine
3. **Edge cases**: Test boundary conditions, overflow, underflow
4. **Error handling**: Test all error paths
5. **Architecture modes**: Test in both Arch32 and Arch64

## Code Style

- Use `[[nodiscard]]` for functions returning values
- Use `noexcept` for functions that won't throw
- Use `constexpr` where possible
- Follow existing naming conventions (snake_case for functions)
- Add Doxygen comments for public APIs
