# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
include(cmake/fetch_cann_cmake.cmake)
project(hixl_fabric_aicpu_sdma_tests VERSION 1.0.0 LANGUAGES C CXX)

init_cann_project()
add_cann_target_options()
include(version.cmake)

option(ACLTEST_BUILD_DEVICE "Build the AclTest-owned built-in AICPU package" ON)

include(cmake/locate_ascend.cmake)
acltest_find_ascend_install_path(ASCEND_INSTALL_PATH)
if(NOT ASCEND_INSTALL_PATH)
  message(FATAL_ERROR
    "CANN was not found under /usr/local/Ascend. Set ASCEND_HOME_PATH to its root.")
endif()
message(STATUS "Using CANN installation: ${ASCEND_INSTALL_PATH}")

set(CMAKE_MODULE_PATH ${CANN_CMAKE_DIR}/modules ${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${ASCEND_INSTALL_PATH} ${CMAKE_PREFIX_PATH})

acltest_find_cann_package(acl_rt MODULE REQUIRED)
acltest_find_cann_package(runtime MODULE REQUIRED)

add_library(acltest_benchmark_logic STATIC src/benchmark_logic.cc)
target_include_directories(acltest_benchmark_logic PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(acltest_benchmark_logic PUBLIC cxx_std_17)

add_executable(acl_copy_bench
  src/main.cc
  src/aicpu_batch_launcher.cc
  src/fabric_memory.cc
)
target_include_directories(acl_copy_bench PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(acl_copy_bench PRIVATE cxx_std_17)
target_compile_options(acl_copy_bench PRIVATE -O2 -Wall -Wextra -Wpedantic)
target_link_libraries(acl_copy_bench PRIVATE
  acltest_benchmark_logic
  $<BUILD_INTERFACE:acl_rt_headers>
  $<BUILD_INTERFACE:runtime_headers>
  acl_rt
  runtime
  pthread
)

enable_testing()
add_executable(benchmark_logic_test tests/benchmark_logic_test.cc)
target_link_libraries(benchmark_logic_test PRIVATE acltest_benchmark_logic)
add_test(NAME benchmark_logic_test COMMAND benchmark_logic_test)

add_executable(fabric_memory_layout_test tests/fabric_memory_layout_test.cc)
target_include_directories(fabric_memory_layout_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_features(fabric_memory_layout_test PRIVATE cxx_std_17)
add_test(NAME fabric_memory_layout_test COMMAND fabric_memory_layout_test)

install(TARGETS acl_copy_bench RUNTIME DESTINATION bin)

if(ACLTEST_BUILD_DEVICE)
  if(NOT EXISTS "${ASCEND_INSTALL_PATH}/toolkit/toolchain/hcc")
    message(FATAL_ERROR "AICPU toolchain not found: ${ASCEND_INSTALL_PATH}/toolkit/toolchain/hcc")
  endif()
  add_cann_device_project(acltest)
endif()
