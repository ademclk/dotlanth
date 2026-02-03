# PDR-0003: Positioning and Target Audience

## Status

Approved

## Executive Summary

Dotlanth is positioned as an **intent-driven execution platform** for developers building applications, services, and systems that require deterministic execution, state management, and coordination primitives. It is not a blockchain, not a decentralized network, and not a cloud wrapper.

## Problem Statement

New infrastructure projects face a positioning challenge:
- Vague descriptions attract no one
- Blockchain comparisons invite misunderstanding
- "Platform" is overloaded to meaninglessness
- Technical audiences need precise capability descriptions

Dotlanth must communicate clearly:
1. What it is and what it does
2. What it is not (to prevent misunderstanding)
3. Who should use it and why
4. How it differs from superficially similar systems

## Goals

- Establish clear, honest positioning
- Define target audience precisely
- Differentiate from commonly confused categories
- Provide vocabulary for consistent communication

## Non-Goals

- Appeal to every possible user
- Use marketing language that overpromises
- Position against specific competitors
- Define final commercial strategy

## Proposed Solution

### Positioning Statement

> Dotlanth is an intent-driven execution platform that provides deterministic execution,
> state-centric programming, and coordination primitives for building reliable applications,
> services, and systems.

### What Dotlanth Is

1. **Virtual execution environment (DotVM)**: A register-based VM with deterministic execution semantics, SIMD support, and JIT compilation
2. **State-centric runtime (DotDB)**: Transactional state management with Merkle proofs, WAL, and snapshot support
3. **Intent-based behavior system**: Declarative specification of desired outcomes, not imperative steps
4. **Platform for applications, services, contracts**: Foundation layer for building higher-level systems

### What Dotlanth Is Not

| Not This | Why Not |
|----------|---------|
| A blockchain | No consensus mechanism, no distributed ledger, no cryptocurrency |
| A decentralized network | Single-node execution; distribution is a separate layer |
| A smart contract chain | No on-chain execution, no gas, no native token |
| A cloud wrapper | Runs locally; not a managed service |
| A database | State layer serves the VM; not a general-purpose DB |

### Target Audience

**Primary: Systems developers** building:
- Deterministic execution environments
- State machine implementations
- Coordination systems
- Custom runtimes

**Secondary: Application developers** who need:
- Reproducible computation
- Transactional state semantics
- Intent-based programming models

**Not targeting:**
- Web developers seeking JavaScript alternatives
- Data scientists needing notebooks
- DevOps engineers seeking deployment tools

### Key Differentiators

| Aspect | Dotlanth | Traditional VMs | Blockchains |
|--------|----------|-----------------|-------------|
| Execution | Deterministic | Platform-dependent | Deterministic |
| State | First-class transactional | External | On-chain |
| Distribution | Not built-in | Not built-in | Required |
| Performance | Optimized | General-purpose | Constrained |
| Intent support | Native | None | Contract-based |

## Success Criteria

- New users understand what Dotlanth is within 30 seconds of reading
- No confusion with blockchain projects
- Clear path from "what is this?" to "how do I use it?"
- Consistent messaging across README, docs, and communication

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Blockchain confusion persists | Medium | Medium | Explicit "What This Is Not" section |
| Too narrow positioning | Low | Medium | Emphasize foundation/platform nature |
| Technical jargon alienates newcomers | Medium | Low | Layer documentation from overview to deep-dive |

## Open Questions

- What is the long-term commercial model (open source + services)?
- How does Dotlanth relate to specific application domains (gaming, finance, etc.)?

## References

- [README.md](../../README.md)
- [PDR-0001: Why Custom VM](0001-why-custom-vm.md)
