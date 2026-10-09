<!--
LATEST SYNC IMPACT REPORT — 2026-10-02
======================================
Version change: 1.0.0 → 1.0.1
Bump rationale: PATCH — 更新 003 的任务快照和 T059 运行证据；生命周期仍为 in-progress。

Changes this revision:
  - 003 完成 T059；任务记录由 70/73 更新为 71/73，T070、T073 保持未完成。
  - 更新 003 notes 和 evidence snapshot，记录完整的本地查询内存计量及仍未执行的远程/独立门禁。

Specs affected: 003
Open questions added/resolved: none
Notes: 003 的受控远程 G3/G5/G6 与独立复现 G7 仍未完成；不提议 implemented 或 verified。

---
HISTORICAL INITIALIZATION SYNC IMPACT REPORT — retained from 2026-09-30
=======================================================================
Version change: none → 1.0.0
Bump rationale: INITIAL — 基于现有项目 artifacts 初始化统一的 spec roadmap。

Initial changes:
  - 纳入 001 — Phase 0–2 本地 OM 可用扫描器，status: implemented。
  - 纳入 002 — Phase 3 规则网格空间下推，status: verified（原始验收范围、Linux AArch64）。
  - 纳入 003 — Phase 4–5 维度语义、远程与并行读取，status: in-progress。
  - 提取目标、范围、设计决策与约束；记录有明确来源的 spec 依赖。
  - 保留 Phase 6–7 的既有意图，spec 拆分、编号与依赖待确认。
  - 记录任务勾选、验收记录与现有产品文档之间的差异。

Specs affected: 001, 002, 003（仅新增台账条目）
Open questions added/resolved: added Q-01–Q-07; resolved none
Initial notes: 状态是对 2026-09-30 已有 artifacts 的归纳，不是本次运行验收结果。
       现有 spec、plan、tasks、产品文档及 constitution 未修改。
       constitution 为占位模板；未检测到项目 ADR 或配置 glob 匹配的 PRD。
-->

# duckomo — Spec Roadmap

本文件将仓库现有 specs 纳入项目级台账，并保存尚未形成 spec 的后续意图。初始化依据为 [产品阶段路线图](../../docs/roadmap.md)、[接口说明](../../docs/spec.md) 和下列各 spec 的规格、计划、任务、契约与验收记录。阶段编号与 spec 编号分别表示产品里程碑和规格目录，不能互换。

[Constitution](constitution.md) 当前仍为未填写模板，其中示例原则不作为已批准的治理规则。下文约束和决策来自现有 artifacts；不新增 ADR、PRD 或项目原则。产品文档与更具体的 artifacts 不一致时保留差异，并记录待确认项。

Status legend (lifecycle): **undecided** · **needs-info** · **planned** · **specced** · **in-progress** · **implemented** · **verified** · **deferred** · **abandoned**。

本台账按以下证据区分生命周期：规格和设计齐备只能证明已完成定义；存在已勾选实施任务且仍有未完成任务支持 `in-progress`；实现和运行门禁已完成但独立验收仍有缺口支持 `implemented`；`verified` 必须有对应范围的完整验收及独立复现记录。任务全部勾选或规格质量清单通过不自动等于 `verified`。证据无法确定的事项标为“待确认”，不按目录编号或历史 `Draft` 标记推定进度。

---

## Vision & End States

- 直接在 DuckDB 查询受支持的 OM 文件，获得稳定、可解释的 schema 与可信值；查询运行时无需预转换，也不依赖 Python/Swift。来源：[001 spec 的 FR-001–008](../../specs/001-local-om-scanner/spec.md#functional-requirements)。
- 让列裁剪、规则网格区域选择和其他维度筛选保持完整 SQL 语义，同时以独立结果核对及实际读取/解码计量证明收益。来源：[001 SC-003–004](../../specs/001-local-om-scanner/spec.md#measurable-outcomes)、[002 SC-001–005](../../specs/002-spatial-pushdown/spec.md#measurable-outcomes)、[003 SC-001–004](../../specs/003-dimensions-remote-parallel/spec.md#measurable-outcomes)。
- 对单个本地、HTTP(S) 或 S3 对象使用一致的坐标、值及缺测语义；通过真实并行、会话缓存和隔离的 profiling 支持大范围与重复分析。完整交付还需三类来源对照、实际网络计量、故障恢复与独立复现。来源：[003 spec](../../specs/003-dimensions-remote-parallel/spec.md)。
- 逐步扩展到投影/Gaussian 网格，随后独立设计科学计算算子；每类新网格须有上游定义和真实样本对照。具体 spec 划分和技术选择尚待确认。来源：[产品路线图 Phase 6–7](../../docs/roadmap.md#阶段)。

## Constraints & Decisions

- **C-01 — 官方 OM reader 的职责：** 格式解析、物理 chunk 选择、字节请求与解码由固定的官方 OM C reader 负责；扫描器、网格和维度层提供逻辑选择、有界切片及 DuckDB Vector/DataChunk 输出。这样可沿用上游格式与压缩实现，避免重复维护物理规划。来源：[001 plan](../../specs/001-local-om-scanner/plan.md#summary)、[002 plan](../../specs/002-spatial-pushdown/plan.md#summary)、[003 plan](../../specs/003-dimensions-remote-parallel/plan.md#summary)。
- **C-02 — 明确的轴与语义证据：** 相同 shape 或文件路径不足以证明变量、空间或时间身份；多变量必须有一致的有序轴证据。显式声明不能覆盖冲突元数据，不自动转置、广播、连接或按重复坐标去重。来源：[001 FR-005–007](../../specs/001-local-om-scanner/spec.md#functional-requirements)、[002 FR-002–005/011](../../specs/002-spatial-pushdown/spec.md#functional-requirements)、[003 FR-001–005](../../specs/003-dimensions-remote-parallel/spec.md#functional-requirements)。
- **C-03 — 保留完整 SQL 条件与依赖：** 只抽取安全必要条件缩小候选；完整 `WHERE` 始终由 DuckDB 执行，输出和过滤依赖均保留。不安全 OR、函数或转换正常回退，不能取单一析取分支删行。来源：[002 plan 的 Constitution Check](../../specs/002-spatial-pushdown/plan.md#constitution-check)、[003 plan 的联合筛选](../../specs/003-dimensions-remote-parallel/plan.md#implementation-sequence)。
- **C-04 — 受支持的单对象子集：** 001–003 以单个、扫描期间不变的 OM v3 对象为边界；值数组为 Float32、FPX_XOR2D 或 PFOR_DELTA2D_INT16，rank 1–8、轴长为正、计数检查溢出。NaN → SQL NULL，保留 ±Inf；与官方解码后的 Float32 值精确比较，PFOR 编码本身可能有损。目录、glob、文件列表、跨文件拼接和写入不属于这些 specs。来源：[基础 SQL 契约](../../specs/001-local-om-scanner/contracts/sql-interface.md#支持矩阵)、[003 Assumptions](../../specs/003-dimensions-remote-parallel/spec.md#assumptions)。
- **C-05 — 固定构建基线：** 采用 C++17 / GNU C11，设计与主要验收固定 DuckDB v1.5.4（`08e34c447bae34eaee3723cac61f2878b6bdf787`）、OM C（`d8855e418e2231ae8439f0c7e840fa3f93b371e3`）及 extension-ci-tools（`b777c70d30942cca5bef62d6d4fa23a13362f398`）。查询运行时的无 Python/Swift 约束不排除开发期工具。来源：[001 plan](../../specs/001-local-om-scanner/plan.md#technical-context)、[003 plan](../../specs/003-dimensions-remote-parallel/plan.md#technical-context)；v1.5.5 的真实文件兼容修复有单独的[样本记录](../../docs/issues/real-world-om-compatibility.md#修复与验证结果)，不能替代其他版本的完整验收。
- **C-06 — 结果与收益分别举证：** 固定输入、参考、查询、列集合、构建、环境和缓存条件，完整消费结果。值参考来自独立官方 reader 路径，坐标参考不调用待测映射；收益以真实字节和解码计量判定。耗时和内存按实际值及作用范围记录；失败、不完整统计、输出行数或 `EXPLAIN` 不能充当成功性能证据。来源：[001 观测契约](../../specs/001-local-om-scanner/contracts/validation-evidence.md)、[002 观测契约](../../specs/002-spatial-pushdown/contracts/validation-evidence.md)、[003 观测契约](../../specs/003-dimensions-remote-parallel/contracts/validation-evidence.md)。
- **C-07 — 平台范围：** 当前明确验收范围是 Linux AArch64；Linux x86_64 支持与验证已按 artifacts 记录的用户决定暂缓。001 原计划的 x86_64 目标是历史记录，不能据 AArch64 结果宣称其已通过。来源：[产品路线图验证范围](../../docs/roadmap.md#已完成部分的验证范围)、[001 quickstart](../../specs/001-local-om-scanner/quickstart.md#supported-behavior-and-current-platform)、[002 最终验收](../../specs/002-spatial-pushdown/evidence/final.md)、[003 plan](../../specs/003-dimensions-remote-parallel/plan.md#technical-context)。
- **C-08 — 五类维度的类型与兼容：** `axes` 使用严格校验的原生 STRUCT；time 输出既有 `valid_time TIMESTAMP`，保留 `valid_times`、已识别时间元数据和标量快照。time/run 为 UTC 微秒时刻，lead_time 为无月分量 INTERVAL，level 为明确 kind/unit 的无损 DOUBLE，member 为精确保留的 BIGINT/VARCHAR。自动识别仅限已有时间证据，其余四类显式映射；不自动推导 `time = run + lead_time`。来源：[003 plan Summary](../../specs/003-dimensions-remote-parallel/plan.md#summary)、[003 SQL 契约](../../specs/003-dimensions-remote-parallel/contracts/sql-interface.md#axes-声明)。
- **C-09 — 远程使用官方 HTTPFS：标准文件接口、当前访问上下文、受检位置读取和可观察长度/版本比较。对象扫描期间稳定；不承诺专用 ABI、每查询新鲜探测或逐响应快照。** 来源：[2026-10-08 修订](../../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)。
- **C-10 — 由 DuckDB 调度并行：** global state 保存不可变 schema/selection、惰性任务分配及终止状态，worker local state 独占句柄、decoder 与缓冲。上限受 DuckDB threads、连接设置及可用任务数共同限制，不另建线程池；每个候选逻辑位置恰好处理一次，失败后停止后续任务。来源：[003 plan](../../specs/003-dimensions-remote-parallel/plan.md#implementation-sequence)、[003 data model](../../specs/003-dimensions-remote-parallel/data-model.md#scantask-与-globallocalstate)。
- **C-11 — 缓存归官方依赖：移除 DuckOMO 自有 LRU 和缓存 SQL 设置/函数，不承诺原容量、访问分区或即时撤权语义。旧 G5 superseded，保留历史失败。** 来源：[2026-10-08 修订](../../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)。
- **C-12 — 诚实的 profiling：自有应用读取/解码/选择/任务/终态保留；远程 transport NULL/complete=false，本地 0/complete=true；自有 cache false/0/removed。服务端发送量独立审计，HTTPFS 总内存不由自有账本约束。** 来源：[2026-10-08 修订](../../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)。
- **C-13 — 后续网格与科学算子的边界：** 更多网格须有上游定义、真实样本对照及可重生 registry；科学算子独立于扫描入口设计，普通 `read_om` 直接输出 DuckDB Vector。xtensor/xsimd 尚未接入，现有 artifacts 未确定它们的后续选型。来源：[产品路线图](../../docs/roadmap.md#后续实现约束)。

## Planned Specs

### 项目台账摘要

| Spec | 产品阶段 | 生命周期 | 任务记录 | 状态边界 |
| --- | --- | --- | --- | --- |
| 001 | Phase 0–2 | `implemented` | 44/44 勾选 | AArch64 运行门禁有通过记录；SC-006 独立复现仍为 Partial |
| 002 | Phase 3 | `verified` | 46/46 勾选 | 原始 Phase 3 / ncep_gfswave025 在 AArch64 独立验收；T042 为延期决定，未执行 |
| 003 | Phase 4–5 | `in-progress` | 本地 G0–G2 通过，G4 只有本地部分结果；G3、G5–G7 未完成，完整交付门禁未闭环 |
| 004 | Phase 6 | `in-progress` | 任务仍在实施；本地网格/选择代码已有进展，真实定义覆盖、远程/内存/版本 gate 和独立复现未闭环 |

生命周期根据实施和验收 artifacts 归纳；任务勾选不代表运行 gate 通过。001 与 003 的 spec 头部仍写 `Draft`；原文件状态保留。

### 001 — Phase 0–2 本地 OM 可用扫描器  [status: implemented]

- **Description:** 建立可直接查询本地 OM 的 DuckDB 扩展，依次完成单变量纵向验证、稳定 schema/多变量对齐和按查询依赖裁剪值变量。
- **Outcome:** 用户无需转换文件即可加载、查看列描述、完整扫描和单变量扫描；值、逻辑位置、NULL 与官方结果一致。固定多变量样本中无关变量解码为零，单变量数据字节严格少于全变量；错误与取消后可恢复，操作可独立复现。
- **Scope (in):** `read_om_raw` 本地 FPX 根数组验证入口；`read_om` 根数组和层级值数组；稳定唯一命名、全变量支持性校验、按有序轴证据对齐；有界批次、列依赖裁剪、metadata-only count、缺测/错误/资源生命周期及实际读取证据。后续真实文件兼容修复纳入 Float32/PFOR、数组附属元数据、coordinates 自动对齐与去掉输出列名前导 `/`，以现行基础契约为依据。
- **Scope (out):** 原 Phase 0–2 不含空间/时间语义下推、domain 注册、HTTP/S3、并行、应用缓存、新网格、科学算子和导出；不含多文件合并或扫描中内容变更一致性。后续能力由对应阶段承接。
- **Depends on:** 无已记录的其他项目 spec 前置；固定官方 reader、DuckDB 及独立样本/oracle 为技术与验证依赖。
- **Governed by:** C-01–C-07（现有 artifacts 提取的约束；无正式 ADR）。
- **Addresses:** [产品路线图 Phase 0–2](../../docs/roadmap.md#阶段)、[接口说明](../../docs/spec.md)。
- **Spec dir:** [specs/001-local-om-scanner/](../../specs/001-local-om-scanner/)；[spec](../../specs/001-local-om-scanner/spec.md)、[plan](../../specs/001-local-om-scanner/plan.md)、[tasks](../../specs/001-local-om-scanner/tasks.md)。
- **Key decisions:** 官方 reader 唯一负责物理格式/解码；bind 校验所有值变量，不因投影隐藏不兼容；scan 使用实际列依赖，重复变量只解码一次；计数使用内部 cardinality 路径；未配置语义时只提供可证明的原始值。原计划的仅 FPX/显式轴方案已由[当前基础契约](../../specs/001-local-om-scanner/contracts/sql-interface.md)及[真实文件兼容修复记录](../../docs/issues/real-world-om-compatibility.md)补充，保留历史计划。
- **Notes:** 44 项任务均勾选，[最终验收](../../specs/001-local-om-scanner/evidence/final.md)记录 AArch64 构建、SQL/native、样本重生、投影及 sanitizer 通过；temperature 单列数据字节 165,767，对照全变量 628,703。该记录明确 SC-006 为 Partial，隔离无 Python/Swift 的执行者来自实现环境，独立复现尚无完成记录，故不提升为 `verified`（Q-02）。002 的独立空间复现不自动补齐 001 的全部四项独立操作。

### 002 — Phase 3 规则网格空间下推  [status: verified]

- **Description:** 在本地扫描与列裁剪上增加有证据的规则经纬度映射，并用安全空间条件缩小逻辑读取范围。
- **Outcome:** 显式网格和已核验 domain 的坐标、值与逻辑位置一致；区域查询与完整扫描后过滤相同，固定可跳块样本的数据字节和解码块均严格减少；空选择、仅坐标和纯空间 count 不读值 index/data、不解码值块；独立验证者能复现并判断收益。
- **Scope (in):** `grid` / `spatial_axes` 或 `domain`；分离轴及明确的 lon_fastest/lat_fastest 展平轴，保留额外轴位置；latitude/longitude、网格/轴/命名冲突校验；有限常量比较、BETWEEN、安全 AND、精确 residual、跨接缝 OR 正确回退；与值过滤、列裁剪、表达式及聚合组合。原始发布门禁包含 ncep_gfswave025 的全域独立坐标和 15 个官方值参考。
- **Scope (out):** 本 spec 的原始实施不含 time、level、lead_time、member、run 映射/下推，远程、并行、应用缓存、投影/Gaussian 网格和科学算子；不承诺任意小区域或文件都有读取收益，不承诺跨接缝 OR 减少读取。
- **Depends on:** 001 — spec 的 [Assumptions](../../specs/002-spatial-pushdown/spec.md#assumptions) 明确依赖 Phase 0–2 及真实文件兼容修复；[tasks Prerequisites](../../specs/002-spatial-pushdown/tasks.md)沿用既有扫描器和固定依赖。
- **Governed by:** C-01–C-07、C-13。
- **Addresses:** [产品路线图 Phase 3](../../docs/roadmap.md#阶段)、[空间接口说明](../../docs/spec.md#输出与查询语义)。
- **Spec dir:** [specs/002-spatial-pushdown/](../../specs/002-spatial-pushdown/)；[spec](../../specs/002-spatial-pushdown/spec.md)、[plan](../../specs/002-spatial-pushdown/plan.md)、[tasks](../../specs/002-spatial-pushdown/tasks.md)。
- **Key decisions:** grid/domain 互斥；完整有序轴证据不能由 shape 替代；纬度/经度坐标按输出值精确过滤，经度统一 [-180,180)，反向 BETWEEN 不环绕；complex-filter callback 保留全部 WHERE；网格层只产逻辑段，不重建物理块规划；参考容差不改变 WHERE；全域坐标按需生成。
- **Notes:** [最终验收](../../specs/002-spatial-pushdown/evidence/final.md)、[AArch64 记录](../../specs/002-spatial-pushdown/evidence/linux-aarch64.md)及[独立复现](../../specs/002-spatial-pushdown/evidence/quickstart-review.md)支持原范围 SC-001–006 均通过。46 项勾选中，T042 按 x86_64 延期决定关闭，未执行；此状态不表示 x86_64 已验证。固定同列全域/区域数据字节为 165,767 → 1,795，解码块 503 → 5。
- **Scope / verification boundary:** [当前 SQL 契约](../../specs/002-spatial-pushdown/contracts/sql-interface.md#命名-domain)已扩展至 68 个规则 domain、额外轴布局及存在时的 WKT BBOX 校验；[domain 审计](../../docs/regular-domains.md)主要是各目录首个样本的元数据绑定，另有具体 CHMI/GeoSphere 全值对照。原验收文件明确不覆盖所有新增 domain，`verified` 仅代表原始验收范围，不宣称 68 项、所有目录对象或后续修改均通过同等级完整验收（Q-05）。

### 003 — Phase 4–5 维度语义、远程与并行读取  [status: in-progress]

- **Description:** 一个 feature 统一承接其他维度、混合筛选、远程单对象、并行扫描、会话缓存和逐查询 profiling；Phase 4 与 Phase 5 未拆成两个 spec。
- **Outcome:** 五类坐标与值逐位置一致，混合查询精确且能证明局部读取收益；本地/HTTP(S)/S3 同内容结果一致，两远端冷局部响应 body 均减少；1/2/4 worker 上限无遗漏重复且固定样本五次并行耗时中位数下降；稳定对象热缓存减少网络字节或请求，权限/版本异常正确失效；G0–G7 和 SC-001–008 有完整记录及独立复现。
- **Scope (in):** 五类一维语义映射及 regular/explicit 坐标，UTC/单位/类型与轴冲突校验、既有 valid_time 兼容；与空间/值过滤/投影联合的安全 typed 条件与有界游标；HTTP(S)/S3 单对象及配套 httpfs 的严格范围、授权和版本会话；DuckDB global/local 并行任务与失败恢复；连接缓存控制和最近扫描指标函数、v3 计量、服务端对账、真实 OM 三来源验证与交付文档。
- **Scope (out):** 目录、glob、列表、跨文件拼接、写入、新格式；跨轴有效时间推导、日历运算、垂直插值、集合统计；投影/Gaussian 新网格及科学算子；账户/凭据管理产品、离线同步、跨进程持久缓存；Linux x86_64 支持与验收。
- **Depends on:** 001、002 — [spec Assumptions](../../specs/003-dimensions-remote-parallel/spec.md#assumptions)明确依赖已完成的 Phase 0–3，[tasks Prerequisites](../../specs/003-dimensions-remote-parallel/tasks.md)再次列出既有 Phase 0–3。已有 valid_time 基线提交 `c593b0e` 是[plan Summary](../../specs/003-dimensions-remote-parallel/plan.md#summary)记录的兼容依赖，不虚构额外 spec 编号。
- **Governed by:** C-01–C-12。
- **Addresses:** [产品路线图 Phase 4–5](../../docs/roadmap.md#阶段)、[现有接口](../../docs/spec.md)；差异见 Q-04。
- **Spec dir:** [specs/003-dimensions-remote-parallel/](../../specs/003-dimensions-remote-parallel/)；[spec](../../specs/003-dimensions-remote-parallel/spec.md)、[plan](../../specs/003-dimensions-remote-parallel/plan.md)、[tasks](../../specs/003-dimensions-remote-parallel/tasks.md)。
- **Key decisions:** time 沿用 valid_time；只自动识别已核验时间元数据，其他语义显式映射；类型精确比较、重复/非单调坐标保留原位置，区间预算超限安全回退全轴；配套 httpfs ABI 先验收，凭据沿用当前访问上下文；worker 独占 decoder，惰性任务不物化全域；缓存只复用已请求范围；QueryEnd 单次发布权威终态，实际 body 不由逻辑请求量推算。
- **Notes:** 截至 2026-10-02，73 项中 71 项勾选；T059 已完成，T070（独立复现）和 T073（完整验收后的路线图收尾）仍未完成。[最终证据记录](../../evidence/003-dimensions-remote-parallel/final.md)显示 G0–G2 在 Linux AArch64 本地通过，G4 有本地部分结果；G3 未通过完整门禁，G5/G6 的受控远程运行未执行，G7 独立复现未执行。T059 的成功本地 v3 profile 记录 `peak_query_owned_bytes=528249` 且 `query_memory_count_complete=true`；RSS 标为 process scope，失败/取消时无法证明的内存值仍为 NULL。任务勾选和本地门禁不等同完整发布验收；因此保持 `in-progress`，不标 `implemented` 或 `verified`。
- **Internal sequencing:** [tasks 的故事依赖](../../specs/003-dimensions-remote-parallel/tasks.md#user-story-dependencies)明确 US2 依赖 US1；完整 G3 待 US2 路径完成；本地与远程完整 G4 依赖 US3；G5/G6 并发集成依赖 US4。部分基础开发可提前进行，任务先行勾选不能替代这些集成门禁。产品路线图现记录本地已验收范围和远程验收缺口；T073 仍待 G0–G7 及独立复现齐备后完成最终收尾。

### 004 — Phase 6 多类型网格与远程空间选择  [status: in-progress]

- **Description:** 在既有单对象扫描器上增加封闭的 rotated/Lambert/stereographic/reduced-Gaussian 定义、保守 native-window 空间选择、可选 source 位置和 `om_grid_info` 描述。
- **Outcome:** 对固定真实来源完整核对坐标、值、布局及源位置；空间条件结果与完整物化后过滤一致，并在真实可跳块输入上证明局部读取收益；内存/取消/并行/终态可审计；逐 definition/domain 标注实际 evidence 等级。
- **Scope (in):** 固定 Open-Meteo 规则和数值来源；明示 N160/N320/N320 区域 Gaussian；有界选择与预算回退；source/grid-info；metrics v4；本地及远程、版本配套和独立复现门禁。公开 HRES O1280 对象为补充 Gaussian-family 证据，不替代 N-grid 样本。Evidence levels 按 definition 隔离。
- **Scope (out):** 任意 CRS/PROJ、Gaussian O/F 接受、由 BBOX 重建区域点序、解码块缓存、写入、多文件拼接、邻接/单元边界/面积/距离/向量方向算子，以及未经 gate 支持的发布声明。
- **Depends on:** 复用 001–003 的 OM v3 reader、DuckDB table function、轴和受检 ReadAtFile 框架。本地网格内核可继续实现；H6 远程收益按官方 HTTPFS 新契约和受控服务独立审计。外部真实样本和独立 oracle 是 Gaussian 完整验收输入门槛，不阻止已有输入下的内核实现。
- **Governed by:** C-01–C-13；spec 自带的 SQL、selection/I/O 和 evidence contracts。
- **Addresses:** [产品路线图 Phase 6](../../docs/roadmap.md#阶段)、[多网格 SQL 契约](../../specs/004-multi-grid-selection/contracts/sql-interface.md)、[逐 definition 证据表](../../docs/grid-domains.md)。
- **Spec dir:** [specs/004-multi-grid-selection/](../../specs/004-multi-grid-selection/)；[spec](../../specs/004-multi-grid-selection/spec.md)、[plan](../../specs/004-multi-grid-selection/plan.md)、[tasks](../../specs/004-multi-grid-selection/tasks.md)。
- **Key decisions:** canonical identity 不含 provenance；未知/冲突 CRS fail closed；候选使用与输出一致的函数且保留 DuckDB residual；任务惰性、有界并可取消；Gaussian 显式完整行表/局部 parent segments；O1280 与 N family 分开；完整 scan 才能发布 authoritative exact count/成功 memory evidence。
- **Notes:** requirements checklist 全部通过。三类真实 Open-Meteo OM v3 投影样本已有独立坐标和官方 OM C 全量值参考；2026-10-08 匹配 baseline H1 子比较对全部可用投影坐标和值逐位置通过，但其 `[ny,nx,ntime]` 轴顺序仍来自 producer profile，OM 对象源轴映射尚无独立证明。HRES O1280 `HSURF.om` 全值参考仍是补充证据，行长/坐标 point order 未映射，不能提升 N160/N320/区域状态。T003/T004 对 N-grid真实对象、区域局部点序和完整验收参考仍未完成；本地合成选择回归不替代 H0/H1/H6。G3 的远程依赖、2.0 matrix 与 H9 独立复现仍待完成。因此保持 `in-progress`，不能只因实现代码或生成 registry 存在而标 `implemented` / `verified`。

### 尚未形成 spec 的既有方向

| 产品方向 | 已有目标与范围依据 | 原产品阶段状态 | 尚待确认 |
| --- | --- | --- | --- |
| Phase 7 — 科学计算 | 独立设计 `om_slice`、`om_reduce`、`om_interp`、`om_regrid`；算子与扫描入口的职责分开。[来源](../../docs/roadmap.md#阶段) | 计划中 | 单一或多个 specs、编号、各算子的语义与约束、技术选型、验收及 spec 级依赖 |

以上只保留尚未形成 spec 的 Phase 7 意图；Phase 6 已由 004 承接。产品阶段顺序本身不足以确定未来科学算子的硬依赖边。

## Open Questions

- **Q-01 — Constitution 治理待确认：** [constitution](constitution.md) 仍含项目名、原则、版本与日期占位符。正式原则、版本与批准日期尚未定义；本 roadmap 不代填，也不把模板示例当成约束。后续以实际批准的 constitution 解决。
- **Q-02 — 001 的独立复现待确认：** [001 final](../../specs/001-local-om-scanner/evidence/final.md#sc-001sc-006-outcomes)仍将 SC-006 标为 Partial，虽然 T044 已勾选。需补充未参与实现者的加载、列描述、完整/单列扫描记录，或明确接受标准的范围修订依据，才可判断是否升级 `verified`。x86_64 延期已有依据，不重新假定其必须通过。
- **Q-03 — 003 各门禁的实际完成状态（已记录，完整验收仍待完成）：** [最终证据](../../evidence/003-dimensions-remote-parallel/final.md)记录了构建身份、命令/退出码、原始本地指标及各门禁状态。G0–G2 本地通过；G4 只有本地部分结果；G3 未通过完整门禁，G5/G6/G7 未执行。后续状态以该记录和新增运行证据为准，不将未执行推定为通过。
- **Q-04 — 产品文档与已记录基线的差异（已同步）：** README、[接口说明](../../docs/spec.md)、[技术架构](../../docs/architecture.md)及[产品路线图](../../docs/roadmap.md)现描述语义轴、并行和远程功能及其验收边界；功能已有实现不代表 Phase 4–5 完整交付。
- **Q-05 — 002 后续 domain 扩展的验收边界待确认：** 68 项 registry 的来源及元数据审计已记录，但[原始 final](../../specs/002-spatial-pushdown/evidence/final.md)明确不覆盖全部新增 domain。后续扩展需要何种完整值、坐标、异常及独立复现证据才能作更广的 `verified` 声明，需补充对应范围与记录；缺样本项保留未知，不推定可读或不可读。
- **Q-06 — Phase 7 的 spec 结构与依赖待确认：** 科学计算方向仍未形成项目 spec 目录。待确定算子拆分、编号、语义、技术约束和明确前置依据；不只凭阶段顺序推定其依赖边。Phase 6 已由 004 定义，生命周期见上文。
- **Q-07 — 003 最终证据归档路径待统一：** T072 指定的仓库根 `evidence/003-dimensions-remote-parallel/final.md` 已存在；plan 的项目结构说明和[观测契约](../../specs/003-dimensions-remote-parallel/contracts/validation-evidence.md#执行范围与成功声明)使用 feature 级 `evidence/` 表述。当前记录可由 T072 路径访问，但设计文档与任务路径仍不一致，需后续明确唯一规范路径并同步引用。

## Cross-Cutting Notes

- **已确认的 spec 依赖：** `002 → 001`；`003 → 001, 002`，箭头表示左侧依赖右侧。依据分别是 002、003 spec 的 Assumptions 及 tasks 的 Prerequisites；003 对 001 的直接列出来自明确的 Phase 0–3 依赖声明。001 的独立复现缺口不被解释为 002 尚未实施或 003 不能开始；依赖要求与整体验收分别记录。
- **阶段映射：** 001 覆盖产品阶段 0–2，002 覆盖 Phase 3，003 同时覆盖 Phase 4 和 5，004 覆盖 Phase 6。tasks 内部的 Phase 编号是任务分组，不能据此生成新的项目 spec 或依赖。
- **历史与后续范围：** 001 原计划仅 FPX、显式轴、Linux x86_64；当前基础契约已有 PFOR 与 coordinates。002 原计划仅首个 domain；当前契约已有 68 项。003 从已有有效时间基线上继续扩展。历史设计、已发布行为、进行中的实现和验收样本覆盖分别以其来源说明为准，范围扩展不自动继承旧验收结论。
- **证据快照：** 2026-10-02 更新依据为当前 003 tasks、仓库根 [final evidence](../../evidence/003-dimensions-remote-parallel/final.md) 与 `local-run/2026-10-02/` 的本地输出。重建后的 Linux AArch64 release 验证通过本地 SQL/native、14 项 sanitizer、fixture、G0–G2 与本地 G4；受控远程 G3/G5/G6 和 G7 未运行。工作树中的实现内容和本地门禁结果不等于完整发布验收。
- **配置路径复核：** 从 duckomo 仓库根运行 `load-config.sh` 返回 `.specify/memory/roadmap.md`，且 `roadmap_exists=true`；本项目未配置 `docs/adr/`。执行路线图核对时仍应确认运行目录和加载器输出指向当前项目。
- **项目来源边界：** 本次台账只纳入仓库根的 `specs/`；`.specify/extensions/roadmap/specs/` 及其内嵌 roadmap/constitution 属于扩展自身示例，不作为 duckomo specs。项目无 `docs/adr/`，配置 PRD globs 未匹配到项目 PRD；普通 `docs/spec.md` 用作接口来源，不改称或编写外部 PRD。

---

**Version**: 1.0.1 | **Ratified**: 2026-09-30 | **Last Amended**: 2026-10-02

## 2026-10-08 官方 HTTPFS 接入

用户指定 v1.5.4、v1.5.5、v1.5.6 / Linux AArch64。C-09/C-11/C-12 已按迁移契约修订；C-10 并行职责保留。官方运行兼容性证据另行记录，不提升 003/004 的 verified 状态。旧 G5 失败保留且 superseded，完整 004 真实网格门禁和独立验证仍未闭环。
