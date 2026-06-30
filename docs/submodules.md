# Product Submodule Rules

Product paths under `src/<product>` are reserved for product git submodules once a product remote exists. Core uses `src/core`, Forge uses `src/forge`, and Entropy uses `src/entropy` to match that boundary.

## Parent Repo Owns

- .NET Aspire AppHost composition.
- Service defaults and shared runtime conventions.
- The single shared frontend shell.
- CI/CD workflows and documentation checks.
- Conservative shared contracts in `Dot.Shared` after reuse is proven.

## Product Folders Own

- Product-specific source, plans, decisions, and release notes once the product remote exists.

## Activation Commands

```bash
git submodule add <remote-url> src/core
git submodule add <remote-url> src/forge
git submodule add <remote-url> src/entropy
git submodule update --init --recursive
git submodule update --remote --merge
git submodule status --recursive
```

Do not add parent-owned product docs when the folder becomes a submodule; keep product-owned documentation in the product remote.
