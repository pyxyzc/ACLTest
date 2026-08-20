# aclrt VMM Tests

这是一套独立的小测试，用来验证 Ascend Runtime 的 VMM 映射、物理内存和跨进程共享接口。

接口用法总结见 [docs/interface_usage.md](docs/interface_usage.md)。

源码按职责分为三层：`src/app` 放测试入口和 runner，`src/runtime` 放 ACL Runtime、
物理内存、映射和拷贝基础能力，`src/ipc` 放 IPC 协议、父子进程编排和测试矩阵。

默认优先使用 V2 接口：

- `aclrtMemExportToShareableHandleV2`
- `aclrtMemSetPidToShareableHandleV2`
- `aclrtMemImportFromShareableHandleV2`

也可以用 `--use-v1` 强制走 V1。

## 覆盖的能力

1. 单进程 VMM 生命周期：
   `aclrtMemGetAllocationGranularity -> aclrtMallocPhysical ->
   aclrtReserveMemAddress -> aclrtMapMem -> aclrtMemSetAccess ->
   aclrtMemcpy -> aclrtUnmapMem -> aclrtFreePhysical ->
   aclrtReleaseMemAddress`。除 host/vector 与 VMM VA 之间的拷贝外，还会申请两块独立
   device physical memory 并验证 VMM device VA-to-device VA memcpy；随后申请 host
   physical memory 与 device physical memory，验证 host VA -> device VA 和 device
   VA -> host VA。
2. Device physical memory 跨进程共享：
   父进程申请 `ACL_MEM_LOCATION_TYPE_DEVICE + ACL_HBM_MEM_HUGE` 物理内存并导出
   shareable handle，子进程 import 后重新 reserve/map/set access。子进程先读父进程
   pattern，再写入 reply pattern，父进程最后读回校验。随后追加两块独立 device
   physical memory 的跨进程 VA-to-VA 校验，parent 和 child 各执行一次 D2D VA-to-VA
   memcpy。
3. Host physical memory 跨进程 memcpy 能力：
   父进程申请 `ACL_MEM_LOCATION_TYPE_HOST_NUMA + ACL_DDR_MEM_HUGE` 物理内存并导出
   shareable handle，子进程 import 后重新 reserve/map/set access。该测试使用
   2MiB 固定对齐申请大小，不再对 host physical prop 调用
   `aclrtMemGetAllocationGranularity`；随后使用 `ACL_MEM_LOCATION_TYPE_HOST`
   access desc 和 `ACL_MEMCPY_HOST_TO_HOST` 做双向校验。
   若子进程 import/map 成功，还会额外用一块 `aclrtMalloc` 申请的 device ptr 探测
   imported host VA 参与 `ACL_MEMCPY_HOST_TO_DEVICE` 与
   `ACL_MEMCPY_DEVICE_TO_HOST` 的能力。
   随后追加两块独立 host physical memory 的跨进程 VA-to-VA 校验，parent 和 child
   各执行一次 H2H VA-to-VA memcpy。任一步不支持或失败都会返回 FAIL。
4. Host physical memory 跨进程 pointer 能力：
   与 memcpy 测试复用相同的 export/import/reserve/map/set access 前置流程，但能力
   阶段不调用 `aclrtMemcpy`，而是由 parent 和 child 分别通过 `volatile uint8_t*`
   直接写入和读取 imported host VA。子进程先发送 setup ready，父进程确认双方
   `aclrtMemSetAccess` 均成功后才开始 pointer 访问；前置流程失败时不会解引用 VA。

## 编译

```bash
cd /root/ACLTest/aclrt_vmm_tests
bash build.sh
```

`build.sh` 默认使用 `Release`、8 路并行，并将程序安装到 `build_out/bin/`。可用参数：

```bash
bash build.sh -j 16 --build-type Debug \
  --ascend-root /usr/local/Ascend/ascend-toolkit/latest
```

清理本项目的本地构建产物：

```bash
bash clear.sh
```

## 运行

单项测试：

```bash
./build_out/bin/aclrt_vmm_single_process_test --device 0 --size 4096
./build_out/bin/aclrt_vmm_device_ipc_test --device 0 --size 4096
./build_out/bin/aclrt_vmm_host_memcpy --device 0 --host-numa 0 --size 4096
./build_out/bin/aclrt_vmm_host_pointer --device 0 --host-numa 0 --size 4096
```

Suite 入口会顺序运行单进程、Device IPC、Host memcpy、Host pointer 和
Device-Host IPC：

```bash
./build_out/bin/aclrt_vmm_tests --device 0 --host-numa 0 --size 4096
```

IPC 测试会从同一目录启动 `aclrt_vmm_ipc_child`。child 进程在独立的 exec 后进程中调用
AscendCL，父进程和 child 之间只通过 pipe 交换 PID、共享句柄和测试结果。

常用参数：

```bash
./build_out/bin/aclrt_vmm_device_ipc_test --device 0 --size 4096 --disable-pid-validation
./build_out/bin/aclrt_vmm_device_ipc_test --device 0 --size 4096 --use-v1
./build_out/bin/aclrt_vmm_device_ipc_test --device 0 --size 4096 --share-type fabric
./build_out/bin/aclrt_vmm_host_memcpy --device 0 --host-numa 0 --size 4096 --use-v1
./build_out/bin/aclrt_vmm_host_pointer --device 0 --host-numa 0 --size 4096 --use-v1
```

`--size` 是实际校验的数据长度；device physical memory 会用
`aclrtMemGetAllocationGranularity(... ACL_RT_MEM_ALLOC_GRANULARITY_MINIMUM ...)`
把申请大小向上对齐，host physical memory 则跳过 granularity 查询并直接按 2MiB
对齐。`--host-numa` 只影响 host physical memory 测试。

## 官方文档

- aclrtMallocPhysical: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0112.html
- aclrtFreePhysical: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0113.html
- aclrtReserveMemAddress: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0114.html
- aclrtReleaseMemAddress: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0115.html
- aclrtMapMem: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0116.html
- aclrtUnmapMem: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0117.html
- aclrtMemExportToShareableHandle: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0118.html
- aclrtDeviceGetBareTgid: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0119.html
- aclrtMemSetPidToShareableHandle: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0120.html
- aclrtMemImportFromShareableHandle: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0121.html
- aclrtMemGetAllocationGranularity: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_0122.html
- aclrtMemExportToShareableHandleV2: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_2006.html
- aclrtMemSetPidToShareableHandleV2: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_2007.html
- aclrtMemImportFromShareableHandleV2: https://www.hiascend.com/document/detail/zh/canncommercial/900/API/runtimeapi/aclcppdevg_03_2008.html
