# duckomo Roadmap

状态：v1 设计基线（2026-09-28）。阶段编号沿用原讨论；每一阶段完成时提交可复现的 fixture、命令和结果记录。优先交付从 SQL 到 OM partial read 的完整纵向切片。

| 阶段 | 交付物 | 退出条件 |
|---|---|---|
| **0 — Spike** | 锁定 DuckDB/OM 版本；本地单变量、已知 shape 的 `read_om_raw('test.om')` | 无 Python 参与，OM C reader 数据进入 DataChunk；与官方 reader 的值/顺序对齐；说明 C API、文件布局、I/O 适配的实际边界 |
| **1 — Proper scanner** | `read_om(path)`；bind OM metadata/hierarchy、变量、类型、维度、shape | `SELECT *` 和 `DESCRIBE` 对支持的 fixture 正确；缺失网格/时间语义时行为明确，不猜测坐标 |
| **2 — Projection pushdown** | `column_ids`→所需变量；避免解码无关变量 | 单变量与全变量 SQL 结果正确；instrumentation 证明未选变量的读取/解码减少 |
| **3 — Spatial pushdown** | `GridMapping`、RegularGrid、显式 domain/网格配置、bbox→OM 逻辑选择；之后 O320/O1280 | 经纬度过滤结果与基准一致，包括边界/跨经线；窄区域 bytes 和 decoded chunks 明显少于全域；不实现 OM chunk/byte-range 规划 |
| **4 — Other dimensions** | `DimensionMapping`：time、level、lead_time、member、run | 各轴条件形成正确 N-D 选择；混合时空谓词、维度顺序和空结果有测试 |
| **5 — Remote + parallel** | DuckDB filesystem 接入 HTTP/S3；任务切分、并行、缓存和 profiling | 本地/远程同一结果；远程查询只请求所需区域；记录 bytes fetched、requests、chunks decoded、耗时、峰值内存，并验证并发正确性 |
| **6 — Full grid coverage** | Projection/rotated/Lambert/stereographic 与 Gaussian N grids；由 Open-Meteo upstream 生成 registry | 每类网格有上游/真实数据对照；N-grid 不沿用旧简化公式；registry 可重生并检测上游变动 |
| **7 — Scientific compute** | 独立的 `om_slice`、`om_reduce`、`om_interp`、`om_regrid`；需要时使用 xtensor | 普通 `read_om` 不依赖 xtensor；科学算子与独立基准对照，重网格逻辑与扫描层隔离 |

## 里程碑与依赖

1. **Phase 0–2：可用扫描器**。先证明官方 OM C reader 和 DuckDB 扩展能够稳定组合，再接 hierarchy 和列裁剪。
2. **Phase 3：核心价值验证**。`WHERE latitude/longitude` 真正缩小 OM 局部读取，而非仅减少返回行数。这是首个产品级 milestone。
3. **Phase 4–5：多维与云端**。在正确的 N-D 语义上增加远程读取、并行和可观测性。
4. **Phase 6–7：覆盖与计算**。补全网格类型后，再独立设计科学算子。

## Phase 0/1 必须先消除的不确定性

- 所选真实 OM fixture 是纯数组还是 hierarchy；变量、维度、时间和 grid 信息分别从何处获得。
- 官方 C reader 的具体 partial-read、错误处理、buffer ownership 与 Sans-I/O 接口；duckomo 如何把请求交给 DuckDB 文件系统。
- 锁定 DuckDB 版本中 table function 对 projection/filter pushdown 的实际调用和剩余过滤语义。
- `om-exporter` registry 与 Open-Meteo 上游 domain/grid 定义的差异；哪些映射已能直接复用上游。

这些发现可能调整 API 的必填参数或阶段内部实现；若改变用户可见语义或职责边界，先更新 [Spec](spec.md) 和 [架构](architecture.md)。
