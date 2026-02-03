# ADR-0005: C++20 Concepts for Type Constraints

## Status

Accepted

## Context

DotVM components (register file, ALU, memory manager) have well-defined interfaces that can be implemented in multiple ways:
- Production implementations optimized for performance
- Test mocks for unit testing
- Instrumented versions for debugging/profiling

Traditional approaches to polymorphism in C++:
1. **Virtual functions**: Runtime dispatch via vtable
2. **Templates with duck typing**: Compile-time but poor error messages
3. **CRTP**: Complex, verbose

We need a mechanism that provides:
- Zero runtime overhead
- Clear interface contracts
- Good compiler error messages
- Easy mocking for tests

## Decision

Use C++20 concepts to define component interfaces with compile-time polymorphism.

```cpp
template<typename T>
concept RegisterFileInterface = requires(T& rf, RegIdx idx, Value val) {
    { rf.read(idx) } -> std::same_as<Value>;
    { rf.write(idx, val) } -> std::same_as<void>;
    { rf.size() } -> std::convertible_to<std::size_t>;
};

template<typename T>
concept MemoryStorable =
    std::is_trivially_copyable_v<T> &&
    !std::is_pointer_v<T> &&
    !std::is_reference_v<T>;
```

Components are then constrained by concepts:

```cpp
template<RegisterFileInterface RF, AluInterface ALU>
class ExecutionEngine {
    RF& registers_;
    ALU& alu_;
    // ...
};
```

## Consequences

### Positive

- **Zero overhead**: No vtable pointer, no indirect calls; everything inlines
- **Compile-time checking**: Interface violations caught at compile time
- **Clear error messages**: Concepts produce readable errors pointing to the unsatisfied requirement
- **Documentation**: Concepts serve as self-documenting interface specifications
- **Easy mocking**: Test doubles just need to satisfy the concept, no inheritance required

### Negative

- **C++20 required**: Not available on older compilers (GCC 10+, Clang 10+, MSVC 19.26+)
- **Template bloat**: Each unique type combination generates new code
- **Header-heavy**: Concept-constrained templates typically live in headers

### Neutral

- Learning curve for developers unfamiliar with concepts
- Debugging template errors still requires some expertise
- Concepts can be refined over time as interfaces evolve

## Alternatives Considered

### Alternative 1: Virtual Functions (Runtime Polymorphism)

```cpp
class IRegisterFile {
public:
    virtual Value read(RegIdx idx) = 0;
    virtual void write(RegIdx idx, Value val) = 0;
    virtual ~IRegisterFile() = default;
};
```

**Rejected because:**
- Vtable pointer overhead (8 bytes per object)
- Indirect call on every operation (cache miss, no inlining)
- Virtual destructor required
- 10-20% performance overhead in hot paths

### Alternative 2: Duck Typing Templates

```cpp
template<typename RF>
void execute(RF& registers) {
    registers.read(0);  // Fails at instantiation if RF doesn't have read()
}
```

**Rejected because:**
- Poor error messages (errors appear at usage site, not definition)
- No documented interface contract
- Easy to accidentally depend on unintended features

### Alternative 3: CRTP (Curiously Recurring Template Pattern)

```cpp
template<typename Derived>
class RegisterFileBase {
    Value read(RegIdx idx) { return static_cast<Derived*>(this)->read_impl(idx); }
};
```

**Rejected because:**
- Verbose and complex
- Requires inheritance
- Confusing for newcomers
- Harder to compose multiple interfaces

### Alternative 4: Type Erasure (std::function, any)

```cpp
std::function<Value(RegIdx)> read_fn;
```

**Rejected because:**
- Heap allocation for non-trivial callables
- Indirect call overhead
- Loses type information

## References

- [Code: include/dotvm/core/concepts.hpp](../../include/dotvm/core/concepts.hpp)
- [C++20 Concepts Reference](https://en.cppreference.com/w/cpp/language/constraints)
- [Bjarne Stroustrup on Concepts](https://www.stroustrup.com/good_concepts.pdf)
- [GCC Concepts Implementation](https://gcc.gnu.org/projects/cxx-status.html#cxx20)
