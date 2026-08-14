#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail

project_dir=$(cd "$(dirname "$0")/.." && pwd)
ascend_home="${ASCEND_HOME_PATH:-}"

# shellcheck disable=SC1091
source "${project_dir}/scripts/ascend_env.sh"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ascend-home)
      [[ $# -ge 2 ]] || { echo "--ascend-home requires a path" >&2; exit 2; }
      ascend_home="$2"
      shift 2
      ;;
    -h|--help)
      echo "Usage: $0 [--ascend-home PATH]"
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

rm -rf -- "${project_dir}/build" "${project_dir}/build_out"
echo "Removed local build artifacts under ${project_dir}."

if [[ -z "${ascend_home}" ]]; then
  ascend_home=$(acltest_find_ascend_home) || {
    echo "CANN was not found; installed artifacts were not changed." >&2
    exit 1
  }
fi

json_target="${ascend_home}/opp/built-in/op_impl/aicpu/config/libacltest_malloc_sdma_kernel.json"
archive_target="${ascend_home}/opp/built-in/op_impl/aicpu/kernel/acltest-malloc-sdma-compat.tar.gz"
rm -f -- "${json_target}" "${archive_target}"
echo "Removed malloc AclTest kernel files from ${ascend_home}; Fabric and HIXL files were untouched."
