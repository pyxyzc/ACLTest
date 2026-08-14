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

acltest_print_secverify_usage() {
  local script_name=$1

  cat <<EOF
Usage: ${script_name} [--device-id ID]
       ${script_name} [-i ID]
       ${script_name} [ID]

With no argument, configure every NPU reported by npu-smi info -l.
With an ID, configure only that NPU.
EOF
}

acltest_parse_device_id() {
  local script_name=$1
  shift
  local device_id=""

  if [[ $# -eq 0 ]]; then
    printf '\n'
    return 0
  fi
  if [[ $# -eq 1 ]]; then
    case $1 in
      --device-id=*) device_id=${1#*=} ;;
      *) device_id=$1 ;;
    esac
  elif [[ $# -eq 2 && ($1 == "--device-id" || $1 == "-i") ]]; then
    device_id=$2
  else
    echo "Invalid arguments." >&2
    acltest_print_secverify_usage "${script_name}" >&2
    return 2
  fi
  if [[ ! ${device_id} =~ ^[0-9]+$ ]]; then
    echo "Invalid device ID '${device_id}': expected a non-negative integer." >&2
    acltest_print_secverify_usage "${script_name}" >&2
    return 2
  fi
  printf '%s\n' "${device_id}"
}

acltest_set_custom_op_secverify() {
  local target_mode=$1
  local state_description=$2
  local script_name=$3
  shift 3
  local npu_smi
  local npu_id
  local npu_id_list
  local requested_npu_id
  local found=0
  local failed=0
  local -a npu_ids=()

  if [[ $# -eq 1 && ($1 == "-h" || $1 == "--help") ]]; then
    acltest_print_secverify_usage "${script_name}"
    return 0
  fi
  if ! requested_npu_id=$(acltest_parse_device_id "${script_name}" "$@"); then
    return 2
  fi
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
  if [[ -n ${requested_npu_id} ]]; then
    while IFS= read -r npu_id; do
      if [[ ${npu_id} == "${requested_npu_id}" ]]; then
        found=1
        break
      fi
    done <<< "${npu_id_list}"
    if [[ ${found} -eq 0 ]]; then
      echo "NPU ID ${requested_npu_id} was not reported by npu-smi info -l." >&2
      echo "Available NPU IDs: ${npu_id_list//$'\n'/ }" >&2
      return 2
    fi
    npu_ids=("${requested_npu_id}")
  else
    mapfile -t npu_ids <<< "${npu_id_list}"
  fi

  echo "Target state: ${state_description}"
  echo "Target NPU IDs: ${npu_ids[*]}"
  if [[ ${target_mode} == "0" ]]; then
    echo "WARNING: signature verification will be disabled on the selected NPU(s)."
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
  echo "Selected NPU(s) are configured: ${state_description}"
}
