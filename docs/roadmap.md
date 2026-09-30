# duckomo Roadmap

本地扫描、列裁剪和规则网格空间查询（Phase 0–3）已实现。当前行为见 [接口说明](spec.md)，模块边界见 [技术架构](architecture.md)。

## 阶段

| 阶段 | 状态 | 交付物 / 验收条件 |
|---|---|---|
| **0 — 原型** | 已完成 | `read_om_raw` 接入官方 OM C reader；本地根数组与官方参考结果一致 |
| **1 — 本地扫描器** | 已完成 | `read_om` 支持元数据、层级变量和稳定 schema；通过 coordinates 或完整 dimensions 对齐多变量 |
| **2 — 列裁剪** | 已完成 | 只读取输出与过滤依赖；结果与全扫一致；`COUNT(*)` 无值 index/data 读取 |
| **3 — 空间选择** | 已实现 | 显式规则网格和 68 个登记 domain；安全经纬度条件缩小逻辑读取；固定多块样本的数据字节与解码块数严格下降 |
| **4 — 其他维度** | 计划中 | time、level、lead_time、member、run 映射；验证混合谓词、维度顺序和空结果 |
| **5 — 远程与并行** | 计划中 | HTTP/S3 文件系统、任务切分、缓存和 profiling；核对本地/远程结果及远程局部读取 |
| **6 — 更多网格** | 计划中 | 旋转、Lambert、stereographic 等投影网格与 Gaussian N grids；每类有上游定义和真实样本对照，registry 可重生 |
| **7 — 科学计算** | 计划中 | 独立设计 `om_slice`、`om_reduce`、`om_interp`、`om_regrid` |

后续先明确其他维度的语义，再增加远程与并行读取。扩展网格类型后，科学算子独立于扫描入口实现；普通 `read_om` 继续直接输出 DuckDB Vector。

## 已完成部分的验证范围

- Phase 0–2：Linux AArch64 上的 SQL/native、样本重生、投影指标与 sanitizer 已通过，见 [验收记录](../specs/001-local-om-scanner/evidence/final.md)。
- Phase 3：原空间门禁、真实 `ncep_gfswave025` 全域核对和独立复现已在 Linux AArch64 通过，见 [空间验收](../specs/002-spatial-pushdown/evidence/final.md)。Linux x86_64 支持与验证暂缓。
- Domain 扩展：68 项定义的来源和三个 AWS 目录的样本覆盖见 [规则网格 domain](regular-domains.md)。元数据绑定成功不代表同目录所有对象或全量值均已验证。

## 后续实现约束

官方 OM C reader 继续负责格式、物理块选择、字节请求与解码。网格层只提供逻辑切片；DuckDB 保留完整 `WHERE`，无法安全分析的条件回退。新优化需同时证明结果一致和实际读取收益。

当前额外轴保留在数据行中，经纬度列只在配置 grid/domain 时生成；时间等语义列属于 Phase 4。xtensor/xsimd 和科学算子尚未接入。

用户可见接口、支持范围或模块职责变化时，同步更新 [README](../README.md)、[接口说明](spec.md)、[技术架构](architecture.md) 和相应 SQL 契约。
