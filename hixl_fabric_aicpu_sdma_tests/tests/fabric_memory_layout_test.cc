/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "acltest/fabric_memory_layout.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
  using acltest::AlignFabricMemorySize;
  using acltest::IsFabricMapAddressAligned;
  using acltest::kFabricMapAddressAlignment;
  using acltest::kFabricSlotBytes;

  assert(AlignFabricMemorySize(1U) == kFabricSlotBytes);
  assert(AlignFabricMemorySize(256ULL * 1024ULL * 1024ULL) == kFabricSlotBytes);
  assert(AlignFabricMemorySize(kFabricSlotBytes) == kFabricSlotBytes);
  assert(AlignFabricMemorySize(kFabricSlotBytes + 1U) == 2U * kFabricSlotBytes);
  assert(IsFabricMapAddressAligned(40ULL * 1024ULL * kFabricSlotBytes));
  assert(!IsFabricMapAddressAligned(kFabricMapAddressAlignment + 1U));

  bool overflow_detected = false;
  try {
    (void)AlignFabricMemorySize(std::numeric_limits<size_t>::max());
  } catch (const std::overflow_error &) {
    overflow_detected = true;
  }
  assert(overflow_detected);
  std::cout << "fabric_memory_layout_test: PASS\n";
  return 0;
}
