#!/usr/bin/env bash
set -uo pipefail

BINARY=./target/debug/oryx
COUNT=${COUNT:-100}

success=0
fail=0

for ((i=1; i<=COUNT; i++)); do
  printf '%3d: ' "$i"
  if "$BINARY" "$@"; then
    echo success
    ((success++))
  else
    echo fail
    ((fail++))
  fi
done

echo "Done: $success succeeded, $fail failed"