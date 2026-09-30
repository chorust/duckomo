# Research: 规则网格空间下推

日期：2026-09-29。源码研究和设计决策不等同于实现验收通过。

本文件记录最初仅有 `ncep_gfswave025` 的 Phase 3 设计。后续已登记 68 个规则网格 domain，当前支持范围与样本核对见 [规则网格 domain](../../docs/regular-domains.md)。

## 1. 依赖与职责边界

**Decision**: 继续使用 C++17 / GNU C11、DuckDB v1.5.4 (`08e34c447bae34eaee3723cac61f2878b6bdf787`) 和 OM C (`d8855e418e2231ae8439f0c7e840fa3f93b371e3`)；查询仍为本地、单线程。新增规则网格映射层，只输出逻辑索引与切片。

**Rationale**: `src/om/reader.cpp` 的 `DecodeSelection` 已接收 N-D offset/count 并调用官方 Sans-I/O reader。无需新增格式解析、块规划或运行时依赖。现有 FPX/PFOR、Float32、NaN→NULL 语义继续适用。

**Alternatives considered**: 改动 OM decoder、自建 chunk 索引、引入 Python/Swift 查询运行时均没有必要；远程、缓存、并行属于后续阶段。

## 2. DuckDB 下推与精确过滤

**Decision**: 注册 `pushdown_complex_filter`，读取表达式但不删除、不改写传入的表达式列表；保留 `filter_pushdown=false`、`filter_prune=false` 和 `projection_pushdown=true`。将安全的空间条件复制为值对象放入本次 bind data，不保存优化器表达式的借用指针。

**Rationale**: 固定版本的 `duckdb/src/optimizer/pushdown/pushdown_get.cpp` 先调用 complex callback，再将剩余表达式重新构建为过滤器；关闭 table-filter pushdown 时 `FinishPushdown` 保留 LogicalFilter。因而候选选择可以缩小扫描，完整 WHERE 仍由 DuckDB 精确执行，过滤用值列仍进入现有 ProjectionPlan。`table_function.hpp` 提供此回调。实现必须用实际 SQL 与计划检查验证该路径，不能仅凭源码判为通过。

**Alternatives considered**: 打开普通 `filter_pushdown` 后自行执行全部 TableFilter 增加语义责任；删除谓词只靠扩大后的 bbox 会返回多余行；全表读取后过滤无法满足物理读取门槛。

解析范围为绑定到当前 LogicalGet 坐标列的直接比较 `=,<,<=,>,>=`、BETWEEN 和 AND，接受反向常量比较并调整运算符。只接受有限 DOUBLE 常量或已安全折叠/无损转换为 DOUBLE 的数值常量。对坐标列施加 cast/函数、参数未解析、NULL/非有限常量、OR/NOT 等保留残余并回退。AND 的独立安全子句可作为必要条件；OR 子树不得抽取单一分支。多次回调、bind data Copy/Equals、prepared statement 重绑定须保持独立性，不能沿用上次执行的区域。由于 callback 修改 bind data，首版覆盖 `SupportStatementCache()` 返回 false（参照固定 DuckDB `multi_file_states.hpp`），并测试同一 prepared statement 改变区域后的重执行；不能仅靠 Copy/Equals 推断缓存安全。

## 3. 参数与轴证据

**Decision**: 增加 `grid` 原生 STRUCT、`spatial_axes` VARCHAR[] 和 `domain` VARCHAR，复用 `dimensions`。grid 与 domain 二选一。`spatial_axes=[lat_axis,lon_axis]` 表示分离轴；单元素表示展平轴，grid 中 `order` 明确 `lon_fastest` 或 `lat_fastest`。所有空间轴名必须来自已通过校验的有序轴声明，不能仅凭 shape 推断。

**Rationale**: `src/scan/dimensions.cpp` 已验证全变量 shape、有序轴身份和 coordinates 元数据冲突；参数只补地理定义和空间映射。独立空间轴可出现在任意文件轴位置，额外轴按现有最后一轴最快的逻辑顺序遍历，不赋予时间等语义。

**Alternatives considered**: JSON 配置引入不必要的解析器；用 dimensions 名称自动推断网格不足以确定原点、步长或展平顺序；允许部分变量声明或隐式广播破坏对齐。

## 4. 浮点、接缝与候选选择

**Decision**: 纬度用 DOUBLE 的 `lat0 + y*dlat`，经度用 `lon0 + x*dlon` 后归一化为 [-180,180)。同一坐标生成函数服务输出和条件匹配；不使用比较容差改变 SQL。参考坐标比较预设绝对容差 `1e-9` 度，值比较沿用官方解码容差 0，NULL/逻辑位置精确比较。

**Rationale**: 单纯 floor/ceil 反算容易在浮点相邻边界出错。首版按一维轴点计算比较并以游标产生连续命中区间，不生成 nx×ny 坐标数组；复杂度为轴级 O(nx+ny)，内存不随全域行数增长。若后续使用二分/反算加速，必须与此精确轴匹配基准一致。经度反向、跨接缝甚至重复周期均能产生多个不重叠逻辑区间；不按相同地理坐标去重。验证所有输出纬度范围和所有坐标的有限性，包含乘法/加法溢出。

**Alternatives considered**: 隐式 epsilon 会改变 WHERE 语义；把经度下界大于上界理解为环绕违背 BETWEEN；全域坐标表物化增加不必要内存。OR 首版正常回退，不承诺降低 I/O。

## 5. 有界扫描与空路径

**Decision**: 新增 selection cursor，在现有逻辑顺序上仅枚举选中位置，并生成最后一轴连续的 `BatchSegment`。每次不超过 DuckDB 向量批次，所有依赖变量共享同一位置序列；官方 reader 负责每段的真实块读取。空选择在创建值 decoder 前返回，并显式标记 scan 完成，避免析构记录 incomplete_scan；无值依赖时直接生成坐标/cardinality，不读取值索引或 payload。

**Rationale**: `src/scan/batch.cpp` 与 `src/include/duckomo/batch.hpp` 已提供连续逻辑段到 N-D read_offset/read_count 的模式。更换范围迭代器即可延续解码、投影和 RAII；无法保证小于块的选择节省读取，验收样本必须固定为可跳块布局。

**Alternatives considered**: 每个点独立 DecodeSelection 放大重复读取；用全域线性包围范围会包含大量无关行；按文件块重排属于本阶段不需要的复杂度。

## 6. 验证与观测

**Decision**: 复用 `test/tools/duckomo_fixture_tool.cpp`、`duckomo_validation.cpp` 和 `ScanMetrics`，扩展版本 2 证据。基准先物化全域结果，再在临时表上执行相同 WHERE；坐标另以人工固定表/上游定义交叉核验，值另以官方 reader 核验。

**Rationale**: 全域 SQL 上再套子查询可能被优化器重新下推，不能作为关闭优化的基准。指标沿用成功定位读取和成功 decoder chunk 范围计数；不从选择大小推算节省。独立子进程测耗时和峰值 RSS，失败/取消单列记录。源码核对发现现有 bind 不接入 metrics，既有 sidecar 仅包含 scan 阶段 metadata 重读；Phase 3 必须让同一 query-local metrics 覆盖 bind/init/scan，分列 bind_metadata_bytes/requests 与 scan_metadata_bytes/requests，并汇总到 metadata 总量。优化器消除扫描的查询仍由 harness 确认完成状态与 bind 计量，不伪造零值。

**Alternatives considered**: EXPLAIN、输出行数或只比较时间不能证明局部读取；以生产 GridMapping 生成参考坐标会共享错误。

实施验收须覆盖任意轴位置、两种展平顺序、额外轴、跨批次、相邻浮点边界、反向/单点轴、接缝、值过滤、聚合及失败恢复。原设计曾要求 Linux x86_64 单独执行；后续用户将其支持与验证暂缓，本期门禁仅针对 Linux AArch64，不对 x86_64 兼容性作结论。

## 7. 首批 domain：ncep_gfswave025

**Decision**: 首批仅纳入 `ncep_gfswave025`，网格 `nx=1440,ny=721,lat0=-90,lon0=-180,dlat=0.25,dlon=0.25`，布局限定分离轴 `[lat,lon]`，shape `[721,1440]`。其他模型、同 domain 的展平/时序文件不通过此条目自动支持；用户可使用显式配置表达其他已知布局。

**Rationale**: 已核对上游提交 `34b9cea169395be9b4686f2b5b23eca26dfef7a2` 的 [GfsDomain.swift](https://github.com/open-meteo/open-meteo/blob/34b9cea169395be9b4686f2b5b23eca26dfef7a2/Sources/App/Gfs/GfsDomain.swift)（domain 对应及 RegularGrid 定义）与 [RegularGrid.swift](https://github.com/open-meteo/open-meteo/blob/34b9cea169395be9b4686f2b5b23eca26dfef7a2/Sources/App/Domains/RegularGrid.swift)（y*nx+x 及坐标公式）。映射能力在 Swift 层；本项目需要的小型 C++ 规则网格层不移植整个 exporter。

该源码参考提交日期为 2026-08-20，早于文件 created_at=2026-09-28T03:59:37Z；它是核对定义的固定版本，不声称已知生产该文件的服务部署提交。

真实文件为 `s3://openmeteo/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om`，本地 `build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om`，5,812,040 字节，SHA-256 `0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd`。15 个 Float32 值数组均 shape `[721,1440]`、chunk `[32,32]`，coordinates 为 `lat lon`，WGS84 BBOX 为 `[-90,-180,90,179.75]`。`forecast_reference_time`/`valid_time` 是 scalar 元数据，不解释为轴。历史读取证据见 `docs/issues/real-world-om-compatibility.md`。

**Alternatives considered**: exporter registry 只能作为线索；凭 shape 注册全部 GFS domain 不充分；投影域和 Gaussian 域超出范围。

当前已完成来源、shape、轴和范围核对，未完成新坐标输出的逐位置验收。实施先固定真实样本 manifest、以独立官方 reader 导出值参考，以固定上游公式独立生成坐标参考（不得调用生产 GridMapping），核对全域逻辑位置/缺测/坐标，再发布 registry 条目。参考角点包括 `(y=0,x=0)→(-90,-180)`、`(360,720)→(0,0)`、`(720,1439)→(90,179.75)`；核对非对称内部点及最后一轴变化方向，不能只用角点证明顺序。若验证失败，阻断 Phase 3 完成，不将此条目标记为支持。

## 研究收敛

参数形式、DuckDB 集成、网格选择、布局、domain 清单与验证方案均已确定，无待用户决策的澄清项。未执行的新功能 SQL、真实 domain 全位置对照、读取减少和取消恢复属于明确的实施门禁，不是已通过证据；x86_64 构建已由用户决定延期，不属于本期门禁。
