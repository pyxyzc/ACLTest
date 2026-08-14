#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)

# shellcheck disable=SC1091
source "${script_dir}/custom_op_secverify_common.sh"

# Mode 5 verifies packages signed by either Huawei or the community certificate.
acltest_set_all_custom_op_secverify 5 "signature verification enabled (Huawei or community certificate)"
