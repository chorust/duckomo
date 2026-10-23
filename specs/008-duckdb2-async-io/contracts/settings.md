# Contract: 会话设置

全部为 DuckDB 会话级设置，`SET` / `RESET` / `SELECT current_setting(...)` 可用。只影响远程 HTTP/HTTPS/S3 的 read_om 系列函数（`read_om`、`read_om_regions`、`om_grid_info` 的远程元数据读取）；本地文件行为不变。

| 名称 | 类型 | 默认 | 取值规则 | 关闭后的行为 |
|---|---|---|---|---|
| `duckomo_remote_merge` | BOOLEAN | `true` | — | 使用当前基线 512 B 间隙 / 64 KiB 上限 |
| `duckomo_remote_merge_gap` | UBIGINT（字节） | 实现阶段校准后写入（候选 65536） | ≥ 0 | — |
| `duckomo_remote_max_request` | UBIGINT（字节） | 实现阶段校准后写入（候选 4194304） | ≥ 512，且对象 LUT chunk 长度大于该值时查询报 `[configuration]` 错误 | — |
| `duckomo_async_io` | BOOLEAN | 2.0：`true`；1.5.x：`false` 且设为 true 不生效 | — | 同步读取 |
| `duckomo_async_max_inflight` | UBIGINT | `8` | 1–256 | — |

约定：

- 非法值在 `SET` 时报 `Invalid Input`，信息不含 URI 或凭据。
- 1.5.x 上 `SET duckomo_async_io=true` 成功，但 `remote_io.async_applied` 恒为 false。
- 设置不改变结果：同一查询在任意合法取值组合下返回相同行集合（顺序语义与现状一致）。
- 现有设置（`duckomo_max_threads` 等）与 DuckDB 自身的 `async_threads`、`threads`、`memory_limit` 语义不变；`async_threads` 为引擎上限，`duckomo_async_max_inflight` 为每 worker 上限。
