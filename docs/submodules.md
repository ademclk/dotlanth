# Product Submodule Rules

Product paths under `products/*` are reserved for future git submodules. Product documentation is not bundled in the parent repo.

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
git submodule add <remote-url> products/dot-core
git submodule update --init --recursive
git submodule update --remote --merge
git submodule status --recursive
```

Do not add parent-owned product docs when the folder becomes a submodule; keep product-owned documentation in the product remote.
