#!/usr/bin/env bash
set -euo pipefail

if [ ! -f .gitmodules ]; then
  echo "No product submodules are activated yet."
  exit 0
fi

submodule_status="$(git submodule status --recursive)"
printf '%s\n' "${submodule_status}"

if printf '%s\n' "${submodule_status}" | grep -nE "^-|^\\+|^U"; then
  echo "Submodule state is not clean."
  exit 1
fi
