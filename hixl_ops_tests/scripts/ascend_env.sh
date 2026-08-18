#!/usr/bin/env bash
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may obtain a copy of the License at
# https://www.hiascend.com/license.

# This file is sourced by the project scripts. It intentionally does not change shell options.

acltest_is_ascend_root() {
  local root="$1"
  [[ -d "${root}" ]] || return 1
  [[ -f "${root}/set_env.sh" || -f "${root}/bin/setenv.bash" || -d "${root}/opp" || -d "${root}/include" ]]
}

acltest_find_ascend_home() {
  local configured_root="${ASCEND_HOME_PATH:-}"
  if [[ -n "${configured_root}" ]]; then
    acltest_is_ascend_root "${configured_root}" || return 1
    printf '%s\n' "${configured_root}"
    return 0
  fi

  local candidate
  for candidate in \
      "/usr/local/Ascend/ascend-toolkit/latest" \
      "/usr/local/Ascend/latest" \
      "/usr/local/Ascend/cann" \
      "/usr/local/Ascend"; do
    if acltest_is_ascend_root "${candidate}"; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done

  local -a versioned_roots=()
  local nullglob_state
  nullglob_state="$(shopt -p nullglob)"
  shopt -s nullglob
  versioned_roots=(/usr/local/Ascend/cann-* /usr/local/Ascend/ascend-toolkit/*)
  eval "${nullglob_state}"
  for ((index = ${#versioned_roots[@]} - 1; index >= 0; --index)); do
    candidate="${versioned_roots[index]}"
    if acltest_is_ascend_root "${candidate}"; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

acltest_source_ascend_env() {
  local ascend_root
  ascend_root="$(acltest_find_ascend_home)" || {
    echo "CANN was not found under /usr/local/Ascend; set ASCEND_HOME_PATH to its root." >&2
    return 1
  }
  export ASCEND_HOME_PATH="${ascend_root}"
  if [[ -f "${ascend_root}/set_env.sh" ]]; then
    # shellcheck disable=SC1090
    source "${ascend_root}/set_env.sh"
  elif [[ -f "${ascend_root}/bin/setenv.bash" ]]; then
    # shellcheck disable=SC1090
    source "${ascend_root}/bin/setenv.bash"
  else
    echo "CANN environment script was not found under ${ascend_root}." >&2
    return 1
  fi
}
