---
description: "Phase 3 规则网格空间下推实施任务"
---

# Tasks: Phase 3 规则网格空间下推

**Input**: `specs/002-spatial-pushdown/` 的 [plan](plan.md)、[spec](spec.md)、[research](research.md)、[data model](data-model.md)、[SQL contract](contracts/sql-interface.md)、[evidence contract](contracts/validation-evidence.md) 和 [quickstart](quickstart.md)。

**Prerequisites**: 既有 Phase 0–2 扫描器；固定 DuckDB v1.5.4、OM C 与工具链版本；实际 Git 分支 `main`。不重新初始化项目或升级依赖。

**Tests**: spec 的验收场景、FR-012–014/016 和 SC-001–006 明确要求正确性、读取及恢复验证，因此包含测试任务。各故事先编写验收测试，确认新功能缺失导致的预期失败，再实现并转绿；不额外要求审批或使用 superpower TDD。constitution 仍是未填写模板，无新增治理门禁。

**Organization**: 按用户故事划分，阶段编号是本任务清单顺序，不是 roadmap 的 Phase 0–7。所有路径相对仓库根目录；新增文件是实施目标。每项只有在描述的产物和验证完成后才勾选，不能把源码检查当作执行通过。

## Format: `[ID] [P?] [Story] Description`

`[P]` 表示在本阶段前置条件完成后可与指定同组任务并行；不允许跳过依赖。故事任务使用 `[US1]`/`[US2]`/`[US3]`，公共任务无故事标签。共享 `CMakeLists.txt`、`test/CMakeLists.txt`、`read_om.cpp`、fixture manifest 和 harness 的修改串行合入；任务新增源文件或测试时同步注册其构建目标，不等到最终验收才使其可运行。

## Phase 1: Setup — 固定基线与构建入口

**Purpose**: 延续已有工程，记录可复现起点。

- [X] T001 核对固定依赖和当前 CPU/工具链，执行 `make release` 与现有 `scripts/validate.sh build/release`，在 `specs/002-spatial-pushdown/evidence/baseline.md` 记录命令、退出码、架构及已有失败；不得将 AArch64 结果标作 x86_64 通过。
- [X] T002 在 `src/include/duckomo/regular_grid.hpp`、`src/include/duckomo/spatial_layout.hpp`、`src/include/duckomo/spatial_filter.hpp`、`src/include/duckomo/spatial_selection.hpp` 定义 data-model 对应的最小接口/所有权，配置 `CMakeLists.txt` 与 `test/CMakeLists.txt` 的新增源/测试接入方式；保持无实现文件时配置可运行，不引入新运行时依赖或通用网格框架。

## Phase 2: Foundational — 所有故事的阻断前置

**Purpose**: 建立独立参考、观测基础和固定版本集成证据。

- [X] T003 [P] 扩展 `test/tools/duckomo_fixture_tool.cpp` 和 `test/data/manifest.json`，生成非方形、反向/单点轴、0–360/接缝及重复接缝位置、两轴换序、两种展平顺序、额外轴在前/中/后、NULL 和坐标命名冲突样本；包含 quickstart 的 `test/data/spatial_flat.om`（shape `[2,6]`），值参考走独立官方 reader，坐标参考用人工轴表/独立公式且不链接生产 GridMapping；固定坐标容差 1e-9 度、值容差 0，并验证临时重生成及所有资产 SHA-256。
- [X] T004 [P] 在 `src/include/duckomo/metrics.hpp` 增加 schema_version=2 字段：bind/scan metadata 分量、grid/layout/source、selection_mode、residual/fallback、candidate_rows 和参考核对结果；保留逐变量实际 index/data/decode 计数与失败不完整标识，在 `test/native/spatial_metrics_test.cpp` 检查序列化、加总及溢出规则。
- [X] T005 将同一查询的 metrics 接入 `src/scan/read_om.cpp` 的 bind/init/scan 与 `src/om/local_file.cpp` 的真实读取边界，避免 bind 重绑定/复制后双计数或串查询；更新 `test/tools/duckomo_validation.cpp` 和 `test/native/projection_evidence_test.cpp` 对 v2 及完整 metadata 口径的断言，旧投影验收仍通过（依赖 T004）。
- [X] T006 在 `test/native/spatial_callback_test.cpp` 实测固定 DuckDB 的最小 table-function complex callback：不删除/改写表达式且 `filter_pushdown=false`、`filter_prune=false` 时 residual 仍执行、过滤列仍保留；验证坐标 DOUBLE 与整数/反向常量绑定形态，将计划及结果写入 `specs/002-spatial-pushdown/evidence/callback.md`；若语义不符先修订设计，不继续接入不安全下推。
- [X] T007 运行 T003–T006 的 fixture/native/旧扫描器检查，在 `specs/002-spatial-pushdown/evidence/foundation.md` 记录独立 oracle、metrics 和 callback 门禁结果；确认错误/取消计量不算成功证据、旧 raw/projection 行为未变后允许进入 US1。

**Checkpoint**: 独立参考和查询计量可用，固定版本保留精确过滤的路径已实测。T003 与 T004 可并行；T005 依赖 T004；T007 汇总全部基础任务。

## Phase 3: User Story 1 — 用可信的经纬度查询规则网格 (Priority: P1) — MVP

**Goal**: 显式网格与至少一个核验 domain 输出稳定、可信的坐标和值，支持分离/展平空间轴及额外轴。

**Independent Test**: 非方形样本逐位置比较独立坐标、官方值和缺测；真实 ncep_gfswave025 全域比较显式配置与 domain；无配置完全保持旧行为，非法配置返回行前失败。无需 US2 的读取优化即可验收。

### Tests for User Story 1

- [X] T008 [P] [US1] 在 `test/sql/spatial.test` 添加 SQL 契约检查：参数缺失/多余/NULL/小数点数/未知 order、grid-domain 互斥、未知 domain、shape/轴冲突、无轴证据单数组、大小写坐标列冲突、非有限/零步长、零大小/溢出/纬度越界；覆盖稳定 DESCRIBE、无配置回归和 quickstart 的 raw.om 六点参考。
- [X] T009 [P] [US1] 在 `test/native/regular_grid_test.cpp` 与 `test/native/spatial_layout_test.cpp` 添加独立参考测试，覆盖负步长、单行/单列、极点、两种经度约定、接缝重复位置、两个分离轴任意顺序、两种展平顺序、额外轴及跨批次逻辑位置；禁止用待测坐标函数构造期望值。
- [X] T010 [P] [US1] 在 `test/native/domain_reference_test.cpp` 定义真实 domain 参考校验：要求固定文件/源码身份、独立坐标及官方值，完整核对 15 个变量和 NULL/逻辑位置，文件缺失或哈希不符必须失败；通过测试专用路径参数读取样本，不将下载文件并入可重生合成 manifest。

### Implementation for User Story 1

- [X] T011 [US1] 在 `src/grid/regular_grid.cpp` 实现 RegularGrid 数值校验与按需坐标生成：有限非零步长、正整数大小、受检乘积及中间运算、纬度 [-90,90]、经度 [-180,180)；保留同坐标的不同源位置，不分配 nx×ny 坐标表，使 T009 的网格部分通过。
- [X] T012 [US1] 在 `src/grid/spatial_layout.cpp` 实现轴身份到位置/stride 的校验与转换，复用 `src/scan/dimensions.cpp` 的全变量对齐证据；实现 separate、lon_fastest、lat_fastest，检查轴名/长度/乘积并保留所有额外轴，不广播或自动赋时间语义，使 T009 的布局部分通过。
- [X] T013 [US1] 在 `test/tools/spatial_reference.hpp` 实现测试专用的真实样本参考导出/比较支持，在 `test/data/domain-manifest.json` 固定 research 的样本 URL、5,812,040 字节、SHA-256 `0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd`、上游 `34b9cea169395be9b4686f2b5b23eca26dfef7a2`、15 个变量布局及参考哈希；参考输出放 `build/evidence/spatial/reference/`，不调用生产映射，执行 T010 的来源和 oracle 检查。
- [X] T014 [US1] 在 `src/scan/read_om.cpp` 接入 grid/spatial_axes/domain 命名参数和 bind 校验，按 SQL 契约对原始 STRUCT 精确检查字段与无损整数转换，校验所有变量而非仅投影变量；扩展 `src/scan/schema.cpp` 在原值列后追加 latitude/longitude DOUBLE 并检测名称冲突；此时未发布 domain 正常拒绝。
- [X] T015 [US1] 扩展 `src/include/duckomo/projection.hpp` 和 `src/scan/projection.cpp`，将槽位区分为 value/latitude/longitude/cardinality，值变量按 ID 去重，坐标与 EMPTY 不索引 OM 数组，保留请求顺序和所有查询依赖；扩展 `test/native/projection_evidence_test.cpp` 验证旧投影路径不回退。
- [X] T016 [US1] 在 `src/scan/read_om.cpp` 的完整扫描路径按每个 BatchSegment 的原始逻辑位置生成坐标，与 T015 的值槽位共用同一位置序列；纯坐标只合成输出，无配置走原有值扫描，Copy/Equals 覆盖网格和布局，跨批次及额外轴不发生错位。
- [X] T017 [US1] 使用 T013 的真实 oracle 全域验证显式网格 `[ny=721,nx=1440,lat0=-90,lon0=-180,dlat=dlon=0.25]`，在 `test/native/domain_reference_test.cpp` 核对全部逻辑位置、15 个变量值/缺测和独立坐标；将结果与来源写入 `specs/002-spatial-pushdown/evidence/domain.md`，失败不得发布 domain。
- [X] T018 [US1] 在 T017 通过后实现 `src/grid/domain_registry.cpp` 与 `src/include/duckomo/domain_registry.hpp` 的单条已核验 ncep_gfswave025 定义并接入 `src/scan/read_om.cpp`；只允许 shape `[721,1440]` 及 matching coordinates `[lat,lon]` 的两轴布局，拒绝展平/时序布局、额外 spatial_axes、轴冲突及未知名称，不凭文件路径或 shape 单独推断。
- [X] T019 [US1] 执行 T008–T010、无配置旧测试和 domain/等价显式网格全域差分，在 `specs/002-spatial-pushdown/evidence/us1.md` 记录坐标容差、逐位置/NULL/行数及域核验结果；确认 domain 查询 count=1,038,240、范围与 quickstart 一致，完成 US1 独立验收。

**Checkpoint**: US1 可独立演示和交付；尚不能宣称 WHERE 已减少值数据读取。T017 的显式网格验证先于 T018 发布，避免“必须先注册 domain 才能核验”的循环依赖。

## Phase 4: User Story 2 — 查询小区域时减少实际读取 (Priority: P1)

**Goal**: 安全空间条件缩小官方 OM 逻辑读取；空区域、纯坐标和纯空间计数不读值。

**Independent Test**: 先物化全域结果再过滤，与区域查询逐位置/多重集核对；固定 projection.om 同列 full/restricted 的实际 data_bytes 和 decoded_chunks 均严格下降；三类零值读取场景指标为零。

### Tests for User Story 2

- [X] T020 [P] [US2] 在 `test/sql/spatial_pushdown.test` 添加单轴/双轴 `=,<,<=,>,>=,BETWEEN`、反向常量比较、整数常量、点上/点间/nextafter 开闭边界、矛盾/不相交条件、常量 FALSE、仅坐标/count 的物化基准差分；包含负步长和经度接缝后的单区间过滤。
- [X] T021 [P] [US2] 在 `test/native/spatial_selection_test.cpp` 添加一维命中区间与 N-D 有界游标测试：各布局/额外轴位置、半开区间、每个源位置恰一次、跨批次、空选择、受检溢出；断言只产生合法最后一轴连续 offset/count，不将零 count 传给 OM decoder。
- [X] T022 [P] [US2] 在 `test/native/spatial_io_test.cpp` 固定 projection.om 的 grid（纬度 -41+y、经度 -126+2*x）、区域 [-2,2]×[-4,4] 和相同输出 temperature 列，添加 full/restricted 两项严格下降及 empty/coordinates/count 的 index/data/decode 全零断言，所有变量需有显式零值记录而非缺字段视为零。

### Implementation for User Story 2

- [X] T023 [US2] 在 `src/scan/spatial_filter.cpp` 实现当前 LogicalGet 坐标列的安全比较/BETWEEN/AND 提取：核对表/列绑定和 depth，处理反向比较及安全折叠的有限常量；存自有数值约束，不留表达式借用指针；不支持子树保留 residual，OR 不抽单分支。
- [X] T024 [US2] 在 `src/scan/spatial_selection.cpp` 实现 full/restricted/empty 选择、按输出坐标精确比较的一维区间游标与额外轴遍历；复用 `src/scan/batch.cpp` 生成最大批次以内连续逻辑段，合并可连续位置，避免逐点 DecodeSelection；不得生成全域坐标数组、文件 chunk ID 或字节规划。
- [X] T025 [US2] 在 `src/scan/read_om.cpp` 注册 complex-filter callback，仅保存 T023 的安全必要条件且不删除/改写输入表达式；保留 projection_pushdown=true、filter_pushdown=false、filter_prune=false；Copy/Equals 覆盖选择状态，SupportStatementCache 返回 false，多次回调不积累过期条件。
- [X] T026 [US2] 将 `src/scan/read_om.cpp` 的扫描推进切换为 T024 的选中逻辑段，全部依赖变量和坐标共享原始位置，通过现有 `src/om/reader.cpp` DecodeSelection 读取有界切片；检查跨批次/多变量/额外轴正确，保持官方库负责块交集及真实 I/O。
- [X] T027 [US2] 在 `src/scan/read_om.cpp` 实现空选择直接 exhausted 和无值依赖的坐标/cardinality 分流，避免创建 decoder/读取值索引，空扫描显式标记完成；在 `src/include/duckomo/metrics.hpp` 正确记录 candidate_rows 与 selection_mode，优化器消除扫描不得被记为 incomplete_scan。
- [X] T028 [US2] 实现 `test/tools/duckomo_spatial_validation.cpp` 的核心 CLI，按契约接收 root/fixtures/output/duckdb/extension/domain-file 并复用既有 `test/tools/duckomo_validation.cpp` 的子进程设施（必要时提取到 `test/tools/validation_support.hpp`）；完整消费 full/restricted/empty/coordinates/count 输出、比较独立参考和先物化基准，记录查询级 v2 sidecar、耗时/RSS/架构/缓存；缺必需样本或计量失败非零退出，optimizer_empty 需计划/成功结果及 bind 计量证据。
- [X] T029 [US2] 执行 T020–T022 和 T028 五类场景，使用同文件/同列/同环境验证 data_bytes、decoded_chunks 严格减少及三类零读取，将 JSON 和命令汇总到 `specs/002-spatial-pushdown/evidence/us2.md`；不得运行后改区域挑选收益，耗时/请求数/RSS 仅如实记录。

**Checkpoint**: 坐标正确和真实读取节省同时成立。单块文件不作为收益样本，COUNT 不能包装值查询充当性能证据。

## Phase 5: User Story 3 — 将区域过滤与现有分析组合 (Priority: P2)

**Goal**: 与值过滤/投影/聚合组合正确，复杂条件安全回退，失败恢复和观测可复现。

**Independent Test**: 多变量固定样本的区域+值过滤、表达式/排序/聚合及跨经线 OR 与物化基准一致；pressure 等无关变量不解码，记录区分 restricted/empty/fallback，取消或失败后能成功重查。

### Tests for User Story 3

- [X] T030 [P] [US3] 在 `test/sql/spatial_composition.test` 添加 temperature 输出/humidity 过滤/pressure 无依赖、重复与重排列、表达式、排序、聚合、NULL、跨接缝 OR/重复接缝位置、反向 BETWEEN、混合 AND/OR、不支持分支、坐标 cast/函数、NULL/Inf/NaN 边界的物化差分；结果必须保留源记录多重性。
- [X] T031 [P] [US3] 在 `test/native/spatial_lifecycle_test.cpp` 添加同一 prepared statement 更换区域、多个独立扫描/别名、失败 bind、扫描中损坏与取消、跨批次失败后恢复及至少 100 次成功/失败交替的资源检查；断言失败/取消不产生成功残缺结果和有效性能比值。

### Implementation for User Story 3

- [X] T032 [US3] 在 `src/scan/spatial_filter.cpp` 完成复杂树回退与诊断：无安全必要条件标 fallback，AND 的独立安全子句允许 restricted 并保留回退原因，OR/NOT/函数子树不局部缩小；`src/include/duckomo/metrics.hpp` 记录 residual_filter_retained 与 fallback_reasons，不更改完整 WHERE。
- [X] T033 [US3] 在 `src/scan/projection.cpp` 与 `src/scan/read_om.cpp` 完成选中位置上的混合依赖集成，保留未输出的过滤变量/坐标、重复引用只解码一次、按同一位置对齐所有值与坐标；修正 T030 暴露的列裁剪/聚合/NULL 问题并在 `test/native/spatial_io_test.cpp` 断言无关 pressure 的 index/data/decode 为零。
- [X] T034 [US3] 在 `src/scan/read_om.cpp` 完成查询生命周期隔离：验证关闭 statement cache 后 prepared statement 重绑定不继承旧区域，跨别名/多扫描不串选择或 metrics；所有 bind/scan 异常与取消释放 grid/cursor/decoder/file 所有权并标 failure/cancelled，使 T031 通过。
- [X] T035 [US3] 扩展 `test/tools/duckomo_spatial_validation.cpp` 的 mixed/fallback/domain 场景，跨接缝 OR 与复杂条件对照完整基准，复用 T013 的真实 oracle 并完整核对 domain 与显式配置；汇总全部场景实际 bytes/requests/chunks/elapsed/RSS 和优化模式，失败及不完整计量不得进入收益比较。
- [X] T036 [US3] 执行 T030–T031、混合依赖原生检查及完整 spatial harness，将组合正确性、回退诊断、无关变量零解码和错误/取消恢复结果记录到 `specs/002-spatial-pushdown/evidence/us3.md`；同时回归 US1/US2 的验收条件。
- [X] T037 [US3] 在 `test/native/spatial_metrics_test.cpp` 与 `test/tools/duckomo_spatial_validation.cpp` 加入证据完整性反例检查（缺字段、错哈希、失败状态、decode_count_complete=false、缺 sidecar、混淆 optimizer_empty/fallback），确保无法将这些记录当成功；将通过记录附入 `specs/002-spatial-pushdown/evidence/us3.md`。

**Checkpoint**: 三个故事均通过各自独立验收；完整空间报告能说明减少了哪些实际工作，以及哪些条件回退。

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: 将新增能力纳入标准验证入口、同步文档，并完成平台和使用验收。

- [X] T038 在 `test/CMakeLists.txt`、`Makefile` 和 `scripts/validate.sh` 将全部空间 SQL/native、合成 fixture 重生成/哈希检查接入标准门禁，并将 grid/selection/lifecycle 关键路径纳入 sanitizer 插桩目标；合成门禁不要求下载真实文件，完整 domain 发布门禁单独执行带必需 domain-file 的 harness。
- [X] T039 [P] 更新 `README.md` 和 `README.en.md` 的显式 grid/domain、坐标约定、轴布局、区域/跨接缝查询、错误和回退说明，使用已通过的 SQL，明确支持的平台与 domain 布局，不宣称所有文件都节省读取。
- [X] T040 [P] 更新 `docs/spec.md`、`docs/architecture.md` 和 `docs/roadmap.md`，记录 complex callback 保留精确 WHERE、GridMapping 逻辑选择与官方 reader 的职责、实际支持矩阵及已通过证据；不将时间/远程/投影网格等后续能力标成已实现。
- [X] T041 [P] 核对并更新 `specs/002-spatial-pushdown/quickstart.md`、`specs/002-spatial-pushdown/contracts/sql-interface.md` 和 `specs/002-spatial-pushdown/contracts/validation-evidence.md` 中的真实命令、CLI 参数、状态、输出及 v2 metadata 口径，链接各故事证据并明确缺样本/哈希不符的失败行为。
- [X] T042（范围调整：按用户决定，Linux x86_64 支持与验证延期，本期不作为门禁；未执行，不声明兼容性。）
- [X] T043 在 Linux AArch64 环境执行同套 release/SQL/native/完整空间检查，将回归证据独立写入 `specs/002-spatial-pushdown/evidence/linux-aarch64.md`，不混用两种架构的耗时、RSS 或依赖产物。
- [X] T044 执行 `make sanitizer-test`，确认新增 grid/selection/生命周期代码确实插桩，记录边界/溢出/损坏/取消检查与插桩范围到 `specs/002-spatial-pushdown/evidence/sanitizer.md`；性能指标仅来自普通 release 构建。
- [X] T045 由未参与实现的验证者按 `specs/002-spatial-pushdown/quickstart.md` 完成显式网格、domain、区域结果与读取收益核对，在 `specs/002-spatial-pushdown/evidence/quickstart-review.md` 记录执行环境、命令、结果和说明修正；未获得独立复现结果不能把 SC-006 标为通过。
- [X] T046 在 `specs/002-spatial-pushdown/evidence/final.md` 汇总 FR-001–016/SC-001–006 到实际证据的映射、fixture/上游/依赖身份、三个故事、本期平台结果及延期平台范围、严格 I/O 门槛、残余限制和失败记录，并核对本文件勾选状态；所有本期门禁均已通过，未对延期平台作兼容声明。

## Dependencies & Execution Order

### Phase dependencies

```text
T001 → T002 → Phase 2 (T003–T007)
                    ↓
             US1 (T008–T019)
                    ↓
             US2 (T020–T029)
                    ↓
             US3 (T030–T037)
                    ↓
       标准门禁接入 T038 → 文档 T039/T040/T041
                    ↓
       范围决定 T042 → 平台/资源 T043/T044 → 独立复现 T045 → 汇总 T046
```

US1 的可信映射是 US2 必需输入，US2 的选择/计量是 US3 必需输入，因此不把三个故事伪装成可同时完成。每个故事在已有前置完成后有自己的验收，后续故事失败不否定前一故事已取得的证据。

### Within-phase dependencies

- Phase 2：T003、T004 可并行；T005 在 T004 后；T006 可在基础接口就绪后单独验证；T007 等待 T003–T006。
- US1：T008/T009/T010 先编写测试；T011→T012，T013 独立建立真实 oracle；T014 等待网格/布局，T015→T016 等待参数/schema；T017 等待 T013/T016，T018 等待 T017，T019 汇总测试并包含已注册 domain。
- US2：T020/T021/T022 先定义验收；T023→T024→T025→T026→T027→T028→T029。选择与 callback 接入前必须保留完整 residual，不能为让早期测试通过删掉 SQL 条件。
- US3：T030/T031 先定义验收；T032→T033→T034→T035→T036→T037。T037 是对已产生证据的反例校验，不能用它替代正向场景执行。
- 最终阶段：T038 后 T039/T040/T041 修改不同文档可并行；本期平台门禁为 Linux AArch64，x86_64 已按用户决定延期；T045 等待代码/文档稳定，T046 等待全部本期门禁。

### Shared-file ownership

所有实现任务默认顺序执行。标注 `[P]` 仅针对下面列出的独立文件组；其共同构建注册统一由一个执行者在组前后串行完成。不得同时修改 read_om.cpp、metrics.hpp、fixture manifest 或 spatial harness。若实际任务需要修改其他任务拥有的文件，先串行合入再继续，不能覆盖他人修改。

## Parallel Example: User Story 1

基础阶段完成后，可同时编写 T008 的 `test/sql/spatial.test`、T009 的 `test/native/regular_grid_test.cpp`/`spatial_layout_test.cpp`、T010 的 `test/native/domain_reference_test.cpp`。三者使用已固定接口/参考约定，不依赖彼此实现；CMake 注册串行完成。T011–T019 按依赖合入并逐项运行对应测试。

## Parallel Example: User Story 2

US1 完成后，可同时编写 T020 的 `test/sql/spatial_pushdown.test`、T021 的 `test/native/spatial_selection_test.cpp`、T022 的 `test/native/spatial_io_test.cpp`。比较基准和区域由契约预先固定，各文件互不修改。实现与指标采样在这些测试定义完成后按序执行。

## Parallel Example: User Story 3

US2 完成后，可同时编写 T030 的 `test/sql/spatial_composition.test` 与 T031 的 `test/native/spatial_lifecycle_test.cpp`。后续 projection/callback/read_om/harness 修改共享代码，按序完成。最终文档 T039/T040/T041 是另一组可并行工作。

## Implementation Strategy

### MVP First

完成 T001–T019，交付 US1：显式网格、两类布局、可靠坐标、一个全量核验的真实 domain、无配置兼容与错误拒绝。此 MVP 不包含读取节省承诺；roadmap Phase 3 的核心价值需要继续完成 US2，整个 feature 还需要 US3 和最终门禁。

### Incremental Delivery

每个故事先完成其验收测试和独立参考，确认预期缺失行为，再实现必要代码、执行对应门禁并记录证据。US1→US2→US3 逐步扩展，沿用已有 raw/projection 回归。测试失败时修复根因，不放宽坐标比较语义、不改变固定区域、不删除复杂条件或用输出行数代替真实指标。仅生成任务不自动提交、发布或实现代码。

## Requirement Coverage

| 要求 | 主要任务 | 通过证据 |
| --- | --- | --- |
| FR-001–003 | T008–T014、T018–T019 | US1 参数、schema、轴证据与拒绝测试 |
| FR-004–005 | T009、T011–T012、T015–T019 | 独立坐标/值/NULL 逐位置及无配置回归 |
| FR-006–007 | T020–T027、T030、T032 | 精确边界、保留 residual、安全必要条件与回退 |
| FR-008 | T003、T030、T032、T035–T036 | 跨接缝 OR、重复逻辑点与反向 BETWEEN |
| FR-009 | T022、T027–T029 | 空/坐标/纯空间 count 值 index/data/decode 零 |
| FR-010–011 | T009、T012、T015–T016、T021、T026、T030、T033 | 额外轴、混合依赖、投影及多变量对齐 |
| FR-012–013 | T004–T005、T022、T028–T029、T035–T037 | v2 全阶段计量、两项严格下降、失败证据拒绝 |
| FR-014 | T010、T013、T017–T019、T035 | 固定真实文件/上游版本及独立全量 oracle |
| FR-015 | T039–T041、T045 | 文档和独立复现 |
| FR-016 | T031、T034、T037、T044 | 失败/取消、恢复、资源及 sanitizer |
| SC-001–002 | T019、T029、T036 | 全域坐标、domain、布局/边界覆盖 |
| SC-003–004 | T029、T033、T035–T037 | 读取减少、零依赖读取及真实指标 |
| SC-005 | T019、T030–T037、T044 | 负向、组合、回退及恢复 |
| SC-006 | T039–T041、T045 | 未参与实现者按指南复现 |

## Notes

共 46 项：公共 Setup/Foundation 7 项，US1 12 项，US2 10 项，US3 8 项，最终阶段 9 项；13 项标记 `[P]`。生成时全部未勾选，不预报实现或性能通过。真实样本不可用、哈希变化、目标平台或独立验证者缺失应记录具体缺口并保留对应任务，不用合成文件或现有平台证据替代。
