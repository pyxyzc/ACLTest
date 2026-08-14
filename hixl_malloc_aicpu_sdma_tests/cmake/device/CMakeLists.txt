# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# Licensed under the CANN Open Software License Agreement Version 2.0.
# See LICENSE in the root of the software repository for the full text.
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
include(../fetch_cann_cmake.cmake)
project(device_acltest_malloc_sdma LANGUAGES C CXX)

init_cann_project(PRODUCT_SIDE "device")
add_definitions(-DPRODUCT_SIDE_DEVICE -D_GLIBCXX_USE_CXX11_ABI=0)
set(USE_CXX11_ABI OFF)
add_cann_target_options()

get_filename_component(ACLTEST_MALLOC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(ACLTEST_COMMON_DIR
  "${ACLTEST_MALLOC_DIR}/../hixl_fabric_aicpu_sdma_tests" ABSOLUTE)
include(${ACLTEST_MALLOC_DIR}/version.cmake)
include(${ACLTEST_MALLOC_DIR}/cmake/locate_ascend.cmake)
acltest_find_ascend_install_path(ASCEND_INSTALL_PATH)
if(NOT ASCEND_INSTALL_PATH)
  message(FATAL_ERROR
    "CANN was not found under /usr/local/Ascend. Set ASCEND_HOME_PATH to its root.")
endif()
message(STATUS "Using CANN installation: ${ASCEND_INSTALL_PATH}")

set(CMAKE_MODULE_PATH ${CANN_CMAKE_DIR}/modules ${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${ASCEND_INSTALL_PATH} ${CMAKE_PREFIX_PATH})
acltest_find_cann_package(ascend_hal MODULE REQUIRED)

add_library(acltest_malloc_sdma_kernel SHARED
  ${ACLTEST_COMMON_DIR}/device/kernel.cc
  ${ACLTEST_COMMON_DIR}/device/stars_sdma.cc
  ${ACLTEST_COMMON_DIR}/device/sync_transfer_context.cc
  ${ACLTEST_COMMON_DIR}/device/transfer_context_manager.cc
)
set_target_properties(acltest_malloc_sdma_kernel PROPERTIES
  OUTPUT_NAME acltest_malloc_sdma_kernel)
target_include_directories(acltest_malloc_sdma_kernel PRIVATE
  ${ACLTEST_COMMON_DIR}/include
  ${ACLTEST_COMMON_DIR}/device
)
target_compile_features(acltest_malloc_sdma_kernel PRIVATE cxx_std_17)
target_compile_options(acltest_malloc_sdma_kernel PRIVATE -O2 -Wall -Wextra)
target_link_libraries(acltest_malloc_sdma_kernel PRIVATE
  $<BUILD_INTERFACE:ascend_hal_headers>
  ascend_hal
)

install(FILES ${ACLTEST_MALLOC_DIR}/device/libacltest_malloc_sdma_kernel.json
  DESTINATION opp/built-in/op_impl/aicpu/config
  COMPONENT acltest_malloc_sdma
)
install(TARGETS acltest_malloc_sdma_kernel
  DESTINATION acltest_malloc/lib
  COMPONENT acltest_malloc_sdma
)

cann_pack_targets_and_files(
  OUTPUT_TARGET pack_acltest_malloc_sdma
  OUTPUT acltest-malloc-sdma-compat.tar.gz
  TARGETS acltest_malloc_sdma_kernel
  TAR_ROOT_DIR aicpu_kernels_device
)

add_cann_sign_file(
  INPUT "acltest-malloc-sdma-compat.tar.gz"
  CONFIG "${ACLTEST_MALLOC_DIR}/cmake/device/acltest_malloc_check_cfg.xml"
  RESULT_VAR acltest_malloc_sdma_sign_file
  VERSION "1.0.0"
  DEPENDS pack_acltest_malloc_sdma
)

install(FILES ${acltest_malloc_sdma_sign_file}
  DESTINATION opp/built-in/op_impl/aicpu/kernel
  COMPONENT acltest_malloc_sdma
)

set_cann_subprj_package()
