/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ACLTEST_INCLUDE_ACLTEST_FABRIC_MEMORY_H_
#define ACLTEST_INCLUDE_ACLTEST_FABRIC_MEMORY_H_

#include <cstddef>
#include <cstdint>

#include "acl/acl.h"

namespace acltest {

// A compact single-process form of HIXL FabricMemAllocator +
// VirtualMemoryManager. It reserves two 1-GiB-aligned VMM slots and maps pinned
// host physical memory and HBM into them. The device access grant on the host
// mapping is required because the AICPU-generated SDMA SQEs use that VA.
class FabricMemoryPair {
 public:
  FabricMemoryPair() = default;
  ~FabricMemoryPair();
  FabricMemoryPair(const FabricMemoryPair &) = delete;
  FabricMemoryPair &operator=(const FabricMemoryPair &) = delete;

  void Initialize(size_t bytes, int32_t logic_device_id);
  void Reset() noexcept;

  void *host() const {
    return host_va_;
  }
  void *device() const {
    return device_va_;
  }
  size_t size() const {
    return bytes_;
  }

 private:
  static aclrtPhysicalMemProp DefaultPhysicalProperty();
  static aclrtDrvMemHandle AllocateHostPhysical(size_t bytes, int32_t logic_device_id);
  static aclrtDrvMemHandle AllocateDevicePhysical(size_t bytes, int32_t logic_device_id);
  static bool IsA3Soc();
  void ReserveArena(size_t bytes);

  void *arena_va_ = nullptr;
  size_t arena_bytes_ = 0U;
  size_t slot_bytes_ = 0U;
  size_t bytes_ = 0U;
  void *host_va_ = nullptr;
  void *device_va_ = nullptr;
  aclrtDrvMemHandle host_handle_ = nullptr;
  aclrtDrvMemHandle device_handle_ = nullptr;
};

}  // namespace acltest

#endif  // ACLTEST_INCLUDE_ACLTEST_FABRIC_MEMORY_H_
