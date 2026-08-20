#include "vmm_ipc_internal.h"
#include "vmm_test_common.h"

namespace acltest {

bool RunVmmDeviceIpcTest(const Options& options)
{
    return internal::RunVmmDeviceIpcEndpointTests(options);
}

bool RunVmmHostMemcpyTest(const Options& options)
{
    return internal::RunVmmHostMemcpyEndpointTests(options);
}

bool RunVmmHostPointerTest(const Options& options)
{
    return internal::RunVmmHostPointerEndpointTest(options);
}

bool RunVmmDeviceHostIpcTest(const Options& options)
{
    return internal::RunVmmDeviceHostIpcEndpointTest(options);
}

}  // namespace acltest
