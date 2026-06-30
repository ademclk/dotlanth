#!/usr/bin/env bash
set -euo pipefail

expected_file="${EXPECTED_SUBMODULES_FILE:-infra/submodules/required-submodules.txt}"

if [ ! -f "${expected_file}" ]; then
  echo "Expected submodule manifest not found: ${expected_file}"
  exit 1
fi

expected_paths="$(grep -vE '^[[:space:]]*(#|$)' "${expected_file}")"
require_all="${REQUIRE_ALL_SUBMODULES:-false}"

if [ ! -f .gitmodules ]; then
  echo "No .gitmodules configured yet. Expected future submodules:"
  printf '%s\n' "${expected_paths}" | sed 's/^/  /'
  exit 0
fi

configured_paths="$(git config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk '{print $2}')"

while IFS= read -r expected_path; do
  [ -n "${expected_path}" ] || continue

  if [ "${require_all}" = "true" ] && ! printf '%s\n' "${configured_paths}" | grep -Fxq "${expected_path}"; then
    echo "Missing expected submodule path in .gitmodules: ${expected_path}"
    exit 1
  fi
done <<EOF
${expected_paths}
EOF

if [ "${require_all}" != "true" ]; then
  missing_paths="$(comm -23 <(printf '%s\n' "${expected_paths}" | sort) <(printf '%s\n' "${configured_paths}" | sort) || true)"
  if [ -n "${missing_paths}" ]; then
    echo "Expected submodules not activated yet:"
    printf '%s\n' "${missing_paths}" | sed 's/^/  /'
  fi
fi

while IFS= read -r configured_path; do
  [ -n "${configured_path}" ] || continue

  if ! printf '%s\n' "${expected_paths}" | grep -Fxq "${configured_path}"; then
    echo "Unexpected submodule path in .gitmodules: ${configured_path}"
    exit 1
  fi

  if ! git ls-files --stage -- "${configured_path}" | grep -q '^160000 '; then
    echo "Configured submodule is not staged as a gitlink: ${configured_path}"
    exit 1
  fi
done <<EOF
${configured_paths}
EOF

submodule_status="$(git submodule status --recursive)"
printf '%s\n' "${submodule_status}"

if printf '%s\n' "${submodule_status}" | grep -nE "^-|^\\+|^U"; then
  echo "Submodule state is not clean."
  exit 1
fi
