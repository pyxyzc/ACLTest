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

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "acl/acl.h"
#include "acltest/a5_hal_test_types.h"
#include "ascend_hal_error.h"
#include "runtime/rt_external_stream.h"

namespace acltest {
namespace {

constexpr const char *kKernelFunction = "AclTestA5HalTest";
constexpr uint32_t kKernelBlockDim = 1U;
constexpr uint32_t kDefaultTimeoutMs = 3000U;
constexpr int kExitTimeout = 124;

struct Options {
    int32_t device_id = 0;
    uint32_t timeout_ms = kDefaultTimeoutMs;
    std::string kernel_json;
    A5HalTestCase test_case = A5HalTestCase::kQuerySqBase;
    std::string case_name;
};

void CheckAcl(aclError error, const char *operation)
{
    if (error != ACL_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed, aclError=" +
                                 std::to_string(error));
    }
}

void CleanupAcl(const char *operation, aclError error) noexcept
{
    if (error != ACL_SUCCESS) {
        std::cerr << "warning: " << operation << " failed during cleanup, aclError=" << error
                  << '\n';
    }
}

bool IsA5Soc(const char *soc_name)
{
    return soc_name != nullptr && std::string_view(soc_name).rfind("Ascend950", 0U) == 0U;
}

A5HalTestCase ParseCase(const std::string &value)
{
    if (value == "query_sq_base") { return A5HalTestCase::kQuerySqBase; }
    if (value == "query_sq_depth") { return A5HalTestCase::kQuerySqDepth; }
    if (value == "query_sq_head") { return A5HalTestCase::kQuerySqHead; }
    if (value == "query_sq_tail") { return A5HalTestCase::kQuerySqTail; }
    if (value == "restore_stream") { return A5HalTestCase::kRestoreStream; }
    if (value == "config_tail") { return A5HalTestCase::kConfigTail; }
    if (value == "report_empty_cq") { return A5HalTestCase::kReportEmptyCq; }
    throw std::invalid_argument("unknown --case: " + value);
}

uint32_t ParseUint32(const char *text, const char *option)
{
    try {
        size_t parsed = 0U;
        const unsigned long long value = std::stoull(text, &parsed, 10);
        if (text[0] == '\0' || parsed != std::strlen(text) ||
            value > std::numeric_limits<uint32_t>::max()) {
            throw std::invalid_argument("invalid value");
        }
        return static_cast<uint32_t>(value);
    } catch (const std::exception &) {
        throw std::invalid_argument(std::string("invalid ") + option + ": " + text);
    }
}

std::string DefaultKernelJson()
{
    const char *configured = std::getenv("ACLTEST_A5_HAL_TEST_KERNEL_JSON");
    if (configured != nullptr && configured[0] != '\0') { return configured; }
    const char *ascend_home = std::getenv("ASCEND_HOME_PATH");
    if (ascend_home == nullptr || ascend_home[0] == '\0') { return {}; }
    return std::string(ascend_home) +
           "/opp/built-in/op_impl/aicpu/config/libacltest_a5_hal_test.json";
}

void PrintUsage(const char *program)
{
    std::cout << "Usage: " << program
              << " --case CASE [--device N] [--timeout-ms N] [--kernel-json PATH]\n"
              << "CASE: query_sq_base, query_sq_depth, query_sq_head, query_sq_tail,\n"
              << "      restore_stream, config_tail, report_empty_cq\n";
}

Options ParseOptions(int argc, char **argv)
{
    Options options;
    options.kernel_json = DefaultKernelJson();
    bool has_case = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--case") {
            if (++index >= argc) { throw std::invalid_argument("--case requires a value"); }
            options.test_case = ParseCase(argv[index]);
            options.case_name = argv[index];
            has_case = true;
        } else if (argument == "--device") {
            if (++index >= argc) { throw std::invalid_argument("--device requires a value"); }
            const uint32_t value = ParseUint32(argv[index], "--device");
            if (value > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--device is outside int32 range");
            }
            options.device_id = static_cast<int32_t>(value);
        } else if (argument == "--timeout-ms") {
            if (++index >= argc) { throw std::invalid_argument("--timeout-ms requires a value"); }
            options.timeout_ms = ParseUint32(argv[index], "--timeout-ms");
            if (options.timeout_ms == 0U ||
                options.timeout_ms > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--timeout-ms must be in [1, INT32_MAX]");
            }
        } else if (argument == "--kernel-json") {
            if (++index >= argc) { throw std::invalid_argument("--kernel-json requires a path"); }
            options.kernel_json = argv[index];
        } else if (argument == "-h" || argument == "--help") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    if (!has_case) { throw std::invalid_argument("--case is required"); }
    if (options.kernel_json.empty()) {
        throw std::invalid_argument(
            "--kernel-json is required when ASCEND_HOME_PATH and "
            "ACLTEST_A5_HAL_TEST_KERNEL_JSON are unset");
    }
    return options;
}

class RuntimeGuard {
public:
    explicit RuntimeGuard(int32_t device_id) : device_id_(device_id)
    {
        try {
            CheckAcl(aclInit(nullptr), "aclInit");
            initialized_ = true;
            CheckAcl(aclrtSetDevice(device_id_), "aclrtSetDevice");
            device_set_ = true;
            CheckAcl(aclrtCreateContext(&context_, device_id_), "aclrtCreateContext");
            CheckAcl(aclrtSetCurrentContext(context_), "aclrtSetCurrentContext");
        } catch (...) {
            Reset();
            throw;
        }
    }

    ~RuntimeGuard() { Reset(); }

    RuntimeGuard(const RuntimeGuard &) = delete;
    RuntimeGuard &operator=(const RuntimeGuard &) = delete;

private:
    void Reset() noexcept
    {
        if (context_ != nullptr) {
            CleanupAcl("aclrtDestroyContext", aclrtDestroyContext(context_));
            context_ = nullptr;
        }
        if (device_set_) {
            CleanupAcl("aclrtResetDevice", aclrtResetDevice(device_id_));
            device_set_ = false;
        }
        if (initialized_) {
            CleanupAcl("aclFinalize", aclFinalize());
            initialized_ = false;
        }
    }

    int32_t device_id_ = 0;
    aclrtContext context_ = nullptr;
    bool initialized_ = false;
    bool device_set_ = false;
};

class HalTestSession {
public:
    HalTestSession(int32_t logical_device_id, const std::string &kernel_json)
    {
        int32_t physical_device_id = -1;
        CheckAcl(aclrtGetPhyDevIdByLogicDevId(logical_device_id, &physical_device_id),
                 "aclrtGetPhyDevIdByLogicDevId");
        if (physical_device_id < 0) {
            throw std::runtime_error("runtime returned a negative physical device id");
        }
        host_device_id_ = static_cast<uint32_t>(physical_device_id);

        try {
            aclrtBinaryLoadOption option{};
            option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
            option.value.cpuKernelMode = 0U;
            aclrtBinaryLoadOptions options{};
            options.numOpt = 1U;
            options.options = &option;
            CheckAcl(aclrtBinaryLoadFromFile(kernel_json.c_str(), &options, &binary_),
                     "aclrtBinaryLoadFromFile");
            CheckAcl(aclrtBinaryGetFunction(binary_, kKernelFunction, &kernel_function_),
                     "aclrtBinaryGetFunction(AclTestA5HalTest)");

            CheckAcl(aclrtCreateStreamWithConfig(
                         &control_stream_, 0U, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC),
                     "aclrtCreateStreamWithConfig(control)");
            CheckAcl(aclrtSetStreamFailureMode(control_stream_, ACL_STOP_ON_FAILURE),
                     "aclrtSetStreamFailureMode(control)");
            CheckAcl(aclrtCreateStreamWithConfig(&worker_stream_, 0U, ACL_STREAM_DEVICE_USE_ONLY),
                     "aclrtCreateStreamWithConfig(worker)");

            int32_t stream_id = -1;
            CheckAcl(aclrtStreamGetId(worker_stream_, &stream_id), "aclrtStreamGetId(worker)");
            if (stream_id < 0 || stream_id > std::numeric_limits<uint16_t>::max()) {
                throw std::runtime_error("worker stream id is outside the RTSQ ABI range");
            }
            stream_id_ = static_cast<uint32_t>(stream_id);
            if (rtStreamGetSqid(reinterpret_cast<rtStream_t>(worker_stream_), &sq_id_) != RT_ERROR_NONE) {
                throw std::runtime_error("rtStreamGetSqid failed");
            }
            uint32_t ignored_cq_id = 0U;
            if (rtStreamGetCqid(reinterpret_cast<rtStream_t>(worker_stream_), &ignored_cq_id,
                                &logic_cq_id_) != RT_ERROR_NONE) {
                throw std::runtime_error("rtStreamGetCqid failed");
            }
        } catch (...) {
            Reset();
            throw;
        }
    }

    ~HalTestSession() { Reset(); }

    HalTestSession(const HalTestSession &) = delete;
    HalTestSession &operator=(const HalTestSession &) = delete;

    struct Execution {
        bool timed_out = false;
        A5HalTestResult result{};
    };

    Execution Run(A5HalTestCase test_case, uint32_t sq_tail, uint32_t timeout_ms)
    {
        void *device_param = nullptr;
        void *device_result = nullptr;
        try {
            CheckAcl(aclrtMalloc(&device_param, sizeof(A5HalTestParam), ACL_MEM_MALLOC_NORMAL_ONLY),
                     "aclrtMalloc(test param)");
            CheckAcl(aclrtMalloc(&device_result, sizeof(A5HalTestResult), ACL_MEM_MALLOC_NORMAL_ONLY),
                     "aclrtMalloc(test result)");

            A5HalTestParam param{};
            param.case_id = static_cast<uint32_t>(test_case);
            param.host_device_id = host_device_id_;
            param.sq_id = sq_id_;
            param.stream_id = stream_id_;
            param.logic_cq_id = logic_cq_id_;
            param.sq_tail = sq_tail;
            param.result_addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device_result));
            A5HalTestResult initial_result{};
            CheckAcl(aclrtMemcpy(device_param, sizeof(param), &param, sizeof(param),
                                 ACL_MEMCPY_HOST_TO_DEVICE),
                     "aclrtMemcpy(test param)");
            CheckAcl(aclrtMemcpy(device_result, sizeof(initial_result), &initial_result,
                                 sizeof(initial_result), ACL_MEMCPY_HOST_TO_DEVICE),
                     "aclrtMemcpy(test result init)");
            CheckAcl(aclrtLaunchKernelV2(kernel_function_, kKernelBlockDim, device_param, sizeof(param),
                                          nullptr, control_stream_),
                     "aclrtLaunchKernelV2(AclTestA5HalTest)");

            const aclError sync_error = aclrtSynchronizeStreamWithTimeout(
                control_stream_, static_cast<int32_t>(timeout_ms));
            if (sync_error == ACL_ERROR_RT_STREAM_SYNC_TIMEOUT) {
                CleanupAcl("aclrtStreamAbort(control)", aclrtStreamAbort(control_stream_));
                // One process executes one case. Device memory can still be referenced by the
                // timed-out AICPU task, so leave it for process teardown after the abort request.
                device_param = nullptr;
                device_result = nullptr;
                Execution execution;
                execution.timed_out = true;
                return execution;
            }
            CheckAcl(sync_error, "aclrtSynchronizeStreamWithTimeout");

            Execution execution;
            CheckAcl(aclrtMemcpy(&execution.result, sizeof(execution.result), device_result,
                                 sizeof(execution.result), ACL_MEMCPY_DEVICE_TO_HOST),
                     "aclrtMemcpy(test result)");
            CleanupAcl("aclrtFree(test result)", aclrtFree(device_result));
            device_result = nullptr;
            CleanupAcl("aclrtFree(test param)", aclrtFree(device_param));
            device_param = nullptr;
            return execution;
        } catch (...) {
            if (device_result != nullptr) { CleanupAcl("aclrtFree(test result)", aclrtFree(device_result)); }
            if (device_param != nullptr) { CleanupAcl("aclrtFree(test param)", aclrtFree(device_param)); }
            throw;
        }
    }

    uint32_t sq_id() const { return sq_id_; }
    uint32_t stream_id() const { return stream_id_; }
    uint32_t logic_cq_id() const { return logic_cq_id_; }

private:
    void Reset() noexcept
    {
        if (control_stream_ != nullptr) {
            CleanupAcl("aclrtDestroyStream(control)", aclrtDestroyStream(control_stream_));
            control_stream_ = nullptr;
        }
        if (worker_stream_ != nullptr) {
            CleanupAcl("aclrtDestroyStream(worker)", aclrtDestroyStream(worker_stream_));
            worker_stream_ = nullptr;
        }
        if (binary_ != nullptr) {
            CleanupAcl("aclrtBinaryUnLoad", aclrtBinaryUnLoad(binary_));
            binary_ = nullptr;
        }
    }

    uint32_t host_device_id_ = 0U;
    uint32_t sq_id_ = 0U;
    uint32_t stream_id_ = 0U;
    uint32_t logic_cq_id_ = 0U;
    aclrtBinHandle binary_ = nullptr;
    aclrtFuncHandle kernel_function_ = nullptr;
    aclrtStream control_stream_ = nullptr;
    aclrtStream worker_stream_ = nullptr;
};

void PrintResult(const char *case_name, const A5HalTestResult &result)
{
    std::cout << "case=" << case_name << " preflight_ret=" << result.preflight_ret
              << " hal_ret=" << result.hal_ret << " local_device=" << result.local_device_id
              << " value0=" << result.value0 << " value1=" << result.value1
              << " report_cqe_num=" << result.report_cqe_num << " phase=" << result.phase << '\n';
}

bool HasCompletedResult(A5HalTestCase test_case, const A5HalTestResult &result)
{
    return result.magic == kA5HalTestResultMagic &&
           result.case_id == static_cast<uint32_t>(test_case) &&
           result.phase == static_cast<uint32_t>(A5HalTestPhase::kAfterHalCall);
}

bool IsPassed(A5HalTestCase test_case, const A5HalTestResult &result)
{
    if (!HasCompletedResult(test_case, result) || result.preflight_ret != DRV_ERROR_NONE) {
        return false;
    }
    if (test_case == A5HalTestCase::kReportEmptyCq) {
        return result.hal_ret == DRV_ERROR_WAIT_TIMEOUT ||
               (result.hal_ret == DRV_ERROR_NONE && result.report_cqe_num == 0U);
    }
    if (test_case == A5HalTestCase::kQuerySqBase) {
        return result.hal_ret == DRV_ERROR_NONE && (result.value0 != 0U || result.value1 != 0U);
    }
    if (test_case == A5HalTestCase::kQuerySqDepth) {
        return result.hal_ret == DRV_ERROR_NONE && result.value0 > 0U;
    }
    return result.hal_ret == DRV_ERROR_NONE;
}

int RunTest(const Options &options)
{
    RuntimeGuard runtime(options.device_id);
    const char *soc_name = aclrtGetSocName();
    if (!IsA5Soc(soc_name)) {
        throw std::runtime_error(std::string("this test only supports Ascend950; detected SoC: ") +
                                 (soc_name == nullptr ? "<unknown>" : soc_name));
    }
    HalTestSession session(options.device_id, options.kernel_json);
    std::cout << "soc=" << soc_name << " case=" << options.case_name << " sq=" << session.sq_id()
              << " stream=" << session.stream_id() << " logic_cq=" << session.logic_cq_id() << '\n';

    uint32_t sq_tail = 0U;
    if (options.test_case == A5HalTestCase::kConfigTail) {
        const HalTestSession::Execution query =
            session.Run(A5HalTestCase::kQuerySqTail, 0U, options.timeout_ms);
        if (query.timed_out) {
            std::cout << "TIMEOUT case=config_tail preflight=query_sq_tail timeout_ms="
                      << options.timeout_ms << '\n';
            return kExitTimeout;
        }
        PrintResult("config_tail.preflight_query_sq_tail", query.result);
        if (!IsPassed(A5HalTestCase::kQuerySqTail, query.result)) {
            std::cout << "FAIL case=config_tail preflight=query_sq_tail\n";
            return 1;
        }
        sq_tail = query.result.value0;
    }

    const HalTestSession::Execution execution =
        session.Run(options.test_case, sq_tail, options.timeout_ms);
    if (execution.timed_out) {
        std::cout << "TIMEOUT case=" << options.case_name << " timeout_ms=" << options.timeout_ms << '\n';
        return kExitTimeout;
    }
    PrintResult(options.case_name.c_str(), execution.result);
    if (!IsPassed(options.test_case, execution.result)) {
        std::cout << "FAIL case=" << options.case_name << '\n';
        return 1;
    }
    std::cout << "PASS case=" << options.case_name << '\n';
    return 0;
}

}  // namespace
}  // namespace acltest

int main(int argc, char **argv)
{
    try {
        return acltest::RunTest(acltest::ParseOptions(argc, argv));
    } catch (const std::exception &error) {
        std::cerr << "acl_a5_hal_test: " << error.what() << '\n';
        return 2;
    }
}
