# 001–006 当前代码核实汇总

日期：2026-10-21。HEAD：`6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`。

> 2026-10-22 最终收口：预算执行与文档漂移已修复并完成三版本定向回归；001 SC-006、003 R21/R22、004 C004-01–04 均已关闭。004 批准的四定义/五对象、三版本/AArch64 H0–H9 与独立 AI 验证通过，见[最终验收](../../../specs/004-multi-grid-selection/evidence/current-20261021/report.md)及[修复映射](../../../evidence/code-audit-remediation-20261021.md)。以下原始结论和待办保留审计时点历史含义，已由本条替代，不追溯改写历史发现；真实 N-grid、2.0、其他平台与全部 domain 不在本轮闭环范围。

六位 `luna_worker`（固定 gpt-6-luna / max）分别直接审阅一个 spec 的当前有效需求、源码调用链和测试。协调者核对证据身份、复查具体发现并补跑定向测试。本轮只新增审计材料，没有修改产品源码、契约、任务勾选或生命周期。

## 结论

已审范围内没有确认高/中严重度运行时缺陷；005 有一项未证明可达超限的区域存储预算检查缺口。主要交付欠账仍是 001 独立复现、003 R21/R22、004 C004-01–04。不能将这些已公开的验收欠账分类为新发现的高严重度代码故障。

| Spec | 源码核实结论 | 剩余事项 | worker 报告 |
| --- | --- | --- | --- |
| 001 | 本地 reader、schema、投影、错误恢复有对应实现；定向检查通过 | SC-006 正式独立复现；实施前文案清理 | [001](spec-001.md) |
| 002 | 规则网格与安全选择有实现；批次与空间 I/O 定向检查通过 | spec 旧 latitude/longitude 名称；不能外推 68 domain 全量验收 | [002](spec-002.md) |
| 003 | 语义轴、HTTPFS、并行及 v4 指标路径存在；定向 SQL 通过 | R21/R22；旧缓存 API/v3 文案清理；按身份核实历史门禁可复用范围 | [003](spec-003.md) |
| 004 | 多网格、布局、选择、source/info 等实现可追踪；定向 SQL 通过 | 当前四定义完整门禁、逐网格远程成本、H8/H9 | [004](spec-004.md) |
| 005 | Polygon 原始点读取、精筛、聚合组合和资源路径有实现；定向检查通过 | 完整 owned-region 预算检查/上界证明；已被替代的成本等文案 | [005](spec-005.md) |
| 006 | nearest 定位、状态、来源、轴与投影路径有实现；三版定向 native/SQL 通过 | 数值契约“待实现”文案；更广网格和平台不在验收范围 | [006](spec-006.md) |

## 复查后的发现

1. **005 预算执行保护缺口，低风险、未复现超限。** `src/scan/region_input.cpp:279` 和 `:321` 检查的 owned 值不含 geometry；`src/scan/read_om.cpp:1529`、`:2653` 加入 geometry/绑定对象估算后只登记指标。契约要求总计 ID/WKB/opaque GEOMETRY/descriptor capacity 在持久化前受 64 MiB 限制。现有 WKB/顶点数量限额也会约束总量，不能认定存在合法超限输入。建议补齐完整上界证明或统一预算检查，再增加对应边界验证；不是已复现内存越界或漏洞。
2. **003 现行 SQL 文档可误导调用。** `specs/003-dimensions-remote-parallel/contracts/sql-interface.md:80` 起仍列已删除的 cache settings/clear_cache 和旧 v3 指标。当前实现与修订需求一致，需同步文档。
3. **002/006 状态文字漂移。** 002 FR-004 保留旧列名，当前 schema 使用 lat/lon；006 numeric-and-capability 开头仍称待实现。001 以及 005 的部分早期文字也仍在，必须遵循显式 supersession，不能把历史记录直接算作当前缺陷。
4. **003/004 验收闭环仍缺。** 不因 005/006 已验收而代签其前置 spec。003 的历史根目录 final 已有本地五轮 1/2/4 测量，R20 摘要已有远程 1/2/4 正确性；剩余重点是身份适用性、当前契约映射和独立复现，不能误报完全没有并行证据，也不能新增必须远程五轮收益的要求。

## 本轮实际验证

- 源码摘要、三版九个核心产物、九份 gate/regression manifest、336 对路径/哈希、157 个去重命令产物匹配。306 条留存命令 exit code=0 是历史记录检查，不是重跑。详见 [身份复核](identity-check.md)。
- worker 逐项报告列出定向 native 和 SQL 检查。004 报告在上下文交接后没有保留全部原始 argv，因此不把其全部测试声明当成完整可重放运行包。
- 协调者额外实际执行三版本 `spatial_io_test`、`spatial_lifecycle_test`、`spatial_metrics_test`，9/9 通过；固定样本结果 10541→25 行，temperature decoded chunks 275→5；三版 bytes 分别为 37700→1433、1438、719。空/坐标/count 零值读取及失败恢复检查通过。命令及输出见 [spatial JSON](parent-spatial-tests.json)。
- 为弥补 003/004 的执行记录不足，协调者实际执行 v1.5.4 的 semantic_axes、axis_filter、parallel_scan、cache_metrics、multi_grid、grid_selection、grid_info 七个 SQL 文件，合计450断言通过。见 [SQL JSON](parent-sql-tests.json)。
- `git diff --check` 通过，未重建共享 build。

## 下一步建议

先处理 005 预算证明/检查与当前接口文档漂移；随后完成 003 R21/R22，并推进 004 当前 C004-01–04。若要新立项，窄范围规则网格 bilinear 仍是合理候选，series 是否提前由实际跨对象时间窗需求决定。本次审计没有发现需要推翻既有架构或重新实现全部已完成 spec 的依据。

未重跑全套 sanitizer、全部远程服务/私有真实输入或所有 spec 全量门禁。本报告是当前源码审计与定向验证，不是一次新的完整发布验收。
