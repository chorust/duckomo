# Spec 003 当前 HEAD 代码一致性审计

审计日期：2026-10-21（Asia/Shanghai）  
仓库：`/home/blizhan/repo/github/duckomo`  
HEAD：`6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`（`perf: coalesce contiguous reads into rectangular batches`）  
范围：只读核对 spec 003 当前有效 FR/SC、实现调用链、相关测试和验收证据；没有改仓库源码、任务勾选、roadmap 或提交。

## 结论摘要

- 已检查的实现路径覆盖语义轴绑定、坐标筛选、值投影/解码、标准 HTTPFS 读取、并行任务/取消和 v4 查询指标。对本轮已读代码和定向测试，**没有确认到 `6b34e52` 矩形批次合并导致的结果错位、重复、遗漏或越界缺陷**。这只适用于已检查路径，不构成全量验收。
- **中等严重性文档漂移**：当前 FR-015 和官方 HTTPFS 修订已移除 DuckOMO 自有跨查询缓存及其 SQL API，但 003 SQL 契约仍把已删除的缓存设置、`duckomo_clear_cache()` 和 metrics v3 写成现行接口；spec 的 US5 验收场景也仍承诺可配置自有缓存。实现和负向测试与现行 FR-015 一致，文档没有同步。
- **验收证据缺口（交付状态，不是运行时缺陷）**：当前有效待办 R21、R22 明确未完成。仓库根目录 `evidence/003-dimensions-remote-parallel/final.md` 日期为 2026-10-02，身份指向旧基线 `50b51787a480d4abf11e45488fa44e5c60fc6394`，不能证明 2026-10-21 HEAD。R22 所要求的逐项绑定当前源码/构建/命令/退出码的 003 最终映射仍未完成，因此不能宣称 SC-001–008 全部通过。
- SC-005 有历史证据：仓库根目录 `evidence/003-dimensions-remote-parallel/final.md:37-49` 记录固定本地样本串行/并行各五轮、1/2/4 worker 中位数，并记录两组并行更快；004 官方 HTTPFS `status.md` 也包含远程 1/2/4 worker 正确性结果（R20 汇总）。尚未完成的是这些历史证据与当前 HEAD、源码 digest 及现行修订契约的 R22 映射。SC-005 不要求远程五轮性能收益。

## 证据身份与范围边界

1. 当前 `HEAD` 与分支由 `git rev-parse HEAD`、`git status --short --branch` 确认。工作树另有协调者/其他 worker 的未提交内容：`.specify/extensions/roadmap`、`.specify/memory/code-audit-20261021/`、`.specify/memory/roadmap-sync-20261021-next-directions.md`；本审计未读取或修改这些协作文件。
2. 协调者提供的当前扩展源码 digest 为 `886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e`，并说明它匹配 005 最新验收。父会话核对的是 005 三版本身份及 336 组文件路径/哈希；本审计未重复该核对。父会话未核对 R20 原始包，本审计也未核对；004 status 在此仅作为汇总证据引用。005 验收不等同于 003 全部 SC 验收。
3. 可复用 release 目录来自 `build/evidence/regions/current-20261020/matrix-testfixed/v1.5.4/build-manifest.json`，其中 `release_dir` 为 `build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release`。该 manifest 的顶层字段不含 `commit` 或 `extension_source_digest`；构建与当前源码的 digest 对应关系以协调者提供的身份核验为准。
4. 当前有效任务索引以 `specs/003-dimensions-remote-parallel/tasks.md:8-15` 为准：旧专用 ABI、observer、自有 LRU、缓存 SQL 和旧 G5 已 superseded；R21 独立复现、R22 最终证据映射仍标为未完成。旧 T001–T073 checkbox 和仓库根目录 `evidence/003-dimensions-remote-parallel/final.md` 只保留历史状态。
5. 仓库根目录 `evidence/003-dimensions-remote-parallel/final.md` 第 3–4 行标明 2026-10-02 和旧源码基线 `50b5178`；第 26–35 行记录当时 G0–G2 本地通过、G3 未通过、G4 仅本地部分通过、G5/G6/G7 未通过；第 37–49 行另记录本地固定样本五轮 1/2/4 worker 中位数及两组并行更快。004 的 `specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md:15-35` 是较新的 2026-10-08 汇总，记录 v1.5.4/1.5.5/1.5.6 各 48 项官方远程 runtime、固定真实样本与访问变化结果，并包含远程 1/2/4 worker 正确性结果；第 33 行仍明确 R21 独立验证者复现未执行。该汇总和旧 final 均未替代 R22 对当前 HEAD 与现行 FR/SC 的逐项映射。R20 原始包身份未由父会话或本审计核验。
6. 依当前 spec 修订判断：`spec.md:131-134,157,175-177` 与 `specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md:3-23` 生效。旧缓存 G5 的能力要求已撤销/替换为官方 HTTPFS O5；不能把“未实现旧 LRU”当成当前代码缺陷，也不能把旧 G5 失败改称通过。

## 逐项需求矩阵

状态含义：**代码吻合（局部证据）**表示读到的实现路径与现行 FR 一致，当前定向测试只能覆盖部分行为；**部分/未核实**表示实现可见但完整门禁、当前来源对照或要求的成本证据不足；**文档漂移**表示当前实现符合现行修订、但 003 文档仍写旧行为。

| 需求 | 当前实现与调用链 | 对应测试/证据 | 判定 |
|---|---|---|---|
| FR-001 五语义轴；显式声明或有依据的文件证据，不靠 shape/path 猜测 | `src/scan/semantic_axes.cpp:140-146,295-389,462-501` 解析五种语义并绑定到已声明逻辑轴；`src/scan/read_om.cpp:2557` 将 axes 声明绑定；时间文件证据校验在 `semantic_axes.cpp:378-389`。自动文件语义仅覆盖既有 time/valid_time，不推测其他四类。 | `test/native/semantic_axes_test.cpp:47-75`；`test/sql/semantic_axes.test:12-85,87-123`。 | **代码吻合（局部证据）**；五语义完整覆盖矩阵未在本轮重跑。 |
| FR-002 轴身份、坐标、类型/单位；规则/显式坐标；一一映射与长度 | `semantic_axes.cpp:149-233` 处理规则坐标和范围检查；`234-294` 处理显式坐标；`295-375` 校验字段、长度及 level 元数据；`462-501` 拒绝重复语义或同轴多映射。 | `semantic_axes_test.cpp:47-75`；`semantic_axes.test:125-200` 覆盖规则/显式、越界/长度及重复映射错误。 | **代码吻合（局部证据）**。 |
| FR-003 UTC/时长/level/member 的含义和精度 | `semantic_axes.cpp:64-123,160-293` 对 timestamp、INTERVAL 月分量、数值 round-trip、int64 member 做类型与范围校验；`410-447` 生成坐标值。level kind/unit 白名单在 `149-158`。 | `semantic_axes.test:87-123,125-143` 覆盖文本 member、压力层、UTC 偏移、负 lead time、重复值。 | **代码吻合（局部证据）**；所有精度边界组合未完全核实。 |
| FR-004 在返回数据前拒绝非法/冲突声明、列名冲突和不兼容布局 | `semantic_axes.cpp:305-325,340-389` 校验已声明轴、多变量一致布局及文件 time 冲突；`src/scan/read_om.cpp:2739-2759` 拒绝输出列名冲突；失败发生在 bind。 | `semantic_axes.test:145-200` 覆盖时间重复声明、非法单位、model level、长度及同轴多语义错误；`semantic_axes_test.cpp:75-139` 覆盖有限/损失校验。 | **代码吻合（局部证据）**；列名与多变量错误的全部分支未在本轮逐个执行。 |
| FR-005 语义坐标与逻辑行对齐；保留重复/未映射轴；默认行为不变 | `semantic_axes.cpp:410-447` 从 row-major 逻辑位置计算轴坐标；`read_om.cpp:2722-2759` 仅追加启用语义列；`ProjectionPlan`/scan 使用同一逻辑位置解码值。 | `semantic_axes.test:47-85` 对比轴顺序变体；`axis_selection_test.cpp:96-103` 验证所有逻辑位置一次覆盖；`batch_test.cpp:83-115` 验证批次覆盖无缺口/重复。 | **代码吻合（局部证据）**。 |
| FR-006 轴常量比较、BETWEEN、安全合取缩小候选 | `src/scan/axis_filter.cpp:22-41,72-82,137-188,205-210` 提取比较、反转常量左比较、BETWEEN 与 AND；`axis_selection.cpp:52-165` 形成每轴候选区间；`read_om.cpp:1618-1644,1773` 将提取条件接入扫描。 | `test/sql/axis_filter.test:12-40` 覆盖常量左比较、BETWEEN 和合取；`axis_selection_test.cpp:51-95,118-143` 覆盖组合、空结果与预算回退。 | **代码吻合（局部证据）**。 |
| FR-007 空间/值过滤、投影、聚合/表达式与完整 SQL 条件 | `read_om.cpp:1618-1654` 观察过滤并抽取必要条件；`GetReadOmFunction()` 在 `3660-3674` 开启 projection pushdown、关闭普通 filter pushdown，并用复杂过滤回调记录/提取条件，使 DuckDB 仍执行完整残余 WHERE；`axis_filter.cpp:190-199` 对 OR/函数等不安全条件记 fallback。 | `axis_filter.test:43-101` 覆盖 OR、函数、矛盾谓词及空间+时间交集；`test/sql/spatial_composition.test`、`test/sql/axis_filter.test` 对应混合筛选。 | **代码吻合（局部证据）**；本轮未对所有聚合、列重排和表达式组合重跑完整集。 |
| FR-008 空/坐标/count 不读值变量及分别计量 | `read_om.cpp:1718-1745` 构建投影、按需要变量决定是否要值解码；`3401-3415` 无值变量时不生成解码 segments；`metrics` 将 coordinate 与 value 读取分开记录（见 `src/include/duckomo/metrics.hpp:588-620`）。 | 旧 003 final `:28` 记录过 G2 零值 I/O，但身份是 2026-10-02 旧基线；本轮测试清单未包含 `grid_zero_io_test`。 | **部分/未核实当前 HEAD 零值 I/O 全集**。 |
| FR-009 固定多块样本上局部查询减少值字节和解码块，混合结果对独立基准 | 候选位置经 `axis_selection.cpp` 生成；`read_om.cpp:3401-3415,3495-3518` 只对选择位置建 segments 并调用 OM reader。`6b34e52` 的 `batch.cpp:26-51` 合并最大连续矩形前缀。 | 旧 G2 数据见旧 final `:28`，不是当前 HEAD 证明；本轮 `reader_capacity_test.cpp:309-342` 验证矩形读取与逐行官方解码逐位相同、chunks 35→5。 | **部分**：代码路径与局部 reader 测试通过；没有在当前审计中重跑完整固定样本的独立 oracle、字节和解码双降幅门禁。 |
| FR-010 单 HTTP(S)/S3 对象、沿用读取语义/权限配置、脱敏 | `src/om/remote_file.cpp:25-59` 解析单对象 HTTP/HTTPS/S3 URI 并脱敏展示路径；`175-205` 使用 DuckDB 标准 opener/FileSystem、`CanAccessFile`、DIRECT_IO 和每 worker 句柄；`read_om.cpp:2492-2497` 接入远程会话。 | `test/native/remote_session_test.cpp:208-254` 覆盖 mock FileSystem、权限、句柄和脱敏；`official_httpfs_test.cpp:29-88` 是真实官方 HTTPFS 集成入口。R20 汇总记录官方远程 runtime，但本轮未启动服务。 | **部分**：源码接入明确；当前端到端三来源运行未由本轮重做，且 003 final 过旧。 |
| FR-011 远程局部读取按需传输、避免预下载，局部响应体小于全扫 | `remote_file.cpp:185-196,210-231` 用标准 DIRECT_IO、NO_CACHING 文件句柄及 OM 精确范围读取；上游协议/缓冲行为归官方 HTTPFS。 | 004 `status.md:25-29` 记录固定样本冷局部服务端发送 5,058 bytes、全扫约 1,107,017 bytes；测量为 1 worker。它是 R20/004 汇总证据，不能代替本轮的完整 SC-005 并行性能证据。 | **部分**：已有远程成本记录，但当前 003 final 未完成逐项新证据映射；没有在此审计独立复跑。 |
| FR-012 不支持/拒绝/短读/失败及可观测对象身份冲突时报错 | `remote_file.cpp:177-231` 检查权限、正长度、范围边界并把 HTTPFS 异常转成脱敏错误；`278-286` 对 bind/worker 可观测 size/version 冲突报错；`reader.cpp:155-210` 校验 OM 文件结构/长度。 | `remote_session_test.cpp:216-249` 覆盖短读/失败、长度/版本变化、取消与恢复；`official_httpfs_test.cpp:43-80` 覆盖真实 HTTP 错误、脱敏、取消、恢复。官方协议逐响应验证由 004 contract `:7-9,21` 归属上游。 | **代码吻合（局部证据）**；真实远程服务未在本轮执行。 |
| FR-013 worker 上限、local/remote 结果相同多重集 | `read_om.cpp:1851-1911` 将 DuckDB threads、`duckomo_max_threads`、可用 task 数取最小；`1957` 暴露 max thread 上限；每 worker local state 在 `2394-2457` 建立。 | `parallel_scan.test:12-78` 比较 1/2/4 worker 完整结果多重集；`parallel_scan_test.cpp:150-245` 验证任务数和 active workers；远程矩阵汇总见 004 status `:17-23`。 | **代码吻合（局部证据）**；本轮没有重跑 HTTP/S3 worker 矩阵。 |
| FR-014 实际多 worker、少任务/空/跨批次正确；失败/取消停止并可恢复 | `read_om.cpp:1858-1911` 以任务可分割量限制 worker；`2275` 起的 `RequestStop`、`ScanReadOm` 异常路径 `3566-3605` 记录失败/取消并请求停止；每批次在完成解码后才提交 cardinality (`3543-3550`)。 | `parallel_scan_test.cpp:247-299` 覆盖取消、坏末块失败及恢复；SQL `parallel_scan.test:57-78` 验证结果；矩形分块边界由 `batch_test.cpp:83-128` 覆盖。 | **代码吻合（局部证据）**；所有跨批次/远端故障排列未由本轮完整矩阵核实。 |
| FR-015 不提供 DuckOMO 跨查询范围缓存或缓存 SQL；使用上游标准行为 | 现行实现不注册旧 cache settings/function；`test/native/session_metrics_test.cpp:35-36,74` 与 `test/sql/cache_metrics.test:12-25,124-137` 断言旧接口报错。现行需求见 `spec.md:131` 和官方契约 `:13,19`。 | 本轮已通过 `cache_metrics.test`（24 assertions）及 `session_metrics_test`。 | **代码符合现行修订；文档漂移**：`contracts/sql-interface.md:80-99` 仍文档化旧 API 和 v3。不要把旧 FR/任务的 LRU 缺失记为实现失败。 |
| FR-016 对象扫描期间稳定；worker 打开时比较可见 size/version；不承诺立即撤权 | `remote_file.cpp:193-196,278-286` 在打开句柄时取得并比较长度/版本；空 token 不伪装为内容一致。`RemoteReadSession::Create/Open` 在 `:263-273`，外部访问权限由标准接口控制。 | `remote_session_test.cpp:216-249` 覆盖强版本变化、无 tag 可读但不声称验证、权限拒绝；官方契约 `:7` 明确不承诺新鲜 HEAD/快照/立即撤权。 | **代码吻合（局部证据）**。 |
| FR-017 查询隔离记录选择/读取/解码/任务/终态/内存；远程 transport unknown，local not-applicable，cache removed | `metrics.hpp:588-620,1147-1170` 区分本地已知 0 与远程 NULL/false，并生成 v4/legacy；`read_om.cpp:99-138,148-178` 按 ClientContext/QueryEnd 发布 scan 快照；`ScanLastMetrics` 在 `3640-3655` 读取快照。 | `scan_metrics_v4_test.cpp:240-264` 覆盖未知字段、legacy v3 嵌入和脱敏；`session_metrics_test.cpp:30-92` 覆盖连接/多 scan 隔离和 QueryEnd；本轮 SQL `cache_metrics.test` 断言外层 v4。 | **代码吻合（局部证据）**。 |
| FR-018 应用读取与服务端发送量分开；失败/取消不当成功成本 | 应用读取计数在 `remote_file.cpp:220-231` 与 `metrics.hpp` 记录；当前官方远程 transport 指标按 `contract.md:19-21` 保持 NULL/unobserved，服务端发送量只由验收侧日志计算。失败/取消终态由 `read_om.cpp:3566-3605` 和 QueryEnd 发布。 | 004 `status.md:27-29` 说明服务端发送量和客户端/内存范围差异；`scan_metrics_v4_test.cpp:240-264` 覆盖未知量不变成 0；取消/失败测试见 `parallel_scan_test.cpp:247-299`、`official_httpfs_test.cpp:73-80`。 | **代码吻合（局部证据）**；本轮未对受控服务器日志重新对账。 |
| FR-019 固定样本/独立 oracle/命令/成本证据覆盖所有关键路径 | 当前有旧 003 final、R20 远程汇总、当前源码针对性回归和其他 spec 的验收证据，但缺 003 当前 final 按当前 digest/HEAD 的 FR/SC 完整映射。 | 当前任务 R21/R22 未完成（tasks `:12-13`）；旧 final `:3-4,26-35,106-117` 明确旧基线及缺失 gate；005 证据身份由协调者核对，只证明其报告范围。 | **未满足完整交付证据要求 / 未核实**。不据 checkbox、旧 final 或 005 状态宣称通过。 |
| FR-020 文档同步实际行为并按证据更新状态 | `docs/spec.md:1,97` 和 `docs/official-httpfs.md:62-64` 已说明移除旧缓存、v4/远程 transport 范围及独立验证条件；当前 roadmap 仍保持未完整验收状态。 | 相反，003 `contracts/sql-interface.md:80-101` 仍写旧 cache API/v3；spec US5 `:85-99` 仍写配置自有缓存；R22 要求的 final/status 更新仍待完成。 | **部分，存在已确认文档漂移**。 |

## Success Criteria 矩阵

| SC | 需求 | 当前证据 | 判定 |
|---|---|---|---|
| SC-001 | 五语义独立坐标对照、轴顺序/非等距/单点/重复等覆盖且 100% 一致 | 实现/定向 SQL/native 测试覆盖常见轴顺序、类型、重复值；旧 final `:27` 曾报旧身份本地 G1 通过。 | **部分/未核实当前 HEAD 全覆盖及 100% oracle**。 |
| SC-002 | 单维/混合/边界/复杂回退与全扫一致，至少固定样本局部读取字节和解码双降 | 当前过滤和 segmentation 逻辑可见；`reader_capacity_test` 仅证明本地矩形合并解码块数下降；旧 final `:28` 是旧基线 G2。 | **部分**；缺当前 HEAD 完整 fixture oracle 和字节+解码组合门禁。 |
| SC-003 | local/HTTP/S3 parity、真实样本、两种远端冷局部 body 均小于全扫 | 004 status `:17-23,27` 记有三版本远端 runtime、固定真实样本及冷局部 body 结果；旧 003 final `:29` 是更早未执行状态。 | **部分**；较新 R20 记录覆盖旧缺口的一部分，但 003 R22 尚未完成证据身份与需求映射，本轮未独立复跑服务。 |
| SC-004 | 可证空/坐标/count 零值 I/O；无关变量零解码 | source projection 分离变量且空选择不创建解码段；旧 final `:28` 有旧 G2 零值 I/O 结果。 | **部分/未核实当前 HEAD 完整测试集合**；本轮没有运行专门 zero-I/O target。 |
| SC-005 | local/remote 1/2/4 worker 正确性；至少一个预先固定样本串/并行各 5 次、中位数并行更快 | 仓库根目录 `evidence/003-dimensions-remote-parallel/final.md:37-49` 记录历史本地样本五轮及 1/2/4 worker 中位数，两组并行更快；004 `status.md` 记录远程 1/2/4 worker 正确性结果（R20 汇总）。SC-005 不要求远程五轮性能收益。 | **有历史证据；当前验收映射未闭合**：尚未在 R22 中把这些证据绑定到当前 HEAD、源码 digest 和现行修订契约；本轮未重新运行。 |
| SC-006 | 官方路径冷/重复完整结果一致，性能变化公开；标准接口错误清晰；旧自有 cache G5 已替换 | 当前 FR `:157` 与官方契约 `:19-23` 将旧 G5 替换；004 status `:27-29` 记冷局部收益和重复查询成本代价，未宣称热缓存优化。 | **部分**：有较新 R20 汇总，但 R21/R22 独立复现和 003 最终映射未完成。 |
| SC-007 | 非法映射、远端异常、对象变化、失败/取消恢复和并发隔离 | 当前 native tests 对非法映射、mock 远端版本变化、失败/取消恢复及 metrics 隔离有覆盖；R20 status `:21` 报有 48 个远端 runtime cases。 | **部分至较强的实现证据**；本轮没有对所有失败类型做完整当前 HEAD 远程运行。 |
| SC-008 | 未参与实现者按指南独立完成四项复现并判断优化/未知指标 | 003 tasks `:12` R21 未勾选；004 status `:33` 明确独立复现尚未执行。 | **未满足**。 |

## `6b34e52` 矩形批次改动专项

改动涉及 `src/include/duckomo/batch.hpp`、`src/scan/batch.cpp`、`src/scan/axis_selection.cpp`、`src/scan/read_om.cpp` 的批次映射/读取链，以及对应 native tests。`BuildContiguousBatchSegment`（`batch.cpp:26-51`）从 row-major 起点只扩展完整的连续 trailing axes；`BuildBatchSegments`（`:54-82`）循环覆盖请求区间；`BuildSelectedBatchSegments`（`axis_selection.cpp:385-425`）先把严格递增位置切成连续 runs，再各自分矩形。调用者在 `read_om.cpp:3401-3415` 形成 segments，`3495-3513` 按 segment 将精确矩形传给 `DecodeSelection` 并写到对应 `batch_offset`。

- `test/native/batch_test.cpp:83-115` 穷举数个 rank 1–8 小 shape 的所有合法 start/count，逐元素验证每段仍精确覆盖所请求的线性位置、无 gap/duplicate，并检查整块轴合并。
- `test/native/reader_capacity_test.cpp:309-342` 对 7 行做合并/逐行解码，要求 float 数据 `memcmp` 相同，且 decoded chunks 为 5 对 35；scratch/output bounds 仍断言不变。
- 本 worker 在本轮中断前实际执行的 native targets 中包含 `batch_test`、`axis_selection_test`、`reader_capacity_test`，均通过。就这些代码路径与测试而言未发现具体回归；未运行 sanitizer 或高维真实网络压力矩阵，因此不把有限单测外推为全量无风险。

## 本 worker 本轮中断前已运行验证

没有重建共享目录，没有运行外部服务、大型矩阵、全量 `make test` 或 sanitizer。下列 8 个 native targets 和 4 个 SQLLogicTest 均由本 worker 在本轮审计中断前实际执行并通过，并非仅读取他人留存结果；报告收敛阶段未重跑：

- Native（8 项）：`batch_test`、`axis_selection_test`、`reader_capacity_test`、`parallel_scan_test`、`semantic_axes_test`、`remote_session_test`、`scan_metrics_v4_test`、`session_metrics_test`，全部通过。
- SQLLogicTest（4 项）：`test/sql/semantic_axes.test`（106 assertions）、`axis_filter.test`（12）、`parallel_scan.test`（11）、`cache_metrics.test`（24），全部通过。
- 测试产物路径由上节 v1.5.4 `build-manifest.json` 的 `release_dir` 提供。native 执行文件位于 `release/test/native/<target>`；SQLLogicTest 由既有 `release/test/unittest` runner 执行。此次报告修订没有运行测试。
- 本轮只读核对命令包括：`git rev-parse HEAD`、`git status --short --branch`、`git show --stat --oneline --decorate --no-renames HEAD`；读取 manifest 的命令为 `python3 -c "import json,pathlib; p=pathlib.Path('build/evidence/regions/current-20261020/matrix-testfixed/v1.5.4/build-manifest.json'); d=json.loads(p.read_text()); print('keys=',sorted(d)); print('release_dir=',d.get('release_dir')); print('build_dir=',d.get('build_dir')); print('commit=',d.get('commit')); print('source_digest=',d.get('extension_source_digest'))"`。

## 未核实项与最终边界

1. 本 worker 本轮未重新执行 remote HTTP/S3 服务测试、服务端 body 对账、完整 local/remote 1/2/4 矩阵、SC-005 性能测量、ASan/UBSan 或独立复现。历史本地五轮结果见仓库根目录 `evidence/003-dimensions-remote-parallel/final.md:37-49`；004 status 提供远程 1/2/4 正确性汇总。
2. 父会话核对了 005 三版本身份及 336 组文件路径/哈希；没有核对 R20 原始包。本审计也未核对 R20 原始包，只引用 004 status 的汇总。
3. R21、R22 明确未完成；未更新仓库根目录 `evidence/003-dimensions-remote-parallel/final.md` 或任何任务/roadmap 文件。当前审计只确认已检查实现范围内没有新发现的代码缺陷；不能据此宣称 003 全量验收或 feature verified。

报告唯一写入位置：`/tmp/duckomo-spec-003-code-audit-20261021.md`。
