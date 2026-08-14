#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.

set -euo pipefail

project_dir=$(cd "$(dirname "$0")" && pwd)
build_dir="${project_dir}/build"
output_dir="${project_dir}/build_out"
jobs=8
build_type=Release
cann_3rd_lib_path="${project_dir}/third_party"

usage() {
  echo "Usage: $0 [-j N] [--build-type Release|Debug] [--cann-3rd-lib-path PATH]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -j)
      jobs="$2"
      shift 2
      ;;
    --build-type)
      build_type="$2"
      shift 2
      ;;
    --cann-3rd-lib-path)
      cann_3rd_lib_path="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "${build_type}" != "Release" && "${build_type}" != "Debug" ]]; then
  echo "--build-type must be Release or Debug" >&2
  exit 2
fi

if [[ -z "${ASCEND_HOME_PATH:-}" ]]; then
  if [[ -f /usr/local/Ascend/cann/set_env.sh ]]; then
    source /usr/local/Ascend/cann/set_env.sh
  elif [[ -f /usr/local/Ascend/ascend-toolkit/set_env.sh ]]; then
    source /usr/local/Ascend/ascend-toolkit/set_env.sh
  else
    echo "CANN environment is unavailable. Source set_env.sh or set ASCEND_HOME_PATH." >&2
    exit 1
  fi
fi

cmake -S "${project_dir}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DCANN_3RD_LIB_PATH="${cann_3rd_lib_path}" \
  -DACLTEST_BUILD_DEVICE=ON
cmake --build "${build_dir}" --parallel "${jobs}"
ctest --test-dir "${build_dir}" --output-on-failure

mkdir -p "${output_dir}/bin" "${output_dir}/opp/built-in/op_impl/aicpu/config" \
  "${output_dir}/opp/built-in/op_impl/aicpu/kernel"
install -m 0755 "${build_dir}/acl_copy_bench" "${output_dir}/bin/acl_copy_bench"
install -m 0444 "${project_dir}/device/libacltest_sdma_kernel.json" \
  "${output_dir}/opp/built-in/op_impl/aicpu/config/libacltest_sdma_kernel.json"

mapfile -t archives < <(find "${build_dir}" -type f -name 'acltest-sdma-compat.tar.gz' | sort)
if [[ ${#archives[@]} -eq 0 ]]; then
  echo "Built AICPU archive acltest-sdma-compat.tar.gz was not found under ${build_dir}" >&2
  exit 1
fi
install -m 0444 "${archives[${#archives[@]}-1]}" \
  "${output_dir}/opp/built-in/op_impl/aicpu/kernel/acltest-sdma-compat.tar.gz"

echo "Build output: ${output_dir}"
echo "Next: ${project_dir}/scripts/install_kernel.sh"
