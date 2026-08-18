# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may obtain a copy of the License at
# https://www.hiascend.com/license.
# ----------------------------------------------------------------------------

function(acltest_find_ascend_install_path output_variable)
  set(_candidates)
  if(DEFINED ENV{ASCEND_HOME_PATH} AND NOT "$ENV{ASCEND_HOME_PATH}" STREQUAL "")
    list(APPEND _candidates "$ENV{ASCEND_HOME_PATH}")
  endif()
  list(APPEND _candidates
    "/usr/local/Ascend/ascend-toolkit/latest"
    "/usr/local/Ascend/latest"
    "/usr/local/Ascend/cann"
    "/usr/local/Ascend"
  )

  file(GLOB _versioned_candidates LIST_DIRECTORIES true
    "/usr/local/Ascend/cann-*"
    "/usr/local/Ascend/ascend-toolkit/*"
  )
  list(SORT _versioned_candidates ORDER DESCENDING)
  list(APPEND _candidates ${_versioned_candidates})

  foreach(_candidate IN LISTS _candidates)
    if(IS_DIRECTORY "${_candidate}" AND
       (EXISTS "${_candidate}/set_env.sh" OR
        EXISTS "${_candidate}/bin/setenv.bash" OR
        IS_DIRECTORY "${_candidate}/opp" OR
        IS_DIRECTORY "${_candidate}/include"))
      set(${output_variable} "${_candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${output_variable} "" PARENT_SCOPE)
endfunction()
