#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
ascend_home="${ASCEND_HOME_PATH:-}"

# shellcheck disable=SC1091
source "${project_dir}/scripts/ascend_env.sh"
# shellcheck disable=SC1091
source "${project_dir}/scripts/package_registration.sh"

if [[ ${1:-} == "--ascend-home" ]]; then
  [[ $# -ge 2 ]] || { echo "--ascend-home requires a path" >&2; exit 2; }
  ascend_home="$2"
  shift 2
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: $0 [--ascend-home PATH]" >&2
  exit 2
fi

if [[ -z "${ascend_home}" ]]; then
  ascend_home=$(acltest_find_ascend_home) || {
    echo "CANN was not found; pass --ascend-home or set ASCEND_HOME_PATH." >&2
    exit 1
  }
fi

json_target="${ascend_home}/opp/built-in/op_impl/aicpu/config/libacltest_sdma_kernel.json"
archive_target="${ascend_home}/opp/built-in/op_impl/aicpu/kernel/acltest-sdma-compat.tar.gz"
rm -f -- "${json_target}" "${archive_target}"
acltest_fabric_unregister_package "${ascend_home}"
echo "Removed AclTest-owned kernel files from ${ascend_home}; HIXL files were untouched."
