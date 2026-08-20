#include "vmm_test_common.h"

int main(int argc, char** argv)
{
    const acltest::TestCase tests[] = {
        {"host VMM IPC pointer", acltest::RunVmmHostPointerTest},
    };
    return acltest::RunTestProgram(argc, argv, "aclrt host VMM IPC pointer test", tests, 1);
}
