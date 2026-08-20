#include "vmm_test_common.h"

int main(int argc, char** argv)
{
    const acltest::TestCase tests[] = {
        {"device VMM IPC", acltest::RunVmmDeviceIpcTest},
    };
    return acltest::RunTestProgram(argc, argv, "aclrt device VMM IPC test", tests, 1);
}
