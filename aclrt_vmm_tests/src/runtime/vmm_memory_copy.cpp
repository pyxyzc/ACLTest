#include "vmm_memory_copy.h"
#include <iostream>
#include "console_utils.h"

namespace acltest::internal {

DeviceBuffer::~DeviceBuffer() { Cleanup(); }

bool DeviceBuffer::Allocate(size_t bytes)
{
    size = bytes;
    const aclError ret = aclrtMalloc(&ptr, size, ACL_MEM_MALLOC_HUGE_FIRST);
    allocated_ = (ret == ACL_SUCCESS);
    return LogAcl("aclrtMalloc(device buffer)", ret);
}

void DeviceBuffer::Cleanup()
{
    if (!allocated_ || ptr == nullptr) { return; }
    (void)LogAcl("cleanup aclrtFree(device buffer)", aclrtFree(ptr));
    ptr = nullptr;
    size = 0;
    allocated_ = false;
}

aclrtMemcpyKind CopyKind(const Endpoint& dst, const Endpoint& src)
{
    if (src.side == MemorySide::Host && dst.side == MemorySide::Host) {
        return ACL_MEMCPY_HOST_TO_HOST;
    }
    if (src.side == MemorySide::Host && dst.side == MemorySide::Device) {
        return ACL_MEMCPY_HOST_TO_DEVICE;
    }
    if (src.side == MemorySide::Device && dst.side == MemorySide::Host) {
        return ACL_MEMCPY_DEVICE_TO_HOST;
    }
    return ACL_MEMCPY_DEVICE_TO_DEVICE;
}

bool CopyEndpoint(const Endpoint& dst, const Endpoint& src, size_t copy_size,
                  const std::string& label)
{
    return CopyWithKind(dst.ptr, dst.size, src.ptr, copy_size, CopyKind(dst, src), label);
}

bool VerifyPattern(const std::vector<uint8_t>& data, uint32_t seed, const std::string& label)
{
    for (size_t i = 0; i < data.size(); ++i) {
        const uint8_t expected = static_cast<uint8_t>((seed + (i * 131U) + (i >> 3U)) & 0xffU);
        if (data[i] != expected) {
            std::cerr << "  " << label << " mismatch at offset=" << i
                      << ", expected=" << static_cast<int>(expected)
                      << ", actual=" << static_cast<int>(data[i]) << "\n";
            PrintRed("  " + label + " ×");
            return false;
        }
    }
    PrintGreen("  " + label + " √");
    return true;
}

}  // namespace acltest::internal
