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

#ifndef ACLTEST_DEVICE_A3_RTSQ_H_
#define ACLTEST_DEVICE_A3_RTSQ_H_

#include <cstdint>

namespace acltest {

constexpr uint8_t kA3SdmaSqeType = 11U;
constexpr uint8_t kA3SdmaKernelCredit = 240U;
constexpr uint8_t kA3SdmaLinkOnChip = 0U;
constexpr uint8_t kA3SdmaDefaultQos = 6U;
constexpr uint8_t kA3NotifyRecordSqeType = 6U;
constexpr uint8_t kA3NotifyKernelCredit = 254U;
constexpr uint32_t kA3NotifyIdLimit = 1U << 13U;
constexpr uint32_t kA3RtsqEntryBytes = 64U;

#pragma pack(push, 1)
struct A3StarsSqeHeader {
    uint8_t type : 6;
    uint8_t l1_lock : 1;
    uint8_t l1_unlock : 1;
    uint8_t ie : 2;
    uint8_t pre_p : 2;
    uint8_t post_p : 2;
    uint8_t wr_cqe : 1;
    uint8_t reserved : 1;
    uint16_t block_dim;
    uint16_t rt_stream_id;
    uint16_t task_id;
};
static_assert(sizeof(A3StarsSqeHeader) == 8U, "A3 STARS SQE header must be 8 bytes");

struct A3SdmaSqe {
    A3StarsSqeHeader header;
    uint32_t reserved0;
    uint16_t reserved1;
    uint8_t kernel_credit;
    uint8_t ptr_mode : 1;
    uint8_t reserved2 : 7;
    uint32_t opcode : 8;
    uint32_t ie2 : 1;
    uint32_t sssv : 1;
    uint32_t dssv : 1;
    uint32_t sns : 1;
    uint32_t dns : 1;
    uint32_t qos : 4;
    uint32_t sro : 1;
    uint32_t dro : 1;
    uint32_t partid : 8;
    uint32_t mpam : 1;
    uint32_t reserved3 : 4;
    uint16_t src_stream_id;
    uint16_t src_substream_id;
    uint16_t dst_stream_id;
    uint16_t dst_substream_id;
    uint32_t length;
    uint32_t src_addr_low;
    uint32_t src_addr_high;
    uint32_t dst_addr_low;
    uint32_t dst_addr_high;
    uint8_t link_type;
    uint8_t reserved4[3];
    uint32_t reserved5[3];
};

struct A3NotifySqe {
    A3StarsSqeHeader header;
    uint32_t notify_id : 13;
    uint32_t reserved0 : 19;
    uint16_t reserved1;
    uint8_t kernel_credit;
    uint8_t reserved2;
    uint32_t timeout;
    uint32_t reserved3[11];
};
#pragma pack(pop)

static_assert(sizeof(A3SdmaSqe) == kA3RtsqEntryBytes, "A3 SDMA SQE ABI changed");
static_assert(sizeof(A3NotifySqe) == kA3RtsqEntryBytes, "A3 NotifyRecord SQE ABI changed");

}  // namespace acltest

#endif  // ACLTEST_DEVICE_A3_RTSQ_H_
