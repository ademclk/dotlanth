#!/usr/bin/env bash
set -euo pipefail

required_files=(Prompt.md Plan.md Implement.md Documentation.md README.md)

for file in "${required_files[@]}"; do
  test -f "${file}"
done

marker_pattern="(T)(ODO)|(T)(BD)|(PLACE)(HOLDER)|(UN)(RESOLVED)"

scan_paths=(Prompt.md Plan.md Implement.md Documentation.md README.md Tracker.md docs infra .github)

if command -v rg >/dev/null 2>&1; then
  marker_matches="$(rg -n "${marker_pattern}" "${scan_paths[@]}" || true)"
else
  marker_matches="$(grep -RInE "${marker_pattern}" "${scan_paths[@]}" || true)"
fi

if [ -n "${marker_matches}" ]; then
  printf '%s\n' "${marker_matches}"
  exit 1
fi
