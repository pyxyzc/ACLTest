/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ACLTEST_INCLUDE_ACLTEST_AICPU_TYPES_H_
#define ACLTEST_INCLUDE_ACLTEST_AICPU_TYPES_H_

#include <cstdint>

namespace acltest {

constexpr uint32_t kMaxDescriptorsPerLaunch = 128U;
constexpr uint32_t kMaxInFlightRtsqTasks = 1920U;
constexpr uint32_t kMinRtsqDepth = kMaxInFlightRtsqTasks + 2U;

enum class TransferDirection : uint32_t {
  kDeviceToHost = 0U,
  kHostToDevice = 1U,
};

// Host and AICPU share these structures as a private, fixed-width ABI. One
// descriptor is one SDMA SQE; the host splits requests larger than UINT32_MAX.
struct AicpuTransferDesc {
  uint64_t src_addr = 0U;
  uint64_t dst_addr = 0U;
  uint64_t length = 0U;
};

struct AicpuKernelParam {
  uint64_t desc_addr = 0U;
  uint32_t desc_count = 0U;
  uint32_t device_id = 0U;
  uint32_t rtsq_id = 0U;
  uint32_t rtsq_stream_id = 0U;
  uint32_t rtsq_task_id = 0U;
  uint32_t rtsq_logic_cq_id = 0U;
  uint32_t direction = 0U;
  uint32_t timeout_ms = 0U;
  uint32_t notify_id = 0U;
  uint32_t emit_notify_record = 0U;
  uint64_t status_addr = 0U;
  uint64_t transfer_ctx_key = 0U;
};

enum TransferContextState : uint32_t {
  kTransferContextInitialized = 0U,
  kTransferContextDeleting = 1U,
  kTransferContextDeleted = 2U,
};

enum TransferContextOp : uint32_t {
  kTransferContextAdd = 0U,
  kTransferContextDelete = 1U,
};

struct TransferContextSyncEntry {
  uint64_t key = 0U;
  uint32_t op = 0U;
  uint32_t reserved0 = 0U;
  uint64_t reserved1 = 0U;
  uint64_t reserved2 = 0U;
};

struct TransferContextSyncParam {
  uint64_t entry_list_addr = 0U;
  uint64_t state_list_addr = 0U;
  uint32_t entry_num = 0U;
  uint32_t reserved = 0U;
};

}  // namespace acltest

#endif  // ACLTEST_INCLUDE_ACLTEST_AICPU_TYPES_H_
