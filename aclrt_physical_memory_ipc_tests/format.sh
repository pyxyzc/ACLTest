#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
shared_header="${script_dir}/../console_utils.h"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "error: clang-format not found" >&2
    exit 1
fi
mapfile -d '' source_files < <(
    find "${script_dir}/src" -type f \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
        -print0
)

if [[ -f "${shared_header}" ]]; then
    source_files+=("${shared_header}")
fi

if (( ${#source_files[@]} == 0 )); then
    echo "no C/C++ source files found" >&2
    exit 0
fi

if [[ "${1:-}" == "--check" ]]; then
    clang-format --dry-run --Werror "${source_files[@]}"
else
    clang-format -i "${source_files[@]}"
fi
