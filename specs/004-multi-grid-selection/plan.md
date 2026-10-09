# Implementation Plan: Phase 6 多类型网格与远程空间选择

**Branch**: `main`（实际 Git 分支；setup-plan 的 feature 标识为 `004-multi-grid-selection`） | **Date**: 2026-10-03 | **Spec**: [spec.md](spec.md)

**Input**: `specs/004-multi-grid-selection/spec.md`

## Summary

在既有规则网格上增加球面旋转经纬度、Lambert conformal conic、stereographic 和 reduced Gaussian N。网格内核提供原生位置到地理坐标及可追溯定义；选择层生成与非空间轴相交的、不重复的对象逻辑位置；官方 OM C reader 继续负责 chunk、LUT、压缩偏移及解码，配套 httpfs 负责严格 HTTP/S3 Range。

首版投影候选采用有界原生窗口的逐点坐标判定，不依赖四角反投影或未经证明的曲线界。Gaussian 使用完整行表及区域局部映射。保留完整 WHERE、默认 schema、列裁剪与零值读取；显式提供网格描述和 opt-in 源位置。运行时不新增 PROJ/GDAL、Python/Swift、xtensor 或空间扩展依赖。

本次完成规划和契约，不执行功能实现或运行验收。三个只读 subagent 的复核结果已并入 [research.md](research.md)。003 的远程门禁、四类真实 OM v3 样本和 2.0 配套补丁均为实施/发布工作，不从本计划推定通过。

## Technical Context

**Language/Version**: C++17 / GNU C11；开发期参考/生成工具可用 Python/Swift，查询运行时不依赖它们。

**Primary Dependencies**: OM C `d8855e418e2231ae8439f0c7e840fa3f93b371e3`；基线 DuckDB v1.5.4 `08e34c447bae34eaee3723cac61f2878b6bdf787` / httpfs `c3f215ab360f04dc3d3d5305fa81849c0121f111` / extension-ci-tools `b777c70d30942cca5bef62d6d4fa23a13362f398`。2.0 预发布固定引擎 `7264a9f0e5b487358100f408826b8ae9e868b031` / httpfs `5e34903685e4d429cbb19b063406abdd8ce30591` / ci-tools `9b1020499dc85966be3342541f98c8e8e4aada89`。组合、补丁与状态见 [版本及验收契约](contracts/validation-evidence.md)。

**Storage**: 单个本地/HTTP(S)/S3 OM v3；固定 registry JSON 输入生成 C++ 定义，必要 Gaussian 行/局部片段为 O(行数/声明片段数)。无运行时远程 registry、全域逐点坐标表或新增持久服务。

**Testing**: native 数值/映射/候选包含性、SQL 全扫差分、官方 OM 解码参考、固定 Swift/独立地理参考、受控 HTTP(S)/S3 body 对账、sanitizer、1/2/4 worker、预算与取消、逐版本同一验收集、独立复现；实施命令见 [quickstart.md](quickstart.md)。

**Target Platform**: Linux AArch64；x86_64 延续暂缓。

**Project Type**: DuckDB 原生扫描扩展。

**Performance Goals**: 每类事先固定可跳块样本的局部值数据字节、解码块及 HTTP/S3 冷响应总 body 分别严格下降；坐标/count/可证空查询值 index/data/decode=0；同块尺寸与线程配置下至少 10 倍原始点数，选择/扫描工作缓冲峰值不超过 2 倍。

**Constraints**: 完整 WHERE；明确网格、轴、对象与版本身份；每原生窗口至多 65536 点、4096 片段、每 worker selector payload≤256 KiB，全局轴选择 payload≤1 MiB；输出 batch 沿用标准向量大小；禁止全量任务/点表；预算超限扩大候选；版本/权限及严格 Range 边界。解码 scratch 按 chunk/reader 实际需求独立公布，不误称 io_size_max 是硬内存上限；完整预算见 [选择契约](contracts/selection-and-io.md)。

**Scale/Scope**: 四类新网格；N160=138346 点、N320=542080 点；固定源派生的 N320 欧洲区域映射候选为 14747 点。registry 来源 `b06f4760fd1f997e5559bb380f64c5e496b4a509`。真实对象取得/冻结和覆盖等级须逐条验收；投影内核首版选择 O(被考察原生点数)，与非空间布局有关的重复计算须计量，不承诺恒定时间定位。无多文件、椭球自动识别、O/F Gaussian、科学算子或隐式几何输出。

## Constitution Check

Constitution 为占位模板，示例不构成批准原则；不代填或宣称 constitution 已批准。前置检查和 Phase 1 设计后复核均依据规格、[台账 C-01–C-13](../../.specify/memory/roadmap.md)和既有契约。

| Gate | 设计前 | 设计后依据 |
| --- | --- | --- |
| 官方 reader 保留物理职责，扫描直接输出 Vector | PASS | 网格只给逻辑段；不新建压缩 planner 或科学容器 |
| 完整 WHERE、全部依赖、轴/值/缺测兼容 | PASS | 残余过滤不删除；legacy grid 不重解释；源位置不去重 |
| 显式网格/有序轴及冲突校验 | PASS | 严格版本化 STRUCT、CRS profile、完整 Gaussian/区域映射 |
| 有界内存、惰性任务、可取消 | PASS | 固定窗口和预算；任务领用不做全域坐标准备；超限安全扩大 |
| 实际读取收益、逐 domain 来源及独立证据 | PASS | H0–H9 区分定义、实现和验收；缺真实样本不能发布该范围 |
| 远程权限/对象/范围与统计完整性 | PASS | 003 缺口先补证；权限上下文变化修复；2.0 补丁单独验收 |
| Linux AArch64 及正式/预发布范围明确 | PASS | 精确组合单独构建，2.0 正式验收保留；x86_64 延续暂缓 |

PASS 表示设计符合约束，不表示实施门禁通过。台账尚未收录 004，仍写 Phase 6“尚未形成 spec”；这是需后续 roadmap-write/sync 处理的已记录状态差异，本次不改动台账或升级状态。

## Project Structure

### Documentation (this feature)

```text
specs/004-multi-grid-selection/
├── spec.md / planning-notes.md / checklists/requirements.md  # 保留输入
├── plan.md / research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── sql-interface.md
│   ├── selection-and-io.md
│   └── validation-evidence.md
└── evidence/                         # 实施期 H0–H9，当前不创建结果
```

tasks.md 由下一步 speckit-tasks 生成，本次不创建。

### Source Code (repository root)

```text
src/grid/
  regular_grid.cpp / spatial_layout.cpp / domain_registry.cpp / domain_bbox.cpp
  grid_definition.cpp / projected_grid.cpp / gaussian_grid.cpp / grid_identity.cpp  # 新增
src/scan/
  read_om.cpp / spatial_filter.cpp / spatial_selection.cpp / axis_selection.cpp / batch.cpp
  grid_info.cpp                      # 新增描述入口，复用 metadata binder
src/om/
  reader.cpp / remote_file.cpp / range_cache.cpp  # 复用；权限/版本必要修复
src/include/duckomo/
  grid_definition.hpp / projected_grid.hpp / gaussian_grid.hpp / grid_identity.hpp  # 新增
  spatial_layout.hpp / spatial_selection.hpp / axis_selection.hpp / metrics.hpp
src/compat/                          # 新增，仅隔离已证实的 1.5/2.0 API 差异
third_party/httpfs-patches/          # 基线补丁及新增 2.0 独立补丁/manifest
scripts/
  generate-grid-registry.py          # 新增，固定 JSON → C++，非查询依赖
  build-version.sh / stage-httpfs.sh / validate.sh / setup-remote-fixtures.py
test/data/
  grids/definitions.json / grids/sample-manifest.json / grids/*.om / grids/*reference* # 新增
test/native/                        # 新增 grid/selection/source/metrics_v4 对照
test/sql/                           # 新增多网格声明/过滤/identity/兼容回归
test/tools/duckomo_grid_validation.cpp # 新增 H0–H9 验收入口
docs/ / README.md / README.en.md     # 实施完成后同步公开范围
```

**Structure Decision**: 扩展现有 grid/scan/om 分层，采用封闭网格 variant 和共享空间位置适配；版本适配集中在引擎边界。现有 registry 没有可重生生成器，本期补固定输入与生成器，保留旧定义及历史参考来源身份，不假定已有工具可用。

## Phase 0 — Research outcomes

[research.md](research.md) 已解决公式/数值策略、候选包含性、布局、惰性调度/计数、OM 切片、SQL/source、CRS 校验、固定版本/httpfs 和样本策略。2026-10-03 复核补充有界子线、worker 内候选准备、权限 epoch、逐行 f32 Gaussian 和 ci-tools/overlay pin。真实样本、补丁和运行结果是有验收标准的实施输入，不写成未决需求或已通过证据。

## Phase 1 — Design decisions

1. **坐标内核**：两种显式数值策略；登记条目复现生产者 f32 算术并 widen DOUBLE。Gaussian 固定完整 2N 纬度、行长、经度规则，区域按有序 parent 段映射局部 point，不按 BBOX 重建。新网格输出必须有限，奇点用受检解析极限；无法定义的源点在输出数据前拒绝地理绑定。
2. **选择与调度**：optimizer只复制可证明必要的谓词。GlobalInit分别计算逻辑记录与窗口数量的受检上界，由窗口数约束worker；惰性发放不重叠原生窗口及非空间位置descriptor，worker完成窗口判定后再发候选，超过片段预算时扩大整个未发窗口。无空间条件直接全窗口；cursor exhausted且全部窗口preflight完成可算exact count，最终成功完整扫描才发布，LIMIT/失败/取消为NULL；可解析轴积的路径注明例外。每256个坐标判定检查取消。规则网格保留快路径，修复展平片段全量物化及预算感知batch分段。
3. **逻辑读取**：按真实 stride 形成位置与官方 read_offset/read_count；跨接缝求源位置并集。必要块内多读、重复解码全部计量；只有实测需要时做有界逻辑矩形合并，不能写物理 planner。
4. **公开接口**：保留 legacy grid 与所有旧参数；四种新 version=1 STRUCT；`include_source=true` 追加唯一 `om_source` STRUCT；`om_grid_info` 提供完整定义/能力及对象证据，不解码值。完整声明、描述与身份字段见 [SQL 契约](contracts/sql-interface.md)。
5. **版本和观测**：metrics v4 保留 legacy_v3 快照，候选上界与 exact/null 分开。基线与固定 2.0 每套匹配构建；官方 httpfs overlays 和本项目 strict-range 补丁分别记录；stock 或不匹配能力 fail closed。权限配置变更必须参与访问分区失效。

## Implementation sequencing and acceptance dependencies

| 阶段 | 可交付工作 | 进入下一门禁的条件 |
| --- | --- | --- |
| A — 固定输入/回归 | registry 输入、生成器、参考策略、真实样本冻结计划、基线范围 | H0 输入齐备；单类本地内核可先实现，缺样本不得标该类已验证 |
| B — 本地网格/身份 | 四内核、轴/区域映射、CRS 冲突校验、描述/source | H1/H2 坐标值及 SQL 候选差分；H3 零值读取 |
| C — 执行/观测 | 窗口/任务、轴预算、取消、metrics v4、必要逻辑合并 | H4/H5 并行及工作缓冲；所有谓词保留 |
| D — 远程基线 | 003 G3/G5/G6/G7 及完整远程 G4 补证、权限 epoch 修复 | 先建立可信 1.5.4 provider；再执行每类 H6 |
| E — 空间/版本 | 点与区域示例；固定 2.0 API/provider 移植与配套构建 | H7/H8；2.0 正式版可用后另行同套复验 |
| F — 发布/复现 | 逐 domain 覆盖、文档/证据、独立执行 | H0–H9 相应范围全部通过后再更新 roadmap |

H0–H9 覆盖 FR-001–030 和 SC-001–011，详见 [validation-evidence.md](contracts/validation-evidence.md)。固定样本应在收益测量前冻结，不得测后挑选或把 synthetic/v2 转换称为真实原生 OM v3。1.5.5 既有样本记录保留，完整支持不从该记录推导。

## Complexity Tracking

无需要豁免的治理约束。封闭 variant、有界窗口及三个公开增量（新声明、描述、opt-in source）由现有需求直接要求；不引入跨进程缓存、全域空间索引、通用 CRS parser 或科学容器。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
