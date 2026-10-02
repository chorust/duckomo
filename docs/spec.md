# duckomo 接口说明

当前实现覆盖本地扫描、维度语义、列裁剪、规则网格空间查询、HTTP(S)/S3 范围读取、并行任务、连接级范围缓存和 v3 扫描指标。[README](../README.md) 提供最小示例；原有空间契约见 [002 SQL 契约](../specs/002-spatial-pushdown/contracts/sql-interface.md)，本期完整参数和错误约定见 [003 SQL 契约](../specs/003-dimensions-remote-parallel/contracts/sql-interface.md)。G3–G7 外部验收及独立复现尚未全部执行，不能据本文宣称这些 gate 已通过。

## 支持范围

| 项目 | 当前行为 |
|---|---|
| 输入 | 单个本地普通文件或 HTTP(S)/S3 URI；不接受目录、glob 或文件列表 |
| 格式 | OM v3；Float32；FPX_XOR2D / PFOR_DELTA2D_INT16 |
| 布局 | 根数组、层级值数组及附属元数据；rank 1–8，不接受零长度轴 |
| 多变量 | shape 和有序轴身份一致；不自动转置、广播或连接 |
| 缺测与精度 | NaN → SQL NULL；保留 ±Inf；值与官方 Float32 解码结果精确一致 |
| 空间坐标 | 显式规则经纬度网格或 68 个登记 domain |
| 扫描 | DuckDB 调度的并行扫描；连接级最大线程设置；列裁剪及安全轴/空间选择 |
| 远程 | 配套固定版本的 httpfs 扩展；严格 HEAD、范围及版本验证 |
| 缓存 | 连接级有界范围缓存；只对强 ETag 或 S3 VersionId 的对象跨查询复用 |
| 尚未实现 | 投影/Gaussian 网格、科学计算算子；G3–G7 尚未全部验收 |

## 参数

```text
read_om(path, dimensions := NULL, grid := NULL,
        spatial_axes := NULL, domain := NULL,
        valid_times := NULL, axes := NULL)
```

参数在绑定时确定；可选参数显式 `NULL` 等同省略。

| 参数 | 类型与用途 |
|---|---|
| `path` | 非 NULL 的常量 `VARCHAR` 本地文件路径 |
| `dimensions` | `MAP(VARCHAR, VARCHAR[])`；为每个值变量声明完整有序轴名 |
| `grid` | 包含且仅包含 `nx, ny, lat0, lon0, dlat, dlon, order` 的 STRUCT |
| `spatial_axes` | `VARCHAR[]`；指定显式 grid 的空间轴身份 |
| `domain` | `VARCHAR`；显式选择登记网格，名称精确且区分大小写 |
| `valid_times` | `TIMESTAMP[]`；为缺少时间元数据的 time 轴提供 UTC 时间，保留旧接口规则 |
| `axes` | `ANY`；显式映射 time、level、lead_time、member、run 至原始轴 |

多变量可使用文件中一致的 `coordinates` 元数据自动对齐。缺少这些元数据时必须提供完整 `dimensions`，键可用列名或内部绝对路径。声明不得与已有轴元数据冲突；相同 shape 本身不能证明轴身份。单数组普通读取可省略轴名，空间查询仍需完整轴身份。

空间配置有两种方式：

- **显式 grid**：须同时提供 `spatial_axes`。`nx` / `ny` 为正整数；起点、非零步长须有限。`order='separate'` 使用 `[纬度轴名, 经度轴名]`；`lon_fastest` / `lat_fastest` 使用一个展平轴名。
- **登记 domain**：使用固定网格和 `lat` / `lon` 轴身份，不接受额外 `grid` 或 `spatial_axes`。检查全部值变量的空间轴长度及文件中存在的 WKT BBOX；不从文件路径自动识别。名称、来源和样本覆盖见 [规则网格 domain](regular-domains.md)。

## 输出与查询语义

根数组输出 `value FLOAT`；层级值列去掉内部路径的前导 `/`，按内部规范路径排序。嵌套列如 `surface/temperature` 需用双引号引用。附属元数据不生成值列。完整命名规则见基础读取契约。

有 grid/domain 时，在值列后追加非 NULL 的 `latitude DOUBLE`、`longitude DOUBLE`；与原列名称冲突时拒绝绑定。没有空间配置时只输出值列，`coordinates` 或 `dimensions` 本身不会生成坐标列。

经度统一为 `[-180,180)`。显式 grid 纬度须在 `[-90,90]`；仅三个登记的 MeteoFrance 海洋 domain 按上游定义保留末行约 90.041664° 的纬度。额外轴参与原数组的逻辑索引。`axes.time` 输出为兼容列名 `valid_time TIMESTAMP`；其他启用项输出为 `level DOUBLE`、`lead_time INTERVAL`、`member BIGINT/VARCHAR` 和 `run TIMESTAMP`，按契约顺序追加。相同坐标值仍对应不同逻辑行。无 `ORDER BY` 时不承诺 SQL 结果顺序。

有限且类型精确相容的常量比较、`BETWEEN` 和安全 `AND` 可缩小空间及语义轴候选范围。DuckDB 始终执行完整 `WHERE`，保证精确结果。混合 `AND` 可使用独立的安全条件；`OR`、函数、转换、非匹配 collation 及无法证明安全的条件保留原 SQL 并回退到更宽候选范围。

跨经线范围使用 `longitude >= 170 OR longitude < -170`。普通 `BETWEEN 170 AND -170` 返回空，不隐式环绕。

## 读取成本与失败行为

列裁剪保留输出及过滤依赖，重复引用的值变量只解码一次。仅坐标、无值依赖的 `COUNT(*)` 和可证明的空选择不读取值数组的 index/data；仍可能读取元数据及坐标。区域读取节省取决于 OM 块布局，不能只凭返回行数或 `EXPLAIN` 判断。

HTTP(S)/S3 需要先 `LOAD` 同次构建的配套 httpfs，再加载 duckomo。连接级 `duckomo_max_threads` 默认为 0（使用 DuckDB 上限），正数限制 worker；`duckomo_cache_enabled` 默认 true，`duckomo_cache_capacity` 默认 67,108,864 字节，0 表示不存储。`duckomo_clear_cache()` 仅清理当前连接。

`duckomo_last_scan_metrics()` 返回最近一次已结束、含 `read_om` 的 SQL query 的每个扫描一行，列为 `query_id VARCHAR, scan_id UBIGINT, metrics VARCHAR`。`metrics` 是 v3 JSON，含 `legacy_v2` 以保留既有 evidence 字段含义。逻辑请求、缓存以下成功读取和远端 response body 分开计数；coordinate 的 index/data/decode 单独列出，逐变量 map 只记录值数组。`scan_complete=false` 可与成功状态并存，例如下游 `LIMIT` 提前停止。`peak_rss_bytes` 的 scope 为 process。正常结束时，`peak_query_owned_bytes` 统计 DuckOMO 元数据、decoder 和选择缓冲的 vector capacity 峰值，`query_memory_scope` 标为 `duckomo_owned_buffer_decoder_selection_capacities`；它不包括共享范围缓存、DuckDB 输出 vector、httpfs/引擎内部内存或 allocator bookkeeping。失败、取消及计数失效时该字段为 JSON `null`，并将 `query_memory_count_complete` 置为 false。读取/清理指标不会覆盖最近扫描；错误和取消会保留已观察成本及终态。

参数、格式、轴、shape、网格和名称冲突在返回行前报告。扫描中的损坏或取消使整个查询失败；释放资源后可继续执行有效查询。

`read_om_raw(path)` 是早期验证入口，仅支持 OM v3 Float32 / FPX 根数组，输出 `value FLOAT`；日常查询使用 `read_om`。

## 验证范围

验收平台为 Linux AArch64，Linux x86_64 支持与验证暂缓。既有空间门禁覆盖合成样本、真实 `ncep_gfswave025` 全域坐标与 15 个官方值参考、读取指标和独立复现。003 的外部 HTTP/S3、性能、缓存审计和 G7 独立复现仍需按 [003 Quickstart](../specs/003-dimensions-remote-parallel/quickstart.md) 完成；缺少固定样本、服务和审计记录的 gate 不算通过。后续扩展的 68 个 domain 以元数据样本核对为主，不能据此声明所有对象均可读；详情见 [domain 样本覆盖](regular-domains.md)。

复现步骤见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md)，已记录结果见 [最终验收](../specs/002-spatial-pushdown/evidence/final.md)。未来能力见 [Roadmap](roadmap.md)。
