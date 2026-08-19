#!/bin/bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

set -Eeuo pipefail

readonly MIRROR_ROOT="${MIRROR_ROOT:-https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master}"
readonly TARGET_ARCH="aarch64"
readonly TARGET_SOC="950"

usage() {
  cat <<'EOF'
Usage:
  bash install_cann_950_aarch64.sh [install-path] [download-dir]

Arguments:
  install-path  Optional Toolkit and ops installation root. Both packages use
                this path. Defaults to /usr/local/Ascend.
  download-dir  Directory for downloaded run packages and installation logs.
                Defaults to /var/tmp/hixl-cann-run-packages.

Environment:
  MIRROR_ROOT   Override the master mirror URL when required.
EOF
}

log() {
  printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

die() {
  log "ERROR: $*" >&2
  exit 1
}

on_error() {
  local line_no="$1"
  local command_text="$2"
  log "ERROR: command failed at line ${line_no}: ${command_text}" >&2
}

trap 'on_error "$LINENO" "$BASH_COMMAND"' ERR

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "required command is not available: $1"
}

fetch_listing() {
  curl --fail --location --silent --show-error --retry 3 --retry-all-errors \
    --connect-timeout 20 --max-time 60 "$1"
}

extract_links() {
  sed -nE 's/.*href="([^"]+)".*/\1/p'
}

is_downloaded_run_valid() {
  local package_path="$1"
  local file_description

  [[ -s "$package_path" ]] || return 1
  file_description="$(file -b "$package_path")"
  [[ ! "$file_description" =~ [Hh][Tt][Mm][Ll] ]] || return 1
  [[ ! "$file_description" =~ [Xx][Mm][Ll] ]] || return 1
  [[ "$(head -c 2 "$package_path")" == "#!" ]] || return 1
}

download_package() {
  local package_name="$1"
  local package_url="$2"
  local package_path="${download_dir}/${package_name}"
  local partial_path="${package_path}.part"

  if is_downloaded_run_valid "$package_path"; then
    log "reuse downloaded package: ${package_path}"
    return 0
  fi

  log "download: ${package_url}"
  curl --fail --location --show-error --retry 3 --retry-all-errors \
    --connect-timeout 20 --output "$partial_path" --progress-bar "$package_url"
  mv -f "$partial_path" "$package_path"

  is_downloaded_run_valid "$package_path" || die "downloaded file is not a valid run package: ${package_path}"
  log "downloaded: ${package_path}"
}

path_is_writable() {
  local path="$1"

  while [[ ! -e "$path" && "$path" != "/" ]]; do
    path="$(dirname "$path")"
  done
  [[ -w "$path" ]]
}

run_installer() {
  local package_path="$1"
  local install_log="$2"
  local -a command=(bash "$package_path" --install --force -q --install-path="$install_path")

  chmod +x "$package_path"
  if [[ "$(id -u)" -ne 0 ]] && ! path_is_writable "$install_path"; then
    command -v sudo >/dev/null 2>&1 || die "${install_path} is not writable and sudo is unavailable"
    command=(sudo "${command[@]}")
  fi

  log "install: $(basename "$package_path")"
  "${command[@]}" 2>&1 | tee "$install_log"
}

find_env_script() {
  find "$install_path" -maxdepth 5 -type f \( -name set_env.sh -o -name setenv.bash \) -print -quit 2>/dev/null || true
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ "$#" -gt 2 ]]; then
  usage >&2
  exit 2
fi

require_command curl
require_command file
require_command sed
require_command sort
require_command tee
require_command grep
require_command head
require_command tail
require_command dirname
require_command find

install_path="${1:-/usr/local/Ascend}"
download_dir="${2:-${DOWNLOAD_DIR:-/var/tmp/hixl-cann-run-packages}}"
mirror_root="${MIRROR_ROOT%/}"

mkdir -p "$download_dir"

host_arch="$(uname -m)"
if [[ "$host_arch" != "$TARGET_ARCH" ]]; then
  die "this installer targets ${TARGET_ARCH}, but the current host is ${host_arch}"
fi

if command -v npu-smi >/dev/null 2>&1; then
  log "NPU information:"
  npu-smi info
else
  log "WARNING: npu-smi is unavailable; using the explicitly requested SOC ${TARGET_SOC}"
fi

log "query latest build directory: ${mirror_root}"
root_listing="$(fetch_listing "${mirror_root}/")"
latest_date="$({
  printf '%s\n' "$root_listing" |
    sed -nE 's/.*href="([0-9]{10,})\/".*/\1/p' |
    LC_ALL=C sort -nr |
    head -n 1
} || true)"
[[ -n "$latest_date" ]] || die "no dated build directory found under ${mirror_root}"

build_url="${mirror_root}/${latest_date}"
log "latest build directory: ${latest_date}"
build_listing="$(fetch_listing "${build_url}/")"

toolkit_name="$({
  printf '%s\n' "$build_listing" |
    extract_links |
    grep -E "^Ascend-cann-toolkit_[^/]+_linux-${TARGET_ARCH}\\.run$" |
    LC_ALL=C sort -V |
    tail -n 1
} || true)"
[[ -n "$toolkit_name" ]] || die "no ${TARGET_ARCH} Toolkit package found in ${build_url}"

toolkit_version="${toolkit_name#Ascend-cann-toolkit_}"
toolkit_version="${toolkit_version%_linux-${TARGET_ARCH}.run}"
ops_name="Ascend-cann-${TARGET_SOC}-ops_${toolkit_version}_linux-${TARGET_ARCH}.run"

if ! printf '%s\n' "$build_listing" | extract_links | grep -Fxq "$ops_name"; then
  die "matching ops package not found: ${ops_name}"
fi

log "selected Toolkit: ${toolkit_name}"
log "selected ops: ${ops_name}"
log "installation root: ${install_path}"
log "download directory: ${download_dir}"

download_package "$toolkit_name" "${build_url}/${toolkit_name}"
download_package "$ops_name" "${build_url}/${ops_name}"

toolkit_log="${download_dir}/toolkit_install.log"
ops_log="${download_dir}/ops_install.log"
run_installer "${download_dir}/${toolkit_name}" "$toolkit_log"
run_installer "${download_dir}/${ops_name}" "$ops_log"

env_script="$(find_env_script)"
log "CANN Toolkit and ${TARGET_SOC} ops installation completed"
if [[ -n "$env_script" ]]; then
  log "load the installed environment with: source ${env_script}"
else
  log "WARNING: installation environment script was not found under ${install_path}"
fi
log "toolkit log: ${toolkit_log}"
log "ops log: ${ops_log}"
