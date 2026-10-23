# Spec 001 当前 HEAD 一致性审计

审计日期：2026-10-21（Asia/Shanghai）  
仓库：`/home/blizhan/repo/github/duckomo`  
审计 HEAD：`6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`（`perf: coalesce contiguous reads into rectangular batches`）  
范围：只读核对 `specs/001-local-om-scanner` 对应的当前实现、调用者、测试及验收状态；不改源码、任务勾选、roadmap 或提交。

## 结论

在本次逐项检查的 Phase 0–2 功能路径中，当前实现与生效基础契约一致；没有发现可证实的代码缺陷。HEAD 新增的矩形连续批读由普通 raw scan 和选中位置扫描复用，当前匹配构建的 batch、axis selection、spatial selection、reader capacity、raw oracle、schema、projection metrics、lifecycle 检查均通过。

当前未闭环项是 **SC-006 的独立复现**：当前审计者可在隔离的无 Python/Swift 环境运行四项操作，但审计者不是未参与实现的独立验证者。此项是验收缺口，不是本次发现的代码错误。Linux x86_64 已由当前 C-07 明确延期，不再作为 001 的当前验收门槛。

另有低风险文档状态漂移：`spec.md:7` 仍写 `Draft`，而 `.specify/memory/roadmap.md:159` 将 001 标为 `implemented`；`tasks.md:9` 仍写 Linux x86_64 前提和“当前只有设计文档”，`plan.md:62` 也留有同类实施前说明。它们不改变当前基础接口契约或代码行为，但容易造成状态误读。

## 证据身份与生效范围

- 当前源码 HEAD 为 `6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`。本次检查的 HEAD 改动集中在 `src/include/duckomo/batch.hpp`、`src/scan/batch.cpp`、`src/scan/axis_selection.cpp` 及相邻 native 测试；001 的 spec/plan/tasks/contracts/final 文件没有在该提交中更新。
- 协调者提供 `scripts/version_matrix.py extension_source_digest` 当前值为 `886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e`，并确认三份现有矩阵 manifest 匹配该值。本审计没有重算 digest，也没有重建共享目录。v1.5.4 manifest 位于 `build/evidence/regions/current-20261020/matrix-testfixed/v1.5.4/build-manifest.json:39`，其现成 `release_dir` 为 `build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release`。v1.5.5、v1.5.6 manifest 的 `extension_source_sha256` 也为同值（各文件第 39 行）。本审计仅在 v1.5.4 现成目录运行测试；未把 005 的三版本 artifact/hash/命令复核算作 001 的测试结果。
- 生效基础行为以 `specs/001-local-om-scanner/contracts/sql-interface.md:3,5,14,33,50` 及 `contracts/validation-evidence.md` 为准。当前契约已纳入真实文件兼容修订，包括 Float32/PFOR、coordinates 自动对齐及完整逐变量 MAP。`plan.md:3` 明确说原始计划范围以后续当前接口契约为准。旧计划里的 FPX-only、x86_64 和实施前结构说明不覆盖当前契约。
- `specs/001-local-om-scanner/tasks.md` 有 44 项勾选且无未勾选项；勾选只表示任务记录完成，不能独立证明当前 HEAD 验收。`checklists/requirements.md:8,37` 也明确其勾选仅代表规格质量审查。
- 当前状态以 `.specify/memory/roadmap.md:159,258` 为准：001 是 `implemented`，但不标 `verified`；Q-02 保留 SC-006 独立复现待完成。`.specify/memory/roadmap.md:135` 的 C-07 已把当前平台验收限定为 Linux AArch64，并明确 x86_64 暂缓。因此 final/quickstart 中“原计划 x86_64”是历史事实，不是当前阻塞理由。
- `specs/001-local-om-scanner/evidence/final.md:1,18` 的最终验收日期为 2026-09-28。它记录 SC-001–005 Pass、SC-006 Partial，并含当时版本的完整 SQL/native、fixture、性能及 sanitizer 结果；这些历史门禁不自动证明当前 HEAD。当前匹配构建的本轮 native/CLI 结果在下文单独列出。

## 功能需求矩阵（FR-001–FR-015）

| 需求 | 当前实现与调用路径 | 对应测试 / 当前结果 | 判断 |
| --- | --- | --- | --- |
| **FR-001** 本地直接读 OM，查询运行时不依赖 Python | `src/om/local_file.cpp:50,101` 校验本地路径并用 DuckDB 本地文件系统打开；`src/om/reader.cpp:157,233` 持有官方 reader 并执行选区解码；扩展入口在 `src/om_extension.cpp:21`。 | `test/native/raw_reader_test.cpp:345,346,347,348` 当前通过官方 reader oracle 对照；当前匹配 CLI 在 `env -i PATH=/nonexistent` 下完成加载和查询（命令见“验证”）。 | **符合**。支持边界是当前契约列出的单个本地 OM 子集。 |
| **FR-002** `read_om_raw` 单变量官方值/展平顺序验证 | `src/scan/raw_scan.cpp:119,129,130,131` bind 根数组并建 `value FLOAT`；`:151,153,163` 按有界 batch 调官方 `DecodeSelection`，`src/scan/raw_scan.cpp:166,167,168,169` 将 NaN 标为 NULL，`src/scan/raw_scan.cpp:176,177` 仅在完成后推进有序游标。 | `test/sql/raw.test:14,60` 包含 schema、参考值、特殊值及负向输入；当前 `raw_reader_test` 官方 oracle 对照通过；隔离 CLI 返回 0–5。 | **符合**，本轮覆盖 raw 及特殊值 oracle。 |
| **FR-003** 在输出数据前从 metadata 建稳定 schema，不为 DESCRIBE 解码所有值 | `src/om/metadata.cpp:163,377` 遍历/验证 metadata；`src/scan/schema.cpp:26` 构建排序后的列 schema；`src/scan/read_om.cpp:2516` 在扫描前读取 metadata 并校验轴。 | `test/native/schema_test.cpp:548,553,558,563` 用损坏 payload 验证 metadata-only DESCRIBE；本轮 `schema_test` 通过。 | **符合**。 |
| **FR-004** 常规 `read_om`、列描述、纯数组与层级多变量 | `src/scan/read_om.cpp:2732,3659` 输出所有值列并注册 `read_om`；`src/scan/schema.cpp:52` 生成根数组/层级列名。 | `test/sql/read_om.test:25` 起覆盖根数组及多变量 schema/读取；本轮隔离 CLI 对 `multi.om` 的 DESCRIBE、全量和窄读通过；当前 `schema_test` 通过。 | **符合**。完整 SQLLogicTest 本轮未重跑。 |
| **FR-005** 只对有证据的同一逻辑维度按位置对齐，拒绝广播/转置 | `src/scan/dimensions.cpp:22,59,77` 校验轴数、非空/唯一轴名、MAP 精确覆盖、shape 与顺序一致及 metadata 冲突。 | `test/native/schema_test.cpp:471` 覆盖共享轴、coordinate 冲突/命名；`test/sql/read_om.test:56,224,319,334` 覆盖显式 MAP、错误声明及缺省多变量轴声明；本轮 `schema_test` 通过。 | **符合**。 |
| **FR-006** 列名稳定唯一，不从文件名猜测变量 | `src/om/metadata.cpp:130` 生成规范路径；`src/scan/schema.cpp:16,40` 按 UTF-8 字节排序、根列命名及 DuckDB 大小写冲突拒绝。 | `test/native/schema_test.cpp:386,429` 覆盖无效/重名路径、冲突列名及 rank/shape 边界；本轮 `schema_test` 通过。 | **符合**。 |
| **FR-007** 无可靠映射时不暴露猜测坐标，语义列缺证据时失败 | 001 基础读取无 grid 时不追加 lat/lon；空间输出只在 `src/scan/read_om.cpp:2530,2736` 的已绑定 grid/layout 路径追加，绑定逻辑在 `:934` 起。基础契约 `sql-interface.md:18,46` 明确该边界。 | 当前 schema/SQL 测试覆盖基础列与缺失列绑定；空间映射另属后续 spec，不用其证据冒充 001 验收。 | **符合 001 基线**。后续空间/时间能力不属于本 FR 的基础范围。 |
| **FR-008** 公布格式/类型/布局/缺测规则；不支持输入在返回数据前拒绝 | 当前支持矩阵见 `contracts/sql-interface.md:33`；`src/om/metadata.cpp:234` 拒绝不支持的值数组类型，`src/scan/schema.cpp:43` 检查 rank/shape/行数。 | `test/native/raw_reader_test.cpp:282` 和 `test/native/schema_test.cpp:516` 覆盖版本、类型、布局、损坏 metadata；本轮两项通过。 | **符合当前契约**。后续真实文件修订允许的 PFOR/coordinates 不属于偏离。 |
| **FR-009** 仅解码查询依赖变量，保留过滤依赖 | `src/scan/projection.cpp:24` 从 DuckDB 投影列构造去重变量依赖并处理 cardinality；`src/scan/read_om.cpp:3669` 开 projection pushdown、关闭危险的 filter pushdown/prune，残余过滤交给 DuckDB。 | 当前 `test/native/projection_evidence_test.cpp:678,704,705,706,721,728,729,730` 断言 temperature 单列未选变量零读取/解码、humidity 过滤依赖仍读取、pressure 未读取。 | **符合**。 |
| **FR-010** 投影顺序、重复引用、表达式及过滤遵守 SQL 语义 | `src/scan/projection.cpp:91` 保留输出 slot 顺序并按唯一底层变量去重；`src/scan/read_om.cpp:3400,3480` 按所需变量解码并写入对应输出位置。 | `test/sql/projection.test:13,67,107,144,167` 覆盖重复列、表达式、过滤依赖、排序/聚合等；本轮 `test/native/projection_evidence_test.cpp:678,704,721,735` projection metrics 子集通过；对应完整 SQL 行为还见 `test/sql/projection.test:13,67,107,144,167`，SQLLogicTest 未重跑。 | **实现符合；全套本轮未重验**。 |
| **FR-011** count-only 由已验证 metadata 产生 cardinality；空结果无伪行 | `src/scan/read_om.cpp:325` 暴露内部 cardinality virtual column；`src/scan/projection.cpp:79` 识别空列请求；`src/scan/read_om.cpp:3472` 只写 cardinality；普通 `WHERE` 仍由 DuckDB 过滤。 | 本轮 `test/native/projection_evidence_test.cpp:735,736,742,744` 断言 count 正确且所有变量 index/data/decode 均为零；`test/sql/projection.test` 覆盖 `COUNT(*)`/空结果。 | **符合**。 |
| **FR-012** 可重现样本、来源、命令、参考比较及阶段记录 | fixture/oracle 文件在 `test/data/`；生成/验证命令在 `quickstart.md:36`；阶段及 final 记录在 `evidence/phase0.md`、`phase1.md`、`phase2.md`、`final.md`；任务 T040–T044 在 `tasks.md:130`。 | 本轮 raw oracle、schema、projection native 和 Python-free CLI 小检查通过；历史 final 的 fixture hashes/144 assertions 不作为当前 HEAD 的新执行结果。 | **交付物满足；HEAD 全门禁未重跑**。 |
| **FR-013** 真实 I/O/解码观测，零无关解码且单变量数据字节严格下降 | `src/om/local_file.cpp:190` 在成功读取后按类别记字节；`src/om/reader.cpp:233` 及 decoder 成功调用处记录实际解码；指标 schema/比较规则见 `contracts/validation-evidence.md:16`。 | 本轮 `test/native/projection_evidence_test.cpp:599,601,678,704,728,729,744,747` 实测断言单变量 bytes/requests/chunks 下降、未选变量零、过滤变量保留、count-only 仅 metadata；历史 final `:35` 记录 165,767 vs 628,703 bytes。 | **实现与指标检查符合**。绝对耗时/RSS 的全 harness 本轮未重跑。 |
| **FR-014** 文件错误/损坏明确失败；失败/取消后释放并可恢复 | `src/om/local_file.cpp:158` 检查范围并报告截断；`src/scan/read_om.cpp:3556,3566,3574,3578,3585,3597` 将取消、reader 错误和普通异常标记失败并抛回，阻止部分结果成为成功查询；reader/decoder 由 worker 状态 RAII 持有。 | 当前 `test/native/raw_reader_test.cpp:282,345`、`test/native/lifecycle_test.cpp:193,207,218,258,261,280,304` 和 `test/native/projection_evidence_test.cpp:767,782,794,821,838,891,904` 均通过，覆盖负向数据、资源稳定、取消/失败恢复。 | **本次覆盖的错误与恢复路径符合**。 |
| **FR-015** 记录依赖、支持边界，并同步用户语义/职责文档 | 固定上游版本及当前支持界限见 `specs/001-local-om-scanner/contracts/sql-interface.md:33`、`specs/001-local-om-scanner/plan.md:15`；用户/架构基线见 `docs/spec.md:1`、`docs/architecture.md:1` 和 `.specify/memory/roadmap.md:159`。 | final `:1` 有固定依赖、平台、fixture 与命令记录；roadmap 当前记录 001 状态和 Q-02。 | **功能边界有记录**；文档状态漂移见下列低风险项。 |

## 成功标准矩阵（SC-001–SC-006）

| 标准 | 规格/历史记录 | 当前 HEAD 证据 | 判断 |
| --- | --- | --- | --- |
| **SC-001** 完整/裁剪扫描逐位置行数、值、缺测与参考 100% 对齐 | final `:22` 标 Pass。 | 当前 `raw_reader_test` 逐值比官方 reader；raw/multi 隔离 CLI 返回预期值；projection native 对完整/单列/过滤结果断言通过。没有重跑全套 SQL reference 矩阵。 | **抽查通过；全量当前 HEAD 复验未做**。 |
| **SC-002** 覆盖纯数组、层级、多维非方形、跨批次 | final `:23` 标 Pass，列出 `[2,3]`、嵌套文件、`[83,127]`。 | 当前 `batch_test` 覆盖多 rank/形状/边界和矩形连续范围；`raw_reader_test` 含 large fixture；当前 v1.5.4 native 检查通过。未重新跑三版本 fixture gate。 | **实现/相关 native 覆盖通过；矩阵未重跑**。 |
| **SC-003** 未选变量零解码，单列数据 bytes 严格小于全量，并记录观测 | final `:24,35` 标 Pass，历史记录单列 165,767 bytes、全量 628,703 bytes。 | 本轮 `projection_evidence_test` 直接断言零未选变量、bytes/requests/chunks 下降、过滤依赖正确及 count-only 零 index/data。 | **当前匹配 v1.5.4 检查通过**；历史具体 bytes 不冒称本轮新测值。 |
| **SC-004** 过滤、表达式、重排、重复、聚合/排序、count、空结果与完整扫描语义一致 | final `:25` 标 Pass。 | 本轮 projection metrics native 验证关键投影/过滤/count 路径；完整具体 SQL 场景列于 `test/sql/projection.test`，但本轮没有执行 SQLLogicTest 全集。 | **历史通过；当前仅子集复验**。 |
| **SC-005** 负向输入、语义缺失、失败/取消恢复 | final `:26` 标 Pass。 | 本轮 raw、schema、lifecycle、projection native 检查均通过，覆盖缺文件/损坏/不支持、metadata-only bind 和失败/取消后恢复。全 sanitizer 与所有负向 SQL 用例未重跑。 | **相关 native 检查通过；全负向门禁未重跑**。 |
| **SC-006** 未参与实现的验证者不依赖 Python/转换，完成加载、DESCRIBE、全量、单列四项并复现结果 | final `:27` 标 Partial；roadmap Q-02 仍开放。 | 本轮匹配构建在 `env -i PATH=/nonexistent` 下完成 raw 与 `multi.om` smoke：LOAD、DESCRIBE、双列完整扫描、temperature 单列扫描均返回预期列和值。执行者仍是本次审计者，无法证明“未参与实现”。 | **Partial，未闭环**。x86_64 已延期，不计为当前失败理由；独立复现者缺失仍是实际缺口。 |

## HEAD 6b34e52 共享 scanner 风险核验

本提交把原本按最后一轴拆分的段改为最大连续矩形前缀：`src/scan/batch.cpp:26,32,38,42,44,48,49` 计算连续矩形 `read_offset/read_count` 并只扩展完整 trailing axes，`:54,56,60,65,69,73,74,78,80` 切分每个向量批次；`src/scan/axis_selection.cpp:385,390,395,396,397,402,405,407,410,413,414,418` 校验并按连续选中逻辑位置复用；普通 raw caller 是 `src/scan/raw_scan.cpp:153`，常规扫描 caller 是 `src/scan/read_om.cpp:3402,3495,3510`。矩形扩展只合并完整的 trailing axes，检查未发现会跨越未选位置或错位输出的情况。

直接运行的相关检查结果：

- `batch_test`：通过；`test/native/batch_test.cpp:83,94,109,113` 检查矩形读取覆盖、边界、顺序与无缺口属性。
- `axis_selection_test`、`spatial_selection_test`：通过；`test/native/axis_selection_test.cpp:70,73,79,81` 和 `test/native/spatial_selection_test.cpp:59,68,70` 验证顺序、间隙切分与空间选择。
- `reader_capacity_test`：通过；`test/native/reader_capacity_test.cpp:330,332` 断言矩形批次 decoder chunks 从 35 降至 5 且 scratch capacity bounds 不变。
- raw oracle、schema、projection metrics/lifecycle、lifecycle native 检查均通过。

因此本次在所检查的共享扫描路径中**未发现 6b34e52 引入的可证实回归**。这个判断限于现成 v1.5.4 匹配构建及下列小型检查，不等同于重跑完整 SQL/sanitizer/三版本门禁。

## 本轮验证命令与限制

使用 coordinator 指定的 v1.5.4 `release_dir`，仅执行现成二进制，没有 CMake、Make 或目录重建。以下 `R` 指该 release 目录：

```sh
R=/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release
"$R/test/native/batch_test"
"$R/test/native/axis_selection_test"
"$R/test/native/spatial_selection_test"
"$R/test/native/reader_capacity_test"
"$R/test/native/raw_reader_test"
"$R/test/native/schema_test"
DUCKOMO_EXTENSION_PATH="$R/extension/duckomo/duckomo.duckdb_extension" "$R/test/native/lifecycle_test"
DUCKOMO_EXTENSION_PATH="$R/extension/duckomo/duckomo.duckdb_extension" \
DUCKOMO_CORE_FUNCTIONS_EXTENSION="$R/extension/core_functions/core_functions.duckdb_extension" \
  "$R/test/native/projection_evidence_test"
env -i PATH=/nonexistent "$R/duckdb" -unsigned :memory: <<SQL
LOAD '$R/extension/duckomo/duckomo.duckdb_extension';
DESCRIBE SELECT * FROM read_om_raw('/home/blizhan/repo/github/duckomo/test/data/raw.om');
SELECT * FROM read_om_raw('/home/blizhan/repo/github/duckomo/test/data/raw.om') ORDER BY value;
SELECT count(*) FROM read_om('/home/blizhan/repo/github/duckomo/test/data/raw.om');
SQL
env -i PATH=/nonexistent "$R/duckdb" -unsigned :memory: <<SQL
LOAD '$R/extension/duckomo/duckomo.duckdb_extension';
DESCRIBE SELECT * FROM read_om('/home/blizhan/repo/github/duckomo/test/data/multi.om', dimensions := map(['humidity', 'temperature'], [['row', 'column'], ['row', 'column']]));
SELECT "humidity", "temperature" FROM read_om('/home/blizhan/repo/github/duckomo/test/data/multi.om', dimensions := map(['humidity', 'temperature'], [['row', 'column'], ['row', 'column']])) ORDER BY "humidity";
SELECT "temperature" FROM read_om('/home/blizhan/repo/github/duckomo/test/data/multi.om', dimensions := map(['humidity', 'temperature'], [['row', 'column'], ['row', 'column']])) ORDER BY "temperature";
SQL
```

8 个 native 程序均以退出码 0 通过。两次隔离 CLI smoke 均退出 0：raw schema 为 `value FLOAT`、值 0–5、count 6；multi schema 为 `humidity FLOAT, temperature FLOAT`，全量对齐值为 `(100,0)` 至 `(105,5)`，单列值为 0–5。

本轮未运行 `make test`、`scripts/validate.sh`、sanitizer、完整 SQLLogicTest、fixture 重生成或 v1.5.5/v1.5.6 native 测试；这些都不属于本次小型只读检查。历史 final 仍可作为历史执行记录引用，但不升级为当前 HEAD 全量验收。没有启动外部服务，没有重建共享 build，也没有修改仓库文件。
