#!/usr/bin/env bash
# Run a single AIFM test binary the same way as test.sh (local iokerneld,
# configs/client.config, optional remote mem server for test_tcp_*).
#
# Usage (from any directory):
#   ./run_one_test.sh test_heap
#   ./run_one_test.sh test_array_add

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=shared.sh
source "$SCRIPT_DIR/shared.sh"

cd "$SCRIPT_DIR" || exit 1

usage() {
  echo "Usage: $(basename "$0") <test_name>" >&2
  echo "  test_name: executable basename under aifm/bin/, e.g. test_heap" >&2
  exit 1
}

[[ "${1:-}" ]] || usage
TEST="${1##*/}"

if [[ ! -x "bin/$TEST" ]]; then
  echo "error: bin/$TEST not found or not executable." >&2
  echo "  Build first: cd aifm && make bin/$TEST" >&2
  exit 1
fi

echo "Running $TEST..."
rerun_local_iokerneld
if [[ "$TEST" == *tcp* ]]; then
  rerun_mem_server
fi

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

run_program "./bin/$TEST" 2>&1 | tee "$LOG"

if grep -q "Passed" "$LOG"; then
  say_passed
  RC=0
else
  say_failed
  RC=1
fi

kill_local_iokerneld
if [[ "$TEST" == *tcp* ]]; then
  kill_mem_server
fi

exit "$RC"
