#include "vmm_ipc_parent.h"
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "console_utils.h"
#include "vmm_ipc_endpoint.h"
#include "vmm_pointer_access.h"

namespace acltest::internal {
namespace {

struct ParentHandle {
    IpcMemoryKind memory_kind = IpcMemoryKind::DevicePhysical;
    PhysicalMemoryConfig config = {};
    PhysicalMapping mapping = {};
    size_t aligned_size = 0;
};

struct IpcDirection {
    IpcEndpointKind src = IpcEndpointKind::HostBuffer;
    IpcEndpointKind dst = IpcEndpointKind::HostBuffer;
    uint32_t seed = 0;
};

bool SetCloseOnExec(int fd)
{
    const int flags = fcntl(fd, F_GETFD);
    if (flags < 0) { return false; }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

void ReportExecFailure(int fd)
{
    const int saved_errno = errno;
    const auto* data = reinterpret_cast<const uint8_t*>(&saved_errno);
    size_t remaining = sizeof(saved_errno);
    while (remaining != 0U) {
        const ssize_t written = write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) { continue; }
            break;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
}

bool CheckExecStatus(int fd, const std::string& executable)
{
    int child_errno = 0;
    auto* data = reinterpret_cast<uint8_t*>(&child_errno);
    size_t received = 0;
    while (received < sizeof(child_errno)) {
        const ssize_t count = read(fd, data + received, sizeof(child_errno) - received);
        if (count < 0) {
            if (errno == EINTR) { continue; }
            std::cerr << "  failed to check child exec status, errno=" << errno << "\n";
            return false;
        }
        if (count == 0) {
            if (received == 0) { return true; }
            std::cerr << "  child exec status was truncated\n";
            return false;
        }
        received += static_cast<size_t>(count);
    }

    std::cerr << "  child helper exec failed, path=" << executable << ", errno=" << child_errno
              << "\n";
    return false;
}

bool LaunchIpcChild(const Options& options, int parent_to_child[2], int child_to_parent[2],
                    pid_t* child_pid)
{
    if (options.ipc_child_executable.empty()) {
        std::cerr << "  child helper path is empty\n";
        return false;
    }

    int exec_status[2] = {-1, -1};
    if (pipe(exec_status) != 0) {
        std::cerr << "  exec status pipe failed, errno=" << errno << "\n";
        return false;
    }
    if (!SetCloseOnExec(exec_status[1])) {
        std::cerr << "  failed to set exec status pipe close-on-exec, errno=" << errno << "\n";
        CloseFd(&exec_status[0]);
        CloseFd(&exec_status[1]);
        return false;
    }

    const std::string read_fd = std::to_string(parent_to_child[0]);
    const std::string write_fd = std::to_string(child_to_parent[1]);
    const char* executable = options.ipc_child_executable.c_str();

    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "  fork failed, errno=" << errno << "\n";
        CloseFd(&exec_status[0]);
        CloseFd(&exec_status[1]);
        return false;
    }

    if (pid == 0) {
        CloseFd(&parent_to_child[1]);
        CloseFd(&child_to_parent[0]);
        CloseFd(&exec_status[0]);

        execl(executable, executable, "--read-fd", read_fd.c_str(), "--write-fd", write_fd.c_str(),
              nullptr);
        ReportExecFailure(exec_status[1]);
        _exit(127);
    }

    *child_pid = pid;
    CloseFd(&exec_status[1]);
    const bool exec_ok = CheckExecStatus(exec_status[0], options.ipc_child_executable);
    CloseFd(&exec_status[0]);
    CloseFd(&parent_to_child[0]);
    CloseFd(&child_to_parent[1]);
    return exec_ok;
}

bool WaitForChild(pid_t pid, bool ok)
{
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        std::cerr << "  waitpid failed, errno=" << errno << "\n";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (ok) {
            std::cerr << "  child exited abnormally, status=" << status << "\n";
        } else if (WIFSIGNALED(status)) {
            std::cerr << "  child terminated by signal=" << WTERMSIG(status) << "\n";
        } else {
            std::cerr << "  child exited with status=" << status << "\n";
        }
        return false;
    }
    return ok;
}

const char* ResultPhaseName(uint32_t phase)
{
    if (phase == static_cast<uint32_t>(IpcResultPhase::VmmSetup)) { return "VMM_SETUP"; }
    if (phase == static_cast<uint32_t>(IpcResultPhase::Capability)) { return "CAPABILITY"; }
    return "UNKNOWN";
}

class ParentIpcSession {
public:
    explicit ParentIpcSession(const Options& options) : options_(options) {}

    bool Launch()
    {
        (void)std::signal(SIGPIPE, SIG_IGN);
        if (pipe(parent_to_child_) != 0 || pipe(child_to_parent_) != 0) {
            std::cerr << "  pipe failed, errno=" << errno << "\n";
            ClosePipes();
            return false;
        }
        if (!LaunchIpcChild(options_, parent_to_child_, child_to_parent_, &child_pid_)) {
            ClosePipes();
            if (child_pid_ > 0) { (void)WaitForChild(child_pid_, false); }
            child_pid_ = -1;
            return false;
        }

        ChildPidMsg pid_msg;
        if (!ReadFull(child_to_parent_[0], &pid_msg, sizeof(pid_msg)) ||
            pid_msg.magic != kPidMagic) {
            std::cerr << "  parent failed to read child bare tgid\n";
            return false;
        }
        child_bare_tgid_ = pid_msg.bare_tgid;
        std::cout << "  child_os_pid=" << pid_msg.os_pid
                  << ", child_bare_tgid=" << pid_msg.bare_tgid << "\n";
        return runtime_.Init() && runtime_.SetDevice(options_.device);
    }

    bool AddHandle(IpcMemoryKind memory_kind, uint32_t* handle_index)
    {
        if (share_msg.handle_count >= kMaxSharedHandleCount) {
            std::cerr << "  too many imported handles in IPC test\n";
            return false;
        }

        const uint32_t index = share_msg.handle_count++;
        *handle_index = index;
        ParentHandle& handle = handles_[index];
        handle.memory_kind = memory_kind;
        handle.config = MakeConfigForKind(options_, memory_kind);

        aclError failure_ret = ACL_ERROR_RT_PARAM_INVALID;
        if (!QueryAlignedSize(handle.config, options_.requested_size, &handle.aligned_size) ||
            !AllocateAndMapPhysical(handle.config, handle.aligned_size, &handle.mapping,
                                    &failure_ret)) {
            std::cerr << "  VMM_SETUP_FAILED parent handle[" << index
                      << "] ret=" << static_cast<int32_t>(failure_ret) << "\n";
            return false;
        }

        share_msg.handle_memory_kinds[index] = static_cast<uint32_t>(memory_kind);
        share_msg.handle_aligned_sizes[index] = handle.aligned_size;
        if (share_msg.aligned_size == 0U) { share_msg.aligned_size = handle.aligned_size; }
        return true;
    }

    bool AddEndpoint(IpcEndpointKind endpoint, uint32_t* handle_index)
    {
        if (!IsImportedEndpoint(endpoint)) {
            *handle_index = kInvalidHandleIndex;
            return true;
        }
        return AddHandle(ImportedMemoryKind(endpoint), handle_index);
    }

    ParentHandle* Handle(uint32_t index)
    {
        if (index >= share_msg.handle_count || index >= kMaxSharedHandleCount) { return nullptr; }
        return &handles_[index];
    }

    bool ExportSendAndWaitReady()
    {
        for (uint32_t i = 0; i < share_msg.handle_count; ++i) {
            if (!ExportShareableHandle(options_, handles_[i].mapping.handle, child_bare_tgid_,
                                       &share_msg.handles[i])) {
                return false;
            }
        }
        if (!WriteFull(parent_to_child_[1], &share_msg, sizeof(share_msg))) {
            std::cerr << "  parent failed to send share msg\n";
            return false;
        }
        share_sent_ = true;

        SetupReadyMsg ready;
        if (!ReadFull(child_to_parent_[0], &ready, sizeof(ready)) || ready.magic != kReadyMagic) {
            std::cerr << "  parent failed to read child VMM setup status\n";
            return false;
        }
        ready_received_ = true;
        std::cout << "  child_setup ok=" << ready.ok << ", ret=" << ready.ret << ", message=\""
                  << ready.message << "\"\n";
        if (ready.ok != 1) {
            std::cerr << "  VMM_SETUP_FAILED child ret=" << ready.ret << "\n";
            return false;
        }
        return true;
    }

    bool StartCapability(bool start)
    {
        if (!ready_received_) { return false; }
        start_sent_ = true;
        if (!SendStartMessage(parent_to_child_[1], start)) {
            std::cerr << "  parent failed to send capability " << (start ? "start" : "stop")
                      << " message\n";
            return false;
        }
        return start;
    }

    bool ReadCapabilityResult()
    {
        ChildResult result;
        if (!ReadFull(child_to_parent_[0], &result, sizeof(result)) ||
            result.magic != kResultMagic) {
            std::cerr << "  parent failed to read child capability result\n";
            return false;
        }
        std::cout << "  child_result phase=" << ResultPhaseName(result.phase)
                  << ", ok=" << result.ok << ", ret=" << result.ret;
        if (result.signal_number != 0) { std::cout << ", signal=" << result.signal_number; }
        std::cout << ", message=\"" << result.message << "\"\n";
        return result.phase == static_cast<uint32_t>(IpcResultPhase::Capability) && result.ok == 1;
    }

    bool Finish(bool ok)
    {
        if (child_pid_ > 0 && !share_sent_ && parent_to_child_[1] >= 0) {
            SendStopMessage(parent_to_child_[1]);
        } else if (child_pid_ > 0 && ready_received_ && !start_sent_ && parent_to_child_[1] >= 0) {
            (void)SendStartMessage(parent_to_child_[1], false);
        }

        ClosePipes();
        for (uint32_t i = 0; i < share_msg.handle_count; ++i) { handles_[i].mapping.Cleanup(); }
        if (child_pid_ <= 0) { return false; }
        const pid_t pid = child_pid_;
        child_pid_ = -1;
        return WaitForChild(pid, ok);
    }

    ~ParentIpcSession() { ClosePipes(); }

    ShareMsg share_msg;

private:
    void ClosePipes()
    {
        CloseFd(&parent_to_child_[0]);
        CloseFd(&parent_to_child_[1]);
        CloseFd(&child_to_parent_[0]);
        CloseFd(&child_to_parent_[1]);
    }

    const Options& options_;
    AclRuntime runtime_;
    std::array<ParentHandle, kMaxSharedHandleCount> handles_;
    int parent_to_child_[2] = {-1, -1};
    int child_to_parent_[2] = {-1, -1};
    pid_t child_pid_ = -1;
    int32_t child_bare_tgid_ = -1;
    bool share_sent_ = false;
    bool ready_received_ = false;
    bool start_sent_ = false;
};

bool SetupMemcpySource(const Options& options, const IpcDirection& direction,
                       ParentIpcSession* session)
{
    if (!IsImportedEndpoint(direction.src)) { return true; }
    ParentHandle* source = session->Handle(session->share_msg.src_handle_index);
    if (source == nullptr) {
        std::cerr << "  invalid source handle index for parent memcpy setup\n";
        return false;
    }

    const auto pattern = MakePattern(options.requested_size, direction.seed);
    return CopyHostToMapping(
        source->mapping.virt, source->mapping.size, pattern, source->config,
        "parent memcpy setup(host buffer -> " + std::string(EndpointName(direction.src)) + ")");
}

bool VerifyMemcpyDestination(const Options& options, const IpcDirection& direction,
                             ParentIpcSession* session)
{
    if (!IsImportedEndpoint(direction.dst)) { return true; }
    ParentHandle* destination = session->Handle(session->share_msg.dst_handle_index);
    if (destination == nullptr) {
        std::cerr << "  invalid destination handle index for parent memcpy readback\n";
        return false;
    }

    std::vector<uint8_t> actual(options.requested_size);
    return CopyMappingToHost(&actual, destination->mapping.virt, actual.size(), destination->config,
                             "parent memcpy readback(" + std::string(EndpointName(direction.dst)) +
                                 " -> host buffer)") &&
           VerifyPattern(actual, direction.seed, DirectionName(direction.src, direction.dst));
}

}  // namespace

bool RunIpcMemcpyDirection(const Options& options, IpcEndpointKind source,
                           IpcEndpointKind destination, uint32_t seed)
{
    const IpcDirection direction{source, destination, seed};
    const std::string direction_name = DirectionName(source, destination);
    ParentIpcSession session(options);

    bool ok = session.Launch();
    session.share_msg.device = options.device;
    session.share_msg.host_numa = options.host_numa;
    session.share_msg.test_size = options.requested_size;
    session.share_msg.parent_seed = direction.seed;
    session.share_msg.test_kind = static_cast<uint32_t>(IpcTestKind::MemcpyDirection);
    session.share_msg.src_endpoint = static_cast<uint32_t>(direction.src);
    session.share_msg.dst_endpoint = static_cast<uint32_t>(direction.dst);

    if (ok) {
        ok = session.AddEndpoint(direction.src, &session.share_msg.src_handle_index) &&
             session.AddEndpoint(direction.dst, &session.share_msg.dst_handle_index) &&
             session.ExportSendAndWaitReady();
    }

    if (ok) { ok = SetupMemcpySource(options, direction, &session); }
    if (ok) {
        ok = session.StartCapability(true) && session.ReadCapabilityResult();
    } else {
        (void)session.StartCapability(false);
    }
    if (ok) { ok = VerifyMemcpyDestination(options, direction, &session); }
    if (!ok && !IsImportedEndpoint(direction.dst)) { PrintRed("  " + direction_name + " ×"); }
    return session.Finish(ok);
}

bool RunIpcHostPointer(const Options& options, uint32_t parent_seed, uint32_t child_seed)
{
    ParentIpcSession session(options);
    bool ok = session.Launch();

    session.share_msg.device = options.device;
    session.share_msg.host_numa = options.host_numa;
    session.share_msg.test_size = options.requested_size;
    session.share_msg.parent_seed = parent_seed;
    session.share_msg.child_seed = child_seed;
    session.share_msg.test_kind = static_cast<uint32_t>(IpcTestKind::HostPointer);
    session.share_msg.src_endpoint = static_cast<uint32_t>(IpcEndpointKind::ImportedHostVa);
    session.share_msg.dst_endpoint = static_cast<uint32_t>(IpcEndpointKind::ImportedHostVa);

    uint32_t handle_index = kInvalidHandleIndex;
    if (ok) {
        ok = session.AddHandle(IpcMemoryKind::HostPhysical, &handle_index);
        session.share_msg.src_handle_index = handle_index;
        session.share_msg.dst_handle_index = handle_index;
    }
    if (ok) { ok = session.ExportSendAndWaitReady(); }

    ParentHandle* handle = ok ? session.Handle(handle_index) : nullptr;
    if (ok && handle == nullptr) {
        std::cerr << "  invalid host handle for parent pointer access\n";
        ok = false;
    }
    if (ok) {
        ok = WritePointerPattern(handle->mapping.virt, options.requested_size, parent_seed,
                                 "parent pointer write")
                 .ok;
    }
    if (ok) {
        ok = session.StartCapability(true) && session.ReadCapabilityResult();
    } else {
        (void)session.StartCapability(false);
    }
    if (ok) {
        ok = VerifyPointerPattern(handle->mapping.virt, options.requested_size, child_seed,
                                  "parent pointer read")
                 .ok;
    }
    return session.Finish(ok);
}

}  // namespace acltest::internal
