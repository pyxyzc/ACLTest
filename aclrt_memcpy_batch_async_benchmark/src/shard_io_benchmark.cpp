#include <acl/acl.h>

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

constexpr size_t kDefaultShardBytes = 176U * 1024U;
constexpr size_t kShardCount = 460U;
constexpr int32_t kDefaultTimeoutMs = 60000;
constexpr size_t kDefaultRounds = 10U;

enum class Direction { kHostToDevice, kDeviceToHost };
enum class Method { kLoop, kBatch };
enum class MethodMode { kBoth, kLoop, kBatch };

struct Options {
    int32_t device_id = 0;
    std::vector<Direction> directions = {Direction::kHostToDevice};
    MethodMode method_mode = MethodMode::kBoth;
    size_t io_bytes = kDefaultShardBytes;
    size_t warmup = 0U;
    size_t rounds = kDefaultRounds;
    int32_t timeout_ms = kDefaultTimeoutMs;
    std::string csv_path = "shard_io_trace.csv";
};

enum class Phase { kSubmit, kSync };

struct TraceRow {
    size_t iteration = 0U;
    Direction direction = Direction::kHostToDevice;
    Method method = Method::kLoop;
    Phase phase = Phase::kSubmit;
    int64_t shard_index = -1;
    size_t shard_count = kShardCount;
    size_t io_bytes = kDefaultShardBytes;
    uint64_t start_ns = 0U;
    uint64_t end_ns = 0U;
};

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

size_t CheckedTotalBytes(size_t io_bytes)
{
    if (io_bytes != 0U && kShardCount > std::numeric_limits<size_t>::max() / io_bytes) {
        throw std::overflow_error("--io-size * shard count overflows size_t");
    }
    return io_bytes * kShardCount;
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

class ShardBuffers {
public:
    ShardBuffers(Direction direction, int32_t device_id, size_t io_bytes)
        : direction_(direction), device_id_(device_id), io_bytes_(io_bytes),
          total_bytes_(CheckedTotalBytes(io_bytes_)), host_(total_bytes_), device_(total_bytes_),
          expected_(total_bytes_), destinations_(kShardCount),
          destination_max_sizes_(kShardCount, io_bytes_), sources_(kShardCount),
          copy_sizes_(kShardCount, io_bytes_)
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
            throw std::runtime_error("shard copy verification failed");
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
        for (size_t index = 0U; index < kShardCount; ++index) {
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
    return phase == Phase::kSubmit ? "submit" : "sync";
}

std::vector<Method> SelectedMethods(MethodMode mode)
{
    if (mode == MethodMode::kLoop) { return {Method::kLoop}; }
    if (mode == MethodMode::kBatch) { return {Method::kBatch}; }
    return {Method::kLoop, Method::kBatch};
}

uint64_t SinceNanoseconds(Clock::time_point origin, Clock::time_point point)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(point - origin).count());
}

void SubmitLoop(ShardBuffers& buffers, aclrtStream stream)
{
    const aclrtMemcpyKind kind = buffers.direction() == Direction::kHostToDevice
                                     ? ACL_MEMCPY_HOST_TO_DEVICE
                                     : ACL_MEMCPY_DEVICE_TO_HOST;
    for (size_t index = 0U; index < kShardCount; ++index) {
        const aclError error = aclrtMemcpyAsync(
            buffers.destinations()[index], buffers.io_bytes(), buffers.sources()[index],
            buffers.io_bytes(),
            kind, stream);
        if (error != ACL_SUCCESS) {
            ThrowAclError("aclrtMemcpyAsync[" + std::to_string(index) + "]", error);
        }
    }
}

void SubmitBatch(ShardBuffers& buffers, aclrtStream stream)
{
    size_t fail_index = std::numeric_limits<size_t>::max();
    const aclError error = aclrtMemcpyBatchAsync(
        buffers.destinations(), buffers.destination_max_sizes(), buffers.sources(),
        buffers.copy_sizes(), kShardCount, buffers.attribute(), buffers.attribute_index(), 1U,
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

void Submit(Method method, ShardBuffers& buffers, aclrtStream stream)
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

void Validate(Method method, ShardBuffers& buffers, aclrtStream stream, int32_t timeout_ms)
{
    buffers.ResetDestination();
    Submit(method, buffers, stream);
    Synchronize(stream, timeout_ms);
    buffers.Verify();
}

void RunMeasured(Method method, size_t iteration, ShardBuffers& buffers, aclrtStream stream,
                 const Clock::time_point origin, int32_t timeout_ms, std::vector<TraceRow>* trace)
{
    buffers.ResetDestination();

    if (method == Method::kLoop) {
        const aclrtMemcpyKind kind = buffers.direction() == Direction::kHostToDevice
                                         ? ACL_MEMCPY_HOST_TO_DEVICE
                                         : ACL_MEMCPY_DEVICE_TO_HOST;
        for (size_t index = 0U; index < kShardCount; ++index) {
            const auto start = Clock::now();
            const aclError error = aclrtMemcpyAsync(
                buffers.destinations()[index], buffers.io_bytes(), buffers.sources()[index],
                buffers.io_bytes(),
                kind, stream);
            const auto end = Clock::now();
            if (error != ACL_SUCCESS) {
                ThrowAclError("aclrtMemcpyAsync[" + std::to_string(index) + "]", error);
            }
            trace->push_back({iteration, buffers.direction(), method, Phase::kSubmit,
                              static_cast<int64_t>(index), 1U, buffers.io_bytes(),
                              SinceNanoseconds(origin, start), SinceNanoseconds(origin, end)});
        }
    } else {
        size_t fail_index = std::numeric_limits<size_t>::max();
        const auto start = Clock::now();
        const aclError error = aclrtMemcpyBatchAsync(
            buffers.destinations(), buffers.destination_max_sizes(), buffers.sources(),
            buffers.copy_sizes(), kShardCount, buffers.attribute(), buffers.attribute_index(), 1U,
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
        trace->push_back({iteration, buffers.direction(), method, Phase::kSubmit, -1, kShardCount,
                          buffers.io_bytes(), SinceNanoseconds(origin, start),
                          SinceNanoseconds(origin, end)});
    }

    const auto sync_start = Clock::now();
    Synchronize(stream, timeout_ms);
    const auto sync_end = Clock::now();
    trace->push_back({iteration, buffers.direction(), method, Phase::kSync, -1, kShardCount,
                      buffers.io_bytes(), SinceNanoseconds(origin, sync_start),
                      SinceNanoseconds(origin, sync_end)});
}

uint64_t ParseUnsigned(const std::string& text, const std::string& option)
{
    if (text.empty() || text[0] == '-') {
        throw std::invalid_argument(option + " requires a non-negative integer");
    }
    size_t parsed = 0U;
    const uint64_t value = std::stoull(text, &parsed, 10);
    if (parsed != text.size()) {
        throw std::invalid_argument("invalid value for " + option);
    }
    return value;
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
            multiplier = 1024U;
            text.pop_back();
        } else if (unit == 'M' || unit == 'm') {
            multiplier = 1024U * 1024U;
            text.pop_back();
        } else if (unit == 'G' || unit == 'g') {
            multiplier = 1024U * 1024U * 1024U;
            text.pop_back();
        }
    }
    const uint64_t value = ParseUnsigned(text, "--io-size");
    if (value == 0U || value > std::numeric_limits<size_t>::max() / multiplier) {
        throw std::invalid_argument("--io-size must be positive and fit in size_t");
    }
    return static_cast<size_t>(value * multiplier);
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

void PrintUsage(const char* program)
{
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Default scenario: " << kDefaultShardBytes << " bytes x " << kShardCount
              << " shards\n"
              << "Options:\n"
              << "  --device N            ACL device (default: 0)\n"
              << "  --direction VALUE     h2d, d2h, or both (default: h2d)\n"
              << "  --method VALUE        loop, batch, or both (default: both)\n"
              << "  --io-size SIZE        bytes per shard, e.g. 176K, 1M (default: 176K)\n"
              << "  --warmup N            unrecorded runs per method (default: 0)\n"
              << "  --rounds N            recorded rounds per method (default: " << kDefaultRounds
              << ")\n"
              << "  --timeout-ms N        stream synchronization timeout (default: "
              << kDefaultTimeoutMs << ")\n"
              << "  --csv PATH            trace output (default: shard_io_trace.csv)\n"
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
            const uint64_t device = ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
            if (device > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--device is too large");
            }
            options.device_id = static_cast<int32_t>(device);
        } else if (name == "--direction") {
            ParseDirection(TakeValue(argc, argv, &index, argument), &options);
        } else if (name == "--method") {
            options.method_mode = ParseMethodMode(TakeValue(argc, argv, &index, argument));
        } else if (name == "--io-size") {
            options.io_bytes = ParseByteSize(TakeValue(argc, argv, &index, argument));
        } else if (name == "--warmup") {
            options.warmup = static_cast<size_t>(
                ParseUnsigned(TakeValue(argc, argv, &index, argument), name));
        } else if (name == "--rounds" || name == "--iterations") {
            options.rounds = static_cast<size_t>(
                ParseUnsigned(TakeValue(argc, argv, &index, argument), name));
            if (options.rounds == 0U) {
                throw std::invalid_argument(name + " must be positive");
            }
        } else if (name == "--timeout-ms") {
            const uint64_t timeout =
                ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
            if (timeout == 0U ||
                timeout > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--timeout-ms must be positive and fit in int32");
            }
            options.timeout_ms = static_cast<int32_t>(timeout);
        } else if (name == "--csv") {
            options.csv_path = TakeValue(argc, argv, &index, argument);
            if (options.csv_path.empty()) { throw std::invalid_argument("--csv must not be empty"); }
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    return options;
}

void WriteCsv(const std::string& path, const std::vector<TraceRow>& trace)
{
    std::ofstream output(path);
    if (!output) { throw std::runtime_error("cannot open CSV for writing: " + path); }
    output << "iteration,direction,method,phase,shard_index,shard_count,io_bytes,start_ns,end_ns,"
              "duration_us\n";
    output << std::fixed << std::setprecision(3);
    for (const TraceRow& row : trace) {
        const double duration_us = static_cast<double>(row.end_ns - row.start_ns) / 1000.0;
        output << row.iteration << ',' << DirectionName(row.direction) << ','
               << MethodName(row.method) << ',' << PhaseName(row.phase) << ',' << row.shard_index
               << ',' << row.shard_count << ',' << row.io_bytes << ',' << row.start_ns << ','
               << row.end_ns << ',' << duration_us << '\n';
    }
}

void PrintConfiguration(const Options& options)
{
    std::cout << "scenario: shard_bytes=" << options.io_bytes << ", shard_count=" << kShardCount
              << ", total_bytes=" << CheckedTotalBytes(options.io_bytes) << '\n'
              << "direction=";
    for (size_t index = 0U; index < options.directions.size(); ++index) {
        if (index != 0U) { std::cout << ','; }
        std::cout << DirectionName(options.directions[index]);
    }
    std::cout << ", method=";
    if (options.method_mode == MethodMode::kBoth) {
        std::cout << "loop,batch";
    } else {
        std::cout << (options.method_mode == MethodMode::kLoop ? "loop" : "batch");
    }
    std::cout << ", warmup=" << options.warmup << ", rounds=" << options.rounds
              << "\nTiming: Host monotonic clock; submit API interval and sync API interval only\n"
              << "Trace: loop has one submit row per shard; batch has one row for one 460-shard API call\n";
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
        std::vector<TraceRow> trace;
        const size_t rows_per_iteration =
            methods.size() * (kShardCount + 1U);
        trace.reserve(options.directions.size() * options.rounds * rows_per_iteration);
        const Clock::time_point origin = Clock::now();

        for (Direction direction : options.directions) {
            ShardBuffers buffers(direction, options.device_id, options.io_bytes);
            for (Method method : methods) {
                Validate(method, buffers, stream.get(), options.timeout_ms);
            }
            for (Method method : methods) {
                for (size_t iteration = 0U; iteration < options.warmup; ++iteration) {
                    buffers.ResetDestination();
                    Submit(method, buffers, stream.get());
                    Synchronize(stream.get(), options.timeout_ms);
                }
            }
            for (size_t iteration = 0U; iteration < options.rounds; ++iteration) {
                for (Method method : methods) {
                    RunMeasured(method, iteration, buffers, stream.get(), origin,
                                options.timeout_ms, &trace);
                }
            }
        }

        WriteCsv(options.csv_path, trace);
        std::cout << "trace_rows=" << trace.size() << "\nCSV written to " << options.csv_path
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
