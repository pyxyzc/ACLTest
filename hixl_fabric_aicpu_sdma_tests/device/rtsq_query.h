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

#ifndef ACLTEST_DEVICE_RTSQ_QUERY_H_
#define ACLTEST_DEVICE_RTSQ_QUERY_H_

#include <cstdint>
#include "ascend_hal_error.h"
#include "hal_pkg/trs_pkg.h"

namespace acltest {

constexpr uint32_t kRtsqQueryFlagIndex = 7U;
static_assert(kRtsqQueryFlagIndex < sizeof(halSqCqQueryInfo{}.value) / sizeof(uint32_t),
              "halSqCqQueryInfo query-flag ABI slot is missing");

inline bool QueryRtsqValues(uint32_t device_id, uint32_t sq_id, drvSqCqPropType_t property,
                            uint32_t &value_low, uint32_t &value_high,
                            drvError_t (*query)(uint32_t, struct halSqCqQueryInfo *),
                            uint32_t query_flag = 0U)
{
    if (query == nullptr) { return false; }
    halSqCqQueryInfo info{};
    info.type = DRV_NORMAL_TYPE;
    info.tsId = 0U;
    info.sqId = sq_id;
    info.cqId = 0U;
    info.prop = property;
    info.value[kRtsqQueryFlagIndex] = query_flag;
    if (query(device_id, &info) != DRV_ERROR_NONE) { return false; }
    value_low = info.value[0U];
    value_high = info.value[1U];
    return true;
}

inline bool QueryRtsqValue(uint32_t device_id, uint32_t sq_id, drvSqCqPropType_t property,
                           uint32_t &value,
                           drvError_t (*query)(uint32_t, struct halSqCqQueryInfo *),
                           uint32_t query_flag = 0U)
{
    uint32_t unused = 0U;
    return QueryRtsqValues(device_id, sq_id, property, value, unused, query, query_flag);
}

}  // namespace acltest

#endif  // ACLTEST_DEVICE_RTSQ_QUERY_H_
