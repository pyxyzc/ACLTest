#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

acltest_malloc_script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck disable=SC1091
source "${acltest_malloc_script_dir}/../../hixl_fabric_aicpu_sdma_tests/scripts/ascend_env.sh"
unset acltest_malloc_script_dir
