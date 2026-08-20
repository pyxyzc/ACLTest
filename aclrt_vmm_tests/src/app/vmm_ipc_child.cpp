#include <cerrno>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <string>
#include "vmm_ipc_internal.h"

namespace {

bool ParseFd(const char* text, int* fd)
{
    if (text == nullptr || text[0] == '\0') { return false; }
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 || value > INT_MAX) { return false; }
    *fd = static_cast<int>(value);
    return true;
}

void PrintUsage(const char* program)
{
    std::cerr << "Usage: " << program << " --read-fd N --write-fd N\n";
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 5 || std::string(argv[1]) != "--read-fd" || std::string(argv[3]) != "--write-fd") {
        PrintUsage(argv[0]);
        return 2;
    }

    int read_fd = -1;
    int write_fd = -1;
    if (!ParseFd(argv[2], &read_fd) || !ParseFd(argv[4], &write_fd)) {
        PrintUsage(argv[0]);
        return 2;
    }

    const int result = acltest::internal::RunIpcChild(read_fd, write_fd);
    acltest::internal::CloseFd(&read_fd);
    acltest::internal::CloseFd(&write_fd);
    return result;
}
