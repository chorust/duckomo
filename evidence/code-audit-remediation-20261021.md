# 2026-10-21 审计问题修复与剩余验收

已修复审计总结中的区域预算执行缺口与现行文档漂移。**2026-10-22 当前状态：001 SC-006、003 R21/R22、004 C004-01–04 全部关闭；004 当前批准范围 H0–H9 通过。** 001 依据[独立 AI 复现及父级封存复核](../specs/001-local-om-scanner/evidence/independent-sc006-20261021.json)，003 依据[现行交付映射](003-dimensions-remote-parallel/current-delivery-20261021.md)，004 依据[最终 H0–H9 及独立 AI 验收](../specs/004-multi-grid-selection/evidence/current-20261021/report.md)。独立验证为 AI、非人工，适用版本/平台以各记录为准。以下正文和原始审计保留修复阶段历史含义。

后续验收已继续推进，以下正文保留修复阶段的执行时点。003 当前三版本工程补跑、真实对象完整跨来源结果及证据哈希见 [当前交付映射](003-dimensions-remote-parallel/current-delivery-20261021.md) 与 [机器核对记录](003-dimensions-remote-parallel/current-engineering-20261021.json)；末尾“未重启服务/未重跑远程”仅描述早先修复阶段。2026-10-21 后续复核确认本报告记录的 180 份产物哈希、当前源码摘要及三版本新扩展身份一致。独立验收结果另行留档。

本轮基线 HEAD 为 `6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`，未提交或发布。修改后源码摘要为 `5e73cfe7ca159159c61cc8bab98cc2a84f02a9e18439e051596a9f4b57d51f4c`。完整命令、退出码、日志和产物 SHA256 在 [机器记录](code-audit-remediation-20261021.json)。

## 代码修复

`RegionCollection::GetOwnedBytes()` 现在包含区域描述符、ID/WKB capacity 和逐区域 opaque geometry 保守估算。解析完成、复制持久集合前检查合计估算，复制后按实际容器 capacity 复核。`ValidateOwnedBytes(binding_bytes)` 对同一总量执行 64 MiB 上限及 uint64 加法溢出检查。

binder 在保留各区域的 predicate/geometry 前检查表达式与预留容器容量；最终绑定和绑定对象复制再次核对。指标复用同一集合计量，geometry 不重复相加。超过上限以 `[region_input_limit]` 拒绝；临时对象由 RAII 清理。64 MiB 仍是 DuckOMO 自有区域存储的预算，不包含无法计量的 spatial/GEOS/HTTPFS 内部分配。

新增测试先在缺少 geometry 计量的实现上失败（`red.txt`，exit 1），修复后通过；覆盖合计计量、恰好 64 MiB、超出 1 字节与 uint64 溢出。边界用例对生产预算检查函数注入 binding capacity，**不是已找到合法 WKB 能实际越限的复现**；原审计“未复现合法超限”的限制保持。

## 文档同步

- 001：去掉“只有设计、尚未实现”的说明；状态为 implemented，AArch64 为当前平台，SC-006 独立复现仍开放。
- 002：FR/场景与可执行 quickstart 同步 `lat/lon`，保留旧证据与全部 domain 验收的范围边界；两个合成 SQL 示例实际执行通过。
- 003：SQL 契约删除现行缓存设置/clear_cache 用法，改为 metrics v4、官方 HTTPFS 与远程 transport NULL/false；US5 和默认假设同步现行契约。新增 [当前交付映射](003-dimensions-remote-parallel/current-delivery-20261021.md)，历史 final 顶部链接新入口。
- 004：历史 final 顶部明确当前四定义、三版本及 C004-01–04 的入口，不把旧 2.0/N-grid 范围当作当前前置。
- 005：SC-004 与 gate 表同步已批准的“三项均不增加且至少一项下降”，当前 sanitizer 为 17 项；roadmap 清除当前行中旧 implemented/16 项/严格下降矛盾，历史 final 正文保留并明确 decoder 已支持范围复用。
- 006：numeric-and-capability 标为已实现的现行契约，能力范围依最终验收声明，不再称待实现。

## 本轮实际验证

| 版本 | 原生回归 | SQLLogicTest | 输入 ASan/UBSan | 官方 CLI 本地检查 |
| --- | --- | --- | --- | --- |
| v1.5.4 | region_input / region_scan / region_sampling 全通过 | 11 文件 / 764 断言 | 通过（含 leak detection） | 9 项通过 |
| v1.5.5 | 同上 | 11 文件 / 764 断言 | 通过（含 leak detection） | 9 项通过 |
| v1.5.6 | 同上 | 11 文件 / 764 断言 | 通过（含 leak detection） | 9 项通过 |

SQL 文件为 regions、region_sampling、两份 spatial dependency、cache_metrics、multi_grid、grid_selection、grid_info、semantic_axes、axis_filter、parallel_scan；总计 2,292 断言。区域 native 包含共享 geometry payload 计量、零值 I/O、取消、短读/解码/对象变化失败恢复和连接隔离。

构建方式为隔离目录 `build/audit-remediation-20261021/<version>/`：重编译变更的 read_om/region_input 和输入测试，使用未变化的固定版本引擎/基础对象重新链接，写入新的源码 build identity，不覆盖共享历史产物。输入 sanitizer 重编译 parser/test 加入 ASan/UBSan，固定 DuckDB 静态库未重新插桩。**这是一轮定向回归，不是 clean full rebuild、完整 sanitizer 集合、P0–P7 或 H0–H9 新验收。**

| 版本 | 本轮 build identity |
| --- | --- |
| v1.5.4 | `fb49547ec3531e42eeb415ba7cf985989b57a4e1853f48425fc3ec187ca5f245` |
| v1.5.5 | `4eff09a4b1bdc8f283fea803e2e895a3062e9231957ad38a1e1d5fc591198e3f` |
| v1.5.6 | `ba5a3e7b3f284dd8ad33fe269baf714f3be6ea5e9f949564e4afa3ac7b3e92c6` |

初始 SQL 调用将临时 `.test` 放到 runner 不枚举的目录，exit 0 但显示 `No test cases matched`。这些记录已明确标为无效验证；随后创建含 `test/sql` 的隔离工作目录，重跑并强制要求 `All tests passed`，上表只统计实际断言。原始输出全部保留。

## 修复阶段的验收事项（历史快照）

| 事项 | 当前具体条件 |
| --- | --- |
| 001 SC-006 | 未参与实现者按指南运行 LOAD、DESCRIBE、完整/单列查询，保存实际输入/产物身份、命令/退出码；本次实现者自测不代签 |
| 003 R21 | 未参与实现者按官方 HTTPFS 指南独立复现，绑定当前实际产物和远程服务日志 |
| 003 R22 | 已新增映射，但依赖 R21 与历史 R20/本地五轮证据的当前适用性核验，故 checkbox 不关闭。已有本地五轮收益、远端 1/2/4 正确性，不新增远端五轮收益要求 |
| 004 C004-01 | 当前四定义的完整 H1/H2/H3/H7 及缺项补跑；坐标/值登记和本次 SQL 不能代替完整 gate |
| 004 C004-02 | 每类网格的 HTTP/HTTPS/签名 S3 完整结果、严格成本、H5/H6 协议/权限/版本/恢复与服务端审计。005 的成本修订不适用于 004 |
| 004 C004-03 | 当前三版本相同冻结输入 H0–H7 完整结果及 H8 适用性审计；本次修改局部编译/回归不代替完整矩阵 |
| 004 C004-04 | 未参与实现者完成 H9，再据真实范围形成最终支持声明 |

已有 `current-20261020-tls-fixed` 及其 v1.5.4 run.env 对应的 HTTP/HTTPS 端点均实际探测为 connection refused；本轮未重启远程 fixture、未运行真实逐网格成本，也未使用私有输入重签验收。R20 runtime 保留扩展路径/hash，build.json 的 build_id 与 release_dir 含不同身份，须核对实际产物再判断继承范围。所有旧记录继续保留，005/006 的验收不代签 003/004。
