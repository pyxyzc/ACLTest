/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ACLTEST_INCLUDE_ACLTEST_FABRIC_MEMORY_LAYOUT_H_
#define ACLTEST_INCLUDE_ACLTEST_FABRIC_MEMORY_LAYOUT_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace acltest {

constexpr size_t kFabricSlotBytes = 1024ULL * 1024ULL * 1024ULL;
constexpr size_t kFabricMapAddressAlignment = 2ULL * 1024ULL * 1024ULL;
static_assert(kFabricSlotBytes % kFabricMapAddressAlignment == 0U,
              "fabric slots must preserve MapMem address alignment");

inline size_t AlignFabricMemorySize(size_t bytes) {
  if (bytes > std::numeric_limits<size_t>::max() - (kFabricSlotBytes - 1U)) {
    throw std::overflow_error("fabric memory size cannot be rounded to a 1-GiB VMM slot");
  }
  return ((bytes + kFabricSlotBytes - 1U) / kFabricSlotBytes) * kFabricSlotBytes;
}

inline bool IsFabricMapAddressAligned(uintptr_t address) {
  return address % kFabricMapAddressAlignment == 0U;
}

}  // namespace acltest

#endif  // ACLTEST_INCLUDE_ACLTEST_FABRIC_MEMORY_LAYOUT_H_
