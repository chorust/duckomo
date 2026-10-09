# duckomo

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

## Install

### GitHub Release

从 [GitHub Releases](https://github.com/chorust/duckomo/releases) 下载与本机 DuckDB **版本和平台完全匹配**的包，校验 `SHA256SUMS` 后解压。先确认环境：

```sql
SELECT version();
PRAGMA platform;
```

发布矩阵为 **DuckDB v1.5.4 / v1.5.5 / v1.5.6** × **Linux glibc x86_64 / ARM64、macOS Intel / Apple Silicon**。以实际发布的资产和验证记录为准；若尚无匹配资产，可按 [Dev](#dev) 从源码构建。

GitHub 包未签名，只加载可信来源。用 `duckdb -unsigned` 启动后安装：

```sql
INSTALL '/path/to/duckomo.duckdb_extension';
LOAD duckomo;
```

Release ZIP 不能直接传给 `INSTALL`，GitHub 仓库地址也不是 DuckDB 扩展仓库。下载、校验、安装及 tag 发布流程见 [Release 指南](docs/releases.md)。

### 官方 HTTPFS

远程读取使用 DuckDB **官方扩展**，无需构建或下载 DuckOMO 专用 HTTPFS：

```sql
INSTALL httpfs;
LOAD httpfs;
```

本地 OM 读取不需要 HTTPFS。DuckOMO 尚未收录到 Community Extensions，当前不要使用 `INSTALL duckomo FROM community`；社区签名安装准备见 [社区说明](docs/community-extensions.md)。

## Usage

### 读取 Open-Meteo 公共 S3 数据

[Open-Meteo Open Data](https://github.com/open-meteo/open-data) 的公共桶为 `s3://openmeteo/`，位于 `us-west-2`，可匿名读取。加载扩展并配置仅作用于该桶的匿名 S3 secret，**不需要 AWS Access Key**：

```sql
LOAD duckomo;
INSTALL httpfs;
LOAD httpfs;

CREATE SECRET openmeteo_public (
  TYPE s3,
  REGION 'us-west-2',
  SCOPE 's3://openmeteo/'
);
```

先用不带滚动预测日期的 GFS 静态地形对象，直接查询一个区域：

```sql
SELECT value AS elevation, latitude, longitude
FROM read_om('s3://openmeteo/data/ncep_gfs025/static/HSURF.om',
  domain := 'ncep_gfs025',
  dimensions := map(['value'], [['lat', 'lon']]))
WHERE latitude BETWEEN 30 AND 30.5
  AND longitude BETWEEN 110 AND 110.5
ORDER BY latitude, longitude;
```

`domain` 显式选择网格；该对象缺少轴名元数据，因此用 `dimensions` 声明 `[lat, lon]`。只读数值时可省略空间配置，但多变量仍须有一致的有序轴身份。不会从路径、目录名或 shape 自动推断网格。

<a id="有效时间查询"></a>

### 查询预测变量与有效时间

公共桶中的预测对象会滚动更新，旧日期可能被清理。先按 [Open Data 目录说明](https://github.com/open-meteo/open-data) 选择当前存在的对象；也可使用 AWS CLI 匿名逐层列出路径：

```sh
aws s3 ls s3://openmeteo/data_spatial/ncep_gfswave025/ \
  --no-sign-request --region us-west-2
```

下面以一个 GFS Wave 空间快照为例，**使用时将日期和对象键替换为仍存在的样本**：

```sql
SET VARIABLE wave_file =
  's3://openmeteo/data_spatial/ncep_gfswave025/2026/10/01/0000Z/2026-10-01T0000.om';

SELECT wave_height, latitude, longitude, valid_time
FROM read_om(getvariable('wave_file'), domain := 'ncep_gfswave025')
WHERE latitude BETWEEN 30 AND 40
  AND longitude BETWEEN 140 AND 150
LIMIT 10;
```

带 Int64 `time` 坐标或标量 `valid_time` 的文件自动输出 UTC `valid_time TIMESTAMP`。可继续添加 `valid_time = TIMESTAMP '...'` 条件；缺少时间元数据时使用显式 `valid_times`，不会从文件名猜测时间。

| 目录 | 已审计样本的常见布局 | 经纬度查询 |
|---|---|---|
| `data_spatial/` | 空间快照，通常 `[lat, lon]` | 通常只需 `domain` |
| `data_run/` | 一次起报，通常 `[lat, lon, time]` | 通常只需 `domain` |
| `data/` | 滚动序列或静态数据，样本常缺少轴元数据 | `domain` + 完整 `dimensions` |

目录名不保证具体文件的轴顺序或格式兼容性。缺少元数据时，`dimensions` 须覆盖每个值变量；目录差异、变量对齐和逐 domain 样本范围见 [规则网格指南](docs/regular-domains.md)。

### HTTPS、私有 S3 与读取指标

同一公共对象也可通过 HTTPS 查询，无需 S3 secret：

```sql
SELECT value
FROM read_om('https://openmeteo.s3.us-west-2.amazonaws.com/data/ncep_gfs025/static/HSURF.om')
LIMIT 5;
```

私有桶按 [官方 HTTPFS S3 文档](https://duckdb.org/docs/current/core_extensions/httpfs/s3api) 配置 DuckDB secret，不把凭据写入共享 SQL。远程对象须支持范围读取，并在扫描期间保持稳定；不承诺扫描快照或每次查询强制刷新。DuckOMO 自有范围缓存及原缓存 SQL 已删除。

```sql
SET threads = 4;
SET duckomo_max_threads = 2; -- 0 使用 DuckDB 线程上限

SELECT query_id, scan_id, metrics::JSON
FROM duckomo_last_scan_metrics();
```

指标区分逻辑请求与标准文件接口成功读取；HTTPFS 实际网络发送量不由 DuckOMO 直接观测。空间筛选的读取收益取决于 OM 块布局，不仅取决于返回行数。详见 [接口说明](docs/spec.md)。

### 本地 OM

只需将 URI 换成本地路径，网格和维度参数保持相同规则：

```sql
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
-- 仓库样本返回 0 到 5。
```

`read_om_raw` 是早期 FPX 根数组验证入口，日常查询使用 `read_om`。显式 `grid`、其他语义轴、`include_source` 和 `om_grid_info` 用法见 [接口说明](docs/spec.md) 与 [多网格契约](specs/004-multi-grid-selection/contracts/sql-interface.md)。

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
