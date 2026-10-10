# 2026-10-10 Gaussian 验收范围修订

## 决定与适用范围

项目负责人在本轮审查中明确决定：“因为 openmeteo 没有这样的数据所以我选择跳过”，并批准同步修复规格、契约、任务与证据声明。本轮只使用已有 Open-Meteo 公开 OM 对象，不要求转换其他来源格式。

- N160、N320 全域及 `n320_ecmwf_aifs_europe_ensemble` 区域的真实对象取得、独立真实坐标/点序/值以及依赖这些对象的远程验收，**本轮跳过，不阻塞 Phase 6 当前范围收口**。原因是已记录的公开来源调查未找到匹配对象，不是证明全球或未来永远不存在此类数据。来源见 [完整目录调查](../evidence/baseline-local/open-meteo-s3-catalog-metadata-20261008-proxy-normalized.md) 和 [样本获取记录](../evidence/baseline-local/open-meteo-s3-sample-acquisition.md)。
- N-grid 定义、身份、完整行表/区域段及 native/synthetic 回归保留，等级仍为 `definition-recorded`；跳过不等于真实验证通过，也不删除历史失败或 `not-run` 记录。
- `ecmwf_ifs` O1280 改为本轮 Gaussian 家族的真实对象验收目标。它有自己的定义身份，不能重标为 N160/N320。指定目标不等于已验收；O1280 自身的独立坐标/点序、显式/domain 查询、空间/时间筛选及远程收益等仍须按原证据标准完成。

本修订覆盖原 spec FR-001、FR-027、SC-001 中 N-grid 真实样本的本轮必选范围，以及 plan、tasks、H0/H1/H6 等门禁中依赖这些对象的条件。原文作为历史要求保留，冲突时以本修订为准。H2–H5/H7–H9 的其他条件、投影对象、完整内存审计及独立复现要求不降低；N-grid 缺口不再阻塞，但 O1280 或其他当前范围缺口仍可阻塞。

## 证据边界与当前状态

继续使用既有四级：`definition-recorded`、`metadata-checked`、`coordinate-value-validated`、`remote-benefit-validated`。不新增 `value-validated`。

~~`ecmwf_ifs` 当前为 **`metadata-checked`**~~（2026-10-10 当日后续决定：按 [pinned producer 参考决定](pinned-producer-reference-20261010.md)，`ecmwf_ifs` 与三个投影定义登记为 **`coordinate-value-validated`**；下文窄范围值证据说明保留），原文：

- 历史 [sample-manifest.json](../../../test/data/grids/sample-manifest.json) 固定了 HSURF 和 temperature chunk_817 的 URI、下载字节 hash、shape/chunks。
- 历史 [value-reference-manifest.json](../../../test/data/grids/value-reference-manifest.json) 的 supplemental 记录包含 HSURF 的 6,599,680 个 logical-index 值、零 mismatch、原始命令及 engine/extension/output hash。比较没有绑定新 domain，不证明坐标或真实对象点序。
- 新登记的 2560 行由 pinned 源码 Float32 规则生成；native 和 HRES 端到端合成测试只证明实现回归，不是独立 GRIB 坐标或真实对象轴点序参考。
- chunk_987 未进入冻结样本清单；本次不保留其“真实 domain/空间/时间查询已验收”的声明。chunk_817/HSURF 的新本地真实 domain/空间/抽样时间查询已在 [o1280-real-local-20261010](../evidence/baseline-local/o1280-real-local-20261010/final.md) 补留完整可审计执行记录（冻结输入/构建、完整 SQL、退出码、逐 scan v4、官方值与 producer 坐标比较），但独立原始 GRIB 扫描/点序到 OM 值对应仍未建立，完整时间值覆盖与远程/复现 gate 未闭环。真实 HSURF 内嵌 `lat lon` 轴名，本轮显式登记 `flattened_axis_alias="lon"` 修复绑定，不放宽冲突元数据断言。`build_pair=null` 明确表示没有绑定本次注册验收的构建组合，不能用历史值比较或本轮本地开发的构建身份填充它。

原始冻结 manifests 及历史 evidence 不追溯改写，其旧 supplemental/N-grid 范围描述以本修订解释；原 hash 和执行结果不变。将来运行当前范围 gate 前须重新冻结新的输入、对象/定义映射、构建组合、容差、完整 SQL、命令/退出码、结果/oracle hash、metrics 和适用服务端审计。现有 runner 如仍要求旧 N-grid 输入，保留其 `not-run`/非零结果，不能仅靠本次文档修订改成 pass。

## 状态记录

- 跳过是有负责人批准的范围决定，不是新增 gate 成功状态。现有机器 gate 状态仍只接受 `pass` / `fail` / `not-run`；旧 N-grid 记录维持 `not-run` 并引用本修订说明本轮不适用。
- 2026-10-10 补充负责人决定：「可以认为一致，按照 Open-Meteo 自己的 Swift 项目就是这样的」——pinned producer 源码经独立移植逐点核对后接受为 O1280 与三个投影定义的坐标/点序参考（限 Open-Meteo 自产对象），升级范围与语义边界见 [pinned-producer-reference-20261010.md](pinned-producer-reference-20261010.md)。
- T004 等含其他参考/映射工作的任务继续开放；不以豁免关闭未完成的 O1280 或投影工作。完整 Phase 6 仍为 `in-progress`，不标 `verified`。
- Phase 8 跨 chunk 入口仅保留为待评审提案；本修订不批准其接口、默认跳过 404 政策或实施范围，当前 `read_om` 单对象契约不变。
