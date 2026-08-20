#include "vmm_test_common.h"

int main(int argc, char** argv)
{
    const acltest::TestCase tests[] = {
        {"host VMM IPC", acltest::RunVmmHostIpcTest},
    };
    return acltest::RunTestProgram(argc, argv, "aclrt host VMM IPC test", tests, 1);
}
