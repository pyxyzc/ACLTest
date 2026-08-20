#include "vmm_test_common.h"

int main(int argc, char** argv)
{
    const acltest::TestCase tests[] = {
        {"host VMM IPC memcpy", acltest::RunVmmHostMemcpyTest},
    };
    return acltest::RunTestProgram(argc, argv, "aclrt host VMM IPC memcpy test", tests, 1);
}
