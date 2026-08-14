#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "physical_memory_ipc_internal.h"

namespace acltest::internal {

IpcEndpointKind EndpointKindFromWire(uint32_t value);
IpcMemoryKind MemoryKindFromWire(uint32_t value);
const char* EndpointName(IpcEndpointKind endpoint);
bool IsImportedEndpoint(IpcEndpointKind endpoint);
IpcMemoryKind ImportedMemoryKind(IpcEndpointKind endpoint);
MemorySide EndpointSide(IpcEndpointKind endpoint);

PhysicalMemoryConfig MakeConfigForKind(const Options& options, IpcMemoryKind memory_kind);
PhysicalMemoryConfig MakeConfigForKind(const ShareMsg& share_msg, IpcMemoryKind memory_kind);

std::string DirectionName(IpcEndpointKind source, IpcEndpointKind destination);
bool CopyIpcEndpoint(const Endpoint& dst, const Endpoint& src, size_t copy_size,
                     const std::string& label_prefix);
bool EndpointUsesDeviceBuffer(IpcEndpointKind source, IpcEndpointKind destination);
bool ValidImportedIndex(uint32_t index, const ShareMsg& share_msg);
uint32_t HandleIndexForEndpoint(const ShareMsg& share_msg, IpcEndpointKind endpoint,
                                bool is_source);

Endpoint ResolveChildEndpoint(IpcEndpointKind endpoint, uint32_t handle_index,
                              std::array<PhysicalMapping, kMaxSharedHandleCount>* mappings,
                              DeviceBuffer* device_buffer, std::vector<uint8_t>* host_buffer);

}  // namespace acltest::internal
