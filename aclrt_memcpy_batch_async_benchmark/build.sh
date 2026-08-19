#!/usr/bin/env bash

set -euo pipefail

project_dir=$(cd "$(dirname "$0")" && pwd)
build_dir="${project_dir}/build"
output_dir="${project_dir}/build_out"
jobs=8
build_type=Release
ascend_root="${ASCEND_HOME_PATH:-}"

usage() {
  echo "Usage: $0 [-j N] [--build-type Release|Debug] [--ascend-root PATH]"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -j)
      [[ $# -ge 2 ]] || { echo "-j requires a value" >&2; exit 2; }
      jobs="$2"
      shift 2
      ;;
    --build-type)
      [[ $# -ge 2 ]] || { echo "--build-type requires a value" >&2; exit 2; }
      build_type="$2"
      shift 2
      ;;
    --ascend-root)
      [[ $# -ge 2 ]] || { echo "--ascend-root requires a path" >&2; exit 2; }
      ascend_root="$2"
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
if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
  echo "-j must be a positive integer" >&2
  exit 2
fi
if [[ -n "${ascend_root}" && ! -d "${ascend_root}" ]]; then
  echo "Ascend root does not exist: ${ascend_root}" >&2
  exit 2
fi
cmake_args=(
  -S "${project_dir}"
  -B "${build_dir}"
  -DCMAKE_BUILD_TYPE="${build_type}"
)
if [[ -n "${ascend_root}" ]]; then
  cmake_args+=("-DASCEND_ROOT=${ascend_root}")
fi
cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --parallel "${jobs}"

mkdir -p "${output_dir}/bin"
executables=(aclrt_memcpy_batch_bench aclrt_memcpy_batch_path_test aclrt_memcpy_shard_bench)
for executable in "${executables[@]}"; do
  install -m 0755 "${build_dir}/${executable}" "${output_dir}/bin/${executable}"
done

echo "Build output: ${output_dir}/bin/aclrt_memcpy_batch_bench"
echo "Path test:    ${output_dir}/bin/aclrt_memcpy_batch_path_test"
