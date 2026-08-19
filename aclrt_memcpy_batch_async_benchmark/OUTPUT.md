# 固定 shard 场景 benchmark

```text
// 输入：io_bytes（默认 176 KiB），460 个 shard；每个 method 都使用同一条 Stream
// 输出：每个 loop shard 的 submit 区间、batch API 的 submit 区间，以及 sync 区间
for method in [loop, batch]:
    Validate(method)                         // 不进入 trace
    for round in 0..rounds-1:                 // 默认 rounds=10，可用 --rounds 修改
        ResetDestination()                   // 不进入 trace

        if method == loop:
            for shard in 0..459:
                start = monotonic_clock()
                aclrtMemcpyAsync(shard, io_bytes)
                end = monotonic_clock()
                trace(round, method, "submit", shard, start, end)
        else:
            start = monotonic_clock()
            aclrtMemcpyBatchAsync(460 shards, io_bytes)
            end = monotonic_clock()
            trace(round, method, "submit", -1, start, end)

        start = monotonic_clock()
        aclrtSynchronizeStreamWithTimeout()
        end = monotonic_clock()
        trace(round, method, "sync", -1, start, end)
```
