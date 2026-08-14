# ACLTest

ACLTest 是一组独立的 Ascend Runtime/HIXL 验证工程。每个子目录都可以单独配置和构建，不共享构建产物。

当前验证工程：

- [aclrt_host_register_v2_tests](aclrt_host_register_v2_tests/README.md)：验证 `aclrtHostRegisterV2` 的重复注册和跨设备行为。
- [aclrt_physical_memory_ipc_tests](aclrt_physical_memory_ipc_tests/README.md)：验证物理内存、VMM 映射和跨进程共享接口。
- [hixl_fabric_aicpu_sdma_tests](hixl_fabric_aicpu_sdma_tests/README.md)：比较单机 D2H/H2D 的循环 `aclrtMemcpyAsync` 和 FabricMem 风格 AICPU 展开 SDMA。

HIXL FabricMem AICPU SDMA 验证的入口：

```bash
cd hixl_fabric_aicpu_sdma_tests
source "${ASCEND_HOME_PATH:-/usr/local/Ascend/cann}/set_env.sh"
bash build.sh
```
