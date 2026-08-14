/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ACLTEST_INCLUDE_ACLTEST_AICPU_BATCH_LAUNCHER_H_
#define ACLTEST_INCLUDE_ACLTEST_AICPU_BATCH_LAUNCHER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "acl/acl.h"
#include "acltest/benchmark_logic.h"

namespace acltest {

class AicpuBatchLauncher {
 public:
  AicpuBatchLauncher() = default;
  ~AicpuBatchLauncher();
  AicpuBatchLauncher(const AicpuBatchLauncher &) = delete;
  AicpuBatchLauncher &operator=(const AicpuBatchLauncher &) = delete;

  void Initialize(int32_t logic_device_id, const std::string &kernel_json);

  // Submit includes descriptor construction, three device allocations, and the
  // descriptor/status uploads, matching FabricMem's current Host submission path.
  void Submit(TransferDirection direction, const std::vector<AddressRange> &ranges, uint32_t timeout_ms);
  void Synchronize(uint32_t timeout_ms);
  // Read status words and free request buffers. Call only after Synchronize.
  void Finish();
  void Abort() noexcept;
  void Shutdown() noexcept;

  const SubmissionShape &last_shape() const {
    return last_shape_;
  }

 private:
  struct RequestResource {
    void *descriptors = nullptr;
    size_t descriptor_bytes = 0U;
    void *statuses = nullptr;
    size_t status_count = 0U;
    void *kernel_args = nullptr;
    size_t kernel_arg_count = 0U;
  };

  void LoadKernel(const std::string &kernel_json);
  void CreateExecutionResources();
  void BuildRtsqParam(uint32_t task_count, AicpuKernelParam &param);
  void LaunchBatches(TransferDirection direction, size_t descriptor_count, uint32_t timeout_ms);
  void LaunchOneBatch(aclrtFuncHandle function, TransferDirection direction, size_t begin, size_t count,
                      size_t descriptor_count, uint32_t timeout_ms, size_t status_index,
                      size_t &tasks_since_notify);
  uint32_t SyncTransferContextOnce(uint32_t operation);
  void SyncTransferContext(uint32_t operation, uint32_t expected_state);
  void ReleaseRequest() noexcept;
  void DestroyExecutionResources(bool abort_streams) noexcept;

  int32_t logic_device_id_ = -1;
  uint32_t physical_device_id_ = 0U;
  uint32_t next_task_id_ = 0U;
  aclrtBinHandle binary_ = nullptr;
  aclrtFuncHandle batch_read_ = nullptr;
  aclrtFuncHandle batch_write_ = nullptr;
  aclrtFuncHandle sync_context_ = nullptr;
  aclrtStream control_stream_ = nullptr;
  aclrtStream worker_stream_ = nullptr;
  aclrtNotify notify_ = nullptr;
  uint32_t notify_id_ = 0U;
  uint64_t transfer_context_key_ = 0U;
  bool transfer_context_registered_ = false;
  bool initialized_ = false;
  bool request_active_ = false;
  bool request_launched_ = false;
  RequestResource request_;
  SubmissionShape last_shape_;
};

}  // namespace acltest

#endif  // ACLTEST_INCLUDE_ACLTEST_AICPU_BATCH_LAUNCHER_H_
