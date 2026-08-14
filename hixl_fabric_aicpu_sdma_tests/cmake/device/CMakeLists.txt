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
include(../fetch_cann_cmake.cmake)
project(device_acltest_sdma LANGUAGES C CXX)

init_cann_project(PRODUCT_SIDE "device")
add_definitions(-DPRODUCT_SIDE_DEVICE -D_GLIBCXX_USE_CXX11_ABI=0)
set(USE_CXX11_ABI OFF)
set(CMAKE_SKIP_RPATH TRUE)
add_cann_target_options()

get_filename_component(ACLTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(${ACLTEST_DIR}/version.cmake)
include(${ACLTEST_DIR}/cmake/locate_ascend.cmake)
acltest_find_ascend_install_path(ASCEND_INSTALL_PATH)
if(NOT ASCEND_INSTALL_PATH)
  message(FATAL_ERROR
    "CANN was not found under /usr/local/Ascend. Set ASCEND_HOME_PATH to its root.")
endif()
message(STATUS "Using CANN installation: ${ASCEND_INSTALL_PATH}")
set(CMAKE_MODULE_PATH ${CANN_CMAKE_DIR}/modules ${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${ASCEND_INSTALL_PATH} ${CMAKE_PREFIX_PATH})

acltest_find_cann_package(ascend_hal MODULE REQUIRED)

add_library(acltest_sdma_kernel SHARED
  ${ACLTEST_DIR}/device/kernel.cc
  ${ACLTEST_DIR}/device/stars_sdma.cc
  ${ACLTEST_DIR}/device/sync_transfer_context.cc
  ${ACLTEST_DIR}/device/transfer_context_manager.cc
)
set_target_properties(acltest_sdma_kernel PROPERTIES OUTPUT_NAME acltest_sdma_kernel)
target_include_directories(acltest_sdma_kernel PRIVATE
  ${ACLTEST_DIR}/include
  ${ACLTEST_DIR}/device
)
target_compile_features(acltest_sdma_kernel PRIVATE cxx_std_17)
target_compile_options(acltest_sdma_kernel PRIVATE -O2 -Wall -Wextra)
target_link_libraries(acltest_sdma_kernel PRIVATE
  $<BUILD_INTERFACE:ascend_hal_headers>
  ascend_hal
)

install(FILES ${ACLTEST_DIR}/device/libacltest_sdma_kernel.json
  DESTINATION opp/built-in/op_impl/aicpu/config
  COMPONENT acltest_sdma
)
install(TARGETS acltest_sdma_kernel DESTINATION acltest/lib COMPONENT acltest_sdma)

cann_pack_targets_and_files(
  OUTPUT_TARGET pack_acltest_sdma
  OUTPUT acltest-sdma-compat.tar.gz
  TARGETS acltest_sdma_kernel
  TAR_ROOT_DIR aicpu_kernels_device
)

add_cann_sign_file(
  INPUT "acltest-sdma-compat.tar.gz"
  CONFIG "${ACLTEST_DIR}/cmake/device/acltest_check_cfg.xml"
  RESULT_VAR acltest_sdma_sign_file
  VERSION "1.0.0"
  DEPENDS pack_acltest_sdma
)

install(FILES ${acltest_sdma_sign_file}
  DESTINATION opp/built-in/op_impl/aicpu/kernel
  COMPONENT acltest_sdma
)

set_cann_subprj_package()
