#include "vmm_test_common.h"

int main(int argc, char** argv)
{
    const acltest::TestCase tests[] = {
        {"single-process VMM",   acltest::RunSingleProcessVmmTest},
        {"device VMM IPC",       acltest::RunVmmDeviceIpcTest    },
        {"host VMM IPC memcpy",  acltest::RunVmmHostMemcpyTest   },
        {"host VMM IPC pointer", acltest::RunVmmHostPointerTest  },
        {"device-host VMM IPC",  acltest::RunVmmDeviceHostIpcTest},
    };
    return acltest::RunTestProgram(argc, argv, "aclrt VMM test suite", tests, 5);
}
