#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AIFM_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${AIFM_ROOT}/shared.sh"

MODE="${1:-fake}"  # fake | tcp
EDGE_FACTOR="${EDGE_FACTOR:-8}"
TRIALS="${TRIALS:-5}"
N_ARR=(4096 8192 16384 32768 65536 131072)

pushd "${SCRIPT_DIR}/aifm" >/dev/null
make clean
make -j
popd >/dev/null

rerun_local_iokerneld

if [[ "${MODE}" == "tcp" ]]; then
  rerun_mem_server
fi

for N in "${N_ARR[@]}"; do
  LOG="${SCRIPT_DIR}/log.${MODE}.${N}"
  if [[ "${MODE}" == "tcp" ]]; then
    sudo stdbuf -o0 "${SCRIPT_DIR}/aifm/main" \
      "${AIFM_ROOT}/configs/client.config" \
      "${N}" "${EDGE_FACTOR}" "${TRIALS}" \
      "${MEM_SERVER_DPDK_IP}:${MEM_SERVER_PORT}" | tee "${LOG}"
  else
    sudo stdbuf -o0 "${SCRIPT_DIR}/aifm/main" \
      "${AIFM_ROOT}/configs/client.config" \
      "${N}" "${EDGE_FACTOR}" "${TRIALS}" | tee "${LOG}"
  fi
done

echo "Wrote logs: ${SCRIPT_DIR}/log.${MODE}.*"
