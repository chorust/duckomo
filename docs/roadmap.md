# duckomo Roadmap

状态：2026-09-28。Phase 0–2 已实现并在 Linux AArch64 验证；Linux x86_64 尚无运行证据。Phase 3–7 为后续计划。已完成阶段的命令、fixture 与结果见 [最终验收记录](../specs/001-local-om-scanner/evidence/final.md)。

| 阶段 | 状态 | 交付物 / 退出条件 |
|---|---|---|
| **0 — Spike** | 已完成 | 固定 DuckDB/OM 版本；`read_om_raw` 将本地 OM C reader 解码结果输出为 DataChunk，并与官方 reader 参考结果对齐。 |
| **1 — Proper scanner** | 已完成 | `read_om(path, dimensions := ...)` 绑定 metadata/hierarchy 和稳定 schema；多变量通过一致的 `coordinates` 元数据或显式轴声明对齐。 |
| **2 — Projection pushdown** | 已完成 | 按 `column_ids` 只读取所需变量，保留过滤依赖；与全量结果及物理读取指标对照。`COUNT(*)` 无索引或数据读取。 |
| **3 — Spatial pushdown** | 计划中 | `GridMapping`、RegularGrid、显式 domain/网格配置、bbox→OM 逻辑选择；经纬度结果与基准一致，窄区域 bytes/chunks 少于全域。 |
| **4 — Other dimensions** | 计划中 | `DimensionMapping`：time、level、lead_time、member、run；验证混合谓词、维度顺序和空结果。 |
| **5 — Remote + parallel** | 计划中 | DuckDB filesystem 接入 HTTP/S3；任务切分、并行、缓存和 profiling；验证本地/远程结果及远程局部读取。 |
| **6 — Full grid coverage** | 计划中 | Projection/rotated/Lambert/stereographic 与 Gaussian N grids；每类网格有上游/真实数据对照，registry 可重生。 |
| **7 — Scientific compute** | 计划中 | 独立的 `om_slice`、`om_reduce`、`om_interp`、`om_regrid`；普通 `read_om` 保持直接输出 DuckDB Vector。 |

## 里程碑与依赖

1. **Phase 0–2：本地扫描器已完成**。官方 OM C reader、hierarchy 和列裁剪已接入；在 Linux AArch64 的 SQL、native、fixture 和 sanitizer 验证已通过。
2. **Phase 3：核心价值验证**。`WHERE latitude/longitude` 真正缩小 OM 局部读取，而非仅减少返回行数。这是首个产品级 milestone。
3. **Phase 4–5：多维与云端**。在正确的 N-D 语义上增加远程读取、并行和可观测性。
4. **Phase 6–7：覆盖与计算**。补全网格类型后，再独立设计科学算子。

## Phase 0–2 已确认的实现决策

- 支持的文件子集为本地 OM v3、Float32、FPX_XOR2D 和 PFOR_DELTA2D_INT16；根数组与层级数组均可读取，其他格式按 [SQL 契约](../specs/001-local-om-scanner/contracts/sql-interface.md) 拒绝。
- 多变量需具备相同 shape 和有序轴身份；身份可来自一致的 `coordinates` 元数据或完整的显式 `dimensions` 声明。当前仍不生成时间或网格坐标列。
- 官方 OM C reader 负责格式、chunk 和解码，本地 Sans-I/O 请求接到 DuckDB 文件系统。
- DuckDB v1.5.4 上已验证 projection pushdown；`filter_pushdown` 和 `filter_prune` 保持关闭，过滤仍由 DuckDB 执行。

网格/domain registry 与上游定义的对照属于 Phase 3 和 Phase 6。若后续改变用户可见语义或职责边界，同步更新 [Spec](spec.md) 和 [架构](architecture.md)。
