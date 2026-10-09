# Research: Phase 6 多类型网格与远程空间选择

日期：2026-10-02；2026-10-03 完成只读复核并收束为 Phase 0 输出。主代理和三个只读 subagent 分别核对固定网格来源/有界远程元数据、现有选择/OM 接口、固定 2.0/httpfs API。此文形成工程决策，不代表构建、测试或新网格交付通过；前期候选记录见 [planning-notes.md](planning-notes.md)。Phase 1 的正式字段与预算以 [data-model](data-model.md) 和 [contracts](contracts/sql-interface.md) 为准。

## R1 — 网格数学与依赖

**Decision**: 查询运行时使用 C++17 的封闭 `GridDefinition` variant：保留现有 RegularGrid，新增 RotatedLatLonGrid、LambertGrid、StereographicGrid 和 ReducedGaussianGrid。球面公式来自固定 Open-Meteo 定义并独立核对，采用明确版本的数值策略；本期不链接 PROJ/GDAL/xtensor，也不新增 Python/Swift 运行依赖。空间扩展仅供显式空间关系示例使用。

**Rationale**: 本期只需有限球面投影及 N 网格，现有工程可实现封闭、受检规则。固定来源是 `b06f4760fd1f997e5559bb380f64c5e496b4a509`；同机构 domain 不能笼统归为同投影。内核坐标原生 x/y 与输出地理角度分别保存；源参数/地球半径和版本影响身份。

**Alternatives considered**: 直接采用任意 WKT+PROJ 扩大格式和部署面；直接复用空间扩展内部对象会把普通读取绑定到额外扩展及 ABI；每类创建独立扫描入口会重复轴、过滤和远程逻辑。

## R2 — 坐标数值与 Gaussian 来源

**Decision**: 新投影接受 `numeric_policy='float64_v1'` 或 `'openmeteo_f32_v1'`。登记条目固定来源兼容的 f32 参数、原点构造和逐步算术规则，输出 widen 为 DOUBLE；显式等价声明采用同一策略。对 libm 结果用事先固定参考容差验证，不宣称跨平台 Float 位模式一致。Gaussian 采用显式纬度/逐行定义，登记 N160/N320 行长取固定上游表；生产者近似纬度由生成器逐行固化完整 2N 的 f32 widen 后 Double 行表，不能用南北镜像。经度也使用 f32 步长、乘法及规范化，禁止不受控 FMA/重结合；角度转换遵守源先乘后除顺序。标准 Legendre 根纬度用不同显式行表/身份，不静默替换。

**Rationale**: 上游 `GaussianGrid.getCoordinates` 使用 `180/(2*N+0.5)`，不是 Legendre 根；N160=138346 点、N320=542080 点。区域映射的 rounding/truncation 和 inclusive 端点也不能从普通 BBOX 重建。行表是 O(行数)，允许存储；全域逐点坐标表不允许。同一数值规则用于候选比较和输出，参考容差不影响 WHERE。

**Alternatives considered**: 标准数学纬度覆盖生产者位置会错配；全域 lat/lon 数组代价随点数增大；只保存 N 值忽略每行点数和经度起点不完整；凭最新来源运行时生成不复现。

## R3 — 保守候选算法

**Decision**: 投影/旋转最低可靠路径采用原生 scanline 子线窗口的逐点地理坐标判定，按真实存储顺序生成相关片段；不反投影用户矩形四角作为候选证明。每窗口≤65536 点、≤4096 个 `[begin,end)` 段，每 worker selector payload≤256 KiB。超限时，在发出该窗口任何位置之前丢弃全部段并扩大到全窗口。无可用空间必要条件时直接 full interval，不逐点预扫。Gaussian 按行表选纬度，再按每行经度规则逐点或受检区间判定；区域 point 序来自显式局部行表。规则网格保留快速一维选择，但展平行片段也改为惰性生成。每256次评估及全部准备/读边界检查取消。

**Rationale**: 候选判定使用和输出完全一致的坐标函数，遍历全部原生点时无角点漏点风险，无需为非线性投影证明连续曲线极值。单遍空间时间 O(P)，布局重放最坏 O(P×非空间记录数)，内存与点数无关；这是有明确成本的首版选择。与其他轴相交按实际 stride 遍历，不能把相关 x/y 拆成独立笛卡尔积。CPU指标报告含重复的coordinate_evaluations，不以全域set统计unique点。

**Alternatives considered**: 四角或固定边采样不能证明不漏点；全域 tile/点 index 增加来源、预算和缓存生命周期；具有区间误差证明的投影 tile pruning 可后续独立优化，首版不要求。保守全扫可保证正确性但不能独自满足每类实际读取收益。

## R4 — 选择生命周期、计数和调度

**Decision**: filter callback只复制有界安全谓词，不在优化阶段全域预扫描。GlobalInit分别计算逻辑记录与NativeWindow数量的受检上界，由窗口数而非ceil(记录数/65536)约束worker。共享游标在短mutex下发放互不重叠descriptor，昂贵坐标preflight在worker内执行，每worker最多65536个源位置并独享decoder。候选精确总数在游标exhaust且全部窗口preflight完成后可算，权威快照只在最终成功完整扫描时发布；LIMIT/取消/失败为NULL。observed是已完成preflight的候选累计，非descriptor原生点上界；可受检轴积确定的无空间条件路径可提前得exact。全局轴区间及复制谓词payload≤1MiB，超限扩大相关条件。坐标/count路径不创建值decoder，含坐标残余的COUNT不等同仅凭候选积返回总数。

**Rationale**: 当前源码只有 SpatialBatchCursor/AxisSelectionCursor 和 `task_positions`，不是已实现的 QuerySelection/ScanTask 类型。当前 BuildFlattenedPointRanges 可物化每行片段，无预算；需修正而非延续。不能为预先得到 candidate_rows 扫描全网格或把上界当精确数。原始点、逻辑记录、候选和实际返回行分别计量。

**Alternatives considered**: bind/optimizer 全扫坐标增加不可取消延迟；预列全任务违反内存要求；为线程精确数而先完整枚举没有必要。

## R5 — 逻辑切片到 OM Range

**Decision**: 保留BuildSelectedBatchSegments的逻辑段语义，但改为预算感知builder/惰性逐段；≤64KiB映射payload必须在分配前缩小batch，不能先物化rank8整批约400KiB再切分。官方DecodeSelection仍接收矩形read_offset/read_count；首版使用连续逻辑段。只有profiling显示重复块解码影响SC-003时，才实现有界逻辑矩形合并，同变量/固定前缀/连续后缀可扩大矩形、position map取候选；每变量额外scratch≤1MiB，不自行规划LUT或压缩偏移，不新增解码块缓存。

**Rationale**: OM API 一次处理一个矩形；片段可能多次命中同块，range cache 可以减少字节但不能减少 decoder 调用，重复 decode 必须全部计数。`io_size_max=64KiB` 是合并目标，不是严格 buffer 上限；单块可超过它。Float32 chunk scratch 由 `4*product(chunk_shape)` 决定，额外缓存/解码工作集不能混入 selector 的 256KiB 预算。

**Alternatives considered**: `index*sizeof(float)` 不适用于压缩 OM；自行块调度违反官方 reader 职责；未计容量的 decoded cache 隐藏复杂性；为零读取查询预取整个物理页违规。正式收益需固定样本实测，首版算法不保证所有布局加速。

## R6 — SQL 与未来空间计算接口

**Decision**: 保留旧七字段 grid；新 grid 使用 `version=1, type=...` 的严格 STRUCT，仍配 spatial_axes。增加 `include_source BOOLEAN=false`，true 时在既有列末尾追加单列 `om_source STRUCT`。增加 `om_grid_info(path, grid/spatial_axes/domain/dimensions...)` 描述函数，复用 metadata binder、不创建值 decoder。定义身份是规范序列化 SHA-256，完整网格和区域网格分开；源位置含对象身份/版本强度、object logical_index、object point_index、parent point_index 与原始轴位置。描述和源位置不自动生成 GEOMETRY。

**Rationale**: opt-in 单列避免默认 SELECT * 变化，原始逻辑位置不是输出行号。结构身份支持后续 slice/reduce/interp/regrid 的源支持范围。CRS描述严格区分 sphere 与 WGS84；点与 polygon 示例使用明确相同坐标来源，x=lon/y=lat，不要求自动空间函数下推。未经证明的邻接、单元、面积和向量方向为 unsupported/unknown。

**Alternatives considered**: 默认追加多列破坏 schema；在每行重复大网格描述增加成本；强制 GEOMETRY 绑定 spatial/CRS版本；以经纬度去重或 domain 名单独作为身份不足。

## R7 — 元数据与可重生 registry

**Decision**: 完整 JSON manifest 为固定生成输入，新增生成器构建时生成纯 C++ 定义；当前registry是手写表，没有可复用生成器。运行时不解析远程 JSON 或最新定义。生成器版本、上游提交、原点构造、数值策略、行表/局部片段和适用轴布局均记录。旧registry数学来源b06f476…和旧真实参考34b9cea…保持各自身份。WKT validation 只接受封闭的已识别源 profile，提取并比较投影、半径、单位、关键参数/轴；未知或不能证明一致的相关 CRS 声明拒绝新网格地理绑定，未提供 CRS 的对象依据显式可信声明。上游部分 WKT 非标准语法应使用专门 profile，不假装通用 WKT parser。BBOX 是来源注释/诊断，不是精确覆盖。

**Rationale**: 区域首纬度可以略超标称 BBOX（生产者取整所致）；投影 WKT 也常只含角点范围。不能重用规则网格端点规则拒绝有效投影点，不能反过来用 BBOX 匹配证明正确。

**Alternatives considered**: 任意 WKT 自动识别扩大范围；丢弃冲突 CRS 不满足 FR-005；仅登记名字和 shape 不可审计。

## R8 — DuckDB 2.0 精确目标

**Decision**: 保留基线 v1.5.4 `08e34c447bae34eaee3723cac61f2878b6bdf787` + httpfs `c3f215ab360f04dc3d3d5305fa81849c0121f111`。2.0 预发布固定 `v2.0-cyanoptera` 的 `7264a9f0e5b487358100f408826b8ae9e868b031`，配套 httpfs `5e34903685e4d429cbb19b063406abdd8ce30591`、ci-tools `9b1020499dc85966be3342541f98c8e8e4aada89`；基线ci-tools仍b777c70…。提供按精确提交的manifest build matrix，重做根目录固定stage与每组合生成的metrics identity，单独适配引擎bind/identifier、TypedKwargs/FunctionSignature、BoundColumnRef Binding/Depth和表函数入口；公共网格内核不包含引擎表达式类型。本期不整体迁移C API。

**Rationale**: 同步精确 FileSystem::Read 仍可用，复杂 filter callback 和 QueryEnd 关键语义可延续，但 binder names 从 string 变 Identifier 且若干 API 改变。现有 CLI 只接受 vMAJOR.MINOR.PATCH，需扩展为 manifest 驱动而非用移动分支构建。正式 v2.0 tag 本次未发现，正式版复验保留为独立发布门禁。

**Alternatives considered**: 只改版本常量未覆盖 ABI；使用最新 main 不复现；为了大版本整体重写或强行异步 I/O不由需求推出。

## R9 — 2.0 httpfs 补丁移植

**Decision**: 1.5与2.0各自持有固定补丁集和配套capability manifest。2.0先施加固定官方overlays一次，再应用自有移植补丁；在签名前S3RequestExecutor/HTTPRequestSession、physical transport attempt边界重新实现strict OM range policy，覆盖core retries和S3 refresh/region retry，阻止ReadAtWithFallback/FullDownload路径。保留每次新鲜HEAD/0-0授权、精确206、对象token、取消、脱敏与全部已收body。新004组合采用ABI3及engine/full pair/header/patch身份核验，旧003 ABI2证据不重写。连接访问分区现未随secret/endpoint/region改变失效，须增加授权epoch/opaque fingerprint及真实签名S3切换/撤权门禁。

**Rationale**: 新 httpfs 源码迁移到 src/http/* 与 src/s3/* 并引入 HTTPTransportManager；本次在 /tmp 固定源码树 `git apply --check` 两份旧补丁均失败，不能宣称可重用。远程完整验收依赖 003 G3–G7补齐，再按新组合复验。

**Alternatives considered**: 委托 stock httpfs 会允许FullDownload并丢失真实计量；自行HTTP/S3客户端重复授权/签名；全数据库 hook 影响隔离。

## R10 — 观测兼容

**Decision**: 公开metrics JSON升至schema_version=4，保留完整旧v3快照为legacy_v3（其中legacy_v2照旧）。正式selection字段为exact_candidate_records/null、observed_candidate_records、upper_bound_records、count_complete、coordinate_evaluations、窗口回退累计及准备时间；memory分别报告selector/global轴选择、tasks/batch、definition、decoder/coalescing、transport control、shared cache和总query-owned。旧candidate_rows数字仅保留旧估计/上界用途。修复全查询transport_attempt_ids_ set无界增长；终结去重使用有界active状态/单调序号及一次终结invariant。reader替换buffer时瞬时双份须原位复用或计入峰值/界。QueryEnd单次发布，失败/取消未知仍null。

**Rationale**: 相关惰性选择无法预知全部候选，不能直接把v3整数改成nullable而不版本化。选择CPU成本、重复decode和部分完成需可见；既有变量、transport body及cache口径不改变。

**Alternatives considered**: 静默改v3字段类型影响脚本；只增加解释文字仍可能把上界当精确；用任务数/EXPLAIN代替收益不成立。

## R11 — 样本与发布门禁

**Decision**: public OM只做有界源调查；真实完整对象在实施期按hash/version固定并独立全量参考。rotated与Lambert已有OM v3候选，stereographic公共旧对象为OM v2、不能充当v3验收；N160与N320区域public prefixes此次未找到对象。样本获取作为实施前验收输入门禁：从可追溯生产归档/明确授权来源取得真实OM v3，不能把v2转换或合成结果称为真实原生v3。未满足任一类真实证据则该类及完整Phase6不得标verified，设计仍可推进。

**Rationale**: 型定义、元数据Range成功不证明值/坐标，更不证明s3://入口、冷态收益。完整门禁应含四类真实、Gaussian行/局部参考、跨源、内存、并行、空间示例、2.0预发布和独立复现。

**Alternatives considered**: 只用合成样本不满足FR-027；通过未知格式转换改变本期OM范围；不存在的公开链接不可写成可执行下载步骤；样本暂缺不应变成重复向用户确认。

## 研究问题收束

网格公式/数值策略、精确版本组合、候选包含性、有界游标/计数、SQL身份、WKT profile和真实样本策略已有工程决策。真实样本归档、补丁移植与运行门禁为实施工作而非未决需求。未完成事项不能记为通过；本次不构建或运行实现测试。

## 2026-10-03 复核证据与取舍

**Decision**: 保留既有固定2.0提交，不追随已移动的dev HEAD；真实样本和输入冻结作为H0/H1/H6门禁。新选择不先建立全域tile摘要或坐标缓存；worker子线preflight与有界轴选择解决长准备、内存和取消。具体字节预算已在selection-and-io确定。

**Rationale**: 官方GitHub refs/API/raw源码确认引擎7264a9f…（提交时间2026-10-02T10:41:43Z）存在，其httpfs.cmake固定5e349036…并APPLY_PATCHES；本次无v2.0* tag。重新git apply --check旧两补丁均失败。httpfs gitlink ci-tools=9b102049…与本组合相符，其自身engine gitlink不能取代顶层engine。现脚本和hardcoded profile pins均需适配，不把read-only源码核查写成编译通过。

Gaussian f32重算的首/末纬度：N160=(89.57877349853516,-89.57878112792969)，N320=(89.78923034667969,-89.78922271728516)。NumPy Legendre根交叉实验与producer最大纬度差约0.00869158°/0.00435344°，说明两规则不能用浮点容差混合。实验只核对数学，不是产品oracle/真实值验收。

N320区域固定源ecmwf_aifs_europe_ensemble[_mean]的名义bounds为lat33.0211..<70.9601/lon-11..<37。按源f32/round/截断重算为parent rows[67,203)、136行、14747点；首行parent67的x[437,450)+[0,47)为60点，末行parent202的x[1091,1125)+[0,116)为150点。首/末纬度70.96018981933594/33.02107620239258略越名义bounds，不能用BBOX截掉。源reduced_gg loader仅核总点数，实际OM/GRIB局部点序仍需独立证明。固定源没有全域N320 producer domain，不能捏造S3 prefix。

Phase1交叉复核修正：Gaussian固定源WKT声明WGS84椭球6378137/298.257223563，新声明允许此封闭地理来源模型，投影/旋转仍为sphere；不新增椭球投影算法。完整网格parent_grid_id=NULL，区域parent是相同定义去掉subset_segments后的canonical身份。旋转右手basis、rotation正方向及源减atan2的180°映射已在SQL契约固定，f32路径保留源θ/ϕ逐步表达式，不能按pole字段名称机械变号。

旋转约定另作独立double算术研究检查：Python random seed=4，1000组pole_lat∈(-80,80)、pole_lon∈(-180,180)、native x∈(-170,170)、y∈(-80,80)，固定源inverse与SQL契约basis（rotation=180）最大角度差1.27e-13 degree；阈值1e-9检查通过。这只验证符号/公式映射，不验证f32、极点或产品实现，H1仍需独立固定参考。

**Alternatives considered**: 继续mutex内整线预扫会延迟首次批次与并行领用；仅放宽选择budget不能解决transport history set；仅记录最终响应遗漏隐藏retry；只跑匿名HTTP撤权不能证明签名S3权限配置变更。扩大候选优先于不安全截断，来源派生不替代真实位置证据。

固定Open-Meteo源文件hash（Sources/App/前缀，commit=b06f4760fd1f997e5559bb380f64c5e496b4a509）：

| 文件 | SHA-256 |
| --- | --- |
| Domains/GaussianGrid.swift | b913a4ec63b5889466d44ffffccd38aa4f9288d07c4c3b6786df9aa9f9c6d516 |
| Domains/GaussianGridArea.swift | 67814e1d2e211f2bbc03c16a9b5866c63607b8861fda04c1ad58aafc7b66d493 |
| EcmwfEcpds/EcmwfEcpdsDomain.swift | 70cccf553cbeef823d8ac7da7df9e686e1761871dc079a90be0dabcfea258805 |
| EcmwfSeas/EcmwfSeasDomain.swift | 2588451da26c1def64bde88dd0ba8af0e3257e4865bb4c78ac7ac3e7099d0d16 |
| Helper/Download/Curl+Grib.swift | 212e97efb147bf80c466ad3f19f1cbd94f08b92a4639f79c47de1ef937c2c593 |
| Domains/ProjectionGrid.swift | 526cb8b4970898eb07cb55e33a2d9a81cd27271e195b9a767e1e4f9f87a77c69 |
| Domains/RotatedLatLon.swift | b468d3cb70d3a92e17ada6b8c2c8de58e8fead46b15b8eca14bcd13067c1e09d |
| Domains/LambertConformalConic.swift | 6f27864ff27eb4157548a61147eee5db00bed1ffa82779ec4a2a0b8db2306067 |
| Domains/Stereographic.swift | 225b3286b1161634810d41a920792d1fce5ee627f419cbf889cf1477428b8d81 |

来源：[固定引擎httpfs配置](https://github.com/duckdb/duckdb/blob/7264a9f0e5b487358100f408826b8ae9e868b031/.github/config/extensions/httpfs.cmake)、[官方预发布说明](https://duckdb.org/2026/09/02/try-duckdb-20-alpha)、[固定网格源码目录](https://github.com/open-meteo/open-meteo/tree/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Domains)。在线发现使用byted-web-search，事实以官方refs/raw源码核验；本次只读检查及数学实验没有构建/下载全量新样本/运行远程性能。
