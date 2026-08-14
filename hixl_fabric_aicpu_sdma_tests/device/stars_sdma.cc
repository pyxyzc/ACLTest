/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "stars_sdma.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>

#include "ascend_hal_error.h"
#include "hal_pkg/trs_pkg.h"
#include "rtsq_query.h"

namespace acltest {
namespace {

constexpr uint32_t kSuccess = 0U;
constexpr uint32_t kFailure = 1U;
constexpr uint32_t kMaxEntriesPerPublish = 128U;
constexpr uint32_t kDefaultPollTimeoutMs = 60000U;
constexpr uint64_t kNsPerSecond = 1000000000ULL;
constexpr uint64_t kNsPerMillisecond = 1000000ULL;
constexpr uint32_t kLogicCqReportCount = 32U;
constexpr size_t kLogicCqeBytes = 32U;
constexpr uint8_t kStarsErrorMask = 0x3FU;
constexpr uint32_t kAnyTaskId = 0xFFFFU;
constexpr uint32_t kMatchCopyVersion = 1U;

extern "C" {
drvError_t __attribute__((weak)) halSqCqQuery(uint32_t dev_id, struct halSqCqQueryInfo *info);
drvError_t __attribute__((weak)) halSqCqConfig(uint32_t dev_id, struct halSqCqConfigInfo *info);
drvError_t __attribute__((weak)) drvGetLocalDevIDByHostDevID(uint32_t host_dev_id, uint32_t *local_dev_id);
drvError_t __attribute__((weak)) halCqReportRecv(uint32_t dev_id, struct halReportRecvInfo *info);
drvError_t __attribute__((weak)) halResourceIdRestore(DriverResourceIdKey *info);
}

void LogError(const char *message) {
  std::printf("[AclTest][AICPU][ERROR] %s\n", message);
}

}  // namespace

uint32_t StarsSdma::Submit(const AicpuKernelParam &param, const AicpuTransferDesc *descriptors) {
  if (!Validate(param, descriptors)) {
    return kFailure;
  }
  RtsqState state;
  if (!InitializeRtsq(param, state)) {
    LogError("failed to initialize worker RTSQ");
    return kFailure;
  }
  uint64_t deadline = 0U;
  if (!BuildDeadline(param.timeout_ms, deadline)) {
    return kFailure;
  }
  RtsqBatch batch;
  const bool sdma_ok = AppendDescriptors(descriptors, param.desc_count, state, batch, deadline) &&
                       Publish(state, batch, deadline);

  // The control stream is already waiting for this notify. Emit it even after
  // an SDMA/CQ error so a failure does not turn into a 60-second host hang.
  bool notify_ok = true;
  if (param.emit_notify_record != 0U) {
    const bool cq_ok = PollLogicCqUntilEmpty(state, deadline);
    notify_ok = AppendNotify(param.notify_id, state, batch, deadline) && Publish(state, batch, deadline);
    notify_ok = cq_ok && notify_ok;
  }
  return sdma_ok && notify_ok ? kSuccess : kFailure;
}

bool StarsSdma::Validate(const AicpuKernelParam &param, const AicpuTransferDesc *descriptors) {
  if (descriptors == nullptr || param.desc_count == 0U || param.desc_count > kMaxDescriptorsPerLaunch ||
      param.emit_notify_record > 1U ||
      (param.emit_notify_record != 0U && param.notify_id >= kA3NotifyIdLimit)) {
    LogError("invalid batch kernel arguments");
    return false;
  }
  for (uint32_t index = 0U; index < param.desc_count; ++index) {
    const AicpuTransferDesc &descriptor = descriptors[index];
    if (descriptor.src_addr == 0U || descriptor.dst_addr == 0U || descriptor.length == 0U ||
        descriptor.length > std::numeric_limits<uint32_t>::max() ||
        descriptor.length > std::numeric_limits<uint64_t>::max() - descriptor.src_addr ||
        descriptor.length > std::numeric_limits<uint64_t>::max() - descriptor.dst_addr) {
      LogError("invalid SDMA descriptor");
      return false;
    }
  }
  return true;
}

bool StarsSdma::ResolveLocalDevice(uint32_t host_device_id, uint32_t &local_device_id) {
  if (drvGetLocalDevIDByHostDevID == nullptr) {
    return false;
  }
  return drvGetLocalDevIDByHostDevID(host_device_id, &local_device_id) == DRV_ERROR_NONE;
}

bool StarsSdma::RestoreStream(const RtsqState &state) {
  if (halResourceIdRestore == nullptr) {
    return true;
  }
  DriverResourceIdKey resource{};
  resource.ruDevId = state.device_id;
  resource.resType = static_cast<uint32_t>(DRV_STREAM_ID);
  resource.resId = state.stream_id;
  return halResourceIdRestore(&resource) == DRV_ERROR_NONE;
}

bool StarsSdma::LoadQueueState(RtsqState &state) {
  uint32_t base_low = 0U;
  uint32_t base_high = 0U;
  if (!QueryRtsqValues(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_BASE, base_low, base_high,
                        halSqCqQuery) ||
      !QueryRtsqValue(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_DEPTH, state.depth,
                      halSqCqQuery) ||
      !QueryRtsqValue(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_HEAD, state.head,
                      halSqCqQuery) ||
      !QueryRtsqValue(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_TAIL, state.tail,
                      halSqCqQuery)) {
    return false;
  }
  state.base_addr = (static_cast<uint64_t>(base_high) << 32U) | base_low;
  return state.base_addr != 0U && state.depth > kMaxEntriesPerPublish && state.head < state.depth &&
         state.tail < state.depth;
}

bool StarsSdma::InitializeRtsq(const AicpuKernelParam &param, RtsqState &state) {
  if (param.rtsq_id > std::numeric_limits<uint16_t>::max() ||
      param.rtsq_stream_id > std::numeric_limits<uint16_t>::max() || halSqCqQuery == nullptr ||
      halSqCqConfig == nullptr || !ResolveLocalDevice(param.device_id, state.device_id)) {
    return false;
  }
  state.sq_id = param.rtsq_id;
  state.stream_id = param.rtsq_stream_id;
  state.logic_cq_id = param.rtsq_logic_cq_id;
  state.next_task_id = param.rtsq_task_id;
  return RestoreStream(state) && LoadQueueState(state);
}

uint64_t StarsSdma::MonotonicNs() {
  timespec value{};
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
    std::printf("[AclTest][AICPU][ERROR] clock_gettime failed, errno=%d\n", errno);
    return 0U;
  }
  return static_cast<uint64_t>(value.tv_sec) * kNsPerSecond + static_cast<uint64_t>(value.tv_nsec);
}

bool StarsSdma::BuildDeadline(uint32_t timeout_ms, uint64_t &deadline) {
  const uint64_t now = MonotonicNs();
  if (now == 0U) {
    return false;
  }
  const uint64_t effective_timeout = timeout_ms == 0U ? kDefaultPollTimeoutMs : timeout_ms;
  deadline = now + effective_timeout * kNsPerMillisecond;
  return true;
}

LogicCqResult StarsSdma::ReceiveLogicCq(const RtsqState &state, uint8_t *reports, uint32_t &count) {
  halReportRecvInfo info{};
  info.type = DRV_LOGIC_TYPE;
  info.tsId = 0U;
  info.report_cqe_num = 0U;
  info.stream_id = state.stream_id;
  info.cqId = state.logic_cq_id;
  info.timeout = 0U;
  info.task_id = kAnyTaskId;
  info.cqe_addr = reports;
  info.cqe_num = kLogicCqReportCount;
  info.res[0] = kMatchCopyVersion;
  const drvError_t result = halCqReportRecv(state.device_id, &info);
  if (result == DRV_ERROR_WAIT_TIMEOUT || (result == DRV_ERROR_NONE && info.report_cqe_num == 0U)) {
    count = 0U;
    return LogicCqResult::kEmpty;
  }
  if (result != DRV_ERROR_NONE || info.report_cqe_num > kLogicCqReportCount) {
    return LogicCqResult::kError;
  }
  count = info.report_cqe_num;
  return LogicCqResult::kReports;
}

bool StarsSdma::PollLogicCqUntilEmpty(const RtsqState &state, uint64_t deadline) {
  if (halCqReportRecv == nullptr) {
    return true;
  }
  alignas(uint64_t) uint8_t reports[kLogicCqReportCount * kLogicCqeBytes]{};
  for (;;) {
    if (MonotonicNs() >= deadline) {
      LogError("logic CQ polling timed out");
      return false;
    }
    uint32_t count = 0U;
    const LogicCqResult result = ReceiveLogicCq(state, reports, count);
    if (result == LogicCqResult::kEmpty) {
      return true;
    }
    if (result == LogicCqResult::kError) {
      LogError("halCqReportRecv failed");
      return false;
    }
    for (uint32_t index = 0U; index < count; ++index) {
      const auto *report = reinterpret_cast<const LogicCqeView *>(
          reports + static_cast<size_t>(index) * kLogicCqeBytes);
      if ((report->error_type & kStarsErrorMask) != 0U) {
        std::printf("[AclTest][AICPU][ERROR] abnormal CQE: stream=%u task=%u code=%u type=%u sq=%u\n",
                    static_cast<uint32_t>(report->stream_id), static_cast<uint32_t>(report->task_id),
                    report->error_code, static_cast<uint32_t>(report->error_type),
                    static_cast<uint32_t>(report->sq_id));
        return false;
      }
    }
  }
}

bool StarsSdma::HasCapacity(const RtsqState &state, uint32_t count) {
  if (count == 0U || count >= state.depth) {
    return false;
  }
  const uint32_t used = (state.tail + state.depth - state.head) % state.depth;
  return used + count < state.depth;
}

bool StarsSdma::EnsureCapacity(RtsqState &state, uint32_t count, uint64_t deadline) {
  if (HasCapacity(state, count)) {
    return true;
  }
  if (!PollLogicCqUntilEmpty(state, deadline) ||
      !QueryRtsqValue(state.device_id, state.sq_id, DRV_SQCQ_PROP_SQ_HEAD, state.head,
                      halSqCqQuery)) {
    return false;
  }
  if (!HasCapacity(state, count)) {
    std::printf("[AclTest][AICPU][ERROR] worker RTSQ has no capacity: depth=%u head=%u tail=%u need=%u "
                "host_budget=%u required_depth=%u\n",
                state.depth, state.head, state.tail, count, kMaxInFlightRtsqTasks, kMinRtsqDepth);
    return false;
  }
  return true;
}

bool StarsSdma::BuildSdmaSqe(const AicpuTransferDesc &descriptor, uint32_t task_id,
                              const RtsqState &state, A3SdmaSqe &sqe) {
  std::memset(&sqe, 0, sizeof(sqe));
  sqe.header.type = kA3SdmaSqeType;
  sqe.header.wr_cqe = 0U;
  sqe.header.rt_stream_id = static_cast<uint16_t>(state.stream_id);
  sqe.header.task_id = static_cast<uint16_t>(task_id);
  sqe.kernel_credit = kA3SdmaKernelCredit;
  sqe.sssv = 1U;
  sqe.dssv = 1U;
  sqe.sns = 1U;
  sqe.dns = 1U;
  sqe.qos = kA3SdmaDefaultQos;
  sqe.length = static_cast<uint32_t>(descriptor.length);
  sqe.src_addr_low = static_cast<uint32_t>(descriptor.src_addr);
  sqe.src_addr_high = static_cast<uint32_t>(descriptor.src_addr >> 32U);
  sqe.dst_addr_low = static_cast<uint32_t>(descriptor.dst_addr);
  sqe.dst_addr_high = static_cast<uint32_t>(descriptor.dst_addr >> 32U);
  sqe.link_type = kA3SdmaLinkOnChip;
  return true;
}

bool StarsSdma::BuildNotifySqe(uint32_t notify_id, uint32_t task_id, const RtsqState &state,
                               A3NotifySqe &sqe) {
  std::memset(&sqe, 0, sizeof(sqe));
  sqe.header.type = kA3NotifyRecordSqeType;
  sqe.header.wr_cqe = 0U;
  sqe.header.rt_stream_id = static_cast<uint16_t>(state.stream_id);
  sqe.header.task_id = static_cast<uint16_t>(task_id);
  sqe.notify_id = notify_id;
  sqe.kernel_credit = kA3NotifyKernelCredit;
  return true;
}

bool StarsSdma::CopyToRing(const RtsqState &state, const RtsqBatch &batch) {
  auto *base = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(state.base_addr));
  const uint32_t first_count = std::min(batch.count, state.depth - state.tail);
  const size_t first_bytes = static_cast<size_t>(first_count) * kA3RtsqEntryBytes;
  std::memcpy(base + static_cast<size_t>(state.tail) * kA3RtsqEntryBytes, batch.entries, first_bytes);
  if (first_count < batch.count) {
    const size_t wrap_bytes = static_cast<size_t>(batch.count - first_count) * kA3RtsqEntryBytes;
    std::memcpy(base, batch.entries + first_count, wrap_bytes);
  }
  return true;
}

bool StarsSdma::CommitTail(RtsqState &state, uint32_t new_tail) {
  std::atomic_thread_fence(std::memory_order_release);
  halSqCqConfigInfo config{};
  config.type = DRV_NORMAL_TYPE;
  config.tsId = 0U;
  config.sqId = state.sq_id;
  config.cqId = 0U;
  config.prop = DRV_SQCQ_PROP_SQ_TAIL;
  config.value[0U] = new_tail;
  if (halSqCqConfig(state.device_id, &config) != DRV_ERROR_NONE) {
    return false;
  }
  state.tail = new_tail;
  return true;
}

bool StarsSdma::Publish(RtsqState &state, RtsqBatch &batch, uint64_t deadline) {
  if (batch.count == 0U) {
    return true;
  }
  if (!EnsureCapacity(state, batch.count, deadline)) {
    return false;
  }
  const uint32_t new_tail = (state.tail + batch.count) % state.depth;
  if (!CopyToRing(state, batch) || !CommitTail(state, new_tail)) {
    return false;
  }
  batch.count = 0U;
  return true;
}

bool StarsSdma::AppendDescriptors(const AicpuTransferDesc *descriptors, uint32_t count,
                                   RtsqState &state, RtsqBatch &batch, uint64_t deadline) {
  for (uint32_t index = 0U; index < count; ++index) {
    if (batch.count == kMaxEntriesPerPublish && !Publish(state, batch, deadline)) {
      return false;
    }
    if (!BuildSdmaSqe(descriptors[index], state.next_task_id++, state,
                      batch.entries[batch.count].sdma)) {
      return false;
    }
    ++batch.count;
  }
  return true;
}

bool StarsSdma::AppendNotify(uint32_t notify_id, RtsqState &state, RtsqBatch &batch,
                             uint64_t deadline) {
  if (batch.count == kMaxEntriesPerPublish && !Publish(state, batch, deadline)) {
    return false;
  }
  if (!BuildNotifySqe(notify_id, state.next_task_id++, state, batch.entries[batch.count].notify)) {
    return false;
  }
  ++batch.count;
  return true;
}

}  // namespace acltest
