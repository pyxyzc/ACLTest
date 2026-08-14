#include <acl/acl.h>
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

// ABI values from the Runtime source being tested. They are intentionally local
// because CANN 9.1 installation headers do not expose them in every package.
constexpr int32_t kFeatureMemcpyBatchAsync = 3;
constexpr int32_t kModuleTypeSystem = 0;
constexpr int32_t kInfoTypeHdConnect = 40;
constexpr int32_t kAclDevAttrNpuArch = 601;
constexpr int64_t kA5NpuArch = 3510;
constexpr int64_t kConnectPcie = 0;
constexpr int64_t kConnectHccs = 1;
constexpr int64_t kConnectUb = 2;
constexpr size_t kProbeBytes = 4096U;

enum class HostMemoryKind { kPinned, kPageable };
enum class ConnectType { kUnknown, kPcie, kHccs, kUb };
enum class ExpectedPath {
    kA5Unconfirmed,
    kNotA5,
    kDeviceIdMappingUnknown,
    kConnectTypeUnknown,
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

struct BoolProbe {
    bool known = false;
    bool value = false;
    std::string detail;
};

struct DeviceIdProbe {
    bool known = false;
    int32_t logic_device_id = -1;
    std::string detail;
};

struct ConnectProbe {
    bool known = false;
    ConnectType type = ConnectType::kUnknown;
    std::string detail;
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

template <typename Function>
Function ResolveFunction(const char* name)
{
    (void)dlerror();
    void* symbol = dlsym(RTLD_DEFAULT, name);
    const char* error = dlerror();
    if (symbol == nullptr || error != nullptr) { return nullptr; }
    static_assert(sizeof(Function) == sizeof(symbol));
    Function function = nullptr;
    std::memcpy(&function, &symbol, sizeof(function));
    return function;
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

private:
    HostMemoryKind kind_ = HostMemoryKind::kPinned;
    void* data_ = nullptr;
};

DeviceIdProbe QueryLogicDeviceId(int32_t user_device_id)
{
    using GetLogicDeviceId = int32_t (*)(int32_t, int32_t*);
    const char* const names[] = {"aclrtGetLogicDevIdByUserDevId", "rtGetLogicDevIdByUserDevId"};
    for (const char* name : names) {
        GetLogicDeviceId function = ResolveFunction<GetLogicDeviceId>(name);
        if (function == nullptr) { continue; }
        int32_t logic_device_id = -1;
        const int32_t error = function(user_device_id, &logic_device_id);
        if (error == 0 && logic_device_id >= 0) {
            return {true, logic_device_id, std::string(name) + " succeeded"};
        }
        return {false, -1, std::string(name) + " failed, error=" + std::to_string(error)};
    }
    return {false, -1, "user-to-logic Device ID conversion symbol is unavailable"};
}

BoolProbe QueryA5(uint32_t user_device_id, const char* soc_name)
{
    using GetDeviceInfo = int32_t (*)(uint32_t, int32_t, int64_t*);
    GetDeviceInfo function = ResolveFunction<GetDeviceInfo>("aclrtGetDeviceInfo");
    if (function != nullptr) {
        int64_t npu_arch = -1;
        const int32_t error = function(user_device_id, kAclDevAttrNpuArch, &npu_arch);
        if (error == 0) {
            return {true, npu_arch == kA5NpuArch,
                    "aclrtGetDeviceInfo(NPU_ARCH)=" + std::to_string(npu_arch)};
        }
    }
    if (soc_name != nullptr && std::string(soc_name).find("950") != std::string::npos) {
        return {true, true, std::string("SoC name identifies A5: ") + soc_name};
    }
    return {false, false, "A5 cannot be confirmed by NPU_ARCH or SoC name"};
}

BoolProbe QueryDriverFeature(uint32_t logic_device_id)
{
    using HalSupportFeature = bool (*)(uint32_t, int32_t);
    HalSupportFeature function = ResolveFunction<HalSupportFeature>("halSupportFeature");
    if (function == nullptr) {
        return {true, false,
                "halSupportFeature symbol is absent; CheckIsSupportFeature returns false"};
    }
    const bool supported = function(logic_device_id, kFeatureMemcpyBatchAsync);
    return {true, supported,
            std::string("halSupportFeature(FEATURE_MEMCPY_BATCH_ASYNC=3)=") +
                (supported ? "true" : "false")};
}

ConnectType ToConnectType(int64_t value)
{
    if (value == kConnectPcie) { return ConnectType::kPcie; }
    if (value == kConnectHccs) { return ConnectType::kHccs; }
    if (value == kConnectUb) { return ConnectType::kUb; }
    return ConnectType::kUnknown;
}

ConnectProbe QueryConnectType(uint32_t logic_device_id)
{
    using HalGetDeviceInfo = int32_t (*)(uint32_t, int32_t, int32_t, int64_t*);
    HalGetDeviceInfo function = ResolveFunction<HalGetDeviceInfo>("halGetDeviceInfo");
    if (function == nullptr) {
        return {false, ConnectType::kUnknown, "halGetDeviceInfo symbol is unavailable"};
    }
    int64_t value = -1;
    const int32_t error =
        function(logic_device_id, kModuleTypeSystem, kInfoTypeHdConnect, &value);
    const ConnectType type = ToConnectType(value);
    if (error != 0 || type == ConnectType::kUnknown) {
        return {false, ConnectType::kUnknown,
                "halGetDeviceInfo(HD_CONNECT_TYPE) failed, error=" + std::to_string(error) +
                    ", value=" + std::to_string(value)};
    }
    return {true, type, "halGetDeviceInfo(HD_CONNECT_TYPE)=" + std::to_string(value)};
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

const char* PathName(ExpectedPath path)
{
    switch (path) {
        case ExpectedPath::kA5Unconfirmed:
            return "INDETERMINATE_A5_UNCONFIRMED";
        case ExpectedPath::kNotA5:
            return "NOT_A5_API_IMPL_DAVID_PATH";
        case ExpectedPath::kDeviceIdMappingUnknown:
            return "INDETERMINATE_DEVICE_ID_MAPPING";
        case ExpectedPath::kConnectTypeUnknown:
            return "INDETERMINATE_CONNECT_TYPE";
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

constexpr ExpectedPath ClassifyPath(bool a5_known, bool is_a5, bool logic_id_known,
                                    bool driver_feature_supported, bool connect_known,
                                    ConnectType connect_type, bool host_registered)
{
    if (!a5_known) { return ExpectedPath::kA5Unconfirmed; }
    if (!is_a5) { return ExpectedPath::kNotA5; }
    if (!logic_id_known) { return ExpectedPath::kDeviceIdMappingUnknown; }
    // Mirrors api_impl_david.cc:882, including the negation.
    if (driver_feature_supported) { return ExpectedPath::kDriverNotSupport; }
    if (!connect_known) { return ExpectedPath::kConnectTypeUnknown; }
    if (connect_type != ConnectType::kUb) { return ExpectedPath::kLoopMemcpyAsync; }
    if (!host_registered) { return ExpectedPath::kSyncMemcpyBatch; }
    return ExpectedPath::kUbBatchDma;
}

static_assert(ClassifyPath(true, true, true, true, true, ConnectType::kUb, true) ==
              ExpectedPath::kDriverNotSupport);
static_assert(ClassifyPath(true, true, true, false, true, ConnectType::kPcie, true) ==
              ExpectedPath::kLoopMemcpyAsync);
static_assert(ClassifyPath(true, true, true, false, true, ConnectType::kUb, false) ==
              ExpectedPath::kSyncMemcpyBatch);
static_assert(ClassifyPath(true, true, true, false, true, ConnectType::kUb, true) ==
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
        HostBuffer host_buffer(options.host_memory);
        (void)host_buffer;

        int32_t current_device = -1;
        CheckAcl("aclrtGetDevice", aclrtGetDevice(&current_device));
        if (current_device != options.device_id) {
            throw std::runtime_error("current ACL device does not match --device");
        }

        const char* soc_name = aclrtGetSocName();
        const BoolProbe a5 = QueryA5(static_cast<uint32_t>(options.device_id), soc_name);
        const DeviceIdProbe logic_id = QueryLogicDeviceId(options.device_id);

        BoolProbe driver_feature;
        ConnectProbe connect;
        if (logic_id.known) {
            driver_feature = QueryDriverFeature(static_cast<uint32_t>(logic_id.logic_device_id));
            connect = QueryConnectType(static_cast<uint32_t>(logic_id.logic_device_id));
        } else {
            driver_feature.detail = "skipped because logical Device ID is unknown";
            connect.detail = "skipped because logical Device ID is unknown";
        }

        const bool host_registered = options.host_memory == HostMemoryKind::kPinned;
        const ExpectedPath path =
            ClassifyPath(a5.known, a5.value, logic_id.known, driver_feature.value,
                         connect.known, connect.type, host_registered);

        std::cout << "MemcpyBatchAsync path precheck\n"
                  << "  user_device_id: " << options.device_id << '\n'
                  << "  logic_device_id: "
                  << (logic_id.known ? std::to_string(logic_id.logic_device_id) : "unknown") << '\n'
                  << "  device_id_detail: " << logic_id.detail << '\n'
                  << "  soc_name: " << (soc_name == nullptr ? "unknown" : soc_name) << '\n'
                  << "  a5_confirmed: "
                  << (a5.known ? (a5.value ? "yes" : "no") : "unknown") << '\n'
                  << "  a5_detail: " << a5.detail << '\n'
                  << "  driver_feature_supported: "
                  << (driver_feature.known ? (driver_feature.value ? "true" : "false") : "unknown")
                  << '\n'
                  << "  runtime_branch_gate_!feature: "
                  << (driver_feature.known ? (!driver_feature.value ? "true" : "false") : "unknown")
                  << '\n'
                  << "  driver_detail: " << driver_feature.detail << '\n'
                  << "  host_device_connect: " << ConnectTypeName(connect.type) << '\n'
                  << "  connect_detail: " << connect.detail << '\n'
                  << "  host_pointer_type: "
                  << (host_registered ? "HOST_LOCKED_OR_REGISTERED" : "HOST_UNREGISTERED")
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
