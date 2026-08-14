/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and
 * conditions of CANN Open Software License Agreement Version 2.0 (the "License"). Please refer to
 * the License for details. You may not use this file except in compliance with the License. THIS
 * SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A
 * PARTICULAR PURPOSE. See LICENSE in the root of the software repository for the full text of the
 * License.
 */

#include "acltest/malloc_memory.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "acltest/acl_check.h"

namespace acltest {
namespace {

constexpr size_t kHostRegisterAlignment = 4096U;

size_t CheckedAlignUp(size_t bytes)
{
    if (bytes > std::numeric_limits<size_t>::max() - (kHostRegisterAlignment - 1U)) {
        throw std::overflow_error("host registration size overflows");
    }
    return (bytes + kHostRegisterAlignment - 1U) & ~(kHostRegisterAlignment - 1U);
}

uintptr_t AlignHostAddress(void *address)
{
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    return (value + kHostRegisterAlignment - 1U) & ~(kHostRegisterAlignment - 1U);
}

const char *HostAddressModeName(HostAddressMode mode)
{
    return mode == HostAddressMode::kMapped ? "mapped" : "direct";
}

void LogCleanupError(const char *operation, aclError error) noexcept
{
    if (error != ACL_SUCCESS) {
        std::cerr << "warning: " << operation << " failed during cleanup, aclError=" << error
                  << '\n';
    }
}

}  // namespace

MallocMemoryPair::~MallocMemoryPair() { Reset(); }

void MallocMemoryPair::Initialize(size_t bytes, int32_t logic_device_id,
                                  HostAddressMode host_address_mode)
{
    if (bytes == 0U || logic_device_id < 0) {
        throw std::invalid_argument(
            "malloc memory requires non-zero bytes and a non-negative device id");
    }
    if (host_allocation_ != nullptr || device_ != nullptr) {
        throw std::logic_error("malloc memory pair is already initialized");
    }

    try {
        if (host_address_mode == HostAddressMode::kMapped) {
            aclrtHostMemMapCapability capability = ACL_RT_HOST_MEM_MAP_NOT_SUPPORTED;
            ACLTEST_CHECK_ACL(aclrtHostMemMapCapabilities(static_cast<uint32_t>(logic_device_id),
                                                          ACL_RT_HAC_TYPE_SDMA, &capability));
            if (capability != ACL_RT_HOST_MEM_MAP_SUPPORTED) {
                throw std::runtime_error(
                    "SDMA cannot access HostRegister-mapped memory on this device");
            }
            const size_t registered_bytes = CheckedAlignUp(bytes);
            ACLTEST_CHECK_ACL(
                aclrtMallocHost(&host_allocation_, registered_bytes + kHostRegisterAlignment - 1U));
            host_ = reinterpret_cast<void *>(AlignHostAddress(host_allocation_));
            ACLTEST_CHECK_ACL(
                aclrtHostRegister(host_, registered_bytes, ACL_HOST_REGISTER_MAPPED, &sdma_host_));
            host_registered_ = true;
        } else {
            ACLTEST_CHECK_ACL(aclrtMallocHost(&host_allocation_, bytes));
            host_ = host_allocation_;
            sdma_host_ = host_;
        }
        ACLTEST_CHECK_ACL(aclrtMalloc(&device_, bytes, ACL_MEM_MALLOC_NORMAL_ONLY));
        bytes_ = bytes;
        std::cerr << "malloc memory: mode=" << HostAddressModeName(host_address_mode)
                  << ", bytes=" << bytes_ << ", host=" << host_ << ", sdma_host=" << sdma_host_
                  << ", device=" << device_ << '\n';
    } catch (...) {
        Reset();
        throw;
    }
}

void MallocMemoryPair::Reset() noexcept
{
    if (device_ != nullptr) {
        LogCleanupError("aclrtFree(device)", aclrtFree(device_));
        device_ = nullptr;
    }
    if (host_registered_) {
        LogCleanupError("aclrtHostUnregister(host)", aclrtHostUnregister(host_));
        host_registered_ = false;
    }
    sdma_host_ = nullptr;
    host_ = nullptr;
    if (host_allocation_ != nullptr) {
        LogCleanupError("aclrtFreeHost(host)", aclrtFreeHost(host_allocation_));
        host_allocation_ = nullptr;
    }
    bytes_ = 0U;
}

}  // namespace acltest
