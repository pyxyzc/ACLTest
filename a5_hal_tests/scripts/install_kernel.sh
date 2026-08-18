#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
source_root="${project_dir}/build_out/opp/built-in/op_impl/aicpu"
ascend_home="${ASCEND_HOME_PATH:-}"
force=0

# shellcheck disable=SC1091
source "${project_dir}/scripts/ascend_env.sh"
# shellcheck disable=SC1091
source "${project_dir}/scripts/package_registration.sh"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ascend-home)
      [[ $# -ge 2 ]] || { echo "--ascend-home requires a path" >&2; exit 2; }
      ascend_home="$2"
      shift 2
      ;;
    --force)
      force=1
      shift
      ;;
    -h|--help)
      echo "Usage: $0 [--ascend-home PATH] [--force]"
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -z "${ascend_home}" ]]; then
  ascend_home=$(acltest_find_ascend_home) || {
    echo "CANN was not found; pass --ascend-home or set ASCEND_HOME_PATH." >&2
    exit 1
  }
fi

json_source="${source_root}/config/libacltest_a5_hal_test.json"
archive_source="${source_root}/kernel/${acltest_a5_hal_package_name}"
json_target="${ascend_home}/opp/built-in/op_impl/aicpu/config/libacltest_a5_hal_test.json"
archive_target="${ascend_home}/opp/built-in/op_impl/aicpu/kernel/${acltest_a5_hal_package_name}"
package_config=$(acltest_a5_hal_package_config_path "${ascend_home}")

[[ -f "${json_source}" ]] || { echo "Missing ${json_source}; run ./build.sh first." >&2; exit 1; }
[[ -f "${archive_source}" ]] || { echo "Missing ${archive_source}; run ./build.sh first." >&2; exit 1; }
[[ -f "${package_config}" ]] || {
  echo "Missing ${package_config}; this CANN installation cannot deploy a custom built-in AICPU package." >&2
  exit 1
}
if [[ ${force} -eq 0 && ( -e "${json_target}" || -e "${archive_target}" ) ]]; then
  echo "A5 HAL test files already exist. Use --force to replace only these two uniquely named files." >&2
  exit 1
fi

mkdir -p "$(dirname "${json_target}")" "$(dirname "${archive_target}")"
install -m 0444 "${json_source}" "${json_target}"
install -m 0444 "${archive_source}" "${archive_target}"
echo "Installed ${json_target}"
echo "Installed ${archive_target}"
acltest_a5_hal_register_package "${ascend_home}"
echo "Restart the test process so TSD reloads the updated package configuration."
