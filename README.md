# duckomo

中文 | [English](README.en.md)

duckomo 是一个 DuckDB C++ 扩展，将本地 [Open-Meteo OM](https://github.com/open-meteo/om-file-format) 文件中的 Float32 数组直接作为 SQL 表查询，无需先转换格式。

- **读取**：支持 OM v3、FPX_XOR2D / PFOR_DELTA2D_INT16 压缩、根数组和层级变量；NaN 转为 SQL `NULL`。
- **按需读取**：只读取输出和过滤需要的值变量；安全的经纬度条件可进一步缩小读取范围。
- **坐标**：可用显式规则网格、[68 个规则 domain](docs/regular-domains.md)，或 version=1 的旋转、Lambert、stereographic 与 reduced Gaussian 定义生成 `latitude`、`longitude`。

读取器支持单个本地 OM 文件，以及通过官方 `httpfs` 扩展进行 HTTP(S)/S3 范围读取。扫描可由 DuckDB 并行调度，并提供连接级线程上限。支持 time、level、lead_time、member、run 语义轴。新投影/Gaussian 网格、`om_source` 与 `om_grid_info` 已进入实现，但 004 的真实网格坐标/值、远程收益、完整内存账和独立复现门禁尚未通过；当前不据此声明生产支持。已固定的 HRES O1280 样本是 Gaussian 补充证据，不能替代 N160、N320 或 N320 区域样本。验收平台为 Linux AArch64，Linux x86_64 支持与验证暂缓；本轮版本范围为 DuckDB v1.5.4、v1.5.5、v1.5.6。

## 社区安装（待收录）

DuckOMO 正在准备提交到 DuckDB Community Extensions，当前尚未收录。首发目标为 **DuckDB v1.5.6 / Linux AArch64（glibc）**。社区收录并发布后，可在普通 DuckDB 中执行：

```sql
INSTALL duckomo FROM community;
LOAD duckomo;
-- HTTP(S)/S3 读取还需官方 HTTPFS：
INSTALL httpfs;
LOAD httpfs;
```

社区负责从源码编译、签名和托管；加载其签名产物无需 `-unsigned`。源码仓库不保存扩展 `.gz`，提交登记也不要求先上传 GitHub Release。[登记草案、CI 与发布步骤](docs/community-extensions.md)。

## 构建与加载

需要 C11 / C++17 编译器、CMake、Make、Git 和 DuckDB 的构建依赖。在仓库根目录执行：

```sh
git submodule update --init --recursive
make release
./build/release/duckdb -unsigned :memory:
```

`make release` 构建本地 DuckDB、DuckOMO 和开发验证工具。读取本地文件只需加载 duckomo；远程读取使用 `INSTALL httpfs; LOAD httpfs;` 安装并加载对应 DuckDB 版本及平台的官方扩展。DuckOMO 尚未签名时以 `duckdb -unsigned` 启动。

在 CLI 中加载扩展并查询仓库样本：

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

DESCRIBE SELECT * FROM read_om('test/data/raw.om');
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
```

输出为 `value FLOAT` 列，值为 `0` 到 `5`。以上路径均相对于仓库根目录。

开发用 DuckDB 子模块仍固定 **v1.5.4**；社区目标为 **v1.5.6**，社区 CI 会切换到目标引擎版本。Makefile 不再覆盖实际引擎的版本标记。固定版本矩阵支持 v1.5.4、v1.5.5、v1.5.6，默认 `make matrix-release` 构建 v1.5.6：

```sh
./scripts/build-version.sh v1.5.6
python3 scripts/version_matrix.py fetch-runtime --root . \
  --matrix test/data/grids/version-matrix.json --pair v1.5.6
./build/official-matrix/v1.5.6/official/duckdb -unsigned :memory:
```

`fetch-runtime` 获取匹配的官方 CLI/HTTPFS 包。随后加载 `build/official-matrix/v1.5.6/release/extension/duckomo/duckomo.duckdb_extension`。各版本分别编译，源码和产物保存在忽略的 `build/official-matrix/<版本>/`。本地产物未签名，开发加载需 `-unsigned`；三版本验证与社区签名发布的状态分别记录。

## `data/`、`data_run/` 与 `data_spatial/` 的读取

三个目录的本地 OM 文件都使用 `read_om()`，不需要指定目录对应的读取模式；读取器根据文件内部元数据解析。HTTP(S)/S3 URI 也可直接传给 `read_om()`，但不改变单对象读取范围。

| 目录 | 已审计样本的常见轴与元数据 | 查询经纬度时的参数 |
|---|---|---|
| `data_spatial/` | 通常为 `[lat, lon]`，多数带完整 `coordinates` | 通常只需 `domain`；缺少轴元数据时补充 `dimensions` |
| `data_run/` | 通常为 `[lat, lon, time]`，多数带完整 `coordinates` | 通常只需 `domain`；缺少轴元数据时补充 `dimensions` |
| `data/` | 可绑定样本缺少 `coordinates` | `domain` + 覆盖每个值变量的完整 `dimensions` |

只读取值时可省略网格参数；多个变量缺少一致有序轴元数据时仍需 `dimensions`。生成经纬度须显式提供 `domain`，或 `grid` + `spatial_axes`；不会从目录、文件名或 shape 推断网格。轴顺序须以具体文件为准，目录名不能代替轴声明。同一经纬度的不同时间位置保留为多行，有时间坐标时追加 `valid_time` 列。

具体 SQL、缺失轴元数据的声明方法、旧版 OM 限制和各 domain 样本覆盖见 [Open-Meteo 规则网格与目录差异](docs/regular-domains.md)。

## 有效时间查询

`data_run` 的 Int64 `time` 坐标数组和 `data_spatial` 的 Int64 标量 `valid_time` 按 UTC Unix 秒解析，自动追加 `valid_time TIMESTAMP`。数组逐位置映射到声明的 `time` 轴，保留实际时间间隔；标量用于整个空间快照。时间列不依赖 `domain`，也可单独查询：

```sql
SELECT value, latitude, longitude, valid_time
FROM read_om('build/s3-samples/data_run/ncep_gfs025/2026/09/28/0000Z/cloud_cover_50hPa.om',
  domain := 'ncep_gfs025')
WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120
  AND valid_time = TIMESTAMP '2026-09-28 03:00:00';
```

该路径可以使用本地文件或受支持的远程 URI。`TIMESTAMP` 列按 UTC 解释，不随会话时区转换。时间条件会缩小安全候选位置，DuckDB 仍执行完整 `WHERE`。

缺少时间元数据时，可通过 `valid_times := [TIMESTAMP '...', ...]` 显式提供每个时间位置的 UTC 时间；长度必须匹配 `time` 轴，缺少轴元数据时还需 `dimensions`。无 `time` 轴的空间快照可传一个时间。列表必须非空、无 NULL 且时间有限；不能覆盖文件已有的不同时间坐标。既无时间元数据也未提供 `valid_times` 的文件保持原有输出，不从路径或起报时间猜测有效时间。

其他语义轴用 `axes` 显式声明；它不替代现有 `valid_time` 输出名。`time` 仍输出为 `valid_time TIMESTAMP`，`run` 输出 `run TIMESTAMP`，其余轴分别输出 `level DOUBLE`、`lead_time INTERVAL` 和保留原类型的 `member`：

```sql
SELECT value, valid_time, member
FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['time_axis','ensemble']]),
  axes := {
    'time': {'axis':'time_axis', 'start':TIMESTAMP '2026-09-30 00:00:00',
             'step':INTERVAL '1 hour'},
    'member': {'axis':'ensemble', 'values':[10,20,30]}
  })
WHERE valid_time=TIMESTAMP '2026-09-30 01:00:00' AND member=20;
```

time/run 使用 UTC 微秒时间；lead_time 不接受月分量；level 必须给出封闭的 `kind` / `unit`；member 整数保留为 `BIGINT`，文本不转数字。显式坐标按逻辑位置保留，不会对重复值去重。完整类型、冲突和过滤回退规则见 [接口说明](docs/spec.md) 与 [SQL 契约](specs/003-dimensions-remote-parallel/contracts/sql-interface.md)。

## 多变量查询

同形状且带一致有序 `coordinates` 元数据的变量可自动对齐。否则，须通过 `dimensions` 为**每个值变量**声明完整且一致的有序轴名：

```sql
SELECT temperature
FROM read_om('test/data/multi.om',
  dimensions := map(
    ['humidity', 'temperature'],
    [['row', 'column'], ['row', 'column']]
  ))
WHERE humidity >= 103
ORDER BY temperature;
```

结果为 `3`、`4`、`5`。虽然 `humidity` 不在输出中，它仍是过滤所需的变量。`dimensions` 用于校验对齐，不生成 `row` / `column` 列，也不能覆盖文件已有的不同轴声明。嵌套列名如 `surface/temperature` 需用双引号引用。

## 经纬度查询

提供完整网格和空间轴身份后，值列后会追加 `latitude DOUBLE`、`longitude DOUBLE`。以下为 `raw.om` 显式赋予一个 3×2 演示网格：

```sql
SELECT value, latitude, longitude
FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['lat', 'lon']]),
  grid := {'nx':3, 'ny':2, 'lat0':10.0, 'lon0':100.0,
           'dlat':1.0, 'dlon':2.0, 'order':'separate'},
  spatial_axes := ['lat', 'lon'])
WHERE latitude >= 11 AND longitude < 104
ORDER BY latitude, longitude;
```

返回 `(3, 11, 100)`、`(4, 11, 102)`。`lat0` / `lon0` 是起点，`dlat` / `dlon` 是步长；展平空间轴也可使用 `lon_fastest` 或 `lat_fastest` 布局，见 [空间查询指南](specs/002-spatial-pushdown/quickstart.md)。

对于已下载到本地的 Open-Meteo 文件，可指定登记的网格：

```sql
SELECT wave_height, latitude, longitude
FROM read_om('/path/to/local-gfswave.om', domain := 'ncep_gfswave025')
WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120;
```

此例要求文件符合该 domain 的网格定义，并含 `wave_height` 变量。`domain` 名称是 AWS 对象键中 `data/`、`data_run/` 或 `data_spatial/` 后的 prefix；必须显式指定，不从路径或 shape 推断。

缺少 `coordinates` 时仍需完整 `dimensions`；`coordinates = 'lat lon'` 本身也不足以定义地理网格。名称、轴、shape 和文件中存在的 WKT BBOX 会在返回数据前校验。样本下载、目录差异和兼容范围见 [规则网格 domain](docs/regular-domains.md)。

## 投影与 Gaussian 网格（实现中）

version=1 `grid` 可声明旋转经纬度、球面 Lambert、stereographic 或 reduced Gaussian 网格。声明使用封闭字段集，必须匹配数组的空间轴顺序和长度；未知或冲突的 CRS 会拒绝绑定。Gaussian 必须给出完整逐行表，区域网格还要给出局部到 parent 的行段。参数和示例见 [多网格接口契约](specs/004-multi-grid-selection/contracts/sql-interface.md)。

```sql
SELECT value, latitude, longitude, om_source.logical_index
FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['y','x']]),
  grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1',
           'earth':{'model':'sphere','radius_m':6371229.0},
           'layout':{'nx':3,'ny':2,'order':'separate'},
           'parameters':{'x0':0.0,'y0':0.0,'dx':1.0,'dy':1.0,
                         'north_pole_latitude':39.25,'north_pole_longitude':-162.0,
                         'rotation':0.0}},
  spatial_axes := ['y','x'], include_source := true)
ORDER BY om_source.logical_index;
```

`om_source` 是 opt-in 的最后一列，保留对象、网格/布局和原数组位置身份。`om_grid_info(path, ...同一组网格参数...)` 返回一行定义、轴/stride、CRS、能力和 provenance 描述，不读取值数组。当前逐 domain 证据等级和 O1280 的边界见 [多网格证据表](docs/grid-domains.md)。代码和合成回归已存在，但上面的演示不构成真实生产网格验收。

经度统一为 `[-180,180)`。有限常量的 `=`, `<`, `<=`, `>`, `>=`, `BETWEEN` 和安全的 `AND` 可缩小扫描；完整 `WHERE` 始终由 DuckDB 执行。跨经线区域写为：

```sql
WHERE longitude >= 170 OR longitude < -170
```

`OR` 及无法安全分析的表达式保留 SQL 过滤，可能读取全域。实际读取节省取决于 OM 块布局；仅坐标、纯空间 `COUNT(*)` 和可证明的空选择不读取值数组。

## 远程、并行与缓存

HTTP(S) 与 S3 使用官方 HTTPFS。对象需在扫描期间保持稳定且支持范围读取；标准文件接口比较可观察长度/版本，不承诺强制新鲜 HEAD 或扫描快照。S3 凭据通过 DuckDB secret/HTTPFS 配置提供。远程 SQL 示例：

```sql
INSTALL httpfs;
LOAD httpfs;
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

SELECT value
FROM read_om('https://example.invalid/path/object.om')
LIMIT 10;
```

S3 读取前应配置 secret，例如 `CREATE SECRET ... (TYPE s3, KEY_ID ..., SECRET ..., REGION ...)`。强 ETag 或 S3 VersionId 才允许跨查询缓存；每次查询仍会重新探测对象身份和范围权限。弱/无版本对象可以读取，但不会跨查询缓存。`read_om_raw(path)` 仍只支持本地文件。

线程上限和应用范围缓存按连接设置：

```sql
SET threads=4;
SET duckomo_max_threads=2;        -- 0 使用 DuckDB 线程上限；正数限制工作者
```

缓存容量以字节计，容量缩小时立即淘汰；设置为 0 不存储。用 `duckomo_last_scan_metrics()` 查看最近一次已结束 SQL 中每个扫描的 v4 JSON：

```sql
SELECT query_id, scan_id, metrics::JSON
FROM duckomo_last_scan_metrics();
```

指标分开记录逻辑请求与文件接口成功读取；远程 transport body/attempts/responses 为 `NULL`、`complete=false`，本地为 0/true。自有 cache 字段为 false/0，原 cache SQL 项已移除。网络发送量由验收侧服务日志独立记录；时间坐标数组的 index/data/decode 与值变量分开统计。`scan_complete=false` 表示查询成功结束但扫描被 LIMIT 等消费者提前停止。`peak_rss_bytes` 的范围是进程；`peak_query_owned_bytes` 记录当前接入账本的 DuckOMO-owned vector 容量峰值，不包括DuckDB 输出向量或 httpfs/引擎内部内存。完整逐项内存 ledger 和远程控制内存审计仍在进行。失败、取消或计数失效时峰值为 `null`，`query_memory_count_complete=false`。v4 字段与边界见 [接口说明](docs/spec.md)。

## 开发与验证

```sh
make test                            # 构建 release 并运行本地验证
./scripts/validate.sh build/release --local-only  # 验证已有构建，不需要远程服务
make sanitizer-test                  # ASan/UBSan 检查
```

`validate.sh` 还需 `jq`、`sha256sum`、`diff`、`mktemp`，证据默认写入 `build/evidence/`。设置 `DUCKOMO_DOMAIN_FILE=/path/to/pinned.om` 可额外运行真实样本的完整空间验证。外部远程服务就绪后，按 [003 Quickstart](specs/003-dimensions-remote-parallel/quickstart.md) 设置环境变量并运行 G3；缺少服务、真实样本或审计日志时远程 gate 不通过。

## 文档与源码

| 内容 | 入口 |
|---|---|
| 参数、输出、支持范围 | [接口说明](docs/spec.md) · [完整 SQL 契约](specs/002-spatial-pushdown/contracts/sql-interface.md) |
| 网格定义与真实样本覆盖 | [规则网格 domain](docs/regular-domains.md) |
| 投影/Gaussian 定义与逐项证据等级 | [多网格证据表](docs/grid-domains.md) |
| 扫描流程与读取指标 | [技术架构](docs/architecture.md) |
| 后续能力 | [Roadmap](docs/roadmap.md) |
| 查询绑定与扫描 / 网格 / 本地 OM 读取 | `src/scan/` / `src/grid/` / `src/om/` |
| SQL 用例 / 原生检查 / 样本 | `test/sql/` / `test/native/` / `test/data/` |

`read_om_raw` 保留为仅支持 FPX 根数组的早期验证入口；日常查询使用 `read_om`。

## 官方版本构建与验证

```bash
scripts/build-version.sh v1.5.4  # 同样支持 v1.5.5、v1.5.6
python3 scripts/version_matrix.py fetch-runtime --root . --matrix test/data/grids/version-matrix.json --pair v1.5.4
scripts/validate.sh --matrix test/data/grids/version-matrix.json --pair v1.5.4 --local-only
```

远程完整验证还需受控 HTTP/HTTPS/签名 S3 服务。配置、命令和证据见 [官方 HTTPFS 复现说明](docs/official-httpfs.md)。原 `duckomo_cache_enabled`、`duckomo_cache_capacity`、`duckomo_clear_cache()` 已删除，请移除对应 SQL；上游缓存不提供原 LRU 的容量/撤权语义。
