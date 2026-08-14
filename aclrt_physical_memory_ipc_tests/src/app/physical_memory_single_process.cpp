#include "physical_memory_common.h"
#include "physical_memory_copy.h"

#include "console_utils.h"

#include <string>
#include <vector>

namespace acltest {
using namespace internal;
namespace {

enum class EndpointKind {
    DeviceVa,
    HostVa,
    DeviceBuffer,
    HostBuffer,
};

struct CopyCase {
    const char* title;
    EndpointKind source;
    EndpointKind destination;
    uint32_t seed_base;
};

const char* EndpointName(EndpointKind kind)
{
    switch (kind) {
        case EndpointKind::DeviceVa:
            return "device VA";
        case EndpointKind::HostVa:
            return "host VA";
        case EndpointKind::DeviceBuffer:
            return "device buffer";
        case EndpointKind::HostBuffer:
            return "host buffer";
    }
    return "unknown";
}

bool IsDeviceBuffer(EndpointKind kind)
{
    return kind == EndpointKind::DeviceBuffer;
}

bool IsVirtualMapping(EndpointKind kind)
{
    return kind == EndpointKind::DeviceVa || kind == EndpointKind::HostVa;
}

bool AllocateMapping(const Options& options, EndpointKind kind,
                     PhysicalMapping* mapping)
{
    const auto config = kind == EndpointKind::HostVa
                            ? MakeHostConfig(options)
                            : MakeDeviceConfig(options);
    size_t aligned_size = 0;
    return QueryAlignedSize(config, options.requested_size, &aligned_size) &&
           AllocateAndMapPhysical(config, aligned_size, mapping);
}

Endpoint ResolveEndpoint(EndpointKind kind, PhysicalMapping* mapping,
                         DeviceBuffer* device_buffer,
                         std::vector<uint8_t>* host_buffer)
{
    Endpoint endpoint;
    endpoint.name = EndpointName(kind);
    switch (kind) {
        case EndpointKind::DeviceVa:
            endpoint.ptr = mapping->virt;
            endpoint.size = mapping->size;
            endpoint.side = MemorySide::Device;
            break;
        case EndpointKind::HostVa:
            endpoint.ptr = mapping->virt;
            endpoint.size = mapping->size;
            endpoint.side = MemorySide::Host;
            break;
        case EndpointKind::DeviceBuffer:
            endpoint.ptr = device_buffer->ptr;
            endpoint.size = device_buffer->size;
            endpoint.side = MemorySide::Device;
            break;
        case EndpointKind::HostBuffer:
            endpoint.ptr = host_buffer->data();
            endpoint.size = host_buffer->size();
            endpoint.side = MemorySide::Host;
            break;
    }
    return endpoint;
}

std::string DirectionName(EndpointKind source, EndpointKind destination)
{
    return std::string(EndpointName(source)) + " -> " + EndpointName(destination);
}

std::string MemcpyLabel(const char* phase, const Endpoint& source,
                        const Endpoint& destination)
{
    return "aclrtMemcpy(" + std::string(phase) + " " + source.name + " -> " +
           destination.name + ")";
}

bool RunDirection(const Options& options, EndpointKind source_kind,
                  EndpointKind destination_kind, PhysicalMapping* source_mapping,
                  PhysicalMapping* destination_mapping, DeviceBuffer* device_buffer,
                  uint32_t seed)
{
    std::vector<uint8_t> expected = MakePattern(options.requested_size, seed);
    std::vector<uint8_t> actual(options.requested_size, 0);
    Endpoint source = ResolveEndpoint(source_kind, source_mapping, device_buffer,
                                      &expected);
    Endpoint destination = ResolveEndpoint(destination_kind, destination_mapping,
                                           device_buffer, &actual);

    bool ok = true;
    if (source_kind != EndpointKind::HostBuffer) {
        const Endpoint host_source{"host buffer", expected.data(), expected.size(),
                                   MemorySide::Host};
        ok = CopyEndpoint(source, host_source, expected.size(),
                          MemcpyLabel("setup", host_source, source));
    }
    if (ok) {
        ok = CopyEndpoint(destination, source, expected.size(),
                          MemcpyLabel("copy", source, destination));
    }
    if (ok && destination_kind != EndpointKind::HostBuffer) {
        const Endpoint host_destination{"host buffer", actual.data(), actual.size(),
                                        MemorySide::Host};
        ok = CopyEndpoint(host_destination, destination, actual.size(),
                          MemcpyLabel("readback", destination, host_destination));
    }

    const std::string direction = DirectionName(source_kind, destination_kind);
    if (!ok) {
        PrintRed("  " + direction + " ×");
        return false;
    }
    return VerifyPattern(actual, seed, direction);
}

bool RunCase(const Options& options, const CopyCase& test_case)
{
    std::cout << "\n";
    PrintRed(test_case.title);

    PhysicalMapping source_mapping;
    PhysicalMapping destination_mapping;
    DeviceBuffer device_buffer;

    bool ok = true;
    if (IsVirtualMapping(test_case.source)) {
        ok = AllocateMapping(options, test_case.source, &source_mapping);
    }
    if (ok && IsVirtualMapping(test_case.destination)) {
        ok = AllocateMapping(options, test_case.destination, &destination_mapping);
    }
    if (ok && (IsDeviceBuffer(test_case.source) ||
               IsDeviceBuffer(test_case.destination))) {
        ok = device_buffer.Allocate(options.requested_size);
    }
    if (!ok) {
        return false;
    }

    ok = RunDirection(options, test_case.source, test_case.destination,
                      &source_mapping, &destination_mapping, &device_buffer,
                      test_case.seed_base + 1U);
    if (ok) {
        ok = RunDirection(options, test_case.destination, test_case.source,
                          &destination_mapping, &source_mapping, &device_buffer,
                          test_case.seed_base + 2U);
    }
    return ok;
}

constexpr CopyCase kCopyCases[] = {
    {"[single-process] device VA <-> device buffer", EndpointKind::DeviceVa,
     EndpointKind::DeviceBuffer, 0x1100U},
    {"[single-process] device VA <-> host buffer", EndpointKind::DeviceVa,
     EndpointKind::HostBuffer, 0x1200U},
    {"[single-process] device VA <-> device VA", EndpointKind::DeviceVa,
     EndpointKind::DeviceVa, 0x1300U},
    {"[single-process] host VA <-> device buffer", EndpointKind::HostVa,
     EndpointKind::DeviceBuffer, 0x1400U},
    {"[single-process] host VA <-> host buffer", EndpointKind::HostVa,
     EndpointKind::HostBuffer, 0x1500U},
    {"[single-process] host VA <-> host VA", EndpointKind::HostVa,
     EndpointKind::HostVa, 0x1600U},
    {"[single-process] device VA <-> host VA", EndpointKind::DeviceVa,
     EndpointKind::HostVa, 0x1700U},
};

}  // namespace

bool RunSingleProcessVmmTest(const Options& options)
{
    AclRuntime runtime;
    if (!runtime.Init() || !runtime.SetDevice(options.device)) {
        return false;
    }

    bool ok = true;
    for (const auto& test_case : kCopyCases) {
        ok = RunCase(options, test_case) && ok;
    }
    return ok;
}

}  // namespace acltest
