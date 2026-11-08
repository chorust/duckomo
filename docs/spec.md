> 当前远程读取使用 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)，不再使用专用 ABI、自有 LRU 或 transport observer。003/004 中相关历史条款由此契约替换；复现请见 [官方流程](official-httpfs.md)。

# DuckOMO 接口说明

当前工作树包含本地扫描、维度语义、列裁剪、规则/投影/Gaussian 网格、多区域 polygon 读取、官方 HTTPFS 的 HTTP(S)/S3 范围读取、并行任务、opt-in 源位置、网格描述和 v4 扫描指标实现。开发源码还包含跨 chunk 时间入口 `read_om_series`；Feature 010 当前源码验收仅在声明范围内 PASS，见 [follow-up 验收报告](../specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/report.md)。v0.3.0 GitHub 包包含实验性 005 区域功能；当前源码的跨平台完整验收仍未完成，已验证范围见 [元数据修复记录](../specs/005-polygon-region-statistics/evidence/metadata-selection-fixes.md)；004 的四定义/五冻结对象、三版本/AArch64 H0–H9 已通过，含独立 AI 复现，见[当前最终验收](../specs/004-multi-grid-selection/evidence/current-20261021/report.md)。DuckDB 2.0.0 兼容性已验证，并随 v0.3.5-rc.1 预发布提供；这不代表各 Spec 门禁完成，也不把开发中的 series 纳入已发布功能身份。[README](../README.md) 提供用法示例；原有空间契约见 [002 SQL 契约](../specs/002-spatial-pushdown/contracts/sql-interface.md)，维度与远程约定见 [003 SQL 契约](../specs/003-dimensions-remote-parallel/contracts/sql-interface.md)，多网格接口见 [004 SQL 契约](../specs/004-multi-grid-selection/contracts/sql-interface.md)，区域接口见 [005 SQL 契约](../specs/005-polygon-region-statistics/contracts/sql-interface.md)。

## 支持范围

| 项目 | 当前行为 |
|---|---|
| 输入 | `read_om` 每次读取一个本地/HTTP(S)/S3 对象；开发版 `read_om_series` 接收一个精确变量目录前缀，按时间计算 chunk 键且不列目录。二者都不接受 glob 或文件列表 |
| 格式 | OM v3；Float32；FPX_XOR2D / PFOR_DELTA2D_INT16 |
| 布局 | 根数组、层级值数组及附属元数据；rank 1–8，不接受零长度轴 |
| 多变量 | shape 和有序轴身份一致；不自动转置、广播或连接 |
| 时间序列（开发版） | `read_om_series` 使用冻结 model 时间默认值或明确覆盖；显式维度/空间声明、闭区间 UTC 窗口、初次缺对象 skip/strict。Feature 010 当前源码验收仅在声明范围内 PASS：linux_arm64 / Linux AArch64，DuckDB v1.5.4、v1.5.5、v1.5.6、v2.0.0。真实 producer 样本仅为两个相邻 IFS025 `temperature_2m` 对象、一个点和一个边界窗口；其他十个注册模型仅有 provenance 与 synthetic runtime equivalence，N160 为合成。`read_om_series` 仅是开发源码功能身份。详见 [follow-up 报告](../specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/report.md) |
| 缺测与精度 | NaN → SQL NULL；保留 ±Inf；值与官方 Float32 解码结果精确一致 |
| 空间坐标 | 显式规则经纬度网格、68 个规则 domain；当前实现还包含 version=1 rotated/Lambert/stereographic/reduced-Gaussian 网格 |
| 新网格验收 | 合成 native/SQL 回归、真实投影样本的全量坐标/值子比较已有；producer 轴点序、Gaussian 参考及四类完整验收/远程收益仍待补齐 |
| 扫描 | DuckDB 调度的并行扫描；连接级最大线程设置；列裁剪及安全轴/空间选择 |
| 远程 | 匹配引擎版本/平台的官方 httpfs；标准范围读取和可观察长度/版本冲突检查，不承诺新鲜 HEAD 或扫描快照 |
| 缓存 | 自有范围缓存及缓存 SQL 已删除；HTTPFS 内部缓存由官方依赖维护 |
| Polygon 区域读取 | 开发版 `read_om_regions` 默认/points 输出原始区域—逻辑记录；nearest 为区域代表点按非空间轴组合选择一个最近网格源值，并输出逐变量状态/来源；调用者提供 `grid_lonlat` WKB，points 使用平面 `covers`，统计由 SQL 等权聚合；spatial 必须与 DuckDB 匹配并显式加载 |
| 尚未实现 | 后续科学计算算子；004 网格验收仅覆盖下述批准范围 |

## 什么时候用哪个入口

- **读取文件或筛选一块经纬度范围：用 `read_om`。** 例如查看有哪些列、读取温度、按 `WHERE lat BETWEEN ... AND ...` 选矩形范围。它返回原始网格记录。
- **跨多个 chunk 的单变量时间序列：用 `read_om_series`（开发版）。** 提供变量目录前缀、完整 `dimensions`、明确 `domain` 或 `grid` 与闭区间时间窗口；series 自动生成每个对象的时间轴。不要传单个 chunk 文件名或期待它发现多个变量。

- **手里已有行政区、流域等 polygon：用 `read_om_regions`。** 它接收一组带 ID 的区域。省略 `method` 或写 `method := 'points'` 时返回面内及边界的原始网格点；小区域可能零样本。`method := 'nearest'` 为每个非空间轴组合选取区域代表点最近的单个网格源值，代表位置的值不是区域面积均值。两种方式都保留原值列，可按 `region_id` 分组。nearest 需要显式加载与 DuckDB 完全匹配的 spatial 扩展；points 保留原有区域依赖。

最小时间序列示例（本地 fixture 前缀；model 只用于时间默认值，grid 仍显式给出）：

```sql
SELECT value AS temperature_2m, valid_time, lat, lon
FROM read_om_series(
  'build/series-fixtures/data/synthetic/temperature',
  model := 'ecmwf_ifs',
  chunk_time_length := 4, -- 独立覆盖注册的槽数；time_step 继承 1 小时注册值
  dimensions := ['lead', 'point', 'time'],
  grid := {'nx':3,'ny':2,'lat0':10.0,'lon0':100.0,
           'dlat':1.0,'dlon':1.0,'order':'lon_fastest'},
  spatial_axes := ['point'],
  valid_times := [TIMESTAMP '1970-01-01 03:00:00',
                  TIMESTAMP '1970-01-01 05:00:00']
)
ORDER BY valid_time, lat, lon;
```

最小区域读取示例（已有 `regions` WKB 列表及匹配的 `grid`/`domain` 配置）：

```sql
-- 兼容的旧行为：返回多边形覆盖的原始点
SELECT * FROM read_om_regions(path, regions := getvariable('regions'), region_crs := 'grid_lonlat');

-- 每个区域与原非空间轴组合返回一个最近源点
SELECT region_id, value, sampling.source_lon, sampling.source_lat, sampling.distance_m
FROM read_om_regions(path, regions := getvariable('regions'), region_crs := 'grid_lonlat', method := 'nearest');
```

Nearest 是单源代表点抽样；它不计算面积均值，也不启用未实现的 bilinear。

## 参数

```text
read_om(path, dimensions := NULL, grid := NULL,
        spatial_axes := NULL, domain := NULL,
        valid_times := NULL, axes := NULL,
        include_source BOOLEAN := false, variable VARCHAR := <省略>)
```

参数在绑定时确定；`dimensions`、`grid`、`spatial_axes`、`domain`、`valid_times`、`axes` 显式 `NULL` 等同省略。`include_source` 例外：省略时默认 false，显式 `NULL` 会被拒绝。`variable` 省略时绑定全部值数组，显式 NULL 或空串拒绝。

| 参数 | 类型与用途 |
|---|---|
| `path` | 非 NULL 的常量 `VARCHAR`，单个本地文件路径或 HTTP(S)/S3 URI |
| `variable` | 非空 `VARCHAR`；按编码后的 SQL 列名选择一个值数组，也接受前导 `/`；根数组接受 `value` 或 `/`。找不到名称时绑定失败 |
| `dimensions` | `VARCHAR[]` 共享列表，或 `MAP(VARCHAR, VARCHAR[])` 逐变量声明；为全部值变量提供完整有序轴名 |
| `grid` | legacy 七字段规则 STRUCT，或契约定义的封闭 version=1 旋转/投影/Gaussian STRUCT |
| `spatial_axes` | `VARCHAR[]`；指定显式 grid 的空间轴身份 |
| `domain` | `VARCHAR`；显式选择登记网格，名称精确且区分大小写 |
| `valid_times` | `TIMESTAMP[]`；为缺少时间元数据的 time 轴提供 UTC 时间，保留旧接口规则 |
| `axes` | `ANY`；显式映射 time、level、lead_time、member、run 至原始轴 |
| `include_source` | 非 NULL `BOOLEAN`；仅绑定 grid/domain 时在最后追加 `om_source STRUCT`，默认 false |

多变量可使用文件中一致的 `coordinates` 元数据自动对齐。缺少这些元数据时可用 `dimensions := [轴名...]` 声明全部变量共享的完整有序轴；原有 MAP 仍支持，键可用列名或内部绝对路径，并须精确覆盖全部值变量。两种形式复用逐变量 rank、shape、有序轴和元数据冲突校验，不自动广播/转置；相同 shape 本身不能证明轴身份。单数组普通读取可省略轴名，空间查询仍需完整轴身份。

`variable` 同样适用于 `read_om_regions` 和 `om_grid_info`。例如嵌套数组用 `layer_a/value`，节点名称中的 `/` 和 `%` 编码为 `%2F` 和 `%25`，匹配区分大小写，不解码为原始名称。选择后只输出该值列及启用的坐标/语义列；`dimensions` MAP 须精确覆盖这个数组，不能继续列出未选变量。顶层紧凑元数据目录以不超过 1 MiB 的范围合并读取；分散目录回退为普通读取，嵌套选择使用完整遍历再筛选。合并范围可能包含其他变量的元数据；不读取其值载荷。

空间配置可使用登记 domain 或显式 grid；显式 grid 又分 legacy 规则定义和 version=1 定义：

- **显式 grid**：须同时提供 `spatial_axes`。`nx` / `ny` 为正整数；起点、非零步长须有限。`order='separate'` 使用 `[纬度轴名, 经度轴名]`；`lon_fastest` / `lat_fastest` 使用一个展平轴名。
- **登记 domain**：使用固定网格及登记的空间轴身份，不接受额外 `grid` 或 `spatial_axes`。68 个规则 domain 使用 `lat` / `lon`；新增投影/Gaussian domain 按各自登记的轴/点序 profile 绑定，不能一概要求 `lat` / `lon`。检查全部值变量的空间轴长度及规则 domain 文件中存在的 WKT BBOX；不从文件路径自动识别。名称、来源和样本覆盖见 [规则网格 domain](regular-domains.md) 与 [多网格证据表](grid-domains.md)。
- **version=1 grid**：使用精确 `version/type/numeric_policy/earth/layout/parameters` 字段；支持 rotated、Lambert、stereographic 和逐行表 reduced Gaussian。CRS、数值规则、轴布局和每个变量的 shape 均做封闭校验。区域 Gaussian 必须显式给出 local-to-parent segments；BBOX 仅作诊断。逐类型及 domain 的证据等级见 [多网格证据表](grid-domains.md)。

v0.3.1 新增 SEAS5/EC46 的 O320 与 N160 产品 domain，名称及实际轴布局见 [季节预测 domain 指南](seasonal-domains.md)。它们沿用展平 Gaussian 布局，保留所有时间/成员/层次轴；domain 不推定单位或缺失的时间标签。v0.3.0 发布包不包含这次新增注册。查询示例与读取优化从 v0.3.1 起可用。

## 跨 chunk 时间序列：`read_om_series`（开发版）

`read_om` 的 `path` 始终是一个对象。series 接收一个变量目录前缀，例如生产布局的 `.../data/<model>/<variable>`，并只构造时间窗口所需的 `chunk_<signed-index>.om`。前缀可为本地路径或 HTTP/HTTPS/S3 URI；只去掉一个末尾斜线，不列目录、不 glob、不读取 `static/meta.json`，也不搜索窗口之外的对象。输入不能是 `.om` 文件、glob、带 query/fragment/userinfo 的 URI，或 `data_run/`、`data_spatial/` 选择器。

```text
read_om_series(directory_prefix,
  model := VARCHAR,
  chunk_time_length := BIGINT,
  time_step := INTERVAL,
  valid_times := [TIMESTAMP start_utc, TIMESTAMP end_utc],
  -- 或者同时提供 start := TIMESTAMP, "end" := TIMESTAMP
  dimensions := VARCHAR[] | MAP(VARCHAR, VARCHAR[]),
  domain := VARCHAR,
  -- 或者 grid := <read_om grid>, spatial_axes := VARCHAR[]
  strict := BOOLEAN, include_source := BOOLEAN, variable := VARCHAR)
```

`valid_times` 是恰好两个有限 UTC 端点的闭区间，不是 `read_om.valid_times` 的逐槽坐标列表。`TIMESTAMP` 按 UTC 解释，`TIMESTAMPTZ` 转为 UTC；若使用命名端点，必须同时提供 `start` 和保留字引用的 `"end"`，且不能混用两种窗口形式。查询按数学 floor 选择首尾 chunk，再对每个子对象应用精确的 `start <= valid_time AND valid_time <= end`。默认最大 4096 个候选 chunk，含缺失对象；过大窗口应拆分。SQL 输出不保证顺序。

`model` 使用区分大小写的冻结 temporal registry，仅提供 `chunk_time_length` 和 `time_step` 默认值，不选择变量、domain 或 grid。两种显式覆盖独立生效；未知或省略 model 时必须同时提供两个。空间声明仍是必需的：传完整 `domain`，或传 `grid` 并同时给 `spatial_axes`。`dimensions` 须完整描述所选数组的有序轴，且每个所选数组恰好含一个 `time`；系列入口自动生成该轴，因此任何 `axes` 参数都会被拒绝。非时间轴名如 `lead` 仍是原始维度，不自动产生 `lead_time`。

输出沿用 `read_om` 命名：根 Float32 数组列名为 `value`，可在 SQL 投影中写 `value AS variable_name`；之后是 `lat`、`lon`、`valid_time`，可选的 `om_source` 在末尾。所有存在的候选对象必须具有一致的所选数组、完整 shape/轴顺序和空间身份；物理压缩块形状可以不同，不做隐式 cast、广播、重排、连接或缺列填充。结果为 `UNION ALL`，保留重复行与 NULL；没有 `ORDER BY` 时不能依赖对象或时间顺序。

`strict` 默认为 false，只跳过初次打开时被结构化状态确认不存在的本地文件或远程对象（例如 HTTP 404）。403、5xx、无结构状态的 transport 错误、损坏、schema/time metadata 冲突以及成功 bind 后的对象移除/替换都报错。`strict := true` 令任何缺失候选失败，即使外层 `LIMIT` 或过滤器不消费该分支。缺失 chunk 不补 NULL 行；存在 chunk 中的 NaN 转换成 SQL NULL，但仍保留原来的行。`strict := NULL` 无效。

Feature 010 当前源码验收仅在声明范围内 **PASS**：Linux AArch64 (`linux_arm64`)，DuckDB pinned pairs v1.5.4、v1.5.5、v1.5.6、v2.0.0。真实 producer 数据仅覆盖两个相邻 IFS025 `temperature_2m` 对象、一个点和一个边界窗口；其余十个注册模型只有 provenance 与 synthetic runtime equivalence，N160 为合成数据。受控故障证明错误不会被静默跳过；official HTTPFS 下 HTTP(S) 403/412 表现为 unknown-status `[transport]`。`corrupt` fixture 返回 HTTP 200 和 `not-an-om-object`，在 range-protocol 阶段失败，没有验证 OM metadata/decode 损坏。当前证据见 [follow-up 报告](../specs/010-read-om-series/evidence/acceptance-20261106-followup-linux-arm64/report.md)；2026-08-03 和首次 2026-11-06 报告为历史记录。Feature 008 未 PASS；SC-006 仍 relaxed / 不会达到原 ≥20% 门槛，T042/T043 保持未勾选。该开发源码结果不扩展平台或生产网格支持声明。

## 输出与查询语义

根数组输出 `value FLOAT`；层级值列去掉内部路径的前导 `/`，按内部规范路径排序。嵌套列如 `surface/temperature` 需用双引号引用。附属元数据不生成值列。完整命名规则见基础读取契约。

有 grid/domain 时，在值列后追加非 NULL 的 `lat DOUBLE`、`lon DOUBLE`；与原列名称冲突时按 DuckDB 标识符规则（含大小写）拒绝绑定。没有空间配置时只输出值列，`coordinates` 或 `dimensions` 本身不会生成坐标列。这是从 `latitude/longitude` 改名的破坏性开发版变更；不默认附加旧别名，迁移方式及原生轴/JSON 不改名的边界见 [接口迁移](interface-migration.md)。

`read_om_regions(path, regions := STRUCT(id VARCHAR,wkb BLOB)[], region_crs := 'grid_lonlat', ...)` 的默认 `points` 模式使用 spatial 的标准 WKB/geometry 函数，把每个原始点记录匹配到零个或多个区域，并在原列末尾追加 `region_id`；边界采用平面 `covers`，外环和洞边界包含，洞内部排除。nearest 模式为每个区域×原非空间轴位置返回一行：顶层 `lat/lon` 是面内代表目标，`sampling.source_lat/source_lon` 是实际最近网格源；EMPTY、域外、超距与所选值缺测用不同状态表达，COUNT(*)计结果行。`include_source := true` 时，仅成功选定源点的nearest结果或points记录带真实 `om_source`。区域/time/member 等统计由 SQL `GROUP BY` 完成，均值是非 NULL 样本等权平均，不是面积平均。nearest 只接受已绑定且通过资格检查的规则球面网格，需显式加载与 DuckDB 匹配的 spatial；支持范围和 gate 状态见 [006 spec](../specs/006-region-nearest-sampling/spec.md)、[quickstart](../specs/006-region-nearest-sampling/quickstart.md) 与 [N0–N7 evidence](../specs/006-region-nearest-sampling/evidence/)。points兼容约束见 [005 SQL 契约](../specs/005-polygon-region-statistics/contracts/sql-interface.md)。

`include_source := true` 时在最后追加单列 `om_source STRUCT`，含脱敏对象身份、版本强度、grid/layout ID、logical/point/parent point index 和原轴 indices；它不会启用值 decoder。`om_grid_info(path, dimensions := ..., grid := ..., spatial_axes := ..., domain := ..., valid_times := ..., axes := ...)` 复用 metadata binder 并输出一行定义/布局/CRS/能力/provenance，不读取值 LUT/data 或解码值。描述结果证明 metadata 绑定，不证明坐标/值或远程收益已验收。

经度统一为 `[-180,180)`。显式 grid 纬度须在 `[-90,90]`；仅三个登记的 MeteoFrance 海洋 domain 按上游定义保留末行约 90.041664° 的纬度。额外轴参与原数组的逻辑索引。`axes.time` 输出为兼容列名 `valid_time TIMESTAMP`；其他启用项输出为 `level DOUBLE`、`lead_time INTERVAL`、`member BIGINT/VARCHAR` 和 `run TIMESTAMP`，按契约顺序追加。相同坐标值仍对应不同逻辑行。无 `ORDER BY` 时不承诺 SQL 结果顺序。

有限且类型精确相容的常量比较、`BETWEEN` 和安全 `AND` 可缩小空间及语义轴候选范围。DuckDB 始终执行完整 `WHERE`，保证精确结果。混合 `AND` 可使用独立的安全条件；`OR`、函数、转换、非匹配 collation 及无法证明安全的条件保留原 SQL 并回退到更宽候选范围。

跨经线范围使用 `lon >= 170 OR lon < -170`。普通 `BETWEEN 170 AND -170` 返回空，不隐式环绕。

## 读取成本与失败行为

列裁剪保留输出及过滤依赖，重复引用的值变量只解码一次。仅坐标、无值依赖的 `COUNT(*)` 和可证明的空选择不读取值数组的 index/data；仍可能读取元数据及坐标。区域读取节省取决于 OM 块布局，不能只凭返回行数或 `EXPLAIN` 判断。

HTTP(S)/S3 需要匹配 DuckDB 版本及平台的官方 `httpfs`；已安装且允许自动加载时，标准文件系统可按需加载它，无需显式 `LOAD httpfs`。离线准备或禁用自动加载时按 [HTTPFS 指南](official-httpfs.md) 显式安装/加载；DuckOMO 不强制开启自动安装或绕过外部访问限制。本地读取无需 HTTPFS。`s3://openmeteo/` 在无自定义 S3 配置时可直接匿名读取，无需预先创建 secret；DuckOMO 不自动创建/覆盖 secret，已有配置遵循官方 HTTPFS 规则。对象须在扫描期间稳定，每 worker 独立句柄和 decoder。默认 `duckomo_revalidate_metadata=false`，扫描复用绑定阶段的不可变元数据。`SET duckomo_revalidate_metadata=true` 启用全树重读并比较相同绑定视图；显式选变量时比较该变量及共享元数据。此设置增加 I/O，仍不提供对象快照保证。连接级 `duckomo_max_threads` 默认为 0（使用 DuckDB 上限），正数限制 worker。`duckomo_cache_enabled`、`duckomo_cache_capacity` 和 `duckomo_clear_cache()` 已移除，旧 SQL 会报未知设置/函数；不承诺重复查询热缓存收益、强制刷新或任意上游缓存即时撤权。v0.3.1 在单 worker/单变量 decoder 内复用最近一次已成功读取的索引与压缩数据范围：只使用现有两个缓冲区，请求完全被已读范围覆盖时避免再次 I/O；不跨查询、变量或 worker 共享。对象须保持稳定，`duckomo_revalidate_metadata` 仍只检查元数据。逻辑 index/data 请求计入全部解码需求，成功位置读取只计实际 I/O；重复解压次数仍如实累计。

`duckomo_last_scan_metrics()` 返回最近一次已结束、含 `read_om` 的 SQL query 的每个扫描一行，列为 `query_id VARCHAR, scan_id UBIGINT, metrics VARCHAR`。`metrics` 是 v4 JSON，完整兼容快照保存在 `legacy_v3`（包含 `legacy_v2`）。逻辑请求与标准文件接口成功读取分开计数；远程 transport body/attempts/responses 为 `null`、`complete=false`，本地为 0/true；原自有 cache 为 false/0，原因 `removed`。网络发送量由验收侧服务日志独立记录，不当作客户端接收量；coordinate 的 index/data/decode 单独列出，逐变量 map 只记录值数组。`scan_complete=false` 可与成功状态并存，例如下游 `LIMIT` 提前停止。`peak_rss_bytes` 的 scope 为 process。`peak_query_owned_bytes` 是当前接入 DuckOMO memory account 的 owned-capacity 峰值；完整逐组件 bound ledger 与远程控制内存核对仍待完成，因此不得将该字段单独解释为进程或全查询所有内存。它不包括 DuckDB 输出 vector、httpfs/引擎内部内存或 allocator bookkeeping。失败、取消及计数失效时该字段为 JSON `null`，并将 `query_memory_count_complete` 置为 false。读取/清理指标不会覆盖最近扫描；错误和取消会保留已观察成本及终态。

远程打开与范围读取通过公开异常类型及结构化 HTTP status 输出脱敏的类别和行动建议（例如 `[configuration]`、`[object_not_found]`）；无结构化信息时明确回退为 `[transport]`，不解析异常文本猜测原因。类别、凭据保护及引擎可能渲染原 SQL 的边界见 [远程诊断](interface-migration.md#3-远程诊断类别行动建议与脱敏)。

参数、格式、轴、shape、网格和名称冲突在返回行前报告。扫描中的损坏或取消使整个查询失败；释放资源后可继续执行有效查询。

`read_om_raw(path)` 是早期验证入口，仅支持 OM v3 Float32 / FPX 根数组，输出 `value FLOAT`；日常查询使用 `read_om`。

## 验证范围

006 nearest sampling 已在 `read_om_regions` 实现，points 默认兼容。006 已完成声明范围内的 N0–N7 验收：Linux AArch64、DuckDB v1.5.4/v1.5.5/v1.5.6、合成规则球面网格与固定 NCEP GFS025；真实输入仅私有留存、不公开分发，N7由独立用户执行。 [验收报告](../specs/006-region-nearest-sampling/evidence/final.md)。

验收平台为 Linux AArch64，Linux x86_64 验证暂缓。既有 002 保持原始规则网格验收范围。004 四定义/五冻结对象、v1.5.4/v1.5.5/v1.5.6 的 H0–H9 及独立 AI 验收已完成，见[最终报告](../specs/004-multi-grid-selection/evidence/current-20261021/report.md)：45 对 HTTP/验证 TLS 的 HTTPS/签名 S3 全量与选择结果一致，三项成本严格下降，四定义在冻结范围登记 remote-benefit-validated。参考为 pinned Open-Meteo producer，不声明原生 GRIB 等价；在 004 的批准验收范围内，真实 N160/N320/N 区域及 DuckDB 2.0 未列为该项 gate 输入；合成回归保留。H5 不覆盖上游 HTTPFS heap/cache；S3 服务权限转换为专用代理策略，并非上游 IAM 修改。68 个规则 domain 不因注册或这些样本通过而获得全部对象验收。逐定义边界见[网格证据表](grid-domains.md)。

复现步骤见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md)，已记录结果见 [最终验收](../specs/002-spatial-pushdown/evidence/final.md)。未来能力见 [Roadmap](roadmap.md)。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
