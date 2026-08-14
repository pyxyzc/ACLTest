#include <acl/acl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint64_t kKiB = 1024ULL;
constexpr uint64_t kMiB = 1024ULL * kKiB;
constexpr uint64_t kGiB = 1024ULL * kMiB;
constexpr uint64_t kDefaultTargetBytes = 256ULL * kMiB;
constexpr size_t kMinAutoIterations = 5U;
constexpr size_t kMaxAutoIterations = 500U;

enum class Direction { kHostToDevice, kDeviceToHost };
enum class Method { kMemcpyLoop, kMemcpyBatch };

struct Options {
    int32_t device_id = 0;
    std::vector<Direction> directions = {Direction::kHostToDevice, Direction::kDeviceToHost};
    std::vector<size_t> sizes = {512U,       1U * kKiB,   2U * kKiB,   4U * kKiB, 32U * kKiB,
                                 64U * kKiB, 256U * kKiB, 512U * kKiB, 1U * kMiB, 2U * kMiB};
    std::vector<size_t> counts = {128U};
    size_t warmup = 5U;
    bool automatic_iterations = true;
    size_t iterations = 0U;
    uint64_t target_bytes = kDefaultTargetBytes;
    int32_t timeout_ms = 60000;
    std::string csv_path = "results.csv";
};

struct Timing {
    double submit_us = 0.0;
    double execution_us = 0.0;
    double e2e_us = 0.0;
};

struct HostTiming {
    double submit_us = 0.0;
    double e2e_us = 0.0;
};

struct MethodStats {
    double submit_p50_us = 0.0;
    double submit_p95_us = 0.0;
    double execution_p50_us = 0.0;
    double execution_p95_us = 0.0;
    double e2e_p50_us = 0.0;
    double e2e_p95_us = 0.0;
    double execution_gib_per_second = 0.0;
    double e2e_gib_per_second = 0.0;
};

struct Result {
    Direction direction = Direction::kHostToDevice;
    size_t size = 0U;
    size_t count = 0U;
    size_t total_bytes = 0U;
    size_t iterations = 0U;
    MethodStats loop;
    MethodStats batch;
    double submit_speedup = 0.0;
    double execution_speedup = 0.0;
    double e2e_speedup = 0.0;
};

std::string RecentAclError()
{
    const char* message = aclGetRecentErrMsg();
    return message == nullptr ? "" : message;
}

[[noreturn]] void ThrowAclError(const std::string& operation, aclError error)
{
    std::ostringstream os;
    os << operation << " failed, aclError=" << static_cast<int64_t>(error);
    const std::string recent = RecentAclError();
    if (!recent.empty()) { os << ", recent=\"" << recent << '"'; }
    throw std::runtime_error(os.str());
}

void CheckAcl(const std::string& operation, aclError error)
{
    if (error != ACL_SUCCESS) { ThrowAclError(operation, error); }
}

class RuntimeSession {
public:
    explicit RuntimeSession(int32_t device_id) : device_id_(device_id)
    {
        const aclError init_error = aclInit(nullptr);
        if (init_error != ACL_SUCCESS) { ThrowAclError("aclInit", init_error); }
        initialized_ = true;

        const aclError set_error = aclrtSetDevice(device_id_);
        if (set_error != ACL_SUCCESS) {
            Cleanup();
            ThrowAclError("aclrtSetDevice", set_error);
        }
        device_set_ = true;
    }

    ~RuntimeSession() { Cleanup(); }

    RuntimeSession(const RuntimeSession&) = delete;
    RuntimeSession& operator=(const RuntimeSession&) = delete;

private:
    void Cleanup() noexcept
    {
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
    bool initialized_ = false;
    bool device_set_ = false;
};

class Stream {
public:
    Stream() { CheckAcl("aclrtCreateStream", aclrtCreateStream(&stream_)); }

    ~Stream()
    {
        if (stream_ != nullptr) {
            const aclError error = aclrtDestroyStream(stream_);
            if (error != ACL_SUCCESS) {
                std::cerr << "warning: aclrtDestroyStream failed, aclError=" << error << '\n';
            }
        }
    }

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    aclrtStream get() const { return stream_; }

private:
    aclrtStream stream_ = nullptr;
};

class EventTimer {
public:
    EventTimer()
    {
        CheckAcl("aclrtCreateEventExWithFlag(start)",
                 aclrtCreateEventExWithFlag(&start_, ACL_EVENT_TIME_LINE));
        const aclError end_error = aclrtCreateEventExWithFlag(&end_, ACL_EVENT_TIME_LINE);
        if (end_error != ACL_SUCCESS) {
            (void)aclrtDestroyEvent(start_);
            start_ = nullptr;
            ThrowAclError("aclrtCreateEventExWithFlag(end)", end_error);
        }
    }

    ~EventTimer()
    {
        Destroy("end", &end_);
        Destroy("start", &start_);
    }

    EventTimer(const EventTimer&) = delete;
    EventTimer& operator=(const EventTimer&) = delete;

    void RecordStart(aclrtStream stream)
    {
        CheckAcl("aclrtRecordEvent(start)", aclrtRecordEvent(start_, stream));
    }

    void RecordEnd(aclrtStream stream)
    {
        CheckAcl("aclrtRecordEvent(end)", aclrtRecordEvent(end_, stream));
    }

    double ElapsedMicroseconds()
    {
        float milliseconds = 0.0F;
        CheckAcl("aclrtEventElapsedTime", aclrtEventElapsedTime(&milliseconds, start_, end_));
        return static_cast<double>(milliseconds) * 1000.0;
    }

private:
    static void Destroy(const char* name, aclrtEvent* event) noexcept
    {
        if (*event == nullptr) { return; }
        const aclError error = aclrtDestroyEvent(*event);
        if (error != ACL_SUCCESS) {
            std::cerr << "warning: aclrtDestroyEvent(" << name << ") failed, aclError=" << error
                      << '\n';
        }
        *event = nullptr;
    }

    aclrtEvent start_ = nullptr;
    aclrtEvent end_ = nullptr;
};

class HostBuffer {
public:
    explicit HostBuffer(size_t bytes) : bytes_(bytes)
    {
        CheckAcl("aclrtMallocHost", aclrtMallocHost(&data_, bytes_));
    }

    ~HostBuffer()
    {
        if (data_ != nullptr) {
            const aclError error = aclrtFreeHost(data_);
            if (error != ACL_SUCCESS) {
                std::cerr << "warning: aclrtFreeHost failed, aclError=" << error << '\n';
            }
        }
    }

    HostBuffer(const HostBuffer&) = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;

    void* get() const { return data_; }
    size_t size() const { return bytes_; }

private:
    void* data_ = nullptr;
    size_t bytes_ = 0U;
};

class DeviceBuffer {
public:
    explicit DeviceBuffer(size_t bytes) : bytes_(bytes)
    {
        CheckAcl("aclrtMalloc", aclrtMalloc(&data_, bytes_, ACL_MEM_MALLOC_HUGE_FIRST));
    }

    ~DeviceBuffer()
    {
        if (data_ != nullptr) {
            const aclError error = aclrtFree(data_);
            if (error != ACL_SUCCESS) {
                std::cerr << "warning: aclrtFree failed, aclError=" << error << '\n';
            }
        }
    }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    void* get() const { return data_; }
    size_t size() const { return bytes_; }

private:
    void* data_ = nullptr;
    size_t bytes_ = 0U;
};

std::string DirectionName(Direction direction)
{
    return direction == Direction::kHostToDevice ? "H2D" : "D2H";
}

size_t CheckedMultiply(size_t left, size_t right)
{
    if (left != 0U && right > std::numeric_limits<size_t>::max() / left) {
        throw std::overflow_error("size * count overflows size_t");
    }
    return left * right;
}

class CaseBuffers {
public:
    CaseBuffers(Direction direction, int32_t device_id, size_t size, size_t count)
        : direction_(direction),
          device_id_(device_id),
          size_(size),
          count_(count),
          total_bytes_(CheckedMultiply(size, count)),
          host_(total_bytes_),
          device_(total_bytes_),
          expected_(total_bytes_),
          destinations_(count),
          destination_max_sizes_(count, size),
          sources_(count),
          copy_sizes_(count, size)
    {
        FillSource();
        BuildPointerArrays();
        BuildAttribute();
        if (direction_ == Direction::kDeviceToHost) {
            CheckAcl("initial H2D aclrtMemcpy",
                     aclrtMemcpy(device_.get(), device_.size(), host_.get(), host_.size(),
                                 ACL_MEMCPY_HOST_TO_DEVICE));
        }
    }

    void ResetDestination()
    {
        constexpr int32_t kSentinel = 0xA5;
        if (direction_ == Direction::kHostToDevice) {
            CheckAcl("aclrtMemset(device destination)",
                     aclrtMemset(device_.get(), device_.size(), kSentinel, device_.size()));
        } else {
            std::memset(host_.get(), kSentinel, host_.size());
        }
    }

    void Verify()
    {
        std::vector<uint8_t> readback;
        const uint8_t* actual = nullptr;
        if (direction_ == Direction::kHostToDevice) {
            readback.resize(total_bytes_);
            CheckAcl("verification D2H aclrtMemcpy",
                     aclrtMemcpy(readback.data(), readback.size(), device_.get(), device_.size(),
                                 ACL_MEMCPY_DEVICE_TO_HOST));
            actual = readback.data();
        } else {
            actual = static_cast<const uint8_t*>(host_.get());
        }

        for (size_t index = 0U; index < total_bytes_; ++index) {
            if (actual[index] != expected_[index]) {
                std::ostringstream os;
                os << "verification failed at byte " << index
                   << ": expected=" << static_cast<unsigned>(expected_[index])
                   << ", actual=" << static_cast<unsigned>(actual[index]);
                throw std::runtime_error(os.str());
            }
        }
    }

    std::vector<void*>& destinations() { return destinations_; }
    std::vector<size_t>& destination_max_sizes() { return destination_max_sizes_; }
    std::vector<void*>& sources() { return sources_; }
    std::vector<size_t>& copy_sizes() { return copy_sizes_; }
    aclrtMemcpyBatchAttr& attribute() { return attribute_; }
    size_t& attribute_index() { return attribute_index_; }
    size_t count() const { return count_; }
    size_t size() const { return size_; }
    size_t total_bytes() const { return total_bytes_; }
    Direction direction() const { return direction_; }

private:
    void FillSource()
    {
        for (size_t index = 0U; index < total_bytes_; ++index) {
            expected_[index] = static_cast<uint8_t>((index * 131U + index / 251U + 17U) & 0xFFU);
        }
        std::memcpy(host_.get(), expected_.data(), expected_.size());
    }

    void BuildPointerArrays()
    {
        auto* host = static_cast<uint8_t*>(host_.get());
        auto* device = static_cast<uint8_t*>(device_.get());
        for (size_t index = 0U; index < count_; ++index) {
            const size_t offset = index * size_;
            if (direction_ == Direction::kHostToDevice) {
                sources_[index] = host + offset;
                destinations_[index] = device + offset;
            } else {
                sources_[index] = device + offset;
                destinations_[index] = host + offset;
            }
        }
    }

    void BuildAttribute()
    {
        attribute_ = {};
        attribute_index_ = 0U;
        if (direction_ == Direction::kHostToDevice) {
            attribute_.srcLoc = {0U, ACL_MEM_LOCATION_TYPE_HOST};
            attribute_.dstLoc = {static_cast<uint32_t>(device_id_), ACL_MEM_LOCATION_TYPE_DEVICE};
        } else {
            attribute_.srcLoc = {static_cast<uint32_t>(device_id_), ACL_MEM_LOCATION_TYPE_DEVICE};
            attribute_.dstLoc = {0U, ACL_MEM_LOCATION_TYPE_HOST};
        }
    }

    Direction direction_;
    int32_t device_id_ = 0;
    size_t size_ = 0U;
    size_t count_ = 0U;
    size_t total_bytes_ = 0U;
    HostBuffer host_;
    DeviceBuffer device_;
    std::vector<uint8_t> expected_;
    std::vector<void*> destinations_;
    std::vector<size_t> destination_max_sizes_;
    std::vector<void*> sources_;
    std::vector<size_t> copy_sizes_;
    aclrtMemcpyBatchAttr attribute_{};
    size_t attribute_index_ = 0U;
};

double ElapsedMicroseconds(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double, std::micro>(end - start).count();
}

void SubmitCopies(Method method, CaseBuffers& buffers, aclrtStream stream)
{
    if (method == Method::kMemcpyLoop) {
        const aclrtMemcpyKind kind = buffers.direction() == Direction::kHostToDevice
                                         ? ACL_MEMCPY_HOST_TO_DEVICE
                                         : ACL_MEMCPY_DEVICE_TO_HOST;
        for (size_t index = 0U; index < buffers.count(); ++index) {
            const aclError error = aclrtMemcpyAsync(
                buffers.destinations()[index], buffers.destination_max_sizes()[index],
                buffers.sources()[index], buffers.copy_sizes()[index], kind, stream);
            if (error != ACL_SUCCESS) {
                ThrowAclError("aclrtMemcpyAsync[" + std::to_string(index) + "]", error);
            }
        }
    } else {
        size_t fail_index = std::numeric_limits<size_t>::max();
        const aclError error = aclrtMemcpyBatchAsync(
            buffers.destinations().data(), buffers.destination_max_sizes().data(),
            buffers.sources().data(), buffers.copy_sizes().data(), buffers.count(),
            &buffers.attribute(), &buffers.attribute_index(), 1U, &fail_index, stream);
        if (error != ACL_SUCCESS) {
            std::ostringstream operation;
            operation << "aclrtMemcpyBatchAsync";
            if (fail_index != std::numeric_limits<size_t>::max()) {
                operation << "[failIndex=" << fail_index << ']';
            }
            ThrowAclError(operation.str(), error);
        }
    }
}

HostTiming RunHostTimed(Method method, CaseBuffers& buffers, aclrtStream stream, int32_t timeout_ms)
{
    const Clock::time_point start = Clock::now();
    SubmitCopies(method, buffers, stream);
    const Clock::time_point submitted = Clock::now();
    CheckAcl("aclrtSynchronizeStreamWithTimeout",
             aclrtSynchronizeStreamWithTimeout(stream, timeout_ms));
    const Clock::time_point completed = Clock::now();
    return {ElapsedMicroseconds(start, submitted), ElapsedMicroseconds(start, completed)};
}

double RunExecutionTimed(Method method, CaseBuffers& buffers, aclrtStream stream,
                         EventTimer& event_timer, int32_t timeout_ms)
{
    event_timer.RecordStart(stream);
    SubmitCopies(method, buffers, stream);
    event_timer.RecordEnd(stream);
    CheckAcl("aclrtSynchronizeStreamWithTimeout(execution timing)",
             aclrtSynchronizeStreamWithTimeout(stream, timeout_ms));
    return event_timer.ElapsedMicroseconds();
}

Timing RunOnce(Method method, CaseBuffers& buffers, aclrtStream stream, EventTimer& event_timer,
               int32_t timeout_ms, bool execution_first)
{
    HostTiming host;
    double execution_us = 0.0;
    if (execution_first) {
        execution_us = RunExecutionTimed(method, buffers, stream, event_timer, timeout_ms);
        host = RunHostTimed(method, buffers, stream, timeout_ms);
    } else {
        host = RunHostTimed(method, buffers, stream, timeout_ms);
        execution_us = RunExecutionTimed(method, buffers, stream, event_timer, timeout_ms);
    }
    return {host.submit_us, execution_us, host.e2e_us};
}

void ValidateMethod(Method method, CaseBuffers& buffers, aclrtStream stream,
                    EventTimer& event_timer, int32_t timeout_ms)
{
    buffers.ResetDestination();
    (void)RunHostTimed(method, buffers, stream, timeout_ms);
    buffers.Verify();
    buffers.ResetDestination();
    (void)RunExecutionTimed(method, buffers, stream, event_timer, timeout_ms);
    buffers.Verify();
}

double Percentile(std::vector<double> samples, double percentile)
{
    if (samples.empty()) { throw std::invalid_argument("cannot summarize empty samples"); }
    std::sort(samples.begin(), samples.end());
    const double position = percentile * static_cast<double>(samples.size() - 1U);
    const size_t lower = static_cast<size_t>(std::floor(position));
    const size_t upper = static_cast<size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return samples[lower] + (samples[upper] - samples[lower]) * fraction;
}

MethodStats Summarize(const std::vector<Timing>& timings, size_t total_bytes)
{
    std::vector<double> submit;
    std::vector<double> execution;
    std::vector<double> e2e;
    submit.reserve(timings.size());
    execution.reserve(timings.size());
    e2e.reserve(timings.size());
    for (const Timing& timing : timings) {
        submit.push_back(timing.submit_us);
        execution.push_back(timing.execution_us);
        e2e.push_back(timing.e2e_us);
    }

    MethodStats stats;
    stats.submit_p50_us = Percentile(submit, 0.50);
    stats.submit_p95_us = Percentile(submit, 0.95);
    stats.execution_p50_us = Percentile(execution, 0.50);
    stats.execution_p95_us = Percentile(execution, 0.95);
    stats.e2e_p50_us = Percentile(e2e, 0.50);
    stats.e2e_p95_us = Percentile(e2e, 0.95);
    if (stats.execution_p50_us > 0.0) {
        stats.execution_gib_per_second = static_cast<double>(total_bytes) /
                                         static_cast<double>(kGiB) /
                                         (stats.execution_p50_us / 1'000'000.0);
    }
    if (stats.e2e_p50_us > 0.0) {
        stats.e2e_gib_per_second = static_cast<double>(total_bytes) / static_cast<double>(kGiB) /
                                   (stats.e2e_p50_us / 1'000'000.0);
    }
    return stats;
}

size_t IterationCount(const Options& options, size_t total_bytes)
{
    if (!options.automatic_iterations) { return options.iterations; }
    const uint64_t bytes = static_cast<uint64_t>(total_bytes);
    const uint64_t iterations =
        options.target_bytes / bytes + (options.target_bytes % bytes == 0U ? 0U : 1U);
    return static_cast<size_t>(
        std::clamp<uint64_t>(iterations, kMinAutoIterations, kMaxAutoIterations));
}

Result RunCase(const Options& options, Direction direction, size_t size, size_t count,
               aclrtStream stream, EventTimer& event_timer)
{
    CaseBuffers buffers(direction, options.device_id, size, count);
    ValidateMethod(Method::kMemcpyLoop, buffers, stream, event_timer, options.timeout_ms);
    ValidateMethod(Method::kMemcpyBatch, buffers, stream, event_timer, options.timeout_ms);

    for (size_t iteration = 0U; iteration < options.warmup; ++iteration) {
        if (iteration % 2U == 0U) {
            (void)RunOnce(Method::kMemcpyLoop, buffers, stream, event_timer, options.timeout_ms,
                          false);
            (void)RunOnce(Method::kMemcpyBatch, buffers, stream, event_timer, options.timeout_ms,
                          true);
        } else {
            (void)RunOnce(Method::kMemcpyBatch, buffers, stream, event_timer, options.timeout_ms,
                          false);
            (void)RunOnce(Method::kMemcpyLoop, buffers, stream, event_timer, options.timeout_ms,
                          true);
        }
    }

    const size_t iterations = IterationCount(options, buffers.total_bytes());
    std::vector<Timing> loop_timings;
    std::vector<Timing> batch_timings;
    loop_timings.reserve(iterations);
    batch_timings.reserve(iterations);
    for (size_t iteration = 0U; iteration < iterations; ++iteration) {
        if (iteration % 2U == 0U) {
            loop_timings.push_back(RunOnce(Method::kMemcpyLoop, buffers, stream, event_timer,
                                           options.timeout_ms, false));
            batch_timings.push_back(RunOnce(Method::kMemcpyBatch, buffers, stream, event_timer,
                                            options.timeout_ms, true));
        } else {
            batch_timings.push_back(RunOnce(Method::kMemcpyBatch, buffers, stream, event_timer,
                                            options.timeout_ms, false));
            loop_timings.push_back(RunOnce(Method::kMemcpyLoop, buffers, stream, event_timer,
                                           options.timeout_ms, true));
        }
    }

    Result result;
    result.direction = direction;
    result.size = size;
    result.count = count;
    result.total_bytes = buffers.total_bytes();
    result.iterations = iterations;
    result.loop = Summarize(loop_timings, buffers.total_bytes());
    result.batch = Summarize(batch_timings, buffers.total_bytes());
    if (result.batch.submit_p50_us > 0.0) {
        result.submit_speedup = result.loop.submit_p50_us / result.batch.submit_p50_us;
    }
    if (result.batch.execution_p50_us > 0.0) {
        result.execution_speedup = result.loop.execution_p50_us / result.batch.execution_p50_us;
    }
    if (result.batch.e2e_p50_us > 0.0) {
        result.e2e_speedup = result.loop.e2e_p50_us / result.batch.e2e_p50_us;
    }
    return result;
}

std::string FormatBytes(size_t bytes)
{
    std::ostringstream os;
    if (bytes >= kGiB && bytes % kGiB == 0U) {
        os << bytes / kGiB << "G";
    } else if (bytes >= kMiB && bytes % kMiB == 0U) {
        os << bytes / kMiB << "M";
    } else if (bytes >= kKiB && bytes % kKiB == 0U) {
        os << bytes / kKiB << "K";
    } else {
        os << bytes << "B";
    }
    return os.str();
}

uint64_t ParseUnsigned(const std::string& text, const std::string& option)
{
    if (text.empty() || text[0] == '-') {
        throw std::invalid_argument(option + " requires a non-negative integer");
    }
    size_t parsed = 0U;
    const uint64_t value = std::stoull(text, &parsed, 10);
    if (parsed != text.size()) { throw std::invalid_argument("invalid value for " + option); }
    return value;
}

size_t ParseByteSize(std::string text)
{
    if (text.empty()) { throw std::invalid_argument("empty byte size"); }
    uint64_t multiplier = 1U;
    const char suffix = text.back();
    if (suffix == 'B' || suffix == 'b') { text.pop_back(); }
    if (!text.empty()) {
        const char unit = text.back();
        if (unit == 'K' || unit == 'k') {
            multiplier = kKiB;
            text.pop_back();
        } else if (unit == 'M' || unit == 'm') {
            multiplier = kMiB;
            text.pop_back();
        } else if (unit == 'G' || unit == 'g') {
            multiplier = kGiB;
            text.pop_back();
        }
    }
    const uint64_t value = ParseUnsigned(text, "byte size");
    if (value == 0U || value > std::numeric_limits<size_t>::max() / multiplier) {
        throw std::invalid_argument("byte size is zero or too large");
    }
    return static_cast<size_t>(value * multiplier);
}

std::vector<std::string> SplitList(const std::string& value)
{
    std::vector<std::string> parts;
    std::istringstream input(value);
    std::string part;
    while (std::getline(input, part, ',')) {
        if (part.empty()) { throw std::invalid_argument("list contains an empty item"); }
        parts.push_back(part);
    }
    if (parts.empty()) { throw std::invalid_argument("list must not be empty"); }
    return parts;
}

std::vector<size_t> ParseSizes(const std::string& value)
{
    std::vector<size_t> sizes;
    for (const std::string& part : SplitList(value)) { sizes.push_back(ParseByteSize(part)); }
    return sizes;
}

std::vector<size_t> ParseCounts(const std::string& value)
{
    std::vector<size_t> counts;
    for (const std::string& part : SplitList(value)) {
        const uint64_t count = ParseUnsigned(part, "--counts");
        if (count == 0U || count > std::numeric_limits<size_t>::max()) {
            throw std::invalid_argument("--counts values must be positive size_t values");
        }
        counts.push_back(static_cast<size_t>(count));
    }
    return counts;
}

std::string TakeValue(int argc, char** argv, int* index, const std::string& argument)
{
    const size_t equals = argument.find('=');
    if (equals != std::string::npos) { return argument.substr(equals + 1U); }
    if (*index + 1 >= argc) { throw std::invalid_argument("missing value for " + argument); }
    return argv[++(*index)];
}

void PrintUsage(const char* program)
{
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
              << "  --device N            ACL logical device (default: 0)\n"
              << "  --direction VALUE     h2d, d2h, or both (default: both)\n"
              << "  --sizes LIST          comma-separated B/K/M/G sizes\n"
              << "                        (default: 512B,1K,2K,4K,32K,64K,256K,"
                 "512K,1M,2M)\n"
              << "  --counts LIST         comma-separated batch counts\n"
              << "                        (default: 128)\n"
              << "  --warmup N            warmup pairs per case (default: 5)\n"
              << "  --iterations auto|N   measured pairs (default: auto)\n"
              << "  --target-bytes SIZE   bytes per method in auto mode (default: 256M)\n"
              << "  --timeout-ms N        stream synchronization timeout (default: 60000)\n"
              << "  --csv PATH            output CSV (default: results.csv)\n"
              << "  -h, --help            show this help\n";
}

Options ParseOptions(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const std::string name = argument.substr(0U, argument.find('='));
        if (name == "-h" || name == "--help") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else if (name == "--device") {
            const uint64_t value = ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
            if (value > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--device is too large");
            }
            options.device_id = static_cast<int32_t>(value);
        } else if (name == "--direction") {
            const std::string value = TakeValue(argc, argv, &index, argument);
            if (value == "h2d") {
                options.directions = {Direction::kHostToDevice};
            } else if (value == "d2h") {
                options.directions = {Direction::kDeviceToHost};
            } else if (value == "both") {
                options.directions = {Direction::kHostToDevice, Direction::kDeviceToHost};
            } else {
                throw std::invalid_argument("--direction must be h2d, d2h, or both");
            }
        } else if (name == "--sizes") {
            options.sizes = ParseSizes(TakeValue(argc, argv, &index, argument));
        } else if (name == "--counts") {
            options.counts = ParseCounts(TakeValue(argc, argv, &index, argument));
        } else if (name == "--warmup") {
            options.warmup =
                static_cast<size_t>(ParseUnsigned(TakeValue(argc, argv, &index, argument), name));
        } else if (name == "--iterations") {
            const std::string value = TakeValue(argc, argv, &index, argument);
            if (value == "auto") {
                options.automatic_iterations = true;
            } else {
                const uint64_t iterations = ParseUnsigned(value, name);
                if (iterations == 0U || iterations > std::numeric_limits<size_t>::max()) {
                    throw std::invalid_argument("--iterations must be positive");
                }
                options.automatic_iterations = false;
                options.iterations = static_cast<size_t>(iterations);
            }
        } else if (name == "--target-bytes") {
            options.target_bytes = ParseByteSize(TakeValue(argc, argv, &index, argument));
        } else if (name == "--timeout-ms") {
            const uint64_t timeout = ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
            if (timeout == 0U ||
                timeout > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--timeout-ms must be a positive int32 value");
            }
            options.timeout_ms = static_cast<int32_t>(timeout);
        } else if (name == "--csv") {
            options.csv_path = TakeValue(argc, argv, &index, argument);
            if (options.csv_path.empty()) {
                throw std::invalid_argument("--csv must not be empty");
            }
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    return options;
}

void PrintHeader()
{
    std::cout << std::left << std::setw(4) << "dir" << std::setw(7) << "size" << std::right
              << std::setw(7) << "count" << std::setw(7) << "iters" << std::setw(11) << "l.s50"
              << std::setw(11) << "l.s95" << std::setw(11) << "b.s50" << std::setw(11) << "b.s95"
              << std::setw(11) << "l.x50" << std::setw(11) << "l.x95" << std::setw(11) << "b.x50"
              << std::setw(11) << "b.x95" << std::setw(11) << "l.e50" << std::setw(11) << "l.e95"
              << std::setw(11) << "b.e50" << std::setw(11) << "b.e95" << std::setw(10) << "l.xGiB/s"
              << std::setw(10) << "b.xGiB/s" << std::setw(10) << "s.speed" << std::setw(10)
              << "x.speed" << std::setw(10) << "e.speed" << '\n';
}

void PrintResult(const Result& result)
{
    std::cout << std::fixed << std::setprecision(2) << std::left << std::setw(4)
              << DirectionName(result.direction) << std::setw(7) << FormatBytes(result.size)
              << std::right << std::setw(7) << result.count << std::setw(7) << result.iterations
              << std::setw(11) << result.loop.submit_p50_us << std::setw(11)
              << result.loop.submit_p95_us << std::setw(11) << result.batch.submit_p50_us
              << std::setw(11) << result.batch.submit_p95_us << std::setw(11)
              << result.loop.execution_p50_us << std::setw(11) << result.loop.execution_p95_us
              << std::setw(11) << result.batch.execution_p50_us << std::setw(11)
              << result.batch.execution_p95_us << std::setw(11) << result.loop.e2e_p50_us
              << std::setw(11) << result.loop.e2e_p95_us << std::setw(11) << result.batch.e2e_p50_us
              << std::setw(11) << result.batch.e2e_p95_us << std::setw(10)
              << result.loop.execution_gib_per_second << std::setw(10)
              << result.batch.execution_gib_per_second << std::setw(10) << result.submit_speedup
              << std::setw(10) << result.execution_speedup << std::setw(10) << result.e2e_speedup
              << '\n';
}

void WriteCsv(const std::string& path, const std::vector<Result>& results)
{
    std::ofstream output(path);
    if (!output) { throw std::runtime_error("cannot open CSV for writing: " + path); }
    output << "direction,size_bytes,count,total_bytes,iterations,"
              "loop_submit_p50_us,loop_submit_p95_us,"
              "loop_execution_p50_us,loop_execution_p95_us,"
              "loop_e2e_p50_us,loop_e2e_p95_us,loop_execution_gib_per_second,"
              "loop_e2e_gib_per_second,batch_submit_p50_us,batch_submit_p95_us,"
              "batch_execution_p50_us,batch_execution_p95_us,"
              "batch_e2e_p50_us,batch_e2e_p95_us,batch_execution_gib_per_second,"
              "batch_e2e_gib_per_second,submit_speedup,execution_speedup,e2e_speedup,verify\n";
    output << std::setprecision(10);
    for (const Result& result : results) {
        output << DirectionName(result.direction) << ',' << result.size << ',' << result.count
               << ',' << result.total_bytes << ',' << result.iterations << ','
               << result.loop.submit_p50_us << ',' << result.loop.submit_p95_us << ','
               << result.loop.execution_p50_us << ',' << result.loop.execution_p95_us << ','
               << result.loop.e2e_p50_us << ',' << result.loop.e2e_p95_us << ','
               << result.loop.execution_gib_per_second << ',' << result.loop.e2e_gib_per_second
               << ',' << result.batch.submit_p50_us << ',' << result.batch.submit_p95_us << ','
               << result.batch.execution_p50_us << ',' << result.batch.execution_p95_us << ','
               << result.batch.e2e_p50_us << ',' << result.batch.e2e_p95_us << ','
               << result.batch.execution_gib_per_second << ',' << result.batch.e2e_gib_per_second
               << ',' << result.submit_speedup << ',' << result.execution_speedup << ','
               << result.e2e_speedup << ",PASS\n";
    }
}

void PrintConfiguration(const Options& options)
{
    std::cout << "device=" << options.device_id << ", warmup=" << options.warmup
              << ", timeout_ms=" << options.timeout_ms << ", iterations=";
    if (options.automatic_iterations) {
        std::cout << "auto (target " << FormatBytes(options.target_bytes) << ", clamp "
                  << kMinAutoIterations << ".." << kMaxAutoIterations << ')';
    } else {
        std::cout << options.iterations;
    }
    std::cout << "\nPinned Host memory: aclrtMallocHost\n"
              << "Timing: submit/e2e use an Event-free pass; execution uses a separate, "
                 "identical timeline-Event pass\n"
              << "Columns: l=aclrtMemcpyAsync loop, b=aclrtMemcpyBatchAsync, "
                 "s=submit, x=execution, e=e2e\n"
              << "Speedup: loop / batch (>1 means aclrtMemcpyBatchAsync is faster)\n\n";
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = ParseOptions(argc, argv);
        PrintConfiguration(options);

        RuntimeSession runtime(options.device_id);
        Stream stream;
        EventTimer event_timer;
        std::vector<Result> results;
        PrintHeader();
        for (Direction direction : options.directions) {
            for (size_t size : options.sizes) {
                for (size_t count : options.counts) {
                    Result result =
                        RunCase(options, direction, size, count, stream.get(), event_timer);
                    PrintResult(result);
                    results.push_back(std::move(result));
                }
            }
        }
        WriteCsv(options.csv_path, results);
        std::cout << "\nCSV written to " << options.csv_path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
