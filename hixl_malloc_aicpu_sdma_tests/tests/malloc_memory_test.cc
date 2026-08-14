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
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uintptr_t kMappedHostAddress = 0x400000000ULL;

struct RuntimeState {
    aclrtHostMemMapCapability capability = ACL_RT_HOST_MEM_MAP_SUPPORTED;
    uint32_t capability_device = 0U;
    aclrtHacType capability_type = ACL_RT_HAC_TYPE_STARS;
    size_t host_allocation_size = 0U;
    void *host_allocation = nullptr;
    void *registered_host = nullptr;
    uint64_t registered_size = 0U;
    void *device_allocation = nullptr;
    size_t capability_calls = 0U;
    size_t register_calls = 0U;
    std::vector<std::string> cleanup_order;
};

RuntimeState g_runtime;

void Expect(bool condition, const char *message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void ResetRuntimeState()
{
    Expect(g_runtime.host_allocation == nullptr, "Host allocation leaked by previous test");
    Expect(g_runtime.device_allocation == nullptr, "Device allocation leaked by previous test");
    g_runtime = {};
    g_runtime.capability = ACL_RT_HOST_MEM_MAP_SUPPORTED;
}

void TestMappedMode()
{
    ResetRuntimeState();
    acltest::MallocMemoryPair memory;
    memory.Initialize(5000U, 2, acltest::HostAddressMode::kMapped);

    Expect(g_runtime.capability_calls == 1U, "Mapped mode did not query SDMA capability");
    Expect(g_runtime.capability_device == 2U, "Capability query used the wrong device");
    Expect(g_runtime.capability_type == ACL_RT_HAC_TYPE_SDMA,
           "Capability query used the wrong accelerator");
    Expect(g_runtime.register_calls == 1U, "Mapped mode did not register Host memory");
    Expect(g_runtime.registered_size == 8192U, "Registration size is not page aligned");
    Expect((reinterpret_cast<uintptr_t>(memory.host()) & 4095U) == 0U,
           "Registered Host address is not page aligned");
    Expect(memory.sdmaHost() == reinterpret_cast<void *>(kMappedHostAddress),
           "Mapped mode did not expose the Device-visible Host address");

    memory.Reset();
    const std::vector<std::string> expected = {"free_device", "unregister_host", "free_host"};
    Expect(g_runtime.cleanup_order == expected, "Mapped cleanup order is incorrect");
}

void TestDirectMode()
{
    ResetRuntimeState();
    acltest::MallocMemoryPair memory;
    memory.Initialize(5000U, 0, acltest::HostAddressMode::kDirect);

    Expect(g_runtime.capability_calls == 0U, "Direct mode queried Host mapping capability");
    Expect(g_runtime.register_calls == 0U, "Direct mode registered Host memory");
    Expect(g_runtime.host_allocation_size == 5000U, "Direct mode changed allocation size");
    Expect(memory.sdmaHost() == memory.host(), "Direct mode did not preserve the raw Host VA");

    memory.Reset();
    const std::vector<std::string> expected = {"free_device", "free_host"};
    Expect(g_runtime.cleanup_order == expected, "Direct cleanup order is incorrect");
}

void TestUnsupportedCapability()
{
    ResetRuntimeState();
    g_runtime.capability = ACL_RT_HOST_MEM_MAP_NOT_SUPPORTED;
    acltest::MallocMemoryPair memory;
    bool threw = false;
    try {
        memory.Initialize(4096U, 0, acltest::HostAddressMode::kMapped);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    Expect(threw, "Unsupported SDMA Host mapping capability did not fail");
    Expect(g_runtime.host_allocation == nullptr, "Unsupported capability allocated Host memory");
    Expect(g_runtime.device_allocation == nullptr,
           "Unsupported capability allocated Device memory");
}

}  // namespace

extern "C" {

const char *aclGetRecentErrMsg() { return ""; }

aclError aclrtHostMemMapCapabilities(uint32_t device_id, aclrtHacType hac_type,
                                     aclrtHostMemMapCapability *capability)
{
    ++g_runtime.capability_calls;
    g_runtime.capability_device = device_id;
    g_runtime.capability_type = hac_type;
    *capability = g_runtime.capability;
    return ACL_SUCCESS;
}

aclError aclrtMallocHost(void **host_ptr, size_t size)
{
    auto *allocation = new (std::nothrow) uint8_t[size];
    if (allocation == nullptr) { return ACL_ERROR_BAD_ALLOC; }
    g_runtime.host_allocation_size = size;
    g_runtime.host_allocation = allocation;
    *host_ptr = allocation;
    return ACL_SUCCESS;
}

aclError aclrtHostRegister(void *ptr, uint64_t size, aclrtHostRegisterType, void **device_ptr)
{
    ++g_runtime.register_calls;
    g_runtime.registered_host = ptr;
    g_runtime.registered_size = size;
    *device_ptr = reinterpret_cast<void *>(kMappedHostAddress);
    return ACL_SUCCESS;
}

aclError aclrtMalloc(void **device_ptr, size_t size, aclrtMemMallocPolicy)
{
    auto *allocation = new (std::nothrow) uint8_t[size];
    if (allocation == nullptr) { return ACL_ERROR_BAD_ALLOC; }
    g_runtime.device_allocation = allocation;
    *device_ptr = allocation;
    return ACL_SUCCESS;
}

aclError aclrtFree(void *device_ptr)
{
    if (device_ptr != g_runtime.device_allocation) { return ACL_ERROR_INVALID_PARAM; }
    delete[] static_cast<uint8_t *>(device_ptr);
    g_runtime.device_allocation = nullptr;
    g_runtime.cleanup_order.emplace_back("free_device");
    return ACL_SUCCESS;
}

aclError aclrtHostUnregister(void *ptr)
{
    if (ptr != g_runtime.registered_host) { return ACL_ERROR_INVALID_PARAM; }
    g_runtime.registered_host = nullptr;
    g_runtime.cleanup_order.emplace_back("unregister_host");
    return ACL_SUCCESS;
}

aclError aclrtFreeHost(void *host_ptr)
{
    if (host_ptr != g_runtime.host_allocation) { return ACL_ERROR_INVALID_PARAM; }
    delete[] static_cast<uint8_t *>(host_ptr);
    g_runtime.host_allocation = nullptr;
    g_runtime.cleanup_order.emplace_back("free_host");
    return ACL_SUCCESS;
}

}  // extern "C"

int main()
{
    try {
        TestMappedMode();
        TestDirectMode();
        TestUnsupportedCapability();
        ResetRuntimeState();
        std::cout << "malloc_memory_test: PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "malloc_memory_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
