#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
test_root=$(mktemp -d /tmp/acltest-fabric-package-registration.XXXXXX)
trap 'rm -rf "${test_root}"' EXIT

# shellcheck disable=SC1091
source "${project_dir}/scripts/package_registration.sh"

mkdir -p "${test_root}/conf"
config_path="${test_root}/conf/ascend_package_load.ini"
printf '%s\n' 'name:cann-existing-package.tar.gz' > "${config_path}"

acltest_fabric_register_package "${test_root}"
grep -Fqx '# BEGIN ACLTEST_FABRIC_AICPU_SDMA' "${config_path}"
grep -Fqx 'name:acltest-sdma-compat.tar.gz' "${config_path}"
grep -Fqx 'package_path:opp/built-in/op_impl/aicpu/kernel' "${config_path}"

acltest_fabric_register_package "${test_root}"
[[ $(grep -Fxc '# BEGIN ACLTEST_FABRIC_AICPU_SDMA' "${config_path}") -eq 1 ]]

acltest_fabric_unregister_package "${test_root}"
grep -Fqx 'name:cann-existing-package.tar.gz' "${config_path}"
if grep -Fq 'ACLTEST_FABRIC_AICPU_SDMA' "${config_path}"; then
  echo "Fabric package registration marker was not removed." >&2
  exit 1
fi
if grep -Fq 'name:acltest-sdma-compat.tar.gz' "${config_path}"; then
  echo "Fabric package registration entry was not removed." >&2
  exit 1
fi

echo "package_registration_test: PASS"
