# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
include(cmake/fetch_cann_cmake.cmake)
project(a5_hal_tests VERSION 1.0.0 LANGUAGES C CXX)

init_cann_project()
add_cann_target_options()

option(ACLTEST_BUILD_DEVICE "Build the A5 HAL test AICPU package" ON)

include(cmake/locate_ascend.cmake)
acltest_find_ascend_install_path(ASCEND_INSTALL_PATH)
if(NOT ASCEND_INSTALL_PATH)
  message(FATAL_ERROR "CANN was not found under /usr/local/Ascend. Set ASCEND_HOME_PATH to its root.")
endif()
message(STATUS "Using CANN installation: ${ASCEND_INSTALL_PATH}")

set(CMAKE_MODULE_PATH ${CANN_CMAKE_DIR}/modules ${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${ASCEND_INSTALL_PATH} ${CMAKE_PREFIX_PATH})

acltest_find_cann_package(acl_rt MODULE REQUIRED)
acltest_find_cann_package(runtime MODULE REQUIRED)
acltest_find_cann_package(ascend_hal MODULE REQUIRED)

add_executable(acl_a5_hal_test src/acl_a5_hal_test.cc)
target_include_directories(acl_a5_hal_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(acl_a5_hal_test PRIVATE cxx_std_17)
target_compile_options(acl_a5_hal_test PRIVATE -O2 -Wall -Wextra -Wpedantic)
target_link_libraries(acl_a5_hal_test PRIVATE
  $<BUILD_INTERFACE:acl_rt_headers>
  $<BUILD_INTERFACE:runtime_headers>
  $<BUILD_INTERFACE:ascend_hal_headers>
  acl_rt
  runtime
  pthread
)

install(TARGETS acl_a5_hal_test RUNTIME DESTINATION bin)

if(ACLTEST_BUILD_DEVICE)
  if(NOT EXISTS "${ASCEND_INSTALL_PATH}/toolkit/toolchain/hcc")
    message(FATAL_ERROR "AICPU toolchain not found: ${ASCEND_INSTALL_PATH}/toolkit/toolchain/hcc")
  endif()
  add_cann_device_project(acltest_a5_hal_test)
endif()
