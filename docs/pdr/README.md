# Product Decision Records

This directory contains Product Decision Records (PDRs) documenting strategic product decisions for Dotlanth.

## What is a PDR?

A PDR captures high-level product and strategic decisions that shape the direction of the project. While ADRs focus on technical implementation choices, PDRs address questions like "why does this project exist?" and "who is it for?"

## PDR Index

| PDR | Title | Status |
|-----|-------|--------|
| [0001](0001-why-custom-vm.md) | Why Build a Custom Virtual Machine | Approved |
| [0002](0002-language-choice-cpp26.md) | Language Choice: C++26 | Approved |
| [0003](0003-positioning-and-audience.md) | Positioning and Target Audience | Approved |

## Creating a New PDR

1. Copy `pdr-template.md` to `NNNN-descriptive-title.md`
2. Fill in all sections
3. Set status to "Draft"
4. Submit for review
5. Update status to "Approved" or "Rejected" after review
6. Update this index

## PDR Statuses

- **Draft**: Under development
- **Proposed**: Ready for review
- **Approved**: Decision adopted
- **Rejected**: Decision not adopted
- **Superseded**: Replaced by a newer PDR (link to replacement)
