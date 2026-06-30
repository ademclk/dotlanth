#!/usr/bin/env bash
set -euo pipefail

marker_pattern="(T)(ODO)|(T)(BD)|(PLACE)(HOLDER)|(UN)(RESOLVED)"

candidate_paths=(Prompt.md Plan.md Implement.md Documentation.md README.md Tracker.md docs infra .github)
scan_paths=()

for path in "${candidate_paths[@]}"; do
  if [ -e "${path}" ]; then
    scan_paths+=("${path}")
  fi
done

if [ "${#scan_paths[@]}" -eq 0 ]; then
  echo "No documentation paths found; skipping docs check."
  exit 0
fi

if command -v rg >/dev/null 2>&1; then
  marker_matches="$(rg -n "${marker_pattern}" "${scan_paths[@]}" || true)"
else
  marker_matches="$(grep -RInE "${marker_pattern}" "${scan_paths[@]}" || true)"
fi

if [ -n "${marker_matches}" ]; then
  printf '%s\n' "${marker_matches}"
  exit 1
fi
