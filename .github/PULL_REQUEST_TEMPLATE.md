## Summary

<!-- 1-3 sentences describing what this PR does and why -->

## Type of Change

- [ ] Bug fix (non-breaking change fixing an issue)
- [ ] New feature (non-breaking change adding functionality)
- [ ] Breaking change (fix or feature causing existing functionality to change)
- [ ] Performance improvement
- [ ] Refactoring (no functional changes)
- [ ] Documentation update
- [ ] Test improvement

## Related Issues

<!-- Link to related issues: Fixes #123, Relates to #456 -->

---

## Safety Considerations

### Memory Safety

<!-- How does this change handle memory? Any new allocations? Ownership changes? -->

- [ ] No new raw pointer usage (using smart pointers/RAII)
- [ ] All new allocations have clear ownership
- [ ] Buffer sizes validated at boundaries
- [ ] N/A - no memory-related changes

### Concurrency

<!-- Does this touch shared state? What synchronization is used? -->

- [ ] Shared state protected by mutex or atomic with documented memory ordering
- [ ] No lock held while calling external code
- [ ] Lock-free code reviewed by second engineer
- [ ] N/A - no concurrent code changes

### Input Handling

<!-- Does this process external input (bytecode, files, network)? How is it validated? -->

- [ ] Length fields validated before allocation
- [ ] Offsets bounds-checked before use
- [ ] Integer overflow prevented on size calculations
- [ ] N/A - no external input processing

### VM Safety (if applicable)

<!-- For changes to VM execution, bytecode, or state management -->

- [ ] Jump targets validated
- [ ] State transitions atomic or rolled back on failure
- [ ] CFI checks maintained
- [ ] Generation counters checked where appropriate
- [ ] N/A - no VM changes

---

## Testing

### Local Verification

- [ ] Unit tests added/updated for new functionality
- [ ] Ran under **ASan+UBSan** locally: `meson test -C build-asan`
- [ ] Ran under **TSan** for concurrent code: `meson test -C build-tsan`
- [ ] Manual testing performed (describe below)

### Test Coverage

<!-- What scenarios does your testing cover? -->

- Edge cases tested:
- Error conditions tested:
- Performance impact measured: (if applicable)

---

## Checklist

- [ ] I have read the [C++ Safety Checklist](.github/CPP_SAFETY_CHECKLIST.md)
- [ ] My code follows the project style guidelines (`clang-format` applied)
- [ ] I have added tests that prove my fix/feature works
- [ ] New and existing tests pass locally
- [ ] I have updated documentation where necessary
- [ ] I have added `// SAFETY:` comments for non-obvious safety reasoning

---

## Reviewer Notes

<!-- Anything reviewers should pay special attention to? Known limitations? Areas of uncertainty? -->

---

## Screenshots / Output

<!-- If applicable, add screenshots or command output demonstrating the change -->
