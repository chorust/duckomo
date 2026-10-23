# DuckOMO Roadmap

本地扫描、列裁剪和规则网格空间查询（Phase 0–3）已实现。Phase 4 的维度语义已实现；Phase 5 的官方 HTTPFS 远程与并行路径已通过 v1.5.4/v1.5.5/v1.5.6 的受控验证，现行声明范围的独立 AI 复现与最终映射已完成，见[003 当前交付](../evidence/003-dimensions-remote-parallel/current-delivery-20261021.md)。Phase 6 的 [004 多网格](../specs/004-multi-grid-selection/spec.md) 已在批准的四定义/五对象、三版本/AArch64 范围通过 H0–H9 与独立 AI H9，见[最终验收](../specs/004-multi-grid-selection/evidence/current-20261021/report.md)。[005 Polygon 区域读取](../specs/005-polygon-region-statistics/spec.md) 当前源码已通过三版本P0–P7声明范围验收，含独立AI复现；固定窗口成本按负责人批准的非递增规则，见[最终记录](../specs/005-polygon-region-statistics/evidence/current-20261020.md)。[006 nearest polygon sampling](../specs/006-region-nearest-sampling/spec.md) 已在既有 `read_om_regions` 中实现，points 默认兼容；006 已完成声明范围内的 N0–N7 验收：Linux AArch64、DuckDB v1.5.4/v1.5.5/v1.5.6、合成规则球面网格与固定 NCEP GFS025；真实输入仅私有留存、不公开分发，N7由独立用户执行。 [验收报告](../specs/006-region-nearest-sampling/evidence/final.md)。

2026-10-22 当前补记：005 合计预算修复与现行接口已同步，见[修复及验证](../evidence/code-audit-remediation-20261021.md)。003 R21/R22 已完成，独立验收为 AI、非人工，fresh 执行限 v1.5.4，三版本工程运行分别绑定；性能失败与波动保留，不承诺普遍加速。004 当前 H0–H9 已通过，C004-01–04 全部关闭，四定义冻结范围为 remote-benefit-validated，见[当前报告](../specs/004-multi-grid-selection/evidence/current-20261021/report.md)，优先于下列较早的 H1/H2 状态说明。窄范围 [007 bilinear](../specs/007-region-bilinear-sampling/spec.md) 已立 Draft，限定规则经纬度/单个连续标量；下一步冻结计划与契约，尚未实现。

## 阶段

| 阶段 | 状态 | 交付物 / 验收条件 |
|---|---|---|
| **0 — 原型** | 已完成 | `read_om_raw` 接入官方 OM C reader；本地根数组与官方参考结果一致 |
| **1 — 本地扫描器** | 已完成 | `read_om` 支持元数据、层级变量和稳定 schema；通过 coordinates 或完整 dimensions 对齐多变量 |
| **2 — 列裁剪** | 已完成 | 只读取输出与过滤依赖；结果与全扫一致；`COUNT(*)` 无值 index/data 读取 |
| **3 — 空间选择** | 已实现 | 显式规则网格和 68 个登记 domain；安全经纬度条件缩小逻辑读取；固定多块样本的数据字节与解码块数严格下降 |
| **4 — 其他维度** | 功能已实现；G0–G2 本地验收通过 | time、level、lead_time、member、run 映射；验证混合谓词、维度顺序和空结果 |
| **5 — 远程与并行** | 现行声明范围验收完成；三版本工程验证与独立 AI 复现通过 | HTTP(S)/S3 标准文件系统、任务切分和 profiling；自有缓存已移除，不承诺热缓存收益 |
| **6 — 更多网格** | 批准范围 verified；独立 AI 验收 | 旋转、Lambert、stereographic 与 Gaussian 定义、局部空间选择、source/grid-info；每类真实坐标/值、远程收益和独立复现门禁按 [004 spec](../specs/004-multi-grid-selection/spec.md) 完成 |
| **005 — Polygon 区域读取** | 当前声明范围验收完成，P0–P7通过 | 三版本/AArch64固定窗口、17项sanitizer、独立AI P7；每目标三项成本均不增加且至少一项下降，见[最终报告](../specs/005-polygon-region-statistics/evidence/current-20261020.md)；非人工、无全域收益声明 |
| **006 — 区域最近邻采样** | 声明范围内验收完成，N0–N7通过 | points/nearest、状态/来源与三版本验证通过；私有真实输入和独立用户复现见[最终报告](../specs/006-region-nearest-sampling/evidence/final.md) |
| **7 — 科学计算** | 计划中 | 独立设计 `om_slice`、`om_reduce`、`om_interp`、`om_regrid` |
| **8 — 跨 chunk 时间序列入口** | 待评审提案；未立项、未实现 | `read_om_series` 按 model 登记的 chunk 时间参数把任意时间窗展开为多个 `data/` 布局 `chunk_*.om` 对象并按 `valid_time` 对齐拼接，用户无需知道目标时间落在哪个 chunk；设计细节见下节 |

**历史阶段说明（2026-10-10，004 状态由上列 2026-10-22 最终验收替代）：** Phase 4 的语义维度和本地筛选已实现。Phase 5 已切换为官方 HTTPFS，跨来源结果、1/2/4 worker、冷局部收益、访问变化、故障恢复、取消及隔离有三版本受控记录；旧自有缓存及其 SQL 已删除，旧 G5 为 superseded，历史失败不改写。Phase 6 的 version=1 新网格与 source/grid-info 已实现；三类真实投影样本的全量坐标/值子比较及 source-only 辅助检查已通过；按 [2026-10-10 pinned producer 参考决定](../specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)，producer 源码即参考，四个 Open-Meteo 定义登记 coordinate-value-validated；完整 H1/H2 尚未通过。**2026-10-10 修订**：按 [负责人批准的范围修订](../specs/004-multi-grid-selection/contracts/gaussian-acceptance-20261010.md)，N160/N320/N 区域真实样本本轮跳过，不阻塞当前范围收口，定义及合成回归保留。`ecmwf_ifs` O1280（HRES）为本轮 Gaussian 验收目标，已登记 coordinate-value-validated（与 Open-Meteo 发布对象一致，非 ECMWF 原生 GRIB 等价）；[本地真实 domain/空间/抽样时间查询](../specs/004-multi-grid-selection/evidence/baseline-local/o1280-real-local-20261010/final.md) 已留证并通过，完整远程 gate 与 H9 仍开放，详见 [网格证据表](grid-domains.md)。2.0 开发版已退出本轮范围。科学算子仍独立于扫描入口实现；普通 `read_om` 继续直接输出 DuckDB Vector。

## 空间采样与计算

针对小行政区在粗网格上没有内部采样点的情况，nearest 已按[空间采样与区域计算设计提案](plans/2026-10-14-spatial-sampling-and-computation.md)和[生态调研](research/2026-10-14-spatial-computation-ecosystem.md)实现在现有 `read_om_regions` 内，以 `method` 选择处理方式：省略或 points 保持原始区内点；nearest 对每个区域和非空间轴组合选取代表点最近的规则球面网格源值。`read_om` 继续用于普通网格读取或坐标范围筛选；已有 polygon 的查询使用 `read_om_regions`。功能与验收范围见 [006 spec](../specs/006-region-nearest-sampling/spec.md)、[实施计划及契约](../specs/006-region-nearest-sampling/plan.md)、[65项任务](../specs/006-region-nearest-sampling/tasks.md) 和 [evidence](../specs/006-region-nearest-sampling/evidence/)；006 已完成声明范围内的 N0–N7 验收：Linux AArch64、DuckDB v1.5.4/v1.5.5/v1.5.6、合成规则球面网格与固定 NCEP GFS025；真实输入仅私有留存、不公开分发，N7由独立用户执行。 [验收报告](../specs/006-region-nearest-sampling/evidence/final.md)。

## Phase 8 — 跨 chunk 时间序列入口（待评审提案）

> 以下保留设计讨论，不是已批准的立项或冻结契约。接口、参数表和默认跳过 404 等政策均待独立评审；“实测”描述尚未附完整冻结执行记录，不能作为验收证据。当前 `read_om` 仍只接受单对象，本次 O1280 修订不实施 series 入口。

### 问题

Open-Meteo 公共桶的 `data/` 布局把每个 model 的时间序列按固定长度切分到 `chunk_NNNN.om` 文件，且这些对象**不内嵌时间元数据**（实测 `chunk_817.om` 等对象的字节中无 `time`/`valid_time`/`time_start` 节点）。时间只由生产者约定隐式给出：

```
chunk 起始时刻 = Unix epoch + chunk序号 × chunk_time_length × 步长
```

各 model 的参数不同（2026-10 实测，与 pinned `b06f4760` 源码 `omFileLength` 交叉核对）：

| model | chunk_time_length | 步长 | 覆盖 |
|---|---|---|---|
| `ecmwf_ifs` | 504 | 1h | 21 天 |
| `ecmwf_ifs025` | 104 | 3h | 13 天 |
| `ncep_gfs013` / `ncep_gfs025` | 481 | 1h | 20 天 |
| `dwd_icon` | 253 | 1h | ~10.5 天 |
| `dwd_icon_eu` | 193 | 1h | ~8 天 |
| `dwd_icon_d2` | 121 | 1h | ~5 天 |
| `jma_gsm` | 110 | 6h | ~27 天 |
| `cmc_gem_gdps` | 110 | 3h | ~14 天 |
| `meteofrance_arpege_world025` | 210 | 1h | ~9 天 |
| `ukmo_global_deterministic_10km` | 193 | 1h | ~8 天 |

当前用户必须自行完成“目标时间 → chunk 序号 → 拼 URI → 手动声明 `axes` 时间起点”四步，且跨 chunk 边界的大窗口需要手写 UNION ALL（已实测 `chunk_921∪922` 可无缝拼接，见下）。

### 目标接口（草案）

```sql
SELECT temperature_2m, valid_time, lat, lon
FROM read_om_series(
  's3://openmeteo/data/ecmwf_ifs/temperature_2m',   -- 目录前缀，不含 chunk 文件名
  model := 'ecmwf_ifs',                              -- 或直接给 chunk_time_length/step
  dimensions := map(['value'], [['lead','point','time']]),
  domain := 'ecmwf_ifs',
  valid_times := [TIMESTAMP '2023-01-04 22:00:00', TIMESTAMP '2023-01-05 02:00:00']
);
```

要点：

- `valid_times` 是 `[start, end]` 二元素数组（沿用 003 语义列命名），也可接受 `start`/`end` 命名参数。
- 函数内部把窗口换算为 chunk 序号闭区间 `[floor((T_start_epoch_s)/(L·dt)) .. floor((T_end_epoch_s)/(L·dt))]`，逐个构造 `chunk_%04d.om` URI，展开为多次单对象 `read_om` 绑定，并把每个 chunk 的隐式时间轴具象化为 `valid_time` 列后 UNION ALL 对齐输出。
- 展开后的每个子扫描完全复用现有 `read_om` 语义（domain/grid、空间筛选、列裁剪、并行、指标）；series 层不引入新的解码或网格逻辑。

### 提案依据（原草案实测描述，执行证据待补）

1. **参数来源**：每个 model 目录下的 `static/meta.json` 含 `chunk_time_length`、`temporal_resolution_seconds`、`data_end_time`，匿名可读；与 pinned Open-Meteo 源码各 `*Domain.swift` 的 `omFileLength` 一致（如 `EcmwfEcpdsDomain.swift` 的 `24*21//504`、`EcmwfDomain.swift` 的 `(240+3*24)/dtHours//104`）。
2. **chunk 布局一致性**：同一 model 同一变量的连续 chunk 均为 `[1, point_count, chunk_time_length]` 根数组（ecmwf_ifs 为 `[1,6599680,504]`，chunks `[1,6,504]`），空间点序一致，因此按 `valid_time` 拼接是安全的行级对齐，无需重采样。
3. **缺口是常态**：`ecmwf_ifs` 在 `chunk_817`→`chunk_921` 之间存在历史缺口；`dwd_icon`、`ecmwf_ifs025` 实测连续。函数必须对单 chunk 404（`[object_not_found]`）容错：默认跳过并在结果中表现为该时段无行，可选 `strict := true` 时报错。
4. **跨 chunk 拼接已验证**：对 `chunk_921`/`chunk_922` 各自声明正确时间轴后 UNION ALL，重庆附近窗口 `2023-01-04 22:00 ~ 2023-01-05 02:00` 返回连续逐小时行，跨界处无缝。
5. **步长不均匀的边界**：部分 model 的预报步长在长时效处变粗（如 ecmwf_ifs025 前 90h 为 3h、之后仍为 3h；ecmwf_ifs 前 90h 为 1h、之后 3h/6h）。`chunk_time_length` 指的是**文件时间轴槽数**，槽位为空时值为 NaN→NULL；series 层只做槽位映射，不推断真实步长，与生产者语义一致。

### 设计约束（对齐现有契约）

- **不自动转置/广播/连接**：series 层只 UNION ALL 同构子扫描；每个子扫描仍要求完整的 dimensions/domain 声明（`data/` 对象无轴元数据，这是现有行为，不在本期自动推断）。
- **model 登记表**：`chunk_time_length`/`temporal_resolution_seconds` 优先内置登记（与 grid registry 同源的冻结数据，避免每次运行时依赖 S3 可用性）；允许用户用 `chunk_time_length`/`time_step` 参数显式覆盖以支持未登记 model。
- **glob 不放宽**：入口参数是目录前缀 + model（或显式参数），不引入通配符展开，保持“read_om 只接受单对象”的现有边界；series 是独立的新表函数。
- **远程容错**：chunk 缺失按 1. 的策略处理；对象在中途被替换/过期（滚动窗口）时沿用现有 `[object_changed]` 诊断，不静默重试。
- **时间轴声明**：series 层自动为每个子扫描生成 `axes := {'time': ...}`（起点=chunk 序号×L×dt，步长=dt），用户无需手写；用户显式传入 `axes` 时拒绝（避免双重声明冲突）。
- **验收要点**：跨 chunk 窗口与手写 UNION ALL 基准逐行一致；chunk 缺失跳过/严格两种行为符合声明；扫描指标按子扫描分列（`duckomo_last_scan_metrics()` 每扫描一行，天然满足）；不承诺大窗口的热缓存收益。

### 明确不做（本期）

- 不做跨变量/跨 model 的自动对齐（用户仍逐变量调用）。
- 不做 `data_run/`/`data_spatial/` 布局的 run 选择器（那些布局自带时间元数据，现有 `read_om` 已自动输出 `valid_time`）。
- 不做任意 glob、不合并相邻 chunk 的物理块读取（每个 chunk 仍是独立对象，I/O 语义不变）。

## 已完成部分的验证范围

- Phase 0–2：Linux AArch64 上的 SQL/native、样本重生、投影指标与 sanitizer 已通过；SC-006 已由未参与实现的 fresh 独立 AI 在 v1.5.4 完成四项操作，父级复核 190 份封存产物及 23 份输入哈希，原声明范围 verified。非人工验证，不含 x86_64，见[独立复现](../specs/001-local-om-scanner/evidence/independent-sc006-20261021.md)与[验收记录](../specs/001-local-om-scanner/evidence/final.md)。
- Phase 3：原空间门禁、真实 `ncep_gfswave025` 全域核对和独立复现已在 Linux AArch64 通过，见 [空间验收](../specs/002-spatial-pushdown/evidence/final.md)。Linux x86_64 支持与验证暂缓。
- Phase 4–5：[003 历史验收记录](../evidence/003-dimensions-remote-parallel/final.md) 保留原 G0–G7 状态；当前官方 HTTPFS 路径的构建/本地回归、每版本 48 项远程 runtime、固定真实样本跨来源一致性、访问变化及性能结果见 [迁移验证记录](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md)。Linux AArch64 的 v1.5.4/v1.5.5/v1.5.6 受控验证及 R21/R22 已完成，当前声明范围 verified，见[当前映射](../evidence/003-dimensions-remote-parallel/current-delivery-20261021.md)。fresh 独立 AI 实际执行限 v1.5.4，非人工；工程三版本结果分别绑定，旧 G5 superseded。
- Phase 6 / spec 004：当前批准范围 H0–H9 全部通过，C004-01–04 关闭，见[最终报告](../specs/004-multi-grid-selection/evidence/current-20261021/report.md)。三版本/五对象完整本地及 45 对远程结果、严格成本与命令溯源通过；独立 AI 实际复现限 v1.5.4，其余工程运行由其独立审计，非人工。N160/N320/N 区域真实样本与 2.0 按批准范围排除，合成 N-grid 保留；不扩大至全部 domain。逐定义等级见[网格证据表](grid-domains.md)。
- Domain 扩展：68 项定义的来源和三个 AWS 目录的样本覆盖见 [规则网格 domain](regular-domains.md)。元数据绑定成功不代表同目录所有对象或全量值均已验证。

## 2026-10-20 当前执行顺序

本轮已将005固定窗口P0–P7证据重新绑定到最终源码/产物，并完成连续矩形读取优化；负责人批准[成本修订](../specs/005-polygon-region-statistics/contracts/non-increasing-cost-20261020.md)：冻结输入与SQL不变，每目标三项成本均不增加且至少一项严格下降。验收结果以005最新final为准。已完成的006按声明范围纳入[总台账](../.specify/memory/roadmap.md)，不重复立项。

003 的 R21/R22 与 004 的 [C004-01–04](../specs/004-multi-grid-selection/tasks.md#当前有效待办2026-10-20) 已于 2026-10-22 在各自声明范围收口；后续新增能力按独立 spec 推进。旧专用 ABI/自有 LRU、已跳过 N-grid 真实样本和退出的 2.0 组合不恢复为本轮前置。

[连续矩形优化记录](../specs/005-polygon-region-statistics/evidence/contiguous-rectangle-performance-20261020.md)区分实际解码/请求收益、物理字节增加及小窗口未改善的情况，不作普遍吞吐承诺。bilinear、Line、面积计算和series仍需各自规格与验收，不由台账更新自动立项。

## 后续实现约束

官方 OM C reader 继续负责格式、物理块选择、字节请求与解码。网格层只提供逻辑切片；DuckDB 保留完整 `WHERE`，无法安全分析的条件回退。新优化需同时证明结果一致和实际读取收益。

`read_om` 可输出 `time`（沿用 `valid_time` 名称）、`level`、`lead_time`、`member` 和 `run` 语义列；经纬度列仍只在配置 grid/domain 时生成。全量发布声明仍须区分已通过的官方 HTTPFS 受控验证和未完成的逐网格/独立复现门禁。xtensor/xsimd 和科学算子尚未接入。

用户可见接口、支持范围或模块职责变化时，同步更新 [README](../README.md)、[接口说明](spec.md)、[技术架构](architecture.md) 和相应 SQL 契约。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
