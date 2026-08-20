#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace acltest::internal {

struct PointerAccessResult {
    bool ok = false;
    int signal_number = 0;
    size_t mismatch_offset = 0;
    uint8_t expected = 0;
    uint8_t actual = 0;
};

PointerAccessResult WritePointerPattern(void* ptr, size_t size, uint32_t seed,
                                        const std::string& label);
PointerAccessResult VerifyPointerPattern(const void* ptr, size_t size, uint32_t seed,
                                         const std::string& label);

}  // namespace acltest::internal
