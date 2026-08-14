/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "acltest/benchmark_logic.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

int main() {
  using namespace acltest;
  assert(ParseByteSize("512B") == 512U);
  assert(ParseByteSize("2K") == 2048U);
  assert(ParseByteSize("1MiB") == 1024U * 1024U);
  assert(ParseCountList("1,128").size() == 2U);

  const uint64_t local = 0x100000000ULL;
  const uint64_t device = 0x200000000ULL;
  const uint64_t long_size = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 17U;
  auto read = BuildDeviceDescriptors(TransferDirection::kDeviceToHost, {{local, device, long_size}});
  assert(read.size() == 2U);
  assert(read[0].src_addr == device && read[0].dst_addr == local);
  assert(read[0].length == std::numeric_limits<uint32_t>::max());
  assert(read[1].length == 17U);
  auto write = BuildDeviceDescriptors(TransferDirection::kHostToDevice, {{local, device, 1U}});
  assert(write[0].src_addr == local && write[0].dst_addr == device);

  const auto shape128 = ComputeSubmissionShape(128U);
  assert(shape128.kernel_launch_count == 1U && shape128.notify_count == 1U);
  const auto shape1921 = ComputeSubmissionShape(1921U);
  assert(shape1921.kernel_launch_count == 16U && shape1921.notify_count == 2U);
  assert(ComputeAutoIterations(64U * 1024U, 256U * 1024U * 1024U) == 500U);
  assert(ComputeAutoIterations(256U * 1024U * 1024U, 256U * 1024U * 1024U) == 5U);
  assert(Percentile({1.0, 2.0, 3.0, 4.0}, 0.5) == 2.5);

  std::cout << "benchmark_logic_test: PASS\n";
  return 0;
}
