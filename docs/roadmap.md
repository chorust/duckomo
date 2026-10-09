# duckomo Roadmap

本地扫描、列裁剪和规则网格空间查询（Phase 0–3）已实现。Phase 4 的维度语义已实现；Phase 5 的官方 HTTPFS 远程与并行路径已通过 v1.5.4/v1.5.5/v1.5.6 的受控验证，独立复现仍待完成。Phase 6 的 [004 多网格](../specs/004-multi-grid-selection/spec.md) 功能已实现，逐网格真实样本和完整验收仍在进行。当前行为见 [接口说明](spec.md)，模块边界见 [技术架构](architecture.md)。

## 阶段

| 阶段 | 状态 | 交付物 / 验收条件 |
|---|---|---|
| **0 — 原型** | 已完成 | `read_om_raw` 接入官方 OM C reader；本地根数组与官方参考结果一致 |
| **1 — 本地扫描器** | 已完成 | `read_om` 支持元数据、层级变量和稳定 schema；通过 coordinates 或完整 dimensions 对齐多变量 |
| **2 — 列裁剪** | 已完成 | 只读取输出与过滤依赖；结果与全扫一致；`COUNT(*)` 无值 index/data 读取 |
| **3 — 空间选择** | 已实现 | 显式规则网格和 68 个登记 domain；安全经纬度条件缩小逻辑读取；固定多块样本的数据字节与解码块数严格下降 |
| **4 — 其他维度** | 功能已实现；G0–G2 本地验收通过 | time、level、lead_time、member、run 映射；验证混合谓词、维度顺序和空结果 |
| **5 — 远程与并行** | 官方 HTTPFS 实现及三版本受控验证通过；独立复现待完成 | HTTP(S)/S3 标准文件系统、任务切分和 profiling；自有缓存已移除，不承诺热缓存收益 |
| **6 — 更多网格** | 功能已实现；完整验收未通过 | 旋转、Lambert、stereographic 与 Gaussian 定义、局部空间选择、source/grid-info；每类真实坐标/值、远程收益和独立复现门禁按 [004 spec](../specs/004-multi-grid-selection/spec.md) 完成 |
| **7 — 科学计算** | 计划中 | 独立设计 `om_slice`、`om_reduce`、`om_interp`、`om_regrid` |

Phase 4 的语义维度和本地筛选已实现。Phase 5 已切换为官方 HTTPFS，跨来源结果、1/2/4 worker、冷局部收益、访问变化、故障恢复、取消及隔离有三版本受控记录；旧自有缓存及其 SQL 已删除，旧 G5 为 superseded，历史失败不改写。Phase 6 的 version=1 新网格与 source/grid-info 已实现；三类真实投影样本的全量坐标/值子比较及 source-only 辅助检查已通过，但独立 producer 源轴/点序证明和完整 H1/H2 尚未通过。N160/N320/N 区域真实 Gaussian 样本与点序参考、逐网格远程收益、完整 memory ledger 和独立复现也未关闭。2.0 开发版已退出本轮范围。公开桶的真实 HRES O1280 是补充 Gaussian 证据，不能替代冻结 N-grid 验收对象。科学算子仍独立于扫描入口实现；普通 `read_om` 继续直接输出 DuckDB Vector。

## 已完成部分的验证范围

- Phase 0–2：Linux AArch64 上的 SQL/native、样本重生、投影指标与 sanitizer 已通过，见 [验收记录](../specs/001-local-om-scanner/evidence/final.md)。
- Phase 3：原空间门禁、真实 `ncep_gfswave025` 全域核对和独立复现已在 Linux AArch64 通过，见 [空间验收](../specs/002-spatial-pushdown/evidence/final.md)。Linux x86_64 支持与验证暂缓。
- Phase 4–5：[003 历史验收记录](../evidence/003-dimensions-remote-parallel/final.md) 保留原 G0–G7 状态；当前官方 HTTPFS 路径的构建/本地回归、每版本 48 项远程 runtime、固定真实样本跨来源一致性、访问变化及性能结果见 [迁移验证记录](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md)。Linux AArch64 的 v1.5.4/v1.5.5/v1.5.6 受控验证已通过，R21 独立验证者复现未执行，roadmap 不提升为 verified。
- Phase 6 / spec 004：本地 synthetic native/SQL 与有限 source-object metadata/value 记录已有；真实投影/Gaussian 完整坐标值、N160/N320/N 区域样本、H2/H4/H5 完整 gate、逐网格签名 S3/HTTP 服务端审计、完整 H8 和未参与实现者复现未闭环；HTTPFS 三版本 runtime 通过不替代这些门禁。逐定义状态见 [网格证据表](grid-domains.md)，任务状态见 [004 tasks](../specs/004-multi-grid-selection/tasks.md)。
- Domain 扩展：68 项定义的来源和三个 AWS 目录的样本覆盖见 [规则网格 domain](regular-domains.md)。元数据绑定成功不代表同目录所有对象或全量值均已验证。

## 后续实现约束

官方 OM C reader 继续负责格式、物理块选择、字节请求与解码。网格层只提供逻辑切片；DuckDB 保留完整 `WHERE`，无法安全分析的条件回退。新优化需同时证明结果一致和实际读取收益。

`read_om` 可输出 `time`（沿用 `valid_time` 名称）、`level`、`lead_time`、`member` 和 `run` 语义列；经纬度列仍只在配置 grid/domain 时生成。全量发布声明仍须区分已通过的官方 HTTPFS 受控验证和未完成的逐网格/独立复现门禁。xtensor/xsimd 和科学算子尚未接入。

用户可见接口、支持范围或模块职责变化时，同步更新 [README](../README.md)、[接口说明](spec.md)、[技术架构](architecture.md) 和相应 SQL 契约。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
