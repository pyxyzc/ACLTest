/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ACLTEST_DEVICE_STARS_SDMA_H_
#define ACLTEST_DEVICE_STARS_SDMA_H_

#include <cstdint>

#include "a3_rtsq.h"
#include "acltest/aicpu_types.h"

namespace acltest {

struct DriverResourceIdKey {
  uint32_t ruDevId = 0U;
  uint32_t tsId = 0U;
  uint32_t resType = 0U;
  uint32_t resId = 0U;
  uint32_t flag = 0U;
  uint32_t reserved[3]{};
};

struct RtsqState {
  uint32_t device_id = 0U;
  uint32_t sq_id = 0U;
  uint32_t stream_id = 0U;
  uint32_t logic_cq_id = 0U;
  uint32_t next_task_id = 0U;
  uint64_t base_addr = 0U;
  uint32_t depth = 0U;
  uint32_t head = 0U;
  uint32_t tail = 0U;
};

struct __attribute__((packed)) LogicCqeView {
  uint16_t stream_id = 0U;
  uint16_t task_id = 0U;
  uint32_t error_code = 0U;
  uint8_t error_type = 0U;
  uint8_t sqe_type = 0U;
  uint16_t sq_id = 0U;
  uint16_t sq_head = 0U;
};

union RtsqEntry {
  A3SdmaSqe sdma;
  A3NotifySqe notify;
  uint8_t bytes[kA3RtsqEntryBytes];
};

struct RtsqBatch {
  RtsqEntry entries[128U]{};
  uint32_t count = 0U;
};

enum class LogicCqResult { kEmpty, kReports, kError };

class StarsSdma {
 public:
  static uint32_t Submit(const AicpuKernelParam &param, const AicpuTransferDesc *descriptors);

 private:
  static bool Validate(const AicpuKernelParam &param, const AicpuTransferDesc *descriptors);
  static bool InitializeRtsq(const AicpuKernelParam &param, RtsqState &state);
  static bool LoadQueueState(RtsqState &state);
  static bool RestoreStream(const RtsqState &state);
  static bool ResolveLocalDevice(uint32_t host_device_id, uint32_t &local_device_id);
  static bool BuildDeadline(uint32_t timeout_ms, uint64_t &deadline);
  static uint64_t MonotonicNs();
  static bool PollLogicCqUntilEmpty(const RtsqState &state, uint64_t deadline);
  static LogicCqResult ReceiveLogicCq(const RtsqState &state, uint8_t *reports, uint32_t &count);
  static bool EnsureCapacity(RtsqState &state, uint32_t count, uint64_t deadline);
  static bool HasCapacity(const RtsqState &state, uint32_t count);
  static bool BuildSdmaSqe(const AicpuTransferDesc &descriptor, uint32_t task_id,
                           const RtsqState &state, A3SdmaSqe &sqe);
  static bool BuildNotifySqe(uint32_t notify_id, uint32_t task_id,
                             const RtsqState &state, A3NotifySqe &sqe);
  static bool Publish(RtsqState &state, RtsqBatch &batch, uint64_t deadline);
  static bool CopyToRing(const RtsqState &state, const RtsqBatch &batch);
  static bool CommitTail(RtsqState &state, uint32_t new_tail);
  static bool AppendDescriptors(const AicpuTransferDesc *descriptors, uint32_t count,
                                RtsqState &state, RtsqBatch &batch, uint64_t deadline);
  static bool AppendNotify(uint32_t notify_id, RtsqState &state, RtsqBatch &batch, uint64_t deadline);
};

}  // namespace acltest

#endif  // ACLTEST_DEVICE_STARS_SDMA_H_
