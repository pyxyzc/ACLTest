# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
include(cmake/fetch_cann_cmake.cmake)
project(hixl_malloc_aicpu_sdma_tests VERSION 1.0.0 LANGUAGES C CXX)

init_cann_project()
add_cann_target_options()
include(version.cmake)

option(ACLTEST_BUILD_DEVICE "Build the malloc benchmark AICPU package" ON)

include(cmake/locate_ascend.cmake)
acltest_find_ascend_install_path(ASCEND_INSTALL_PATH)
if(NOT ASCEND_INSTALL_PATH)
  message(FATAL_ERROR
    "CANN was not found under /usr/local/Ascend. Set ASCEND_HOME_PATH to its root.")
endif()
message(STATUS "Using CANN installation: ${ASCEND_INSTALL_PATH}")

get_filename_component(ACLTEST_COMMON_DIR
  "${PROJECT_SOURCE_DIR}/../hixl_fabric_aicpu_sdma_tests" ABSOLUTE)
if(NOT EXISTS "${ACLTEST_COMMON_DIR}/src/aicpu_batch_launcher.cc")
  message(FATAL_ERROR "Shared AICPU benchmark sources were not found: ${ACLTEST_COMMON_DIR}")
endif()

set(CMAKE_MODULE_PATH ${CANN_CMAKE_DIR}/modules ${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${ASCEND_INSTALL_PATH} ${CMAKE_PREFIX_PATH})

acltest_find_cann_package(acl_rt MODULE REQUIRED)
acltest_find_cann_package(runtime MODULE REQUIRED)

add_library(acltest_malloc_benchmark_logic STATIC
  ${ACLTEST_COMMON_DIR}/src/benchmark_logic.cc)
target_include_directories(acltest_malloc_benchmark_logic PUBLIC
  ${ACLTEST_COMMON_DIR}/include)
target_compile_features(acltest_malloc_benchmark_logic PUBLIC cxx_std_17)

add_executable(acl_malloc_copy_bench
  src/main.cc
  src/malloc_memory.cc
  ${ACLTEST_COMMON_DIR}/src/aicpu_batch_launcher.cc
)
target_include_directories(acl_malloc_copy_bench PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${ACLTEST_COMMON_DIR}/include
)
target_compile_features(acl_malloc_copy_bench PRIVATE cxx_std_17)
target_compile_options(acl_malloc_copy_bench PRIVATE -O2 -Wall -Wextra -Wpedantic)
target_link_libraries(acl_malloc_copy_bench PRIVATE
  acltest_malloc_benchmark_logic
  $<BUILD_INTERFACE:acl_rt_headers>
  $<BUILD_INTERFACE:runtime_headers>
  acl_rt
  runtime
  pthread
)

enable_testing()
add_executable(malloc_benchmark_logic_test
  ${ACLTEST_COMMON_DIR}/tests/benchmark_logic_test.cc)
target_link_libraries(malloc_benchmark_logic_test PRIVATE acltest_malloc_benchmark_logic)
add_test(NAME malloc_benchmark_logic_test COMMAND malloc_benchmark_logic_test)

add_executable(malloc_memory_test
  tests/malloc_memory_test.cc
  src/malloc_memory.cc)
target_include_directories(malloc_memory_test PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${ACLTEST_COMMON_DIR}/include)
target_compile_features(malloc_memory_test PRIVATE cxx_std_17)
target_compile_options(malloc_memory_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(malloc_memory_test PRIVATE
  $<BUILD_INTERFACE:acl_rt_headers>)
add_test(NAME malloc_memory_test COMMAND malloc_memory_test)

install(TARGETS acl_malloc_copy_bench RUNTIME DESTINATION bin)

if(ACLTEST_BUILD_DEVICE)
  if(NOT EXISTS "${ASCEND_INSTALL_PATH}/toolkit/toolchain/hcc")
    message(FATAL_ERROR "AICPU toolchain not found: ${ASCEND_INSTALL_PATH}/toolkit/toolchain/hcc")
  endif()
  add_cann_device_project(acltest_malloc)
endif()
