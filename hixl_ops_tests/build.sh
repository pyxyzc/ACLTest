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
  # shellcheck disable=SC1091
  source "${project_dir}/scripts/ascend_env.sh"
  acltest_source_ascend_env
fi

cmake -S "${project_dir}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE="${build_type}" \
  -DCANN_3RD_LIB_PATH="${cann_3rd_lib_path}"
cmake --build "${build_dir}" --parallel "${jobs}"
ctest --test-dir "${build_dir}" --output-on-failure

mkdir -p "${output_dir}/bin"
install -m 0755 "${build_dir}/hixl_ops_bench" "${output_dir}/bin/hixl_ops_bench"

echo "Build output: ${output_dir}/bin/hixl_ops_bench"
