#pragma once

#include "vmm_ipc_internal.h"

namespace acltest::internal {

bool RunIpcMemcpyDirection(const Options& options, IpcEndpointKind source,
                           IpcEndpointKind destination, uint32_t seed);
bool RunIpcHostPointer(const Options& options, uint32_t parent_seed, uint32_t child_seed);

}  // namespace acltest::internal
