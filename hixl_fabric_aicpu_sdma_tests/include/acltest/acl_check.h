/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ACLTEST_INCLUDE_ACLTEST_ACL_CHECK_H_
#define ACLTEST_INCLUDE_ACLTEST_ACL_CHECK_H_

#include <sstream>
#include <stdexcept>
#include <string>

#include "acl/acl.h"

namespace acltest {

class AclException : public std::runtime_error {
 public:
  AclException(const std::string &operation, aclError error)
      : std::runtime_error(BuildMessage(operation, error)), error_(error) {}

  aclError error() const noexcept {
    return error_;
  }

 private:
  static std::string BuildMessage(const std::string &operation, aclError error) {
    std::ostringstream out;
    out << operation << " failed, aclError=" << error;
    const char *message = aclGetRecentErrMsg();
    if (message != nullptr && message[0] != '\0') {
      out << ", recent_error=" << message;
    }
    return out.str();
  }

  aclError error_;
};

inline void CheckAcl(aclError error, const char *operation) {
  if (error != ACL_SUCCESS) {
    throw AclException(operation, error);
  }
}

}  // namespace acltest

#define ACLTEST_CHECK_ACL(expression) ::acltest::CheckAcl((expression), #expression)

#endif  // ACLTEST_INCLUDE_ACLTEST_ACL_CHECK_H_
