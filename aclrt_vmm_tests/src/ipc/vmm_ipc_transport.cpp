#include <cerrno>
#include <cstdio>
#include <iostream>
#include <unistd.h>
#include "vmm_ipc_internal.h"

namespace acltest::internal {
namespace {

template <typename Message>
void FillMessage(Message* result, bool ok, aclError ret, const std::string& message)
{
    result->ok = ok ? 1 : 0;
    result->ret = static_cast<int32_t>(ret);
    std::snprintf(result->message, sizeof(result->message), "%s", message.c_str());
}

}  // namespace

bool WriteFull(int fd, const void* data, size_t size)
{
    const auto* ptr = static_cast<const uint8_t*>(data);
    while (size != 0U) {
        const ssize_t written = write(fd, ptr, size);
        if (written < 0) {
            if (errno == EINTR) { continue; }
            return false;
        }
        ptr += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

bool ReadFull(int fd, void* data, size_t size)
{
    auto* ptr = static_cast<uint8_t*>(data);
    while (size != 0U) {
        const ssize_t got = read(fd, ptr, size);
        if (got < 0) {
            if (errno == EINTR) { continue; }
            return false;
        }
        if (got == 0) { return false; }
        ptr += got;
        size -= static_cast<size_t>(got);
    }
    return true;
}

void CloseFd(int* fd)
{
    if (*fd >= 0) {
        (void)close(*fd);
        *fd = -1;
    }
}

void SendSetupReady(int write_fd, bool ok, aclError ret, const std::string& message)
{
    SetupReadyMsg ready;
    FillMessage(&ready, ok, ret, message);
    (void)WriteFull(write_fd, &ready, sizeof(ready));
}

bool SendStartMessage(int fd, bool start)
{
    StartMsg msg;
    msg.magic = start ? kStartMagic : kStopMagic;
    return WriteFull(fd, &msg, sizeof(msg));
}

bool WaitForStartMessage(int fd)
{
    StartMsg msg;
    if (!ReadFull(fd, &msg, sizeof(msg))) { return false; }
    return msg.magic == kStartMagic;
}

void SendChildResult(int write_fd, bool ok, aclError ret, const std::string& message,
                     IpcResultPhase phase, int signal_number)
{
    ChildResult result;
    FillMessage(&result, ok, ret, message);
    result.phase = static_cast<uint32_t>(phase);
    result.signal_number = signal_number;
    (void)WriteFull(write_fd, &result, sizeof(result));
}

void SendStopMessage(int fd)
{
    ShareMsg msg;
    msg.magic = kStopMagic;
    if (!WriteFull(fd, &msg, sizeof(msg))) { std::cerr << "  parent failed to send stop msg\n"; }
}

}  // namespace acltest::internal
