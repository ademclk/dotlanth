# Architecture Decision Records

This directory contains Architecture Decision Records (ADRs) documenting significant technical decisions made during Dotlanth development.

## What is an ADR?

An ADR captures a single architectural decision along with its context and consequences. ADRs help new team members understand why the system is built the way it is and provide a historical record of technical choices.

## ADR Index

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-nan-boxing-values.md) | NaN-Boxing for Value Representation | Accepted |
| [0002](0002-handle-based-memory.md) | Handle-Based Memory Management | Accepted |
| [0003](0003-computed-goto-dispatch.md) | Computed-Goto Instruction Dispatch | Accepted |
| [0004](0004-fixed-instruction-encoding.md) | Fixed 32-bit Instruction Encoding | Accepted |
| [0005](0005-cpp20-concepts.md) | C++20 Concepts for Type Constraints | Accepted |
| [0006](0006-std-expected-errors.md) | std::expected for Error Handling | Accepted |
| [0007](0007-dual-architecture.md) | Dual Architecture Support (32/64-bit) | Accepted |

## Creating a New ADR

1. Copy `adr-template.md` to `NNNN-descriptive-title.md`
2. Fill in all sections
3. Set status to "Proposed"
4. Submit for review
5. Update status to "Accepted" or "Rejected" after review
6. Update this index

## ADR Statuses

- **Proposed**: Under discussion
- **Accepted**: Decision adopted
- **Rejected**: Decision not adopted
- **Deprecated**: Superseded by another ADR
- **Superseded**: Replaced by a newer ADR (link to replacement)

## References

- [ADR GitHub Organization](https://adr.github.io/)
- [Michael Nygard's original ADR article](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions)
