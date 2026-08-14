/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "transfer_context_manager.h"

namespace acltest {

TransferContextManager &TransferContextManager::Instance() {
  static TransferContextManager manager;
  return manager;
}

std::shared_ptr<TransferContext> TransferContextManager::Get(uint64_t key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = contexts_.find(key);
  return found == contexts_.end() ? nullptr : found->second;
}

TransferContextState TransferContextManager::Add(uint64_t key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto &context = contexts_[key];
  if (context == nullptr) {
    context = std::make_shared<TransferContext>();
  }
  context->SetState(kTransferContextInitialized);
  return kTransferContextInitialized;
}

TransferContextState TransferContextManager::Delete(uint64_t key) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = contexts_.find(key);
  if (found == contexts_.end() || found->second == nullptr) {
    return kTransferContextDeleted;
  }
  const std::shared_ptr<TransferContext> context = found->second;
  context->SetState(kTransferContextDeleting);
  if (!context->try_lock()) {
    return kTransferContextDeleting;
  }
  context->SetState(kTransferContextDeleted);
  contexts_.erase(found);
  context->unlock();
  return kTransferContextDeleted;
}

}  // namespace acltest
