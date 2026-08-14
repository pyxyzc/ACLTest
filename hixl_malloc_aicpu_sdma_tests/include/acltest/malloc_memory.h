/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ACLTEST_INCLUDE_ACLTEST_MALLOC_MEMORY_H_
#define ACLTEST_INCLUDE_ACLTEST_MALLOC_MEMORY_H_

#include <cstddef>
#include <cstdint>

#include "acl/acl.h"

namespace acltest {

// Conventional single-process ACL memory pair. Host memory is pinned by
// aclrtMallocHost and device memory is allocated by aclrtMalloc. This class
// intentionally contains no VMM physical allocation, MapMem, or MemSetAccess.
class MallocMemoryPair {
 public:
  MallocMemoryPair() = default;
  ~MallocMemoryPair();
  MallocMemoryPair(const MallocMemoryPair &) = delete;
  MallocMemoryPair &operator=(const MallocMemoryPair &) = delete;

  void Initialize(size_t bytes, int32_t logic_device_id);
  void Reset() noexcept;

  void *host() const {
    return host_;
  }
  void *device() const {
    return device_;
  }
  size_t size() const {
    return bytes_;
  }

 private:
  void *host_ = nullptr;
  void *device_ = nullptr;
  size_t bytes_ = 0U;
};

}  // namespace acltest

#endif  // ACLTEST_INCLUDE_ACLTEST_MALLOC_MEMORY_H_
