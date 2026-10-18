# Research: 统一区域入口的最近邻采样

**Date**: 2026-10-15
**Scope**: Phase 0；以 [spec.md](spec.md) 为准，只规划 points/nearest，不实现代码。
**Baseline**: main / `d2a134d04ba4fd95b9c26e9f6ec656926709c336`。代码行号仅为本次阅读位置，后续实现以符号为准。

## R1 — 公共入口与参数

**Decision**: 仅扩展 read_om_regions；新增 method（points 默认、nearest）、max_distance_m、out_of_bounds（null/error）。其它策略首版固定为 point_on_surface、grid_earth sphere 和 propagate，不注册只有一个合法值的 representative/distance_metric/missing 开关。points 显式携带采样专用参数时拒绝。

**Rationale**: 用户要求减少函数和主要模式选择。`GetReadOmRegionsFunction` 已与 read_om 共用 binder/global/local 回调（`src/scan/read_om.cpp:3077`）；方法绑定时确定即可保持原始路径和 schema 完全不变。

**Alternatives considered**: 新采样函数、单数别名、region_mode+method 已由用户否决；遇空自动 nearest 会混淆点平均与代表值。

## R2 — 复用读取链路，分开任务语义

**Decision**: common bind/reader/lifecycle 保留，在现有内部状态增加 RegionMethod 和 optional immutable NearestPlan；nearest 有独立内部 cursor 和 scan 分支，不复制整套 scanner、不注册新 SQL factory。

**Rationale**: `GetNextRegionTask` 枚举区域×源逻辑位置；`FilterRegionCandidates` 最终 ST_Covers 原始源点（read_om.cpp:1969、2636）。nearest 的源点常在面外，无法经过该过滤。`SpatialLayout` 已保存 non_spatial_axes、strides 和真实点序（spatial_layout.hpp:20；spatial_layout.cpp:69–172），但缺少由非空间轴 indices + x/y 反建 logical_index 的受检 helper。

**Alternatives considered**: 将最近点塞给 points 过滤会再次剔除源点；用全 shape AxisSelectionCursor 枚举再筛固定点会随空间点数放大调度。推荐非空间轴 cursor 复用各轴选择逻辑，空间位置作为固定常量插入。

## R3 — 代表位置与依赖证据

**Decision**: nearest-only adapter 精确绑定 ST_PointOnSurface(GEOMETRY)→GEOMETRY、ST_X/Y(GEOMETRY)→DOUBLE，求点后用 ST_Covers 验证。EMPTY 在求点前分流。points 不增加这三个函数的必需依赖。记录 `point_on_surface_v1` 和实际 spatial extension_version；不声称跨依赖版本代表点完全相同。

**Rationale**: 现有 RegionGeometryAdapter 已通过系统 catalog、FunctionBinder 和 ExpressionExecutor 获取公开空间函数（region_geometry.cpp:34–116）。继续使用该边界，不读私有 GEOMETRY 布局或直接链接 GEOS。

**Evidence actually executed**: 父代理在本地官方 DuckDB v1.5.4/v1.5.5/v1.5.6 匹配 spatial 包探测上述三个准确签名；extension_version 分别为 28db190、eb1e57c、04270fe。三个版本对矩形 (100.1,20.1)–(100.2,20.2) 返回 POINT (100.15 20.15)，ST_Covers=true。此探测仅证明依赖接口，不是 nearest 已实现证据。运行版本/hash仍由实施 N0 重新冻结。

**Alternatives considered**: centroid 不保证在面内；自写几何内点会新增拓扑与边界责任；从 WKB 读取第一个顶点不符合既定代表策略。

## R4 — 能力、覆盖与周期

**Decision**: 首版实现纯规则经纬度球面定位器；复用 spatial.identity_definition 的地球半径，legacy explicit grid 当前已有默认 sphere radius=6371229m，不新造地球模型。规则坐标使用现有 RegularGrid::Coordinate，不能为新算子换坐标公式。ncep_gfs025 提供单独 source/capability 证据；显式规则网格通过数值/拓扑资格检查接受，声明来源仍由调用者承担。

周期为可审计的资格结果：nx≥2、有限且不重复的经度序列、一圈步长闭合 `nx*abs(dlon)==360`，首末源点不重复；全球覆盖还要求纬度端点正好 -90 与 +90。不满足该精确闭合规则的网格不得作为全球周期；原始经度跨度小于360且资格合法时只按区域闭域处理，否则能力拒绝。没有“近似周期”的模糊容差。非周期区域经度先在原始 unwrapped 轴上检查跨度<360、唯一及可解释的 target lift，纬度使用坐标端点闭域。经度规范化跨 ±180 不能把区域域扩大成接近全球。

**Rationale**: `RegularGrid` 接受正负步长并归一化 lon（regular_grid.cpp:48–80、122–136），读取容许的重复 wrap 或 legacy 超纬行不自动适合采样。ncep_gfs025 固定 1440×721、lat0=-90/lon0=-180、step=.25（domain_registry.cpp:112），周期闭合可精确表达。GridDefinition 的 regular 分支实际沿用 binary64 RegularGrid，不将 projected/Gaussian 的 Float32 numeric policy 生搬到规则网格。

**Alternatives considered**: 对所有已登记 domain 一次开放会继承未经核验的能力；对归一化 lon 求 min/max 会误判跨 seam 的区域覆盖；用半格单元扩展域会改变 spec。

## R5 — 最近邻数值核与初版安全算法

**Decision**: 冻结 `sphere_haversine_f64_v1`：规范经纬度，差经度到 [-180,180]，binary64 的 haversine→clamp[0,1]→atan2，距离为中心角乘 grid sphere radius。距离 double 相等时比较原始 point_index；不使用 epsilon 改平局或覆盖。源纬度为 ±90°时用精确极点公式，消除 cos(pi/2) 浮点残差；目标等于源坐标则距离精确 0。计算文件关闭 contraction/fast-math。

首版可信路径使用流式全坐标穷举，每次只持有一个候选和当前最优值，定位一次/区域，不读取任何值或坐标数据文件，不物化空间点数组；取消每≤256次比较检查。坐标遍历复杂度 O(regions×nx×ny) 明确记录，不能把值读取局部性宣传成定位 O(1)。实现阶段只有在独立 oracle 和浮点平局证明完成后才能替换成候选缩减；正确性基线始终保留。该策略满足本期有界内存和局部值读取，性能优化另用证据决定。

**Rationale**: 真正地表最近不总等于逐轴最近。父代理对目标 (5°,44.95°)、经度[0°,10°]、纬度[40°,50°] 独立计算，轴最近纬度40°距离约686.10km，地表最近纬度50°约675.36km。250组随机小网格上逐行最近经度候选与全点穷举相同，但这不是浮点平局证明：接近极点/对跖点时非最近经度也可能舍入为相等距离，省略它可能破坏最小point_index。流式基线避免承诺未经证明的剪枝。

**Alternatives considered**: 简单经纬舍入不满足 FR-004；只保留每行两个经度候选存在浮点等距风险；KD-tree 需要全域坐标/索引内存；首版引入复杂区间算术定位器收益不足。未来可在经审计误差界下剪枝，不能以任意容差修改契约。

## R6 — sampling schema、状态与来源

**Decision**: nearest 只追加一个 STRUCT sampling（region_id 后、om_source 前）；原变量名/FLOAT 保留。sampling 含几何状态、源坐标/索引、距离、方法/依赖/地球模型身份和非空间轴 indices；variables 为 MAP(VARCHAR,STRUCT(status,source_count,valid_source_count))。

几何状态与变量状态分开。source_null 不令几何状态变成失败；beyond_max_distance 保留候选但 om_source=NULL；域外/EMPTY 无候选。nearest 成功或源值缺测时 om_source 指向真实 logical index。include_source 沿用原开关，不再新增 include_sources；旧 om_source 本就没有 lon/lat 字段，真实源坐标在 sampling 内。

**Rationale**: 原 `MakeOmSourceValue` 以真实完整 logical_index 获取所有轴位置（read_om.cpp:807–839），不可用代表目标索引冒充原始位置。变量状态与几何定位可以有不同依赖和基数。

**Alternatives considered**: 通用 value 列和大量顶层诊断列提高迁移成本；把 sampling.status 同时代表所有变量会在混合缺测时失真。

## R7 — 子字段投影与 MAP 的保守依赖

**Decision**: nearest 才为 sampling 启用 supports_pushdown_extract，ProjectionPlan 接收 TableFunctionInitInput.column_indexes，维护 FULL_READ 与 PUSHDOWN_EXTRACT 路径和最终输出叶类型。仅几何子字段无值依赖；variables MAP 或完整 sampling 依赖全部当前绑定变量。首版不承诺按 MAP 常量 key 再裁剪变量；用户通过 variable 参数限定数组。

**Rationale**: 当前投影仅消费 input.column_ids（read_om.cpp:2597），丢失 STRUCT 子路径；DuckDB 已提供 ColumnIndex 与 supports_pushdown_extract（duckdb function/table_function.hpp:117–145、315–317）。父代理另检查三版本头文件均具备接口。MAP key 访问不是 STRUCT 子字段，不能凭 key 字面量就猜某个状态未被观察。若宿主返回 FULL_READ 就计算完整 STRUCT，宁可多读也不能输出假状态。

**Alternatives considered**: 仅加一个合成列 enum 而不处理 nested projection 会让几何详情读取全部值；为少读填 NULL 状态会破坏结果。自定义表达式 rewrite 捕获 MAP key 暂不值得，且无法只凭返回列推断过滤依赖。

## R8 — filter 与结果 cursor

**Decision**: nearest 只提取非空间轴的安全必要条件，lat/lon/值/采样详情保持最终 residual。目标完整校验和定位在 WHERE/LIMIT 前完成；严格域外参数即使被过滤掉仍报错。结果 cursor 为每个区域独立枚举非空间轴，EMPTY/域外/超距也输出同样轴组合而不创建 decoder 请求。

**Rationale**: 当前 ObserveComplexFilter 将 lat/lon 作用到源空间谓词（read_om.cpp:1551），nearest 必须按 method 分流。源点在面外是正常情况，不能再使用 polygon covers 或 envelope 删除来源。BuildSelectedBatchSegments 要求逻辑位置严格递增且无重复（axis_selection.cpp:385），需批内 sort/unique + scatter 或单区域递增批次，不能直接混入重复来源。

**Alternatives considered**: 在 source 读取前执行最终值过滤会选择错误；给 EMPTY 虚造一个 source index 会污染来源与语义轴。

## R9 — 内存、计量与复用边界

**Decision**: 保留 005 输入预算；NearestPlan 自有容量≤8MiB（≤4096目标），任务描述符只 O(workers)，全局调度自有容量≤8MiB，单 worker nearest 新增 scratch≤512KiB，实际批次取 min(STANDARD_VECTOR_SIZE,容量预算)。reader 缓冲沿用现有受检账本，单列第三方未知内存，不将以上称为全查询上限。

operation 仍 read_om_regions；points 的 region_record_pair 与历史 fields 原义不变，nearest 添加 sampling 节点和 region_axis_result 单位，未执行的区域 covers 指标不能伪造数值。所有 exact 计数只在对应工作完整后发布；失败/取消/提前停止明确 incomplete。CPU定位成本和I/O成本分列。

**Alternatives considered**: 为所有region×time预建任务表或全域树违背有界要求；用返回行数推网络字节、复活跨查询缓存不在范围。

## R10 — 测试、真实输入和运行矩阵

**Decision**: 复用 C++17/native、SQLLogicTest、Python开发期参考工具和现有 fixture/runtime脚本。Linux AArch64，DuckDB1.5.4/1.5.5/1.5.6 与匹配 spatial/官方HTTPFS；不扩大平台。新增独立采样manifest与N0–N7验收记录，旧005历史gate不改写。

固定合成fixture无网络可重生，至少3非空间轴、多变量、反向/展平布局、周期/平局及拒绝能力。独立参考穷举所有源坐标，不调用生产 locator，值由官方reader直接读取核对。高精度几何参考与binary64兼容参考分开，源索引精确、距离误差冻结。

真实东城区/112068504字节GFS文件当前在忽略的build目录，不能作为唯一交付输入。实施时先将既有52顶点局部几何转换为标准WKB，记录原文件/子几何hash、来源声明和引用许可，附有限数据获取/本地路径输入流程。滚动GFS对象失效不能悄悄换文件；无冻结输入N5=blocked。可用原文件提取小型等坐标合成fixture支持无网络教学，但不能冒充真实原文件验收。

**Existing commands**: `make region-validation` 包含区域依赖/验证；普通 `make test`/validate.sh默认会略过regions.test及region native，不能替代006验证。`scripts/build-version.sh v1.5.4` 等在隔离版本目录构建；`scripts/fetch-region-runtime.py`管理官方包/身份。

**Alternatives considered**: 只写新SQL而不接验证注册可能被默认跳过；只依赖ignored文件不可独立复现；整份GFS入仓库增加不必要体积。

## Evidence status and delegated collection

第二路报告确认规则坐标应固定为现有DOUBLE仿射公式（GFS .25°恰可表示），不是另改成上游Float32；已采用原始unwrapped域及流式穷举来解决其列出的数值/周期疑问。报告另核查GADM archive只有北京整体而没有本地这份东城区边界，因此N5来源声明保持未知，不能错归GADM。

子代理信息收集使用用户指定 `magpie/codex/gpt-6-luna:max`，两路只读：scan-integration（run `9f7ca810-628c-4f1c-922b-d8411871b5ff`）与 locator-validation（run `a2d15bb4-a82f-4583-a0d5-232873432438`），workflow `edbe5f4f-5291-4703-8d53-cd6775edf081`。最终计划由父代理整合并对方案取舍负责。

已经运行：三版本公开空间函数签名与简单内点覆盖探测；父代理球面反例和250组小网格候选/穷举数学实验。未运行：006 SQL、new native、内存/性能/远程/独立复现。没有待澄清技术占位；依赖hash、真实输入和完整运行证据由执行阶段按契约收集，缺失时阻塞对应gate而不是虚填。
