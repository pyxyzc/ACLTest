#pragma once

#include "physical_memory_ipc_internal.h"

namespace acltest::internal {

bool RunIpcCopyDirection(const Options& options, IpcEndpointKind source,
                         IpcEndpointKind destination, uint32_t seed);

}  // namespace acltest::internal
