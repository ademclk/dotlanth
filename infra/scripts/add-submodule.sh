#!/usr/bin/env bash
set -euo pipefail

expected_file="${EXPECTED_SUBMODULES_FILE:-infra/submodules/required-submodules.txt}"

usage() {
  echo "Usage: $0 <path> <remote-url> [branch]" >&2
  echo "Example: $0 products/dot-core git@github.com:ademclk/dot-core.git main" >&2
}

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  usage
  exit 2
fi

path="$1"
url="$2"
branch="${3:-}"

if [ ! -f "${expected_file}" ]; then
  echo "Expected submodule manifest not found: ${expected_file}" >&2
  exit 1
fi

if ! grep -vE '^[[:space:]]*(#|$)' "${expected_file}" | grep -Fxq "${path}"; then
  echo "Path is not listed in ${expected_file}: ${path}" >&2
  exit 1
fi

if [ -e "${path}" ] && [ -n "$(find "${path}" -mindepth 1 -maxdepth 1 2>/dev/null)" ]; then
  echo "Path already exists and is not empty: ${path}" >&2
  echo "Move its contents into the target repository, remove the local directory from this repo, then rerun this command." >&2
  exit 1
fi

mkdir -p "$(dirname "${path}")"

if [ -n "${branch}" ]; then
  git submodule add -b "${branch}" "${url}" "${path}"
else
  git submodule add "${url}" "${path}"
fi

git submodule update --init --recursive "${path}"
