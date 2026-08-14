#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

acltest_fabric_package_name="acltest-sdma-compat.tar.gz"
acltest_fabric_config_begin="# BEGIN ACLTEST_FABRIC_AICPU_SDMA"
acltest_fabric_config_end="# END ACLTEST_FABRIC_AICPU_SDMA"

acltest_fabric_package_config_path() {
  local ascend_home=$1
  printf '%s/conf/ascend_package_load.ini\n' "${ascend_home}"
}

acltest_fabric_check_config_markers() {
  local config_path=$1
  local begin_count
  local end_count
  begin_count=$(grep -Fxc "${acltest_fabric_config_begin}" "${config_path}" || true)
  end_count=$(grep -Fxc "${acltest_fabric_config_end}" "${config_path}" || true)
  if [[ ${begin_count} -gt 1 || ${end_count} -gt 1 || ${begin_count} -ne ${end_count} ]]; then
    echo "Invalid Fabric AclTest marker state in ${config_path}; fix it manually before continuing." >&2
    return 1
  fi
}

acltest_fabric_register_package() {
  local ascend_home=$1
  local config_path
  local package_line="name:${acltest_fabric_package_name}"
  local temp_path
  config_path=$(acltest_fabric_package_config_path "${ascend_home}")

  [[ -f "${config_path}" ]] || {
    echo "Missing ${config_path}; this CANN installation cannot register the AICPU package." >&2
    return 1
  }
  acltest_fabric_check_config_markers "${config_path}"

  if grep -Fqx "${acltest_fabric_config_begin}" "${config_path}"; then
    echo "Fabric package registration already exists in ${config_path}"
    return 0
  fi
  if grep -Fqx "${package_line}" "${config_path}"; then
    echo "Package ${acltest_fabric_package_name} is already registered outside the Fabric AclTest block; leaving it unchanged."
    return 0
  fi

  temp_path=$(mktemp)
  if ! cp -- "${config_path}" "${temp_path}"; then
    rm -f -- "${temp_path}"
    return 1
  fi
  if [[ -s "${temp_path}" ]] && [[ $(tail -c 1 "${temp_path}" | wc -l) -eq 0 ]]; then
    printf '\n' >> "${temp_path}"
  fi
  {
    printf '%s\n' "${acltest_fabric_config_begin}"
    printf '%s\n' "${package_line}"
    printf '%s\n' 'install_path:2'
    printf '%s\n' 'optional:true'
    printf '%s\n' 'package_path:opp/built-in/op_impl/aicpu/kernel'
    printf '%s\n' 'load_as_per_soc:false'
    printf '%s\n' "${acltest_fabric_config_end}"
  } >> "${temp_path}"
  if ! cp -- "${temp_path}" "${config_path}"; then
    rm -f -- "${temp_path}"
    return 1
  fi
  rm -f -- "${temp_path}"
  echo "Registered ${acltest_fabric_package_name} in ${config_path}"
}

acltest_fabric_unregister_package() {
  local ascend_home=$1
  local config_path
  local temp_path
  config_path=$(acltest_fabric_package_config_path "${ascend_home}")

  if [[ ! -f "${config_path}" ]]; then
    echo "Package config does not exist: ${config_path}; registration was not changed."
    return 0
  fi
  acltest_fabric_check_config_markers "${config_path}"
  if ! grep -Fqx "${acltest_fabric_config_begin}" "${config_path}"; then
    echo "No Fabric AclTest-owned package registration found in ${config_path}"
    return 0
  fi

  temp_path=$(mktemp)
  if ! awk -v begin="${acltest_fabric_config_begin}" -v end="${acltest_fabric_config_end}" '
    $0 == begin { skip = 1; next }
    $0 == end { skip = 0; next }
    !skip { print }
  ' "${config_path}" > "${temp_path}"; then
    rm -f -- "${temp_path}"
    return 1
  fi
  if ! cp -- "${temp_path}" "${config_path}"; then
    rm -f -- "${temp_path}"
    return 1
  fi
  rm -f -- "${temp_path}"
  echo "Unregistered ${acltest_fabric_package_name} from ${config_path}"
}
