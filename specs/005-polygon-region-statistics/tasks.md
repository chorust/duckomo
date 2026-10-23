---
description: "005 多区域 Polygon 读取与等权统计实施任务"
---

# Tasks: 多区域 Polygon 读取与等权统计

## 2026-10-20 当前产物收口

原50项实施任务保持历史完成记录。本轮按负责人批准的[成本修订](contracts/non-increasing-cost-20261020.md)，三版本当前源码P0–P5、每版34 native/16 Python suites/21 SQL/17 sanitizer全部通过，P6完整审计通过，最终产物独立AI P7复验通过（非人工）。源码、产物、命令和证据hash见[evidence/current-20261020.md](evidence/current-20261020.md)。连续矩形优化不改变原区域/值/NULL/source语义；旧P5失败、证书过期和构建测试失败均保留。新结论优先于下文旧产物待验收描述，不追溯改写旧包。

**Input**: `specs/005-polygon-region-statistics/` 下的 spec.md、plan.md、research.md、data-model.md、contracts/ 和 quickstart.md。

**Prerequisites**: 按上述设计实现；治理依据为 spec 与现行项目契约，`.specify/memory/constitution.md` 仍为占位模板。使用现有 `main`，不创建分支或工作树。完成标记只反映对应任务已有实现/证据，不代表相关完整 gate 已通过。

**Tests**: spec 明确要求独立参考、SQL/native、零值 I/O、资源/生命周期、真实远程、三版本回归和独立复现，故包含相应测试任务。测试先写并确认能暴露目标缺陷，再实现；不另加通用测试框架。

**Organization**: 四个故事保持 spec 的顺序（US1/US2/US3 为 P1，US4 为 P2）。共享的输入安全与依赖可行性是前置条件；不能推迟到 US4 才校验输入。每个故事有独立验收目标，实际依赖见文末。

## Format: `[ID] [P?] [Story] Description`

- 每项使用 `- [ ] Tnnn`，故事任务带 `[USn]`，路径相对仓库根。
- `[P]` 只表示满足列明前置条件后，可与不同文件的任务并行；不授权启动额外 agent。
- 共享 `read_om.cpp`、projection、metrics、CMake/Makefile、manifest 和 validator 的修改依次集成。同一文件不安排多个 writer。
- 现有 Python 工具只用于开发期参考/审计；查询运行时不加入 Python、Swift、私有 spatial/GEOS ABI 或新统计引擎。

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: 冻结可追溯依赖、输入和独立参考，登记构建入口。

- [X] T001 [P] 在 `scripts/fetch-region-runtime.py` 实现匹配官方 spatial 包的获取、下载/解压 SHA256 校验和小型自检，复用 `scripts/version_matrix.py` 的现有身份/下载能力，生成 `test/data/regions/runtime-matrix.json`；继承 `test/data/grids/version-matrix.json` 的 OM/ci-tools/CLI/HTTPFS pins，并固定 research R7 的 v1.5.4/v1.5.5/v1.5.6 engine/spatial pins 与 Linux AArch64；记录包 URL、失败原因及身份差异，不猜包 hash、不跟随 latest。
- [X] T002 [P] 扩展 `test/tools/duckomo_fixture_tool.cpp`，以官方 OM writer/reader 生成 `test/data/regions/regions.om` 和独立值参考：Float32 `[member=2,time=3,lat=5,lon=5]`，值 `1000*m+100*t+10*y+x`，t=2 与中心点为 NaN；同时生成 rank/轴序、多值列、重复坐标、无时间轴、长窗及 ±Inf 的补充 fixture，保持现有 fixture 内容/hash 不变。
- [X] T003 在 `scripts/generate-region-reference.py` 生成 `test/data/regions/manifest.json`、10 个冻结区域的标准 WKB/身份、membership 与统计 oracle；依赖 T002，严格采用 validation contract 的 A/B/Hole/Empty/Outside/Multi/CopyA/Narrow/Gap/Corner，断言总记录数 438 及 A/m0/t0 的 9/8/77/9.625/0/21；一般 synthetic 用独立开发期点面算法，加入边界、nextafter、洞及任一 endian 自检，禁止调用 DuckOMO adapter 或用它生成期望。
- [X] T004 在 `test/data/regions/manifest.json` 冻结 ncep_gfswave025、gem_rdps_10km、gem_regional、aladin_central_europe_2km、ecmwf_ifs O1280 的对象 SHA/大小、变量、轴/chunk、区域 WKB/hash、完整 SQL、投影和 full/local 条件；用已有官方值及 producer 坐标参考核验原始位置/坐标/值基线，把结果和缺输入状态写入 `specs/005-polygon-region-statistics/evidence/baseline.md`，必须在性能测量前完成，不修改历史 004 manifest/证据。
- [X] T005 在 `CMakeLists.txt`、`test/CMakeLists.txt` 和 `Makefile` 按现有模式登记 plan 中的 region_input/region_geometry 源、三项 region native 测试、regions SQL 文件、fixture/region validator 与验证目标；可先沿用缺文件时的配置行为，但完成时必须实际构建全部登记目标，不新增依赖框架或普通读取的 spatial 链接依赖。

**Checkpoint**: 输入与参考可重生且带 hash；官方包未取得时保持 blocked/not-run，不能编造身份或通过记录。

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: 先证明 host spatial adapter 可运行，再提供所有故事共用的完整输入校验。

**Critical**: T008 的真实 adapter capability 子检查通过前，不开始区域 scanner 接入。输入/oracle 的离线准备可以推进；完整 P0 还需 US1/US4 的绑定用例。

- [X] T006 在 `test/native/region_geometry_test.cpp` 编写真实加载匹配 spatial 的 adapter 测试，覆盖系统 catalog 精确签名、Polygon 外边界/洞边界/洞内部、EMPTY、非法整体 MultiPolygon、临时同名函数覆盖、异常释放和 worker expression state 隔离；同时记录 `duckdb_extensions()` 的 extension_version/install_path、函数 probes 与包 hash，不把 SQL 冒烟等同 C++ adapter 通过。
- [X] T007 在 `src/include/duckomo/region_geometry.hpp` 与 `src/scan/region_geometry.cpp` 实现 RegionGeometryAdapter/RegionPredicateEvaluator：通过系统 catalog 和 FunctionBinder 绑定 ST_GeomFromWKB(BLOB)、ST_IsValid、ST_Point(DOUBLE,DOUBLE)、ST_Covers 的准确签名；确认 spatial 显式加载，拒绝 shadow/signature mismatch，不自动安装/加载；绑定模板可复制，worker 独占 ExpressionExecutor/local state，owned GEOMETRY 左常量安全引用，每批最多 256 点且保持源位置顺序。
- [X] T008 用 T001 的匹配产物运行 `test/native/region_geometry_test.cpp`，并在无 spatial 的独立进程运行现有 `test/sql/read_om.test` 等普通读取基线，把能力、异常/状态释放及依赖缺失结果记入 `specs/005-polygon-region-statistics/evidence/dependency.md`；这是 P0 前置子检查，包 403、实际加载失败或 adapter 不可用则停止 scanner 接入并记录阻塞，不回退私有 ABI/自写拓扑。
- [X] T009 在 `test/native/region_input_test.cpp` 编写严格输入与拥有容量测试：精确 LIST/STRUCT/字段类型和大小写、字段顺序无关、UTF-8/ID 唯一、NULL/空集合、WKB endian/EMPTY、截断/尾随/非法计数/Z/M/EWKB/非面嵌套/未闭合/非有限坐标、接缝/极区及限额边界；断言受检乘加不溢出，校验全集不因前项 EMPTY 而短路。
- [X] T010 在 `src/include/duckomo/region_input.hpp` 与 `src/scan/region_input.cpp` 实现 immutable RegionCollection 和非递归标准 2D WKB 审计，仅计算 envelope/计数与结构安全，拓扑交 T007；严格执行 grid_lonlat、ID 1–256 字节、4096 区域、1 MiB/区域与16 MiB总 WKB、65536/区域与1048576总顶点、64 MiB owned input、2 MiB/区域 opaque geometry；分配/复制前检查，逐区域释放 transient，每≤256顶点检查取消，拥有输入副本且内容可比较，错误不自动修复/转换/截断。

**Checkpoint**: host 接入与安全输入模块可独立测试；普通 read_om 没有新增空间依赖。基础校验服务 US1 和 US4，不在后续故事重复实现。

## Phase 3: User Story 1 — 一次读取多个区域中的原始点值 (Priority: P1) — MVP

**Goal**: 单对象一次返回精确的区域—原始记录关系、点值、坐标及原有维度。

**Independent Test**: 教学 fixture 的 438 条记录与人工/独立 membership 和官方值参考逐位置逐值一致；双向 EXCEPT ALL 差异为 0，洞、边界、重叠、同几何不同 ID、MultiPolygon、EMPTY 及重复坐标均覆盖。完整 P1 还须五类真实对象的坐标/值差分，不能以 synthetic 替代。

### Tests for User Story 1

- [X] T011 [P] [US1] 在 `test/sql/regions.test` 编写新入口正向绑定和行语义测试：字面量/getvariable、字段重排、PREPARE/EXECUTE 与变量变化后 rebind、typed empty、grid/domain 和 axes、region_id/om_source 列序与冲突、无时间对象、重叠/洞/边界/MultiPolygon/CopyA，并对完整物化 read_om 基准做双向 EXCEPT ALL。
- [X] T012 [P] [US1] 在 `test/native/region_scan_test.cpp` 编写任务与跨批回归：一个任务/批次一个区域，源位置递增，区域 ordinal 跨部分输出保持不变，跨窗不重复，同源记录跨区域保留，空中间 batch 不提前 EOF，游标耗尽但 outstanding>0 不标完整；覆盖长窗、轴序、多值列与相同坐标不同原始位置。

### Implementation for User Story 1

- [X] T013 [US1] 在 `src/scan/read_om.cpp` 增加 sibling factory GetReadOmRegionsFunction 并共用现有 bind/init/local/scan 回调，在 `src/include/duckomo/read_om.hpp` 声明、`src/om_extension.cpp` 注册；regions/region_crs 注册为 ANY 后严格检查实际类型，复用原有参数与 OM/grid/axis 验证，全集校验先于 empty/LIMIT；BindData Copy/Equals 包含模式/区域内容/声明，继承 SupportStatementCache=false，不借用可变输入或继承 final 类。
- [X] T014 [US1] 在 `src/include/duckomo/projection.hpp`、`src/scan/projection.cpp` 和 `src/scan/read_om.cpp` 增加 RegionId 合成槽，默认原列后追加 region_id、include_source=true 时 om_source 仍最后；按 DuckDB 名称比较规则拒绝 region_id 冲突，即使冲突值列未投影，保持普通入口 schema、虚拟列和 required variable/filter dependencies。
- [X] T015 [US1] 在 `src/scan/read_om.cpp` 扩展惰性 region/window cursor：只保存当前区域和 native window cursor，短锁领取带 task_id/region_ordinal 的 descriptor 并登记 outstanding，EMPTY 跳过；任务自身持久保存 ordinal，保留每任务成功/空的单次结算，不预物化区域×窗口/点表。
- [X] T016 [US1] 在 `src/scan/read_om.cpp` 将 immutable 用户必要条件与 whole envelope 在锁外求交，复用网格输出坐标和 T007 做 exact-before-decode，稳定压缩 survivors 后才调用既有 BuildSelectedBatchSegments/DecodeSelection 并输出当前 region_id；空子批继续领取/迭代，多区域重复位置不混进单个递增 batch，完整 WHERE/filter pruning 策略不变，不建立第二套物理 chunk planner。
- [X] T017 [US1] 运行 `test/sql/regions.test`、`test/native/region_input_test.cpp`、`test/native/region_geometry_test.cpp` 和 `test/native/region_scan_test.cpp`，修复实际集成偏差，将教学438条、补充rank/轴序/多值/重复位置及快照 rebind 的结果/hash写入 `specs/005-polygon-region-statistics/evidence/us1-local.md`；未覆盖的完整 P0/P1 条件保持 not-run。
- [X] T018 [US1] 在 `test/tools/duckomo_region_validation.cpp` 实现 validation contract 的 CLI 和 P0/P1：只写新建空 output，校验 manifest/产物/输入 hash，保存 commands/exit codes、结果/oracle hash 和逐 scan metrics，以人工/独立几何及官方值参考为主、完整物化 read_om+ST_Covers 双向差分为辅；请求缺输入退出2、实际失败1、全部请求通过0，支持局部执行但不替未请求 gate 签署通过。
- [X] T019 [US1] 用 `test/tools/duckomo_region_validation.cpp` 执行当前目标版本 P0 的已实现绑定范围和 P1 synthetic/五类真实对象完整差分，保存 `specs/005-polygon-region-statistics/evidence/us1-correctness.md` 及 `build/evidence/regions/` bulk artifacts；值 Float32/NULL 精确、边界不借坐标容差扩大，缺真实输入不得将完整 P1 写为 pass，P0 缺依赖负例由 T041 补齐。

**Checkpoint**: 原始点值 MVP 可演示；不代表局部收益、完整资源/生命周期或三版本交付已验收。

## Phase 4: User Story 2 — 获得各区域在各时刻的基础统计 (Priority: P1)

**Goal**: 使用原生 SQL 获得等权 AVG/MIN/MAX/SUM、样本总数和有效值数，并显式补齐空组。

**Independent Test**: 10区域×3时刻×2成员共60组与冻结 oracle 一致；A/m0/t0 为 sample=9、valid=8、sum=77、avg=9.625、min=0、max=21。t2 为9/0/NULL，Empty/Outside/Gap 补齐后为0/0/NULL；有限 sum/avg 的误差≤max(1e-6,1e-6×abs(reference))，计数及 min/max 精确。

### Tests and implementation for User Story 2

- [X] T020 [US2] 扩展 `test/sql/regions.test` 的区域/valid_time/member/level 分组验收，使用 T003 oracle 验证 COUNT(*) 与 COUNT(value)、原始 NaN→NULL、±Inf 宿主聚合、重复时间标签与原始位置区别；显式区域×时间×成员集合先聚合再 LEFT JOIN，仅将计数 COALESCE 为0，数值聚合保留 NULL，不制造原始占位行或推断时间。
- [X] T021 [US2] 在 `test/tools/duckomo_region_validation.cpp` 增加 P2 完整分组/oracle 校验和冻结浮点容差，记录60组结果与 hash，区分无样本/全 NULL/非有限值；直接执行原生 SQL，不新增 om_reduce、聚合器或统计缓冲层。
- [X] T022 [US2] 校对并执行 `specs/005-polygon-region-statistics/quickstart.md` 中的区域表→WKB list 快照、多区域原始查询、分时统计及空组补齐 SQL；保留3区域演示108条/18组与10区域验收438条/60组的区分，使用实际产物路径，不把实现者执行写成 P7 独立复现。
- [X] T023 [US2] 执行 `test/sql/regions.test` 与 region validator P2，把精确计数/极值、sum/avg误差及空/缺测/非有限判读写入 `specs/005-polygon-region-statistics/evidence/us2-statistics.md`，失败时修正查询/集成而不更改教学 fixture 或冻结 oracle。

**Checkpoint**: US2 仅依赖 US1 的普通点值输出，可独立验证统计；不等待远程收益，也不引入专用计算语言。

## Phase 5: User Story 3 — 查询局部区域而不必读取全部值 (Priority: P1)

**Goal**: 精确筛选先于值解码、无值依赖保持零值成本，并在预冻结可跳块对象上证明实际收益和有界工作内存。

**Independent Test**: region/coordinate/source-only、COUNT(*)、typed empty/all EMPTY/轴空/精确无点的 value index/data/decode 均为0；不投影 value 的值过滤仍正确。五类相同内容 local/HTTP/HTTPS/签名S3结果一致，HTTP/S3冷full/local的 value bytes、decoded blocks、服务发送量严格下降。固定区域/chunk/线程/窄结果，≥10×空间点数的完整自有 scan work peak 与隔离进程 RSS 增长比均≤2。

### Tests for User Story 3

- [X] T024 [US3] 扩展 `test/native/region_scan_test.cpp` 的零 I/O 与资源测试，覆盖仅区域/位置/source/count、集合空/全EMPTY/轴空/候选非空但exact为空、未投影值过滤、4096/4097 selector ranges 回退、复杂多区域及1/2/4实际active workers；测试钩子确认 ST_Covers 在 segments/decode 前执行且 metadata/坐标准备不计成值成本。

### Implementation for User Story 3

- [X] T025 [US3] 在 `src/scan/read_om.cpp` 核对并补齐无值依赖分支：RegionId/Cardinality 仍执行 exact membership，但不创建值 reader/decoder/segments；保留 ObserveComplexFilter 的未投影值依赖，empty须完成全部参数/区域/OM绑定后才短路；必要时仅调整 `src/scan/projection.cpp` 的共享映射，不从 envelope 候选数推断 count。
- [X] T026 [US3] 在 `src/scan/read_om.cpp` 与 `src/scan/region_geometry.cpp` 落实 native window≤65536位置、task map≤512KiB、selector≤256KiB/4096 ranges、geometry batch≤256点与新增scratch≤64KiB/worker、一个当前opaque geometry≤2MiB、原output/segment预算；复用 `src/include/duckomo/selection_budget.hpp`、`src/scan/axis_selection.cpp`、`src/scan/batch.cpp` 的机制，超预算只安全扩大候选或缩批，最小批仍不满足则失败，保留exact与完整WHERE。
- [X] T027 [P] [US3] 扩展 `test/native/scan_metrics_v4_test.cpp` 的 regions additive schema、region_record_pair 单位和终态断言，并在 `test/native/scan_metrics_v3_test.cpp` 校验legacy原义不变；匹配计数不能用SQL输出反推，未完成 exact=NULL，spatial内部峰值=NULL/complete=false，远程transport未知不可补零。
- [X] T028 [US3] 在 `src/include/duckomo/metrics.hpp` 增加 `operation='read_om_regions'` 与 execution contract 的 regions 字段：输入量/峰值、candidate/tested/matched pairs、geometry batches/evaluations、envelope rejects、prepare/evaluate ns、exact/complete和owned work；保持普通字段、legacy_v3/v2、duckomo_last_scan_metrics schema与一次调用一次scan，不把服务端发送量写入客户端transport。
- [X] T029 [US3] 在 `src/scan/read_om.cpp`、`src/scan/region_input.cpp` 和 `src/scan/region_geometry.cpp` 接入 T028 的实际计数/计时与现有 owned ledger：完整计入既有 scanner/reader/decoder/selector/batch 和新增 scratch/task，input存储单列，只在成功且cursor/outstanding全部完成时发布exact；第三方transient/spatial内部保持unknown，不记录原始WKB、区域文本、secret或敏感URI。
- [X] T030 [US3] 在 `test/tools/duckomo_region_validation.cpp` 增加 P3 和 P4 的资源/并行子检查：验证所有零值路径/值过滤、fallback无漏点、线程完整多重集合及实际active workers；以固定区域/chunk/线程/相近窄结果比较≥10×点数的完整owned work≤2×，另起隔离进程测total peak RSS≤2×并记录启动/输入基线和不可细分依赖内存；未解释增长或缺测不能通过。
- [X] T031 [US3] 执行 `test/native/region_scan_test.cpp`、metrics回归及 validator P3/P4资源子集，保存 `specs/005-polygon-region-statistics/evidence/us3-resources.md`，记录容量边界、1/2/4worker、10×/2×实验与unknown字段；P4生命周期尚未由 T041 覆盖时只登记子检查结果，不把完整P4标通过。
- [X] T032 [US3] 在 `test/tools/duckomo_region_validation.cpp` 增加 P5，复用 `scripts/setup-remote-fixtures.py`、`scripts/validate-official-httpfs.py` 和 `test/tools/validation_support.hpp` 的受控服务/官方HTTPFS模式，只读已有授权配置；五类冻结同内容对象逐一完整消费local/HTTP/HTTPS/签名S3结果，同列冷态full/local比较计入跨区域重复解码及全部重试，审计服务JSONL/hash并脱敏，无法确认冷态或来源缺失则not-run。
- [X] T033 [US3] 用 `test/tools/duckomo_region_validation.cpp` 执行当前版本五类 P5，将一致性、value bytes/decoded blocks、HTTP及签名S3服务发送量严格下降的原始结果与完整终态保存到 `specs/005-polygon-region-statistics/evidence/us3-remote.md`；全扫基准只读一次再生成同区域多重集合，不重复N次全扫制造收益，不测后选区域/改oracle/加decoded cache。
- [X] T034 [US3] 在 `specs/005-polygon-region-statistics/evidence/us3-remote.md` 汇总逐对象/来源的P3与P5范围、远程对象变化/权限/短读行为和收益结论，引用已冻结输入及受控服务日志hash；收益失败先定位候选/window/segments的真实成本并修复相应共享文件，修订设计须说明，LIMIT/失败/取消/未知成本不进入成功收益样本。

**Checkpoint**: 当前版本局部收益有独立证据；完整 P4 与三版本 P6 仍需后续任务。US3 未通过不能把原始点值演示称作完整功能交付。

## Phase 6: User Story 4 — 理解输入错误和执行边界 (Priority: P2)

**Goal**: 全集无效输入明确拒绝，缺依赖诊断可操作，取消/失败/提前停止资源释放且随后查询成功，普通入口无空间依赖。

**Independent Test**: 无效末项配合WHERE/LIMIT仍整体失败，错误类别符合SQL contract且无敏感内容；隔离进程有/无spatial两种行为正确；在准备/geometry/decode取消和故障注入后同连接有效查询成功，LIMIT与失败/取消的exact为NULL、不计收益，QueryEnd只发布一次。

### Tests for User Story 4

- [X] T035 [P] [US4] 在 `test/sql/regions_spatial_dependency.test` 编写有/无spatial隔离进程用例，确保测试runner的加载状态不会掩盖缺依赖；普通read_om/read_om_raw/om_grid_info schema与行为不变，新入口含空集合也报告 `[spatial_dependency]`，临时同名覆盖/签名不符拒绝且不隐式安装/联网。
- [X] T036 [P] [US4] 扩展 `test/sql/regions.test` 的用户可见负例，涵盖精确类型/NULL/重复ID/非法拓扑/坐标声明/接缝/极区/未知grid/列冲突、预算边界和溢出；末尾非法项配合只选前一区域的WHERE或LIMIT仍拒绝，empty不能隐藏非法原参数/对象，诊断ordinal/字段及 `[region_input]`/`[region_reference]`/`[region_input_limit]` 不泄露几何或凭据。
- [X] T037 [US4] 根据 T035/T036 失败，在 `src/scan/read_om.cpp`、`src/scan/region_input.cpp` 与 `src/scan/region_geometry.cpp` 补齐共享诊断、全集验证顺序和异常资源清理；缺依赖提示显式加载匹配版本，沿用既有OM/远程类别，不吞异常、不静默repair，不修改普通入口NULL/错误行为。
- [X] T038 [US4] 扩展 `test/native/region_scan_test.cpp` 的准备/WKB遍历/geometry/decode取消、短读/对象变化/解码故障、LIMIT、多scan同query、多连接、失败后有效查询及长期区域切换测试；确认每task一次结算、每scan一次QueryEnd、expression/reader状态不串扰且释放，记录GEOS单调用无法保证即时取消的实测延迟。

### Implementation for User Story 4

- [X] T039 [US4] 在 `src/scan/read_om.cpp`、`src/scan/region_geometry.cpp` 和 `src/scan/region_input.cpp` 完善取消检查与异常/early-stop路径，几何每≤256点调用前后检查、WKB每≤256顶点检查，并沿用既有I/O检查；任务成功/空/异常均一次结算，cursor耗尽且outstanding=0才完整，QueryEnd一次发布，释放worker当前geometry/executor/reader，失败/取消/LIMIT保留observed且exact=NULL。
- [X] T040 [US4] 在 `test/tools/duckomo_region_validation.cpp` 补齐P0隔离依赖负例和P4的输入预算/取消/故障/恢复/LIMIT/多scan/多连接检查，沿用 T030 的资源结果；绑定失败尚无scan时不伪造last_scan_metrics，metrics读取不覆盖最近scan，取消延迟据实记录而不承诺固定毫秒上界。
- [X] T041 [US4] 运行 `test/sql/regions_spatial_dependency.test`、regions负例、native生命周期及validator完整P0/P4，将当前版本的诊断、清理、恢复、完整性和RSS/owned联合结果写入 `specs/005-polygon-region-statistics/evidence/us4-boundaries.md`，核验普通读取及legacy metrics回归；任何子检查缺失则完整gate不通过。

**Checkpoint**: 当前版本四故事均有独立可查结果，输入安全、零值读取和执行边界始终保留；尚不扩大为三版本/独立复现通过。

## 2026-10-13 负责人验收范围修订

负责人明确授权“可以修改，不用全域”。T032–T034、T046–T050 中涉及全域远程基准/收益的原始文字保留作历史，现行执行按 [固定局部窗口修订](contracts/bounded-remote-validation-20261013.md)：保持五目标/三版本/四来源、完整值和时间维，窗口基准物化后筛面，与区域入口逐行对照；HTTP/S3 三项成本仍须相对固定较大窗口严格下降，不宣称全域收益。新增冻结输入 `test/data/regions/remote-windows.json`，P6 校验其 hash；已中断全域记录不得视为本轮 pass。

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: 统一证据审计、完整版本回归、交付说明和独立复现，支持状态只随实际证据变化。

- [X] T042 在 `scripts/validate.sh`、`test/CMakeLists.txt` 和 `Makefile` 接入regions测试/validator与匹配spatial路径传递，普通无spatial回归使用隔离运行模式；请求regions验证但缺spatial/fixture/服务时退出2或明确not-run，不被缺文件跳过当pass，保留现有普通验证流程。
- [X] T043 [P] 在 `test/tools/region_evidence_test.py` 编写证据契约审计和负例自检，检查requested/completed gates、source/version覆盖、输入/包/结果/服务日志hash、完整终态、unknown≠0、observed≠exact、失败/LIMIT不计收益、10×owned/RSS双约束及输出目录安全；伪造hash、缺目标版本/来源或仅局部gate的报告必须被拒绝。
- [X] T044 在 `test/tools/duckomo_region_validation.cpp` 实现P6汇总，要求 `--runtime-matrix` 与 `--evidence-root`，逐版本重算产物/输入/证据hash并校验相同冻结样本，验证 `<version>/P0-P4/`、`<version>/P5/` 的全部请求条件；记录extension_version/install_path/probes但不加入可重生build ID，不把一次本地运行或编译通过替代三版本gate。
- [X] T045 在 `test/CMakeLists.txt` 为 region_input/region_geometry/region_scan 及必要validator检查添加实际被测源的ASan/UBSan目标，复用现有隔离sanitizer artifact/runtime方式；保留13项既有检查并新增3项区域检查（当前共16项），不仅给测试壳加instrumentation，也不混用普通/不同版本产物。
- [X] T046 使用 `scripts/build-version.sh` 和 `scripts/validate.sh` 分别构建Linux AArch64 v1.5.4/v1.5.5/v1.5.6隔离产物，每版本执行完整现有普通SQLLogicTest/native、当前16项ASan/UBSan（13项既有检查及3项区域检查）、fixture/reference hash和region测试，并运行相同输入P0–P5；将按版本摘要写入 `specs/005-polygon-region-statistics/evidence/version-matrix.md`、bulk放 `build/evidence/regions/<version>/P0-P4/` 与 `P5/`，仅确实发现host API差异时调整 `src/compat/duckdb_api.cpp`。2026-10-13：三版本最终源码构建、16/16 ASan/UBSan、普通SQL/native/Python回归、fixture/reference hash、domain参考回归、P0–P4以及按本文件前置修订缩小范围的固定局部窗口 P5 均通过。逐版本记录在 `build/evidence/regions/005-completion-20261012/<version>/` 和 `build/evidence/regions/005-bounded-20261013/<version>/P5/`。全域远程P5曾中断，不计通过；本任务不宣称全域远程性能收益。
- [X] T047 用 `test/tools/duckomo_region_validation.cpp` 执行P6及 `test/tools/region_evidence_test.py` 审计，将三版本逐项结论/重算hash写入 `specs/005-polygon-region-statistics/evidence/p6-summary.md`；缺包/目标/服务/完整回归为blocked/not-run而非pass，不继承历史003/004 gate或扩大其他平台/N-grid范围。2026-10-13 revised-window审计通过，JSON/Markdown位于 `build/evidence/regions/005-bounded-audit-20261013/`；P6输入明确覆盖每版本P0–P5、五目标与local/HTTP/HTTPS/S3。
- [X] T048 更新 `README.md`、`README.en.md`、`docs/spec.md`、`docs/architecture.md`、`docs/roadmap.md` 和 `specs/005-polygon-region-statistics/quickstart.md`，提供经实跑的raw/分时/空组/零值/远程full-local命令及真实产物取得方式，说明grid_lonlat调用者责任、平面covers/洞边界、等权非面积平均、NULL/Inf、限额/取消限制、spatial仅区域入口依赖及逐网格/对象/来源/版本证据范围。
- [X] T049 由未参与实现的验证者仅按 `specs/005-polygon-region-statistics/quickstart.md` 和冻结输入执行多区域点值、分时统计、空/缺测辨别及一次固定窗口/区域远程收益比较，在 `specs/005-polygon-region-statistics/evidence/quickstart-review.md` 记录身份/环境、产物和输入hash、命令/退出码/结果与问题；这是独立P7任务，实现者只能准备指南，未取得独立记录则P7保持not-run。AI验证者须明确标注身份和fresh上下文，不能标成人工验收。2026-10-13：三次独立 fresh-context AI 验证尝试均完整留档；第三次（GPT-6，fresh context，未参与实现）通过全部本地教学检查（108行 A/B 多重集与冻结 oracle 双向差异0、12组统计全对、18组空/全NULL区分）和 ncep_gfswave025 固定窗口 HTTPS 基准/区域对照（各9行逐行相同、终态 success/complete、值字节 77,580→25,860、解码块 135→45、按唯一 audit ID 的服务发送字节 150,469→79,671 严格下降）。父会话独立复核了 SQL 冻结体、行内容、服务日志配对计数与字节累计。P7 为 AI 验证通过，非人工验收；全域远程收益仍未验证。前两次尝试（fixture PID 探测失败、HTTPS 代理环境失败）作为历史记录保留在报告中。
- [X] T050 在 `specs/005-polygon-region-statistics/evidence/final.md` 汇总P0–P7、FR/SC、五目标×来源×三版本覆盖、未执行/失败及限制，引用审计和独立报告hash；按实际结果更新 `.specify/memory/roadmap.md` 与本文件完成标记，复核 `test/data/grids/definitions.json` 与 `src/include/duckomo/generated_grid_registry.hpp` 的既有支持声明不被区域能力自动抬升，不改历史证据。需注明全域扫描中止、局部验收范围以及跨扫描切片重复小远程读取的实现性能限制。2026-10-13：final.md 已写入（P0–P6 三版本全过、P7 AI 验证通过非人工、全域未验证、重复小请求限制与两次失败尝试均留档）；definitions.json/registry hash 复核未变（4 coordinate-value-validated + 4 definition-recorded）；roadmap 005 更新为 implemented，记录固定局部窗口修订范围；P7 为独立 fresh-context AI 复现、非人工验收，全域远程收益未验证。只读 debrief 建议治理方评估是否升级 verified，未应用建议。

**Checkpoint**: 完整交付要求四故事、P0–P7和既有普通回归均有实际结果；仅生成本任务文档不触发上述实施、外部复现或支持状态修改。

## Dependencies & Execution Order

### Phase Dependencies

```text
Setup T001–T005
  └─ Foundation T006–T010（T008 capability 先通过）
       └─ US1 T011–T019
            ├─ US2 T020–T023（SQL统计）
            └─ US3 T024–T034（局部读取/资源/远程收益）
                 └─ US4 T035–T041（补齐错误与生命周期验收）
                      └─ Integration T042–T045
                           └─ Per-version P0–P5 T046 → P6 T047
                                └─ Docs T048 → external P7 T049 → final T050
```

- **Setup**: T001 与 T002可并行；T003依赖T002，T004在普通坐标/值基线可用后冻结真实选择且必须早于任何收益测量；T005登记共享构建文件。
- **Foundation**: T006→T007→T008。T009→T010，T010的转换/拓扑依赖T007。缺实际spatial时不启动T013–T016；离线输入/oracle测试可推进，但不签署依赖gate。
- **US1**: T011/T012先写；T013→T014→T015→T016顺序修改共享扫描链路，随后T017→T018→T019。US1本地MVP不要求先关闭整份历史004，但所触及的坐标/源位置基线错误必须修复。
- **US2**: 依赖US1本地点值链路T017及T018的本地正确性检查；T020→T021→T022→T023。无需新增运行时代码。T019缺真实输入时不阻塞统计或US3本地资源工作，但完整P1必须在最终交付前完成。
- **US3**: 依赖US1；T024→T025→T026；T027可与T026并行，T028依赖T027并先于T029→T030→T031；T032→T033→T034依赖已冻结五类输入、正确性、完整成本字段与授权服务。缺真实/远程输入只阻塞对应gate，不能将其省略为pass。
- **US4**: 安全校验已由Foundation/US1建立。T035/T036在US1后即可写且可并行；T037及T039在US3共享scanner/metrics变更集成后执行，T038先于T039，T040→T041依赖完整资源与生命周期实现。
- **Final**: T042/T044/T045修改共享注册与validator，顺序集成；T043可并行，T044完成后运行审计。T046须四故事、T042–T045完成，所有版本P0–P5记录齐全后T047；T048准备稳定指南后才开展T049，T050依赖实际证据。版本产物可隔离，源码修改不并行。

### Gate and requirement coverage

| 验收/需求 | 主要任务 | 完成判据 |
|---|---|---|
| P0；FR-001/002/007/017 | T001、T006–T010、T011–T014、T018–T019、T035–T037、T040–T041 | 实际adapter、绑定快照/精确类型、缺依赖与普通入口隔离；T008只签子检查 |
| P1；SC-001/003，FR-003–008/011/018 | T002–T004、T009–T019 | 人工/独立geometry+官方值、多重集合零差异、五类真实坐标/值 |
| P2；SC-002/003，FR-009–011 | T003、T020–T023 | 60组计数/极值精确、sum/avg冻结容差、空/全NULL/Inf语义 |
| P3；SC-005，FR-008/012/013 | T024–T031 | exact-before-decode、零值路径、未投影值过滤、4096/4097 fallback |
| P4；SC-006，FR-002/015/016 | T009–T010、T012、T015–T016、T024–T031、T038–T041 | 预算/并行/owned与RSS、跨批、取消/失败/恢复/终态全部通过 |
| P5；SC-004，FR-012/014/018/019 | T004、T019、T028–T034 | 五类同内容四来源一致；冻结窗口 HTTP/S3 三项成本均不增加且至少一项下降（2026-10-20 修订） |
| P6；FR-017–019 | T001、T042–T047 | 三版本同输入P0–P5+全部普通回归/当前17项sanitizer，身份/hash重算 |
| P7；SC-007，FR-020 | T022、T048–T050 | 未参与实现者依指南执行并留记录；实现者自测不能替代 |

### Parallel Opportunities

- T001（依赖包）与T002（官方fixture）独立；T003/T004共用manifest与oracle生成脚本，顺序处理。
- US1的SQL契约T011与native任务语义T012分别持有不同文件。
- US2修改SQL/quickstart期间，可与US3的native/metrics文件任务并行；T021与T030/T032共用validator必须排队，US2与US4共用regions.test也必须排队。
- T027的metrics测试可与T026的执行预算实现并行，T028/T029随后顺序接入。
- US4的隔离依赖测试T035与输入负例T036独立；取消/故障T038另用region_scan_test，但须等待US3对该文件的任务完成。
- 最终证据审计T043独立于注册/validator修改，可先编写针对契约的正负样例；运行依赖实际报告。

## Parallel Example: User Story 1

```text
前置：Foundation完成。
T011：test/sql/regions.test 的绑定/原始点值契约。
T012：test/native/region_scan_test.cpp 的task/ordinal/跨批契约。
完成后：T013–T016顺序接入，不并发改read_om.cpp。
```

## Parallel Example: User Story 2

```text
前置：US1输出已稳定。
T020：test/sql/regions.test 的统计/空组/NULL测试。
T024：test/native/region_scan_test.cpp 的US3零I/O/资源测试。
T021的validator写入与T030/T032串行；US2自身不强造并行拆分。
```

## Parallel Example: User Story 3

```text
前置：T024/T025完成。
T026：src/scan/read_om.cpp 与region_geometry.cpp 的扫描预算。
T027：test/native/scan_metrics_v4_test.cpp 与scan_metrics_v3_test.cpp 的计量契约。
完成后：T028→T029→T030接入并执行，不能测试壳通过而未计真实成本。
```

## Parallel Example: User Story 4

```text
前置：US1绑定输出已可测试；先结束US2的regions.test编辑。
T035：test/sql/regions_spatial_dependency.test 的有/无spatial隔离。
T036：test/sql/regions.test 的全集负例与用户诊断。
完成后：T037顺序修共享输入/binder；T038→T039测试并修生命周期。
```

## Implementation Strategy

### MVP First (User Story 1 Only)

1. 冻结依赖与fixture/reference，完成Foundation；先验证真实spatial adapter，不能先写完整scanner再发现不可用。
2. 完成US1的全集绑定、精确点值、合成列与惰性区域任务，并验证本地438条和双向差分。
3. 以US1作为可演示MVP；完整P1真实输入按范围留证。这个检查点不代表本feature的收益/执行边界/三版本/独立复现完成。

### Incremental Delivery

1. 在稳定US1输出上加US2原生SQL统计与空组流程，保持人工可手核参考。
2. 完成US3零值/值过滤、预算与诚实指标，再做事前冻结五类真实冷full/local收益。
3. US4补齐可操作诊断及取消/失败/恢复，输入校验从第一版就保留，不作为可裁剪项。
4. 完整执行三版本普通与region回归/sanitizer、P0–P5，审计P6，再取得独立P7；据实交付覆盖表。

### Scope and stop conditions

- 只支持单稳定OM对象、标准2D Polygon/MultiPolygon、平面covers和等权SQL统计；跨chunk、多对象、Line、面积权重、插值、重网格、隐式CRS转换、共享prepared/decoded缓存留后续。
- 官方spatial包、真实样本、授权服务或独立验证者缺失时，记录对应blocked/not-run及取得方法；时间经过、一般spatial冒烟或旧gate不构成通过。
- 坐标/位置基线错误、WKB/spatial解释不一致、worker生命周期不可用、预算/取消不可靠或普通读取回归失败，应先修复或说明设计修订，再继续对应gate。
- 收益与正确性分别判断；未知计量保持NULL/false，跨区域重复成本完整累计，禁止测后换样本、改oracle或扩大支持声明。

## Task Counts

| 范围 | 任务数 |
|---|---:|
| Setup | 5 |
| Foundational | 5 |
| US1 | 9 |
| US2 | 4 |
| US3 | 11 |
| US4 | 7 |
| Polish & cross-cutting | 9 |
| **Total** | **50** |

共8项标记 `[P]`（T001/T002/T011/T012/T027/T035/T036/T043）；50项已完成，0项未完成。完成标记反映已落盘的对应实施与验证记录；不同源码快照的 gate 覆盖以证据汇总为准。所有50项均包含顺序ID、checkbox、对应阶段的故事标签及明确文件路径。

## 2026-10-14 Review 修复后收口

三项review修复完成；T031/T040/T045/T046涉及的资源、生命周期和三版本回归已在新产物上重跑：每版P0–P4、16/16 ASan/UBSan、19个SQL文件/1,028个断言、9个Python suite/93项测试和普通native/domain回归通过。T048/T050的指南、证据和任务汇总已更新，见 [review修复验证](evidence/review-fixes-20261014.md)。T047/T049的P6/P7及P5完成记录保持历史产物范围，本轮未重跑，不能将50项勾选解释为当前产物所有gate均刷新。
