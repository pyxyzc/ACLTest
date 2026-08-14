#include "physical_memory_ipc_parent.h"

#include "console_utils.h"

#include <cstddef>
#include <iostream>

namespace acltest::internal {
namespace {

bool RunIpcEndpointPair(const Options& options, const char* section,
                        IpcEndpointKind left, IpcEndpointKind right,
                        uint32_t seed_base)
{
    std::cout << "\n";
    PrintRed(section);

    bool ok = RunIpcCopyDirection(options, left, right, seed_base + 1U);
    ok = RunIpcCopyDirection(options, right, left, seed_base + 2U) && ok;
    return ok;
}

struct IpcCase {
    const char* section;
    IpcEndpointKind left;
    IpcEndpointKind right;
    uint32_t seed_base;
};

bool RunIpcCases(const Options& options, const IpcCase* cases, size_t case_count)
{
    bool ok = true;
    for (size_t i = 0; i < case_count; ++i) {
        const auto& test_case = cases[i];
        ok = RunIpcEndpointPair(options, test_case.section, test_case.left,
                                test_case.right, test_case.seed_base) && ok;
    }
    return ok;
}

}  // namespace

bool RunDevicePhysicalIpcEndpointTests(const Options& options)
{
    constexpr IpcCase cases[] = {
        {"[multi-process] imported device VA <-> device buffer",
         IpcEndpointKind::ImportedDeviceVa, IpcEndpointKind::DeviceBuffer, 0x2100U},
        {"[multi-process] imported device VA <-> host buffer",
         IpcEndpointKind::ImportedDeviceVa, IpcEndpointKind::HostBuffer, 0x2200U},
        {"[multi-process] imported device VA <-> imported device VA",
         IpcEndpointKind::ImportedDeviceVa, IpcEndpointKind::ImportedDeviceVa,
         0x2300U},
    };
    return RunIpcCases(options, cases, sizeof(cases) / sizeof(cases[0]));
}

bool RunHostPhysicalIpcEndpointTests(const Options& options)
{
    constexpr IpcCase cases[] = {
        {"[multi-process] imported host VA <-> device buffer",
         IpcEndpointKind::ImportedHostVa, IpcEndpointKind::DeviceBuffer, 0x2400U},
        {"[multi-process] imported host VA <-> host buffer",
         IpcEndpointKind::ImportedHostVa, IpcEndpointKind::HostBuffer, 0x2500U},
        {"[multi-process] imported host VA <-> imported host VA",
         IpcEndpointKind::ImportedHostVa, IpcEndpointKind::ImportedHostVa, 0x2600U},
    };
    return RunIpcCases(options, cases, sizeof(cases) / sizeof(cases[0]));
}

bool RunDeviceHostPhysicalIpcEndpointTest(const Options& options)
{
    constexpr IpcCase test_case = {
        "[multi-process] imported device VA <-> imported host VA",
        IpcEndpointKind::ImportedDeviceVa, IpcEndpointKind::ImportedHostVa, 0x2700U};
    return RunIpcCases(options, &test_case, 1);
}

}  // namespace acltest::internal
