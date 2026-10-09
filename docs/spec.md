> 2026-10-08：当前未发布版本使用 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)。下文专用 ABI、LRU 或 observer 的条款/命令为历史约定，已被本次契约替换。历史证据状态保持，当前复现请见 [官方流程](official-httpfs.md)。

# duckomo 接口说明

当前工作树包含本地扫描、维度语义、列裁剪、规则/投影/Gaussian 网格、HTTP(S)/S3 范围读取、并行任务、连接级范围缓存、opt-in 源位置、网格描述和 v4 扫描指标实现。004 的新网格真实坐标/值、远程收益、完整内存 ledger、DuckDB 2.0 和独立复现门禁尚未通过；代码存在不等于生产支持。[README](../README.md) 提供用法示例；原有空间契约见 [002 SQL 契约](../specs/002-spatial-pushdown/contracts/sql-interface.md)，维度与远程约定见 [003 SQL 契约](../specs/003-dimensions-remote-parallel/contracts/sql-interface.md)，新网格接口见 [004 SQL 契约](../specs/004-multi-grid-selection/contracts/sql-interface.md)。

## 支持范围

| 项目 | 当前行为 |
|---|---|
| 输入 | 单个本地普通文件或 HTTP(S)/S3 URI；不接受目录、glob 或文件列表 |
| 格式 | OM v3；Float32；FPX_XOR2D / PFOR_DELTA2D_INT16 |
| 布局 | 根数组、层级值数组及附属元数据；rank 1–8，不接受零长度轴 |
| 多变量 | shape 和有序轴身份一致；不自动转置、广播或连接 |
| 缺测与精度 | NaN → SQL NULL；保留 ±Inf；值与官方 Float32 解码结果精确一致 |
| 空间坐标 | 显式规则经纬度网格、68 个规则 domain；当前实现还包含 version=1 rotated/Lambert/stereographic/reduced-Gaussian 网格 |
| 新网格验收 | 合成 native/SQL 回归及有限真实 OM v3 metadata 记录；四类完整坐标/值和远程收益仍未验收 |
| 扫描 | DuckDB 调度的并行扫描；连接级最大线程设置；列裁剪及安全轴/空间选择 |
| 远程 | 配套固定版本的 httpfs 扩展；严格 HEAD、范围及版本验证 |
| 缓存 | 连接级有界范围缓存；只对强 ETag 或 S3 VersionId 的对象跨查询复用 |
| 尚未实现 | 投影/Gaussian 网格、科学计算算子；G3–G7 尚未全部验收 |

## 参数

```text
read_om(path, dimensions := NULL, grid := NULL,
        spatial_axes := NULL, domain := NULL,
        valid_times := NULL, axes := NULL,
        include_source BOOLEAN := false)
```

参数在绑定时确定；可选参数显式 `NULL` 等同省略。

| 参数 | 类型与用途 |
|---|---|
| `path` | 非 NULL 的常量 `VARCHAR` 本地文件路径 |
| `dimensions` | `MAP(VARCHAR, VARCHAR[])`；为每个值变量声明完整有序轴名 |
| `grid` | legacy 七字段规则 STRUCT，或契约定义的封闭 version=1 规则/投影/Gaussian STRUCT |
| `spatial_axes` | `VARCHAR[]`；指定显式 grid 的空间轴身份 |
| `domain` | `VARCHAR`；显式选择登记网格，名称精确且区分大小写 |
| `valid_times` | `TIMESTAMP[]`；为缺少时间元数据的 time 轴提供 UTC 时间，保留旧接口规则 |
| `axes` | `ANY`；显式映射 time、level、lead_time、member、run 至原始轴 |
| `include_source` | 非 NULL `BOOLEAN`；仅绑定 grid/domain 时在最后追加 `om_source STRUCT`，默认 false |

多变量可使用文件中一致的 `coordinates` 元数据自动对齐。缺少这些元数据时必须提供完整 `dimensions`，键可用列名或内部绝对路径。声明不得与已有轴元数据冲突；相同 shape 本身不能证明轴身份。单数组普通读取可省略轴名，空间查询仍需完整轴身份。

空间配置有两种方式：

- **显式 grid**：须同时提供 `spatial_axes`。`nx` / `ny` 为正整数；起点、非零步长须有限。`order='separate'` 使用 `[纬度轴名, 经度轴名]`；`lon_fastest` / `lat_fastest` 使用一个展平轴名。
- **登记 domain**：使用固定网格和 `lat` / `lon` 轴身份，不接受额外 `grid` 或 `spatial_axes`。检查全部值变量的空间轴长度及文件中存在的 WKT BBOX；不从文件路径自动识别。名称、来源和样本覆盖见 [规则网格 domain](regular-domains.md)。
- **version=1 grid**：使用精确 `version/type/numeric_policy/earth/layout/parameters` 字段；支持 rotated、Lambert、stereographic 和逐行表 reduced Gaussian。CRS、数值规则、轴布局和每个变量的 shape 均做封闭校验。区域 Gaussian 必须显式给出 local-to-parent segments；BBOX 仅作诊断。逐类型及 domain 的证据等级见 [多网格证据表](grid-domains.md)。

## 输出与查询语义

根数组输出 `value FLOAT`；层级值列去掉内部路径的前导 `/`，按内部规范路径排序。嵌套列如 `surface/temperature` 需用双引号引用。附属元数据不生成值列。完整命名规则见基础读取契约。

有 grid/domain 时，在值列后追加非 NULL 的 `latitude DOUBLE`、`longitude DOUBLE`；与原列名称冲突时拒绝绑定。没有空间配置时只输出值列，`coordinates` 或 `dimensions` 本身不会生成坐标列。

`include_source := true` 时在最后追加单列 `om_source STRUCT`，含脱敏对象身份、版本强度、grid/layout ID、logical/point/parent point index 和原轴 indices；它不会启用值 decoder。`om_grid_info(path, dimensions := ..., grid := ..., spatial_axes := ..., domain := ..., valid_times := ..., axes := ...)` 复用 metadata binder 并输出一行定义/布局/CRS/能力/provenance，不读取值 LUT/data 或解码值。描述结果证明 metadata 绑定，不证明坐标/值或远程收益已验收。

经度统一为 `[-180,180)`。显式 grid 纬度须在 `[-90,90]`；仅三个登记的 MeteoFrance 海洋 domain 按上游定义保留末行约 90.041664° 的纬度。额外轴参与原数组的逻辑索引。`axes.time` 输出为兼容列名 `valid_time TIMESTAMP`；其他启用项输出为 `level DOUBLE`、`lead_time INTERVAL`、`member BIGINT/VARCHAR` 和 `run TIMESTAMP`，按契约顺序追加。相同坐标值仍对应不同逻辑行。无 `ORDER BY` 时不承诺 SQL 结果顺序。

有限且类型精确相容的常量比较、`BETWEEN` 和安全 `AND` 可缩小空间及语义轴候选范围。DuckDB 始终执行完整 `WHERE`，保证精确结果。混合 `AND` 可使用独立的安全条件；`OR`、函数、转换、非匹配 collation 及无法证明安全的条件保留原 SQL 并回退到更宽候选范围。

跨经线范围使用 `longitude >= 170 OR longitude < -170`。普通 `BETWEEN 170 AND -170` 返回空，不隐式环绕。

## 读取成本与失败行为

列裁剪保留输出及过滤依赖，重复引用的值变量只解码一次。仅坐标、无值依赖的 `COUNT(*)` 和可证明的空选择不读取值数组的 index/data；仍可能读取元数据及坐标。区域读取节省取决于 OM 块布局，不能只凭返回行数或 `EXPLAIN` 判断。

HTTP(S)/S3 需要先 `LOAD` 同次构建的配套 httpfs，再加载 duckomo。连接级 `duckomo_max_threads` 默认为 0（使用 DuckDB 上限），正数限制 worker；`duckomo_cache_enabled` 默认 true，`duckomo_cache_capacity` 默认 67,108,864 字节，0 表示不存储。`duckomo_clear_cache()` 仅清理当前连接。

`duckomo_last_scan_metrics()` 返回最近一次已结束、含 `read_om` 的 SQL query 的每个扫描一行，列为 `query_id VARCHAR, scan_id UBIGINT, metrics VARCHAR`。`metrics` 是 v4 JSON，完整兼容快照保存在 `legacy_v3`（包含 `legacy_v2`）。逻辑请求、缓存以下成功读取和远端 response body 分开计数；coordinate 的 index/data/decode 单独列出，逐变量 map 只记录值数组。`scan_complete=false` 可与成功状态并存，例如下游 `LIMIT` 提前停止。`peak_rss_bytes` 的 scope 为 process。`peak_query_owned_bytes` 是当前接入 DuckOMO memory account 的 owned-capacity 峰值；完整逐组件 bound ledger 与远程控制内存核对仍待完成，因此不得将该字段单独解释为进程或全查询所有内存。它不包括共享范围缓存、DuckDB 输出 vector、httpfs/引擎内部内存或 allocator bookkeeping。失败、取消及计数失效时该字段为 JSON `null`，并将 `query_memory_count_complete` 置为 false。读取/清理指标不会覆盖最近扫描；错误和取消会保留已观察成本及终态。

参数、格式、轴、shape、网格和名称冲突在返回行前报告。扫描中的损坏或取消使整个查询失败；释放资源后可继续执行有效查询。

`read_om_raw(path)` 是早期验证入口，仅支持 OM v3 Float32 / FPX 根数组，输出 `value FLOAT`；日常查询使用 `read_om`。

## 验证范围

验收平台为 Linux AArch64，Linux x86_64 支持与验证暂缓。既有 002 空间门禁通过其原始规则网格范围；004 的合成实现回归与有限真实 metadata/值输入记录见 [004 本地 evidence](../specs/004-multi-grid-selection/evidence/baseline-local/)，不能替代四类真实坐标/值和独立 Gaussian 行序参考。公开 Open-Meteo bucket 中存在真实 ECMWF HRES O1280 Gaussian OM v3 对象，但 O1280 仅作补充，不替代本期冻结的 N160/N320/N 区域覆盖。远程签名 S3 收益、服务端对账、完整内存 ledger、版本矩阵和独立复现尚未闭环；缺输入/服务/审计的 gate 保持 not-run。68 个规则 domain 以各自已有 evidence 记录为准，不能因注册而推定所有对象已验证；新网格覆盖见 [多网格证据表](grid-domains.md)。

复现步骤见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md)，已记录结果见 [最终验收](../specs/002-spatial-pushdown/evidence/final.md)。未来能力见 [Roadmap](roadmap.md)。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
