#include <acl/acl.h>
#include <ascend_hal_base.h>
#include <dlfcn.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

// Keep these values aligned with the target Runtime/driver source. The static
// assertion makes an enum ABI change a build failure instead of a wrong probe.
static_assert(static_cast<int32_t>(FEATURE_MEMCPY_BATCH_ASYNC) == 3);
constexpr int64_t kA5NpuArch = 3510;
constexpr size_t kProbeBytes = 4096U;

enum class HostMemoryKind { kPinned, kPageable };
enum class ConnectType { kUnknown, kPcie, kHccs, kUb };
enum class ExpectedPath {
    kNotA5,
    kDriverNotSupport,
    kLoopMemcpyAsync,
    kSyncMemcpyBatch,
    kUbBatchDma,
};

struct Options {
    int32_t device_id = 0;
    HostMemoryKind host_memory = HostMemoryKind::kPinned;
    bool require_ub = false;
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

class HostBuffer {
public:
    explicit HostBuffer(HostMemoryKind kind) : kind_(kind)
    {
        if (kind_ == HostMemoryKind::kPinned) {
            CheckAcl("aclrtMallocHost", aclrtMallocHost(&data_, kProbeBytes));
        } else {
            data_ = std::malloc(kProbeBytes);
            if (data_ == nullptr) { throw std::bad_alloc(); }
        }
    }

    ~HostBuffer()
    {
        if (data_ == nullptr) { return; }
        if (kind_ == HostMemoryKind::kPinned) {
            const aclError error = aclrtFreeHost(data_);
            if (error != ACL_SUCCESS) {
                std::cerr << "warning: aclrtFreeHost failed, aclError=" << error << '\n';
            }
        } else {
            std::free(data_);
        }
    }

    HostBuffer(const HostBuffer&) = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;
    void* get() const { return data_; }

private:
    HostMemoryKind kind_ = HostMemoryKind::kPinned;
    void* data_ = nullptr;
};

struct DriverFeatureResult {
    bool symbol_available = false;
    bool feature_supported = false;
    std::string detail;
};

DriverFeatureResult QueryDriverFeature(uint32_t logic_device_id)
{
    DriverFeatureResult result;
    (void)dlerror();
    void* symbol = dlsym(RTLD_DEFAULT, "halSupportFeature");
    const char* load_error = dlerror();
    if (symbol == nullptr || load_error != nullptr) {
        result.detail =
            "halSupportFeature symbol is absent; NpuDriver::CheckIsSupportFeature returns false";
        return result;
    }

    // Use the exact declaration from ascend_hal_base.h, but resolve it from the
    // process at runtime so the executable does not add a HAL NEEDED entry.
    using HalSupportFeature = decltype(&halSupportFeature);
    static_assert(sizeof(HalSupportFeature) == sizeof(symbol));
    HalSupportFeature function = nullptr;
    std::memcpy(&function, &symbol, sizeof(function));
    result.symbol_available = true;
    result.feature_supported = function(logic_device_id, FEATURE_MEMCPY_BATCH_ASYNC);
    result.detail = "halSupportFeature(logicDeviceId, FEATURE_MEMCPY_BATCH_ASYNC=3) returned ";
    result.detail += result.feature_supported ? "true" : "false";
    return result;
}

const char* ConnectTypeName(ConnectType type)
{
    switch (type) {
        case ConnectType::kPcie:
            return "PCIe";
        case ConnectType::kHccs:
            return "HCCS";
        case ConnectType::kUb:
            return "UB";
        case ConnectType::kUnknown:
            return "unknown";
    }
    return "unknown";
}

ConnectType ToConnectType(int64_t value)
{
    switch (value) {
        case ACL_HOST_DEVICE_CONNECT_TYPE_PCIE:
            return ConnectType::kPcie;
        case ACL_HOST_DEVICE_CONNECT_TYPE_HCCS:
            return ConnectType::kHccs;
        case ACL_HOST_DEVICE_CONNECT_TYPE_UB:
            return ConnectType::kUb;
        default:
            return ConnectType::kUnknown;
    }
}

const char* MemoryTypeName(aclrtMemLocationType type)
{
    switch (type) {
        case ACL_MEM_LOCATION_TYPE_HOST:
            return "HOST_LOCKED_OR_REGISTERED";
        case ACL_MEM_LOCATION_TYPE_DEVICE:
            return "DEVICE";
        case ACL_MEM_LOCATION_TYPE_UNREGISTERED:
            return "HOST_UNREGISTERED";
        case ACL_MEM_LOCATION_TYPE_MANAGED:
            return "MANAGED";
        case ACL_MEM_LOCATION_TYPE_HOST_NUMA:
            return "HOST_NUMA";
    }
    return "UNKNOWN";
}

const char* PathName(ExpectedPath path)
{
    switch (path) {
        case ExpectedPath::kNotA5:
            return "NOT_A5_API_IMPL_DAVID_PATH";
        case ExpectedPath::kDriverNotSupport:
            return "RT_ERROR_DRV_NOT_SUPPORT_EXPECTED";
        case ExpectedPath::kLoopMemcpyAsync:
            return "LOOP_MEMCPY_ASYNC";
        case ExpectedPath::kSyncMemcpyBatch:
            return "SYNC_MEMCPY_BATCH";
        case ExpectedPath::kUbBatchDma:
            return "UB_BATCH_DMA";
    }
    return "UNKNOWN";
}

constexpr ExpectedPath ClassifyPath(bool is_a5, bool driver_feature_supported,
                                    ConnectType connect_type,
                                    aclrtMemLocationType host_memory_type)
{
    if (!is_a5) { return ExpectedPath::kNotA5; }
    // This deliberately mirrors api_impl_david.cc:882, including the '!'.
    if (driver_feature_supported) { return ExpectedPath::kDriverNotSupport; }
    if (connect_type != ConnectType::kUb) { return ExpectedPath::kLoopMemcpyAsync; }
    if (host_memory_type == ACL_MEM_LOCATION_TYPE_UNREGISTERED) {
        return ExpectedPath::kSyncMemcpyBatch;
    }
    return ExpectedPath::kUbBatchDma;
}

static_assert(ClassifyPath(false, false, ConnectType::kUb, ACL_MEM_LOCATION_TYPE_HOST) ==
              ExpectedPath::kNotA5);
static_assert(ClassifyPath(true, true, ConnectType::kUb, ACL_MEM_LOCATION_TYPE_HOST) ==
              ExpectedPath::kDriverNotSupport);
static_assert(ClassifyPath(true, false, ConnectType::kPcie, ACL_MEM_LOCATION_TYPE_HOST) ==
              ExpectedPath::kLoopMemcpyAsync);
static_assert(ClassifyPath(true, false, ConnectType::kUb,
                           ACL_MEM_LOCATION_TYPE_UNREGISTERED) ==
              ExpectedPath::kSyncMemcpyBatch);
static_assert(ClassifyPath(true, false, ConnectType::kUb, ACL_MEM_LOCATION_TYPE_HOST) ==
              ExpectedPath::kUbBatchDma);

uint64_t ParseUnsigned(const std::string& value, const std::string& name)
{
    size_t parsed = 0U;
    uint64_t result = 0U;
    try {
        result = std::stoull(value, &parsed, 10);
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid value for " + name + ": " + value);
    }
    if (parsed != value.size()) {
        throw std::invalid_argument("invalid value for " + name + ": " + value);
    }
    return result;
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
              << "  --device N                       ACL user device ID (default: 0)\n"
              << "  --host-memory pinned|pageable    Host pointer type (default: pinned)\n"
              << "  --require-ub                     fail unless verdict is UB_BATCH_DMA\n"
              << "  -h, --help\n";
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
        }
        if (name == "--device") {
            const uint64_t value = ParseUnsigned(TakeValue(argc, argv, &index, argument), name);
            if (value > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                throw std::invalid_argument("--device is too large");
            }
            options.device_id = static_cast<int32_t>(value);
        } else if (name == "--host-memory") {
            const std::string value = TakeValue(argc, argv, &index, argument);
            if (value == "pinned") {
                options.host_memory = HostMemoryKind::kPinned;
            } else if (value == "pageable") {
                options.host_memory = HostMemoryKind::kPageable;
            } else {
                throw std::invalid_argument("--host-memory must be pinned or pageable");
            }
        } else if (name == "--require-ub") {
            if (argument != name) {
                throw std::invalid_argument("--require-ub does not take a value");
            }
            options.require_ub = true;
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    return options;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = ParseOptions(argc, argv);
        RuntimeSession runtime(options.device_id);

        int32_t current_device = -1;
        CheckAcl("aclrtGetDevice", aclrtGetDevice(&current_device));
        if (current_device != options.device_id) {
            throw std::runtime_error("current ACL device does not match --device");
        }

        int32_t logic_device_id = -1;
        CheckAcl("aclrtGetLogicDevIdByUserDevId",
                 aclrtGetLogicDevIdByUserDevId(options.device_id, &logic_device_id));

        int64_t npu_arch = -1;
        CheckAcl("aclrtGetDeviceInfo(NPU_ARCH)",
                 aclrtGetDeviceInfo(static_cast<uint32_t>(options.device_id),
                                    ACL_DEV_ATTR_NPU_ARCH, &npu_arch));
        const bool is_a5 = npu_arch == kA5NpuArch;

        int64_t connect_value = -1;
        ConnectType connect_type = ConnectType::kUnknown;
        if (is_a5) {
            CheckAcl("aclrtGetDeviceInfo(HD_CONNECT_TYPE)",
                     aclrtGetDeviceInfo(static_cast<uint32_t>(options.device_id),
                                        ACL_DEV_ATTR_HD_CONNECT_TYPE, &connect_value));
            connect_type = ToConnectType(connect_value);
        }

        const DriverFeatureResult driver =
            QueryDriverFeature(static_cast<uint32_t>(logic_device_id));
        HostBuffer host_buffer(options.host_memory);
        aclrtPtrAttributes host_attributes{};
        CheckAcl("aclrtPointerGetAttributes(host)",
                 aclrtPointerGetAttributes(host_buffer.get(), &host_attributes));

        const ExpectedPath path = ClassifyPath(is_a5, driver.feature_supported, connect_type,
                                               host_attributes.location.type);
        const char* soc_name = aclrtGetSocName();
        std::cout << "MemcpyBatchAsync path precheck\n"
                  << "  user_device_id: " << options.device_id << '\n'
                  << "  logic_device_id: " << logic_device_id << '\n'
                  << "  soc_name: " << (soc_name == nullptr ? "unknown" : soc_name) << '\n'
                  << "  npu_arch: " << npu_arch << " (A5 expected: " << kA5NpuArch << ")\n"
                  << "  api_impl_david_a5: " << (is_a5 ? "yes" : "no") << '\n'
                  << "  driver_symbol_available: " << (driver.symbol_available ? "yes" : "no")
                  << '\n'
                  << "  driver_feature_supported: "
                  << (driver.feature_supported ? "true" : "false") << '\n'
                  << "  runtime_branch_gate_!feature: "
                  << (!driver.feature_supported ? "true" : "false") << '\n'
                  << "  driver_detail: " << driver.detail << '\n'
                  << "  host_device_connect: " << ConnectTypeName(connect_type) << '\n'
                  << "  host_pointer_type: " << MemoryTypeName(host_attributes.location.type)
                  << '\n'
                  << "VERDICT=" << PathName(path) << '\n';

        if (options.require_ub && path != ExpectedPath::kUbBatchDma) {
            std::cerr << "ERROR: --require-ub failed; VERDICT=" << PathName(path) << '\n';
            return 2;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
