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

#include "acltest/a5_hal_test_types.h"

#include <cstdint>
#include "ascend_hal_error.h"
#include "hal_pkg/trs_pkg.h"

namespace acltest {
namespace {

constexpr uint32_t kAnyTaskId = 0xFFFFU;
constexpr uint32_t kMatchCopyVersion = 1U;
constexpr uint32_t kLogicCqeBytes = 32U;

struct DriverResourceIdKey {
    uint32_t ruDevId = 0U;
    uint32_t tsId = 0U;
    uint32_t resType = 0U;
    uint32_t resId = 0U;
    uint32_t flag = 0U;
    uint32_t reserved[3]{};
};

extern "C" {
drvError_t __attribute__((weak)) halSqCqQuery(uint32_t device_id, struct halSqCqQueryInfo *info);
drvError_t __attribute__((weak)) halSqCqConfig(uint32_t device_id, struct halSqCqConfigInfo *info);
drvError_t __attribute__((weak)) drvGetLocalDevIDByHostDevID(uint32_t host_device_id,
                                                             uint32_t *local_device_id);
drvError_t __attribute__((weak)) halCqReportRecv(uint32_t device_id, struct halReportRecvInfo *info);
drvError_t __attribute__((weak)) halResourceIdRestore(DriverResourceIdKey *info);
}

void InitializeResult(A5HalTestResult &result, uint32_t case_id)
{
    result = {};
    result.magic = kA5HalTestResultMagic;
    result.case_id = case_id;
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kPreflight);
}

bool ResolveLocalDevice(const A5HalTestParam &param, A5HalTestResult &result)
{
    if (drvGetLocalDevIDByHostDevID == nullptr) {
        result.preflight_ret = kA5HalTestSymbolUnavailable;
        return false;
    }
    uint32_t local_device_id = 0U;
    const drvError_t ret = drvGetLocalDevIDByHostDevID(param.host_device_id, &local_device_id);
    result.preflight_ret = static_cast<int32_t>(ret);
    result.local_device_id = local_device_id;
    return ret == DRV_ERROR_NONE;
}

drvSqCqPropType_t QueryProperty(A5HalTestCase test_case)
{
    switch (test_case) {
        case A5HalTestCase::kQuerySqBase:
            return DRV_SQCQ_PROP_SQ_BASE;
        case A5HalTestCase::kQuerySqDepth:
            return DRV_SQCQ_PROP_SQ_DEPTH;
        case A5HalTestCase::kQuerySqHead:
            return DRV_SQCQ_PROP_SQ_HEAD;
        case A5HalTestCase::kQuerySqTail:
            return DRV_SQCQ_PROP_SQ_TAIL;
        default:
            return DRV_SQCQ_PROP_MAX;
    }
}

void TestQuery(const A5HalTestParam &param, A5HalTestResult &result, A5HalTestCase test_case)
{
    if (halSqCqQuery == nullptr) {
        result.hal_ret = kA5HalTestSymbolUnavailable;
        return;
    }
    halSqCqQueryInfo query{};
    query.type = DRV_NORMAL_TYPE;
    query.tsId = 0U;
    query.sqId = param.sq_id;
    query.cqId = 0U;
    query.prop = QueryProperty(test_case);
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kBeforeHalCall);
    const drvError_t ret = halSqCqQuery(result.local_device_id, &query);
    result.hal_ret = static_cast<int32_t>(ret);
    result.value0 = query.value[0U];
    result.value1 = query.value[1U];
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kAfterHalCall);
}

void TestRestoreStream(const A5HalTestParam &param, A5HalTestResult &result)
{
    if (halResourceIdRestore == nullptr) {
        result.hal_ret = kA5HalTestSymbolUnavailable;
        return;
    }
    DriverResourceIdKey resource{};
    resource.ruDevId = result.local_device_id;
    resource.resType = static_cast<uint32_t>(DRV_STREAM_ID);
    resource.resId = param.stream_id;
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kBeforeHalCall);
    const drvError_t ret = halResourceIdRestore(&resource);
    result.hal_ret = static_cast<int32_t>(ret);
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kAfterHalCall);
}

void TestConfigTail(const A5HalTestParam &param, A5HalTestResult &result)
{
    if (halSqCqConfig == nullptr) {
        result.hal_ret = kA5HalTestSymbolUnavailable;
        return;
    }
    halSqCqConfigInfo config{};
    config.type = DRV_NORMAL_TYPE;
    config.tsId = 0U;
    config.sqId = param.sq_id;
    config.cqId = 0U;
    config.prop = DRV_SQCQ_PROP_SQ_TAIL;
    config.value[0U] = param.sq_tail;
    result.value0 = param.sq_tail;
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kBeforeHalCall);
    const drvError_t ret = halSqCqConfig(result.local_device_id, &config);
    result.hal_ret = static_cast<int32_t>(ret);
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kAfterHalCall);
}

void TestEmptyLogicCq(const A5HalTestParam &param, A5HalTestResult &result)
{
    if (halCqReportRecv == nullptr) {
        result.hal_ret = kA5HalTestSymbolUnavailable;
        return;
    }
    alignas(uint64_t) uint8_t report[kLogicCqeBytes]{};
    halReportRecvInfo receive{};
    receive.type = DRV_LOGIC_TYPE;
    receive.tsId = 0U;
    receive.cqId = param.logic_cq_id;
    receive.timeout = 0;
    receive.cqe_addr = report;
    receive.cqe_num = 1U;
    receive.report_cqe_num = 0U;
    receive.stream_id = param.stream_id;
    receive.task_id = kAnyTaskId;
    receive.res[0U] = kMatchCopyVersion;
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kBeforeHalCall);
    const drvError_t ret = halCqReportRecv(result.local_device_id, &receive);
    result.hal_ret = static_cast<int32_t>(ret);
    result.report_cqe_num = receive.report_cqe_num;
    result.phase = static_cast<uint32_t>(A5HalTestPhase::kAfterHalCall);
}

}  // namespace
}  // namespace acltest

extern "C" uint32_t AclTestA5HalTest(acltest::A5HalTestParam *param)
{
    if (param == nullptr || param->result_addr == 0U) { return 1U; }
    auto *result = reinterpret_cast<acltest::A5HalTestResult *>(
        static_cast<uintptr_t>(param->result_addr));
    acltest::InitializeResult(*result, param->case_id);
    if (!acltest::ResolveLocalDevice(*param, *result)) {
        result->phase = static_cast<uint32_t>(acltest::A5HalTestPhase::kAfterHalCall);
        return 0U;
    }

    const auto test_case = static_cast<acltest::A5HalTestCase>(param->case_id);
    switch (test_case) {
        case acltest::A5HalTestCase::kQuerySqBase:
        case acltest::A5HalTestCase::kQuerySqDepth:
        case acltest::A5HalTestCase::kQuerySqHead:
        case acltest::A5HalTestCase::kQuerySqTail:
            acltest::TestQuery(*param, *result, test_case);
            break;
        case acltest::A5HalTestCase::kRestoreStream:
            acltest::TestRestoreStream(*param, *result);
            break;
        case acltest::A5HalTestCase::kConfigTail:
            acltest::TestConfigTail(*param, *result);
            break;
        case acltest::A5HalTestCase::kReportEmptyCq:
            acltest::TestEmptyLogicCq(*param, *result);
            break;
        default:
            result->phase = static_cast<uint32_t>(acltest::A5HalTestPhase::kUnsupportedCase);
            return 0U;
    }
    return 0U;
}
