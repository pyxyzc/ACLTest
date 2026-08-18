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

#ifndef ACLTEST_A5_HAL_TESTS_INCLUDE_ACLTEST_A5_HAL_TEST_TYPES_H_
#define ACLTEST_A5_HAL_TESTS_INCLUDE_ACLTEST_A5_HAL_TEST_TYPES_H_

#include <cstdint>

namespace acltest {

enum class A5HalTestCase : uint32_t {
    kQuerySqBase = 1U,
    kQuerySqDepth = 2U,
    kQuerySqHead = 3U,
    kQuerySqTail = 4U,
    kRestoreStream = 5U,
    kConfigTail = 6U,
    kReportEmptyCq = 7U,
};

enum class A5HalTestPhase : uint32_t {
    kNotStarted = 0U,
    kPreflight = 1U,
    kBeforeHalCall = 2U,
    kAfterHalCall = 3U,
    kInvalidParam = 4U,
    kUnsupportedCase = 5U,
};

constexpr uint32_t kA5HalTestResultMagic = 0xA5C0A11EU;
constexpr int32_t kA5HalTestSymbolUnavailable = -1;

// Input: copied by Host into device memory and passed to AclTestA5HalTest.
struct A5HalTestParam {
    uint32_t case_id = 0U;
    uint32_t host_device_id = 0U;
    uint32_t sq_id = 0U;
    uint32_t stream_id = 0U;
    uint32_t logic_cq_id = 0U;
    uint32_t sq_tail = 0U;
    uint64_t result_addr = 0U;
};

// Output: written by AICPU and copied back by Host after stream synchronization.
struct A5HalTestResult {
    uint32_t magic = 0U;
    uint32_t case_id = 0U;
    uint32_t phase = static_cast<uint32_t>(A5HalTestPhase::kNotStarted);
    int32_t preflight_ret = 0;
    int32_t hal_ret = 0;
    uint32_t local_device_id = 0U;
    uint32_t value0 = 0U;
    uint32_t value1 = 0U;
    uint32_t report_cqe_num = 0U;
};

static_assert(sizeof(A5HalTestParam) == 32U, "A5 HAL test input ABI changed");
static_assert(sizeof(A5HalTestResult) == 36U, "A5 HAL test output ABI changed");

}  // namespace acltest

#endif  // ACLTEST_A5_HAL_TESTS_INCLUDE_ACLTEST_A5_HAL_TEST_TYPES_H_
