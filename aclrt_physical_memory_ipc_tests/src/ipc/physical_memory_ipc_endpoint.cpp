#include "physical_memory_ipc_endpoint.h"

namespace acltest::internal {

IpcEndpointKind EndpointKindFromWire(uint32_t value)
{
    switch (static_cast<IpcEndpointKind>(value)) {
        case IpcEndpointKind::ImportedDeviceVa:
        case IpcEndpointKind::ImportedHostVa:
        case IpcEndpointKind::DeviceBuffer:
        case IpcEndpointKind::HostBuffer: return static_cast<IpcEndpointKind>(value);
    }
    return IpcEndpointKind::HostBuffer;
}

IpcMemoryKind MemoryKindFromWire(uint32_t value)
{
    switch (static_cast<IpcMemoryKind>(value)) {
        case IpcMemoryKind::DevicePhysical:
        case IpcMemoryKind::HostPhysical: return static_cast<IpcMemoryKind>(value);
    }
    return IpcMemoryKind::DevicePhysical;
}

const char* EndpointName(IpcEndpointKind endpoint)
{
    switch (endpoint) {
        case IpcEndpointKind::ImportedDeviceVa: return "imported device VA";
        case IpcEndpointKind::ImportedHostVa: return "imported host VA";
        case IpcEndpointKind::DeviceBuffer: return "device buffer";
        case IpcEndpointKind::HostBuffer: return "host buffer";
    }
    return "unknown";
}

bool IsImportedEndpoint(IpcEndpointKind endpoint)
{
    return endpoint == IpcEndpointKind::ImportedDeviceVa ||
           endpoint == IpcEndpointKind::ImportedHostVa;
}

IpcMemoryKind ImportedMemoryKind(IpcEndpointKind endpoint)
{
    return endpoint == IpcEndpointKind::ImportedHostVa ? IpcMemoryKind::HostPhysical
                                                       : IpcMemoryKind::DevicePhysical;
}

MemorySide EndpointSide(IpcEndpointKind endpoint)
{
    return endpoint == IpcEndpointKind::ImportedDeviceVa ||
                   endpoint == IpcEndpointKind::DeviceBuffer
               ? MemorySide::Device
               : MemorySide::Host;
}

PhysicalMemoryConfig MakeConfigForKind(const Options& options, IpcMemoryKind memory_kind)
{
    return memory_kind == IpcMemoryKind::HostPhysical ? MakeHostConfig(options)
                                                      : MakeDeviceConfig(options);
}

PhysicalMemoryConfig MakeConfigForKind(const ShareMsg& share_msg, IpcMemoryKind memory_kind)
{
    Options options;
    options.device = share_msg.device;
    options.host_numa = share_msg.host_numa;
    options.requested_size = static_cast<size_t>(share_msg.test_size);
    return MakeConfigForKind(options, memory_kind);
}

std::string DirectionName(IpcEndpointKind source, IpcEndpointKind destination)
{
    return std::string(EndpointName(source)) + " -> " + EndpointName(destination);
}

bool CopyIpcEndpoint(const Endpoint& dst, const Endpoint& src, size_t copy_size,
                     const std::string& label_prefix)
{
    const std::string label =
        label_prefix + " aclrtMemcpy(" + std::string(src.name) + " -> " + dst.name + ")";
    return CopyEndpoint(dst, src, copy_size, label);
}

bool EndpointUsesDeviceBuffer(IpcEndpointKind source, IpcEndpointKind destination)
{
    return source == IpcEndpointKind::DeviceBuffer || destination == IpcEndpointKind::DeviceBuffer;
}

bool ValidImportedIndex(uint32_t index, const ShareMsg& share_msg)
{
    return index < share_msg.handle_count && index < kMaxSharedHandleCount;
}

uint32_t HandleIndexForEndpoint(const ShareMsg& share_msg, IpcEndpointKind endpoint, bool is_source)
{
    if (!IsImportedEndpoint(endpoint)) { return kInvalidHandleIndex; }
    return is_source ? share_msg.src_handle_index : share_msg.dst_handle_index;
}

Endpoint ResolveChildEndpoint(IpcEndpointKind endpoint, uint32_t handle_index,
                              std::array<PhysicalMapping, kMaxSharedHandleCount>* mappings,
                              DeviceBuffer* device_buffer, std::vector<uint8_t>* host_buffer)
{
    Endpoint resolved;
    resolved.name = EndpointName(endpoint);
    resolved.side = EndpointSide(endpoint);
    if (endpoint == IpcEndpointKind::DeviceBuffer) {
        resolved.ptr = device_buffer->ptr;
        resolved.size = device_buffer->size;
    } else if (endpoint == IpcEndpointKind::HostBuffer) {
        resolved.ptr = host_buffer->data();
        resolved.size = host_buffer->size();
    } else if (handle_index < kMaxSharedHandleCount) {
        resolved.ptr = (*mappings)[handle_index].virt;
        resolved.size = (*mappings)[handle_index].size;
    }
    return resolved;
}

}  // namespace acltest::internal
