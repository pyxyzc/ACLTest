# ACLTest

ACLTest 是一组独立的 Ascend Runtime/HIXL 验证工程。每个子目录都可以单独配置和构建，不共享构建产物。

当前验证工程：

- [aclrt_host_register_v2_tests](aclrt_host_register_v2_tests/README.md)：验证 `aclrtHostRegisterV2` 的重复注册和跨设备行为。
- [aclrt_vmm_tests](aclrt_vmm_tests/README.md)：验证物理内存、VMM 映射和跨进程共享接口。
- [aclrt_memcpy_batch_async_benchmark](aclrt_memcpy_batch_async_benchmark/README.md)：比较 `aclrtMemcpyBatchAsync` 与循环 `aclrtMemcpyAsync` 的 H2D/D2H 提交、执行和端到端性能。
- [hixl_fabric_aicpu_sdma_tests](hixl_fabric_aicpu_sdma_tests/README.md)：比较单机 D2H/H2D 的循环 `aclrtMemcpyAsync` 和 FabricMem 风格 AICPU 展开 SDMA。
- [hixl_malloc_aicpu_sdma_tests](hixl_malloc_aicpu_sdma_tests/README.md)：使用普通 `aclrtMallocHost`/`aclrtMalloc` 缓冲区，隔离验证单机 D2H/H2D 的 AICPU 展开 SDMA。

HIXL FabricMem AICPU SDMA 验证的入口：

```bash
cd hixl_fabric_aicpu_sdma_tests
bash build.sh
```

`build.sh` 会优先使用 `ASCEND_HOME_PATH`，未设置时自动发现 `/usr/local/Ascend`
下的 CANN 安装；也可以在执行前手动 source 对应版本的 `set_env.sh`。
