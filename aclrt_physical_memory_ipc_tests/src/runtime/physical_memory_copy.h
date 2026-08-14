#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "physical_memory_utils.h"

namespace acltest::internal {

enum class MemorySide {
    Host,
    Device,
};

struct Endpoint {
    const char* name = "";
    void* ptr = nullptr;
    size_t size = 0;
    MemorySide side = MemorySide::Host;
};

class DeviceBuffer {
public:
    DeviceBuffer() = default;
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    ~DeviceBuffer();

    bool Allocate(size_t bytes);
    void Cleanup();

    void* ptr = nullptr;
    size_t size = 0;

private:
    bool allocated_ = false;
};

aclrtMemcpyKind CopyKind(const Endpoint& dst, const Endpoint& src);

bool CopyEndpoint(const Endpoint& dst, const Endpoint& src, size_t copy_size,
                  const std::string& label);
bool VerifyPattern(const std::vector<uint8_t>& data, uint32_t seed, const std::string& label);

}  // namespace acltest::internal
