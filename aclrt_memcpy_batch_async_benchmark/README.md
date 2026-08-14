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
- 两种方法各自的预运行逐字节正确性校验。

Host 内存固定通过 `aclrtMallocHost` 申请。普通 `malloc` 内存可能让异步接口退化为同步执行，
不适合衡量提交开销。

为避免 Event Record 的 Host 开销污染小包 e2e，每个 measured iteration 对每种方法执行两个
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

## 运行

默认扫描 `512B,1K,2K,4K,32K,64K,256K,512K,1M,2M`，batch count 固定为 128，
方向为 H2D 和 D2H，与仓库中已有 copy benchmark 的默认 case 保持一致：

```bash
./build_out/bin/aclrt_memcpy_batch_bench --csv results.csv
```

快速 smoke：

```bash
./build_out/bin/aclrt_memcpy_batch_bench \
  --device 0 --direction both --sizes 4K --counts 1,32 \
  --warmup 1 --iterations 5 --csv smoke.csv
```

所有参数：

```text
--device N
--direction h2d|d2h|both
--sizes 512B,4K,1M
--counts 1,8,32,128
--warmup N
--iterations auto|N
--target-bytes 256M
--timeout-ms 60000
--csv PATH
```

自动迭代模式按每种方法累计约 `--target-bytes` 的流量计算迭代次数，并限制在 5 到 500
次。终端和 CSV 都保留 p50/p95。speedup 大于 1 表示 batch 接口更快。

## 测试边界

- batch API 规定每个批次只能使用 H2D 或 D2H 中的一种方向，因此本程序不混合方向，也不测 D2D；
- batch 内部不保证按数组顺序执行，因此每段源/目的地址均互不重叠；
- 数据准备、目标缓冲区重置和正确性校验不计入时延；
- 每个 measured iteration 都运行两种方法，并交换方法顺序及 Event/无 Event 副本顺序，
  降低固定顺序偏差；
- `submit speedup` 衡量 Host 下发开销，`execution speedup` 衡量 Event 区间内的实际执行，
  `e2e speedup` 则衡量无 Event 插桩时的 Host 提交及同步等待。
