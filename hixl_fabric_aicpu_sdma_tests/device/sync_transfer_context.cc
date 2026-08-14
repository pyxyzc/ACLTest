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

#include "acltest/aicpu_types.h"
#include "transfer_context_manager.h"

namespace acltest {

uint32_t DoSyncTransferContext(TransferContextSyncParam *param)
{
    if (param == nullptr || param->entry_num == 0U || param->entry_list_addr == 0U ||
        param->state_list_addr == 0U) {
        return 1U;
    }
    auto *entries = reinterpret_cast<TransferContextSyncEntry *>(
        static_cast<uintptr_t>(param->entry_list_addr));
    auto *states = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(param->state_list_addr));
    for (uint32_t index = 0U; index < param->entry_num; ++index) {
        TransferContextState state = kTransferContextDeleted;
        if (entries[index].op == kTransferContextAdd) {
            state = TransferContextManager::Instance().Add(entries[index].key);
        } else if (entries[index].op == kTransferContextDelete) {
            state = TransferContextManager::Instance().Delete(entries[index].key);
        } else {
            return 1U;
        }
        states[index] = static_cast<uint32_t>(state);
    }
    return 0U;
}

}  // namespace acltest

extern "C" uint32_t AclTestSyncTransferContext(acltest::TransferContextSyncParam *param)
{
    return acltest::DoSyncTransferContext(param);
}
