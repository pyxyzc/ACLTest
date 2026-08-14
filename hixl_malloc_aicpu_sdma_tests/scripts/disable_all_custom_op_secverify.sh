#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)

# shellcheck disable=SC1091
source "${script_dir}/custom_op_secverify_common.sh"

# The enable flag exposes the configuration switch; mode 0 disables verification itself.
acltest_set_custom_op_secverify 0 "signature verification disabled" "$0" "$@"
