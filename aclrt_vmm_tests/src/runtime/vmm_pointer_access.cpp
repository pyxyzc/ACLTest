#include "vmm_pointer_access.h"
#include <atomic>
#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <iostream>
#include <unistd.h>
#include "console_utils.h"
#include "vmm_memory_utils.h"

namespace acltest::internal {
namespace {

thread_local sigjmp_buf g_pointer_jump;
thread_local volatile sig_atomic_t g_pointer_access_active = 0;
thread_local volatile sig_atomic_t g_pointer_signal = 0;

void PointerSignalHandler(int signal_number)
{
    if (g_pointer_access_active != 0) {
        g_pointer_signal = signal_number;
        siglongjmp(g_pointer_jump, 1);
    }
    _exit(128 + signal_number);
}

class PointerSignalScope {
public:
    bool Install()
    {
        struct sigaction action = {};
        action.sa_handler = PointerSignalHandler;
        sigemptyset(&action.sa_mask);

        if (sigaction(SIGSEGV, &action, &old_segv_) != 0) {
            std::cerr << "  install SIGSEGV handler failed, errno=" << errno << "\n";
            return false;
        }
        segv_installed_ = true;
        if (sigaction(SIGBUS, &action, &old_bus_) != 0) {
            std::cerr << "  install SIGBUS handler failed, errno=" << errno << "\n";
            Restore();
            return false;
        }
        bus_installed_ = true;
        return true;
    }

    void Restore()
    {
        if (bus_installed_) {
            (void)sigaction(SIGBUS, &old_bus_, nullptr);
            bus_installed_ = false;
        }
        if (segv_installed_) {
            (void)sigaction(SIGSEGV, &old_segv_, nullptr);
            segv_installed_ = false;
        }
    }

    ~PointerSignalScope() { Restore(); }

private:
    struct sigaction old_segv_ = {};
    struct sigaction old_bus_ = {};
    bool segv_installed_ = false;
    bool bus_installed_ = false;
};

PointerAccessResult FaultResult(const std::string& label)
{
    PointerAccessResult result;
    result.signal_number = static_cast<int>(g_pointer_signal);
    std::cerr << "  " << label << " caught signal=" << result.signal_number << "\n";
    PrintRed("  " + label + " ×");
    return result;
}

}  // namespace

PointerAccessResult WritePointerPattern(void* ptr, size_t size, uint32_t seed,
                                        const std::string& label)
{
    PointerAccessResult result;
    PointerSignalScope signal_scope;
    if (ptr == nullptr || !signal_scope.Install()) {
        PrintRed("  " + label + " ×");
        return result;
    }

    g_pointer_signal = 0;
    if (sigsetjmp(g_pointer_jump, 1) != 0) {
        g_pointer_access_active = 0;
        signal_scope.Restore();
        return FaultResult(label);
    }

    g_pointer_access_active = 1;
    auto* bytes = static_cast<volatile uint8_t*>(ptr);
    for (size_t i = 0; i < size; ++i) { bytes[i] = PatternByte(seed, i); }
    std::atomic_thread_fence(std::memory_order_seq_cst);
    g_pointer_access_active = 0;
    signal_scope.Restore();

    result.ok = true;
    PrintGreen("  " + label + " √");
    return result;
}

PointerAccessResult VerifyPointerPattern(const void* ptr, size_t size, uint32_t seed,
                                         const std::string& label)
{
    PointerAccessResult result;
    PointerSignalScope signal_scope;
    if (ptr == nullptr || !signal_scope.Install()) {
        PrintRed("  " + label + " ×");
        return result;
    }

    g_pointer_signal = 0;
    if (sigsetjmp(g_pointer_jump, 1) != 0) {
        g_pointer_access_active = 0;
        signal_scope.Restore();
        return FaultResult(label);
    }

    std::atomic_thread_fence(std::memory_order_seq_cst);
    g_pointer_access_active = 1;
    const auto* bytes = static_cast<const volatile uint8_t*>(ptr);
    for (size_t i = 0; i < size; ++i) {
        const uint8_t expected = PatternByte(seed, i);
        const uint8_t actual = bytes[i];
        if (actual != expected) {
            g_pointer_access_active = 0;
            signal_scope.Restore();
            result.mismatch_offset = i;
            result.expected = expected;
            result.actual = actual;
            std::cerr << "  " << label << " mismatch at offset=" << i
                      << ", expected=" << static_cast<int>(expected)
                      << ", actual=" << static_cast<int>(actual) << "\n";
            PrintRed("  " + label + " ×");
            return result;
        }
    }
    g_pointer_access_active = 0;
    signal_scope.Restore();

    result.ok = true;
    PrintGreen("  " + label + " √");
    return result;
}

}  // namespace acltest::internal
