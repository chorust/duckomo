# Implementation Plan: Phase 3 规则网格空间下推

**Branch**: `main` | **Date**: 2026-09-29 | **Spec**: [spec.md](spec.md)

**Input**: `specs/002-spatial-pushdown/spec.md`

setup-plan 返回 `BRANCH=002-spatial-pushdown` 为 feature 目录回退标识；实际 Git 分支仍为 main，未配置分支创建 hook。本次结束于 Spec Kit Phase 1 设计，不执行功能实现或生成 tasks.md。

这是历史设计阶段记录。后续实施由 [tasks.md](tasks.md) 执行；当前验证状态、平台限制和逐项证据见 [evidence/final.md](evidence/final.md)。

## Summary

在既有本地 OM 扫描器上增加明确的规则经纬度配置和经来源核验的 domain。GridMapping 将经纬度必要条件转为逻辑空间选择，selection cursor 为各非空间位置生成有界读取段，继续由官方 OM reader 规划块与字节读取。通过 DuckDB complex-filter 回调读取谓词但保留完整 WHERE，维持精确过滤及已有变量裁剪。首批 domain 固定 ncep_gfswave025 的 spatial 两轴布局，发布前完成独立坐标和值参考核对。

## Technical Context

**Language/Version**: C++17；官方 OM 核心 GNU C11。

**Primary Dependencies**: DuckDB v1.5.4 / `08e34c447bae34eaee3723cac61f2878b6bdf787`；OM C / `d8855e418e2231ae8439f0c7e840fa3f93b371e3`；extension-ci-tools / `b777c70d30942cca5bef62d6d4fa23a13362f398`。上游 domain 参考版本 `34b9cea169395be9b4686f2b5b23eca26dfef7a2` 为构建期来源证据，不新增运行时依赖。

**Storage**: 单个扫描期间不变的本地 OM v3 文件；Float32 FPX/PFOR；测试 manifest 与 JSON evidence，无持久服务。

**Testing**: SQLLogicTest、原生 grid/selection/生命周期测试、官方 reader 值 oracle、独立坐标参考、完整结果差分及 release 子进程 I/O/RSS 证据；关键路径 ASan/UBSan。

**Target Platform**: 本期验收目标为 Linux AArch64。按用户后续范围调整，Linux x86_64 支持与执行验证暂缓；不对 x86_64 兼容性作声明，也不将 AArch64 结果外推。

**Project Type**: DuckDB 可加载扩展及开发期验证工具。

**Performance Goals**: 固定多块样本小区域的值数据字节和实际解码块数均严格小于同列全域；空选择、纯坐标、纯空间 count 的值 index/data/decode 为零；无关变量解码为零。耗时/RSS/请求数记录但不预设降幅。

**Constraints**: 不重写 OM 格式、块规划或解压；不引入远程/并行/缓存、Gaussian/投影、时间/level/member 映射或科学算子；保留缺测、变量对齐、错误与取消语义。坐标容差预设 1e-9 度，仅用于 oracle，不改变 WHERE。

**Scale/Scope**: rank 1–8；正轴长度和受检 INT64_MAX 行数；两个独立空间轴或一个明确展平轴，任意额外轴位置；全域坐标按需生成，不物化全域坐标数组；轴匹配 O(nx+ny)，扫描按选中位置和有界向量批次进行。

## Constitution Check

研究前：PASS（无已批准的 constitution 原则）。`.specify/memory/constitution.md` 仍是占位模板，其示例 TDD/审批不构成规则，不代替用户制定原则。

| 项目实际门禁 | 研究前 | 设计后 |
| --- | --- | --- |
| 官方 reader 唯一负责格式/chunk/byte-range/解码 | PASS | PASS：GridSelection 仅给逻辑切片 |
| 坐标与轴证据明确，不凭 shape 猜测 | PASS | PASS：grid+spatial_axes+dimensions 或已核验 domain |
| 保留精确 WHERE、所有过滤依赖 | PASS | PASS：complex callback 不删除表达式，普通 pushdown/prune 关闭 |
| 正确性与实际读取减少分别验证 | PASS | PASS：独立 oracle、物化基准、真实计数 |
| 阶段范围和已有接口兼容 | PASS | PASS：无配置仍为原始值扫描，后续阶段能力不引入 |

设计后 PASS 表示设计满足门禁，不声称新功能实现/性能已通过。没有需要豁免的治理违规。

## Project Structure

### Documentation (this feature)

```text
specs/002-spatial-pushdown/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── sql-interface.md
│   └── validation-evidence.md
└── checklists/requirements.md
```

后续 tasks.md 由 speckit-tasks 生成；实施证据放 evidence/。

### Source Code (repository root)

下列新增路径为实施目标，不声称已经存在。

```text
src/
├── grid/                         # 新增 regular_grid、spatial_layout、domain_registry
├── include/duckomo/               # 新增对应头文件；扩展 metrics/projection
├── scan/
│   ├── read_om.cpp                # bind 参数、callback、初始化与坐标输出
│   ├── dimensions.cpp            # 复用既有对齐与冲突校验
│   ├── projection.cpp            # value/坐标/cardinality 槽位
│   ├── spatial_filter.cpp        # 新增：安全必要条件提取
│   ├── spatial_selection.cpp     # 新增：有界逻辑选择游标
│   └── batch.cpp                 # 连续逻辑段映射
└── om/                           # 复用官方 reader 和本地 I/O；保留观测边界
test/
├── native/                       # grid/selection/生命周期与指标测试
├── sql/spatial.test              # 新增：参数/坐标/SQL 组合契约
├── data/                         # 空间 fixtures、独立参考与 manifest
└── tools/
    ├── duckomo_fixture_tool.cpp  # 扩展空间样本
    ├── duckomo_validation.cpp    # 复用既有 harness 支持设施
    └── duckomo_spatial_validation.cpp # 新增完整空间验收入口
scripts/validate.sh               # 加入合成空间门禁
CMakeLists.txt                    # 注册新增源文件
test/CMakeLists.txt               # 注册测试和验收目标
```

**Structure Decision**: 保持单扩展仓库，新增 grid 职责层和 scan 内的选择逻辑；domain registry 为小型编译期数据表附来源 manifest，不引入通用插件/配置框架。

## Implementation Sequence

1. 固定真实 ncep_gfswave025 样本与上游版本，建立独立坐标和值 oracle、合成多布局 fixture；优先完成 DuckDB callback 保留 residual 的纵向验证。若固定版本集成不符合研究判断，修订设计后再推进，禁止静默丢 WHERE。
2. 实现严格参数解析、RegularGrid/SpatialLayout、domain 校验和坐标输出。grid 使用原始 STRUCT 输入校验字段/数值再转换（named parameter 可注册 ANY），避免 DuckDB 到 BIGINT 的隐式舍入隐藏非法点数。无配置回归通过后进入选择阶段。
3. 实现安全条件抽取、axis interval cursor 与跨额外轴的有界批次；扩展 ProjectionPlan 槽位；完整谓词留在 DuckDB。空选择/纯坐标/count 在值 decoder 初始化前分流。保留 query-local 状态 Copy/Equals，关闭 bind data statement cache 并验证 prepared statement 隔离。空选择显式标记扫描完成。
4. 将 query-local metrics 接入 bind/init/scan、分列 bind 与 scan metadata 并汇总；完成全域/区域/空/回退/混合依赖指标，以及坐标边界、接缝、复杂 OR、额外轴、损坏/取消测试。小区域数据字节和解码块数两项严格下降才通过；不能靠调整 SELECT 或仅比较输出行数。
5. 真实 domain 全位置核对通过后注册发布；完成本期 Linux AArch64 门禁。Linux x86_64 暂不纳入本期支持和验收。同步 README 中英文、docs/spec.md、docs/architecture.md、docs/roadmap.md 的接口、职责和实际支持状态；quickstart 由未参与实现者复现。

## Validation Coverage

| 要求 | 设计/验收责任 |
| --- | --- |
| FR-001–005，SC-001 | 严格 SQL 参数、轴证据、schema、独立坐标/domain 校验、无配置回归 |
| FR-006–008，SC-002/005 | callback 提取、保留 residual、轴比较、接缝 OR 与浮点边界 |
| FR-009–011，SC-004 | 空/坐标/count 无值 I/O、ProjectionPlan、额外轴和依赖对齐 |
| FR-012–013，SC-003 | schema v2 metrics、固定多变量多块样本、同列 full/restricted 对比 |
| FR-014，SC-001 | 上游固定提交+真实文件 hash+官方值 oracle+独立坐标全域核验 |
| FR-015，SC-006 | quickstart、契约、README 与产品/架构/roadmap 同步 |
| FR-016，SC-005 | 损坏、取消、跨批次失败及恢复/资源检查 |

本轮只检查设计完整性、来源依据、引用和文档一致性，不运行尚未实现的空间功能测试。
