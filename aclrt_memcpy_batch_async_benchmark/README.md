# aclrtMemcpyBatchAsync benchmark

该工程对比以下两个官方 Runtime API：

- [`aclrtMemcpyBatchAsync`](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/latest/API/runtimeapi/aclcppdevg_03_1957.html)：一次提交一批内存复制；
- [`aclrtMemcpyAsync`](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/latest/API/runtimeapi/aclcppdevg_03_0106.html)：baseline 对同一批分段逐次调用，最后同步一次 Stream。

每个 case 使用相同的锁页 Host 缓冲区、Device 缓冲区、Stream、IO 大小和 IO 数量，扫描
H2D 与 D2H 两种方向。程序分别记录三层时延：

- `submit`：Host 侧异步接口本身的调用耗时；loop 为 N 次 `aclrtMemcpyAsync`，batch 为一次
  `aclrtMemcpyBatchAsync`；
- `execution`：在复制任务前后插入 `ACL_EVENT_TIME_LINE` Event，通过
  `aclrtEventElapsedTime` 得到的 Stream 执行时延；
- `e2e`：从开始调用异步接口到一次 `aclrtSynchronizeStream` 返回的 Host 墙钟时延；
- 三类时延各自的 p50/p95、execution/e2e GiB/s，以及 `loop / batch` speedup；
- 每种被选中方法的预运行逐字节正确性校验。

Host 内存固定通过 `aclrtMallocHost` 申请。普通 `malloc` 内存可能让异步接口退化为同步执行，
不适合衡量提交开销。

为避免 Event Record 的 Host 开销污染小包 e2e，每个 measured iteration 对每种被选中方法执行两个
相同副本：无 Event 的副本统计 `submit/e2e`，带 timeline Event 的副本只统计 `execution`。
两个副本的先后顺序也逐次交换。`--target-bytes` 表示每个计时副本的目标累计流量，因此每种
方法的实际复制流量约为该值的两倍。

## 构建

需要所选 CANN Toolkit 的头文件中已经包含 `aclrtMemcpyBatchAsync`。该接口当前只在
Ascend 950、Atlas A3、Atlas A2 系列产品上受支持；其他产品会在运行时返回不支持。

```bash
cd /root/ACLTest/aclrt_memcpy_batch_async_benchmark
bash build.sh
```

如果 CANN 不在默认路径：

```bash
bash build.sh --ascend-root /path/to/ascend-toolkit/latest -j 8
```

构建脚本会把最终程序放到 `build_out/bin/`。清理本地构建产物（保留 CSV）：

```bash
bash clear.sh
```

运行前需加载同一套 CANN 的环境，例如：

```bash
source /usr/local/Ascend/ascend-toolkit/latest/set_env.sh
```

## UB 路径预判

独立程序 `aclrt_memcpy_batch_path_test` 按
`src/runtime/core/src/api_impl/api_impl_david.cc:867` 的判断顺序预判
`aclrtMemcpyBatchAsync` 路径，不执行正式 benchmark：

```bash
./build_out/bin/aclrt_memcpy_batch_path_test \
  --device 0 --host-memory pinned
```

需要脚本化断言必须进入 UB Batch DMA 时增加 `--require-ub`；若未命中，程序返回 2：

```bash
./build_out/bin/aclrt_memcpy_batch_path_test \
  --device 0 --host-memory pinned --require-ub
```

该 test 会依次检查：

- 通过运行时符号查询 NPU 架构号 3510 或 SoC 名称确认 Ascend 950PR/950DT（A5）；
- 在运行时解析用户 Device ID 到 Runtime 逻辑 Device ID的转换接口，再调用进程中已加载的
  `halSupportFeature(logicDeviceId, FEATURE_MEMCPY_BATCH_ASYNC=3)`；
- 严格保留源码 `!NpuDriver::CheckIsSupportFeature(...)` 的取反语义；
- 调用与 Runtime 相同的 `halGetDeviceInfo(..., INFO_TYPE_HD_CONNECT_TYPE, ...)` 判断 UB；
- `aclrtMallocHost` 分配的锁页内存或普通 `malloc` 的非注册内存。

最终 `VERDICT` 的含义：

| VERDICT | 对应源码路径 |
| --- | --- |
| `UB_BATCH_DMA` | 驱动 gate 进入 Runtime 分支、互联为 UB、Host 指针已注册 |
| `LOOP_MEMCPY_ASYNC` | PCIe/HCCS 的逐项异步复制路径 |
| `SYNC_MEMCPY_BATCH` | UB 分支中发现非锁页 Host 指针，先同步 Stream 再同步 batch |
| `RT_ERROR_DRV_NOT_SUPPORT_EXPECTED` | 驱动 feature 查询为 true，按当前 867 行实现将返回不支持 |
| `NOT_A5_API_IMPL_DAVID_PATH` | NPU 架构不是 A5 的 3510，不能用该 A5 判断下结论 |
| `INDETERMINATE_*` | 当前安装包未导出某个内部查询符号，程序拒绝猜测路径 |

可用 `--host-memory pageable` 专门验证非锁页判断。若当前进程中没有
`halSupportFeature` 符号，test 会与 `NpuDriver::CheckIsSupportFeature` 的弱符号处理保持一致，
将该 feature 判为 false，并在 `driver_detail` 中明确打印原因。

为了兼容不同形态的 9.1 安装包，test 不要求安装包公开 HAL 内部头文件，而是在运行时解析
所需符号。`FEATURE_MEMCPY_BATCH_ASYNC=3`、`MODULE_TYPE_SYSTEM=0` 和
`INFO_TYPE_HD_CONNECT_TYPE=40` 来自所参考的 Runtime 源码；如果换到修改过这些 ABI 值或分支
逻辑的 Runtime 源码，需同步更新 test。正式版 CANN Runtime 9.1.0 包含完整的
A5 UB Batch DMA 单算子和 ACL Graph/Software SQ 路径；`9.1.0-beta.2.pre`、
`9.1.0-beta.3` 只有较早的单算子基础版本，不能只根据 test 的单算子结论外推 Graph 路径。

## 3-IO size sweep benchmark

`aclrt_memcpy_shard_bench` 是独立入口。默认扫描 `16K,32K,48K,...,256K`，每个 size case
每轮固定下发 3 个相同大小、连续且互不重叠的 memcpy IO，默认 H2D、warmup 10 轮、测量 100 轮。

- `loop`：每轮连续调用 3 次 `aclrtMemcpyAsync`；原始 trace 分别记录 `io_index=0,1,2`；
- `batch`：每轮一次调用 `aclrtMemcpyBatchAsync`，其中 `numBatches=3`，原始 trace 使用
  `io_index=-1,io_count=3`；
- `submit_api`：单次 API 调用区间；loop 汇总 3×rounds 个样本，batch 汇总 rounds 个样本；
- `submit_round`：loop 从第一个 API 开始到第三个 API 结束的整轮提交区间，batch 等于 batch
  API 区间；
- `sync`：所有 IO 下发后，仅记录 `aclrtSynchronizeStreamWithTimeout` 前后的区间；
- 不记录端到端区间，不插入 Event；所有时间戳都是 Host 单调时钟的相对纳秒值。

汇总 CSV 使用 `size_bytes,direction,method,metric,samples,p50_us,p95_us` 字段；原始 trace CSV
保留每条区间的 `start_ns/end_ns/duration_us`，并额外记录 round、method 和 io_index。loop 与
batch 每轮成对执行，并逐轮交替先后顺序。程序结束前也会在命令行打印按 size、方向和 method
聚合的简洁表格，列出 `submit_api`、`submit_round` 和 `sync` 的 `p50/p95`。

```bash
./build_out/bin/aclrt_memcpy_shard_bench \
  --device 0 --method both --direction h2d \
  --sizes 16K,32K,48K,64K,80K,96K,112K,128K,144K,160K,176K,192K,208K,224K,240K,256K \
  --warmup 10 --rounds 100 \
  --csv shard_io_summary.csv --trace shard_io_trace.csv
```

可用 `--sizes 16K,32K,64K` 自定义多个 size，也可以用 `--io-size 64K` 只运行一个 size；两者
同时指定会报错。`--rounds N` 控制 p50/p95 的 measured 样本数，`--iterations N` 仍作为兼容
别名保留。也可以用 `--direction d2h` 或 `--direction both`，以及 `--method loop`、
`--method batch` 单独采集一种方案。构建脚本会同时安装这个新入口：

```text
build_out/bin/aclrt_memcpy_shard_bench
```

## 原有 sweep benchmark

默认扫描 `512B,1K,2K,4K,32K,64K,256K,512K,1M,2M`，batch count 固定为 128，
方向为 H2D 和 D2H，与仓库中已有 copy benchmark 的默认 case 保持一致：

```bash
./build_out/bin/aclrt_memcpy_batch_bench --csv results.csv
```

快速 smoke：

```bash
./build_out/bin/aclrt_memcpy_batch_bench \
  --device 0 --method both --direction both --sizes 4K --counts 1,32 \
  --warmup 1 --iterations 5 --csv smoke.csv
```

只运行其中一种方案时使用 `--method loop` 或 `--method batch`。未运行方法及 speedup 的结果
显示为 `nan`；只有 `both` 模式计算 speedup。

所有参数：

```text
--device N
--method loop|batch|both
--direction h2d|d2h|both
--sizes 512B,4K,1M
--counts 1,8,32,128
--warmup N
--iterations auto|N
--target-bytes 256M
--timeout-ms 60000
--csv PATH
```

自动迭代模式按每种被选中方法累计约 `--target-bytes` 的流量计算迭代次数，并限制在 5 到 500
次。终端和 CSV 都保留 p50/p95。speedup 大于 1 表示 batch 接口更快。

## msprof

单独采集 loop：

```bash
msprof --output=./msprof_loop \
  --ascendcl=on --runtime-api=on --task-time=on \
  --aicpu=off --ai-core=off --type=text \
  ./build_out/bin/aclrt_memcpy_batch_bench \
  --device 0 --method loop --direction h2d --sizes 4K --counts 128 \
  --warmup 0 --iterations 1 --csv ./msprof_loop.csv
```

单独采集 batch：

```bash
msprof --output=./msprof_batch \
  --ascendcl=on --runtime-api=on --task-time=on \
  --aicpu=off --ai-core=off --type=text \
  ./build_out/bin/aclrt_memcpy_batch_bench \
  --device 0 --method batch --direction h2d --sizes 4K --counts 128 \
  --warmup 0 --iterations 1 --csv ./msprof_batch.csv
```

`--warmup 0 --iterations 1` 下，所选方案会下发 4 次：正确性验证的无 Event/有 Event
各一次，以及 measured iteration 的无 Event/有 Event 各一次；不会混入另一种异步方案。

## 测试边界

- batch API 规定每个批次只能使用 H2D 或 D2H 中的一种方向，因此本程序不混合方向，也不测 D2D；
- batch 内部不保证按数组顺序执行，因此每段源/目的地址均互不重叠；
- 数据准备、目标缓冲区重置和正确性校验不计入时延；
- `both` 模式的每个 measured iteration 都运行两种方法并交换方法顺序；单方法模式只执行指定
  方法。两种模式都会交换 Event/无 Event 副本顺序，降低固定顺序偏差；
- `submit speedup` 衡量 Host 下发开销，`execution speedup` 衡量 Event 区间内的实际执行，
  `e2e speedup` 则衡量无 Event 插桩时的 Host 提交及同步等待。
