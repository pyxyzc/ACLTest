# aclrtSetDevice Tests

这是一个独立的 Ascend Runtime 硬件单测工程，用于验证
[`aclrtSetDevice`](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/latest/API/runtimeapi/aclcppdevg_03_0039.html)
的使用、当前线程设备选择、默认 Context 以及资源释放行为。

## 覆盖内容

1. 获取可用设备数量后，对 `device0` 调用两次 `aclrtSetDevice`；每次通过
   `aclrtGetDevice` 验证当前线程的 Device，通过 `aclrtGetCurrentContext` 验证默认
   Context 已创建且重复调用仍为同一个 Context；随后调用两次 `aclrtResetDevice` 配对释放。
2. 以 `aclrtGetDeviceCount` 返回的设备数作为越界 Device ID，验证
   `aclrtSetDevice` 返回失败；不绑定具体错误码。
3. 有两张可用卡时，验证 `device0 -> device1 -> device0` 切换后每一步的当前 Device；
   单卡、`device1` 不可用或两个 ID 相同时自动跳过该项。
4. 两个线程同时对同一个 Device 调用 `aclrtSetDevice`，分别验证线程当前 Device 和
   `aclrtGetCurrentContext`，并断言二者获得相同默认 Context；每个线程各自调用一次
   `aclrtResetDevice`。

无可用 NPU 时程序输出 `SKIP` 并以退出码 `77` 结束，CTest 会标记为跳过。任何 API
调用异常、当前 Device 不符、默认 Context 为空或跨线程 Context 不同都会使测试失败。

## 编译

```bash
cd /path/to/ACLTest/aclrt_set_device_tests
source /usr/local/Ascend/ascend-toolkit/set_env.sh
cmake -S . -B build -DASCEND_ROOT=/usr/local/Ascend/ascend-toolkit/latest
cmake --build build --parallel
```

`ASCEND_ROOT`、`ASCEND_HOME`、`ASCEND_TOOLKIT_HOME` 或 `ASCEND_INSTALL_PATH` 已正确设置时，
可以省略 `-DASCEND_ROOT=...`。

## 运行

```bash
./build/aclrt_set_device_probe
ctest --test-dir build --output-on-failure
```

默认使用 `device0=0` 与 `device1=1`。可用参数：

```bash
./build/aclrt_set_device_probe --device0 0 --device1 1
./build/aclrt_set_device_probe --help
```

`device0` 必须在 `aclrtGetDeviceCount` 返回的合法范围内，否则程序以参数错误退出。
