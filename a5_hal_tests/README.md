# A5 HAL 单项测试

这个目录单独验证 A5 AICPU 中的四个 HAL 调用：

| `--case` | 调用 | 做什么 |
| --- | --- | --- |
| `query_sq_base` | `halSqCqQuery` | 查询 worker SQ 的基地址 |
| `query_sq_depth` | `halSqCqQuery` | 查询 worker SQ 的深度 |
| `query_sq_head` | `halSqCqQuery` | 查询 worker SQ 的 head |
| `query_sq_tail` | `halSqCqQuery` | 查询 worker SQ 的 tail |
| `restore_stream` | `halResourceIdRestore` | 恢复 worker stream 的资源 ID |
| `config_tail` | `halSqCqConfig` | 先读当前 tail，再把同一个 tail 写回 |
| `report_empty_cq` | `halCqReportRecv` | 从未提交任务的 logic CQ 读取一次 |

它不申请 Fabric Host 内存、不构造 SDMA SQE、不发送 Notify。这样发生超时时，范围就收敛在 AICPU kernel 调度或这一个 HAL 调用附近，而不是数据搬运路径。

`config_tail` 为了不改变 SQ 状态，会先执行一次 `query_sq_tail`，把读到的值原样写回；不会提交任何 SQE。

## 构建与安装

需要完整的 CANN Toolkit 和 AICPU toolchain。先加载对应 CANN 环境，然后构建：

```bash
cd /root/ACLTest/a5_hal_tests
bash build.sh
```

构建会生成：

- `build_out/bin/acl_a5_hal_test`：Host 测试程序；
- `build_out/opp/built-in/op_impl/aicpu/config/libacltest_a5_hal_test.json`：AICPU 函数配置；
- `build_out/opp/built-in/op_impl/aicpu/kernel/acltest-a5-hal-test-compat.tar.gz`：AICPU 设备包。

安装设备包并登记到当前 CANN：

```bash
bash scripts/install_kernel.sh
```

若需要指定 CANN 根目录：

```bash
bash scripts/install_kernel.sh --ascend-home /path/to/cann
```

安装脚本只写入本目录专用的 JSON、归档和 `ACLTEST_A5_HAL_TEST` 登记块，不会覆盖原 benchmark 的 AICPU 包。重复安装时若文件已存在，需要明确传入 `--force`。

## 运行

每个 case 都应该在独立进程中运行，这样一个 case 卡住不会影响下一个。最简单的方式是运行全部 case：

```bash
bash scripts/run_all.sh --device 0 --timeout-ms 3000
```

也可以只运行一个 case：

```bash
build_out/bin/acl_a5_hal_test --case query_sq_tail --device 0 --timeout-ms 3000
```

程序只接受 `aclrtGetSocName()` 以 `Ascend950` 开头的 A5 SoC。默认 kernel JSON 路径来自 `ASCEND_HOME_PATH`；若安装在其它位置，可传 `--kernel-json PATH`，或设置 `ACLTEST_A5_HAL_TEST_KERNEL_JSON`。

## 如何看结果

每个 case 都输出一行原始返回值，再输出最终结果：

- `PASS`：`drvGetLocalDevIDByHostDevID` 和目标 HAL 调用均正常返回。`report_empty_cq` 允许返回“无 CQE”的超时，或成功且 CQE 数量为零。
- `FAIL`：AICPU kernel 已返回，但目标 HAL 返回错误，或查询出的关键值不合法。
- `TIMEOUT`：Host 在指定时间内未等到这个 AICPU kernel 完成，进程返回码为 `124`。

输出中的 `preflight_ret` 是 `drvGetLocalDevIDByHostDevID` 的返回值。这不是单独的测试 case；它只是和 `stars_sdma.cc` 相同的必要前置步骤：先把 Host 的物理 device ID 转成 HAL 调用所需的本地 device ID。

若某个 case `TIMEOUT`，先保留该 case 名称、Host 输出与同一时间段的 AICPU 日志；随后可只重跑该 case 复现问题。卸载本测试包时执行：

```bash
bash scripts/uninstall_kernel.sh
```
