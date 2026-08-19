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

#include "acltest/hixl_ops_launcher.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include "acltest/acl_check.h"
#include "runtime/rt_external_stream.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace acltest {
namespace {

constexpr const char *kBatchReadFunction = "HixlFabricMemBatchRead";
constexpr const char *kBatchWriteFunction = "HixlFabricMemBatchWrite";
constexpr const char *kSyncContextFunction = "HixlSyncTransferContext";
constexpr uint32_t kKernelBlockDim = 1U;
constexpr uint32_t kNotifyIdLimit = 1U << 13U;
constexpr uint32_t kDefaultTimeoutMs = 60000U;
constexpr uint32_t kSyncContextTimeoutMs = 10000U;
constexpr uint32_t kDeleteRetryMs = 30000U;
constexpr uint32_t kDeleteRetryIntervalMs = 100U;

void LogCleanupAcl(const char *operation, aclError error) noexcept
{
    if (error != ACL_SUCCESS) {
        std::cerr << "warning: " << operation << " failed during cleanup, aclError=" << error
                  << '\n';
    }
}

void LoadFunction(aclrtBinHandle binary, const char *name, aclrtFuncHandle &function)
{
    const aclError error = aclrtBinaryGetFunction(binary, name, &function);
    if (error != ACL_SUCCESS) {
        throw AclException(std::string("aclrtBinaryGetFunction(") + name + ")", error);
    }
}

std::string ResolveReadableRegularFile(const std::string &path)
{
    char resolved_path[PATH_MAX] = {0};
    errno = 0;
    if (realpath(path.c_str(), resolved_path) == nullptr) {
        const int error = errno;
        throw std::runtime_error("HIXL kernel JSON cannot be resolved: path=" + path +
                                 ", errno=" + std::to_string(error) + " (" +
                                 std::strerror(error) + ")");
    }

    struct stat file_status {};
    if (stat(resolved_path, &file_status) != 0) {
        const int error = errno;
        throw std::runtime_error("HIXL kernel JSON cannot be stat'ed: path=" +
                                 std::string(resolved_path) + ", errno=" +
                                 std::to_string(error) + " (" + std::strerror(error) + ")");
    }
    if (!S_ISREG(file_status.st_mode)) {
        throw std::runtime_error("HIXL kernel JSON is not a regular file: " +
                                 std::string(resolved_path));
    }
    if (access(resolved_path, R_OK) != 0) {
        const int error = errno;
        throw std::runtime_error("HIXL kernel JSON is not readable: path=" +
                                 std::string(resolved_path) + ", errno=" +
                                 std::to_string(error) + " (" + std::strerror(error) + ")");
    }
    return resolved_path;
}

}  // namespace

HixlOpsLauncher::~HixlOpsLauncher() { Shutdown(); }

void HixlOpsLauncher::LoadKernel(const std::string &kernel_json)
{
    if (kernel_json.empty()) {
        throw std::invalid_argument("official HIXL kernel JSON path cannot be empty");
    }
    const std::string resolved_kernel_json = ResolveReadableRegularFile(kernel_json);
    aclrtBinaryLoadOption option{};
    option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpuKernelMode = 0U;
    aclrtBinaryLoadOptions options{};
    options.numOpt = 1U;
    options.options = &option;
    const aclError load_error =
        aclrtBinaryLoadFromFile(resolved_kernel_json.c_str(), &options, &binary_);
    if (load_error != ACL_SUCCESS) {
        throw AclException("aclrtBinaryLoadFromFile(" + resolved_kernel_json + ")", load_error);
    }
    LoadFunction(binary_, kBatchReadFunction, batch_read_);
    LoadFunction(binary_, kBatchWriteFunction, batch_write_);
    LoadFunction(binary_, kSyncContextFunction, sync_context_);
}

void HixlOpsLauncher::CreateExecutionResources()
{
    ACLTEST_CHECK_ACL(aclrtCreateStreamWithConfig(&control_stream_, 0U,
                                                  ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC));
    ACLTEST_CHECK_ACL(aclrtSetStreamFailureMode(control_stream_, ACL_STOP_ON_FAILURE));
    ACLTEST_CHECK_ACL(aclrtCreateStreamWithConfig(&worker_stream_, 0U, ACL_STREAM_DEVICE_USE_ONLY));
    ACLTEST_CHECK_ACL(aclrtCreateNotify(&notify_, ACL_NOTIFY_DEVICE_USE_ONLY));
    ACLTEST_CHECK_ACL(aclrtGetNotifyId(notify_, &notify_id_));
    if (notify_id_ >= kNotifyIdLimit) {
        throw std::runtime_error("runtime notify id exceeds the A3/A5 13-bit NotifyRecord ABI");
    }
    transfer_context_key_ = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(notify_));
}

void HixlOpsLauncher::Initialize(int32_t logic_device_id, const std::string &kernel_json)
{
    if (initialized_ || logic_device_id < 0) {
        throw std::invalid_argument(
            "HIXL ops launcher is already initialized or device id is invalid");
    }
    logic_device_id_ = logic_device_id;
    int32_t physical = -1;
    try {
        ACLTEST_CHECK_ACL(aclrtGetPhyDevIdByLogicDevId(logic_device_id_, &physical));
        if (physical < 0) {
            throw std::runtime_error("runtime returned a negative physical device id");
        }
        physical_device_id_ = static_cast<uint32_t>(physical);
        LoadKernel(kernel_json);
        CreateExecutionResources();
        SyncTransferContext(kTransferContextAdd, kTransferContextInitialized);
        transfer_context_registered_ = true;
        initialized_ = true;
    } catch (...) {
        DestroyExecutionResources(false);
        if (binary_ != nullptr) {
            LogCleanupAcl("aclrtBinaryUnLoad", aclrtBinaryUnLoad(binary_));
            binary_ = nullptr;
        }
        logic_device_id_ = -1;
        throw;
    }
}

void HixlOpsLauncher::BuildRtsqParam(uint32_t task_count, HixlKernelParam &param)
{
    if (worker_stream_ == nullptr || task_count == 0U) {
        throw std::invalid_argument("invalid RTSQ stream or task count");
    }
    int32_t stream_id = -1;
    ACLTEST_CHECK_ACL(aclrtStreamGetId(worker_stream_, &stream_id));
    if (stream_id < 0 || stream_id > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error("worker stream id exceeds the A3/A5 SQE ABI");
    }
    uint32_t sq_id = 0U;
    if (rtStreamGetSqid(reinterpret_cast<rtStream_t>(worker_stream_), &sq_id) != RT_ERROR_NONE) {
        throw std::runtime_error("rtStreamGetSqid failed");
    }
    uint32_t cq_id = 0U;
    uint32_t logic_cq_id = 0U;
    if (rtStreamGetCqid(reinterpret_cast<rtStream_t>(worker_stream_), &cq_id, &logic_cq_id) !=
        RT_ERROR_NONE) {
        throw std::runtime_error("rtStreamGetCqid failed");
    }
    (void)cq_id;
    param.device_id = physical_device_id_;
    param.rtsq_id = sq_id;
    param.rtsq_stream_id = static_cast<uint32_t>(stream_id);
    param.rtsq_logic_cq_id = logic_cq_id;
    param.rtsq_task_id = static_cast<uint16_t>(next_task_id_);
    next_task_id_ += task_count;
}

void HixlOpsLauncher::LaunchOneBatch(aclrtFuncHandle function, TransferDirection direction,
                                     size_t begin, size_t count, size_t descriptor_count,
                                     uint32_t timeout_ms, size_t status_index,
                                     size_t &tasks_since_notify)
{
    const size_t next_task_count = tasks_since_notify + count;
    const bool transfer_tail = begin + count == descriptor_count;
    const bool emit_notify = next_task_count >= kMaxInFlightRtsqTasks || transfer_tail;

    HixlKernelParam param{};
    param.desc_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(request_.descriptors) +
                                            begin * sizeof(HixlTransferDesc));
    param.desc_count = static_cast<uint32_t>(count);
    param.direction = static_cast<uint32_t>(direction);
    param.timeout_ms = timeout_ms;
    param.notify_id = notify_id_;
    param.emit_notify_record = emit_notify ? 1U : 0U;
    param.status_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(request_.statuses) +
                                              status_index * sizeof(uint32_t));
    param.transfer_ctx_key = transfer_context_key_;
    BuildRtsqParam(static_cast<uint32_t>(count) + (emit_notify ? 1U : 0U), param);

    void *device_args =
        static_cast<uint8_t *>(request_.kernel_args) + status_index * sizeof(HixlKernelParam);
    ACLTEST_CHECK_ACL(
        aclrtMemcpy(device_args, sizeof(param), &param, sizeof(param), ACL_MEMCPY_HOST_TO_DEVICE));
    ACLTEST_CHECK_ACL(aclrtLaunchKernelV2(function, kKernelBlockDim, device_args, sizeof(param),
                                          nullptr, control_stream_));
    request_launched_ = true;
    if (emit_notify) {
        const uint32_t effective_timeout = timeout_ms == 0U ? kDefaultTimeoutMs : timeout_ms;
        const uint32_t timeout_seconds =
            std::max(1U, effective_timeout / 1000U + ((effective_timeout % 1000U) == 0U ? 0U : 1U));
        ACLTEST_CHECK_ACL(aclrtWaitAndResetNotify(notify_, control_stream_, timeout_seconds));
        tasks_since_notify = 0U;
    } else {
        tasks_since_notify = next_task_count;
    }
}

void HixlOpsLauncher::LaunchBatches(TransferDirection direction, size_t descriptor_count,
                                    uint32_t timeout_ms)
{
    aclrtFuncHandle function =
        direction == TransferDirection::kDeviceToHost ? batch_read_ : batch_write_;
    size_t begin = 0U;
    size_t status_index = 0U;
    size_t tasks_since_notify = 0U;
    while (begin < descriptor_count) {
        const size_t count =
            std::min(static_cast<size_t>(kMaxDescriptorsPerLaunch), descriptor_count - begin);
        LaunchOneBatch(function, direction, begin, count, descriptor_count, timeout_ms,
                       status_index, tasks_since_notify);
        begin += count;
        ++status_index;
    }
}

void HixlOpsLauncher::Submit(TransferDirection direction,
                             const std::vector<AddressRange> &ranges, uint32_t timeout_ms)
{
    if (!initialized_ || request_active_) {
        throw std::logic_error(
            "HIXL ops launcher is not initialized or already has an active request");
    }
    const std::vector<HixlTransferDesc> descriptors = BuildDeviceDescriptors(direction, ranges);
    last_shape_ = ComputeSubmissionShape(descriptors.size());
    request_active_ = true;
    request_launched_ = false;
    request_.descriptor_bytes = descriptors.size() * sizeof(HixlTransferDesc);
    request_.status_count = last_shape_.kernel_launch_count;
    request_.kernel_arg_count = last_shape_.kernel_launch_count;

    try {
        ACLTEST_CHECK_ACL(aclrtMalloc(&request_.descriptors, request_.descriptor_bytes,
                                      ACL_MEM_MALLOC_NORMAL_ONLY));
        ACLTEST_CHECK_ACL(aclrtMalloc(&request_.statuses, request_.status_count * sizeof(uint32_t),
                                      ACL_MEM_MALLOC_NORMAL_ONLY));
        ACLTEST_CHECK_ACL(aclrtMalloc(&request_.kernel_args,
                                      request_.kernel_arg_count * sizeof(HixlKernelParam),
                                      ACL_MEM_MALLOC_NORMAL_ONLY));
        const std::vector<uint32_t> zero_status(request_.status_count, 0U);
        ACLTEST_CHECK_ACL(aclrtMemcpy(request_.statuses, request_.status_count * sizeof(uint32_t),
                                      zero_status.data(), zero_status.size() * sizeof(uint32_t),
                                      ACL_MEMCPY_HOST_TO_DEVICE));
        ACLTEST_CHECK_ACL(aclrtMemcpy(request_.descriptors, request_.descriptor_bytes,
                                      descriptors.data(), request_.descriptor_bytes,
                                      ACL_MEMCPY_HOST_TO_DEVICE));
        LaunchBatches(direction, descriptors.size(), timeout_ms);
    } catch (...) {
        if (request_launched_) {
            Abort();
        } else {
            ReleaseRequest();
        }
        throw;
    }
}

void HixlOpsLauncher::Synchronize(uint32_t timeout_ms)
{
    if (!request_active_) { throw std::logic_error("no active HIXL ops request to synchronize"); }
    const uint32_t effective_timeout = timeout_ms == 0U ? kDefaultTimeoutMs : timeout_ms;
    try {
        ACLTEST_CHECK_ACL(aclrtSynchronizeStreamWithTimeout(
            control_stream_, static_cast<int32_t>(effective_timeout)));
    } catch (...) {
        Abort();
        throw;
    }
}

void HixlOpsLauncher::Finish()
{
    if (!request_active_) { throw std::logic_error("no completed HIXL ops request to finish"); }
    std::vector<uint32_t> statuses(request_.status_count, 0U);
    try {
        ACLTEST_CHECK_ACL(aclrtMemcpy(statuses.data(), statuses.size() * sizeof(uint32_t),
                                      request_.statuses, request_.status_count * sizeof(uint32_t),
                                      ACL_MEMCPY_DEVICE_TO_HOST));
    } catch (...) {
        ReleaseRequest();
        throw;
    }
    ReleaseRequest();
    for (size_t index = 0U; index < statuses.size(); ++index) {
        if (statuses[index] != 0U) {
            throw std::runtime_error("HIXL ops launch status[" + std::to_string(index) +
                                     "]=" + std::to_string(statuses[index]));
        }
    }
}

uint32_t HixlOpsLauncher::SyncTransferContextOnce(uint32_t operation)
{
    HixlTransferContextSyncEntry entry{};
    entry.thread = transfer_context_key_;
    entry.op = operation;
    void *device_entry = nullptr;
    void *device_state = nullptr;
    void *device_args = nullptr;
    try {
        ACLTEST_CHECK_ACL(aclrtMalloc(&device_entry, sizeof(entry), ACL_MEM_MALLOC_NORMAL_ONLY));
        ACLTEST_CHECK_ACL(aclrtMalloc(&device_state, sizeof(uint32_t), ACL_MEM_MALLOC_NORMAL_ONLY));
        ACLTEST_CHECK_ACL(aclrtMemcpy(device_entry, sizeof(entry), &entry, sizeof(entry),
                                      ACL_MEMCPY_HOST_TO_DEVICE));
        HixlTransferContextSyncParam param{};
        param.entry_list_addr = reinterpret_cast<uintptr_t>(device_entry);
        param.state_list_addr = reinterpret_cast<uintptr_t>(device_state);
        param.entry_num = 1U;
        ACLTEST_CHECK_ACL(aclrtMalloc(&device_args, sizeof(param), ACL_MEM_MALLOC_NORMAL_ONLY));
        ACLTEST_CHECK_ACL(aclrtMemcpy(device_args, sizeof(param), &param, sizeof(param),
                                      ACL_MEMCPY_HOST_TO_DEVICE));
        aclrtStream default_stream = nullptr;
        ACLTEST_CHECK_ACL(aclrtCtxGetCurrentDefaultStream(&default_stream));
        ACLTEST_CHECK_ACL(aclrtLaunchKernelV2(sync_context_, kKernelBlockDim, device_args,
                                              sizeof(param), nullptr, default_stream));
        ACLTEST_CHECK_ACL(aclrtSynchronizeStreamWithTimeout(
            default_stream, static_cast<int32_t>(kSyncContextTimeoutMs)));
        uint32_t state = kTransferContextDeleted;
        ACLTEST_CHECK_ACL(aclrtMemcpy(&state, sizeof(state), device_state, sizeof(state),
                                      ACL_MEMCPY_DEVICE_TO_HOST));
        ACLTEST_CHECK_ACL(aclrtFree(device_args));
        device_args = nullptr;
        ACLTEST_CHECK_ACL(aclrtFree(device_state));
        device_state = nullptr;
        ACLTEST_CHECK_ACL(aclrtFree(device_entry));
        device_entry = nullptr;
        return state;
    } catch (...) {
        if (device_args != nullptr) {
            LogCleanupAcl("aclrtFree(sync args)", aclrtFree(device_args));
        }
        if (device_state != nullptr) {
            LogCleanupAcl("aclrtFree(sync state)", aclrtFree(device_state));
        }
        if (device_entry != nullptr) {
            LogCleanupAcl("aclrtFree(sync entry)", aclrtFree(device_entry));
        }
        throw;
    }
}

void HixlOpsLauncher::SyncTransferContext(uint32_t operation, uint32_t expected_state)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kDeleteRetryMs);
    for (;;) {
        const uint32_t state = SyncTransferContextOnce(operation);
        if (state == expected_state) { return; }
        if (operation != kTransferContextDelete || state != kTransferContextDeleting) {
            throw std::runtime_error("unexpected HIXL transfer context state " +
                                     std::to_string(state));
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("timed out deleting HIXL transfer context");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kDeleteRetryIntervalMs));
    }
}

void HixlOpsLauncher::ReleaseRequest() noexcept
{
    if (request_.kernel_args != nullptr) {
        LogCleanupAcl("aclrtFree(kernel args)", aclrtFree(request_.kernel_args));
    }
    if (request_.statuses != nullptr) {
        LogCleanupAcl("aclrtFree(statuses)", aclrtFree(request_.statuses));
    }
    if (request_.descriptors != nullptr) {
        LogCleanupAcl("aclrtFree(descriptors)", aclrtFree(request_.descriptors));
    }
    request_ = {};
    request_active_ = false;
    request_launched_ = false;
}

void HixlOpsLauncher::DestroyExecutionResources(bool abort_streams) noexcept
{
    if (control_stream_ != nullptr) {
        if (abort_streams) {
            LogCleanupAcl("aclrtStreamAbort(control)", aclrtStreamAbort(control_stream_));
        }
        LogCleanupAcl("aclrtDestroyStream(control)", aclrtDestroyStream(control_stream_));
        control_stream_ = nullptr;
    }
    if (worker_stream_ != nullptr) {
        if (abort_streams) {
            LogCleanupAcl("aclrtStreamStop(worker)", aclrtStreamStop(worker_stream_));
        }
        LogCleanupAcl("aclrtDestroyStream(worker)", aclrtDestroyStream(worker_stream_));
        worker_stream_ = nullptr;
    }
    if (notify_ != nullptr) {
        LogCleanupAcl("aclrtDestroyNotify", aclrtDestroyNotify(notify_));
        notify_ = nullptr;
    }
}

void HixlOpsLauncher::Abort() noexcept
{
    if (control_stream_ != nullptr) {
        LogCleanupAcl("aclrtStreamAbort(control)", aclrtStreamAbort(control_stream_));
    }
    if (worker_stream_ != nullptr) {
        LogCleanupAcl("aclrtStreamStop(worker)", aclrtStreamStop(worker_stream_));
    }
    if (transfer_context_registered_) {
        try {
            SyncTransferContext(kTransferContextDelete, kTransferContextDeleted);
        } catch (const std::exception &error) {
            std::cerr << "warning: transfer context cleanup after abort failed: " << error.what()
                      << '\n';
        }
        transfer_context_registered_ = false;
    }
    ReleaseRequest();
    initialized_ = false;
}

void HixlOpsLauncher::Shutdown() noexcept
{
    if (request_active_) {
        Abort();
    } else if (transfer_context_registered_) {
        try {
            SyncTransferContext(kTransferContextDelete, kTransferContextDeleted);
        } catch (const std::exception &error) {
            std::cerr << "warning: transfer context cleanup failed: " << error.what() << '\n';
        }
        transfer_context_registered_ = false;
    }
    DestroyExecutionResources(false);
    if (binary_ != nullptr) {
        LogCleanupAcl("aclrtBinaryUnLoad", aclrtBinaryUnLoad(binary_));
        binary_ = nullptr;
    }
    batch_read_ = nullptr;
    batch_write_ = nullptr;
    sync_context_ = nullptr;
    initialized_ = false;
    logic_device_id_ = -1;
}

}  // namespace acltest
