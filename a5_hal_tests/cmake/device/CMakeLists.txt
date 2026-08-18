# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# ----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
include(../fetch_cann_cmake.cmake)
project(device_acltest_a5_hal_test LANGUAGES C CXX)

init_cann_project(PRODUCT_SIDE "device")
add_definitions(-DPRODUCT_SIDE_DEVICE -D_GLIBCXX_USE_CXX11_ABI=0)
set(USE_CXX11_ABI OFF)
set(CMAKE_SKIP_RPATH TRUE)
add_cann_target_options()

get_filename_component(ACLTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include(${ACLTEST_DIR}/cmake/locate_ascend.cmake)
acltest_find_ascend_install_path(ASCEND_INSTALL_PATH)
if(NOT ASCEND_INSTALL_PATH)
  message(FATAL_ERROR "CANN was not found under /usr/local/Ascend. Set ASCEND_HOME_PATH to its root.")
endif()
set(CMAKE_MODULE_PATH ${CANN_CMAKE_DIR}/modules ${CMAKE_MODULE_PATH})
set(CMAKE_PREFIX_PATH ${ASCEND_INSTALL_PATH} ${CMAKE_PREFIX_PATH})

acltest_find_cann_package(ascend_hal MODULE REQUIRED)

add_library(acltest_a5_hal_test SHARED ${ACLTEST_DIR}/device/a5_hal_test.cc)
set_target_properties(acltest_a5_hal_test PROPERTIES OUTPUT_NAME acltest_a5_hal_test)
target_include_directories(acltest_a5_hal_test PRIVATE ${ACLTEST_DIR}/include)
target_compile_features(acltest_a5_hal_test PRIVATE cxx_std_17)
target_compile_options(acltest_a5_hal_test PRIVATE -O2 -Wall -Wextra)
target_link_libraries(acltest_a5_hal_test PRIVATE
  $<BUILD_INTERFACE:ascend_hal_headers>
  ascend_hal
)

install(FILES ${ACLTEST_DIR}/device/libacltest_a5_hal_test.json
  DESTINATION opp/built-in/op_impl/aicpu/config
  COMPONENT acltest_a5_hal_test
)
install(TARGETS acltest_a5_hal_test DESTINATION acltest/lib COMPONENT acltest_a5_hal_test)

cann_pack_targets_and_files(
  OUTPUT_TARGET pack_acltest_a5_hal_test
  OUTPUT acltest-a5-hal-test-compat.tar.gz
  TARGETS acltest_a5_hal_test
  TAR_ROOT_DIR aicpu_kernels_device
)

add_cann_sign_file(
  INPUT "acltest-a5-hal-test-compat.tar.gz"
  CONFIG "${ACLTEST_DIR}/cmake/device/acltest_a5_hal_test_check_cfg.xml"
  RESULT_VAR acltest_a5_hal_test_sign_file
  VERSION "1.0.0"
  DEPENDS pack_acltest_a5_hal_test
)

install(FILES ${acltest_a5_hal_test_sign_file}
  DESTINATION opp/built-in/op_impl/aicpu/kernel
  COMPONENT acltest_a5_hal_test
)

set_cann_subprj_package()
