#include <cstring>
#include <iostream>
#include "physical_memory_ipc_internal.h"

#ifndef ACL_RT_VMM_EXPORT_FLAG_DEFAULT
#define ACL_RT_VMM_EXPORT_FLAG_DEFAULT 0x0UL
#endif

#ifndef ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION
#define ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION 0x1UL
#endif

namespace acltest::internal {
namespace {

uint32_t ShareKindValue(const Options& options)
{
    return options.share_kind == ShareKind::Fabric ? 1U : 0U;
}

const char* BlobApiName(uint32_t api_version)
{
    if (api_version == static_cast<uint32_t>(ShareApi::V1)) { return "V1"; }
    if (api_version == static_cast<uint32_t>(ShareApi::V2)) { return "V2"; }
    return "unknown";
}

const char* BlobShareKindName(uint32_t share_type)
{
    return share_type == 0U ? "default" : "fabric";
}

uint64_t ReadU64Prefix(const uint8_t* data, size_t size)
{
    uint64_t value = 0;
    if (size >= sizeof(value)) { std::memcpy(&value, data, sizeof(value)); }
    return value;
}

void PrintSharedHandleBlob(const char* label, const SharedHandleBlob& blob)
{
    std::cout << "  " << label << " api=" << BlobApiName(blob.api_version) << "("
              << blob.api_version << ")" << ", share_kind=" << BlobShareKindName(blob.share_type)
              << "(" << blob.share_type << ")" << ", share_len=" << blob.share_len;
    if (blob.share_len >= sizeof(uint64_t)) {
        std::cout << ", u64_prefix=" << ReadU64Prefix(blob.share, blob.share_len);
    }
    std::cout << "\n";
}

const char* RuntimeShareTypeName(aclrtMemSharedHandleType share_type)
{
    if (share_type == ACL_MEM_SHARE_HANDLE_TYPE_DEFAULT) {
        return "ACL_MEM_SHARE_HANDLE_TYPE_DEFAULT";
    }
#if ACLTEST_HAS_FABRIC_SHARE_TYPE
    if (share_type == ACL_MEM_SHARE_HANDLE_TYPE_FABRIC) {
        return "ACL_MEM_SHARE_HANDLE_TYPE_FABRIC";
    }
#endif
    return "unknown";
}

bool ImportShareableHandle(const SharedHandleBlob& blob, int device, aclrtDrvMemHandle* handle,
                           aclError* failure_ret)
{
    if (failure_ret != nullptr) { *failure_ret = ACL_SUCCESS; }

    if (blob.api_version == static_cast<uint32_t>(ShareApi::V2)) {
#if ACLTEST_HAS_V2_SHARE_API
        aclrtMemSharedHandleType share_type = ACL_MEM_SHARE_HANDLE_TYPE_DEFAULT;
        void* share_ptr = nullptr;
        uint64_t default_handle = 0;
#if ACLTEST_HAS_FABRIC_SHARE_TYPE
        aclrtMemFabricHandle fabric_handle = {};
#endif

        if (blob.share_type == 0U) {
            if (blob.share_len != sizeof(default_handle)) {
                std::cerr << "  invalid V2 default handle size=" << blob.share_len << "\n";
                if (failure_ret != nullptr) { *failure_ret = ACL_ERROR_RT_PARAM_INVALID; }
                return false;
            }
            std::memcpy(&default_handle, blob.share, sizeof(default_handle));
            share_ptr = &default_handle;
        } else {
#if ACLTEST_HAS_FABRIC_SHARE_TYPE
            if (blob.share_len != sizeof(fabric_handle)) {
                std::cerr << "  invalid V2 fabric handle size=" << blob.share_len << "\n";
                if (failure_ret != nullptr) { *failure_ret = ACL_ERROR_RT_PARAM_INVALID; }
                return false;
            }
            std::memcpy(&fabric_handle, blob.share, sizeof(fabric_handle));
            share_type = ACL_MEM_SHARE_HANDLE_TYPE_FABRIC;
            share_ptr = &fabric_handle;
#else
            std::cerr << "  this CANN header does not define fabric share handles\n";
            return false;
#endif
        }

        std::cout << "  import request api=V2"
                  << ", runtime_share_type=" << RuntimeShareTypeName(share_type) << "("
                  << static_cast<int>(share_type) << ")" << ", flags=0"
                  << ", current_device=" << device;
        if (blob.share_len >= sizeof(uint64_t)) {
            std::cout << ", u64_prefix=" << ReadU64Prefix(blob.share, blob.share_len);
        }
        std::cout << "\n";
        const aclError ret = aclrtMemImportFromShareableHandleV2(share_ptr, share_type, 0, handle);
        if (failure_ret != nullptr) { *failure_ret = ret; }
        return LogAcl("aclrtMemImportFromShareableHandleV2", ret);
#else
        std::cerr << "  this CANN header does not define V2 shareable handle APIs\n";
        if (failure_ret != nullptr) { *failure_ret = ACL_ERROR_RT_FEATURE_NOT_SUPPORT; }
        return false;
#endif
    }

    uint64_t shareable_handle = 0;
    if (blob.share_len != sizeof(shareable_handle)) {
        std::cerr << "  invalid V1 handle size=" << blob.share_len << "\n";
        if (failure_ret != nullptr) { *failure_ret = ACL_ERROR_RT_PARAM_INVALID; }
        return false;
    }
    std::memcpy(&shareable_handle, blob.share, sizeof(shareable_handle));
    std::cout << "  import request api=V1" << ", device=" << device
              << ", shareable_handle=" << shareable_handle << "\n";
    const aclError ret = aclrtMemImportFromShareableHandle(shareable_handle, device, handle);
    if (failure_ret != nullptr) { *failure_ret = ret; }
    return LogAcl("aclrtMemImportFromShareableHandle(V1)", ret);
}

}  // namespace

bool ExportShareableHandle(const Options& options, aclrtDrvMemHandle handle,
                           int32_t child_bare_tgid, SharedHandleBlob* blob)
{
    blob->share_type = ShareKindValue(options);
    const uint64_t flags = options.disable_pid_validation
                               ? ACL_RT_VMM_EXPORT_FLAG_DISABLE_PID_VALIDATION
                               : ACL_RT_VMM_EXPORT_FLAG_DEFAULT;

#if ACLTEST_HAS_V2_SHARE_API
    if (!options.force_v1) {
        blob->api_version = static_cast<uint32_t>(ShareApi::V2);
        aclrtMemSharedHandleType share_type = ACL_MEM_SHARE_HANDLE_TYPE_DEFAULT;
        uint64_t default_handle = 0;
        void* share_ptr = &default_handle;
        size_t share_len = sizeof(default_handle);

#if ACLTEST_HAS_FABRIC_SHARE_TYPE
        aclrtMemFabricHandle fabric_handle = {};
        if (options.share_kind == ShareKind::Fabric) {
            share_type = ACL_MEM_SHARE_HANDLE_TYPE_FABRIC;
            share_ptr = &fabric_handle;
            share_len = sizeof(fabric_handle);
        }
#else
        if (options.share_kind == ShareKind::Fabric) {
            std::cerr << "  this CANN header does not define fabric share handles\n";
            return false;
        }
#endif

        std::cout << "  export request api=V2"
                  << ", runtime_share_type=" << RuntimeShareTypeName(share_type) << "("
                  << static_cast<int>(share_type) << ")" << ", flags=" << flags
                  << ", child_bare_tgid=" << child_bare_tgid << "\n";
        aclError ret = aclrtMemExportToShareableHandleV2(handle, flags, share_type, share_ptr);
        if (!LogAcl("aclrtMemExportToShareableHandleV2", ret)) { return false; }

        if (!options.disable_pid_validation) {
            ret = aclrtMemSetPidToShareableHandleV2(share_ptr, share_type, &child_bare_tgid, 1);
            if (!LogAcl("aclrtMemSetPidToShareableHandleV2", ret)) { return false; }
        }

        blob->share_len = static_cast<uint32_t>(share_len);
        std::memcpy(blob->share, share_ptr, share_len);
        PrintSharedHandleBlob("exported share blob", *blob);
        return true;
    }
#else
    if (!options.force_v1 && options.share_kind == ShareKind::Fabric) {
        std::cerr << "  this CANN header does not define V2 shareable handle APIs\n";
        return false;
    }
#endif

    if (options.share_kind == ShareKind::Fabric) {
        std::cerr << "  --share-type fabric requires V2 APIs\n";
        return false;
    }

    blob->api_version = static_cast<uint32_t>(ShareApi::V1);
    uint64_t shareable_handle = 0;
    std::cout << "  export request api=V1" << ", handle_type=ACL_MEM_HANDLE_TYPE_NONE"
              << ", flags=" << flags << ", child_bare_tgid=" << child_bare_tgid << "\n";
    aclError ret =
        aclrtMemExportToShareableHandle(handle, ACL_MEM_HANDLE_TYPE_NONE, flags, &shareable_handle);
    if (!LogAcl("aclrtMemExportToShareableHandle(V1)", ret)) { return false; }

    if (!options.disable_pid_validation) {
        ret = aclrtMemSetPidToShareableHandle(shareable_handle, &child_bare_tgid, 1);
        if (!LogAcl("aclrtMemSetPidToShareableHandle(V1)", ret)) { return false; }
    }

    blob->share_len = sizeof(shareable_handle);
    std::memcpy(blob->share, &shareable_handle, sizeof(shareable_handle));
    PrintSharedHandleBlob("exported share blob", *blob);
    return true;
}

bool ImportAndMapSharedHandle(const ShareMsg& share_msg, size_t index,
                              const PhysicalMemoryConfig& config, PhysicalMapping* mapping,
                              aclError* failure_ret)
{
    if (failure_ret != nullptr) { *failure_ret = ACL_ERROR_RT_PARAM_INVALID; }
    if (index >= share_msg.handle_count || index >= kMaxSharedHandleCount) {
        std::cerr << "  invalid shared handle index=" << index
                  << ", handle_count=" << share_msg.handle_count << "\n";
        return false;
    }

    const uint64_t map_size = share_msg.handle_aligned_sizes[index] != 0U
                                  ? share_msg.handle_aligned_sizes[index]
                                  : share_msg.aligned_size;
    std::cout << "  import handle[" << index << "] for " << config.name
              << ", share_msg_device=" << share_msg.device << ", aligned_size=" << map_size
              << ", test_size=" << share_msg.test_size << "\n";
    PrintSharedHandleBlob("received share blob", share_msg.handles[index]);

    aclrtDrvMemHandle imported = nullptr;
    if (!ImportShareableHandle(share_msg.handles[index], share_msg.device, &imported,
                               failure_ret)) {
        return false;
    }

    mapping->owns_handle = true;
    if (!mapping->ReserveMapAndSetAccess(imported, static_cast<size_t>(map_size),
                                         config.access_location)) {
        mapping->Cleanup();
        if (failure_ret != nullptr) { *failure_ret = ACL_ERROR_RT_PARAM_INVALID; }
        return false;
    }
    return true;
}

}  // namespace acltest::internal
