set shell := ["bash", "-euo", "pipefail", "-c"]

default: check

fmt:
    cargo fmt --all --check

clippy:
    cargo clippy --workspace --all-targets --all-features --locked -- -D warnings

test:
    cargo test --workspace --locked

check: fmt clippy test
