/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "acltest/fabric_memory.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "acltest/acl_check.h"

extern "C" {
__attribute__((weak)) aclError aclrtReserveMemAddressNoUCMemory(void **vir_ptr, size_t size, size_t alignment,
                                                                void *expect_ptr, uint64_t flags);
}

namespace acltest {
namespace {

constexpr size_t kBlockBytes = 1024ULL * 1024ULL * 1024ULL;
constexpr uintptr_t kPreferredArenaStart = 40ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t kHugePageReserveFlag = 1U;
constexpr int32_t kDevicesPerChip = 4;
constexpr int32_t kNumaNodeStep = 2;
constexpr size_t kOneAccessDescriptor = 1U;

size_t RoundUpToBlock(size_t bytes) {
  if (bytes > std::numeric_limits<size_t>::max() - (kBlockBytes - 1U)) {
    throw std::overflow_error("fabric memory size cannot be rounded to a 1-GiB VMM slot");
  }
  return ((bytes + kBlockBytes - 1U) / kBlockBytes) * kBlockBytes;
}

void LogCleanupError(const char *operation, aclError error) noexcept {
  if (error != ACL_SUCCESS) {
    std::cerr << "warning: " << operation << " failed during cleanup, aclError=" << error << '\n';
  }
}

}  // namespace

FabricMemoryPair::~FabricMemoryPair() {
  Reset();
}

aclrtPhysicalMemProp FabricMemoryPair::DefaultPhysicalProperty() {
  aclrtPhysicalMemProp property{};
  property.handleType = ACL_MEM_HANDLE_TYPE_NONE;
  property.allocationType = ACL_MEM_ALLOCATION_TYPE_PINNED;
  property.reserve = 0U;
  return property;
}

aclrtDrvMemHandle FabricMemoryPair::AllocateDevicePhysical(size_t bytes, int32_t logic_device_id) {
  aclrtPhysicalMemProp property = DefaultPhysicalProperty();
  property.memAttr = ACL_HBM_MEM_HUGE;
  property.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
  property.location.id = static_cast<uint32_t>(logic_device_id);
  aclrtDrvMemHandle handle = nullptr;
  ACLTEST_CHECK_ACL(aclrtMallocPhysical(&handle, bytes, &property, 0U));
  return handle;
}

aclrtDrvMemHandle FabricMemoryPair::AllocateHostPhysical(size_t bytes, int32_t logic_device_id) {
  int32_t physical_device_id = -1;
  ACLTEST_CHECK_ACL(aclrtGetPhyDevIdByLogicDevId(logic_device_id, &physical_device_id));
  if (physical_device_id < 0) {
    throw std::runtime_error("aclrtGetPhyDevIdByLogicDevId returned a negative physical device id");
  }

  aclrtPhysicalMemProp property = DefaultPhysicalProperty();
  property.memAttr = ACL_MEM_P2P_HUGE1G;
  property.location.type = ACL_MEM_LOCATION_TYPE_HOST_NUMA;
  property.location.id = static_cast<uint32_t>((physical_device_id / kDevicesPerChip) * kNumaNodeStep);
  aclrtDrvMemHandle handle = nullptr;
  aclError error = aclrtMallocPhysical(&handle, bytes, &property, 0U);
  if (error == ACL_SUCCESS) {
    return handle;
  }

  handle = nullptr;
  property.location.type = ACL_MEM_LOCATION_TYPE_HOST;
  property.location.id = 0U;
  error = aclrtMallocPhysical(&handle, bytes, &property, 0U);
  if (error == ACL_SUCCESS) {
    return handle;
  }

  handle = nullptr;
  property.memAttr = ACL_MEM_P2P_HUGE;
  ACLTEST_CHECK_ACL(aclrtMallocPhysical(&handle, bytes, &property, 0U));
  return handle;
}

bool FabricMemoryPair::IsA3Soc() {
  const char *name = aclrtGetSocName();
  if (name == nullptr) {
    return false;
  }
  constexpr std::array<const char *, 6U> kA3Names = {
      "Ascend910_9391", "Ascend910_9381", "Ascend910_9392",
      "Ascend910_9382", "Ascend910_9372", "Ascend910_9362",
  };
  return std::any_of(kA3Names.begin(), kA3Names.end(),
                     [name](const char *candidate) { return name == std::string(candidate); });
}

void FabricMemoryPair::ReserveArena(size_t bytes) {
  slot_bytes_ = RoundUpToBlock(bytes);
  if (slot_bytes_ > std::numeric_limits<size_t>::max() / 2U) {
    throw std::overflow_error("fabric VMM arena size overflows");
  }
  arena_bytes_ = slot_bytes_ * 2U;
  if (IsA3Soc() && aclrtReserveMemAddressNoUCMemory != nullptr) {
    const aclError error = aclrtReserveMemAddressNoUCMemory(
        &arena_va_, arena_bytes_, 0U, reinterpret_cast<void *>(kPreferredArenaStart), kHugePageReserveFlag);
    if (error == ACL_SUCCESS) {
      return;
    }
    std::cerr << "warning: aclrtReserveMemAddressNoUCMemory failed with " << error
              << "; falling back to aclrtReserveMemAddress\n";
  }
  ACLTEST_CHECK_ACL(aclrtReserveMemAddress(&arena_va_, arena_bytes_, 0U, nullptr, kHugePageReserveFlag));
}

void FabricMemoryPair::Initialize(size_t bytes, int32_t logic_device_id) {
  if (bytes == 0U || logic_device_id < 0) {
    throw std::invalid_argument("fabric memory requires non-zero bytes and a non-negative device id");
  }
  if (arena_va_ != nullptr) {
    throw std::logic_error("fabric memory pair is already initialized");
  }

  try {
    ReserveArena(bytes);
    bytes_ = bytes;
    host_handle_ = AllocateHostPhysical(bytes, logic_device_id);
    host_va_ = arena_va_;
    ACLTEST_CHECK_ACL(aclrtMapMem(host_va_, bytes, 0U, host_handle_, 0U));

    aclrtMemAccessDesc access{};
    access.flags = ACL_RT_MEM_ACCESS_FLAGS_READWRITE;
    access.location.type = ACL_MEM_LOCATION_TYPE_DEVICE;
    access.location.id = static_cast<uint32_t>(logic_device_id);
    ACLTEST_CHECK_ACL(aclrtMemSetAccess(host_va_, bytes, &access, kOneAccessDescriptor));

    device_handle_ = AllocateDevicePhysical(bytes, logic_device_id);
    device_va_ = static_cast<uint8_t *>(arena_va_) + slot_bytes_;
    ACLTEST_CHECK_ACL(aclrtMapMem(device_va_, bytes, 0U, device_handle_, 0U));
  } catch (...) {
    Reset();
    throw;
  }
}

void FabricMemoryPair::Reset() noexcept {
  if (device_va_ != nullptr) {
    LogCleanupError("aclrtUnmapMem(device)", aclrtUnmapMem(device_va_));
    device_va_ = nullptr;
  }
  if (device_handle_ != nullptr) {
    LogCleanupError("aclrtFreePhysical(device)", aclrtFreePhysical(device_handle_));
    device_handle_ = nullptr;
  }
  if (host_va_ != nullptr) {
    LogCleanupError("aclrtUnmapMem(host)", aclrtUnmapMem(host_va_));
    host_va_ = nullptr;
  }
  if (host_handle_ != nullptr) {
    LogCleanupError("aclrtFreePhysical(host)", aclrtFreePhysical(host_handle_));
    host_handle_ = nullptr;
  }
  if (arena_va_ != nullptr) {
    LogCleanupError("aclrtReleaseMemAddress", aclrtReleaseMemAddress(arena_va_));
    arena_va_ = nullptr;
  }
  arena_bytes_ = 0U;
  slot_bytes_ = 0U;
  bytes_ = 0U;
}

}  // namespace acltest
