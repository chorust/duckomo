# Research: 多区域 Polygon 读取与等权统计

日期：2026-10-11。范围：设计研究，不是功能实现或运行验收。

## 研究结论与证据等级

- 已核对现有扫描器及固定 DuckDB 源码，并由只读研究子任务复核扫描接入点；没有修改源码。
- 已读取固定 spatial 源码中的 ST_Covers、ST_IsValid 和函数本地状态实现。
- 官方 v1.5.4 CLI 的只读查询确认 `st_geomfromwkb(BLOB) -> GEOMETRY`、`st_aswkb(GEOMETRY) -> BLOB` 在 core catalog 存在。
- 对 `https://extensions.duckdb.org/v1.5.4/linux_arm64/spatial.duckdb_extension.gz` 的 HEAD 返回 HTTP 403；本机未发现已安装 spatial。故本轮没有 spatial 加载、C++ adapter 执行或三版本运行证据。设计选型已确定，运行兼容性是实施首个 gate；缺包时停止该 gate，不能声称通过或偷偷换几何引擎。
- 官网函数页面的 readable 抽取失败，未用该页面内容作为证据；以下结论依据固定源码，而非当前 main 的不稳定行为。

## R1 — 区域输入和公开入口

**Decision**: 新增 `read_om_regions(path, regions := STRUCT(id VARCHAR, wkb BLOB)[], region_crs := 'grid_lonlat', ...)`。`regions` 与 `region_crs` 必填且绑定时确定；保留 `read_om` 既有参数。输出增加 `region_id VARCHAR`，仍由普通 SQL 聚合。区域集合来自字面量或 `SET VARIABLE ... = (SELECT list(...))` 后的 `getvariable`，不是相关子查询或流式关系参数。

**Rationale**: 绑定前获得整个集合，才能在返回任何行之前发现重复 ID 和末尾非法几何；避免 lateral 每行扫描使完整集合验证失效。WKB 是公开交换格式，无需 DuckOMO 在注册时依赖 spatial 类型/私有序列化。用户可以用 spatial 的 `ST_AsWKB` 从区域表生成输入。

**Evidence**: `duckdb/src/function/scalar/generic/getvariable.cpp` 的 GetVariableBind/BindGetVariableExpression 将变量快照变成常量；`duckdb/src/planner/binder/tableref/bind_table_function.cpp` 在调用 table bind 前求值参数。现有 `GetReadOmFunction` 用 ANY 校验结构参数，可同样避免自动强制转换掩盖类型错误。

**Alternatives considered**: GEOMETRY[] 会使注册和类型解析更耦合；传 SQL 字符串或表名需要二次解析/查询生命周期；相关区域表输入需要 table-in/table-out 或优化器接入，首版不采用。用户准备 list 的内存属于输入物化，不伪装成扫描器内存。

## R2 — 精确几何关系通过 DuckDB 表达式执行

**Decision**: `RegionGeometryAdapter` 使用系统 catalog 的固定函数签名绑定 `ST_GeomFromWKB`、`ST_IsValid`、`ST_Point`、`ST_Covers`；使用 FunctionBinder 和每 worker 独占的 ExpressionExecutor，不直接调用 scalar function pointer，不访问 spatial 私有 C++ 类/GEOS 符号，不在扫描回调中执行嵌套 SQL。

绑定阶段用 ST_GeomFromWKB 创建 opaque GEOMETRY Value，ST_IsValid 校验完整拓扑。worker 的三列输入为常量 geometry vector 和 lon/lat double vectors；表达式为 `ST_Covers(geometry, ST_Point(lon, lat))`。几何保持在左侧常量 vector。每次最多 256 点；每次调用前后检查取消。可复用表达式结构及状态，区域切换只替换输入常量。

**Rationale**: spatial 的固定实现采用 per-expression LocalState，持有 GEOS context；ST_Covers 的 AsymmetricPreparedBinaryFunction 对左侧常量准备几何并逐点测试。这些初始化/异常/生命周期必须交由正常 expression state 承接。源码显示 prepared geometry 是每次 vector 调用局部对象，不承诺跨 batch 缓存。

**Evidence**:

- `duckdb/src/include/duckdb/function/function_binder.hpp`: BindScalarFunction 有 ClientContext 和 catalog-entry 重载。
- `duckdb/src/include/duckdb/execution/expression_executor.hpp`: ExecuteExpression、SelectExpression、EvaluateScalar。
- 固定 spatial `src/spatial/modules/geos/geos_module.cpp`: LocalState、AsymmetricPreparedBinaryFunction、ST_Covers、ST_IsValid。
- 固定 spatial `src/spatial/modules/main/spatial_functions_scalar.cpp`: ST_Point。ST_GeomFromWKB 在该版本的 spatial 注册块被注释，不能误认为它必须由 spatial 注册；本地 core 查询已核实。

**Alternatives considered**: 自写 ray casting 不承担非法拓扑检查；直接链接 GEOS 重复几何引擎并增加发布依赖；SQL cross join 只作结果 oracle，不能保证值解码前筛选；私有 capability/跨扩展指针方案不采用。

## R3 — WKB 结构审计不等于另造几何引擎

**Decision**: 在交给 spatial 前执行受限、非递归的标准 2D WKB Polygon/MultiPolygon walker，只做尺寸、计数、类型、坐标及 envelope 检查。接受任一 endian，拒绝 Z/M、EWKB/SRID、非面嵌套、截断和尾随字节。逐环检查闭合、坐标有限、经纬度范围与接缝/极区约束；几何是否自交、洞是否合法等拓扑由 ST_IsValid 决定。EMPTY 只接受标准零环 Polygon、零部件 MultiPolygon 及由有效空 Polygon 组成的有效空 MultiPolygon。

**Rationale**: 避免在调用第三方分配前才发现恶意计数；冻结顶点预算和精确 double envelope，不依赖可能采用 float bounds 的私有 geometry header。WKB 不带可信 CRS，因此 `region_crs='grid_lonlat'` 是调用者明确的同参考系声明，不是自动验证或转换。

**Alternatives considered**: 用 ST_Dump/ST_Points 一次展开全部顶点会先分配再检查预算；手动解析 spatial 内部 GEOMETRY blob 不稳定；自动 ST_MakeValid 或 ST_Transform 改变用户数据语义，禁止。

## R4 — 复用扫描器，按区域惰性调度

**Decision**: 在 `read_om.cpp` 现有内部状态中增加可选 immutable RegionCollection 与 region mode，提供同文件 sibling factory `GetReadOmRegionsFunction`；几何/输入校验放独立模块，不复制 scanner、不创建另一套 reader。Global state 惰性遍历“区域 ordinal → native window”，一个任务只属于一个区域；worker 在锁外完成 envelope/轴筛选及精确关系，然后调用既有 batch segment 和 OM DecodeSelection。

**Evidence**: ReadOmBindData 是 final 且在匿名 namespace，不能直接继承；`ScanReadOmImpl` 的正确插入点是 logical_positions 到 BuildSelectedBatchSegments 之前；`BuildSelectedBatchSegments` 要求源位置严格递增，因此不同区域的重复源位置不能混入同一 batch。GetNextNativeWindowTask 已有短锁领取、锁外 preflight 的形态。

**Rationale**: 每个区域全几何只遍历一次，不按 MultiPolygon 部件分别输出，不需要全域去重集合。重叠区域重新解码可接受且据实计量，先保证语义和基本局部收益。区域选择共享同一函数调用的 reader、metadata binding、metrics 及终态。

**Alternatives considered**: 所有区域 union 后一次读取再 fan-out 会引入匹配表、输出重复调度和更多内存；一套 scanner/decoder 每区域同时驻留不满足内存目标；先解码再精确过滤不是目标实现。

## R5 — COUNT、列依赖和生命周期

**Decision**: RegionId 为 synthetic output slot，不使值变量成为依赖；没有值依赖时也必须做精确 membership，不以 envelope 候选数替代真实 count。保留完整 WHERE、projection pushdown 与原 filter pruning/pushdown 关闭策略。区域 ID 条件首版留 residual，不另建区域 predicate optimizer。

**Evidence**: ProjectionPlan::IsCardinalityOnly 实际表示没有 OM 值依赖；COLUMN_IDENTIFIER_EMPTY 为 count 哨兵。现有 scanner 只在 required variable IDs 非空时创建解码器、规划值 segments。ObserveComplexFilter 保留 SQL 表达式，保证未输出的值过滤依赖。

**Decision**: 所有区域合法性校验先于 empty/LIMIT 短路；每任务持有自己的 ordinal，不读取会前进的 global 当前区域。精确筛空一个 batch 时继续迭代，不返回零 cardinality 伪装 EOF。游标耗尽且所有领取任务完成才能标 full scan；失败/取消沿用 QueryEnd 单次发布，不能等同成功。

## R6 — 有界资源与可观察性

**Decision**: 冻结输入上限和小批量 geometry 调用；一个 worker 只持有当前区域表达式状态，不缓存所有区域 prepared state。详见 execution contract。新指标是 v4 下 additive `regions` 对象，不更改旧扫描字段语义；区域模式 candidate 的单位显式为 region-record pair。

Spatial/GEOS 的内部堆分配不能从 DuckOMO 自有容量账本得出。分别报告自有 work peak、输入存储和隔离进程 RSS；`spatial_internal_memory_complete=false`，未知 peak 为 NULL。不为通过内存 gate 偷改 scope；完整内存验收要求既有 owned ledger 与隔离进程增长实验同时留证，未解释的空间依赖增长作为失败处理。

**Alternatives considered**: 将 geometry 内存当作零、给未知分配虚构 hard byte bound、用 RSS 当 query-owned 精确值均拒绝。运行时调用 GEOS 不能承诺单个谓词中的即时取消；限制输入复杂度并在每次最多 256 点调用前后检查取消，记录实测延迟。

## R7 — 版本与证据治理

| DuckDB | 固定 engine SHA | engine `spatial.cmake` pin | 官方 linux_arm64 spatial 包源码 SHA |
|---|---|---|---|
| v1.5.4 | 08e34c447bae34eaee3723cac61f2878b6bdf787 | b68b309d371dba936c5bb362980e559b7756b16d | 28db190f7184bcf61eb01d291e0cba79849bddb6 |
| v1.5.5 | d8cdaa33fda8df955cc76ef58a280f68f4cd43fa | eb1e57c9d92c0f3f76eb03eaa52c315090f328cc | eb1e57c9d92c0f3f76eb03eaa52c315090f328cc |
| v1.5.6 | 069cc9f9b5be802405797faecc284961b07c70ef | 04270fe7bb4eb412765cce8afde0276a9d81d3ba | 04270fe7bb4eb412765cce8afde0276a9d81d3ba |

**Decision**: Linux AArch64；OM/ci-tools 继承 `test/data/grids/version-matrix.json` 的固定 SHA。运行版本的 engine `.github/config/extensions/spatial.cmake` pin 与发布到官方 extension 仓库的包源码身份分别记录；二者不同必须审查，不能将 `extension_version` 猜作 provenance。v1.5.4 官方空间包报告 `28db190`，与 engine 配置的 `b68b309` 不同。通过官方 DuckDB Spatial GitHub API 核对：完整提交 `28db190f7184bcf61eb01d291e0cba79849bddb6` 签名验证为真、目标 DuckDB 子模块指针为固定 `08e34c447bae34eaee3723cac61f2878b6bdf787`；它从 `b68b309d371dba936c5bb362980e559b7756b16d` 前进 7 个提交，差异仅有发布工作流、DuckDB/ci-tools 子模块指针及 GEOS vcpkg 打包文件，没有 spatial 函数源码变更。因此 v1.5.4 发布包身份固定为该精确官方提交，而保留 `b68b309` 作为 engine 构建配置 pin。v1.5.5/v1.5.6 官方包身份与 engine config pin 一致。runtime matrix 冻结包 URL 与下载/解压 SHA，记录两类源码身份、`duckdb_extensions()` 的 extension_version/install_path 和 SQL capability probes；identity 与函数签名在运行时复核，不进入可重生 build ID。若后续观察与固定身份不符，gate 停止并审查来源；不自动跟随 latest。

**Rationale**: 官方 HTTPFS 三版本通过不说明 spatial 或新区域查询通过；旧 ABI、缓存和 2.0 gate 不恢复。5 类真实对象的区域收益及独立复现仍需本 spec 自己的证据。

## 可追溯来源

- [v1.5.4 spatial pin](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/.github/config/extensions/spatial.cmake)
- [v1.5.5 spatial pin](https://github.com/duckdb/duckdb/blob/d8cdaa33fda8df955cc76ef58a280f68f4cd43fa/.github/config/extensions/spatial.cmake)
- [v1.5.6 spatial pin](https://github.com/duckdb/duckdb/blob/069cc9f9b5be802405797faecc284961b07c70ef/.github/config/extensions/spatial.cmake)
- [官方 v1.5.4 spatial package source commit](https://github.com/duckdb/duckdb-spatial/commit/28db190f7184bcf61eb01d291e0cba79849bddb6)（签名验证通过；更新到固定 v1.5.4 DuckDB/ci-tools 子模块）。[与 engine config pin 的 compare](https://github.com/duckdb/duckdb-spatial/compare/b68b309d371dba936c5bb362980e559b7756b16d...28db190f7184bcf61eb01d291e0cba79849bddb6) 显示仅发布/打包文件变化，无 spatial 函数源码变化。
- [固定 GEOS scalar 实现](https://github.com/duckdb/duckdb-spatial/blob/b68b309d371dba936c5bb362980e559b7756b16d/src/spatial/modules/geos/geos_module.cpp)；读取文本 SHA256 `15f328eb8728e6aae71cd388eb8723fdccafbcf6993cbbc63fc4249382735e6d`。
- [固定 spatial scalar 实现](https://github.com/duckdb/duckdb-spatial/blob/b68b309d371dba936c5bb362980e559b7756b16d/src/spatial/modules/main/spatial_functions_scalar.cpp)；SHA256 `5ff248eb0b827e7f068a9afa1cd039468225f6cafcb56de75d19b77ad2c97831`。
- `src/scan/read_om.cpp`、`src/scan/projection.cpp`、`src/scan/axis_selection.cpp`、`src/include/duckomo/selection_budget.hpp`：当前 scanner seams。
- 只读研究输出 `/tmp/duckomo-005-scanner-research.md`，child run `388879e4-9360-4127-ac04-deb9ae46e6d4`；本文件已经整理其结论，不依赖临时文件作为唯一长期证据。

所有设计选择已决；运行 gate 尚未执行不等于研究中存在未决产品需求。若实施首个 capability gate 证明该 adapter 不可行，应记录阻塞并修订设计，而非宣称兼容或暗中换路径。
