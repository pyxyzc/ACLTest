#include <acl/acl.h>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kSkipReturnCode = 77;

struct Options {
    int device0 = 0;
    int device1 = 1;
};

std::string AclErrorHint(aclError ret)
{
    switch (ret) {
        case ACL_SUCCESS: return "ACL_SUCCESS";
        case ACL_ERROR_REPEAT_INITIALIZE: return "ACL_ERROR_REPEAT_INITIALIZE";
        case ACL_ERROR_INVALID_PARAM: return "ACL_ERROR_INVALID_PARAM";
        case ACL_ERROR_RT_PARAM_INVALID: return "ACL_ERROR_RT_PARAM_INVALID";
        case ACL_ERROR_RT_NO_DEVICE: return "ACL_ERROR_RT_NO_DEVICE";
        default: return "unknown";
    }
}

std::string FormatAclRet(aclError ret)
{
    std::ostringstream os;
    os << static_cast<int64_t>(ret) << " (" << AclErrorHint(ret) << ")";
    const char* recent = aclGetRecentErrMsg();
    if (ret != ACL_SUCCESS && recent != nullptr && recent[0] != '\0') {
        os << ", recent=\"" << recent << "\"";
    }
    return os.str();
}

bool LogAcl(const std::string& label, aclError ret)
{
    std::cout << "  " << label << " ret=" << FormatAclRet(ret) << '\n';
    return ret == ACL_SUCCESS;
}

void PrintUsage(const char* program)
{
    std::cout << "Usage: " << program << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --device0 N       Primary Ascend device id. Default: 0\n"
              << "  --device1 N       Secondary Ascend device id. Default: 1\n"
              << "  --help            Show this help.\n";
}

bool ParseDeviceId(const char* text, int* out)
{
    char* end = nullptr;
    const long value = std::strtol(text, &end, 0);
    if (end == text || *end != '\0' || value < 0 || value > std::numeric_limits<int>::max()) {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool ParseArgs(int argc, char** argv, Options* options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto require_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << name << " requires a value\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            std::exit(0);
        }
        if (arg == "--device0" || arg == "--device1") {
            const char* value = require_value(arg.c_str());
            int* device = arg == "--device0" ? &options->device0 : &options->device1;
            if (value == nullptr || !ParseDeviceId(value, device)) { return false; }
            continue;
        }
        std::cerr << "Unknown option: " << arg << '\n';
        return false;
    }
    return true;
}

class AclRuntime {
public:
    bool Init()
    {
        const aclError ret = aclInit(nullptr);
        std::cout << "aclInit ret=" << FormatAclRet(ret) << '\n';
        if (ret != ACL_SUCCESS && ret != ACL_ERROR_REPEAT_INITIALIZE) { return false; }
        initialized_here_ = ret == ACL_SUCCESS;
        return true;
    }

    ~AclRuntime()
    {
        if (initialized_here_) {
            const aclError ret = aclFinalize();
            std::cout << "aclFinalize ret=" << FormatAclRet(ret) << '\n';
        }
    }

private:
    bool initialized_here_ = false;
};

bool SetAndVerifyDevice(const std::string& label, int device, aclrtContext* context,
                        bool* set_succeeded)
{
    *set_succeeded = false;
    bool ok =
        LogAcl(label + "/aclrtSetDevice(" + std::to_string(device) + ")", aclrtSetDevice(device));
    if (!ok) { return false; }
    *set_succeeded = true;

    int32_t current_device = -1;
    const aclError get_device_ret = aclrtGetDevice(&current_device);
    ok = LogAcl(label + "/aclrtGetDevice", get_device_ret) && ok;
    std::cout << "  " << label << "/current_device=" << current_device << '\n';
    ok = current_device == device && ok;
    if (current_device != device) {
        std::cerr << "  " << label << "/aclrtGetDevice returned unexpected device\n";
    }

    *context = nullptr;
    const aclError get_context_ret = aclrtGetCurrentContext(context);
    ok = LogAcl(label + "/aclrtGetCurrentContext", get_context_ret) && ok;
    std::cout << "  " << label << "/default_context=" << *context << '\n';
    if (*context == nullptr) {
        std::cerr << "  " << label << "/default context is null\n";
        ok = false;
    }
    return ok;
}

bool ResetDevice(const std::string& label, int device)
{
    return LogAcl(label + "/aclrtResetDevice(" + std::to_string(device) + ")",
                  aclrtResetDevice(device));
}

bool RunRepeatedSetCase(int device)
{
    std::cout << "\n[case1] repeated aclrtSetDevice on one device\n";
    bool ok = true;
    int successful_sets = 0;
    aclrtContext first_context = nullptr;
    aclrtContext second_context = nullptr;

    const aclError first_set_ret = aclrtSetDevice(device);
    ok = LogAcl("case1/first aclrtSetDevice(" + std::to_string(device) + ")", first_set_ret) && ok;
    if (first_set_ret == ACL_SUCCESS) {
        ++successful_sets;
        int32_t current_device = -1;
        const aclError get_device_ret = aclrtGetDevice(&current_device);
        ok = LogAcl("case1/first aclrtGetDevice", get_device_ret) && ok;
        ok = current_device == device && ok;
        const aclError get_context_ret = aclrtGetCurrentContext(&first_context);
        ok = LogAcl("case1/first aclrtGetCurrentContext", get_context_ret) && ok;
        ok = first_context != nullptr && ok;
    }

    const aclError second_set_ret = aclrtSetDevice(device);
    ok =
        LogAcl("case1/second aclrtSetDevice(" + std::to_string(device) + ")", second_set_ret) && ok;
    if (second_set_ret == ACL_SUCCESS) {
        ++successful_sets;
        int32_t current_device = -1;
        const aclError get_device_ret = aclrtGetDevice(&current_device);
        ok = LogAcl("case1/second aclrtGetDevice", get_device_ret) && ok;
        ok = current_device == device && ok;
        const aclError get_context_ret = aclrtGetCurrentContext(&second_context);
        ok = LogAcl("case1/second aclrtGetCurrentContext", get_context_ret) && ok;
        ok = second_context != nullptr && ok;
    }

    if (first_context != nullptr && second_context != nullptr && first_context != second_context) {
        std::cerr << "  case1/repeated SetDevice produced different default contexts\n";
        ok = false;
    }
    while (successful_sets-- > 0) { ok = ResetDevice("case1/cleanup", device) && ok; }
    return ok;
}

bool RunInvalidDeviceCase(uint32_t device_count)
{
    std::cout << "\n[case2] invalid device id is rejected\n";
    if (device_count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        std::cout << "  SKIP: device count cannot be represented by int32_t\n";
        return true;
    }
    const int invalid_device = static_cast<int>(device_count);
    const aclError ret = aclrtSetDevice(invalid_device);
    std::cout << "  case2/aclrtSetDevice(" << invalid_device << ") ret=" << FormatAclRet(ret)
              << '\n';
    if (ret == ACL_SUCCESS) {
        std::cerr << "  invalid device id was unexpectedly accepted\n";
        (void)ResetDevice("case2/cleanup", invalid_device);
        return false;
    }
    return true;
}

bool RunDeviceSwitchCase(int device0, int device1, uint32_t device_count)
{
    std::cout << "\n[case3] switch between devices\n";
    if (device0 == device1 || static_cast<uint32_t>(device1) >= device_count) {
        std::cout << "  SKIP: secondary device " << device1 << " is unavailable or identical\n";
        return true;
    }

    bool ok = true;
    std::vector<int> successful_sets;
    for (const int device : {device0, device1, device0}) {
        aclrtContext context = nullptr;
        bool set_succeeded = false;
        if (SetAndVerifyDevice("case3", device, &context, &set_succeeded)) {
            // Validation succeeded; the successful SetDevice is recorded below.
        } else {
            ok = false;
        }
        if (set_succeeded) { successful_sets.push_back(device); }
    }
    for (auto it = successful_sets.rbegin(); it != successful_sets.rend(); ++it) {
        ok = ResetDevice("case3/cleanup", *it) && ok;
    }
    return ok;
}

struct ThreadResult {
    aclError set_ret = ACL_SUCCESS;
    aclError get_device_ret = ACL_SUCCESS;
    aclError get_context_ret = ACL_SUCCESS;
    aclError reset_ret = ACL_SUCCESS;
    int32_t current_device = -1;
    aclrtContext context = nullptr;
};

void RunThreadSetDevice(int device, ThreadResult* result, std::mutex* mutex,
                        std::condition_variable* cv, int* ready, bool* release)
{
    result->set_ret = aclrtSetDevice(device);
    if (result->set_ret == ACL_SUCCESS) {
        result->get_device_ret = aclrtGetDevice(&result->current_device);
        result->get_context_ret = aclrtGetCurrentContext(&result->context);
    }

    {
        std::unique_lock<std::mutex> lock(*mutex);
        ++*ready;
        cv->notify_one();
        cv->wait(lock, [&release] { return *release; });
    }
    if (result->set_ret == ACL_SUCCESS) { result->reset_ret = aclrtResetDevice(device); }
}

bool RunThreadCase(int device)
{
    std::cout << "\n[case4] two threads share the default context\n";
    std::mutex mutex;
    std::condition_variable cv;
    int ready = 0;
    bool release = false;
    ThreadResult first;
    ThreadResult second;
    std::thread first_thread(RunThreadSetDevice, device, &first, &mutex, &cv, &ready, &release);
    std::thread second_thread(RunThreadSetDevice, device, &second, &mutex, &cv, &ready, &release);

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&ready] { return ready == 2; });
        release = true;
    }
    cv.notify_all();
    first_thread.join();
    second_thread.join();

    bool ok = true;
    const std::vector<ThreadResult*> results = {&first, &second};
    for (size_t index = 0; index < results.size(); ++index) {
        const std::string label = "case4/thread" + std::to_string(index);
        const ThreadResult& result = *results[index];
        ok = LogAcl(label + "/aclrtSetDevice", result.set_ret) && ok;
        ok = LogAcl(label + "/aclrtGetDevice", result.get_device_ret) && ok;
        ok = LogAcl(label + "/aclrtGetCurrentContext", result.get_context_ret) && ok;
        ok = LogAcl(label + "/aclrtResetDevice", result.reset_ret) && ok;
        std::cout << "  " << label << "/current_device=" << result.current_device
                  << ", default_context=" << result.context << '\n';
        if (result.current_device != device || result.context == nullptr) { ok = false; }
    }
    if (first.context != second.context) {
        std::cerr << "  case4/threads did not receive the same default context\n";
        ok = false;
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!ParseArgs(argc, argv, &options)) {
        PrintUsage(argv[0]);
        return 2;
    }

    std::cout << "aclrtSetDevice probe\n";
    std::cout << "  device0=" << options.device0 << ", device1=" << options.device1 << '\n';

    AclRuntime runtime;
    if (!runtime.Init()) { return 1; }

    uint32_t device_count = 0;
    const aclError count_ret = aclrtGetDeviceCount(&device_count);
    std::cout << "aclrtGetDeviceCount ret=" << FormatAclRet(count_ret) << ", count=" << device_count
              << '\n';
    if (count_ret != ACL_SUCCESS || device_count == 0U) {
        std::cout << "SKIP: no available Ascend device\n";
        return kSkipReturnCode;
    }
    if (static_cast<uint32_t>(options.device0) >= device_count) {
        std::cerr << "device0=" << options.device0 << " is outside [0, " << device_count - 1U
                  << "]\n";
        return 2;
    }

    bool ok = true;
    ok = RunRepeatedSetCase(options.device0) && ok;
    ok = RunInvalidDeviceCase(device_count) && ok;
    ok = RunDeviceSwitchCase(options.device0, options.device1, device_count) && ok;
    ok = RunThreadCase(options.device0) && ok;

    std::cout << "\nRESULT: " << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
