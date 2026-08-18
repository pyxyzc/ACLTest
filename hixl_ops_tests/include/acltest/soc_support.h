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

#ifndef ACLTEST_SOC_SUPPORT_H
#define ACLTEST_SOC_SUPPORT_H

#include <array>
#include <string_view>

namespace acltest {

inline bool IsA3SocName(std::string_view name)
{
    constexpr std::array<std::string_view, 6U> kNames = {
        "Ascend910_9391", "Ascend910_9381", "Ascend910_9392",
        "Ascend910_9382", "Ascend910_9372", "Ascend910_9362",
    };
    for (std::string_view candidate : kNames) {
        if (name == candidate) { return true; }
    }
    return false;
}

inline bool IsA5SocName(std::string_view name)
{
    constexpr std::string_view kPrefix = "Ascend950";
    return name.size() >= kPrefix.size() && name.compare(0U, kPrefix.size(), kPrefix) == 0;
}

inline bool IsSupportedDirectRtsqSoc(const char *name)
{
    if (name == nullptr) { return false; }
    const std::string_view soc_name(name);
    return IsA3SocName(soc_name) || IsA5SocName(soc_name);
}

}  // namespace acltest

#endif  // ACLTEST_SOC_SUPPORT_H
