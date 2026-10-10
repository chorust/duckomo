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

当前每次读取**单个对象**，不接受目录、glob 或文件列表。新投影/Gaussian 网格的代码已实现，但逐网格真实样本、完整内存审计和独立复现尚未全部验收；实现不等于生产就绪，详见 [支持范围](docs/spec.md) 与 [网格证据](docs/grid-domains.md)。

2026-10-10 [范围修订](specs/004-multi-grid-selection/contracts/gaussian-acceptance-20261010.md)：因已调查 Open-Meteo 公开来源未找到匹配对象，负责人批准本轮跳过 N160/N320/N 区域真实验收，保留定义和合成回归，不记为通过也不阻塞当前范围收口。按 [2026-10-10 pinned producer 参考决定](specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)，`ecmwf_ifs` O1280 与三个投影定义登记为 `coordinate-value-validated`（与 Open-Meteo 发布对象一致，非原生 GRIB 网格等价）；O1280 依据 [真实本地查询验证](specs/004-multi-grid-selection/evidence/baseline-local/o1280-real-local-20261010/final.md)（全量 HSURF 值/坐标、显式/domain、空间与抽样时间序列）。远程收益、完整 gate 与 H9 独立复现仍开放。Phase 8 跨 chunk 入口仍是待评审提案。

## Install

### GitHub Release

从 [GitHub Releases](https://github.com/chorust/duckomo/releases) 下载与本机 DuckDB **版本和平台完全匹配**的包，校验 `SHA256SUMS` 后解压。先确认环境：

```sql
SELECT version();
PRAGMA platform;
```

发布矩阵为 **DuckDB v1.5.4 / v1.5.5 / v1.5.6** × **Linux glibc x86_64 / ARM64、macOS Intel / Apple Silicon**。以实际发布的资产和验证记录为准；若尚无匹配资产，可按 [Dev](#dev) 从源码构建。

GitHub 包未签名，只加载可信来源。用 `duckdb -unsigned` 启动后安装。Release 资产文件名带有版本和平台，直接把 URL 传给 `INSTALL` 时 DuckDB 会按第一个 `.` 截断扩展名（存成 `duckomo-v0.duckdb_extension`），导致后续 `LOAD duckomo` 找不到文件。因此先下载 `.duckdb_extension.gz` 并解压为标准文件名，再本地安装：

```bash
DUCKOMO_VERSION=v0.2.0                                                    # DuckOMO 版本
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

`dimensions` 支持共享轴列表或逐变量 MAP；目录布局与更多示例见 [规则网格指南](docs/regular-domains.md)。

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
