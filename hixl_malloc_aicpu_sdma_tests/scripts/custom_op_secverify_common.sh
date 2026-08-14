#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

acltest_find_npu_smi() {
  local candidate
  for candidate in /usr/local/sbin/npu-smi /usr/local/bin/npu-smi; do
    if [[ -x "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  command -v npu-smi 2>/dev/null
}

acltest_list_npu_ids() {
  local npu_smi=$1
  local list_output
  if ! list_output=$("${npu_smi}" info -l 2>&1); then
    echo "npu-smi info -l failed:" >&2
    echo "${list_output}" >&2
    return 1
  fi

  printf '%s\n' "${list_output}" | awk -F ':' '
    /NPU ID/ {
      value = $2
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
      if (value ~ /^[0-9]+$/) {
        print value
      }
    }
  ' | sort -nu
}

acltest_show_secverify_state() {
  local npu_smi=$1
  local npu_id=$2
  local failed=0

  "${npu_smi}" info -t custom-op-secverify-enable -i "${npu_id}" || failed=1
  "${npu_smi}" info -t custom-op-secverify-mode -i "${npu_id}" || failed=1
  return "${failed}"
}

acltest_set_all_custom_op_secverify() {
  local target_mode=$1
  local state_description=$2
  local npu_smi
  local npu_id_list
  local failed=0
  local -a npu_ids=()

  if [[ ${EUID} -ne 0 ]]; then
    echo "This script must be run as root on the physical host." >&2
    return 1
  fi
  if [[ ${target_mode} != "0" && ${target_mode} != "5" ]]; then
    echo "Internal error: unsupported secverify mode ${target_mode}." >&2
    return 1
  fi
  npu_smi=$(acltest_find_npu_smi) || {
    echo "npu-smi was not found." >&2
    return 1
  }
  npu_id_list=$(acltest_list_npu_ids "${npu_smi}") || return 1
  if [[ -z "${npu_id_list}" ]]; then
    echo "No NPU IDs were reported by npu-smi info -l." >&2
    return 1
  fi
  mapfile -t npu_ids <<< "${npu_id_list}"

  echo "Target state: ${state_description}"
  echo "Target NPU IDs: ${npu_ids[*]}"
  if [[ ${target_mode} == "0" ]]; then
    echo "WARNING: signature verification will be disabled on every listed NPU."
  fi

  for npu_id in "${npu_ids[@]}"; do
    echo "Configuring NPU ${npu_id}: custom-op-secverify-enable=1, mode=${target_mode}"
    if ! printf 'y\n' | "${npu_smi}" set -t custom-op-secverify-enable -i "${npu_id}" -d 1; then
      echo "Failed to enable secverify configuration on NPU ${npu_id}." >&2
      failed=1
      continue
    fi
    if ! "${npu_smi}" set -t custom-op-secverify-mode -i "${npu_id}" -d "${target_mode}"; then
      echo "Failed to set secverify mode ${target_mode} on NPU ${npu_id}." >&2
      failed=1
      continue
    fi
    if ! acltest_show_secverify_state "${npu_smi}" "${npu_id}"; then
      echo "Failed to verify the resulting state on NPU ${npu_id}." >&2
      failed=1
    fi
  done

  if [[ ${failed} -ne 0 ]]; then
    echo "One or more NPUs could not be configured or verified." >&2
    return 1
  fi
  echo "All NPUs are configured: ${state_description}"
}
