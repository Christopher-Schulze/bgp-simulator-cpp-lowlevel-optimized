#!/bin/bash
# Simple regression test harness for the BGP simulator

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/.."
BINARY="${PROJECT_ROOT}/build/bgp_sim"
TEST_DIR="${SCRIPT_DIR}/tests"

# 1) Build in Release mode
bash "${SCRIPT_DIR}/build.sh" >/dev/null

# 2) Run simulator on mini test data
ACTUAL="${TEST_DIR}/mini_actual.csv"
ACTUAL_MT="${TEST_DIR}/mini_actual_mt.csv"
EXPECTED="${TEST_DIR}/mini_expected.csv"

"${BINARY}" "${TEST_DIR}/mini_anns.csv" "${TEST_DIR}/mini_rov.csv" >"${ACTUAL}"

# 3) Verify header
ACTUAL_HEADER="$(head -n1 "${ACTUAL}")"
EXPECTED_HEADER="$(head -n1 "${EXPECTED}")"

if [[ "${ACTUAL_HEADER}" != "${EXPECTED_HEADER}" ]]; then
  echo "[FAIL] Header mismatch: expected '${EXPECTED_HEADER}', got '${ACTUAL_HEADER}'" >&2
  exit 1
fi

# 4) Compare contents ignoring order (sort both without header)
tail -n +2 "${ACTUAL}" | sort >"${ACTUAL}.sorted"
tail -n +2 "${EXPECTED}" | sort >"${EXPECTED}.sorted"

if cmp -s "${ACTUAL}.sorted" "${EXPECTED}.sorted"; then
  echo "[OK] Mini regression test passed"
else
  echo "[FAIL] Mini regression test FAILED" >&2
  exit 1
fi

# 5) Multi-threaded check (e.g. 4 threads) should produce identical result
"${BINARY}" "${TEST_DIR}/mini_anns.csv" "${TEST_DIR}/mini_rov.csv" 4 >"${ACTUAL_MT}"

ACTUAL_MT_HEADER="$(head -n1 "${ACTUAL_MT}")"
if [[ "${ACTUAL_MT_HEADER}" != "${EXPECTED_HEADER}" ]]; then
  echo "[FAIL] Header mismatch in multi-threaded run" >&2
  exit 1
fi

tail -n +2 "${ACTUAL_MT}" | sort >"${ACTUAL_MT}.sorted"

if cmp -s "${ACTUAL_MT}.sorted" "${EXPECTED}.sorted"; then
  echo "[OK] Multi-threaded regression test passed"
else
  echo "[FAIL] Multi-threaded regression test FAILED" >&2
  exit 1
fi
