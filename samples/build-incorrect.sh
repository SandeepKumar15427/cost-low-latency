#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/samples/incorrect_strategy.so"

g++ -std=c++20 -O2 -shared -fPIC -Wall -Wextra -Wpedantic \
  -I"${ROOT}/include" \
  -o "${OUT}" \
  "${ROOT}/samples/incorrect_strategy.cpp"

echo "Built ${OUT}"
file "${OUT}"
# Portable check for exported symbol `create_strategy`.
# On Linux ELF, `nm -D` lists dynamic symbols. On macOS Mach-O, use `nm -g` and
# account for the leading underscore in symbol names.
if nm -D "${OUT}" >/dev/null 2>&1; then
  nm -D "${OUT}" | grep -F create_strategy >/dev/null 2>&1 || {
    echo "ERROR: create_strategy not exported" >&2
    exit 1
  }
else
  # macOS fallback: external symbols are shown by `nm -g` and often prefixed with '_'
  if nm -g "${OUT}" >/dev/null 2>&1; then
    nm -g "${OUT}" | grep -F create_strategy >/dev/null 2>&1 || \
    nm -g "${OUT}" | grep -F _create_strategy >/dev/null 2>&1 || {
      echo "ERROR: create_strategy not exported" >&2
      exit 1
    }
  else
    echo "WARNING: unable to inspect symbols; skipping export check" >&2
  fi
fi
