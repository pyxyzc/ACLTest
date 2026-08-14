#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
common_dir="${project_dir}/../hixl_fabric_aicpu_sdma_tests"
test_dir=$(mktemp -d /tmp/acltest-malloc-logic.XXXXXX)
trap 'rm -rf "${test_dir}"' EXIT

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -I"${common_dir}/include" \
  "${common_dir}/src/benchmark_logic.cc" \
  "${common_dir}/tests/benchmark_logic_test.cc" \
  -o "${test_dir}/malloc_benchmark_logic_test"
"${test_dir}/malloc_benchmark_logic_test"
