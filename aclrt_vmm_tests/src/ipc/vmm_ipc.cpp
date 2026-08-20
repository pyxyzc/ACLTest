#include "vmm_ipc_internal.h"
#include "vmm_test_common.h"

namespace acltest {

bool RunVmmDeviceIpcTest(const Options& options)
{
    return internal::RunVmmDeviceIpcEndpointTests(options);
}

bool RunVmmHostIpcTest(const Options& options)
{
    return internal::RunVmmHostIpcEndpointTests(options);
}

bool RunVmmDeviceHostIpcTest(const Options& options)
{
    return internal::RunVmmDeviceHostIpcEndpointTest(options);
}

}  // namespace acltest
