#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$repo_root"

mode="format"
case "${1:-}" in
    "") ;;
    --check) mode="check" ;;
    --help|-h)
        echo "Usage: $0 [--check]"
        echo "Formats all repository C/C++ source and header files using .clang-format."
        echo "--check  Report formatting violations without modifying files."
        exit 0
        ;;
    *)
        echo "error: unknown option: $1" >&2
        exit 2
        ;;
esac

if ! command -v clang-format >/dev/null 2>&1; then
    echo "error: clang-format is not installed or not in PATH" >&2
    exit 127
fi

if [[ ! -f .clang-format ]]; then
    echo "error: .clang-format was not found in $repo_root" >&2
    exit 1
fi

if [[ "$mode" == "format" ]]; then
    clang_format_args=(-i)
else
    clang_format_args=(--dry-run --Werror)
fi

find . \
    \( -path './.git' -o -path '*/build' -o -path '*/cmake-build-*' -o -path '*/third_party' \
       -o -path '*/third-party' -o -path '*/vendor' \) -prune -o \
    -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
              -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
    -print0 | xargs -0 -r clang-format --style=file "${clang_format_args[@]}"
