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

#ifndef ACLTEST_INCLUDE_ACLTEST_BENCHMARK_LOGIC_H_
#define ACLTEST_INCLUDE_ACLTEST_BENCHMARK_LOGIC_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "acltest/aicpu_types.h"

namespace acltest {

struct AddressRange {
    uint64_t local_addr = 0U;
    uint64_t device_addr = 0U;
    uint64_t length = 0U;
};

struct SubmissionShape {
    size_t descriptor_count = 0U;
    size_t kernel_launch_count = 0U;
    size_t notify_count = 0U;
};

uint64_t ParseByteSize(const std::string &text);
std::vector<uint64_t> ParseByteSizeList(const std::string &text);
std::vector<size_t> ParseCountList(const std::string &text);
std::vector<AicpuTransferDesc> BuildDeviceDescriptors(TransferDirection direction,
                                                      const std::vector<AddressRange> &ranges);
SubmissionShape ComputeSubmissionShape(size_t descriptor_count);
size_t ComputeAutoIterations(uint64_t bytes_per_iteration, uint64_t target_bytes);
double Percentile(std::vector<double> values, double percentile);
std::string FormatByteSize(uint64_t bytes);

}  // namespace acltest

#endif  // ACLTEST_INCLUDE_ACLTEST_BENCHMARK_LOGIC_H_
