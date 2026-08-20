#include <array>
#include <iostream>
#include "vmm_ipc_child_setup.h"
#include "vmm_pointer_access.h"

namespace acltest::internal {

int RunIpcHostPointerChild(int read_fd, int write_fd, const ShareMsg& share_msg)
{
    if (share_msg.test_kind != static_cast<uint32_t>(IpcTestKind::HostPointer) ||
        share_msg.handle_count != 1U ||
        share_msg.handle_memory_kinds[0] != static_cast<uint32_t>(IpcMemoryKind::HostPhysical)) {
        SendSetupReady(write_fd, false, ACL_ERROR_RT_PARAM_INVALID,
                       "child expected one host pointer mapping");
        return 1;
    }

    std::array<PhysicalMapping, kMaxSharedHandleCount> mappings;
    aclError failure_ret = ACL_ERROR_RT_PARAM_INVALID;
    if (!PrepareChildMappings(share_msg, &mappings, &failure_ret)) {
        SendSetupReady(write_fd, false, failure_ret, "child VMM setup failed");
        CleanupChildMappings(&mappings, share_msg.handle_count);
        return 1;
    }
    if (!SignalSetupReadyAndWaitForStart(read_fd, write_fd)) {
        CleanupChildMappings(&mappings, share_msg.handle_count);
        return 1;
    }

    const size_t test_size = static_cast<size_t>(share_msg.test_size);
    const PointerAccessResult read_result = VerifyPointerPattern(
        mappings[0].virt, test_size, share_msg.parent_seed, "child pointer read");
    PointerAccessResult write_result;
    if (read_result.ok) {
        write_result = WritePointerPattern(mappings[0].virt, test_size, share_msg.child_seed,
                                           "child pointer write");
    }

    const bool ok = read_result.ok && write_result.ok;
    const int signal_number =
        read_result.signal_number != 0 ? read_result.signal_number : write_result.signal_number;
    CleanupChildMappings(&mappings, share_msg.handle_count);
    SendChildResult(write_fd, ok, ok ? ACL_SUCCESS : ACL_ERROR_RT_PARAM_INVALID,
                    ok ? "child pointer access passed" : "child pointer access failed",
                    IpcResultPhase::Capability, signal_number);
    return ok ? 0 : 1;
}

}  // namespace acltest::internal
