> 当前远程读取使用 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)，不再使用专用 ABI、自有 LRU 或 transport observer。003/004 中相关历史条款由此契约替换；复现请见 [官方流程](official-httpfs.md)。

# DuckOMO 接口说明

当前工作树包含本地扫描、维度语义、列裁剪、规则/投影/Gaussian 网格、多区域 polygon 读取、官方 HTTPFS 的 HTTP(S)/S3 范围读取、并行任务、opt-in 源位置、网格描述和 v4 扫描指标实现。v0.3.0 GitHub 包包含实验性 005 区域功能；当前源码的跨平台完整验收仍未完成，已验证范围见 [元数据修复记录](../specs/005-polygon-region-statistics/evidence/metadata-selection-fixes.md)；004 的新网格完整验收也尚未通过。DuckDB 2.0 已退出当前支持范围。[README](../README.md) 提供用法示例；原有空间契约见 [002 SQL 契约](../specs/002-spatial-pushdown/contracts/sql-interface.md)，维度与远程约定见 [003 SQL 契约](../specs/003-dimensions-remote-parallel/contracts/sql-interface.md)，多网格接口见 [004 SQL 契约](../specs/004-multi-grid-selection/contracts/sql-interface.md)，区域接口见 [005 SQL 契约](../specs/005-polygon-region-statistics/contracts/sql-interface.md)。

## 支持范围

| 项目 | 当前行为 |
|---|---|
| 输入 | 单个本地普通文件或 HTTP(S)/S3 URI；不接受目录、glob 或文件列表 |
| 格式 | OM v3；Float32；FPX_XOR2D / PFOR_DELTA2D_INT16 |
| 布局 | 根数组、层级值数组及附属元数据；rank 1–8，不接受零长度轴 |
| 多变量 | shape 和有序轴身份一致；不自动转置、广播或连接 |
| 缺测与精度 | NaN → SQL NULL；保留 ±Inf；值与官方 Float32 解码结果精确一致 |
| 空间坐标 | 显式规则经纬度网格、68 个规则 domain；当前实现还包含 version=1 rotated/Lambert/stereographic/reduced-Gaussian 网格 |
| 新网格验收 | 合成 native/SQL 回归、真实投影样本的全量坐标/值子比较已有；producer 轴点序、Gaussian 参考及四类完整验收/远程收益仍待补齐 |
| 扫描 | DuckDB 调度的并行扫描；连接级最大线程设置；列裁剪及安全轴/空间选择 |
| 远程 | 匹配引擎版本/平台的官方 httpfs；标准范围读取和可观察长度/版本冲突检查，不承诺新鲜 HEAD 或扫描快照 |
| 缓存 | 自有范围缓存及缓存 SQL 已删除；HTTPFS 内部缓存由官方依赖维护 |
| Polygon 区域读取 | 开发版 `read_om_regions` 默认/points 输出原始区域—逻辑记录；nearest 为区域代表点按非空间轴组合选择一个最近网格源值，并输出逐变量状态/来源；调用者提供 `grid_lonlat` WKB，points 使用平面 `covers`，统计由 SQL 等权聚合；spatial 必须与 DuckDB 匹配并显式加载 |
| 尚未实现 | 科学计算算子；新网格已实现，但 004 完整验收和独立复现未完成 |

## 什么时候用哪个入口

- **读取文件或筛选一块经纬度范围：用 `read_om`。** 例如查看有哪些列、读取温度、按 `WHERE lat BETWEEN ... AND ...` 选矩形范围。它返回原始网格记录。
- **手里已有行政区、流域等 polygon：用 `read_om_regions`。** 它接收一组带 ID 的区域。省略 `method` 或写 `method := 'points'` 时返回面内及边界的原始网格点；小区域可能零样本。`method := 'nearest'` 为每个非空间轴组合选取区域代表点最近的单个网格源值，代表位置的值不是区域面积均值。两种方式都保留原值列，可按 `region_id` 分组。nearest 需要显式加载与 DuckDB 完全匹配的 spatial 扩展；points 保留原有区域依赖。

最小切换示例（已有 `regions` WKB 列表及匹配的 `grid`/`domain` 配置）：

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

验收平台为 Linux AArch64，Linux x86_64 支持与验证暂缓。既有 002 空间门禁通过其原始规则网格范围；004 的合成实现回归与有限真实 metadata/值输入记录见 [004 本地 evidence](../specs/004-multi-grid-selection/evidence/baseline-local/)，不能替代四类真实坐标/值和独立 Gaussian 行序参考。**2026-10-10 修订**：按 [负责人批准的 Gaussian 范围修订](../specs/004-multi-grid-selection/contracts/gaussian-acceptance-20261010.md)，未取得匹配公开对象的 N160/N320/N 区域真实验收本轮跳过，不阻塞当前范围收口，定义及合成回归保留且不记为真实通过。按 [2026-10-10 pinned producer 参考决定](../specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)，`ecmwf_ifs` O1280 与三个投影定义登记为 coordinate-value-validated（与 Open-Meteo 发布对象一致，非原生 GRIB 网格等价）；O1280 依据 [真实本地查询记录](../specs/004-multi-grid-selection/evidence/baseline-local/o1280-real-local-20261010/final.md)（官方值全量对照、producer 坐标误差 0、空间/时间筛选），详见 [多网格证据表](grid-domains.md)。完整远程 gate 与 H9 仍开放。官方 HTTPFS 迁移已在 v1.5.4、v1.5.5、v1.5.6 通过本地/HTTP/HTTPS/签名 S3 一致性、受控远程回归、访问变化及固定性能样本的局部收益检查，见 [迁移验证记录](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md)。这些结果不替代 004 每类真实网格的远程收益、完整内存 ledger 和独立复现；完整 H8 仍未通过，2.0 开发版不在本轮范围。缺输入/服务/审计的 gate 保持 not-run。68 个规则 domain 以各自已有 evidence 记录为准，不能因注册而推定所有对象已验证；新网格覆盖见 [多网格证据表](grid-domains.md)。

复现步骤见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md)，已记录结果见 [最终验收](../specs/002-spatial-pushdown/evidence/final.md)。未来能力见 [Roadmap](roadmap.md)。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
