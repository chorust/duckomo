# Spec 002 与当前 HEAD 实现一致性审计

审计日期：2026-10-21。审计对象：`/home/blizhan/repo/github/duckomo`，当前 HEAD `6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`（`perf: coalesce contiguous reads into rectangular batches`）。本次只读；没有改仓库文件、任务勾选、roadmap 或提交。

## 有效文档与证据身份

- 需求索引以当前 `specs/002-spatial-pushdown/spec.md` 的 FR-001–016、SC-001–006 为准，设计与任务以对应 `plan.md`、`tasks.md` 为准。当前 `tasks.md` 有 46 项勾选；T042 明确记录 Linux x86_64 按用户决定延期且未执行（`tasks.md:118-122`）。这不是 x86_64 已支持的证据。
- 当前 SQL 契约已经修订为 SQL 地理列 `lat/lon`、共享或 MAP 形式的 `dimensions`，并说明历史 evidence 对应旧构建（`contracts/sql-interface.md:3-5,19-32`）。`docs/interface-migration.md:3-17` 明确该修订覆盖 001–004 契约，但不改变文件支持子集或既有验收等级。旧 quickstart 也在页首提醒其 `latitude/longitude` 示例是历史接口（`quickstart.md:3-5`）。
- 原始 `evidence/final.md` 报告的 AArch64 验收和性能值是历史结果：DuckDB v1.5.4，固定 `projection.om` 的值数据字节 `165,767 → 1,795`、解码块 `503 → 5`（`evidence/final.md:23-36`）。roadmap 明确 `verified` 限于原验收范围，不能外推到后续 68 个 domain 或 HEAD 后续改动（`.specify/memory/roadmap.md:151,172-184,262`）。
- 协调者报告 `scripts/version_matrix.py extension_source_digest` 当前值 `886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e` 与 005 最新验收相符，并已核对三版本 artifact/hash 与 306 条留存命令的退出码。本审计不重算这些值；只从 `build/evidence/regions/current-20261020/matrix-testfixed/v*/build-manifest.json` 读取各版本 `release_dir` 并运行下述小型 native 测试。

## FR 矩阵

| 需求 | 当前 HEAD 实现与调用路径 | 测试/证据 | 结论 |
|---|---|---|---|
| FR-001 显式网格或命名 domain，二者互斥 | `read_om.cpp:934-945` 解析并拒绝互斥配置；`read_om.cpp:951-1014` 查 domain；`read_om.cpp:1084-1130` 校验显式 grid；`regular_grid.cpp:48-80` 校验网格值。 | `test/sql/spatial.test:253,258,263,268` 等负向 SQL；`test/native/domain_reference_test.cpp:77-92` 对真实 domain 与显式网格对照；`evidence/us1.md`。 | 实现路径与当前契约一致；本次未运行 SQL/domain 集成测试。 |
| FR-002 点数、起点、有符号步长、轴身份、separate/flattened 顺序 | 参数解析在 `read_om.cpp:352-399,1084-1129`；轴身份、顺序及尺寸校验在 `spatial_layout.cpp:224-300`；坐标到逻辑位置映射在 `spatial_layout.cpp:130-166`。 | `test/native/regular_grid_test.cpp`；`test/native/spatial_layout_test.cpp:41-121`；`test/sql/spatial.test:54-122,140-189`。 | 实现与现行契约一致；有源码和测试覆盖。 |
| FR-003 返回前拒绝无效参数、冲突/缺轴、形状不符、未知 domain、列名冲突，不凭 shape/路径猜测 | grid/domain 校验位于 `read_om.cpp:943-1014,1084-1130`；布局要求完整轴身份且各值数组有同序轴定义见 `spatial_layout.cpp:224-242`；domain bbox 校验入口见 `read_om.cpp:869-883,1042`；坐标列冲突见 `schema.cpp:81-92`。 | SQL 负向组 `test/sql/spatial.test:253-328,339-354`；`test/native/spatial_layout_test.cpp:124` 起的拒绝用例；domain 样本及元数据边界记录在 `docs/regular-domains.md:7-29`。 | 参数和布局防错代码存在。68 项 registry 的样本覆盖限制见“审计发现 2”；没有证实产生错误坐标的样本。 |
| FR-004 坐标与值位置一致、经度归一化、误差容差 | 公式及归一化在 `regular_grid.cpp:22-43,118-140`；逻辑位置映射在 `spatial_layout.cpp:130-166`；坐标输出跟随同一 `logical_positions` 在 `read_om.cpp:3431-3445`。输出列名由 `schema.cpp:81-92` 固定为 `lat/lon`。 | `test/sql/spatial.test:36-52,191-234`；`test/native/regular_grid_test.cpp`；独立全域 oracle 记录在 `evidence/us3.md:13-16`。 | **语义一致，规范文字不一致（P2 文档漂移）**：spec FR-004 仍写 `latitude/longitude`（`spec.md:84`），当前生效契约及实现输出 `lat/lon`（`contracts/sql-interface.md:32`、`schema.cpp:89-92`）。迁移文件明确新名为当前接口；应把 spec 条文同步到该修订。 |
| FR-005 无配置时维持原始值 schema，不猜坐标 | `BindSpatialConfiguration` 仅有 grid/domain 才设置空间配置（`read_om.cpp:934-946,1016-1083`）；schema 仅在 `HasGrid()` 时追加空间列（`read_om.cpp:2726-2737`）。 | `test/sql/spatial.test:12-22`；`test/sql/read_om.test` 的 raw 回归。 | 实现和测试存在，符合要求；本次未运行 SQL。 |
| FR-006 常量比较、BETWEEN、单/双轴、开闭边界缩小候选 | 比较识别及反向比较在 `spatial_filter.cpp:126-162`；有限常量提取在 `spatial_filter.cpp:74-98`；选择转成轴范围见 `spatial_selection.cpp:469-517,540-575`，随后由 `read_om.cpp:1961-1980` 取候选。 | `test/sql/spatial_pushdown.test:12-65,102-118` 覆盖等值、反向比较、BETWEEN、nextafter、空区间和展平轴；`test/native/spatial_selection_test.cpp`。 | 实现及针对性用例齐备；本次只运行 native 选择测试，没有运行 SQL 用例。 |
| FR-007 完整 residual 保留；不安全表达式/OR 安全回退 | 谓词收集与 unsupported 诊断见 `spatial_filter.cpp:290-420`；安全经纬度 OR 识别见 `spatial_filter.cpp:337-355`；选择状态保留 residual/fallback 见 `spatial_selection.cpp:469-517`；DuckDB 最终 filter callback 设置与状态见 `read_om.cpp:1753-1771`。 | `test/native/spatial_callback_test.cpp:46-84`；`test/sql/spatial_composition.test:102-148`；`test/native/spatial_io_test.cpp:221-260`。 | 静态路径按完整谓词保留、复杂 OR 回退；当前 HEAD 未运行这些集成测试。 |
| FR-008 跨经度接缝 OR 正确，普通反向 BETWEEN 不环绕 | 两区间 OR 收集在 `spatial_filter.cpp:337-355`；候选位置按 union 检查在 `spatial_selection.cpp:61-112`；最终完整 WHERE 仍保留。 | `test/sql/spatial_pushdown.test:76-100`；`test/sql/spatial_composition.test:102-148`；历史多重集核对 `evidence/us3.md:7-10`。 | 代码和测试契约吻合；本次未重跑 SQL/多重集验证。 |
| FR-009 可判空、仅坐标、纯空间 count 不读值 | ProjectionPlan 选择实际依赖列（`projection.cpp:137-160`）；没有值依赖时跳过 segment 建立/值 decode（`read_om.cpp:1744,3400-3417,3480-3513`）；坐标由 logical index 直接生成（`read_om.cpp:3431-3445`）。 | `test/native/spatial_io_test.cpp:191-208`；`test/tools/duckomo_spatial_validation.cpp:606-620`；历史 v2 sidecar。 | 代码分流和零计数断言存在；当前 HEAD 的 I/O 集成测试未运行。 |
| FR-010 与投影、过滤变量、重复列、表达式、排序、聚合、NULL 协同 | required variables 来自 `projection.cpp:137-160`；同一逻辑位置解码并复用在 `read_om.cpp:3480-3535`；坐标和值从相同逻辑位置生成。 | `test/sql/spatial_composition.test:22-100`；`test/native/spatial_io_test.cpp:210-220`；历史 US3 多变量证据。 | 实现和当前测试覆盖相符；未发现静态缺项，本次未运行集成测试。 |
| FR-011 每个非空间逻辑位置单独映射，不广播/折叠额外轴 | 轴身份、strides、non-spatial axes 与 source position 在 `spatial_layout.cpp:224-300,130-166`；空间 cursor 保留非空间轴范围见 `spatial_selection.cpp:540-580`。 | `test/native/spatial_layout_test.cpp:41-121`；`test/sql/spatial.test:140-189`；`test/native/spatial_selection_test.cpp`。 | 实现/测试路径吻合。 |
| FR-012 可复现的正确性与 I/O 观测 | `ScanMetrics` 保存 schema v2、selection、残余条件、bytes/requests/chunks、elapsed/RSS（`metrics.hpp:119,150-154,202-205,630-643,848-905,1097-1149`）；空间 harness 校验 sidecar 与失败状态（`duckomo_spatial_validation.cpp:270-385`）。 | `test/native/spatial_metrics_test.cpp`；`test/native/spatial_io_test.cpp:71-85`；`contracts/validation-evidence.md:13-40`。 | 能力与验证器存在；历史结果采用 v2。当前 HEAD 未重跑完整 harness。 |
| FR-013 固定多块样本上值 bytes 与 decoded chunks 均严格减少 | 验收 harness 对同列 full/restricted 两项都要求严格小于（`duckomo_spatial_validation.cpp:591-604`）；空间读取经 `BuildSelectedBatchSegments` 执行（`read_om.cpp:3400-3417,3495-3513`）。 | 历史 `evidence/us2.md:14-25` 与 `evidence/final.md:16,27` 记录固定 fixture 的下降；工具有硬断言。 | 功能与硬门禁存在，但历史数字不能证明 6b34e52 上仍通过；本次没有运行完整空间 I/O harness。 |
| FR-014 至少一个真实、可追溯 domain 与独立坐标/值 oracle | 旧 domain registry 在 `domain_registry.cpp:31-145,197-205`；bind 时与轴布局校验在 `read_om.cpp:951-1014`；domain BBOX 条件校验在 `read_om.cpp:869-883,1042`。 | 固定 ncep manifest 在 `test/data/domain-manifest.json`；完整独立比较在 `domain_reference_test.cpp:49-93`；结果记于 `evidence/us3.md:13-16`。 | 对原始 ncep_gfswave025 验收范围有历史全位置 oracle。不能推成 68 个 domain 都有同等级真实文件/全值验证；roadmap Q-05 已明确此界限。 |
| FR-015 使用说明和产品文档覆盖参数、布局、回退与错误 | 当前接口迁移见 `docs/interface-migration.md:3-17`；当前契约见 `contracts/sql-interface.md:19-48`；quickstart 页首标明旧 SQL 示例及迁移。 | README、`docs/spec.md`、`docs/architecture.md`、`docs/roadmap.md`；独立复现记录 `evidence/quickstart-review.md:3-5,22-24`。 | 当前产品文档说明了新 API；spec FR-004 和历史 quickstart 正文仍保留旧列名并靠显式迁移提示解释，属于文档同步缺口，不是当前 SQL 输出错误。 |
| FR-016 错误/取消清理，不接受残缺成功，失败后可恢复 | 全局状态未完成时登记 `incomplete_scan`（`read_om.cpp:1951-1954`）；读取与中断检查在 `read_om.cpp:3495-3518`；metrics 区分失败状态。 | `test/native/spatial_lifecycle_test.cpp:86-124`；`test/native/spatial_io_test.cpp`；`evidence/sanitizer.md`、`evidence/us3.md:17-20`。 | 源码与历史门禁记录支持该要求；本次未运行生命周期、损坏/取消或 sanitizer 测试。 |

## SC 矩阵

| 标准 | 直接代码/测试证据 | 对当前 HEAD 的判断 |
|---|---|---|
| SC-001 全位置显式网格和至少一个 domain 对照 | `domain_reference_test.cpp:49-93` 固定 1,038,240 个位置与 15 个值变量；`evidence/final.md:25` 记录 AArch64 通过。 | 历史验收通过；未在本轮重跑真实文件全量 oracle，不能作为 6b34e52 的当前运行证明。 |
| SC-002 布局、边界、接缝、空区、跨批次 | `spatial.test:54-234`；`spatial_pushdown.test:12-118`；`regular_grid_test.cpp`、`spatial_layout_test.cpp`、`spatial_selection_test.cpp`。 | 当前 HEAD 定向 native 子集运行通过；SQL 全覆盖只见历史 evidence，本轮未运行。 |
| SC-003 严格减少 bytes 和 decode chunks | harness 断言 `duckomo_spatial_validation.cpp:599-604`；历史数值见 `evidence/us2.md:14-25`。 | 仅历史固定 fixture 有通过数值；最新 batch coalescing 会改变实际分段/读取计数，本轮没有重跑 full/restricted，当前验收未复证。 |
| SC-004 空、坐标、count 三类零值读取及无关变量零解码 | `spatial_io_test.cpp:191-220` 与 harness `duckomo_spatial_validation.cpp:606-620`。 | 有覆盖及历史 sidecar；本轮未运行 I/O 集成测试，当前 HEAD 运行结果未复证。 |
| SC-005 组合、回退、错误、恢复 | `spatial_composition.test:22-148`；`spatial_lifecycle_test.cpp:86-124`；`spatial_metrics_test.cpp`；`evidence/sanitizer.md`。 | 历史 AArch64 release/sanitizer 通过；当前 HEAD 未运行完整组合和生命周期门禁。 |
| SC-006 未参与实现者按交付说明复现 | `evidence/quickstart-review.md:1-5,22-30` 记载独立验证者于 2026-09-29 在 v1.5.4 完成；该证据使用历史 `latitude/longitude` 名称。 | 历史接口版本上通过；当前接口在之后迁移为 `lat/lon`，没有本轮针对当前 HEAD/当前 API 的独立复现。 |

## 6b34e52 共享 scanner 变更核查

该提交把连续逻辑位置合并为最大连续矩形：实现位于 `src/scan/batch.cpp:26-51,54-82`；空间稀疏选择调用新 builder 于 `src/scan/axis_selection.cpp:385-424`。真实消费链为 `ReadOmGlobalState` 生成候选（`read_om.cpp:1961-1980`）→ `BuildSelectedBatchSegments`（`read_om.cpp:3400-3417`）→ 按 `read_offset/read_count` 解码并放入 `batch_offset`（`read_om.cpp:3495-3518`）；raw scan 也使用 `BuildBatchSegments`（`raw_scan.cpp:151-165`）。因此此提交直接触及 FR-006–013 的空间读取数据通路，但没有改谓词提取或坐标计算。

`batch_test.cpp:83-116` 穷举多个 shape、起点和连续长度，逐项验证矩形只覆盖精确 source range；`axis_selection_test.cpp:77-84` 验证稀疏选择合并时 gap 和输出 offset 保持正确。按协调者给出的三个已匹配 release_dir，本审计运行了每版 `batch_test`、`axis_selection_test`、`spatial_selection_test`，九次均通过。未运行会写临时 metrics sidecar 的 `spatial_io_test`，也未运行 SQL/full harness；所以这次只能证明分段器与选择游标测试通过，不能替代端到端空间 I/O 和 SC-003 性能复核。

## 审计发现与严重性

1. **P2，规范 API 字段名漂移，非运行时缺陷。** spec FR-004 仍承诺 `latitude/longitude`；当前生效契约、schema 输出及迁移文档规定 `lat/lon`。实现符合当前生效契约，但 spec 本文若单独作为调用接口依据会误导用户。证据：`spec.md:84`、`contracts/sql-interface.md:32`、`src/scan/schema.cpp:81-92`、`docs/interface-migration.md:5-17`。本审计没有改文件。
2. **中等验证限制，不是已证实错误坐标。** roadmap 的 `verified` 只覆盖原验收范围；68-domain 扩展的样本审计以 metadata bind 为主，部分无样本或解析失败，完整值/坐标核对仅有明确样本。资料本身要求不要外推（`docs/regular-domains.md:7-29,47-52`；`.specify/memory/roadmap.md:183-184,262`）。本审计未找到一个可复现的错误 domain 输出，故不把证据覆盖不足判成代码 bug。
3. **当前 HEAD 的全量验收证据有限。** 6b34e52 后本轮只执行了三版本定向 native 测试，没有重跑空间 SQL、spatial I/O harness、真实 domain oracle、sanitizer 或独立复现。历史 final/勾选不能替代这些当前 HEAD 证据。因此只对所读实现与已运行测试的范围报告“未发现已证实运行时缺陷”，不宣称 FR/SC 全量当前验收通过。

## 本轮执行记录

只读识别/核对：

```sh
git status --short
git rev-parse HEAD
git log -1 --format='%H %cs %s'
```

三版本定向 native 测试（release_dir 从各自 matrix-testfixed `build-manifest.json` 读取）：

```sh
set -e
for version in v1.5.4 v1.5.5 v1.5.6; do
  manifest="build/evidence/regions/current-20261020/matrix-testfixed/$version/build-manifest.json"
  release_dir="$(jq -r '.release_dir' "$manifest")"
  "$release_dir/test/native/batch_test"
  "$release_dir/test/native/axis_selection_test"
  "$release_dir/test/native/spatial_selection_test"
done
```

结果：三版本 × 三测试均退出 0，分别输出 `batch checks passed`、`semantic axis selection checks passed`、`spatial selection checks passed`。未构建、未重建共享 build、未访问外部服务。

审计期间看到 `.specify/extensions/roadmap` 与 `.specify/memory/roadmap-sync-20261021-next-directions.md` 等其他工作区状态；未修改或覆盖。审计报告唯一写入路径为 `/tmp/duckomo-spec-002-code-audit-20261021.md`。
