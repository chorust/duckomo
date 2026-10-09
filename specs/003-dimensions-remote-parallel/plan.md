# Implementation Plan: Phase 4–5 维度语义、远程与并行读取

**Branch**: `main` | **Date**: 2026-09-30 | **Spec**: [spec.md](spec.md)

**Input**: `specs/003-dimensions-remote-parallel/spec.md`

setup-plan 返回 `BRANCH=003-dimensions-remote-parallel` 是 feature 目录回退标识；实际 Git 分支为 main，未配置分支 hook。本命令止于 Spec Kit Phase 1 设计，不生成 tasks.md 或实现功能。

## Summary

扩展现有 OM 扫描器的坐标模型，将 time（沿用输出名 valid_time）、level、lead_time、member、run 与逻辑轴绑定，联合现有空间必要条件生成有界选择。保持官方 OM reader 负责物理块和字节请求、DuckDB 保留完整 WHERE。随后接入远程文件、由 DuckDB 调度并行扫描、会话内有界缓存及逐查询观测。保留提交 c593b0e 已提供的 valid_times 参数、自动时间元数据和无时间轴快照行为。

## Technical Context

**Language/Version**: C++17；官方 OM 核心 GNU C11；开发期辅助验证使用 Python 3 和 shell。

**Primary Dependencies**: DuckDB v1.5.4 / `08e34c447bae34eaee3723cac61f2878b6bdf787`；OM C / `d8855e418e2231ae8439f0c7e840fa3f93b371e3`；extension-ci-tools / `b777c70d30942cca5bef62d6d4fa23a13362f398`。远程使用 httpfs / `c3f215ab360f04dc3d3d5305fa81849c0121f111` 加受控 range-session 补丁；依赖其既有 TLS/HTTP/S3 构建配置，见 [research.md](research.md)。

**Storage**: 单个 OM v3 本地文件或 HTTP(S)/S3 对象；Float32 FPX/PFOR 值数组；会话内易失缓存，不新增数据库或持久服务。

**Testing**: SQLLogicTest、native 轴/选择/生命周期测试、官方 reader 值参考、独立坐标参考、可控远端故障与传输日志、release 完整结果差分和 5 次耗时对照；关键路径 ASan/UBSan。

**Target Platform**: Linux AArch64；Linux x86_64 支持和验收继续暂缓。

**Project Type**: DuckDB 可加载扩展及开发期验证工具。

**Performance Goals**: 非空间局部查询的值字节和解码块数均严格减少；HTTP/S3 冷缓存实际响应数据减少；空/坐标/count 零值 I/O；合适样本 5 次并行耗时中位数低于串行；稳定对象热缓存减少网络字节或请求数。

**Constraints**: 不改写 OM 格式/块规划/解压；不删除完整 WHERE；不共享可变 decoder；不将应用读取量冒充网络量；不泄露凭据；无隐式完整远程下载；不改变现有 valid_time schema。

**Scale/Scope**: rank 1–8、正轴长、受检总行数；五类一维语义和原有标量时间；单对象、1/2/4 工作者验收；缓存容量有界；不物化全域笛卡尔积。

研究问题已解决：以封闭 typed axes、统一选择游标、DuckDB local state、配套 httpfs range-session 补丁及 ClientContextState 缓存承接需求。具体取舍与源码证据见 [research.md](research.md)，公开接口见 [SQL 契约](contracts/sql-interface.md)。

## Constitution Check

研究前：PASS。constitution 仍为占位模板，没有已批准原则；其中示例 TDD/审批要求不生效。以 spec、已发布接口和现有 reader 职责为实际约束。设计后复核如下；PASS 表示设计满足约束，不代表代码或性能已经验收。

| 门禁 | 研究前 | 设计后 |
| --- | --- | --- |
| 官方 reader 负责格式/块/字节选择/解码 | PASS | PASS：坐标层仅提供逻辑选择，缓存仅复用已请求范围 |
| 已有 schema、valid_time、完整 WHERE 保留 | PASS | PASS：time 映射到 valid_time，无重复列，typed predicate 保留 residual |
| 本地与远程正确性和实际成本分别证明 | PASS | PASS：v3 双层计数、独立 oracle、服务端交叉核验 |
| 并发状态和缓存生命周期明确 | PASS | PASS：worker 独占 decoder，连接持有缓存，QueryEnd 权威终态 |
| 无越权/失效缓存与隐式完整下载 | PASS | PASS：新鲜授权探测、强版本验证、严格 206，配套 capability fail-closed |
| 范围符合单对象及 Phase 4–5 | PASS | PASS：无新网格/科学算子/跨文件能力 |

没有需豁免的 constitution 违规；配套 httpfs 的窄补丁是满足现有需求的依赖适配，其维护成本已在研究中比较。

## Project Structure

### Documentation (this feature)

```text
specs/003-dimensions-remote-parallel/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── sql-interface.md
│   ├── remote-io.md
│   └── validation-evidence.md
└── checklists/requirements.md
```

后续 tasks.md 由 speckit-tasks 生成，实际验收证据放 evidence/。

### Source Code (repository root)

下面列出实施目标；新增路径不代表已经存在。

```text
src/scan/
  read_om.cpp                 # bind/global/local/QueryEnd 与 settings/functions
  semantic_axes.cpp          # 新增：严格坐标解析、类型/单位与证据核对
  axis_filter.cpp            # 新增：typed 安全必要条件
  axis_selection.cpp         # 新增：联合选择、ordinal cursor 与任务分配
  projection.cpp             # output descriptor 替代坐标布尔偏移
src/om/
  local_file.cpp              # 保留本地校验；抽出统一 ReadAt 边界
  remote_file.cpp            # 新增：context opener、range session、版本与观测
  range_cache.cpp            # 新增：会话缓存与 checked allocator
  reader.cpp / metadata.cpp  # immutable 元数据、coordinate 分类、worker decoder
src/include/duckomo/          # 对应头及 metrics v3；原 grid 层复用
third_party/duckdb-httpfs/    # 新增固定上游依赖
third_party/httpfs-patches/  # 新增窄补丁、共享 observer/provider ABI 与来源说明
test/native/                # axes/selection/parallel/cache/remote lifecycle
test/sql/                  # 新语义及设置契约，保留既有回归
test/tools/                # fixture 扩展、dimensions/remote validation
test/data/                 # 合成样本、独立参考、固定 manifest
scripts/
  validate.sh               # 本地门禁接入
  setup-remote-fixtures.py   # 新增：测试服务/上传/审计代理编排
CMakeLists.txt
extension_config.cmake      # 配套 httpfs 构建及 patched source staging
test/CMakeLists.txt         # native/SQL/harness targets
```

**Structure Decision**: 保持单扩展项目，scan 内增加轴与任务职责、om 内增加传输/缓存职责；httpfs 的凭据和签名仍属于其自身。共享协议只表达范围读取策略和事件，不建立通用插件框架。

## Implementation Sequence

1. **依赖与基础纵向验证**：固定 httpfs 源码/补丁 ABI，验证实际 LOAD 顺序、capability、当前凭据、206/短读/版本变化及 body 事件；静态 native 与可加载扩展都必须通过。若能力不足，修订具体补丁而不是用应用 I/O 冒充网络指标。同步建立独立多维 fixtures 和当前 valid_time 回归。
2. **维度映射**：严格解析 axes；输出 descriptor；保留自动时间、valid_times 和标量快照；文件格式自动识别仅限已核验时间。完成类型/单位、轴排列、多变量及列冲突验证。
3. **联合筛选**：typed predicate → per-axis ranges → 有界 cursor，与现有空间选择相交。空/坐标/count 在值 decoder 之前分流，完整 WHERE 始终保留。完成本地值/坐标 oracle、读取减少与回退证据。
4. **远程串行**：ReadAt 抽象接入配套 range-session，带访问上下文的新鲜打开、版本与精确响应校验，网络异常脱敏。HTTP/S3/本地同内容核对与服务器日志交叉验证；默认关闭新缓存做首轮基准。
5. **并行扫描**：global 惰性任务分配，local 独占 reader/decoder/buffers，共享 immutable 元数据；首次失败停止任务，QueryEnd 发布终态。1/2/4 工作者正确性、跨批次、取消与恢复通过后再计时。
6. **会话缓存及观测**：精确范围 LRU、容量/禁用/清理、权限及版本复核；v3 指标和 last_scan_metrics，多查询隔离。先采用已有互斥聚合计数；仅实测锁争用后再考虑本地 delta 合并。
7. **完整验收与交付**：固定样本与环境执行 G0–G7；真实 OM 三来源结果核对、HTTP/S3 网络下降、冷热收益及 5 次串/并行中位数。同步 README 中英文、docs/spec.md、architecture.md、roadmap.md 和查询契约，由未参与实现者复现 quickstart。

## Validation Coverage

| Spec 要求 | 设计与验收责任 |
| --- | --- |
| FR-001–005 / SC-001 | SemanticAxis、SQL 契约、G0/G1；显式和现有时间证据、独立坐标、布局与错误 |
| FR-006–009 / SC-002/004 | QuerySelection、完整 residual、G2；同列 full/restricted 和零值读取 |
| FR-010–012 / SC-003 | remote-io 契约、G3；配套依赖、授权、严格范围/版本、真实 OM |
| FR-013–014 / SC-005/007 | Global/LocalState、G4；任务覆盖、真实参与者、错误取消恢复、重复计时 |
| FR-015–016 / SC-006 | SessionRangeCache、G5；占用上限、失效、撤权、冷热比较 |
| FR-017–019 / SC-007 | QueryProfile、v3 观测契约、G6；逐扫描隔离与服务器审计 |
| FR-020 / SC-008 | quickstart、文档同步与 G7 独立复现 |

## Risks and Mitigations

- **配套 httpfs ABI 与发布**：引入受版本约束的窄补丁，普通第三方 httpfs 不保证兼容；先过跨扩展装载门禁，再推进远程实现。发布同时携带两个匹配产物及 manifest，不静默降级。
- **弱版本远端**：只能保证检查可观测变化，不能检测服务器不提供标识的等长静默替换；公开输入必须不变，禁用跨查询缓存。
- **选择碎片与内存**：区间预算超限安全扩大为全轴；惰性任务与规则坐标避免全域物化，代价是某些查询收益降低而不是错结果。
- **并行收益依赖工作量**：预先固定足够大的样本和环境，报告全部重复值；未达到中位数收益则 SC-005 未通过，不能仅按 worker 数标完成。
- **性能与内存计量**：网络响应 body 独立于逻辑/底层字节；并发时 RSS 标为 process，单查询工作内存单列；失败完整性单列。

本轮只验证设计一致性、引用、接口与需求覆盖；没有执行尚未实现功能的性能或正确性验收。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
