# DotVM Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

## Security Model

DotVM is designed as a sandboxed bytecode execution environment with defense-in-depth security measures.

### Threat Model

**In-Scope Threats:**
- Malicious bytecode attempting memory corruption
- Use-after-free exploitation attempts
- Buffer overflow attacks
- Integer overflow exploits
- Control flow hijacking
- Resource exhaustion (DoS)

**Out-of-Scope Threats:**
- Physical access attacks
- Side-channel attacks (timing, cache)
- Attacks requiring root/admin privileges
- Social engineering

### Security Guarantees

#### Memory Safety

- **Generation-based handles**: Prevent use-after-free by tracking allocation generations. Each allocation receives a generation counter that increments on deallocation, invalidating stale handles.
- **Bounds checking**: All memory operations are bounds-checked before execution.
- **No raw pointers in bytecode**: Memory is accessed only through validated handles.
- **Page-aligned allocations**: Memory allocated via system calls (mmap/VirtualAlloc) with platform-level protection.

#### Control Flow Integrity (CFI)

- **Jump target validation**: All jump targets verified against code section bounds.
- **Instruction alignment enforcement**: Jumps must target 4-byte instruction boundaries.
- **Call/return stack**: Shadow stack for return address validation.
- **Backward jump limits**: Configurable limits to detect infinite loops.
- **Reserved opcode rejection**: Invalid opcodes trigger CFI violations.

#### Input Validation

- **Bytecode validation**: Magic bytes, version, architecture, and flags validated before loading.
- **Section bounds checking**: All sections verified against file size with overflow protection.
- **Integer range checking**: 48-bit integer range enforced for value types.
- **Constant pool limits**: Entry count capped at 1M entries to prevent memory exhaustion.

#### Resource Limits

| Limit | Default | Purpose |
|-------|---------|---------|
| Max allocation size | 64MB | Per-VM memory cap |
| Max file size | 2GB | Bytecode file size limit |
| Max constant pool entries | 1M | DoS prevention |
| Max call depth | 1024 | Stack overflow prevention |
| Max backward jumps | 10000 | Infinite loop detection |

### Security Statistics

DotVM tracks security-relevant events for monitoring and auditing:

| Event | Description |
|-------|-------------|
| Bounds violations | Out-of-bounds memory access attempts |
| Invalid handle accesses | Use of deallocated or invalid handles |
| CFI violations | Control flow integrity failures |
| Allocation limit hits | Memory allocation cap reached |
| Handle table exhaustions | Handle table full |
| Generation wraparounds | Handle generation counter wrapped |

Access statistics via `security_stats()` on VmContext or MemoryManager.

## Reporting a Vulnerability

**DO NOT** create public GitHub issues for security vulnerabilities.

To report a security issue:

1. **Email**: security@dotlanth.io (or create a private security advisory on GitHub)
2. **Include**:
   - Detailed description of the vulnerability
   - Steps to reproduce
   - Potential impact assessment
   - Suggested fix (if available)
3. **Expected response**: Within 72 hours
4. **Coordinated disclosure**: 90-day policy

## Security Configuration

### Recommended Settings for Untrusted Input

**C++ API:**
```cpp
auto config = dotvm::core::VmConfig::secure();
// Enables:
// - CFI with strict policy
// - Strict overflow checking
// - Default memory limits

// Or create a fully sandboxed configuration:
auto config = dotvm::core::VmConfig::sandboxed();
// Adds resource limits on top of secure settings
```

**C API:**
```c
dotvm_config_t config = DOTVM_CONFIG_INIT;
config.cfi_enabled = 1;
config.strict_overflow = 1;
config.max_memory = 16 * 1024 * 1024;  // 16MB limit

dotvm_vm_t* vm = dotvm_create(&config);
```

### CFI Policy Options

```cpp
// Strict policy (recommended for untrusted code)
auto policy = dotvm::core::cfi::CfiPolicy::strict();

// Relaxed policy (for trusted code with debugging)
auto policy = dotvm::core::cfi::CfiPolicy::relaxed();

// Custom policy
dotvm::core::cfi::CfiPolicy policy{
    .max_backward_jumps = 10000,
    .max_call_depth = 1024,
    .strict_jump_alignment = true,
    .reject_reserved_opcodes = true,
    .validate_indirect_jumps = true
};
```

## Secure Development Practices

### Code Quality

- All memory operations use `std::memcpy` (no `strcpy`, `sprintf`, etc.)
- No raw pointer arithmetic in public APIs
- Signed/unsigned conversion warnings treated as errors (`-Wconversion -Wsign-conversion`)
- Static analysis via clang-tidy with security-focused checks
- CodeQL analysis for semantic vulnerability detection

### Testing

- Address Sanitizer (ASAN) enabled in CI debug builds
- Undefined Behavior Sanitizer (UBSAN) enabled in CI debug builds
- LibFuzzer harnesses for bytecode parsing and C API
- Comprehensive edge case and fuzz testing

### Build Hardening

Enable additional hardening flags for production builds:

```cmake
# Stack protection
target_compile_options(dotvm_core PRIVATE -fstack-protector-strong)

# Position Independent Executables
target_compile_options(dotvm_core PRIVATE -fPIE)

# Fortify source
target_compile_definitions(dotvm_core PRIVATE _FORTIFY_SOURCE=2)
```

## Audit History

| Date       | Auditor  | Scope              | Findings |
|------------|----------|-------------------|----------|
| 2026-01-11 | Internal | Initial security review | See this document |

## Security Checklist for Contributors

Before submitting PRs that touch security-sensitive code:

- [ ] No new uses of unsafe C functions (strcpy, sprintf, gets, etc.)
- [ ] All buffer accesses are bounds-checked
- [ ] All casts are explicit and reviewed for truncation
- [ ] Error paths don't leak sensitive information
- [ ] New public APIs have null-pointer checks
- [ ] Tests cover edge cases and error conditions
- [ ] CI passes with sanitizers enabled

## Additional Resources

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [CERT C++ Coding Standard](https://wiki.sei.cmu.edu/confluence/pages/viewpage.action?pageId=88046682)
- [OWASP Secure Coding Practices](https://owasp.org/www-project-secure-coding-practices-quick-reference-guide/)
