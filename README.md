<h1 align="center">
  <img src="logo.png" alt="duckomo logo" width="160"><br>
  duckomo
</h1>

中文 | [English](README.en.md)

duckomo 是一个 DuckDB C++ 扩展，将 [Open-Meteo OM](https://github.com/open-meteo/om-file-format) 文件（本地或 HTTP(S)/S3）中的 Float32 数组直接作为 SQL 表查询，无需先转换格式。

[Features](#features) · [Install](#install) · [Usage](#usage) · [Dev](#dev) · [Docs](#docs) · [Acknowledgements](#acknowledgements)

## Features

- **直接查询 OM**：支持 OM v3、Float32、FPX_XOR2D / PFOR_DELTA2D_INT16、根数组及层级变量；NaN 转为 SQL `NULL`。
- **远程按需读取**：通过 DuckDB 官方 HTTPFS 读取 HTTP(S)/S3 对象，不需要先下载整个文件。
- **列裁剪与空间筛选**：只读取输出和过滤依赖的值变量；安全的经纬度、时间等条件可缩小候选范围，完整 `WHERE` 仍由 DuckDB 执行。
- **多种网格 mapping**：规则经纬度、[68 个规则 domain](docs/regular-domains.md)、旋转经纬度、Lambert、stereographic 和 reduced Gaussian。
- **多变量与语义轴**：按有序轴身份对齐变量，支持 `valid_time`、`level`、`lead_time`、`member`、`run`，不自动转置、广播或连接。
- **并行与可观测性**：DuckDB 调度并行扫描；提供线程上限、扫描指标、可选 `om_source` 源位置及 `om_grid_info` 网格描述。
- **Polygon 区域读取（实验性）**：`read_om_regions` 默认保留原始面内点（`method := 'points'`）；`method := 'nearest'` 可按区域代表点为每个非空间轴组合选取一个最近网格源值。nearest 返回代表点采样值，不是面积均值；006 已完成声明范围内的 N0–N7 验收：Linux AArch64、DuckDB v1.5.4/v1.5.5/v1.5.6、合成规则球面网格与固定 NCEP GFS025；真实输入仅私有留存、不公开分发，N7由独立用户执行。 [验收报告](specs/006-region-nearest-sampling/evidence/final.md)。

当前每次读取**单个对象**，不接受目录、glob 或文件列表。`read_om_regions` 的 points/nearest 都需要显式加载匹配版本的 DuckDB spatial；普通 `read_om` 不需要。区域 WKB 的 `grid_lonlat` 参考由调用者负责，points 边界按平面 `covers` 处理，统计为 SQL 等权聚合。nearest 的顶层 `lat/lon` 是面内目标，实际源坐标、距离、逐变量状态位于 `sampling`，COUNT(*)计结果行。006 已完成声明范围内的 N0–N7 验收：Linux AArch64、DuckDB v1.5.4/v1.5.5/v1.5.6、合成规则球面网格与固定 NCEP GFS025；真实输入仅私有留存、不公开分发，N7由独立用户执行。 [验收报告](specs/006-region-nearest-sampling/evidence/final.md)。

本仓库 tag release 已配置 DuckDB v1.5.4、v1.5.5、v1.5.6、v2.0.0 × 四平台，共 16 个开发产物。DuckDB 2.0.0 已验证兼容，通过 [v0.3.5-rc.1 预发布](https://github.com/chorust/duckomo/releases/tag/v0.3.5-rc.1) 提供四平台产物；异步 I/O 仍为实验功能，无稳定加速承诺。2026-10-22 [最终验收](specs/004-multi-grid-selection/evidence/current-20261021/report.md)：`gem_rdps_10km`、`gem_regional`、`aladin_central_europe_2km`、`ecmwf_ifs` 四定义的五个冻结对象在 Linux AArch64 / DuckDB v1.5.4、v1.5.5、v1.5.6 通过 H0–H9；45 对 HTTP/HTTPS/签名 S3 完整远程结果与三项严格成本下降通过，范围内为 `remote-benefit-validated`。H9 为未参与产品实现的独立 AI 验证，非人工；构建为变更 TU 重编译/relink。坐标遵循 [pinned Open-Meteo producer](specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)，不宣称原生 GRIB 等价。真实 N160/N320/N 区域按[批准范围](specs/004-multi-grid-selection/contracts/gaussian-acceptance-20261010.md)跳过，合成回归保留；不外推全部 68 个规则 domain、其他对象/平台或 DuckDB 2.0。Phase 8 跨 chunk 入口仍是待评审提案。

## Install

### GitHub Release

从 [GitHub Releases](https://github.com/chorust/duckomo/releases) 下载与本机 DuckDB **版本和平台完全匹配**的包，校验 `SHA256SUMS` 后解压。先确认环境：

```sql
SELECT version();
PRAGMA platform;
```

GitHub tag 开发发行矩阵配置为 **DuckDB v1.5.4 / v1.5.5 / v1.5.6 / v2.0.0** × **Linux glibc x86_64 / ARM64、macOS Intel / Apple Silicon**。DuckDB 2.0.0 已验证兼容，当前通过 v0.3.5-rc.1 预发布提供，尚非稳定版发布；以实际发布的资产和对应验证记录为准。若尚无匹配资产，可按 [Dev](#dev) 从源码构建。

GitHub 包未签名，只加载可信来源。用 `duckdb -unsigned` 启动后安装。Release 资产文件名带有版本和平台，直接把 URL 传给 `INSTALL` 时 DuckDB 会按第一个 `.` 截断扩展名（存成 `duckomo-v0.duckdb_extension`），导致后续 `LOAD duckomo` 找不到文件。因此先下载 `.duckdb_extension.gz` 并解压为标准文件名，再本地安装：

```bash
DUCKOMO_VERSION=v0.3.4                                                    # DuckOMO 版本
DUCKDB_VERSION=$(duckdb -list -noheader :memory: "SELECT version();")      # 从本机 DuckDB 获取
PLATFORM=$(duckdb -list -noheader :memory: "PRAGMA platform;")            # 从本机 DuckDB 获取

curl -sfL "https://github.com/chorust/duckomo/releases/download/${DUCKOMO_VERSION}/duckomo-${DUCKOMO_VERSION}-duckdb-${DUCKDB_VERSION}-${PLATFORM}.duckdb_extension.gz" | gunzip > duckomo.duckdb_extension
```

```sql
INSTALL './duckomo.duckdb_extension';
LOAD duckomo;
```

也可以手动下载 Release ZIP（内含同样的 `duckomo.duckdb_extension`）及独立的 `SHA256SUMS`，先校验 ZIP，再解压并执行同样的本地安装。

Release ZIP 不能直接传给 `INSTALL`，GitHub 仓库地址也不是 DuckDB 扩展仓库。下载、校验、安装及 tag 发布流程见 [Release 指南](docs/releases.md)。

## Usage

### 读取 Open-Meteo 公共 S3 数据

直接查询 [Open-Meteo 公共数据](https://github.com/open-meteo/open-data) 中的 GFS 地形：

```sql
LOAD duckomo;
INSTALL httpfs;
LOAD httpfs;

-- 可选：显式指定 Open-Meteo 公共 S3 桶的 endpoint 和 region，无需凭据。
-- 通常可直接匿名读取；如已有覆盖 s3:// 的其他 secret，可用更精确的 scope 指定此桶。
-- CREATE SECRET openmeteo_public (
--   TYPE s3, PROVIDER config,
--   ENDPOINT 's3.us-west-2.amazonaws.com',
--   REGION 'us-west-2',
--   SCOPE 's3://openmeteo/'
-- );

SELECT value AS elevation, lat, lon
FROM read_om('s3://openmeteo/data/ncep_gfs025/static/HSURF.om',
  domain := 'ncep_gfs025',
  dimensions := ['lat', 'lon'])
WHERE lat BETWEEN 30 AND 30.5
  AND lon BETWEEN 110 AND 110.5
ORDER BY lat, lon;
```

示例输出：

```bash
┌───────────┬───────┬────────┐
│ elevation │  lat  │  lon   │
├───────────┼───────┼────────┤
│ 1292.0    │ 30.0  │ 110.0  │
│ 1258.0    │ 30.0  │ 110.25 │
│ 1272.0    │ 30.0  │ 110.5  │
│ 1265.0    │ 30.25 │ 110.0  │
│ 1275.0    │ 30.25 │ 110.25 │
│ 1278.0    │ 30.25 │ 110.5  │
│ 1043.0    │ 30.5  │ 110.0  │
│ 1058.0    │ 30.5  │ 110.25 │
│ 1035.0    │ 30.5  │ 110.5  │
└───────────┴───────┴────────┘
```

`domain` 选择网格，`dimensions` 声明数组轴。公共桶匿名读取，无需创建 secret。注意：本机已有的 scope 为 `s3://` 的 secret（如 OSS/MinIO 等自定义 endpoint 的配置）会接管 Open-Meteo 请求并导致 404；用 `SELECT name, scope FROM duckdb_secrets();` 检查，删除或收窄该 secret 的 scope 即可。

<a id="有效时间查询"></a>

### 查询预测变量与有效时间

以 GFS Wave 空间快照为例。从 [Open Data 目录](https://github.com/open-meteo/open-data) 选取可用文件，**替换下面的日期和对象键**：

```sql
SET VARIABLE wave_file =
  's3://openmeteo/data_spatial/ncep_gfswave025/2026/10/01/0000Z/2026-10-01T0000.om';

SELECT wave_height, lat, lon, valid_time
FROM read_om(getvariable('wave_file'), domain := 'ncep_gfswave025')
WHERE lat BETWEEN 30 AND 40
  AND lon BETWEEN 140 AND 150
LIMIT 10;
```

带时间元数据的文件会自动输出 UTC `valid_time`，可直接用于时间筛选。

| 目录 | 常见布局 | 经纬度查询 |
|---|---|---|
| `data_spatial/` | 空间快照，通常 `[lat, lon]` | 通常只需 `domain` |
| `data_run/` | 一次起报，通常 `[lat, lon, time]` | 通常只需 `domain` |
| `data/` | 滚动序列或静态数据，样本常缺少轴元数据 | `domain` + 完整 `dimensions` |

`dimensions` 支持共享轴列表或逐变量 MAP；目录布局与更多示例见 [规则网格指南](docs/regular-domains.md)。v0.3.1 的 `read_om`、`read_om_regions`、`om_grid_info` 均支持 `variable := '列名'`，在绑定时仅保留该值数组，适合变量很多的远程文件。嵌套列按编码路径选择，如 `weird%2Fname%25/value`；维度 MAP 只声明所选数组。默认扫描复用绑定元数据；如需全树重读检查，设置 `duckomo_revalidate_metadata=true`，会增加读取成本。

### 按多边形筛选、聚合或取最近代表值

开发版的 `read_om_regions` 接收标准 WKB 区域。默认或 `method := 'points'` 返回落在多边形内的原始网格点；统计由 SQL 完成，均值是点的等权平均，不是面积加权。`method := 'nearest'` 为每个区域×非空间轴组合输出代表位置最近的一个网格源值，顶层 `lat/lon` 是面内目标，`sampling.source_lat/source_lon` 是实际源坐标；COUNT(*)计结果行。nearest 仅支持通过资格检查的规则球面网格，并须加载与 DuckDB 匹配的 spatial。006 已完成声明范围内的 N0–N7 验收：Linux AArch64、DuckDB v1.5.4/v1.5.5/v1.5.6、合成规则球面网格与固定 NCEP GFS025；真实输入仅私有留存、不公开分发，N7由独立用户执行。 [验收报告](specs/006-region-nearest-sampling/evidence/final.md)。

```sh
mkdir -p build/manual-region-queries
curl -fL https://geodata.ucdavis.edu/gadm/gadm4.1/json/gadm41_CHN_2.json.zip \
  -o build/manual-region-queries/gadm41_CHN_2.json.zip
python3 - <<'PY'
import json, zipfile
from pathlib import Path
folder = Path('build/manual-region-queries')
with zipfile.ZipFile(folder / 'gadm41_CHN_2.json.zip') as archive:
    data = json.loads(archive.read('gadm41_CHN_2.json'))
assert data['crs']['properties']['name'] == 'urn:ogc:def:crs:OGC:1.3:CRS84'
data['features'] = [f for f in data['features'] if f['properties']['NAME_1'] == 'Qinghai']
assert len(data['features']) == 8
(folder / 'qinghai-gadm41.geojson').write_text(json.dumps(data, ensure_ascii=False))
PY
```

该文件声明 CRS84（x=经度、y=纬度），下例据此传入 `grid_lonlat`。边界版本为 GADM 4.1，不代表实时行政区划；不再使用未核实坐标参考的 DataV 绘图边界。

```sql
-- 先按 Install 章节安装 v0.3.4 的匹配包，并以 duckdb -unsigned 启动。
LOAD duckomo;
INSTALL httpfs; LOAD httpfs;
INSTALL spatial; LOAD spatial; -- 与当前 DuckDB 匹配；首次安装需要联网

CREATE TEMP TABLE qinghai AS
SELECT feature.properties.GID_2 AS region_id,
       ST_GeomFromGeoJSON(feature.geometry::JSON) AS geometry
FROM (SELECT unnest(features) AS feature
      FROM read_json_auto('build/manual-region-queries/qinghai-gadm41.geojson'));

SET VARIABLE qinghai_regions = (
    SELECT list({'id': region_id, 'wkb': ST_AsWKB(geometry)} ORDER BY region_id)
    FROM qinghai
);

SELECT region_id, count(*) AS grid_points,
       round(avg(temperature_80m), 3) AS mean_celsius
FROM read_om_regions(
    's3://openmeteo/data_spatial/ncep_gfs025/2026/10/14/0000Z/2026-10-14T0000.om',
    domain := 'ncep_gfs025', variable := 'temperature_80m',
    regions := getvariable('qinghai_regions'),
    region_crs := 'grid_lonlat')
GROUP BY region_id
ORDER BY region_id;
```

该快照没有 `temperature_2m`，上例使用实际存在的 `temperature_80m`。对象会过期，替换前先从 Open-Meteo 目录确认新的对象键。

v0.3.1 新增 SEAS5/EC46 的 8 个 O320 与 2 个 N160 domain，并复用 decoder 已读范围，减少逐点重复的索引/数据请求。完整 Nino S3 示例实测约 24–26 秒完成；产品列表、月度查询及单位边界见 [季节预测 domain 指南](docs/seasonal-domains.md)。

### SEAS5 月度海温

月度文件内嵌真实时间坐标，无需手动设置 `axes.start`：

```sql
SELECT valid_time, avg(value) AS mean_raw
FROM read_om(
  's3://openmeteo/data_run/ecmwf_seas5_monthly/2026/10/01/0000Z/sea_surface_temperature_mean.om',
  domain := 'ecmwf_seas5_monthly')
WHERE lat BETWEEN -5 AND 5 AND lon BETWEEN -170 AND -120
GROUP BY valid_time ORDER BY valid_time;
```

该样本的 unit 标签与数值量级存在疑点，因此输出保留原值 `mean_raw`，不标作摄氏度；完整双区域 polygon 示例见上述指南。

### 区域平均：Nino 海温

`s3://openmeteo/data/copernicus_era5/sea_surface_temperature/` 是 chunk 目录，不能直接作为 `read_om` 路径；选择其中一个 `chunk_*.om`。以 `chunk_987.om` 计算 Nino3.4 和 Nino1+2：

```sql
SET VARIABLE nino_regions = [
    {'id': 'nino34', 'wkb': ST_AsWKB(ST_GeomFromText(
        'POLYGON((-170 -5,-120 -5,-120 5,-170 5,-170 -5))'))},
    {'id': 'nino12', 'wkb': ST_AsWKB(ST_GeomFromText(
        'POLYGON((-90 -10,-80 -10,-80 0,-90 0,-90 -10))'))}
];

SELECT region_id, valid_time, count(*) AS grid_points,
       round(avg(value), 3) AS mean_celsius
FROM read_om_regions(
    's3://openmeteo/data/copernicus_era5/sea_surface_temperature/chunk_987.om',
    domain := 'copernicus_era5',
    dimensions := ['lat', 'lon', 'time'],
    axes := {'time': {'axis': 'time', 'start': TIMESTAMP '2026-10-01',
                      'step': INTERVAL '1 hour'}},
    regions := getvariable('nino_regions'),
    region_crs := 'grid_lonlat')
WHERE valid_time >= TIMESTAMP '2026-10-01'
  AND valid_time < TIMESTAMP '2026-10-02'
GROUP BY region_id, valid_time
ORDER BY region_id, valid_time;
```

时间标签依据 [固定 producer 的 ERA5 配置](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Era5/Era5Domain.swift)、[chunk 划分](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Helper/OmFileSplitter.swift) 与 [Unix 时间索引](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/OmTime/Time.swift)：每文件 504 小时，chunk N 从 `1970-01-01 + N × 504 小时` 开始。因此 987 对应 2026-10-01 00:00 UTC，共 21 天；示例只统计第一天。更换对象时重新计算起始时间并核对 shape/producer 约定；`axes.start` 赋予标签，不会发现真实日期。结果是网格点等权平均，不是气候态距平或面积加权的 Nino 指数。

### HTTPS、私有 S3 与读取指标

同一对象也可通过 HTTPS 查询：

```sql
SELECT value
FROM read_om('https://openmeteo.s3.us-west-2.amazonaws.com/data/ncep_gfs025/static/HSURF.om')
LIMIT 5;
```

私有桶的凭据配置见 [HTTPFS S3 文档](https://duckdb.org/docs/current/core_extensions/httpfs/s3api)。线程配置和最近一次扫描的读取指标：

```sql
SET threads = 4;
SET duckomo_max_threads = 2; -- 0 使用 DuckDB 线程上限

远程 HTTP/HTTPS/S3 的实验设置：`duckomo_remote_merge`、`duckomo_remote_merge_gap`、`duckomo_remote_max_request`；DuckDB 2.0 还提供 `duckomo_async_io`、`duckomo_async_max_inflight` 和实验开关 `duckomo_async_prefetch_batches`（1–4，默认 1 即关闭跨批预读）。实测异步没有稳定加速；跨批窗口虽增加并发，但增加重复读取、使固定样本变慢，因此默认关闭。这些设置及 `remote_io` 指标尚未通过 spec 008 的完整远程/异步验收；当前不要将其视为已交付性能承诺。

SELECT query_id, scan_id, metrics::JSON
FROM duckomo_last_scan_metrics();
```

指标含义见 [接口说明](docs/spec.md)。

### 本地 OM

将 URI 换成本地路径即可。以仓库中的样本为例：

```sql
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
```

```bash
┌───────┐
│ value │
├───────┤
│ 0.0   │
│ 1.0   │
│ 2.0   │
│ 3.0   │
│ 4.0   │
│ 5.0   │
└───────┘
```

显式网格、语义轴与来源信息等用法见 [接口说明](docs/spec.md)。

## Dev

需要 C11 / C++17 编译器、CMake、Make、Git、Python 3 和 DuckDB 构建依赖。在仓库根目录执行：

```sh
git submodule update --init --recursive
make release
./build/release/duckdb -unsigned :memory:
```

开发构建在 SQL 中直接加载：

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
```

开发子模块固定 DuckDB v1.5.4；按其他固定版本构建可运行 `scripts/build-version.sh v1.5.6`（也支持 v1.5.4 / v1.5.5）。版本间不共用扩展二进制；官方运行包及完整复现步骤见 [HTTPFS 验证指南](docs/official-httpfs.md)。

普通构建不要求 spatial runtime 产物。需要运行区域 sanitizer 测试时，先通过 `scripts/fetch-region-runtime.py` 准备匹配的 runtime，再向 `scripts/build-version.sh` 传入 `--region-sanitizers`。`--extension-only` 仅构建扩展，跳过区域 runtime 前置检查。

```sh
make test                                       # 构建并运行本地 SQL/native/工具验证
./scripts/validate.sh build/release --local-only  # 验证已有构建
make sanitizer-test                             # ASan / UBSan
python3 test/tools/release_tools_test.py           # 发布契约检查
```

已有深入验收主要来自 Linux AArch64；新增平台以其实际 CI 结果为准。完整远程验证还需受控 HTTP/HTTPS/签名 S3 服务和审计日志；本地测试或 Release 冒烟通过不替代真实网格及独立复现门禁。

源码入口：`src/scan/` 负责绑定与扫描，`src/grid/` 负责网格和布局，`src/om/` 负责 OM 读取；测试及样本在 `test/sql/`、`test/native/`、`test/data/`。

## Docs

| 内容 | 文档 |
|---|---|
| 参数、输出、支持范围与指标 | [接口说明](docs/spec.md) |
| 公共数据目录、轴声明和 68 个规则 domain | [规则网格指南](docs/regular-domains.md) |
| 投影 / Gaussian 定义与真实样本证据 | [网格证据表](docs/grid-domains.md) · [SQL 契约](specs/004-multi-grid-selection/contracts/sql-interface.md) |
| Polygon 区域原始读取、分时统计与验证入口 | [005 Quickstart](specs/005-polygon-region-statistics/quickstart.md) · [SQL 契约](specs/005-polygon-region-statistics/contracts/sql-interface.md) |
| 模块职责、扫描和 I/O 流程 | [技术架构](docs/architecture.md) |
| GitHub 下载、安装与 tag 发布 | [Release 指南](docs/releases.md) |
| 官方 HTTPFS 支持与受控验证 | [HTTPFS 验证指南](docs/official-httpfs.md) |
| 社区登记与签名发布 | [Community Extensions](docs/community-extensions.md) |
| 开发阶段与未完成验收 | [Roadmap](docs/roadmap.md) |

## Acknowledgements

- [DuckDB](https://github.com/duckdb/duckdb)：SQL 引擎、扩展接口，以及官方 HTTPFS 远程文件系统。
- [Open-Meteo OM File Format](https://github.com/open-meteo/om-file-format)：OM 格式及本扩展使用的官方 C 读取/解码实现。
- [Open-Meteo Open Data](https://github.com/open-meteo/open-data) 和上游气象数据提供方：公开样本、模型数据和网格定义来源。
- [DuckDB extension-ci-tools](https://github.com/duckdb/extension-ci-tools)：社区构建与跨平台分发工具链。
- [AWS Open Data](https://aws.amazon.com/opendata/)：公共数据托管计划。

本项目代码采用 [Apache-2.0](LICENSE)；气象数据的许可和署名要求以各数据提供方的条款为准。
