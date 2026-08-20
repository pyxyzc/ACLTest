#pragma once

#include <array>
#include "vmm_ipc_internal.h"

namespace acltest::internal {

bool PrepareChildMappings(const ShareMsg& share_msg,
                          std::array<PhysicalMapping, kMaxSharedHandleCount>* mappings,
                          aclError* failure_ret);
void CleanupChildMappings(std::array<PhysicalMapping, kMaxSharedHandleCount>* mappings,
                          uint32_t handle_count);
bool SignalSetupReadyAndWaitForStart(int read_fd, int write_fd);

}  // namespace acltest::internal
