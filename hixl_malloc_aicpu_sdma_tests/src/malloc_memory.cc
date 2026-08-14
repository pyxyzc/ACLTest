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
#include <iostream>
#include <stdexcept>
#include "acltest/acl_check.h"

namespace acltest {
namespace {

void LogCleanupError(const char *operation, aclError error) noexcept
{
    if (error != ACL_SUCCESS) {
        std::cerr << "warning: " << operation << " failed during cleanup, aclError=" << error
                  << '\n';
    }
}

}  // namespace

MallocMemoryPair::~MallocMemoryPair() { Reset(); }

void MallocMemoryPair::Initialize(size_t bytes, int32_t logic_device_id)
{
    if (bytes == 0U || logic_device_id < 0) {
        throw std::invalid_argument(
            "malloc memory requires non-zero bytes and a non-negative device id");
    }
    if (host_ != nullptr || device_ != nullptr) {
        throw std::logic_error("malloc memory pair is already initialized");
    }

    try {
        ACLTEST_CHECK_ACL(aclrtMallocHost(&host_, bytes));
        ACLTEST_CHECK_ACL(aclrtMalloc(&device_, bytes, ACL_MEM_MALLOC_NORMAL_ONLY));
        bytes_ = bytes;
        std::cerr << "malloc memory: bytes=" << bytes_ << ", host=" << host_
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
    if (host_ != nullptr) {
        LogCleanupError("aclrtFreeHost(host)", aclrtFreeHost(host_));
        host_ = nullptr;
    }
    bytes_ = 0U;
}

}  // namespace acltest
