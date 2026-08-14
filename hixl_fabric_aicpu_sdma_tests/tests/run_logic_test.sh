#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d /tmp/acltest-logic.XXXXXX)
trap 'rm -rf "${test_dir}"' EXIT

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  -I"${project_dir}/include" \
  "${project_dir}/src/benchmark_logic.cc" \
  "${project_dir}/tests/benchmark_logic_test.cc" \
  -o "${test_dir}/benchmark_logic_test"
"${test_dir}/benchmark_logic_test"
