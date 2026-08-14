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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ascend-home)
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
  ascend_home="$(acltest_find_ascend_home)" || {
    echo "CANN was not found under /usr/local/Ascend; pass --ascend-home or set ASCEND_HOME_PATH." >&2
    exit 1
  }
fi

json_source="${source_root}/config/libacltest_sdma_kernel.json"
archive_source="${source_root}/kernel/acltest-sdma-compat.tar.gz"
json_target="${ascend_home}/opp/built-in/op_impl/aicpu/config/libacltest_sdma_kernel.json"
archive_target="${ascend_home}/opp/built-in/op_impl/aicpu/kernel/acltest-sdma-compat.tar.gz"

[[ -f "${json_source}" ]] || { echo "Missing ${json_source}; run ./build.sh first." >&2; exit 1; }
[[ -f "${archive_source}" ]] || { echo "Missing ${archive_source}; run ./build.sh first." >&2; exit 1; }
if [[ ${force} -eq 0 && ( -e "${json_target}" || -e "${archive_target}" ) ]]; then
  echo "AclTest kernel files already exist. Use --force to replace only these two uniquely named files." >&2
  exit 1
fi

mkdir -p "$(dirname "${json_target}")" "$(dirname "${archive_target}")"
install -m 0444 "${json_source}" "${json_target}"
install -m 0444 "${archive_source}" "${archive_target}"
echo "Installed ${json_target}"
echo "Installed ${archive_target}"
