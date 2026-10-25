# Contract: 会话设置

全部为 DuckDB 会话级设置，`SET` / `RESET` / `SELECT current_setting(...)` 可用。只影响远程 HTTP/HTTPS/S3 的 read_om 系列函数（`read_om`、`read_om_regions`、`om_grid_info` 的远程元数据读取）；本地文件行为不变。

| 名称 | 类型 | 默认 | 取值规则 | 关闭后的行为 |
|---|---|---|---|---|
| `duckomo_remote_merge` | BOOLEAN | `true` | — | 使用当前解码器 512 B 间隙 / 64 KiB 上限 |
| `duckomo_remote_merge_gap` | UBIGINT（字节） | `4096` | 0–1 GiB（限制规划与预读缓冲） | — |
| `duckomo_remote_max_request` | UBIGINT（字节） | `65536` | 512 B–1 GiB，且对象 LUT chunk 长度大于该值时查询报 `[configuration]` 错误 | — |
| `duckomo_async_io` | BOOLEAN | 2.0：`true`；1.5.x：`false` 且设为 true 不生效 | 2.0 远程值读取使用引擎 AsyncResult 调度；实际并发受 executor 线程数及预算限制 | 同步读取 |
| `duckomo_async_max_inflight` | UBIGINT | `8` | 1–256 | — |
| `duckomo_async_prefetch_batches` | UBIGINT | `1` | 1–4；2.0 单变量无过滤远程全扫的实验取数窗口，仍受 max_inflight 和内存限制；1.5.x 不生效 | 1 仅当前批；默认关闭跨批预读，因为实测增加请求且变慢 |

约定：

- 非法值在 `SET` 时报 `Invalid Input`，信息不含 URI 或凭据。
- 1.5.x 上 `SET duckomo_async_io=true` 成功，但 `remote_io.async_applied` 恒为 false。
- DuckDB 2.0 通过 `AsyncResult(tasks)` 将有限数量的读取队列交给引擎；每个队列顺序读取，多个队列可并行。每批根据 BufferManager 剩余内存与最大扫描 worker 数重算上限，紧张时缩为 1，后续批次可恢复。
- 非局部选择允许最多 `merge_gap` 字节的尾部预读，总请求仍不超过 `max_request`；空间、轴谓词及 region 选择禁止尾部预读。预读和未选择 chunk 均计入 `gap_bytes`。
- 设置不改变结果：同一查询在任意合法取值组合下返回相同行集合（顺序语义与现状一致）。
- 现有设置（`duckomo_max_threads` 等）与 DuckDB 自身的 `async_threads`、`threads`、`memory_limit` 语义不变；当前 pinned 2.0 的 AsyncResult 使用引擎 executor 调度，实际并发受 `threads` 限制，`duckomo_async_max_inflight` 为每 worker 上限。
