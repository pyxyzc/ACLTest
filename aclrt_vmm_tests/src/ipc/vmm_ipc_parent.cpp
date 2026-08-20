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

bool AddImportedHandle(const Options& options, IpcEndpointKind endpoint, ShareMsg* share_msg,
                       std::array<ParentHandle, kMaxSharedHandleCount>* handles,
                       uint32_t* handle_index)
{
    if (!IsImportedEndpoint(endpoint)) {
        *handle_index = kInvalidHandleIndex;
        return true;
    }
    if (share_msg->handle_count >= kMaxSharedHandleCount) {
        std::cerr << "  too many imported endpoints in IPC direction\n";
        return false;
    }

    const uint32_t index = share_msg->handle_count++;
    *handle_index = index;

    ParentHandle& handle = (*handles)[index];
    handle.memory_kind = ImportedMemoryKind(endpoint);
    handle.config = MakeConfigForKind(options, handle.memory_kind);
    if (!QueryAlignedSize(handle.config, options.requested_size, &handle.aligned_size) ||
        !AllocateAndMapPhysical(handle.config, handle.aligned_size, &handle.mapping)) {
        return false;
    }

    share_msg->handle_memory_kinds[index] = static_cast<uint32_t>(handle.memory_kind);
    share_msg->handle_aligned_sizes[index] = handle.aligned_size;
    if (share_msg->aligned_size == 0U) { share_msg->aligned_size = handle.aligned_size; }
    return true;
}

bool SetupImportedSource(const Options& options, const IpcDirection& direction, ShareMsg* share_msg,
                         std::array<ParentHandle, kMaxSharedHandleCount>* handles)
{
    if (!IsImportedEndpoint(direction.src)) { return true; }
    if (!ValidImportedIndex(share_msg->src_handle_index, *share_msg)) {
        std::cerr << "  invalid source handle index for parent setup\n";
        return false;
    }

    ParentHandle& source = (*handles)[share_msg->src_handle_index];
    const auto pattern = MakePattern(options.requested_size, direction.seed);
    return CopyHostToMapping(source.mapping.virt, source.mapping.size, pattern, source.config,
                             "parent setup aclrtMemcpy(host buffer -> " +
                                 std::string(EndpointName(direction.src)) + ")");
}

bool ExportParentHandles(const Options& options, int32_t child_bare_tgid, ShareMsg* share_msg,
                         std::array<ParentHandle, kMaxSharedHandleCount>* handles)
{
    for (uint32_t i = 0; i < share_msg->handle_count; ++i) {
        if (!ExportShareableHandle(options, (*handles)[i].mapping.handle, child_bare_tgid,
                                   &share_msg->handles[i])) {
            return false;
        }
    }
    return true;
}

bool VerifyImportedDestination(const Options& options, const IpcDirection& direction,
                               const ShareMsg& share_msg,
                               std::array<ParentHandle, kMaxSharedHandleCount>* handles)
{
    if (!IsImportedEndpoint(direction.dst)) { return true; }
    if (!ValidImportedIndex(share_msg.dst_handle_index, share_msg)) {
        std::cerr << "  invalid destination handle index for parent readback\n";
        return false;
    }

    ParentHandle& destination = (*handles)[share_msg.dst_handle_index];
    std::vector<uint8_t> actual(options.requested_size);
    return CopyMappingToHost(&actual, destination.mapping.virt, actual.size(), destination.config,
                             "parent readback aclrtMemcpy(" +
                                 std::string(EndpointName(direction.dst)) + " -> host buffer)") &&
           VerifyPattern(actual, direction.seed, DirectionName(direction.src, direction.dst));
}

void CleanupParentHandles(std::array<ParentHandle, kMaxSharedHandleCount>* handles,
                          uint32_t handle_count)
{
    for (uint32_t i = 0; i < handle_count && i < kMaxSharedHandleCount; ++i) {
        (*handles)[i].mapping.Cleanup();
    }
}

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
        } else {
            std::cerr << "  child exited with status=" << status << "\n";
        }
        return false;
    }
    return ok;
}

}  // namespace

bool RunIpcCopyDirection(const Options& options, IpcEndpointKind source,
                         IpcEndpointKind destination, uint32_t seed)
{
    (void)std::signal(SIGPIPE, SIG_IGN);
    const IpcDirection direction{source, destination, seed};
    const std::string direction_name = DirectionName(source, destination);

    int parent_to_child[2] = {-1, -1};
    int child_to_parent[2] = {-1, -1};
    if (pipe(parent_to_child) != 0 || pipe(child_to_parent) != 0) {
        std::cerr << "  pipe failed, errno=" << errno << "\n";
        CloseFd(&parent_to_child[0]);
        CloseFd(&parent_to_child[1]);
        CloseFd(&child_to_parent[0]);
        CloseFd(&child_to_parent[1]);
        return false;
    }

    pid_t pid = -1;
    if (!LaunchIpcChild(options, parent_to_child, child_to_parent, &pid)) {
        CloseFd(&parent_to_child[0]);
        CloseFd(&parent_to_child[1]);
        CloseFd(&child_to_parent[0]);
        CloseFd(&child_to_parent[1]);
        if (pid > 0) { (void)WaitForChild(pid, false); }
        return false;
    }

    bool ok = true;
    ChildPidMsg pid_msg;
    if (!ReadFull(child_to_parent[0], &pid_msg, sizeof(pid_msg)) || pid_msg.magic != kPidMagic) {
        std::cerr << "  parent failed to read child bare tgid\n";
        ok = false;
    } else {
        std::cout << "  child_os_pid=" << pid_msg.os_pid
                  << ", child_bare_tgid=" << pid_msg.bare_tgid << "\n";
    }

    AclRuntime runtime;
    ok = ok && runtime.Init() && runtime.SetDevice(options.device);

    ShareMsg share_msg;
    share_msg.device = options.device;
    share_msg.host_numa = options.host_numa;
    share_msg.test_size = options.requested_size;
    share_msg.parent_seed = direction.seed;
    share_msg.test_kind = static_cast<uint32_t>(IpcTestKind::CopyDirection);
    share_msg.src_endpoint = static_cast<uint32_t>(direction.src);
    share_msg.dst_endpoint = static_cast<uint32_t>(direction.dst);

    std::array<ParentHandle, kMaxSharedHandleCount> handles;
    if (ok) {
        ok = AddImportedHandle(options, direction.src, &share_msg, &handles,
                               &share_msg.src_handle_index) &&
             AddImportedHandle(options, direction.dst, &share_msg, &handles,
                               &share_msg.dst_handle_index) &&
             SetupImportedSource(options, direction, &share_msg, &handles) &&
             ExportParentHandles(options, pid_msg.bare_tgid, &share_msg, &handles);
    }

    if (ok) {
        ok = WriteFull(parent_to_child[1], &share_msg, sizeof(share_msg));
        if (!ok) { std::cerr << "  parent failed to send copy-direction share msg\n"; }
    } else if (parent_to_child[1] >= 0) {
        SendStopMessage(parent_to_child[1]);
    }
    CloseFd(&parent_to_child[1]);

    ChildResult result;
    if (ReadFull(child_to_parent[0], &result, sizeof(result)) && result.magic == kResultMagic) {
        std::cout << "  child_result ok=" << result.ok << ", ret=" << result.ret << ", message=\""
                  << result.message << "\"\n";
        ok = ok && result.ok == 1;
    } else {
        std::cerr << "  parent failed to read child result\n";
        ok = false;
    }
    CloseFd(&child_to_parent[0]);

    ok = ok && VerifyImportedDestination(options, direction, share_msg, &handles);
    if (!ok && !IsImportedEndpoint(direction.dst)) { PrintRed("  " + direction_name + " ×"); }

    CleanupParentHandles(&handles, share_msg.handle_count);
    return WaitForChild(pid, ok);
}

}  // namespace acltest::internal
