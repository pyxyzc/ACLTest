#include <acl/acl.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kKiB = 1024U;
constexpr size_t kIoCount = 3U;
constexpr size_t kFirstIoBytes = 16U * kKiB;
constexpr size_t kLastIoBytes = 256U * kKiB;
constexpr size_t kIoStepBytes = 16U * kKiB;
constexpr size_t kDefaultWarmup = 10U;
constexpr size_t kDefaultRounds = 100U;
constexpr int32_t kDefaultTimeoutMs = 60000;

enum class Direction { kHostToDevice, kDeviceToHost };
enum class Method { kLoop, kBatch };
enum class MethodMode { kBoth, kLoop, kBatch };
enum class Phase { kSubmitApi, kSubmitRound, kSync };

struct Options {
    int32_t device_id = 0;
    std::vector<Direction> directions = {Direction::kHostToDevice};
    std::vector<size_t> sizes;
    MethodMode method_mode = MethodMode::kBoth;
    size_t warmup = kDefaultWarmup;
    size_t rounds = kDefaultRounds;
    int32_t timeout_ms = kDefaultTimeoutMs;
    std::string summary_path = "shard_io_summary.csv";
    std::string trace_path = "shard_io_trace.csv";
};

struct Span {
    Clock::time_point start;
    Clock::time_point end;
};

struct Measurement {
    std::vector<double> submit_api_us;
    double submit_round_us = 0.0;
    double sync_us = 0.0;
};

struct MethodSamples {
    std::vector<double> submit_api_us;
    std::vector<double> submit_round_us;
    std::vector<double> sync_us;
};

struct TraceRow {
    size_t size_bytes = 0U;
    size_t round = 0U;
    Direction direction = Direction::kHostToDevice;
    Method method = Method::kLoop;
    Phase phase = Phase::kSubmitApi;
    int64_t io_index = -1;
    size_t io_count = kIoCount;
    uint64_t start_ns = 0U;
    uint64_t end_ns = 0U;
};

struct SummaryRow {
    size_t size_bytes = 0U;
    Direction direction = Direction::kHostToDevice;
    Method method = Method::kLoop;
    Phase phase = Phase::kSubmitApi;
    size_t samples = 0U;
    double avg_us = 0.0;
    double p50_us = 0.0;
    double p95_us = 0.0;
};

std::vector<size_t> DefaultSizes()
{
    std::vector<size_t> sizes;
    for (size_t bytes = kFirstIoBytes; bytes <= kLastIoBytes; bytes += kIoStepBytes) {
        sizes.push_back(bytes);
    }
    return sizes;
}

std::string RecentAclError()
{
    const char* message = aclGetRecentErrMsg();
    return message == nullptr ? "" : message;
}

[[noreturn]] void ThrowAclError(const std::string& operation, aclError error)
{
    std::ostringstream output;
    output << operation << " failed, aclError=" << static_cast<int64_t>(error);
    const std::string recent = RecentAclError();
    if (!recent.empty()) { output << ", recent=\"" << recent << '"'; }
    throw std::runtime_error(output.str());
}

void CheckAcl(const std::string& operation, aclError error)
{
    if (error != ACL_SUCCESS) { ThrowAclError(operation, error); }
}

size_t CheckedMultiply(size_t left, size_t right)
{
    if (left != 0U && right > std::numeric_limits<size_t>::max() / left) {
        throw std::overflow_error("IO size * IO count overflows size_t");
    }
    return left * right;
}

class RuntimeSession {
public:
    explicit RuntimeSession(int32_t device_id) : device_id_(device_id)
    {
        CheckAcl("aclInit", aclInit(nullptr));
        initialized_ = true;
        try {
            CheckAcl("aclrtSetDevice", aclrtSetDevice(device_id_));
            device_set_ = true;
        } catch (...) {
            Cleanup();
            throw;
        }
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

private:
    void* data_ = nullptr;
    size_t bytes_ = 0U;
};

class IoBuffers {
public:
    IoBuffers(Direction direction, int32_t device_id, size_t io_bytes)
        : direction_(direction), device_id_(device_id), io_bytes_(io_bytes),
          total_bytes_(CheckedMultiply(io_bytes_, kIoCount)), host_(total_bytes_),
          device_(total_bytes_), expected_(total_bytes_), destinations_(kIoCount),
          destination_max_sizes_(kIoCount, io_bytes_), sources_(kIoCount),
          copy_sizes_(kIoCount, io_bytes_)
    {
        FillSource();
        BuildPointers();
        BuildAttribute();
        if (direction_ == Direction::kDeviceToHost) {
            CheckAcl("initial H2D aclrtMemcpy",
                     aclrtMemcpy(device_.get(), total_bytes_, host_.get(), total_bytes_,
                                 ACL_MEMCPY_HOST_TO_DEVICE));
        }
    }

    void ResetDestination()
    {
        constexpr int32_t kSentinel = 0xA5;
        if (direction_ == Direction::kHostToDevice) {
            CheckAcl("aclrtMemset(device destination)",
                     aclrtMemset(device_.get(), total_bytes_, kSentinel, total_bytes_));
        } else {
            std::memset(host_.get(), kSentinel, total_bytes_);
        }
    }

    void Verify()
    {
        std::vector<uint8_t> readback;
        const uint8_t* actual = nullptr;
        if (direction_ == Direction::kHostToDevice) {
            readback.resize(total_bytes_);
            CheckAcl("verification D2H aclrtMemcpy",
                     aclrtMemcpy(readback.data(), readback.size(), device_.get(), total_bytes_,
                                 ACL_MEMCPY_DEVICE_TO_HOST));
            actual = readback.data();
        } else {
            actual = static_cast<const uint8_t*>(host_.get());
        }
        if (std::memcmp(actual, expected_.data(), total_bytes_) != 0) {
            throw std::runtime_error("3-IO memcpy verification failed");
        }
    }

    Direction direction() const { return direction_; }
    size_t io_bytes() const { return io_bytes_; }
    void** destinations() { return destinations_.data(); }
    size_t* destination_max_sizes() { return destination_max_sizes_.data(); }
    void** sources() { return sources_.data(); }
    size_t* copy_sizes() { return copy_sizes_.data(); }
    aclrtMemcpyBatchAttr* attribute() { return &attribute_; }
    size_t* attribute_index() { return &attribute_index_; }

private:
    void FillSource()
    {
        for (size_t index = 0U; index < total_bytes_; ++index) {
            expected_[index] = static_cast<uint8_t>((index * 131U + index / 251U + 17U) & 0xFFU);
        }
        std::memcpy(host_.get(), expected_.data(), expected_.size());
    }

    void BuildPointers()
    {
        auto* host = static_cast<uint8_t*>(host_.get());
        auto* device = static_cast<uint8_t*>(device_.get());
        for (size_t index = 0U; index < kIoCount; ++index) {
            const size_t offset = index * io_bytes_;
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
    size_t io_bytes_ = 0U;
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

std::string DirectionName(Direction direction)
{
    return direction == Direction::kHostToDevice ? "h2d" : "d2h";
}

std::string MethodName(Method method)
{
    return method == Method::kLoop ? "loop" : "batch";
}

std::string PhaseName(Phase phase)
{
    if (phase == Phase::kSubmitApi) { return "submit_api"; }
    if (phase == Phase::kSubmitRound) { return "submit_round"; }
    return "sync";
}

std::string FormatBytes(size_t bytes)
{
    std::ostringstream output;
    if (bytes % (kKiB * kKiB) == 0U) {
        output << bytes / (kKiB * kKiB) << 'M';
    } else if (bytes % kKiB == 0U) {
        output << bytes / kKiB << 'K';
    } else {
        output << bytes << 'B';
    }
    return output.str();
}

std::string FormatMicroseconds(double value)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

std::vector<Method> SelectedMethods(MethodMode mode)
{
    if (mode == MethodMode::kLoop) { return {Method::kLoop}; }
    if (mode == MethodMode::kBatch) { return {Method::kBatch}; }
    return {Method::kLoop, Method::kBatch};
}

size_t MethodIndex(const std::vector<Method>& methods, Method method)
{
    for (size_t index = 0U; index < methods.size(); ++index) {
        if (methods[index] == method) { return index; }
    }
    return methods.size();
}

uint64_t SinceNanoseconds(Clock::time_point origin, Clock::time_point point)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(point - origin).count());
}

double DurationMicroseconds(const Span& span)
{
    return std::chrono::duration<double, std::micro>(span.end - span.start).count();
}

void SubmitLoop(IoBuffers& buffers, aclrtStream stream)
{
    const aclrtMemcpyKind kind = buffers.direction() == Direction::kHostToDevice
                                     ? ACL_MEMCPY_HOST_TO_DEVICE
                                     : ACL_MEMCPY_DEVICE_TO_HOST;
    for (size_t index = 0U; index < kIoCount; ++index) {
        const aclError error = aclrtMemcpyAsync(
            buffers.destinations()[index], buffers.io_bytes(), buffers.sources()[index],
            buffers.io_bytes(), kind, stream);
        if (error != ACL_SUCCESS) {
            ThrowAclError("aclrtMemcpyAsync[" + std::to_string(index) + "]", error);
        }
    }
}

void SubmitBatch(IoBuffers& buffers, aclrtStream stream)
{
    size_t fail_index = std::numeric_limits<size_t>::max();
    const aclError error = aclrtMemcpyBatchAsync(
        buffers.destinations(), buffers.destination_max_sizes(), buffers.sources(),
        buffers.copy_sizes(), kIoCount, buffers.attribute(), buffers.attribute_index(), 1U,
        &fail_index, stream);
    if (error != ACL_SUCCESS) {
        std::ostringstream operation;
        operation << "aclrtMemcpyBatchAsync";
        if (fail_index != std::numeric_limits<size_t>::max()) {
            operation << "[failIndex=" << fail_index << ']';
        }
        ThrowAclError(operation.str(), error);
    }
}

void Submit(Method method, IoBuffers& buffers, aclrtStream stream)
{
    if (method == Method::kLoop) {
        SubmitLoop(buffers, stream);
    } else {
        SubmitBatch(buffers, stream);
    }
}

void Synchronize(aclrtStream stream, int32_t timeout_ms)
{
    CheckAcl("aclrtSynchronizeStreamWithTimeout",
             aclrtSynchronizeStreamWithTimeout(stream, timeout_ms));
}

void Validate(Method method, IoBuffers& buffers, aclrtStream stream, int32_t timeout_ms)
{
    buffers.ResetDestination();
    Submit(method, buffers, stream);
    Synchronize(stream, timeout_ms);
    buffers.Verify();
}

void RunUnrecorded(Method method, IoBuffers& buffers, aclrtStream stream, int32_t timeout_ms)
{
    buffers.ResetDestination();
    Submit(method, buffers, stream);
    Synchronize(stream, timeout_ms);
}

void AppendTrace(std::vector<TraceRow>* trace, size_t size_bytes, size_t round,
                 Direction direction, Method method, Phase phase, int64_t io_index,
                 Clock::time_point origin, const Span& span)
{
    trace->push_back({size_bytes, round, direction, method, phase, io_index, kIoCount,
                      SinceNanoseconds(origin, span.start), SinceNanoseconds(origin, span.end)});
}

Measurement RunMeasured(Method method, size_t size_bytes, size_t round, IoBuffers& buffers,
                        aclrtStream stream, Clock::time_point origin, int32_t timeout_ms,
                        std::vector<TraceRow>* trace)
{
    buffers.ResetDestination();
    Measurement measurement;
    Span submit_round;

    if (method == Method::kLoop) {
        const aclrtMemcpyKind kind = buffers.direction() == Direction::kHostToDevice
                                         ? ACL_MEMCPY_HOST_TO_DEVICE
                                         : ACL_MEMCPY_DEVICE_TO_HOST;
        for (size_t index = 0U; index < kIoCount; ++index) {
            const auto start = Clock::now();
            const aclError error = aclrtMemcpyAsync(
                buffers.destinations()[index], buffers.io_bytes(), buffers.sources()[index],
                buffers.io_bytes(), kind, stream);
            const auto end = Clock::now();
            if (error != ACL_SUCCESS) {
                ThrowAclError("aclrtMemcpyAsync[" + std::to_string(index) + "]", error);
            }
            const Span span{start, end};
            if (index == 0U) { submit_round.start = start; }
            if (index + 1U == kIoCount) { submit_round.end = end; }
            measurement.submit_api_us.push_back(DurationMicroseconds(span));
            AppendTrace(trace, size_bytes, round, buffers.direction(), method, Phase::kSubmitApi,
                        static_cast<int64_t>(index), origin, span);
        }
    } else {
        size_t fail_index = std::numeric_limits<size_t>::max();
        const auto start = Clock::now();
        const aclError error = aclrtMemcpyBatchAsync(
            buffers.destinations(), buffers.destination_max_sizes(), buffers.sources(),
            buffers.copy_sizes(), kIoCount, buffers.attribute(), buffers.attribute_index(), 1U,
            &fail_index, stream);
        const auto end = Clock::now();
        if (error != ACL_SUCCESS) {
            std::ostringstream operation;
            operation << "aclrtMemcpyBatchAsync";
            if (fail_index != std::numeric_limits<size_t>::max()) {
                operation << "[failIndex=" << fail_index << ']';
            }
            ThrowAclError(operation.str(), error);
        }
        const Span span{start, end};
        submit_round = span;
        measurement.submit_api_us.push_back(DurationMicroseconds(span));
        AppendTrace(trace, size_bytes, round, buffers.direction(), method, Phase::kSubmitApi, -1,
                    origin, span);
    }

    measurement.submit_round_us = DurationMicroseconds(submit_round);
    AppendTrace(trace, size_bytes, round, buffers.direction(), method, Phase::kSubmitRound, -1,
                origin, submit_round);

    const auto sync_start = Clock::now();
    Synchronize(stream, timeout_ms);
    const Span completed_sync{sync_start, Clock::now()};
    measurement.sync_us = DurationMicroseconds(completed_sync);
    AppendTrace(trace, size_bytes, round, buffers.direction(), method, Phase::kSync, -1, origin,
                completed_sync);
    return measurement;
}

double Percentile(std::vector<double> samples, double percentile)
{
    if (samples.empty()) { throw std::invalid_argument("cannot summarize empty samples"); }
    std::sort(samples.begin(), samples.end());
    const double position = percentile * static_cast<double>(samples.size() - 1U);
    const size_t lower = static_cast<size_t>(position);
    const size_t upper = lower + (lower + 1U < samples.size() ? 1U : 0U);
    const double fraction = position - static_cast<double>(lower);
    return samples[lower] + (samples[upper] - samples[lower]) * fraction;
}

double Average(const std::vector<double>& samples)
{
    if (samples.empty()) { throw std::invalid_argument("cannot average empty samples"); }
    double total = 0.0;
    for (double sample : samples) { total += sample; }
    return total / static_cast<double>(samples.size());
}

SummaryRow MakeSummary(size_t size_bytes, Direction direction, Method method, Phase phase,
                       const std::vector<double>& samples)
{
    return {size_bytes, direction, method, phase, samples.size(), Average(samples),
            Percentile(samples, 0.50), Percentile(samples, 0.95)};
}

void AddMethodSummaries(size_t size_bytes, Direction direction, Method method,
                        const MethodSamples& samples, std::vector<SummaryRow>* output)
{
    output->push_back(MakeSummary(size_bytes, direction, method, Phase::kSubmitApi,
                                  samples.submit_api_us));
    output->push_back(MakeSummary(size_bytes, direction, method, Phase::kSubmitRound,
                                  samples.submit_round_us));
    output->push_back(
        MakeSummary(size_bytes, direction, method, Phase::kSync, samples.sync_us));
}

size_t ParseUnsigned(const std::string& text, const std::string& option)
{
    if (text.empty() || text[0] == '-') {
        throw std::invalid_argument(option + " requires a non-negative integer");
    }
    size_t parsed = 0U;
    const uint64_t value = std::stoull(text, &parsed, 10);
    if (parsed != text.size()) { throw std::invalid_argument("invalid value for " + option); }
    if (value > std::numeric_limits<size_t>::max()) {
        throw std::invalid_argument(option + " does not fit in size_t");
    }
    return static_cast<size_t>(value);
}

size_t ParseByteSize(std::string text)
{
    if (text.empty()) { throw std::invalid_argument("empty IO size"); }
    uint64_t multiplier = 1U;
    const char suffix = text.back();
    if (suffix == 'B' || suffix == 'b') { text.pop_back(); }
    if (!text.empty()) {
        const char unit = text.back();
        if (unit == 'K' || unit == 'k') {
            multiplier = kKiB;
            text.pop_back();
        } else if (unit == 'M' || unit == 'm') {
            multiplier = kKiB * kKiB;
            text.pop_back();
        } else if (unit == 'G' || unit == 'g') {
            multiplier = kKiB * kKiB * kKiB;
            text.pop_back();
        }
    }
    const uint64_t value = ParseUnsigned(text, "IO size");
    if (value == 0U || value > std::numeric_limits<size_t>::max() / multiplier) {
        throw std::invalid_argument("IO size must be positive and fit in size_t");
    }
    return static_cast<size_t>(value * multiplier);
}

std::vector<std::string> SplitList(const std::string& text)
{
    std::vector<std::string> parts;
    std::istringstream input(text);
    std::string part;
    while (std::getline(input, part, ',')) {
        if (part.empty()) { throw std::invalid_argument("size list contains an empty item"); }
        parts.push_back(part);
    }
    if (parts.empty()) { throw std::invalid_argument("size list must not be empty"); }
    return parts;
}

std::vector<size_t> ParseSizeList(const std::string& text)
{
    std::vector<size_t> sizes;
    for (const std::string& part : SplitList(text)) { sizes.push_back(ParseByteSize(part)); }
    return sizes;
}

std::string TakeValue(int argc, char** argv, int* index, const std::string& argument)
{
    const size_t equals = argument.find('=');
    if (equals != std::string::npos) { return argument.substr(equals + 1U); }
    if (*index + 1 >= argc) { throw std::invalid_argument("missing value for " + argument); }
    return argv[++(*index)];
}

MethodMode ParseMethodMode(const std::string& value)
{
    if (value == "loop") { return MethodMode::kLoop; }
    if (value == "batch") { return MethodMode::kBatch; }
    if (value == "both") { return MethodMode::kBoth; }
    throw std::invalid_argument("--method must be loop, batch, or both");
}

void ParseDirection(const std::string& value, Options* options)
{
    if (value == "h2d") {
        options->directions = {Direction::kHostToDevice};
    } else if (value == "d2h") {
        options->directions = {Direction::kDeviceToHost};
    } else if (value == "both") {
        options->directions = {Direction::kHostToDevice, Direction::kDeviceToHost};
    } else {
        throw std::invalid_argument("--direction must be h2d, d2h, or both");
    }
}

Options ParseOptions(int argc, char** argv)
{
    Options options;
    options.sizes = DefaultSizes();
    bool sizes_set = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const std::string name = argument.substr(0U, argument.find('='));
        if (name == "-h" || name == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n\n"
                      << "Scenario: 3 non-overlapping memcpy IOs per round\n"
                      << "Options:\n"
                      << "  --device N            ACL device (default: 0)\n"
                      << "  --direction VALUE     h2d, d2h, or both (default: h2d)\n"
                      << "  --method VALUE        loop, batch, or both (default: both)\n"
                      << "  --sizes LIST          comma-separated sizes (default: 16K..256K by 16K)\n"
                      << "  --io-size SIZE        single-size shortcut; conflicts with --sizes\n"
                      << "  --warmup N            unrecorded rounds (default: " << kDefaultWarmup << ")\n"
                      << "  --rounds N            measured rounds (default: " << kDefaultRounds << ")\n"
                      << "  --timeout-ms N        stream sync timeout (default: " << kDefaultTimeoutMs
                      << ")\n"
                      << "  --csv PATH            summary CSV (default: shard_io_summary.csv)\n"
                      << "  --trace PATH          raw trace CSV (default: shard_io_trace.csv)\n"
                      << "  -h, --help            show this help\n";
            std::exit(0);
        } else if (name == "--device") {
            const uint64_t device = ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
            if (device > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--device is too large");
            }
            options.device_id = static_cast<int32_t>(device);
        } else if (name == "--direction") {
            ParseDirection(TakeValue(argc, argv, &index, argument), &options);
        } else if (name == "--method") {
            options.method_mode = ParseMethodMode(TakeValue(argc, argv, &index, argument));
        } else if (name == "--sizes") {
            if (sizes_set) { throw std::invalid_argument("--sizes specified more than once"); }
            options.sizes = ParseSizeList(TakeValue(argc, argv, &index, argument));
            sizes_set = true;
        } else if (name == "--io-size") {
            if (sizes_set) { throw std::invalid_argument("--io-size conflicts with --sizes"); }
            options.sizes = {ParseByteSize(TakeValue(argc, argv, &index, argument))};
            sizes_set = true;
        } else if (name == "--warmup") {
            options.warmup = ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
        } else if (name == "--rounds" || name == "--iterations") {
            options.rounds = ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
            if (options.rounds == 0U) { throw std::invalid_argument(name + " must be positive"); }
        } else if (name == "--timeout-ms") {
            const uint64_t timeout = ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
            if (timeout == 0U ||
                timeout > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--timeout-ms must be positive and fit in int32");
            }
            options.timeout_ms = static_cast<int32_t>(timeout);
        } else if (name == "--csv") {
            options.summary_path = TakeValue(argc, argv, &index, argument);
            if (options.summary_path.empty()) { throw std::invalid_argument("--csv is empty"); }
        } else if (name == "--trace") {
            options.trace_path = TakeValue(argc, argv, &index, argument);
            if (options.trace_path.empty()) { throw std::invalid_argument("--trace is empty"); }
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    return options;
}

void WriteTrace(const std::string& path, const std::vector<TraceRow>& trace)
{
    std::ofstream output(path);
    if (!output) { throw std::runtime_error("cannot open trace CSV: " + path); }
    output << "size_bytes,round,direction,method,phase,io_index,io_count,start_ns,end_ns,duration_us\n";
    output << std::fixed << std::setprecision(3);
    for (const TraceRow& row : trace) {
        const double duration_us = static_cast<double>(row.end_ns - row.start_ns) / 1000.0;
        output << row.size_bytes << ',' << row.round << ',' << DirectionName(row.direction) << ','
               << MethodName(row.method) << ',' << PhaseName(row.phase) << ',' << row.io_index
               << ',' << row.io_count << ',' << row.start_ns << ',' << row.end_ns << ','
               << duration_us << '\n';
    }
}

void WriteSummary(const std::string& path, const std::vector<SummaryRow>& summary)
{
    std::ofstream output(path);
    if (!output) { throw std::runtime_error("cannot open summary CSV: " + path); }
    output << "size_bytes,direction,method,metric,samples,avg_us,p50_us,p95_us\n";
    output << std::fixed << std::setprecision(6);
    for (const SummaryRow& row : summary) {
        output << row.size_bytes << ',' << DirectionName(row.direction) << ','
               << MethodName(row.method) << ',' << PhaseName(row.phase) << ',' << row.samples
               << ',' << row.avg_us << ',' << row.p50_us << ',' << row.p95_us << '\n';
    }
}

void PrintSummary(const std::vector<SummaryRow>& summary)
{
    std::cout << "\nSummary (microseconds)\n"
              << std::left << std::setw(8) << "size" << std::setw(8) << "dir" << std::setw(8)
              << "method" << std::setw(12) << "api_avg" << std::setw(12) << "api_p50"
              << std::setw(12) << "api_p95" << std::setw(14) << "round_avg" << std::setw(14)
              << "round_p50" << std::setw(14) << "round_p95" << std::setw(12) << "sync_avg"
              << std::setw(12) << "sync_p50" << std::setw(12) << "sync_p95" << '\n';
    std::cout << std::string(116U, '-') << '\n';
    for (size_t index = 0U; index + 2U < summary.size(); index += 3U) {
        const SummaryRow& api = summary[index];
        const SummaryRow& round = summary[index + 1U];
        const SummaryRow& sync = summary[index + 2U];
        std::cout << std::left << std::setw(8) << FormatBytes(api.size_bytes) << std::setw(8)
                  << DirectionName(api.direction) << std::setw(8) << MethodName(api.method)
                  << std::setw(12) << FormatMicroseconds(api.avg_us) << std::setw(12)
                  << FormatMicroseconds(api.p50_us) << std::setw(12)
                  << FormatMicroseconds(api.p95_us) << std::setw(14)
                  << FormatMicroseconds(round.avg_us) << std::setw(14)
                  << FormatMicroseconds(round.p50_us) << std::setw(14)
                  << FormatMicroseconds(round.p95_us) << std::setw(12)
                  << FormatMicroseconds(sync.avg_us) << std::setw(12)
                  << FormatMicroseconds(sync.p50_us) << std::setw(12)
                  << FormatMicroseconds(sync.p95_us) << '\n';
    }
}

std::vector<Method> RoundOrder(const std::vector<Method>& methods, size_t round)
{
    if (methods.size() == 2U && round % 2U != 0U) {
        return {methods[1], methods[0]};
    }
    return methods;
}

void PrintConfiguration(const Options& options)
{
    std::cout << "sizes=";
    for (size_t index = 0U; index < options.sizes.size(); ++index) {
        if (index != 0U) { std::cout << ','; }
        std::cout << options.sizes[index];
    }
    std::cout << " bytes, io_count=" << kIoCount << ", direction=";
    for (size_t index = 0U; index < options.directions.size(); ++index) {
        if (index != 0U) { std::cout << ','; }
        std::cout << DirectionName(options.directions[index]);
    }
    std::cout << ", warmup=" << options.warmup << ", rounds=" << options.rounds
              << "\nTiming: submit_api, submit_round, and sync only; no end-to-end interval\n";
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = ParseOptions(argc, argv);
        PrintConfiguration(options);

        RuntimeSession runtime(options.device_id);
        Stream stream;
        const std::vector<Method> methods = SelectedMethods(options.method_mode);
        const Clock::time_point origin = Clock::now();
        std::vector<TraceRow> trace;
        std::vector<SummaryRow> summary;
        trace.reserve(options.sizes.size() * options.directions.size() * options.rounds *
                      methods.size() * (kIoCount + 2U));

        for (Direction direction : options.directions) {
            for (size_t size_bytes : options.sizes) {
                IoBuffers buffers(direction, options.device_id, size_bytes);
                std::vector<MethodSamples> samples(methods.size());

                for (Method method : methods) {
                    Validate(method, buffers, stream.get(), options.timeout_ms);
                }
                for (size_t warmup = 0U; warmup < options.warmup; ++warmup) {
                    for (Method method : RoundOrder(methods, warmup)) {
                        RunUnrecorded(method, buffers, stream.get(), options.timeout_ms);
                    }
                }

                for (size_t round = 0U; round < options.rounds; ++round) {
                    for (Method method : RoundOrder(methods, round)) {
                        const Measurement measurement =
                            RunMeasured(method, size_bytes, round, buffers, stream.get(), origin,
                                        options.timeout_ms, &trace);
                        const size_t method_index = MethodIndex(methods, method);
                        if (method_index >= samples.size()) { continue; }
                        MethodSamples& method_samples = samples[method_index];
                        method_samples.submit_api_us.insert(method_samples.submit_api_us.end(),
                                                            measurement.submit_api_us.begin(),
                                                            measurement.submit_api_us.end());
                        method_samples.submit_round_us.push_back(measurement.submit_round_us);
                        method_samples.sync_us.push_back(measurement.sync_us);
                    }
                }

                for (size_t method_index = 0U; method_index < methods.size(); ++method_index) {
                    AddMethodSummaries(size_bytes, direction, methods[method_index],
                                       samples[method_index], &summary);
                }
            }
        }

        WriteSummary(options.summary_path, summary);
        WriteTrace(options.trace_path, trace);
        PrintSummary(summary);
        std::cout << "summary_rows=" << summary.size() << ", trace_rows=" << trace.size()
                  << "\nSummary written to " << options.summary_path << "\nTrace written to "
                  << options.trace_path << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
