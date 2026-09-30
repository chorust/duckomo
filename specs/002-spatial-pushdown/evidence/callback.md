# 固定 DuckDB complex-filter callback 验收

**状态：通过（2026-09-29，Linux AArch64，DuckDB v1.5.4）**

验证命令：

```sh
cmake --build build/release --target spatial_callback_test -j2
build/release/test/native/spatial_callback_test
```

测试确认 `projection_pushdown=true`、`filter_pushdown=false`、`filter_prune=false`，并且 callback 已注册。固定版本的 `EXPLAIN` 保留了 `FILTER (humidity = 96.0)`，`READ_OM` 投影仍包含未输出的 `humidity` 与输出列 `temperature`。执行 `humidity = 96`、`96 <= humidity AND humidity <= 96` 后均返回 108 行，查询级 v2 sidecar 报告 `filter_callback_invoked=true`，且保留 `/humidity` 的读取计量。

DOUBLE 坐标测试使用 `latitude >= 11` 与反向常量 `11 <= latitude`。计划仍包含完整 `FILTER (latitude >= 11.0)`，实际查询返回 3 行；没有使用比较容差，也没有删除或改写过滤表达式。由于 callback 只观察和保存候选信息，DuckDB residual filter 继续负责精确结果。

该证据仅证明 callback 的保留语义和绑定形态；不证明空间下推或减少读取，相关门槛由后续空间 I/O 验收负责。
