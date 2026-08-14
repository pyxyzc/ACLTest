#!/usr/bin/env bash

set -euo pipefail

project_dir=$(cd "$(dirname "$0")" && pwd)

if [[ $# -gt 0 ]]; then
  if [[ $# -eq 1 && ("$1" == "-h" || "$1" == "--help") ]]; then
    echo "Usage: $0"
    exit 0
  fi
  echo "Unknown argument: $1" >&2
  exit 2
fi

rm -rf -- "${project_dir}/build" "${project_dir}/build_out"
echo "Removed local build artifacts under ${project_dir}."
echo "Benchmark CSV files were kept."
