# hixl_ops_tests：官方 HIXL ops 小 IO 批量下发测试

这个工程只比较两条单机 H2D/D2H 路径：

1. baseline：Host 循环调用 `aclrtMemcpyAsync`，最后同步一次 stream；
2. HIXL ops：Host 把小 IO 组织成描述符批次，直接 launch CANN 已安装的
   `HixlFabricMemBatchRead` 或 `HixlFabricMemBatchWrite`，由 AICPU 向 worker RTSQ 批量写入
   SDMA SQE。

代码只构建 Host 可执行文件，不编译、不安装自定义 AICPU 包，也不链接
`libcann_hixl.so`。设备侧直接复用当前 CANN 中的
`libcann_hixl_kernel.json` 和 `libcann_hixl_kernel.so`。

## 运行关系

- D2H 使用 `HixlFabricMemBatchRead`：Device 地址是源，Host VMM 地址是目的；
- H2D 使用 `HixlFabricMemBatchWrite`：Host VMM 地址是源，Device 地址是目的；
- 初始化和退出使用 `HixlSyncTransferContext` 注册、删除官方 kernel 所需的 context；
- 每个 kernel 最多带 128 个描述符；累计到 1920 个 RTSQ task 或本次传输结束时，追加
  NotifyRecord，并让 control stream 等待 notify；
- 每个 case 都和 `aclrtMemcpyAsync` 循环做全量逐字节正确性对比。

Host 参数布局按 `/root/hixl` 提交
`39a7e36201ac240e61f8a799b704f4e6732ffdd3` 的 FabricMem AICPU ABI 固定，并由
`hixl_ops_abi_test` 检查关键 size、offset 和枚举值。若安装的 CANN 包更旧，JSON 中缺少
上述三个函数，程序会在启动时明确报出缺少的函数名。

## 构建

需要已安装的 CANN Toolkit。执行：

```bash
cd /root/ACLTest/hixl_ops_tests
bash build.sh
```

首次配置会按现有 ACLTest 工程的方式获取 `cann-cmake`。离线环境可以将它放到
`third_party/cann-cmake`，或传 `--cann-3rd-lib-path PATH`。

构建结果只有 Host 程序：`build_out/bin/hixl_ops_bench`。工程不会修改 CANN 安装目录。

## 先跑一个简单 smoke

```bash
build_out/bin/hixl_ops_bench \
  --device 0 --direction both --sizes 512B,4K --counts 1,128 \
  --warmup 1 --iterations 3 --timeout-ms 60000 --csv smoke.csv
```

确认 smoke 的 H2D、D2H 都通过后，再跑默认扫描：

```bash
build_out/bin/hixl_ops_bench --csv results.csv
```

默认扫描 `512B` 到 `2M`，每组 128 个 IO，并测试 H2D 和 D2H。可用参数：

```text
--device N
--direction d2h|h2d|both
--sizes 512B,4K,1M
--counts 1,32,128
--warmup N
--iterations auto|N
--target-bytes 256M
--timeout-ms 60000
--csv PATH
--kernel-json PATH
```

程序优先使用 `--kernel-json`，其次使用 `ACLTEST_HIXL_KERNEL_JSON`，再从
`ASCEND_HOME_PATH` 和 `/usr/local/Ascend` 查找：

`opp/built-in/op_impl/aicpu/config/libcann_hixl_kernel.json`

## A5 内存路径

为保持和现有 benchmark 一致，Host/Device 都使用 VMM 地址。A5 Host 物理内存使用
`ACL_MEM_P2P_HUGE`（2 MiB 粒度），Host 和 Device 的 VA 仍按完整 1 GiB slot 预留和映射。
启动日志会打印逻辑大小和两侧实际映射大小。

## 结果怎么看

- `memcpy_*`：循环 `aclrtMemcpyAsync` 的结果；
- `hixl_ops_*`：官方 AICPU ops 的结果；
- `submit`：描述符构造/分配/上传到任务入流完成；
- `e2e`：从 submit 起点到 stream synchronize 完成；
- `speedup = memcpy / hixl_ops`，大于 1 表示 HIXL ops 更快；
- `verify=PASS` 表示两种路径的逐字节校验都通过。

当前官方 kernel 每次 launch 会输出 INFO 日志。功能验证不受影响；做性能数据时应统一设备
日志级别，否则大量小 IO case 的日志开销可能干扰结果。
