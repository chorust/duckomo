# Implementation Plan: Phase 0–2 本地 OM 可用扫描器

本文记录 2026-09-28 的原始实施范围；真实 OM 兼容性修复后的支持范围与 SQL 命名规则以 [当前接口契约](contracts/sql-interface.md) 为准。

**Branch**: `main` | **Date**: 2026-09-28 | **Spec**: [spec.md](spec.md)

**Input**: `specs/001-local-om-scanner/spec.md`

> setup-plan 返回的 `BRANCH=001-local-om-scanner` 是 feature 目录回退标识；实际 Git 分支为 `main`。本次只生成设计，不创建实现代码或任务清单。下文实施 Phase 0–2 是项目 roadmap 阶段，不是 Spec Kit 的研究/设计阶段编号。

## Summary

实现可直接查询本地 OM 的 DuckDB 扩展：先验证官方 reader 与单变量读取，再提供稳定列模式及有证据的多变量对齐，最后按查询依赖裁剪变量。值进入 DuckDB 输出批次；OM 库负责格式、压缩和逻辑切片。采用固定依赖、官方生成与读取的 fixture、SQL 对照及实际读取/解码记录共同验收。

## Technical Context

**Language/Version**: C++17；官方 OM C 核心使用 GNU C11 模式，具体编译兼容性在实施 Phase 0 验证。

**Primary Dependencies**: DuckDB v1.5.4 (`08e34c447bae34eaee3723cac61f2878b6bdf787`)、OM (`d8855e418e2231ae8439f0c7e840fa3f93b371e3`)、extension-template 样板 (`cfaf3e236008e782d27f4341b0ee036002d0a449`)；extension-ci-tools 固定 `b777c70d30942cca5bef62d6d4fa23a13362f398`。详见 [research.md](research.md)。

**Storage**: 单个本地只读 OM 文件；无数据库持久层，无远程读取或目录发现。

**Testing**: DuckDB SQLLogicTest、原生 reader/fixture/integration 测试、独立进程读取指标与资源检查；运行查询不依赖 Python。

**Target Platform**: 首先 Linux x86_64；其他平台不作为本里程碑退出条件。

**Project Type**: 可加载的 DuckDB 扩展及开发期原生验证工具。

**Performance Goals**: 未选变量解码数为零；固定可独立读取多变量样本中，单变量数据读取字节严格小于全量；报告耗时与峰值内存，不设无证据的绝对阈值。

**Constraints**: 不重写 OM parser、chunk/byte-range planner 或压缩；无 Python 查询运行时、xtensor、空间/时间下推、远程访问或并行扫描。原始文件扫描期间不变。

**Scale/Scope**: OM v3 根数组/层级 float32、FPX_XOR2D；rank 1–8，正轴长度；NaN→NULL，保留无穷；多变量必须显式声明轴一致。按官方解码成功调用的实际 chunk 范围累计块次数。

## Constitution Check

研究前：PASS。`.specify/memory/constitution.md` 仍是占位模板，没有生效原则；不把示例中的 TDD、审批等视作治理要求。按用户偏好及仓库基线执行。

实际项目边界检查：保留官方 reader 为唯一格式核心；先本地单线程；无时空下推与科学算子；正确性和读取减少分别验收。设计后复核：PASS。逐变量 dimensions 参数仅提供索引对齐证据，不引入网格映射；观测记录不改变格式或解码算法；无额外项目治理豁免。详见 [research.md](research.md)、[data-model.md](data-model.md) 和 [接口契约](contracts/sql-interface.md)。

## Project Structure

### Documentation (this feature)

```text
specs/001-local-om-scanner/
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

`tasks.md` 由后续 `$speckit-tasks` 生成。

### Source Code (repository root)

以下为实施时创建的目标结构；当前仓库只有设计文档。

```text
CMakeLists.txt
Makefile
extension_config.cmake
duckdb/                     # 固定提交的构建依赖
extension-ci-tools/         # 固定提交的构建辅助
third_party/om-file-format/ # 固定上游提交
src/
├── om_extension.cpp
├── include/duckomo/
├── scan/                   # bind、schema、projection、有界批次输出
└── om/                     # 官方 reader 生命周期、DuckDB 本地 I/O
scripts/
└── validate.sh             # 汇总原生/SQL验证，生成证据
test/
├── sql/
├── native/
├── data/                   # OM、参考值、manifest和校验和
└── tools/                  # 官方 fixture writer、参考 reader、metrics harness
```

**Structure Decision**: 单扩展仓库，两个直接职责边界（scan、om）；暂不创建 grid、dimensions、generated registry 或科学计算模块。

## Implementation Sequence

### 实施 Phase 0 — 最小纵向验证

1. 固定依赖和工具链，建立静态/可加载扩展构建与原生测试入口。根目录 Makefile 为源文件，需修正当前 `.gitignore` 对所有 Makefile 的忽略规则。
2. 用官方 writer 生成已知 shape 的单变量样本，用独立官方 reader 路径导出参考值并记录 SHA-256；原生 oracle 不复用 scanner 展平或对齐逻辑。
3. 完成本地定位读取适配、所有权管理、错误转换及 `read_om_raw`；按最后一轴连续段构造有界逻辑切片，避免自行推导文件 chunk 或字节布局。
4. 验证逐位置数值、NaN/NULL、跨批次顺序、损坏输入及取消释放；记录依赖版本、文件版本、I/O 边界和实际执行结果。

退出门槛：官方读取对照通过，Python-free 查询演示成功；若固定版本接口或样本不符合研究预期，先修正文档与证据，不能以猜测继续 Phase 1。

### 实施 Phase 1 — 正式扫描与稳定模式

1. 实现 metadata traversal、变量选择范围、稳定唯一命名、类型与 shape 校验，输出 `DESCRIBE` 模式。
2. 实现显式逐变量轴声明；所有变量必须通过支持性与对齐校验后才允许扫描，不以投影掩盖不受支持的文件。
3. 完成 `read_om`、多变量按同一逻辑范围扫描、缺测规则及所有权状态转换；先不启用列裁剪作为基准。
4. 加入层级、重名、同 shape 不同轴、形状不兼容、无语义坐标、空/截断输入用例，更新支持矩阵。

退出门槛：故事 2 全部通过；纯数组和层级文件的列描述、值、对齐均可独立复现。

### 实施 Phase 2 — 投影与读取证据

1. 启用 projection pushdown，按请求列映射到唯一变量；保留正常 SQL 过滤，禁用 filter pushdown/filter prune。
2. 注册 get_virtual_columns 的 COLUMN_IDENTIFIER_EMPTY 布尔虚拟列，处理无值列、COLUMN_IDENTIFIER_EMPTY、重复引用、表达式、排序/聚合依赖；投影开启/关闭或完整结果物化后做差分验证。
3. 在实际 I/O 与官方解码调用完成位置记录指标，分别运行全变量、单变量、带过滤依赖和计数查询。禁止用“请求了多少变量”推算真实解码块数。
4. 完成单独进程耗时/内存、100次有效/错误查询的句柄资源检查和取消后恢复；整理 Phase 0–2 证据包。

退出门槛：SC-001–006 全部有实际证据；未选变量解码为零且数据读取减少。不要求常量假以外的过滤减少行范围读取。

## Validation Coverage

| 要求 | 设计与验收责任 |
| --- | --- |
| FR-001–002 | Phase 0 原生 oracle、加载/查询演示、无 Python 查询环境 |
| FR-003–008 | schema/alignment 单元测试与 SQLLogicTest；接口契约支持矩阵 |
| FR-009–011 | 六类查询的结果差分、COLUMN_IDENTIFIER_EMPTY/cardinality 路径、正常残余过滤 |
| FR-012 | fixture manifest、参考结果、命令及 SHA-256 |
| FR-013 | 固定样本实际 I/O/解码记录与同环境对比，见 evidence contract |
| FR-014 | 负向输入、取消、RAII、错误后成功扫描与句柄数复核 |
| FR-015 | 锁定版本、阶段证据、基线文档同步 |

本次只验证设计文档的完整性、一致性及引用，不执行尚不存在的构建或测试。
