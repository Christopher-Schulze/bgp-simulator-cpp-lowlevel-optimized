#!/bin/bash
# Benchmark harness for BGP simulator (single- and multi-thread)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/../.."
BINARY="${PROJECT_ROOT}/build/bgp_sim"

# Dataset selection: default mini tests (scripts/tests), optional "large" (scripts/tests/test_*.csv)
DATASET="${1:-mini}"
TEST_DIR="${SCRIPT_DIR}/tests"

if [[ "${DATASET}" == "large" ]]; then
  ANN="${TEST_DIR}/test_anns.csv"
  ROV="${TEST_DIR}/test_rov.csv"
else
  ANN="${TEST_DIR}/mini_anns.csv"
  ROV="${TEST_DIR}/mini_rov.csv"
fi

if [[ ! -f "${ANN}" || ! -f "${ROV}" ]]; then
  echo "[ERROR] No announcements/ROV CSVs found (expected scripts/tests/mini_*.csv or scripts/tests/test_*.csv)." >&2
  exit 1
fi

# Build Release binary
bash "${PROJECT_ROOT}/scripts/build.sh" >/dev/null

if [[ ! -x "${BINARY}" ]]; then
  echo "[ERROR] Binary not found after build: ${BINARY}" >&2
  exit 1
fi

run_bench() {
  local threads="$1"
  if [[ -z "${threads}" ]]; then
    echo "[BENCH] threads=1 (single-thread)"
    TIMEFORMAT='  real %3R s'
    time "${BINARY}" "${ANN}" "${ROV}" >/dev/null
  else
    echo "[BENCH] threads=${threads}"
    TIMEFORMAT='  real %3R s'
    time "${BINARY}" "${ANN}" "${ROV}" "${threads}" >/dev/null
  fi
}

# Single-thread baseline
run_bench ""

# Multi-thread runs (clamped by simulator to <=16 and hardware concurrency)
for t in 2 4 8 16; do
  run_bench "${t}"
done
