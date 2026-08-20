#include "vmm_ipc_child_setup.h"
#include <iostream>
#include "vmm_ipc_endpoint.h"

namespace acltest::internal {

bool PrepareChildMappings(const ShareMsg& share_msg,
                          std::array<PhysicalMapping, kMaxSharedHandleCount>* mappings,
                          aclError* failure_ret)
{
    if (failure_ret != nullptr) { *failure_ret = ACL_ERROR_RT_PARAM_INVALID; }
    if (share_msg.handle_count == 0U || share_msg.handle_count > kMaxSharedHandleCount) {
        std::cerr << "  child received invalid handle_count=" << share_msg.handle_count << "\n";
        return false;
    }

    for (uint32_t i = 0; i < share_msg.handle_count; ++i) {
        const auto memory_kind = MemoryKindFromWire(share_msg.handle_memory_kinds[i]);
        const auto config = MakeConfigForKind(share_msg, memory_kind);
        if (!ImportAndMapSharedHandle(share_msg, i, config, &(*mappings)[i], failure_ret)) {
            return false;
        }
    }
    return true;
}

void CleanupChildMappings(std::array<PhysicalMapping, kMaxSharedHandleCount>* mappings,
                          uint32_t handle_count)
{
    for (uint32_t i = 0; i < handle_count && i < kMaxSharedHandleCount; ++i) {
        (*mappings)[i].Cleanup();
    }
}

bool SignalSetupReadyAndWaitForStart(int read_fd, int write_fd)
{
    SendSetupReady(write_fd, true, ACL_SUCCESS, "child VMM setup ready");
    if (WaitForStartMessage(read_fd)) { return true; }
    std::cerr << "  child did not receive capability start message\n";
    return false;
}

}  // namespace acltest::internal
