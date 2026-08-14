cmake_minimum_required(VERSION 3.16)

project(aclrt_memcpy_batch_async_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ASCEND_ROOT "" CACHE PATH "Path to the Ascend Toolkit root")

if(NOT ASCEND_ROOT)
    set(_ascend_root_candidates
        "$ENV{ASCEND_ROOT}"
        "$ENV{ASCEND_HOME_PATH}"
        "$ENV{ASCEND_HOME}"
        "$ENV{ASCEND_TOOLKIT_HOME}"
        "$ENV{ASCEND_INSTALL_PATH}/ascend-toolkit/latest"
        "/usr/local/Ascend/ascend-toolkit/latest"
    )
    foreach(_candidate IN LISTS _ascend_root_candidates)
        if(_candidate AND EXISTS "${_candidate}")
            set(ASCEND_ROOT "${_candidate}" CACHE PATH "Path to the Ascend Toolkit root" FORCE)
            break()
        endif()
    endforeach()
endif()

find_path(ACL_INCLUDE_DIR
    NAMES acl/acl.h
    HINTS
        "${ASCEND_ROOT}/include"
        "$ENV{ASCEND_ROOT}/include"
        "$ENV{ASCEND_HOME_PATH}/include"
        "$ENV{ASCEND_HOME}/include"
        "$ENV{ASCEND_TOOLKIT_HOME}/include"
        "$ENV{ASCEND_INSTALL_PATH}/ascend-toolkit/latest/include"
        "/usr/local/Ascend/ascend-toolkit/latest/include"
)

find_library(ASCENDCL_LIBRARY
    NAMES ascendcl
    HINTS
        "${ASCEND_ROOT}/lib64"
        "${ASCEND_ROOT}/lib64/stub"
        "$ENV{ASCEND_ROOT}/lib64"
        "$ENV{ASCEND_HOME_PATH}/lib64"
        "$ENV{ASCEND_HOME}/lib64"
        "$ENV{ASCEND_TOOLKIT_HOME}/lib64"
        "$ENV{ASCEND_INSTALL_PATH}/ascend-toolkit/latest/lib64"
        "/usr/local/Ascend/ascend-toolkit/latest/lib64"
)

if(NOT ACL_INCLUDE_DIR)
    message(FATAL_ERROR
        "Cannot find acl/acl.h. Pass -DASCEND_ROOT=/path/to/ascend-toolkit/latest")
endif()

if(NOT ASCENDCL_LIBRARY)
    message(FATAL_ERROR
        "Cannot find libascendcl.so. Pass -DASCEND_ROOT=/path/to/ascend-toolkit/latest")
endif()

include(CheckCXXSourceCompiles)
set(_saved_try_compile_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_REQUIRED_INCLUDES "${ACL_INCLUDE_DIR}")
check_cxx_source_compiles([[#include <acl/acl.h>
#include <cstddef>
using BatchFn = aclError (*)(void**, size_t*, void**, size_t*, size_t,
                             aclrtMemcpyBatchAttr*, size_t*, size_t, size_t*, aclrtStream);
using CreateEventFn = aclError (*)(aclrtEvent*, uint32_t);
using ElapsedFn = aclError (*)(float*, aclrtEvent, aclrtEvent);
using SyncFn = aclError (*)(aclrtStream, int32_t);
BatchFn batch_fn = static_cast<BatchFn>(&aclrtMemcpyBatchAsync);
CreateEventFn create_event_fn = &aclrtCreateEventExWithFlag;
ElapsedFn elapsed_fn = &aclrtEventElapsedTime;
SyncFn sync_fn = &aclrtSynchronizeStreamWithTimeout;
int main() {
    return batch_fn == nullptr || create_event_fn == nullptr || elapsed_fn == nullptr ||
           sync_fn == nullptr || ACL_EVENT_TIME_LINE == 0U;
}
]] ACLTEST_HAS_REQUIRED_BENCHMARK_APIS)
set(CMAKE_REQUIRED_INCLUDES)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_saved_try_compile_target_type}")

if(NOT ACLTEST_HAS_REQUIRED_BENCHMARK_APIS)
    message(FATAL_ERROR
        "The selected CANN headers do not provide the batch memcpy and timeline Event APIs")
endif()

add_executable(aclrt_memcpy_batch_bench
    src/main.cpp
)

target_include_directories(aclrt_memcpy_batch_bench PRIVATE
    "${ACL_INCLUDE_DIR}"
)

target_compile_options(aclrt_memcpy_batch_bench PRIVATE
    -O2
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(aclrt_memcpy_batch_bench PRIVATE
    "${ASCENDCL_LIBRARY}"
)

if(UNIX)
    target_link_libraries(aclrt_memcpy_batch_bench PRIVATE pthread dl)
endif()

message(STATUS "ACL include dir: ${ACL_INCLUDE_DIR}")
message(STATUS "AscendCL library: ${ASCENDCL_LIBRARY}")
