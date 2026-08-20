cmake_minimum_required(VERSION 3.16)

project(aclrt_vmm_tests LANGUAGES CXX)

if(NOT UNIX)
    message(FATAL_ERROR "This test uses Linux process and pipe primitives.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CheckCXXSourceCompiles)

set(ASCEND_ROOT "" CACHE PATH "Path to Ascend toolkit root")

if(NOT ASCEND_ROOT)
    set(_ascend_root_candidates
        "$ENV{ASCEND_ROOT}"
        "$ENV{ASCEND_HOME}"
        "$ENV{ASCEND_TOOLKIT_HOME}"
        "$ENV{ASCEND_INSTALL_PATH}/ascend-toolkit/latest"
        "/usr/local/Ascend/ascend-toolkit/latest"
    )
    foreach(_candidate IN LISTS _ascend_root_candidates)
        if(_candidate AND EXISTS "${_candidate}")
            set(ASCEND_ROOT "${_candidate}" CACHE PATH "Path to Ascend toolkit root" FORCE)
            break()
        endif()
    endforeach()
endif()

find_path(ACL_INCLUDE_DIR
    NAMES acl/acl.h
    HINTS
        "${ASCEND_ROOT}/include"
        "$ENV{ASCEND_ROOT}/include"
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

set(CMAKE_REQUIRED_INCLUDES "${ACL_INCLUDE_DIR}")
check_cxx_source_compiles("
#include <acl/acl.h>
int main()
{
    using ExportFn = decltype(&aclrtMemExportToShareableHandleV2);
    using ImportFn = decltype(&aclrtMemImportFromShareableHandleV2);
    using SetPidFn = decltype(&aclrtMemSetPidToShareableHandleV2);
    ExportFn export_fn = nullptr;
    ImportFn import_fn = nullptr;
    SetPidFn set_pid_fn = nullptr;
    (void)export_fn;
    (void)import_fn;
    (void)set_pid_fn;
    return 0;
}
" ACLTEST_HAS_V2_SHARE_API)

check_cxx_source_compiles("
#include <acl/acl.h>
int main()
{
    aclrtMemSharedHandleType type = ACL_MEM_SHARE_HANDLE_TYPE_FABRIC;
    aclrtMemFabricHandle handle = {};
    (void)type;
    (void)handle;
    return 0;
}
" ACLTEST_HAS_FABRIC_SHARE_TYPE)

check_cxx_source_compiles("
#include <acl/acl.h>
int main()
{
    using VersionFn = decltype(&aclsysGetVersionStr);
    VersionFn version_fn = nullptr;
    char version_str[ACL_PKG_VERSION_MAX_SIZE] = {0};
    (void)version_fn;
    (void)version_str;
    return 0;
}
" ACLTEST_HAS_ACLSYS_GET_VERSION_STR)
unset(CMAKE_REQUIRED_INCLUDES)

add_library(aclrt_vmm_common STATIC
    src/app/vmm_test_common.cpp
    src/runtime/vmm_memory_utils.cpp
    src/runtime/vmm_memory_copy.cpp
    src/runtime/vmm_pointer_access.cpp
    src/app/vmm_single_process.cpp
    src/ipc/vmm_ipc.cpp
    src/ipc/vmm_ipc_transport.cpp
    src/ipc/vmm_ipc_process.cpp
    src/ipc/vmm_ipc_share.cpp
    src/ipc/vmm_ipc_endpoint.cpp
    src/ipc/vmm_ipc_parent.cpp
    src/ipc/vmm_ipc_child_ops.cpp
    src/ipc/vmm_ipc_child_setup.cpp
    src/ipc/vmm_ipc_pointer.cpp
    src/ipc/vmm_ipc_cases.cpp
)

target_include_directories(aclrt_vmm_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${ACL_INCLUDE_DIR}"
)

target_link_libraries(aclrt_vmm_common PUBLIC
    "${ASCENDCL_LIBRARY}"
    pthread
    dl
)

if(ACLTEST_HAS_FABRIC_SHARE_TYPE)
    target_compile_definitions(aclrt_vmm_common PUBLIC
        ACLTEST_HAS_FABRIC_SHARE_TYPE=1)
else()
    target_compile_definitions(aclrt_vmm_common PUBLIC
        ACLTEST_HAS_FABRIC_SHARE_TYPE=0)
endif()

if(ACLTEST_HAS_V2_SHARE_API)
    target_compile_definitions(aclrt_vmm_common PUBLIC
        ACLTEST_HAS_V2_SHARE_API=1)
else()
    target_compile_definitions(aclrt_vmm_common PUBLIC
        ACLTEST_HAS_V2_SHARE_API=0)
endif()

if(ACLTEST_HAS_ACLSYS_GET_VERSION_STR)
    target_compile_definitions(aclrt_vmm_common PUBLIC
        ACLTEST_HAS_ACLSYS_GET_VERSION_STR=1)
else()
    target_compile_definitions(aclrt_vmm_common PUBLIC
        ACLTEST_HAS_ACLSYS_GET_VERSION_STR=0)
endif()

function(add_vmm_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE aclrt_vmm_common)
endfunction()

add_vmm_test(aclrt_vmm_single_process_test
    src/app/vmm_single_process_test.cpp)
add_vmm_test(aclrt_vmm_device_ipc_test
    src/app/vmm_device_ipc_test.cpp)
add_vmm_test(aclrt_vmm_host_memcpy
    src/app/vmm_host_memcpy.cpp)
add_vmm_test(aclrt_vmm_host_pointer
    src/app/vmm_host_pointer.cpp)
add_vmm_test(aclrt_vmm_tests
    src/app/vmm_tests.cpp)

add_vmm_test(aclrt_vmm_ipc_child
    src/app/vmm_ipc_child.cpp)

message(STATUS "ACL include dir: ${ACL_INCLUDE_DIR}")
message(STATUS "AscendCL library: ${ASCENDCL_LIBRARY}")
message(STATUS "V2 shareable handle API: ${ACLTEST_HAS_V2_SHARE_API}")
message(STATUS "Fabric share handle type: ${ACLTEST_HAS_FABRIC_SHARE_TYPE}")
message(STATUS "CANN version string API: ${ACLTEST_HAS_ACLSYS_GET_VERSION_STR}")
