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

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glob.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "acl/acl.h"
#include "acltest/acl_check.h"
#include "acltest/aicpu_batch_launcher.h"
#include "acltest/benchmark_logic.h"
#include "acltest/fabric_memory.h"

namespace acltest {
namespace {

using Clock = std::chrono::steady_clock;

constexpr uint64_t kDefaultTargetBytes = 256ULL * 1024ULL * 1024ULL;
constexpr uint32_t kDefaultTimeoutMs = 60000U;

struct Options {
    int32_t device_id = 0;
    std::vector<TransferDirection> directions = {
        TransferDirection::kDeviceToHost,
        TransferDirection::kHostToDevice,
    };
    std::vector<uint64_t> sizes = {
        512U,        1024U,        2U * 1024U,   4U * 1024U,    32U * 1024U,
        64U * 1024U, 256U * 1024U, 512U * 1024U, 1024U * 1024U, 2U * 1024U * 1024U,
    };
    std::vector<size_t> counts = {128U};
    size_t warmup = 5U;
    bool automatic_iterations = true;
    size_t iterations = 0U;
    uint64_t target_bytes = kDefaultTargetBytes;
    uint32_t timeout_ms = kDefaultTimeoutMs;
    std::string csv_path = "results.csv";
    std::string kernel_json;
};

struct Timing {
    double submit_us = 0.0;
    double e2e_us = 0.0;
};

struct MethodStats {
    double submit_p50_us = 0.0;
    double submit_p95_us = 0.0;
    double e2e_p50_us = 0.0;
    double e2e_p95_us = 0.0;
    double gib_per_second = 0.0;
};

struct ResultRow {
    TransferDirection direction = TransferDirection::kDeviceToHost;
    uint64_t io_size = 0U;
    size_t io_count = 0U;
    uint64_t total_bytes = 0U;
    size_t iterations = 0U;
    SubmissionShape shape;
    MethodStats memcpy;
    MethodStats aicpu;
    double submit_speedup = 0.0;
    double e2e_speedup = 0.0;
};

class RuntimeGuard {
public:
    explicit RuntimeGuard(int32_t device_id) : device_id_(device_id)
    {
        try {
            ACLTEST_CHECK_ACL(aclInit(nullptr));
            initialized_ = true;
            ACLTEST_CHECK_ACL(aclrtSetDevice(device_id_));
            device_set_ = true;
            ACLTEST_CHECK_ACL(aclrtCreateContext(&context_, device_id_));
            ACLTEST_CHECK_ACL(aclrtSetCurrentContext(context_));
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
            const aclError error = aclrtDestroyContext(context_);
            if (error != ACL_SUCCESS) {
                std::cerr << "warning: aclrtDestroyContext failed, aclError=" << error << '\n';
            }
            context_ = nullptr;
        }
        if (device_set_) {
            const aclError error = aclrtResetDevice(device_id_);
            if (error != ACL_SUCCESS) {
                std::cerr << "warning: aclrtResetDevice failed, aclError=" << error << '\n';
            }
            device_set_ = false;
        }
        if (initialized_) {
            const aclError error = aclFinalize();
            if (error != ACL_SUCCESS) {
                std::cerr << "warning: aclFinalize failed, aclError=" << error << '\n';
            }
            initialized_ = false;
        }
    }

    int32_t device_id_ = 0;
    aclrtContext context_ = nullptr;
    bool initialized_ = false;
    bool device_set_ = false;
};

class BaselineStream {
public:
    BaselineStream()
    {
        ACLTEST_CHECK_ACL(aclrtCreateStreamWithConfig(
            &stream_, 0U, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC));
        try {
            ACLTEST_CHECK_ACL(aclrtSetStreamFailureMode(stream_, ACL_STOP_ON_FAILURE));
        } catch (...) {
            (void)aclrtDestroyStream(stream_);
            stream_ = nullptr;
            throw;
        }
    }

    ~BaselineStream()
    {
        if (stream_ != nullptr) {
            const aclError error = aclrtDestroyStream(stream_);
            if (error != ACL_SUCCESS) {
                std::cerr << "warning: aclrtDestroyStream(baseline) failed, aclError=" << error
                          << '\n';
            }
        }
    }

    aclrtStream get() const { return stream_; }

private:
    aclrtStream stream_ = nullptr;
};

double ElapsedUs(Clock::time_point begin, Clock::time_point end)
{
    return std::chrono::duration<double, std::micro>(end - begin).count();
}

std::string DirectionName(TransferDirection direction)
{
    return direction == TransferDirection::kDeviceToHost ? "D2H" : "H2D";
}

bool IsA3Soc(const char *name)
{
    if (name == nullptr) { return false; }
    constexpr std::array<const char *, 6U> kNames = {
        "Ascend910_9391", "Ascend910_9381", "Ascend910_9392",
        "Ascend910_9382", "Ascend910_9372", "Ascend910_9362",
    };
    return std::any_of(kNames.begin(), kNames.end(),
                       [name](const char *candidate) { return std::string(name) == candidate; });
}

std::string KernelJsonPath(const std::string &root)
{
    return root + "/opp/built-in/op_impl/aicpu/config/libacltest_sdma_kernel.json";
}

bool IsReadableFile(const std::string &path)
{
    std::ifstream file(path);
    return file.good();
}

std::string FindInstalledKernelJson()
{
    constexpr std::array<const char *, 4U> kPreferredRoots = {
        "/usr/local/Ascend/ascend-toolkit/latest",
        "/usr/local/Ascend/latest",
        "/usr/local/Ascend/cann",
        "/usr/local/Ascend",
    };
    for (const char *root : kPreferredRoots) {
        const std::string path = KernelJsonPath(root);
        if (IsReadableFile(path)) { return path; }
    }

    constexpr std::array<const char *, 2U> kRootPatterns = {
        "/usr/local/Ascend/cann-*",
        "/usr/local/Ascend/ascend-toolkit/*",
    };
    for (const char *pattern : kRootPatterns) {
        glob_t matches{};
        if (glob(pattern, 0U, nullptr, &matches) == 0) {
            for (size_t index = matches.gl_pathc; index > 0U; --index) {
                const std::string path = KernelJsonPath(matches.gl_pathv[index - 1U]);
                if (IsReadableFile(path)) {
                    globfree(&matches);
                    return path;
                }
            }
        }
        globfree(&matches);
    }
    return KernelJsonPath("/usr/local/Ascend");
}

std::string DefaultKernelJson()
{
    const char *override_path = std::getenv("ACLTEST_KERNEL_JSON");
    if (override_path != nullptr && override_path[0] != '\0') { return override_path; }
    const char *ascend_home = std::getenv("ASCEND_HOME_PATH");
    if (ascend_home != nullptr && ascend_home[0] != '\0') { return KernelJsonPath(ascend_home); }
    return FindInstalledKernelJson();
}

std::string TakeValue(int argc, char **argv, int &index, const std::string &argument)
{
    const size_t equals = argument.find('=');
    if (equals != std::string::npos) { return argument.substr(equals + 1U); }
    if (index + 1 >= argc) { throw std::invalid_argument("missing value for " + argument); }
    return argv[++index];
}

std::vector<TransferDirection> ParseDirections(const std::string &value)
{
    if (value == "d2h") { return {TransferDirection::kDeviceToHost}; }
    if (value == "h2d") { return {TransferDirection::kHostToDevice}; }
    if (value == "both") {
        return {TransferDirection::kDeviceToHost, TransferDirection::kHostToDevice};
    }
    throw std::invalid_argument("direction must be d2h, h2d, or both");
}

void PrintUsage(const char *program)
{
    std::cout << "Usage: " << program << " [options]\n"
              << "  --device N              ACL logical device (default: 0)\n"
              << "  --direction VALUE       d2h, h2d, or both (default: both)\n"
              << "  --sizes LIST            comma-separated B/K/M/G sizes\n"
              << "                          default: 512B,1K,2K,4K,32K,64K,256K,512K,1M,2M\n"
              << "  --counts LIST           comma-separated IO counts (default: 128)\n"
              << "  --warmup N              warmup pairs per case (default: 5)\n"
              << "  --iterations auto|N     measured pairs (default: auto)\n"
              << "  --target-bytes SIZE     auto-iteration accumulated bytes (default: 256M)\n"
              << "  --timeout-ms N          stream/RTSQ timeout (default: 60000)\n"
              << "  --csv PATH              result CSV (default: results.csv)\n"
              << "  --kernel-json PATH      installed AICPU config JSON\n"
              << "  -h, --help              show this help\n";
}

Options ParseOptions(int argc, char **argv)
{
    Options options;
    options.kernel_json = DefaultKernelJson();
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            PrintUsage(argv[0]);
            std::exit(0);
        }
        const std::string name = argument.substr(0U, argument.find('='));
        if (name == "--device") {
            options.device_id = std::stoi(TakeValue(argc, argv, index, argument));
        } else if (name == "--direction") {
            options.directions = ParseDirections(TakeValue(argc, argv, index, argument));
        } else if (name == "--sizes") {
            options.sizes = ParseByteSizeList(TakeValue(argc, argv, index, argument));
        } else if (name == "--counts") {
            options.counts = ParseCountList(TakeValue(argc, argv, index, argument));
        } else if (name == "--warmup") {
            options.warmup =
                static_cast<size_t>(std::stoull(TakeValue(argc, argv, index, argument)));
        } else if (name == "--iterations") {
            const std::string value = TakeValue(argc, argv, index, argument);
            options.automatic_iterations = value == "auto";
            if (!options.automatic_iterations) {
                options.iterations = static_cast<size_t>(std::stoull(value));
            }
        } else if (name == "--target-bytes") {
            options.target_bytes = ParseByteSize(TakeValue(argc, argv, index, argument));
        } else if (name == "--timeout-ms") {
            const uint64_t timeout = std::stoull(TakeValue(argc, argv, index, argument));
            if (timeout > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--timeout-ms exceeds ACL's signed timeout range");
            }
            options.timeout_ms = static_cast<uint32_t>(timeout);
        } else if (name == "--csv") {
            options.csv_path = TakeValue(argc, argv, index, argument);
        } else if (name == "--kernel-json") {
            options.kernel_json = TakeValue(argc, argv, index, argument);
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    if (options.device_id < 0 || options.warmup > 100000U ||
        (!options.automatic_iterations && options.iterations == 0U) || options.timeout_ms == 0U ||
        options.csv_path.empty()) {
        throw std::invalid_argument("invalid zero, negative, or excessive option value");
    }
    return options;
}

uint64_t CheckedTotalBytes(uint64_t size, size_t count)
{
    if (count == 0U || size > std::numeric_limits<uint64_t>::max() / count ||
        size * count > std::numeric_limits<size_t>::max()) {
        throw std::overflow_error("IO size multiplied by count overflows addressable memory");
    }
    return size * count;
}

std::vector<AddressRange> BuildRanges(void *host, void *device, uint64_t io_size, size_t io_count)
{
    std::vector<AddressRange> ranges;
    ranges.reserve(io_count);
    for (size_t index = 0U; index < io_count; ++index) {
        const uint64_t offset = io_size * index;
        ranges.push_back({reinterpret_cast<uintptr_t>(host) + offset,
                          reinterpret_cast<uintptr_t>(device) + offset, io_size});
    }
    return ranges;
}

Timing RunMemcpyLoop(const std::vector<AddressRange> &ranges, TransferDirection direction,
                     aclrtStream stream, uint32_t timeout_ms)
{
    const aclrtMemcpyKind kind = direction == TransferDirection::kDeviceToHost
                                     ? ACL_MEMCPY_DEVICE_TO_HOST
                                     : ACL_MEMCPY_HOST_TO_DEVICE;
    const auto begin = Clock::now();
    for (const AddressRange &range : ranges) {
        void *destination = reinterpret_cast<void *>(
            direction == TransferDirection::kDeviceToHost ? range.local_addr : range.device_addr);
        const void *source = reinterpret_cast<const void *>(
            direction == TransferDirection::kDeviceToHost ? range.device_addr : range.local_addr);
        ACLTEST_CHECK_ACL(aclrtMemcpyAsync(destination, static_cast<size_t>(range.length), source,
                                           static_cast<size_t>(range.length), kind, stream));
    }
    const auto submitted = Clock::now();
    ACLTEST_CHECK_ACL(aclrtSynchronizeStreamWithTimeout(stream, static_cast<int32_t>(timeout_ms)));
    const auto complete = Clock::now();
    return {ElapsedUs(begin, submitted), ElapsedUs(begin, complete)};
}

Timing RunAicpu(AicpuBatchLauncher &launcher, const std::vector<AddressRange> &ranges,
                TransferDirection direction, uint32_t timeout_ms)
{
    const auto begin = Clock::now();
    launcher.Submit(direction, ranges, timeout_ms);
    const auto submitted = Clock::now();
    launcher.Synchronize(timeout_ms);
    const auto complete = Clock::now();
    launcher.Finish();
    return {ElapsedUs(begin, submitted), ElapsedUs(begin, complete)};
}

std::vector<uint8_t> MakePattern(size_t bytes, TransferDirection direction, uint64_t io_size,
                                 size_t count)
{
    std::vector<uint8_t> pattern(bytes);
    uint64_t state = 0x9E3779B97F4A7C15ULL ^ io_size ^ (static_cast<uint64_t>(count) << 32U) ^
                     static_cast<uint32_t>(direction);
    for (size_t index = 0U; index < bytes; ++index) {
        state ^= state << 7U;
        state ^= state >> 9U;
        state ^= state << 8U;
        pattern[index] = static_cast<uint8_t>(state);
    }
    return pattern;
}

void CheckEqual(const uint8_t *actual, const std::vector<uint8_t> &expected,
                const std::string &method)
{
    for (size_t index = 0U; index < expected.size(); ++index) {
        if (actual[index] != expected[index]) {
            throw std::runtime_error(method + " correctness failure at byte " +
                                     std::to_string(index) +
                                     ": actual=" + std::to_string(actual[index]) +
                                     " expected=" + std::to_string(expected[index]));
        }
    }
}

void VerifyMethods(FabricMemoryPair &memory, const std::vector<AddressRange> &ranges,
                   TransferDirection direction, uint64_t io_size, size_t io_count,
                   BaselineStream &baseline, AicpuBatchLauncher &launcher, uint32_t timeout_ms)
{
    const size_t total_bytes = static_cast<size_t>(CheckedTotalBytes(io_size, io_count));
    const std::vector<uint8_t> pattern = MakePattern(total_bytes, direction, io_size, io_count);
    auto *host = static_cast<uint8_t *>(memory.host());

    if (direction == TransferDirection::kDeviceToHost) {
        std::memcpy(host, pattern.data(), total_bytes);
        ACLTEST_CHECK_ACL(aclrtMemcpy(memory.device(), total_bytes, host, total_bytes,
                                      ACL_MEMCPY_HOST_TO_DEVICE));
        std::memset(host, 0, total_bytes);
        (void)RunMemcpyLoop(ranges, direction, baseline.get(), timeout_ms);
        CheckEqual(host, pattern, "memcpy-loop D2H");
        std::memset(host, 0, total_bytes);
        (void)RunAicpu(launcher, ranges, direction, timeout_ms);
        CheckEqual(host, pattern, "AICPU batch D2H");
        return;
    }

    std::memcpy(host, pattern.data(), total_bytes);
    ACLTEST_CHECK_ACL(aclrtMemset(memory.device(), total_bytes, 0, total_bytes));
    (void)RunMemcpyLoop(ranges, direction, baseline.get(), timeout_ms);
    std::vector<uint8_t> actual(total_bytes);
    ACLTEST_CHECK_ACL(aclrtMemcpy(actual.data(), total_bytes, memory.device(), total_bytes,
                                  ACL_MEMCPY_DEVICE_TO_HOST));
    CheckEqual(actual.data(), pattern, "memcpy-loop H2D");
    ACLTEST_CHECK_ACL(aclrtMemset(memory.device(), total_bytes, 0, total_bytes));
    (void)RunAicpu(launcher, ranges, direction, timeout_ms);
    ACLTEST_CHECK_ACL(aclrtMemcpy(actual.data(), total_bytes, memory.device(), total_bytes,
                                  ACL_MEMCPY_DEVICE_TO_HOST));
    CheckEqual(actual.data(), pattern, "AICPU batch H2D");
}

MethodStats Summarize(const std::vector<Timing> &timings, uint64_t total_bytes)
{
    std::vector<double> submit;
    std::vector<double> e2e;
    submit.reserve(timings.size());
    e2e.reserve(timings.size());
    for (const Timing &timing : timings) {
        submit.push_back(timing.submit_us);
        e2e.push_back(timing.e2e_us);
    }
    MethodStats stats;
    stats.submit_p50_us = Percentile(submit, 0.50);
    stats.submit_p95_us = Percentile(submit, 0.95);
    stats.e2e_p50_us = Percentile(e2e, 0.50);
    stats.e2e_p95_us = Percentile(e2e, 0.95);
    stats.gib_per_second = static_cast<double>(total_bytes) * 1000000.0 /
                           (1024.0 * 1024.0 * 1024.0 * stats.e2e_p50_us);
    return stats;
}

ResultRow RunCase(FabricMemoryPair &memory, BaselineStream &baseline, AicpuBatchLauncher &launcher,
                  const Options &options, TransferDirection direction, uint64_t io_size,
                  size_t io_count)
{
    const uint64_t total_bytes = CheckedTotalBytes(io_size, io_count);
    const size_t iterations = options.automatic_iterations
                                  ? ComputeAutoIterations(total_bytes, options.target_bytes)
                                  : options.iterations;
    const std::vector<AddressRange> ranges =
        BuildRanges(memory.host(), memory.device(), io_size, io_count);

    VerifyMethods(memory, ranges, direction, io_size, io_count, baseline, launcher,
                  options.timeout_ms);
    std::cout << "  running " << DirectionName(direction) << " size=" << FormatByteSize(io_size)
              << " count=" << io_count << " iterations=" << iterations << "\n";

    for (size_t index = 0U; index < options.warmup; ++index) {
        if ((index & 1U) == 0U) {
            (void)RunMemcpyLoop(ranges, direction, baseline.get(), options.timeout_ms);
            (void)RunAicpu(launcher, ranges, direction, options.timeout_ms);
        } else {
            (void)RunAicpu(launcher, ranges, direction, options.timeout_ms);
            (void)RunMemcpyLoop(ranges, direction, baseline.get(), options.timeout_ms);
        }
    }

    std::vector<Timing> memcpy_timings;
    std::vector<Timing> aicpu_timings;
    memcpy_timings.reserve(iterations);
    aicpu_timings.reserve(iterations);
    for (size_t index = 0U; index < iterations; ++index) {
        if ((index & 1U) == 0U) {
            memcpy_timings.push_back(
                RunMemcpyLoop(ranges, direction, baseline.get(), options.timeout_ms));
            aicpu_timings.push_back(RunAicpu(launcher, ranges, direction, options.timeout_ms));
        } else {
            aicpu_timings.push_back(RunAicpu(launcher, ranges, direction, options.timeout_ms));
            memcpy_timings.push_back(
                RunMemcpyLoop(ranges, direction, baseline.get(), options.timeout_ms));
        }
    }

    ResultRow result;
    result.direction = direction;
    result.io_size = io_size;
    result.io_count = io_count;
    result.total_bytes = total_bytes;
    result.iterations = iterations;
    result.shape = launcher.last_shape();
    result.memcpy = Summarize(memcpy_timings, total_bytes);
    result.aicpu = Summarize(aicpu_timings, total_bytes);
    result.submit_speedup = result.memcpy.submit_p50_us / result.aicpu.submit_p50_us;
    result.e2e_speedup = result.memcpy.e2e_p50_us / result.aicpu.e2e_p50_us;
    return result;
}

void PrintResults(const std::vector<ResultRow> &results)
{
    std::cout
        << "\nAll correctness checks passed. Timings are microseconds; bandwidth is GiB/s.\n\n";
    std::cout << std::left << std::setw(4) << "dir" << std::setw(7) << "size" << std::right
              << std::setw(7) << "count" << std::setw(7) << "iters" << std::setw(7) << "kern"
              << std::setw(7) << "ntfy" << std::setw(11) << "m.s50" << std::setw(11) << "m.s95"
              << std::setw(11) << "a.s50" << std::setw(11) << "a.s95" << std::setw(11) << "m.e50"
              << std::setw(11) << "m.e95" << std::setw(11) << "a.e50" << std::setw(11) << "a.e95"
              << std::setw(10) << "m.GiB/s" << std::setw(10) << "a.GiB/s" << std::setw(10)
              << "s.speed" << std::setw(10) << "e.speed" << '\n';
    for (const ResultRow &row : results) {
        std::cout << std::left << std::setw(4) << DirectionName(row.direction) << std::setw(7)
                  << FormatByteSize(row.io_size) << std::right << std::setw(7) << row.io_count
                  << std::setw(7) << row.iterations << std::setw(7) << row.shape.kernel_launch_count
                  << std::setw(7) << row.shape.notify_count << std::fixed << std::setprecision(2)
                  << std::setw(11) << row.memcpy.submit_p50_us << std::setw(11)
                  << row.memcpy.submit_p95_us << std::setw(11) << row.aicpu.submit_p50_us
                  << std::setw(11) << row.aicpu.submit_p95_us << std::setw(11)
                  << row.memcpy.e2e_p50_us << std::setw(11) << row.memcpy.e2e_p95_us
                  << std::setw(11) << row.aicpu.e2e_p50_us << std::setw(11) << row.aicpu.e2e_p95_us
                  << std::setw(10) << row.memcpy.gib_per_second << std::setw(10)
                  << row.aicpu.gib_per_second << std::setw(10) << row.submit_speedup
                  << std::setw(10) << row.e2e_speedup << '\n';
    }
}

void WriteCsv(const std::string &path, const std::vector<ResultRow> &results)
{
    std::ofstream output(path, std::ios::trunc);
    if (!output) { throw std::runtime_error("cannot open CSV output: " + path); }
    output << "direction,io_size_bytes,io_count,total_bytes,iterations,aicpu_kernel_launches,aicpu_"
              "notifies,"
              "memcpy_submit_p50_us,memcpy_submit_p95_us,aicpu_submit_p50_us,aicpu_submit_p95_us,"
              "memcpy_e2e_p50_us,memcpy_e2e_p95_us,aicpu_e2e_p50_us,aicpu_e2e_p95_us,"
              "memcpy_gib_per_s,aicpu_gib_per_s,submit_speedup,e2e_speedup,verify\n";
    output << std::fixed << std::setprecision(6);
    for (const ResultRow &row : results) {
        output << DirectionName(row.direction) << ',' << row.io_size << ',' << row.io_count << ','
               << row.total_bytes << ',' << row.iterations << ',' << row.shape.kernel_launch_count
               << ',' << row.shape.notify_count << ',' << row.memcpy.submit_p50_us << ','
               << row.memcpy.submit_p95_us << ',' << row.aicpu.submit_p50_us << ','
               << row.aicpu.submit_p95_us << ',' << row.memcpy.e2e_p50_us << ','
               << row.memcpy.e2e_p95_us << ',' << row.aicpu.e2e_p50_us << ','
               << row.aicpu.e2e_p95_us << ',' << row.memcpy.gib_per_second << ','
               << row.aicpu.gib_per_second << ',' << row.submit_speedup << ',' << row.e2e_speedup
               << ",PASS\n";
    }
    if (!output) { throw std::runtime_error("failed while writing CSV output: " + path); }
}

int Run(int argc, char **argv)
{
    const Options options = ParseOptions(argc, argv);
    uint64_t max_total_bytes = 0U;
    for (uint64_t size : options.sizes) {
        for (size_t count : options.counts) {
            max_total_bytes = std::max(max_total_bytes, CheckedTotalBytes(size, count));
        }
    }

    RuntimeGuard runtime(options.device_id);
    const char *soc_name = aclrtGetSocName();
    if (!IsA3Soc(soc_name)) {
        throw std::runtime_error(
            std::string("AICPU direct RTSQ benchmark supports A3 only; detected SoC: ") +
            (soc_name == nullptr ? "<unknown>" : soc_name));
    }
    std::cout << "device=" << options.device_id << " soc=" << soc_name
              << " kernel_json=" << options.kernel_json << '\n';
    std::cout << "Timing boundary: submit starts before descriptor allocation/build and ends after "
                 "enqueue; "
                 "e2e ends after stream synchronization. Verification and cleanup are untimed.\n";

    std::vector<ResultRow> results;
    {
        FabricMemoryPair memory;
        memory.Initialize(static_cast<size_t>(max_total_bytes), options.device_id);
        BaselineStream baseline;
        AicpuBatchLauncher launcher;
        launcher.Initialize(options.device_id, options.kernel_json);
        for (TransferDirection direction : options.directions) {
            for (uint64_t size : options.sizes) {
                for (size_t count : options.counts) {
                    results.push_back(
                        RunCase(memory, baseline, launcher, options, direction, size, count));
                }
            }
        }
    }
    PrintResults(results);
    WriteCsv(options.csv_path, results);
    std::cout << "\nCSV written to " << options.csv_path << '\n';
    return 0;
}

}  // namespace
}  // namespace acltest

int main(int argc, char **argv)
{
    try {
        return acltest::Run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "acl_copy_bench: " << error.what() << '\n';
        return 1;
    }
}
