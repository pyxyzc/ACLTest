#include <array>
#include <iostream>
#include <vector>
#include "console_utils.h"
#include "physical_memory_ipc_endpoint.h"

namespace acltest::internal {

int RunIpcCopyDirectionChild(int write_fd, const ShareMsg& share_msg)
{
    if (share_msg.test_kind != static_cast<uint32_t>(IpcTestKind::CopyDirection)) {
        SendChildResult(write_fd, false, ACL_ERROR_RT_PARAM_INVALID,
                        "child expected copy-direction test");
        return 1;
    }
    if (share_msg.handle_count > kMaxSharedHandleCount) {
        SendChildResult(write_fd, false, ACL_ERROR_RT_PARAM_INVALID,
                        "child received too many shared handles");
        return 1;
    }

    const auto source_kind = EndpointKindFromWire(share_msg.src_endpoint);
    const auto destination_kind = EndpointKindFromWire(share_msg.dst_endpoint);
    const std::string direction = DirectionName(source_kind, destination_kind);

    std::array<PhysicalMapping, kMaxSharedHandleCount> mappings;
    bool ok = true;
    aclError failure_ret = ACL_ERROR_RT_PARAM_INVALID;
    for (uint32_t i = 0; i < share_msg.handle_count && ok; ++i) {
        const auto memory_kind = MemoryKindFromWire(share_msg.handle_memory_kinds[i]);
        const auto config = MakeConfigForKind(share_msg, memory_kind);
        ok = ImportAndMapSharedHandle(share_msg, i, config, &mappings[i], &failure_ret);
    }

    DeviceBuffer device_buffer;
    if (ok && EndpointUsesDeviceBuffer(source_kind, destination_kind)) {
        ok = device_buffer.Allocate(static_cast<size_t>(share_msg.test_size));
    }

    const size_t test_size = static_cast<size_t>(share_msg.test_size);
    std::vector<uint8_t> expected = MakePattern(test_size, share_msg.parent_seed);
    std::vector<uint8_t> actual(test_size, 0);

    const uint32_t source_handle_index = HandleIndexForEndpoint(share_msg, source_kind, true);
    const uint32_t destination_handle_index =
        HandleIndexForEndpoint(share_msg, destination_kind, false);
    if (ok && IsImportedEndpoint(source_kind) &&
        !ValidImportedIndex(source_handle_index, share_msg)) {
        std::cerr << "  child invalid source handle index\n";
        failure_ret = ACL_ERROR_RT_PARAM_INVALID;
        ok = false;
    }
    if (ok && IsImportedEndpoint(destination_kind) &&
        !ValidImportedIndex(destination_handle_index, share_msg)) {
        std::cerr << "  child invalid destination handle index\n";
        failure_ret = ACL_ERROR_RT_PARAM_INVALID;
        ok = false;
    }

    Endpoint source;
    Endpoint destination;
    if (ok) {
        source = ResolveChildEndpoint(source_kind, source_handle_index, &mappings, &device_buffer,
                                      &expected);
        destination = ResolveChildEndpoint(destination_kind, destination_handle_index, &mappings,
                                           &device_buffer, &actual);
    }

    if (ok && source_kind == IpcEndpointKind::DeviceBuffer) {
        const Endpoint setup_source{"host buffer", expected.data(), expected.size(),
                                    MemorySide::Host};
        ok = CopyIpcEndpoint(source, setup_source, expected.size(), "child setup");
    }
    if (ok) { ok = CopyIpcEndpoint(destination, source, expected.size(), "child copy"); }
    if (ok && destination_kind == IpcEndpointKind::DeviceBuffer) {
        const Endpoint readback_destination{"host buffer", actual.data(), actual.size(),
                                            MemorySide::Host};
        ok = CopyIpcEndpoint(readback_destination, destination, actual.size(), "child readback");
    }

    const bool verify_in_child = ok && !IsImportedEndpoint(destination_kind);
    if (verify_in_child) { ok = VerifyPattern(actual, share_msg.parent_seed, direction); }
    if (!ok && !verify_in_child) { PrintRed("  " + direction + " ×"); }

    device_buffer.Cleanup();
    for (uint32_t i = 0; i < share_msg.handle_count; ++i) { mappings[i].Cleanup(); }

    const aclError result_ret =
        ok ? ACL_SUCCESS : (failure_ret == ACL_SUCCESS ? ACL_ERROR_RT_PARAM_INVALID : failure_ret);
    SendChildResult(write_fd, ok, result_ret,
                    ok ? "child copied direction" : "child copy direction failed");
    return ok ? 0 : 1;
}

}  // namespace acltest::internal
