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

#include "acltest/soc_support.h"
#include <cassert>
#include <iostream>

int main()
{
    using acltest::IsSupportedDirectRtsqSoc;

    assert(IsSupportedDirectRtsqSoc("Ascend910_9391"));
    assert(IsSupportedDirectRtsqSoc("Ascend910_9381"));
    assert(IsSupportedDirectRtsqSoc("Ascend910_9392"));
    assert(IsSupportedDirectRtsqSoc("Ascend910_9382"));
    assert(IsSupportedDirectRtsqSoc("Ascend910_9372"));
    assert(IsSupportedDirectRtsqSoc("Ascend910_9362"));

    assert(IsSupportedDirectRtsqSoc("Ascend950"));
    assert(IsSupportedDirectRtsqSoc("Ascend950PR_9599"));
    assert(IsSupportedDirectRtsqSoc("Ascend950DT_9591"));
    assert(IsSupportedDirectRtsqSoc("Ascend950B"));

    assert(!IsSupportedDirectRtsqSoc(nullptr));
    assert(!IsSupportedDirectRtsqSoc(""));
    assert(!IsSupportedDirectRtsqSoc("Ascend910B1"));
    assert(!IsSupportedDirectRtsqSoc("prefix_Ascend950PR_9599"));

    std::cout << "soc_support_test: PASS\n";
    return 0;
}
