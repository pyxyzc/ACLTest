#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
test_bin="${ACLTEST_A5_HAL_TEST_BIN:-${project_dir}/build_out/bin/acl_a5_hal_test}"

for test_case in \
    query_sq_base query_sq_depth query_sq_head query_sq_tail \
    restore_stream config_tail report_empty_cq; do
  echo "=== ${test_case} ==="
  "${test_bin}" --case "${test_case}" "$@"
done
