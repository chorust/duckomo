# US4 本地零值读取证据

记录日期：2026-10-06。此文件记录本地合成覆盖；它不代表 H3 完整 gate、远程收益或真实 OM v3 样本通过。

## 本地结果

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 四类网格坐标投影、局部 COUNT、可证空查询 | 通过 | [H3 本地 manifest](h3-local-20261006-v3/h3-local/manifest.json) 和逐场景 [v4 metrics](h3-local-20261006-v3/h3-local/zero-io-metrics.json) |
| 坐标、COUNT、可证空路径的值成本 | 全部为 0 index/data bytes、requests 和 decoded chunks | H3 metrics-validation 命令退出码 0；每变量独立核对 |
| 值过滤对照 | temperature 有值读取/解码；humidity 为 0 | `interleaved_value_filter` 场景 |
| 非空间轴重复记录 | 同一地理位置保留 32 条 time/level/lead/member/run 记录 | `interleaved_coordinates`、`interleaved_count` |
| 256 KiB 值 chunk 隔离 | 坐标路径不读取该变量的 index/data，也不解码 | `large_value_chunk_coordinates`；fixture SHA-256 `26501603c5bf7994d02a3f3a1f5369c85fb0adc95c7da1390172135caf973b52` |
| 坐标准备成本 | 18 个场景均有数值 evaluations 且 `complete=true`；范围 12–65792 | 同一逐场景 v4 metrics；完整值在 JSON 中 |
| 长时间轴/预算回归 | 通过 | `time_test`、`reader_capacity_test`、`axis_selection_test` 均退出码 0 |

H3 runner 中 native、SQLLogicTest、metrics-validation 三条命令分别退出码 0。runner 的整体退出码 2 是预期状态：synthetic local 子项为 `pass`，完整 H3 为 `not-run`。

## 本次验证命令

- `cmake --build build/release-vcpkg --target grid_zero_io_test duckomo_grid_validation time_test reader_capacity_test axis_selection_test -j2`：退出码 0。
- `build/release-vcpkg/test/native/time_test`：退出码 0。
- `build/release-vcpkg/test/native/reader_capacity_test`：退出码 0。
- `build/release-vcpkg/test/native/axis_selection_test`：退出码 0。
- H3 runner 命令及 native/SQL/metrics 子命令：见上述 manifest；三个子命令退出码均为 0。
- `build/release-vcpkg/test/native/session_metrics_test`：退出码 0；检查 SQL v4、嵌入的完整 legacy_v3、多个 scan 的 QueryEnd 发布及读取/清缓存不覆盖最近扫描。
- `build/release-vcpkg/test/unittest /home/blizhan/repo/github/duckomo/test/sql/cache_metrics.test`：退出码 0，25 assertions 通过；覆盖 LIMIT 的 `scan_complete=false`、空候选计数和终态标记。
- v3/v4 sidecar 检查：v3 文件仍是 schema 3；v4 文件的每个 profile 为 schema 4 且已发布 QueryEnd 终态。

## 未运行范围

- 本环境没有配置 HTTP/HTTPS 远程 fixture、签名 S3 桶或对应审计服务，因此没有远程 H3 执行记录。
- source/info 子用例依赖 US5；在其实现前完整 H3 保持 `not-run`。真实 OM v3 样本和独立 oracle 的缺项也仍按 sample manifest 记录，不用合成 fixture 代替。
- 这不改变本地零值读取结论，也不构成远程收益或逐 domain 支持声明。
