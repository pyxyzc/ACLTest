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

#include <cstddef>
#include <cstdint>
#include <iostream>
#include "acltest/hixl_ops_types.h"

namespace {

int failures = 0;

void Expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main()
{
    using namespace acltest;
    Expect(sizeof(HixlTransferDesc) == 24U, "descriptor size");
    Expect(sizeof(HixlKernelParam) == 64U, "kernel parameter size");
    Expect(offsetof(HixlKernelParam, direction) == 32U, "direction offset");
    Expect(offsetof(HixlKernelParam, status_addr) == 48U, "status address offset");
    Expect(offsetof(HixlKernelParam, transfer_ctx_key) == 56U, "context key offset");
    Expect(sizeof(HixlTransferContextSyncEntry) == 32U, "context entry size");
    Expect(sizeof(HixlTransferContextSyncParam) == 24U, "context parameter size");
    Expect(static_cast<uint32_t>(TransferDirection::kDeviceToHost) == 0U,
           "official batch-read direction value");
    Expect(static_cast<uint32_t>(TransferDirection::kHostToDevice) == 1U,
           "official batch-write direction value");
    Expect(kMaxDescriptorsPerLaunch == 128U, "official descriptor batch limit");
    Expect(kMaxInFlightRtsqTasks == 1920U, "official RTSQ in-flight budget");
    return failures == 0 ? 0 : 1;
}
