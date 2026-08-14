/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>
#include <mutex>

#include "acltest/aicpu_types.h"
#include "stars_sdma.h"
#include "transfer_context_manager.h"

namespace acltest {
namespace {

constexpr uint32_t kSuccess = 0U;
constexpr uint32_t kFailure = 1U;

void WriteStatus(AicpuKernelParam *param, uint32_t status) {
  if (param != nullptr && param->status_addr != 0U) {
    *reinterpret_cast<volatile uint32_t *>(static_cast<uintptr_t>(param->status_addr)) = status;
  }
}

uint32_t ExecuteBatch(AicpuKernelParam *param, TransferDirection expected_direction) {
  if (param == nullptr || param->desc_addr == 0U || param->desc_count == 0U ||
      param->desc_count > kMaxDescriptorsPerLaunch ||
      param->direction != static_cast<uint32_t>(expected_direction) || param->transfer_ctx_key == 0U) {
    WriteStatus(param, kFailure);
    return kFailure;
  }
  const std::shared_ptr<TransferContext> context =
      TransferContextManager::Instance().Get(param->transfer_ctx_key);
  if (context == nullptr || context->GetState() != kTransferContextInitialized) {
    WriteStatus(param, kFailure);
    return kFailure;
  }
  std::lock_guard<TransferContext> lock(*context);
  if (context->GetState() != kTransferContextInitialized) {
    WriteStatus(param, kFailure);
    return kFailure;
  }
  const auto *descriptors = reinterpret_cast<const AicpuTransferDesc *>(
      static_cast<uintptr_t>(param->desc_addr));
  const uint32_t result = StarsSdma::Submit(*param, descriptors);
  WriteStatus(param, result);
  return result;
}

}  // namespace
}  // namespace acltest

extern "C" {
uint32_t AclTestSdmaBatchRead(acltest::AicpuKernelParam *param) {
  return acltest::ExecuteBatch(param, acltest::TransferDirection::kDeviceToHost);
}

uint32_t AclTestSdmaBatchWrite(acltest::AicpuKernelParam *param) {
  return acltest::ExecuteBatch(param, acltest::TransferDirection::kHostToDevice);
}
}
