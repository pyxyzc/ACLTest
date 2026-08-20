#include <iostream>
#include <unistd.h>
#include "vmm_ipc_internal.h"

namespace acltest::internal {

int RunIpcChild(int read_fd, int write_fd)
{
    std::cout << "\n[child-helper] started, os_pid=" << getpid() << "\n";

    AclRuntime runtime;
    aclError init_ret = ACL_ERROR_RT_PARAM_INVALID;
    if (!runtime.Init(&init_ret)) {
        SendChildResult(write_fd, false, init_ret, "child aclInit failed");
        return 1;
    }

    int32_t bare_tgid = -1;
    aclError ret = aclrtDeviceGetBareTgid(&bare_tgid);
    if (!LogAcl("aclrtDeviceGetBareTgid(child)", ret)) {
        SendChildResult(write_fd, false, ret, "child aclrtDeviceGetBareTgid failed");
        return 1;
    }

    ChildPidMsg pid_msg;
    pid_msg.bare_tgid = bare_tgid;
    pid_msg.os_pid = static_cast<int32_t>(getpid());
    if (!WriteFull(write_fd, &pid_msg, sizeof(pid_msg))) {
        std::cerr << "[child-helper] failed to send bare tgid\n";
        return 1;
    }

    ShareMsg share_msg;
    if (!ReadFull(read_fd, &share_msg, sizeof(share_msg))) {
        SendChildResult(write_fd, false, ACL_ERROR_RT_PARAM_INVALID,
                        "child failed to read share msg");
        return 1;
    }
    if (share_msg.magic == kStopMagic) {
        SendChildResult(write_fd, false, ACL_ERROR_RT_PARAM_INVALID,
                        "parent setup failed before sharing handle");
        return 1;
    }
    if (share_msg.magic != kShareMagic) {
        SendChildResult(write_fd, false, ACL_ERROR_RT_PARAM_INVALID,
                        "child received invalid share msg");
        return 1;
    }

    aclError set_device_ret = ACL_ERROR_RT_PARAM_INVALID;
    if (!runtime.SetDevice(share_msg.device, &set_device_ret)) {
        SendChildResult(write_fd, false, set_device_ret, "child set device failed");
        return 1;
    }

    if (share_msg.test_kind == static_cast<uint32_t>(IpcTestKind::CopyDirection)) {
        return RunIpcCopyDirectionChild(write_fd, share_msg);
    }

    SendChildResult(write_fd, false, ACL_ERROR_RT_PARAM_INVALID,
                    "child received unsupported IPC test kind");
    return 1;
}

}  // namespace acltest::internal
