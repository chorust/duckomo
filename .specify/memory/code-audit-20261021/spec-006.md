# Spec 006 当前 HEAD 实现一致性审计

**审计日期：** 2026-10-21（Asia/Shanghai）  
**范围：** `/home/blizhan/repo/github/duckomo` 的 `specs/006-region-nearest-sampling/` 当前有效需求、contracts、实现调用链、相关测试，以及 HEAD `6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1` 对共享 scanner 的改动。  
**方式：** 只读核查；未改源码、测试、checkbox、roadmap 或仓库文件；唯一写入本报告。

## 结论

截至本次审计，已检查的实现中**没有发现可证实的 FR/SC 运行时违例**。`read_om_regions(method := 'nearest')` 的绑定、代表位置、规则球面最近定位、非空间轴输出、状态/来源以及按需值读取均能在代码中追踪到对应实现和调用者。HEAD 新增的连续矩形批读取经当前匹配构建的定向测试覆盖，未观察到值错位或扩大读取范围。

发现 **1 项低严重度文档状态漂移**：`contracts/numeric-and-capability.md:3` 仍称数值规则“待实现”，与 locator 已实现及最终 N1 通过记录不一致。它不表示当前代码有已证实缺陷。

FR-018/019、SC-005–007 的部分 gate（尤其完整资源比例、远程输入矩阵）和 SC-001/SC-008 的私有真实输入/独立用户执行**没有在本次对 HEAD 完整重跑**。它们有 spec 006 留存的已接受证据；本次用当前 HEAD 匹配构建运行了有限的 scanner/SQL 检查。故本报告是实现一致性审计，不宣称在 `6b34e52` 上重新完成 N0–N7 全量验收。

## 需求到实现与测试矩阵

状态含义：**满足（已查代码及当前定向验证）**；**满足（范围内留存 gate + 当前相关路径定向验证，未全量重跑 gate）**；**当前证据有限**表示该需求的验收边界依赖历史或私有/独立运行记录，本次没有重新执行该类场景。

| 需求 | 实现与调用路径（HEAD） | 测试/证据 | 判断 |
|---|---|---|---|
| FR-001–002 单入口、默认 points 兼容 | 注册仍为 `GetReadOmRegionsFunction`，method 通过共享 binder 解析：`src/scan/read_om.cpp:3678`、`src/scan/region_sampling.cpp:156`。points 分支不进入 nearest 逻辑，nearest-only 参数在 points 下拒绝：`region_sampling.cpp:167`、`:170`。 | 当前 `regions.test` 每版本 92 assertions；`region_sampling.test` 每版本 212 assertions；`region_scan_test` 含 points 与 nearest 查询。 | 满足（已查代码及当前定向验证） |
| FR-003 面内代表位置、MultiPolygon 单代表位置、EMPTY | `RegionGeometryAdapter::PointOnSurfaceCoordinate` 调 spatial `ST_PointOnSurface`，检查坐标有限并以 `ST_Covers` 验证：`src/scan/region_geometry.cpp:95`、`:110`。`BuildNearestSamplingPlan` 对 EMPTY 直接产生 `empty_geometry`，其它区域保存一个 representative：`src/scan/region_sampling.cpp:254`。 | 当前 `region_sampling.test`、`region_scan_test`；已接受 N2/N5 范围记录：`specs/006-region-nearest-sampling/evidence/N2-sql-matrix.md`、`evidence/N5-remote.md`。 | 满足（已查代码及当前定向验证） |
| FR-004–005 球面地表最近、确定性平局及 point index | 能力仅接受规则球面定义；遍历 `RegularGrid::Coordinate` 的完整源轴，按返回 binary64 距离比较，等距时取最小原始 point index：`src/grid/nearest_sampling.cpp:53`、`:188`、`:203`。存储顺序映射在 `nearest_sampling.cpp:46`。 | 当前三版本 `nearest_sampling_test` 均通过；N1 留存独立数值参考及有限能力边界矩阵：`evidence/N1-locator.md`、`evidence/final.md:14`。 | 满足（声明范围内）；不外推到未验收网格/任意 binary64 全空间 |
| FR-006–007 覆盖域、域外策略、米制距离阈值 | `NearestTargetInsideDomain` 区分周期/非周期及轴端点闭域：`nearest_sampling.cpp:117` 起；绑定计划先判域外，再应用 max distance，超限保留候选：`src/scan/region_sampling.cpp:264`。参数严格验证类型、有限非负数及 `null/error`：`region_sampling.cpp:173`、`:197`。 | 当前 `nearest_sampling_test` 与 `region_sampling.test` 三版本通过；N1/N2 保存阈值/边界/能力矩阵。 | 满足（声明范围内） |
| FR-008 固定几何来源、缺测不换点；FR-009 原始非空间轴位置独立；FR-010 原变量名/FLOAT、多变量同源 | `ScanNearestBatch` 先为每个非空间轴 tuple 生成同一 geometry source logical index，再解码其变量；NaN 转 NULL，不重新定位：`src/scan/read_om.cpp:3126`、`:3130`、`:3142`、`:3162`。轴 tuple 来自 `NearestAxisCursor`：`src/scan/region_sampling.cpp:20`、`:101`。 | 当前 `region_scan_test:480` 覆盖 separate/interleaved layout、3 非空间轴与 513 行任务；当前 SQL sampling 矩阵覆盖重复标签及逐变量 source_null。 | 满足（已查代码及当前定向验证） |
| FR-011–012 sampling 详情与真实 `om_source` | STRUCT 字段定义含 algorithm/版本/grid/layout、距离、坐标、索引、轴位置和变量状态：`src/scan/read_om.cpp:808`。结果字段由 plan/source 生成：`read_om.cpp:3227`、`:3244`；`include_source` 对有 source 的行调用真实 logical index：`:3306`。 | 当前 `region_scan_test` 核验 source logical index；SQL sampling/regions tests 通过；N2/N3 retained evidence。 | 满足（已查代码及当前定向验证） |
| FR-013–014 状态分类与整组输入/声明校验 | plan 区分 EMPTY、outside_domain、beyond_max_distance、ok：`src/scan/region_sampling.cpp:254`、`:266`、`:272`。绑定阶段完成区域输入、空间依赖、grid capability 和计划校验：`src/scan/read_om.cpp:2638`–`:2718`；非法 method/options 在 parser 拒绝。 | 当前 SQL sampling 212 assertions/版本、空间依赖 SQL 5 assertions/版本；留存 N2 有限 precedence/parser 矩阵。 | 满足（声明范围内） |
| FR-015–017 完整 residual filter、投影依赖、零值 I/O与有限值读取 | nearest 的 complex-filter callback 不提取空间 target 条件、仅处理安全轴条件并保留完整 residual：`src/scan/read_om.cpp:1618`–`:1672`。sampling leaf 的变量依赖由 `src/scan/projection.cpp:137`–`:183` 控制。Nearest 只对已选来源位置排序去重并调用 `BuildSelectedBatchSegments`/`DecodeSelection`：`read_om.cpp:3142`–`:3160`。 | 当前 `region_scan_test` 覆盖零值 I/O、nearest 输出及 residual/filter；当前 `reader_capacity_test` 三版本均逐位比对 decoder 输出并观测 chunks `35 → 5`；`batch_test` 穷举小 shape 的连续区间。N3 的 nearest 与 full-read 指标是 retained evidence，未在本次重新跑完整 N3 gate。 | 满足（HEAD 路径定向验证）；完整 N3 指标仅依据已接受快照 |
| FR-018–019 有界任务/内存、取消恢复、本地与远程一致 | cursor/task 逐批发出区域×轴结果：`region_sampling.cpp:20`–`:101`；scratch 先估算并限制 512 KiB：`read_om.cpp:3119`–`:3125`；异常后提交 cardinality 的路径及 task 账本在 `read_om.cpp:3316`–`:3325`、`:3333`–`:3365`。 | 当前三版本 `region_scan_test` 通过取消、LIMIT、短读/decoder/object-change 失败和同连接恢复；线程子检查在该测试内运行。N4/N5 的完整内存/远程门为留存证据，本次未启动远程服务或重跑 16× 资源矩阵。 | 满足（留存声明范围内）；HEAD 上仅重验定向本地路径 |
| FR-020 指南说明入口、nearest 代表值语义和状态/来源 | `README.md:134`–`:136`、`docs/spec.md:83`、`specs/006-region-nearest-sampling/quickstart.md` 提供 points/nearest、lat/lon 与 source 坐标区别、COUNT 及状态例子。 | 当前 docs 与注册/SQL schema 一致；独立 N7 人工执行留存于 `evidence/N7-independent.md`。本次未要求另一位独立用户再执行。 | 满足（文档存在且与实现相符）；SC-008 依赖 retained N7 |
| SC-001 冻结东城区/GFS025 样本 | nearest 输出目标与真实 source 分离，source 由同一 `read_om` logical index 产生。 | `evidence/N5-remote.md` 记录 points 零行、nearest `(116.5,40.0)`、直接源记录比对和私有授权边界；最终 gate 汇总见 `evidence/final.md:18`。本次未读取/重跑私有 GFS。 | 当前证据有限：已接受私有 gate；未在本次复跑真实样本 |
| SC-002 独立数值 oracle 与 capability 矩阵 | `grid/nearest_sampling.cpp` 实现合同中的 binary64 球面距离和 point-index tie-break。 | 当前 `nearest_sampling_test` 三版本 pass；`evidence/N1-locator.md` 与最终摘要 `evidence/final.md:14` 记录有限参考/12 个固定 GFS025 目标等范围。 | 满足（声明范围内；有限矩阵非穷尽证明） |
| SC-003 points 零差异 | 默认 `Points` parser 路径与 nearest 分离；旧 points 扫描仍走原区域谓词/输出。 | 当前 `regions.test` 92 assertions/版本，`region_sampling.test` 212 assertions/版本均通过；N0 retained baseline：`evidence/N0-points-baseline.md`。 | 满足（当前兼容回归通过） |
| SC-004 状态矩阵及错误不吞 | 状态建立在 geometry plan 与读出的值状态两个阶段；I/O 错误仍从 reader 传播，不映射为 source_null。 | 当前 region sampling SQL、native scan 通过；N2/N5 retained matrix 覆盖 EMPTY、域外、阈值、NULL 与错误恢复。 | 满足（声明范围内） |
| SC-005 多轴、线程和来源一致 | 轴 cursor 按原始 axis index 输出，source logical index由 layout 组合；nearest geometry source不随变量值改变。 | 当前 `region_scan_test` 覆盖多 layout、513 个轴位置及 1/2/4 worker 子检查；远程同内容对照见 retained N5。 | 满足（当前本地路径重验；远程仍用 retained evidence） |
| SC-006 零值 I/O及局部收益 | projection 不含 value dependency 时不创建值 decode；最近源列表只传给 selected batch decoder。HEAD 批次逻辑只合并完整连续矩形，不引入间隙。 | 当前 batch exhaustive assertions 通过；reader capacity 35→5 chunks 且输出 bitwise 相同；region scan 零值 I/O通过。N3 nearest-vs-full具体比例来自留存 gate，未本次复测。 | 满足（当前定向检查通过；完整 N3 性能快照未更新到本次运行） |
| SC-007 16×空间规模内存界限与失败恢复 | 任务不物化 region×axis 全表；worker scratch 有 512 KiB 硬界，batch mapping 有 64 KiB 上界。 | 当前 `region_scan_test` 验证取消/失败/恢复；16×内存对照及 sanitizer pass 见 `evidence/N4-resources.md`。未在 HEAD 上重跑完整 16×隔离进程矩阵。 | 当前证据有限：恢复路径有当前测试，增长比率依据已接受 N4 快照 |
| SC-008 未参与实现者按指南复现 | quickstart 描述普通入口、points/nearest 和 source/status 解释。 | 人工 N7 与正式 evidence interface 结果见 `evidence/N7-independent.md`、`evidence/final.md:20`、`:30`。本次没有新增独立执行。 | 满足（以 retained 独立 N7 记录为证） |

## HEAD 变化及其风险核对

HEAD `6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1` 的相关改动新增 `BuildContiguousBatchSegment`，由按最后一轴分段改为尽量合并完整尾部轴的矩形连续区间：`src/scan/batch.cpp:26`–`:51`、`:54`–`:82`；选择位置路径在 `src/scan/axis_selection.cpp:385`–`:420` 改用该 builder。nearest 调用者确实命中它：`src/scan/read_om.cpp:3142`–`:3159`。普通空间/raw 扫描另由 `spatial_selection.cpp:668`、`raw_scan.cpp:153` 使用同一段构建。

风险审查结果：range 校验先确认起点在 shape 内，再以减法检查长度；builder 每次必须消费至少一个已选元素；仅在本轴从 0 开始且该轴完整选中时才扩大到更高轴；因此矩形不会跨越未选 linear index。新增 `test/native/batch_test.cpp:83`–`:115` 对多个 rank/shape 的所有合法 start/count 穷举，逐元素确认无间隙、无重复、顺序和边界；新增 reader 测试 `test/native/reader_capacity_test.cpp:309`–`:335` 对 official reader 结果逐位比较。三版本均通过。

这部分是此前 Feature 006 N0–N7 快照之后的共享 scanner 修改，不能把旧 N3/N4 运行记录当作本 patch 的直接执行证明。本次在提供的 current build 目录上运行了对应批次、nearest 和 region scan 定向检查；未重跑远程服务、真实输入、完整 16× memory matrix 或 N0–N7 validator。

## 发现

**低严重度 — 数值合同状态未同步。** `specs/006-region-nearest-sampling/contracts/numeric-and-capability.md:3` 仍写“006规范化的待实现数值规则”；但 `src/grid/nearest_sampling.cpp:53` 起已实现 capability qualification，`specs/006-region-nearest-sampling/evidence/final.md:14` 将 N1 记为声明范围通过，且文件首行说明当前最终 acceptance。建议后续仅更新合同状态文字，使其表达已实现及有限验收范围。本次按只读约束未修改。

其它早期状态描述按文件中的 supersession 说明处理：spec 首行声明后续旧进度为历史；tasks 最后 reconciliation 明确早期 56/65、60/65、61/65 是历史快照，最终为声明范围 65/65。它们没有被误当作当前验收状态。

## 证据身份与工作区

- 当前源码身份：`main`，HEAD `6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`，提交标题 `perf: coalesce contiguous reads into rectangular batches`。
- 父会话提供的当前构建身份：`scripts/version_matrix.py extension_source_digest` 得到 `886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e`，并确认三版本的 `build/evidence/regions/current-20261020/matrix-testfixed/<version>/build-manifest.json` 对应当前源码；本审计使用其中 `release_dir` 下的预建测试二进制，没有重建或重复 hash/artifact 核对。
- 最近 Feature 006 final 在 `specs/006-region-nearest-sampling/evidence/final.md`（该文件最后由 `1378bfc` 更新）。其中接受范围为 Linux AArch64、DuckDB v1.5.4/v1.5.5/v1.5.6、合成规则球面网格与固定 NCEP GFS025；记录明确保留 N0–N6 runtime snapshots 及独立人工 N7。HEAD 后续改动不由那些历史命令自动覆盖。
- 初始工作区已有 `? .specify/extensions/roadmap` 与 `?? .specify/memory/roadmap-sync-20261021-next-directions.md`；本次未触碰。测试后未在仓库生成/修改审计文件。没有运行构建、全量 `scripts/validate.sh`、外部服务或大型 gate 矩阵。

## 本次执行命令与结果

版本与 release ID：

- v1.5.4：`build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release`
- v1.5.5：`build/official-matrix/v1.5.5/builds/10fb4c1331f717e97196b6fb6990cacbab48cd44646c6a51ea4aa371ae577f52/release`
- v1.5.6：`build/official-matrix/v1.5.6/builds/ab0257d665a469cae1f1e33da2449c91009137e745262de32b2ad447a00d3e5e/release`

对以上三个版本逐一运行：

- `release/test/native/batch_test`、`axis_selection_test`、`nearest_sampling_test`、`region_sampling_test`、`region_scan_test <匹配 spatial.duckdb_extension>`、`reader_capacity_test`：**18/18 通过**。reader-capacity 每版本输出 decoded chunks `35 -> 5`；region scan 的取消延迟只作观察记录，不视为固定性能界限。
- `DUCKOMO_SPATIAL_EXTENSION=<匹配 spatial> release/test/unittest --test-dir "$PWD" "$PWD/test/sql/region_sampling.test"`：**每版本 212 assertions 通过**。
- 同命令改跑 `test/sql/regions.test`：**每版本 92 assertions 通过**。
- 同命令改跑 `test/sql/region_sampling_spatial_dependency.test`（不加载 spatial）：**每版本 5 assertions 通过**，确认普通 `read_om` 可用且区域入口按契约要求 spatial。
- `git diff --check HEAD^ HEAD`：通过（无 whitespace error）。未因这些通过结果推断未运行 gate 也通过。

**未做：** 未重跑私有 GFS/S3/HTTPS、N5/N6/N7 完整验收、全套 N0–N7 validator、当前 HEAD 的 16× 资源隔离矩阵或全仓测试；以上均超出本次小型只读定向验证。报告仅覆盖已列需求、源码调用链和运行命令。
