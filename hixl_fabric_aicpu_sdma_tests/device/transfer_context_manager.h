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

#ifndef ACLTEST_DEVICE_TRANSFER_CONTEXT_MANAGER_H_
#define ACLTEST_DEVICE_TRANSFER_CONTEXT_MANAGER_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "acltest/aicpu_types.h"

namespace acltest {

struct TransferContext {
    TransferContext() = default;
    TransferContext(const TransferContext &) = delete;
    TransferContext &operator=(const TransferContext &) = delete;

    TransferContextState GetState() const { return state.load(std::memory_order_acquire); }
    void SetState(TransferContextState new_state)
    {
        state.store(new_state, std::memory_order_release);
    }
    void lock()
    {
        while (spin_lock.test_and_set(std::memory_order_acquire)) {}
    }
    bool try_lock() { return !spin_lock.test_and_set(std::memory_order_acquire); }
    void unlock() { spin_lock.clear(std::memory_order_release); }

    std::atomic_flag spin_lock = ATOMIC_FLAG_INIT;
    std::atomic<TransferContextState> state{kTransferContextInitialized};
};

class TransferContextManager {
public:
    static TransferContextManager &Instance();
    std::shared_ptr<TransferContext> Get(uint64_t key) const;
    TransferContextState Add(uint64_t key);
    TransferContextState Delete(uint64_t key);

private:
    TransferContextManager() = default;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<TransferContext>> contexts_;
};

}  // namespace acltest

#endif  // ACLTEST_DEVICE_TRANSFER_CONTEXT_MANAGER_H_
