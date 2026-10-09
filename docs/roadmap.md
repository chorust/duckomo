# duckomo Roadmap

本地扫描、列裁剪和规则网格空间查询（Phase 0–3）已实现。Phase 4–5 的维度、远程与并行功能已有实现，但完整验收尚未通过。Phase 6 已形成 [004 多网格 spec](../specs/004-multi-grid-selection/spec.md)，当前实现和真实样本/独立验证仍在进行。当前行为见 [接口说明](spec.md)，模块边界见 [技术架构](architecture.md)。

## 阶段

| 阶段 | 状态 | 交付物 / 验收条件 |
|---|---|---|
| **0 — 原型** | 已完成 | `read_om_raw` 接入官方 OM C reader；本地根数组与官方参考结果一致 |
| **1 — 本地扫描器** | 已完成 | `read_om` 支持元数据、层级变量和稳定 schema；通过 coordinates 或完整 dimensions 对齐多变量 |
| **2 — 列裁剪** | 已完成 | 只读取输出与过滤依赖；结果与全扫一致；`COUNT(*)` 无值 index/data 读取 |
| **3 — 空间选择** | 已实现 | 显式规则网格和 68 个登记 domain；安全经纬度条件缩小逻辑读取；固定多块样本的数据字节与解码块数严格下降 |
| **4 — 其他维度** | 功能已实现；G0–G2 本地验收通过 | time、level、lead_time、member、run 映射；验证混合谓词、维度顺序和空结果 |
| **5 — 远程与并行** | 部分实现；完整验收未通过 | HTTP/S3 文件系统、任务切分、缓存和 profiling；完成跨来源、远程性能、缓存/故障审计及独立复现 |
| **6 — 更多网格** | 实施中；完整验收未通过 | 旋转、Lambert、stereographic 与 Gaussian 定义、局部空间选择、source/grid-info；每类真实坐标/值、远程收益和独立复现门禁按 [004 spec](../specs/004-multi-grid-selection/spec.md) 完成 |
| **7 — 科学计算** | 计划中 | 独立设计 `om_slice`、`om_reduce`、`om_interp`、`om_regrid` |

Phase 4 的语义维度和本地筛选已实现。Phase 5 的远程、并行与缓存代码也已提供；远程 G3、G5、G6 和独立复现 G7 尚未通过，G4 目前只有本地部分结果。Phase 6 的 version=1 新网格与 source/grid-info 实现在当前工作树中；三类真实投影样本已生成独立数学坐标和官方 OM C 全量值参考，但 DuckOMO 对照与实际源轴映射仍未验证。N160/N320/N 区域真实 Gaussian 样本与点序参考、受控远程收益、完整 memory ledger、2.0 matrix 和独立复现也未关闭。公开桶的真实 HRES O1280 是补充 Gaussian 证据，不能替代冻结 N-grid 验收对象。科学算子仍独立于扫描入口实现；普通 `read_om` 继续直接输出 DuckDB Vector。

## 已完成部分的验证范围

- Phase 0–2：Linux AArch64 上的 SQL/native、样本重生、投影指标与 sanitizer 已通过，见 [验收记录](../specs/001-local-om-scanner/evidence/final.md)。
- Phase 3：原空间门禁、真实 `ncep_gfswave025` 全域核对和独立复现已在 Linux AArch64 通过，见 [空间验收](../specs/002-spatial-pushdown/evidence/final.md)。Linux x86_64 支持与验证暂缓。
- Phase 4–5：Linux AArch64 本地 G0–G2 已通过；G4 仅有本地部分结果。G3、G5、G6 的远程验收和 G7 独立复现尚未完成，故不能据此宣称 Phase 4–5 完整交付。命令、构建/样本身份和各门禁状态见 [当前验收记录](../evidence/003-dimensions-remote-parallel/final.md)。
- Phase 6 / spec 004：本地 synthetic native/SQL 与有限 source-object metadata/value 记录已有；真实投影/Gaussian 完整坐标值、N160/N320/N 区域样本、H2/H4/H5 完整 gate、签名 S3/HTTP 服务端审计、版本矩阵和未参与实现者复现未闭环。逐定义状态见 [网格证据表](grid-domains.md)，任务状态见 [004 tasks](../specs/004-multi-grid-selection/tasks.md)。
- Domain 扩展：68 项定义的来源和三个 AWS 目录的样本覆盖见 [规则网格 domain](regular-domains.md)。元数据绑定成功不代表同目录所有对象或全量值均已验证。

## 后续实现约束

官方 OM C reader 继续负责格式、物理块选择、字节请求与解码。网格层只提供逻辑切片；DuckDB 保留完整 `WHERE`，无法安全分析的条件回退。新优化需同时证明结果一致和实际读取收益。

`read_om` 可输出 `time`（沿用 `valid_time` 名称）、`level`、`lead_time`、`member` 和 `run` 语义列；经纬度列仍只在配置 grid/domain 时生成。Phase 4–5 的全量发布声明须等待远程门禁、完整性能与独立复现证据。xtensor/xsimd 和科学算子尚未接入。

用户可见接口、支持范围或模块职责变化时，同步更新 [README](../README.md)、[接口说明](spec.md)、[技术架构](architecture.md) 和相应 SQL 契约。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
