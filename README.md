# duckomo

中文 | [English](README.en.md)

duckomo 是一个 DuckDB C++ 扩展，将本地 [Open-Meteo OM](https://github.com/open-meteo/om-file-format) 文件中的 Float32 数组直接作为 SQL 表查询，无需先转换格式。

- **读取**：支持 OM v3、FPX_XOR2D / PFOR_DELTA2D_INT16 压缩、根数组和层级变量；NaN 转为 SQL `NULL`。
- **按需读取**：只读取输出和过滤需要的值变量；安全的经纬度条件可进一步缩小读取范围。
- **坐标**：可用显式规则网格或 [68 个已登记的 domain](docs/regular-domains.md) 生成 `latitude`、`longitude`。

当前仅支持单个本地文件、单线程扫描。除经纬度和有效时间外，其他轴不生成语义列；远程读取、并行扫描及投影/Gaussian 网格尚未实现。已验证平台为 Linux AArch64，Linux x86_64 支持与验证暂缓。

## 构建与加载

需要 C11 / C++17 编译器、CMake、Make、Git 和 DuckDB 的构建依赖。在仓库根目录执行：

```sh
git submodule update --init --recursive
make release
./build/release/duckdb -unsigned :memory:
```

在 CLI 中加载扩展并查询仓库样本：

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

DESCRIBE SELECT * FROM read_om('test/data/raw.om');
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
```

输出为 `value FLOAT` 列，值为 `0` 到 `5`。以上路径均相对于仓库根目录。

仓库固定 DuckDB **v1.5.4**。扩展也可加载到同版本、同平台的官方 DuckDB 中；本地扩展未签名，CLI 需加 `-unsigned`。为其他正式版本构建并运行 SQL 用例：

```sh
./scripts/build-version.sh v1.5.5
./build/versions/v1.5.5/release/duckdb -unsigned :memory:
```

随后加载 `build/versions/v1.5.5/release/extension/duckomo/duckomo.duckdb_extension`。脚本接受 `vX.Y.Z` tag，源码和产物放在 `build/versions/<版本>/`。每个目标 DuckDB 版本需分别编译；脚本成功运行的 SQL 用例是该次构建的兼容性依据。

## `data/`、`data_run/` 与 `data_spatial/` 的读取

三个目录的本地 OM 文件都使用 `read_om()`，不需要指定目录对应的读取模式；读取器根据文件内部元数据解析。远程对象须先下载到本地。

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

该路径需先下载对应文件。`TIMESTAMP` 列按 UTC 解释，不随会话时区转换。时间条件由 DuckDB 执行，当前不缩小 OM 时间轴的读取范围。

缺少时间元数据时，可通过 `valid_times := [TIMESTAMP '...', ...]` 显式提供每个时间位置的 UTC 时间；长度必须匹配 `time` 轴，缺少轴元数据时还需 `dimensions`。无 `time` 轴的空间快照可传一个时间。列表必须非空、无 NULL 且时间有限；不能覆盖文件已有的不同时间坐标。既无时间元数据也未提供 `valid_times` 的文件保持原有输出，不从路径或起报时间猜测有效时间。

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

经度统一为 `[-180,180)`。有限常量的 `=`, `<`, `<=`, `>`, `>=`, `BETWEEN` 和安全的 `AND` 可缩小扫描；完整 `WHERE` 始终由 DuckDB 执行。跨经线区域写为：

```sql
WHERE longitude >= 170 OR longitude < -170
```

`OR` 及无法安全分析的表达式保留 SQL 过滤，可能读取全域。实际读取节省取决于 OM 块布局；仅坐标、纯空间 `COUNT(*)` 和可证明的空选择不读取值数组。

## 开发与验证

```sh
make test                            # 构建 release 并执行 validate.sh
./scripts/validate.sh build/release   # 验证已有构建：SQL/native、样本重生与哈希、投影读取指标
make sanitizer-test                  # ASan/UBSan 检查
```

`validate.sh` 还需 `jq`、`sha256sum`、`diff`、`mktemp`，证据默认写入 `build/evidence/`。设置 `DUCKOMO_DOMAIN_FILE=/path/to/pinned.om` 可额外运行真实样本的完整空间验证；所需样本与哈希见 [空间查询指南](specs/002-spatial-pushdown/quickstart.md)。已有 AArch64 结果和独立复现见 [验收记录](specs/002-spatial-pushdown/evidence/final.md)。

## 文档与源码

| 内容 | 入口 |
|---|---|
| 参数、输出、支持范围 | [接口说明](docs/spec.md) · [完整 SQL 契约](specs/002-spatial-pushdown/contracts/sql-interface.md) |
| 网格定义与真实样本覆盖 | [规则网格 domain](docs/regular-domains.md) |
| 扫描流程与读取指标 | [技术架构](docs/architecture.md) |
| 后续能力 | [Roadmap](docs/roadmap.md) |
| 查询绑定与扫描 / 网格 / 本地 OM 读取 | `src/scan/` / `src/grid/` / `src/om/` |
| SQL 用例 / 原生检查 / 样本 | `test/sql/` / `test/native/` / `test/data/` |

`read_om_raw` 保留为仅支持 FPX 根数组的早期验证入口；日常查询使用 `read_om`。
