# Code Review Guide for DotLanth

This guide helps reviewers identify issues that matter most in a virtual machine codebase. Focus your time on safety-critical issues rather than style nitpicks.

---

## Review Priority Order

Review in this order, spending more time on higher priorities:

1. **Correctness** - Does it do what it claims to do?
2. **Security** - Can it be exploited or abused?
3. **Safety** - Are there memory, concurrency, or resource issues?
4. **Performance** - Only if there's measurable impact (benchmarks exist)
5. **Style** - Let `clang-format` and `clang-tidy` handle this

---

## Red Flags to Search For

### Memory Safety Red Flags

| Pattern | Risk | Action |
|---------|------|--------|
| `new` without `delete` or smart pointer | Memory leak | Require RAII wrapper |
| `new T[n]` with `n` from input | Allocation overflow | Require size validation |
| Raw pointer returned from function | Unclear ownership | Clarify or use smart pointer |
| `reinterpret_cast` | Aliasing violation | Require justification |
| `memcpy`/`memmove` with computed size | Buffer overflow | Verify size is bounded |
| `delete` in destructor | Double-free risk | Prefer smart pointers |
| Reference member variable | Dangling reference | Ensure lifetime correctness |

**Search commands:**
```bash
git diff --name-only | xargs grep -n 'new \|delete \|reinterpret_cast'
git diff --name-only | xargs grep -n 'memcpy\|memmove\|memset'
```

### Concurrency Red Flags

| Pattern | Risk | Action |
|---------|------|--------|
| `std::atomic` with default ordering | Race condition | Require explicit `memory_order_*` |
| Lock acquired in one function, released in another | Deadlock risk | Use RAII lock guards |
| Callback invoked while holding lock | Deadlock | Release lock before callback |
| `shared_ptr` passed across threads | Data race on pointee | Document thread safety |
| `condition_variable::wait()` without predicate | Spurious wakeup | Use predicate form |
| Static variable without thread-safe init | Race condition | Use `std::call_once` or atomic |

**Search commands:**
```bash
git diff --name-only | xargs grep -n 'std::atomic\|mutex\|lock_guard'
git diff --name-only | xargs grep -n '\.lock()\|\.unlock()'
```

### VM/Bytecode Red Flags

| Pattern | Risk | Action |
|---------|------|--------|
| Jump target not validated | Code injection | Require bounds check |
| Opcode handler modifying PC and continuing | Double execute | Return after PC modification |
| State read without generation check | Use-after-free | Check generation counter |
| Unchecked integer in immediate field | Integer overflow | Validate or use safe arithmetic |
| Register index from bytecode unchecked | Out-of-bounds | Bounds check before access |
| Memory address from bytecode unchecked | Arbitrary read/write | Validate against memory bounds |

**Search commands:**
```bash
git diff --name-only | xargs grep -n 'set_pc\|instruction_pointer'
git diff --name-only | xargs grep -n 'rs1()\|rs2()\|rd()\|imm_'
```

### Input Processing Red Flags

| Pattern | Risk | Action |
|---------|------|--------|
| Length read from input used in allocation | Memory exhaustion | Cap at maximum |
| Offset from input used as array index | Out-of-bounds | Bounds check first |
| String from input used in format string | Format string attack | Use `{}` formatting |
| Size calculation without overflow check | Integer overflow | Use `checked_mul` or validate |
| Path from input without sanitization | Path traversal | Sandbox or validate |

**Search commands:**
```bash
git diff --name-only | xargs grep -n 'read_u\|read_i'
git diff --name-only | xargs grep -n '\[.*input\|data\['
```

---

## Questions to Ask During Review

### For Every Change

1. **"What happens if this fails?"** - Is the error handled? Logged? Propagated?
2. **"What happens if the input is malicious?"** - Assume adversarial input
3. **"Is there a simpler way?"** - Complexity is the enemy of security

### For Concurrent Code

4. **"What happens if another thread modifies X while this runs?"**
5. **"What's the lock ordering?"** - Are multiple locks acquired in consistent order?
6. **"Is this lock-free code correct?"** - Consider requesting a second reviewer

### For VM Code

7. **"Can a bytecode sequence trigger this bug?"** - Think like an attacker
8. **"Is the state consistent after partial failure?"** - Transactions should be atomic
9. **"Are resource limits enforced?"** - Instructions, memory, time

### For State Management

10. **"What happens on crash mid-operation?"** - WAL recovery path
11. **"Is the generation counter updated?"** - Prevent stale reads
12. **"Are transactions properly bounded?"** - Commit/rollback on all paths

---

## Review Checklist by Component

### Core VM (`src/dotvm/core/`, `src/dotvm/exec/`)

- [ ] Instruction decoding validates all fields
- [ ] Opcode handlers check resource limits
- [ ] Memory access validates address ranges
- [ ] Register access validates indices
- [ ] State modifications update generation counters
- [ ] No unbounded loops in execution path

### JIT Compiler (`src/dotvm/jit/`)

- [ ] Generated code is bounds-checked
- [ ] Code buffer permissions (RX, not RWX)
- [ ] Deoptimization paths are safe
- [ ] Profile data validated before use

### State/Replication (`src/dotvm/core/state/`)

- [ ] Transaction boundaries correct
- [ ] WAL operations ordered correctly
- [ ] MPT operations maintain invariants
- [ ] Raft messages validated on receipt
- [ ] Network input length-limited

### Crypto (`src/dotvm/core/crypto/`)

- [ ] Using approved algorithms only
- [ ] Keys zeroed after use
- [ ] No timing side channels
- [ ] Random from cryptographic source

### CLI (`src/dotvm/cli/`)

- [ ] User input sanitized
- [ ] File paths validated
- [ ] Error messages don't leak sensitive info
- [ ] Resource cleanup on all exit paths

---

## Providing Feedback

### Severity Levels

Use these prefixes to indicate severity:

- **`BLOCKER:`** - Must fix before merge. Security issue, data corruption, crash.
- **`MUST:`** - Should fix before merge. Correctness issue, missing validation.
- **`SHOULD:`** - Recommended fix. Code quality, maintainability.
- **`NIT:`** - Minor suggestion. Style preference, optional improvement.

### Good Feedback Examples

```markdown
BLOCKER: This reads `len` from untrusted input and allocates without validation.
An attacker could trigger OOM with `len = SIZE_MAX`.

Suggestion:
```cpp
if (len > MAX_MESSAGE_SIZE) {
    return std::unexpected(Error::MessageTooLarge);
}
```

MUST: The lock is released before checking the condition variable predicate,
which could cause a race. Use `wait(lock, predicate)` form instead.

SHOULD: Consider using `std::span` instead of pointer + size here.
It's clearer and prevents parameter ordering mistakes.

NIT: This could be a range-based for loop.
```

### Things NOT to Comment On

- Formatting issues (use `clang-format`)
- Naming style (if consistent with codebase)
- "I would have done it differently" (if current approach is correct)
- Performance micro-optimizations without benchmarks

---

## After Review

- **Approve** when all BLOCKER and MUST items are addressed
- **Request Changes** for any BLOCKER items
- **Comment** for ongoing discussion without blocking

Remember: The goal is shipping correct, safe code—not perfection.

---

*Last updated: 2024*
*For questions about this guide, open an issue or discuss in PR comments.*
