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

#ifndef ACLTEST_INCLUDE_ACLTEST_HIXL_OPS_TYPES_H_
#define ACLTEST_INCLUDE_ACLTEST_HIXL_OPS_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace acltest {

constexpr uint32_t kMaxDescriptorsPerLaunch = 128U;
constexpr uint32_t kMaxInFlightRtsqTasks = 1920U;
constexpr uint32_t kMinRtsqDepth = kMaxInFlightRtsqTasks + 2U;

enum class TransferDirection : uint32_t {
    kDeviceToHost = 0U,
    kHostToDevice = 1U,
};

// Minimal private ABI used by the official HIXL FabricMem AICPU ops. Keep this layout aligned with
// ~/hixl at commit 39a7e36201ac240e61f8a799b704f4e6732ffdd3.
struct HixlTransferDesc {
    uint64_t src_addr = 0U;
    uint64_t dst_addr = 0U;
    uint64_t length = 0U;
};

struct HixlKernelParam {
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

struct HixlTransferContextSyncEntry {
    uint64_t thread = 0U;
    uint32_t op = 0U;
    uint32_t notify_id = 0U;
    uint64_t err_flag_dev_va = 0U;
    uint64_t reserved = 0U;
};

struct HixlTransferContextSyncParam {
    uint64_t entry_list_addr = 0U;
    uint64_t state_list_addr = 0U;
    uint32_t entry_num = 0U;
    uint32_t reserved = 0U;
};

static_assert(std::is_standard_layout<HixlTransferDesc>::value, "HIXL descriptor must be standard-layout");
static_assert(sizeof(HixlTransferDesc) == 24U, "HIXL descriptor ABI changed");
static_assert(sizeof(HixlKernelParam) == 64U, "HIXL kernel parameter ABI changed");
static_assert(offsetof(HixlKernelParam, desc_count) == 8U, "HIXL desc_count offset changed");
static_assert(offsetof(HixlKernelParam, direction) == 32U, "HIXL direction offset changed");
static_assert(offsetof(HixlKernelParam, status_addr) == 48U, "HIXL status address offset changed");
static_assert(offsetof(HixlKernelParam, transfer_ctx_key) == 56U,
              "HIXL transfer-context key offset changed");
static_assert(sizeof(HixlTransferContextSyncEntry) == 32U, "HIXL context entry ABI changed");
static_assert(offsetof(HixlTransferContextSyncEntry, notify_id) == 12U,
              "HIXL context notify offset changed");
static_assert(sizeof(HixlTransferContextSyncParam) == 24U, "HIXL context parameter ABI changed");

}  // namespace acltest

#endif  // ACLTEST_INCLUDE_ACLTEST_HIXL_OPS_TYPES_H_
