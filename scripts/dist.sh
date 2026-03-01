#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

version="$(
  cargo metadata --no-deps --format-version 1 |
    python3 -c 'import json,sys; d=json.load(sys.stdin); p=next(p for p in d["packages"] if p["name"]=="dot"); print(p["version"])'
)"
host="$(rustc -vV | awk '/^host: / {print $2}')"

pkg="dotlanth-v${version}-${host}"

rm -rf dist
mkdir -p "dist/${pkg}"

cargo build --release -p dot --locked

cp target/release/dot "dist/${pkg}/dot"
cp README.md LICENSE LICENSE-GPLv3 LICENSE-COMMERCIAL "dist/${pkg}/"

tar -C dist -czf "dist/${pkg}.tar.gz" "${pkg}"

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "dist/${pkg}.tar.gz" >"dist/${pkg}.tar.gz.sha256"
elif command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "dist/${pkg}.tar.gz" >"dist/${pkg}.tar.gz.sha256"
fi

echo "wrote dist/${pkg}.tar.gz"

