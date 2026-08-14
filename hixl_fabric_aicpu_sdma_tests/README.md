# hixl_fabric_aicpu_sdma_tests：单机小包 D2H/H2D AICPU 展开对比

这个工程比较两条使用同一组 VMM Host/HBM 缓冲区的单机拷贝路径：

1. baseline：循环调用 `aclrtMemcpyAsync`，最后只同步一次 stream；
2. AICPU batch：Host 一次上传最多 128 个描述符并 launch 一个 AICPU kernel，AICPU 直接向独立 worker RTSQ 写入 SDMA SQE，尾部写入 NotifyRecord，Host control stream 等待 notify。

工程不链接 `libcann_hixl.so`，AICPU 函数名、JSON 和归档名也都使用 `AclTest` 前缀，不会覆盖 HIXL 的设备包。

## 来源与边界

核心逻辑从 HIXL FabricMem 在提交 `39a7e36` 上的实现裁剪而来，主要对应：

- `src/hixl/fabric_mem/fabric_mem_aicpu_dispatcher.cc`
- `src/hixl/fabric_mem/fabric_mem_allocator.cc`
- `src/hixl/fabric_mem/virtual_memory_manager.cc`
- `src/ops/hixl_kernel/fabric_mem_aicpu_kernel.cc`
- `src/ops/hixl_kernel/fabric_mem_stars_sdma.cc`
- `src/ops/hixl_kernel/transfer_context_manager.cc`

保留的关键行为包括：

- Host VMM 映射使用 pinned P2P Host 物理内存，并授予 Device READWRITE 权限；
- Host 和 Device 物理内存都按完整的 1 GiB VMM 槽申请和映射；
- 调用 `aclrtMapMem`/`aclrtMemSetAccess` 前显式校验预留 VA 的 2 MiB 对齐；
- 每个传输描述符对应一个 SDMA SQE，大于 `UINT32_MAX` 的长度先在 Host 拆分；
- 每次 AICPU launch 最多处理 128 个描述符；
- 最多累计 1920 个 RTSQ task 后插入 NotifyRecord 并由 control stream 等待；
- A3 SDMA SQE type=11、kernel credit=240、QoS=6、on-chip link；
- AICPU 查询 worker SQ base/depth/head/tail，写 ring 后以 release fence 提交 tail；
- 轮询 logic CQ 检查异常 CQE；即使 SDMA/CQ 路径失败，也尽量发出尾部 notify；
- TransferContext 在 kernel 使用描述符期间加锁，异常清理先停 stream、删除 context，再释放请求 buffer。

这是针对 A3 的验证程序，不是通用 memcpy 替代实现。设备代码直接使用 HAL RTSQ 接口，因此内核配置为 built-in `AICPUKernel`，仅支持以下 A3 SoC 名称：`Ascend910_9391`、`Ascend910_9381`、`Ascend910_9392`、`Ascend910_9382`、`Ascend910_9372`、`Ascend910_9362`。

即使默认 case 的最大有效数据量只有 256 MiB，启动日志中的 `logical_bytes`、
`host_mapping_bytes` 和 `device_mapping_bytes` 也应全部为 `1073741824`。映射 VA
必须来自 `aclrtReserveMemAddress*`，不能替换成普通 `malloc` 或
`aclrtMallocHost` 返回的地址。

## 构建

需要完整的 CANN Toolkit/AICPU toolchain，并先加载环境：

```bash
cd /root/ACLTest/hixl_fabric_aicpu_sdma_tests
bash build.sh
```

`build.sh` 会优先使用 `ASCEND_HOME_PATH`，未设置时自动从 `/usr/local/Ascend`
下发现 CANN 安装目录，不依赖具体 CANN 版本号。也可以在执行前手动 source
对应安装目录中的 `set_env.sh`。

首次配置会按 HIXL 当前构建方式获取 `cann-cmake` 的 `master-044`。离线环境可以把仓库放到 `third_party/cann-cmake`，或者传入：

```bash
bash build.sh --cann-3rd-lib-path /path/to/third_party
```

构建会运行不依赖 NPU 的参数/拆分/统计单元测试，并把结果放在 `build_out/`。它不会自动修改 CANN 安装目录。

## 安装独立 AICPU 包

运行 benchmark 前，需要把两个唯一命名的设备包文件安装到当前 CANN：

```bash
bash scripts/install_kernel.sh
```

如果需要显式指定 CANN 根目录：

```bash
bash scripts/install_kernel.sh --ascend-home /path/to/cann
```

已有同名 AclTest 文件时脚本默认拒绝覆盖；确认要替换时显式加 `--force`。卸载只删除这两个
AclTest 文件及本工程拥有的包登记块：

```bash
bash scripts/uninstall_kernel.sh
```

安装脚本还会在当前 CANN 的 `conf/ascend_package_load.ini` 中登记
`acltest-sdma-compat.tar.gz`。这一步用于让 TSD 在业务进程启动时把归档发送并解压到
Device；如果只复制 JSON 和 tar 而不登记，Host 可以下发 kernel，但 Device 侧会因找不到
`libacltest_sdma_kernel.so` 报 `errcode:11002, msg:open so failed`。登记内容使用
`ACLTEST_FABRIC_AICPU_SDMA` 专用标记，重复安装不会重复追加，卸载和清理只移除该标记块，
不会修改 CANN 自带的 HIXL/Fabric 包配置。安装后应重新启动 benchmark 进程。

清理本工程的 `build/`、`build_out/`、已安装的两个 AclTest 文件和包登记块：

```bash
bash scripts/clear.sh
```

脚本不会删除结果 CSV，也不会触碰 HIXL 或其他 CANN 文件；未自动发现 CANN 时可传入
`--ascend-home PATH`。

## 运行

VMM 物理内存的申请粒度由 `aclrtMemGetAllocationGranularity` 按 Host/Device
物理属性分别查询。程序会将申请长度向上对齐，并使用同一个对齐后的长度调用
`aclrtMallocPhysical`、`aclrtMapMem` 和 `aclrtMemSetAccess`；描述符中的 IO size
仍然是命令行指定的逻辑长度。这样可以避免 `ACL_MEM_P2P_HUGE1G` 等大页属性在
小 IO benchmark 中出现 `aclrtMapMem(...)=507899`。启动时会打印逻辑长度和两侧
实际映射长度，便于确认运行时采用的粒度。

默认扫描 D2H 和 H2D，IO size 为 `512B,1K,2K,4K,32K,64K,256K,512K,1M,2M`，固定 IO count 为 128：

```bash
build_out/bin/acl_copy_bench --csv results.csv
```

每个 case 的 measured iterations 自动按累计约 256 MiB 计算，并限制在 5 到 500 次。可用选项：

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

快速真机 smoke：

```bash
build_out/bin/acl_copy_bench \
  --device 0 --direction both --sizes 512B,4K --counts 1,128 \
  --warmup 1 --iterations 3 --csv smoke.csv
```

若 JSON 安装在非默认位置，可传 `--kernel-json`，或设置 `ACLTEST_KERNEL_JSON`。归档仍需安装到同一套 CANN 的 `opp/built-in/op_impl/aicpu/kernel`。

## 测量口径

- submit：从方法调用前开始。baseline 包含 N 次 `aclrtMemcpyAsync` 入流；AICPU 包含描述符构造、device descriptor/status/args 分配和上传、AICPU kernel launch、notify wait 入流。
- e2e：同一个起点，截止到该方法的 stream synchronize 返回。
- 状态回读、request buffer 释放、数据准备和逐字节正确性校验都不计时。
- 每个 measured iteration 都执行两种方法，并逐次交换先后顺序，降低固定顺序偏差。
- GiB/s 使用 e2e p50；speedup 定义为 `memcpy-loop / AICPU-batch`，大于 1 表示 AICPU 更快。

程序先对每个 case 的两种方法分别做全量逐字节校验。D2H 直接检查 Host VMM buffer；H2D 使用一次不计时的 D2H 回读检查。默认 `count=128` 且单 IO 不超过 4 GiB 时，CSV 中应看到 `aicpu_kernel_launches=1`、`aicpu_notifies=1`。

可以确认 Host 可执行文件没有误链接 HIXL：

```bash
ldd build_out/bin/acl_copy_bench | grep -E 'hixl|acl_rt|runtime'
```

## 输出

终端表格和 CSV 都包含两种方法的 submit/e2e p50、p95、e2e GiB/s、两类 speedup，以及 AICPU kernel/notify 数量。CSV 的 `verify` 列为 `PASS`；任一逐字节检查失败时程序立即返回非零，不会输出误导性的成功结果。
