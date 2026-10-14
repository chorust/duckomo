# Implementation Plan: 多区域 Polygon 读取与等权统计

**Branch**: `main`（实际 Git 分支，未切换） | **Date**: 2026-10-11 | **Spec**: [spec.md](spec.md)

**Feature directory**: `specs/005-polygon-region-statistics`。setup-plan 返回的 BRANCH 为 feature key `005-polygon-region-statistics`，不代表创建了同名 Git 分支。

**Input**: 已确认的多区域首版、区域时间序列优先、允许 spatial 依赖及首版等权统计。

## Summary

2026-10-13 执行范围修订：负责人取消全域远程扫描。以下 full/local 原始措辞保留为设计历史，现行 P5/P6/P7 按 [固定局部窗口修订](contracts/bounded-remote-validation-20261013.md)；五目标/三版本/四来源和精确结果不变，严格收益只比较固定较大窗口与目标 polygon，不宣称全域收益。本地完整 domain 回归已经运行，不因本次远程范围调整失效。

新增单对象表函数 `read_om_regions`，绑定时接受带唯一 ID 的标准 2D Polygon/MultiPolygon WKB 集合和显式 `region_crs='grid_lonlat'` 声明。新入口复用现有 OM 元数据、网格/维度、列依赖、reader、并行及 QueryEnd；按区域惰性领取 native window，先保守候选、再通过 DuckDB spatial 的 ST_Covers 精确筛选，最后只解码匹配位置。返回普通行并附 region_id，统计交给原生 SQL 聚合，不新增 om_reduce。

采用系统 catalog + FunctionBinder + worker-local ExpressionExecutor，而不是私有 spatial ABI 或自写几何引擎。普通 `read_om` 不新增空间依赖，支持声明不继承未完成的历史 gate。跨 chunk、Line、面积加权、插值和重网格不在本计划。

## Technical Context

**Language/Version**: C++17 / GNU C11；开发期 Python3 生成参考与审计，查询运行时无 Python/Swift。

**Primary Dependencies**: DuckDB v1.5.4/v1.5.5/v1.5.6、匹配 spatial、远程官方 HTTPFS、固定 OM C reader。engine/spatial pins 见 [research R7](research.md)；OM `d8855e418e2231ae8439f0c7e840fa3f93b371e3`，ci-tools `b777c70d30942cca5bef62d6d4fa23a13362f398`。不链接 GEOS/PROJ 私有接口，不引入 xtensor。

**Storage**: 单个扫描期间稳定的 OM v3 对象，本地/HTTP(S)/S3；区域输入仅 query/bind 生命周期，无持久数据库、缓存或数据迁移。

**Testing**: C++ native/SQLLogicTest、fixture/oracle 工具、Python evidence tests、ASan/UBSan；5类真实数据、受控HTTP/HTTPS/签名S3与独立复现。

**Target Platform**: Linux AArch64 深度验证；三版本隔离产物；本计划不扩大为其他平台验收。

**Project Type**: DuckDB C++ 扩展；新增独立 SQL table function，共用 scanner。

**Performance Goals**: 可跳块样本 full/local 值字节与解码块、HTTP/S3 服务发送量严格下降；零值路径保持零值成本；固定区域和chunk下≥10×空间点数的自有工作峰值≤2×并附隔离RSS≤2×验证。无普遍延迟或任意布局收益承诺。

**Constraints**: 所有区域先完整校验；精确关系先于值解码；完整 WHERE 保留；每 task/batch 单一区域；内存/取消/未知指标诚实；不建立第二套物理chunk规划或共享区域prepared缓存。

**Scale/Scope**: 最多4096区域、单WKB≤1MiB/总≤16MiB、单区域≤65536顶点/总≤1048576；区域owned存储≤64MiB，opaque geometry≤2MiB/区域；每geometry batch≤256点。具体资源与超限策略见 execution contract。

## Constitution Check

`.specify/memory/constitution.md` 尚为未填写模板，没有可当成已批准原则的条目。下列 gate 源于现行 spec、既有项目契约和用户决定，不伪造 constitution ratification。

| 检查 | Phase 0 前 | Phase 1 后 |
|---|---|---|
| 官方 OM reader 负责物理块和解码 | 保留 | 仅提供 logical positions，PASS |
| 轴/坐标证据不由 shape/path 猜测 | 保留 | 复用 binder + 显式 grid_lonlat 声明，PASS |
| 不丢完整 WHERE/值过滤依赖 | 必需 | 原 callback/pruning策略保留，PASS |
| 普通读取不依赖 spatial | 用户约束 | 新入口才解析spatial函数，普通factory不触发，PASS |
| 单对象范围、无隐式转置/广播 | 必需 | 不改变既有接口，PASS |
| 正确性/收益独立，unknown不补零 | 必需 | P0–P7与v4 additive字段区分，PASS |
| 有界任务与取消、无全域匹配表 | 必需 | 单区域cursor、worker bounded exact filter，PASS |
| 不恢复旧自有缓存/HTTPFS ABI/2.0要求 | 必需 | 官方依赖和现行三版本，PASS |
| 只生成设计，不提前实现/宣称验收 | 必需 | 所有新入口及gate明确待实现，PASS |

规划治理 gate 通过，无需例外。实际 spatial 包探测 HTTP403 是运行验证输入风险，不是运行通过；P0 未通过时不可继续声称 adapter 可交付。

## Project Structure

### Documentation (this feature)

```text
specs/005-polygon-region-statistics/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── checklists/requirements.md
├── contracts/
│   ├── sql-interface.md
│   ├── execution-and-metrics.md
│   └── validation-evidence.md
└── tasks.md                      # 后续 speckit-tasks 生成，本次不创建
```

### Source Code (repository root; proposed changes)

```text
src/include/duckomo/
├── region_input.hpp              # 新：有界输入、标准WKB审计与envelope
├── region_geometry.hpp           # 新：DuckDB expression adapter接口
├── read_om.hpp                   # 新factory声明
├── projection.hpp                # RegionId合成槽
└── metrics.hpp                   # additive regions计量
src/scan/
├── region_input.cpp              # 新：结构校验/预算；不做拓扑引擎
├── region_geometry.cpp           # 新：scalar绑定/worker evaluator
├── read_om.cpp                   # 共用bind/global/local/scan，区域task游标
├── projection.cpp               # 无值依赖/新列映射
└── batch.cpp / axis_selection.cpp # 复用递增源位置和既有batch预算，不重建reader
src/om_extension.cpp             # 注册新入口
src/compat/duckdb_api.cpp         # 仅实际发现的三版本host API差异
scripts/
├── generate-region-reference.py # 新：独立oracle/fixture manifest
├── fetch-region-runtime.py      # 新：官方spatial包、身份/hash与matrix
└── validate.sh                  # 登记区域验证，不把缺输入当pass
test/native/
├── region_input_test.cpp         # 新
├── region_geometry_test.cpp      # 新：真实加载spatial与表达式状态
└── region_scan_test.cpp          # 新：调度/零I/O/生命周期/预算
test/sql/
├── regions.test                  # 新：绑定/语义/组合/统计
└── regions_spatial_dependency.test # 新：有/无spatial隔离进程
test/tools/
├── duckomo_fixture_tool.cpp      # 扩展regions fixture
├── duckomo_region_validation.cpp # 新：P0–P6、证据写入
└── region_evidence_test.py        # 新：证据契约审计
test/data/regions/               # 新：manifest、runtime-matrix、区域/值/oracle
```

**Structure Decision**: 新模块集中输入与几何职责，scanner现有匿名namespace内用可选region mode扩展，避免继承final BindData或复制2600行扫描代码。仅当实际需要隔离DuckDB版本差异时扩充现有compat，不预造通用插件框架。CMakeLists、test/CMakeLists、Makefile按现有注册方式新增源及测试。

## Phase 0 — Research outcomes

详见 [research.md](research.md)：

1. WKB STRUCT list + getvariable 快照为绑定入口，类型检查用 ANY 后严格验证。
2. 标准WKB walker只负责安全审计/envelope，spatial ST_IsValid负责拓扑，ST_Covers负责边界包含。
3. FunctionBinder/ExpressionExecutor提供host接入，无新跨扩展ABI；worker状态独占，左geometry constant vector。
4. region-tagged native任务保持原始位置递增，exact-before-decode；COUNT不绕过geometry。
5. 统计复用SQL；空区域通过显式区域/时间集合补齐，不造样本。
6. 三版本与资源预算已选择；官方包运行验证尚未执行，首个实施gate先完成capability子检查。

未留产品/技术选型占位；产物hash、运行结果是执行阶段采集值，不填虚构数据。

## Phase 1 — Design outputs

- [data-model.md](data-model.md)：区域拥有/快照、bind/task/worker模型及终态。
- [SQL contract](contracts/sql-interface.md)：精确输入/错误/输出/统计/兼容行为。
- [execution contract](contracts/execution-and-metrics.md)：接入顺序、预算、取消、v4字段。
- [validation contract](contracts/validation-evidence.md)：fixture、独立oracle、P0–P7和版本/来源范围。
- [quickstart.md](quickstart.md)：预期可执行的多区域、分时统计、空结果、差分和远程验证指南；新命令明确尚未实现。

## Implementation sequence for task generation

1. **依赖与基线**：获取/冻结三版本官方spatial输入；先做adapter native capability、边界/洞/异常/状态释放与无spatial普通回归。确认既有五类目标坐标/位置读取基线；缺输入记阻塞，不要求关闭历史004全部gate。P0完整绑定用例在入口接入后补齐。
2. **输入与独立参考**：先生成10区域×3时间×2成员oracle、边界/异常/预算测试；实现strict collection/WKB audit与spatial拓扑校验，冻结输入拥有范围。
3. **区域读链路**：新增factory/参数/schema，扩展region task cursor、锁外membership与原值batch接口；先测试空中间batch、区域切换、count及不投影值过滤，再接实际decode。
4. **统计与用户流程**：SQL差分、分组/NULL/空组补齐、多维/重叠语义；同一教学fixture保持可手核期望。
5. **执行与计量**：资源预算、取消失败恢复、1/2/4worker、v4终态、registry/旧schema回归和sanitizer；不得先标函数完成再遗漏零值和内存。
6. **真实与远程收益**：五类同内容多来源full/local完整消费、受控服务审计，测量跨区域重复成本；基本收益不达标先定位逻辑分段，不新增decoded cache或伪造全扫基准。
7. **三版本和独立交付**：P6同输入矩阵及P7独立复现；更新README双语/docs/spec/architecture/roadmap及feature evidence，支持状态只随实际证据改变。

共享 `read_om.cpp`、projection/metrics、build注册与manifest的变更顺序集成，不让多个writer并发修改。输入模块/独立oracle可并行研究，但本计划不授权额外实现者或新工作树。

## Compatibility and migration

无数据迁移；新增table function及指标operation，普通入口schema/列名不变。新 `region_id` 保留名只约束区域入口；include_source仍最后一列。客户端依赖metrics operation枚举时需接受新值，v4/legacy原有字段不重定义。输入容量超限是明确错误，不自动改变统计范围。

面向下一阶段保留普通点值行和region ID，不设计跨对象时间轴或series scheduler；006应复用这条单对象路径，而不是重造区域几何逻辑。

## Risks and stop conditions

- 当前spatial官方下载HEAD为403；取得可核验包并实际加载前P0不通过，不回退私有ABI/自写拓扑。
- 不可证明的全量工作内存保持unknown，GEOS内部由隔离增长实验补充约束；异常增长不能靠scope改名通过。
- 多区域重读可能使成本高于全扫；真实收益失败必须留证，必要设计修订须先说明，不能测后选样或改oracle。
- user grid_lonlat声明不能证明来源CRS真实性；文档清楚告知调用者义务，发现已知冲突必须拒绝。
- 自有WKB审计与spatial解释不一致、worker expression lifecycle不可用、取消状态不可靠或旧读取回归失败时停止推进对应gate，先修复或重新评审。

## Complexity Tracking

无已批准治理规则的例外。新增复杂性限于：严格区域输入模块、host空间表达式adapter和scanner的region-tagged任务。它们分别承担完整输入验证、可选几何依赖及有界精确按需读取；不新增通用几何框架、统计引擎、空间索引服务或缓存系统。
