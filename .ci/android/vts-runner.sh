#!/usr/bin/env bash
# shellcheck disable=SC1091 # no need to follow references to other shell scripts

set -e

# shellcheck disable=SC2034 # exit code is used in the err trap
EXIT_CODE=1

if [[ -z ${CI_PROJECT_DIR} ]]; then
    CI_PROJECT_DIR="$(dirname "${0}")/../.."
fi

source "${CI_PROJECT_DIR}/.ci/android/launch-cvd.sh"

function run_vts() {
  if [[ -z ${1-} ]]; then
    echo "run_vts: missing required argument <test_name>"
    return 1
  fi
  local test_name=$1
  local skip_file_name=${2:-vts-skips.txt}
  local results_log="vts_results_${test_name}"
  adb shell stop surfaceflinger
  adb logcat -c

  ADB_TEST_CMD="(/data/local/tmp/VtsHalGraphicsComposer3_TargetTest 2>&1 \
    | tee /data/local/tmp/${results_log})"

  set +e
  local TIMED_OUT=0
  # If drm-hwcomposer crashes, the test may timeout rather than fail
  timeout -k 30s 3m adb shell "${ADB_TEST_CMD}"
  TIMED_OUT=$?
  if [[ "$TIMED_OUT" -ne 0 ]]; then
      adb logcat -d | grep -E "hwc|VtsHalGraphicsComposer3_TargetTest|ActivityManager" > "${RESULTS_DIR}/vts_results_${test_name}.timeout"
      echo "${test_name} timed out."
  fi

  adb pull "/data/local/tmp/${results_log}" "${RESULTS_DIR}"
  [ -s "${RESULTS_DIR}/${results_log}" ]
  MISSING_RESULTS=$?
  if [ "${MISSING_RESULTS}" -ne 0 ]; then
    echo "Missing results."
    return "${MISSING_RESULTS}";
  fi

  mapfile -t expected_skips < <(
    sed -nE '/^(#|[[:space:]]*$)/!{ s/^[[:space:]]+//; s/[[:space:]]+$//; p }' \
      "${CI_PROJECT_DIR}/.ci/android/${skip_file_name}"
  )

  local actual_skips_file="vts_results_${test_name}_actual_skips.txt"
  sed -n '/\[ *SKIPPED *\].*listed below:/,$p' \
    "${RESULTS_DIR}/${results_log}" | sed '1d' > "${RESULTS_DIR}/${actual_skips_file}"

  mapfile -t actual_skips < <(
    sed -nE 's/^\[[[:space:]]*SKIPPED[[:space:]]*\][[:space:]]*(.*)$/\1/p' \
      "${RESULTS_DIR}/${actual_skips_file}"
  )

  UNEXPECTED_SKIPS=0
  if [[ "${expected_skips[*]}" != "${actual_skips[*]}" ]]; then
    UNEXPECTED_SKIPS=1
    echo "Unexpected changes to the skipped tests."
    unexpected_run=$(comm -23 <(printf '%s\n' "${expected_skips[@]}" | sort) \
        <(printf '%s\n' "${actual_skips[@]}"   | sort) || true)
    unexpected_skip=$(comm -13 <(printf '%s\n' "${expected_skips[@]}" | sort) \
        <(printf '%s\n' "${actual_skips[@]}"   | sort) || true)

    if [[ -n $unexpected_run ]]; then
      echo -e "\nTest(s) listed in ${skip_file_name}, were actually run: "
      printf '%s\n' "$unexpected_run"
    fi

    if [[ -n $unexpected_skip ]]; then
      echo -e "\nTest(s) not listed in ${skip_file_name}, were actually skipped:"
      printf '%s\n' "$unexpected_skip"
    fi
  fi

  # shellcheck disable=SC2251 # don't exit on failure
  ! grep -i "FAILED" "${RESULTS_DIR}/${results_log}"
  FAILED_TESTS=$?
  if [ "${FAILED_TESTS}" -ne 0 ]; then
    echo "Failed tests."
  fi

  [ "${TIMED_OUT}" = "0" ] && \
  [ "${FAILED_TESTS}" = "0" ] && \
  [ "${UNEXPECTED_SKIPS}" = "0" ]
  return $?
}

adb wait-for-device devices
adb root
adb push "/${BINARIES_DIR}/VtsHalGraphicsComposer3_TargetTest" /data/local/tmp/
