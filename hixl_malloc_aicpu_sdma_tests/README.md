# hixl_malloc_aicpu_sdma_tests：普通 ACL malloc 的单机 AICPU SDMA 展开测试

这个工程用于隔离验证单机小包 H2D/D2H 的 AICPU 展开能力。它比较：

1. baseline：循环调用 `aclrtMemcpyAsync`，最后同步一次 stream；
2. AICPU batch：一次上传描述符，launch AICPU kernel，由 AICPU 向 worker RTSQ 展开 SDMA SQE。

与 `hixl_fabric_aicpu_sdma_tests` 的关键区别是内存来源：

- Host 缓冲区只通过 `aclrtMallocHost` 分配；
- Device 缓冲区只通过 `aclrtMalloc(..., ACL_MEM_MALLOC_NORMAL_ONLY)` 分配；
- 不调用 `aclrtReserveMemAddress*`、`aclrtMallocPhysical`、`aclrtMapMem`、
  `aclrtMemSetAccess` 或 `aclrtUnmapMem`。

因此，这个工程可以直接验证普通 ACL pinned Host 地址能否被 AICPU 生成的 SDMA SQE
访问。如果 baseline 正常而 AICPU 路径失败，说明普通 `aclrtMallocHost` 地址不能直接满足
该 RTSQ/SDMA 路径的寻址要求；这个失败本身就是验证结果，而不是 VMM 初始化问题。

## 复用边界

工程直接复用相邻 `hixl_fabric_aicpu_sdma_tests` 中已经验证过的内容：

- AICPU kernel、STARS SDMA SQE 和 TransferContext 实现；
- Host 侧 AICPU launcher；
- 描述符拆分、提交形状、统计及正确性验证逻辑。

本工程只新增普通 malloc 内存管理、独立入口和独立构建/安装包装。生成的设备文件为：

- `libacltest_malloc_sdma_kernel.json`
- `acltest-malloc-sdma-compat.tar.gz`
- `libacltest_malloc_sdma_kernel.so`

它们不会覆盖 Fabric 测试的 `libacltest_sdma_kernel.*`。

自定义 built-in AICPU 包不会因为复制到 `opp/built-in/op_impl/aicpu/kernel` 就被
自动加载。安装脚本还会在当前 CANN 的 `conf/ascend_package_load.ini` 中登记该归档，
让 TSD 在业务进程启动时把它发送并解压到 Device。登记块带有 AclTest 专用标记，卸载
和清理脚本只移除该标记块，不修改 CANN 自带的 HIXL/Fabric 配置。

## 构建与安装

构建依赖 CANN Runtime `>=9.0.0`，运行依赖 `>=8.5`。脚本优先使用
`ASCEND_HOME_PATH`；未设置时从 `/usr/local/Ascend` 动态发现当前 CANN，不依赖具体版本目录名。

```bash
cd /workspace/ACLTest/hixl_malloc_aicpu_sdma_tests
bash build.sh
bash scripts/install_kernel.sh
```

已有同名测试包时使用：

```bash
bash scripts/install_kernel.sh --force
```

安装完成后需要重新启动 benchmark 进程，使 TSD 读取更新后的包配置。若只复制 JSON
和 tar、没有上述登记，Host 仍能下发 kernel，但 Device 侧会因找不到
`libacltest_malloc_sdma_kernel.so` 报 `errcode:11002, msg:open so failed`。

## 运行

默认扫描 D2H/H2D、10 档 IO size、IO count 128：

```bash
build_out/bin/acl_malloc_copy_bench --csv results.csv
```

先做最小验证可以使用：

```bash
build_out/bin/acl_malloc_copy_bench \
  --direction both \
  --sizes 512B,4K \
  --counts 1,128 \
  --iterations 5 \
  --warmup 1 \
  --csv results_smoke.csv
```

程序会先分别验证 baseline 和 AICPU 结果，再进入计时。提交耗时包含描述符构造、请求
buffer 分配与上传以及 kernel enqueue；端到端耗时截止到 stream 同步完成。

## 清理

```bash
bash scripts/uninstall_kernel.sh
bash scripts/clear.sh
```

两个脚本只删除本工程唯一命名的设备包及其 AclTest-owned TSD 登记块；`clear.sh` 还会
删除本目录下的 `build/` 和 `build_out/`，不会删除 CSV、Fabric 测试或 HIXL 文件。
