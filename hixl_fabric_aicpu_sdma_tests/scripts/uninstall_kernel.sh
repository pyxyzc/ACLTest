#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
ascend_home="${ASCEND_HOME_PATH:-/usr/local/Ascend/cann}"
if [[ ${1:-} == "--ascend-home" ]]; then
  ascend_home="$2"
  shift 2
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: $0 [--ascend-home PATH]" >&2
  exit 2
fi

json_target="${ascend_home}/opp/built-in/op_impl/aicpu/config/libacltest_sdma_kernel.json"
archive_target="${ascend_home}/opp/built-in/op_impl/aicpu/kernel/acltest-sdma-compat.tar.gz"
rm -f -- "${json_target}" "${archive_target}"
echo "Removed AclTest-owned kernel files from ${ascend_home}; HIXL files were untouched."
