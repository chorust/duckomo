# 真实 Open-Meteo OM v3 文件兼容性（已修复）

## 问题

修复前，`read_om` 可读取仓库内的 OM v3 测试样本，但无法读取从公开的 `s3://openmeteo/` 下载的 11 个真实文件。测试覆盖 `data/`、`data_run/`、`data_spatial/`，包括 CHMI、GeoSphere 和 NCEP GFS 系列。所有文件下载到本地后，在 `DESCRIBE SELECT * FROM read_om(...)` 阶段被拒绝，尚未进入数值解码。

这些样本的文件头均为 `OM\x03`。主要不兼容点：

1. 抽样的数值数组使用 `PFOR_DELTA2D_INT16`（OM 压缩枚举值 0）；修复前仅接受 `FPX_XOR2D`（值 1）。
2. 真实数组可能带元数据子节点，如 `coordinates`、`time`、`unit`、`crs_wkt`；修复前只要数组有子节点就拒绝。
3. `data_spatial/` 文件以容器为根，变量数组也带子节点；修复前在第一个这样的变量处停止。

格式与元数据校验逻辑见 [`src/om/metadata.cpp`](../../src/om/metadata.cpp) 的 `AddArray`。当前支持子集见 [SQL 接口契约](../../specs/001-local-om-scanner/contracts/sql-interface.md)。

## 复现环境

- 日期：2026-09-29。
- DuckDB：官方 CLI v1.5.5；duckomo：针对 v1.5.5 构建的本地扩展。
- 来源：公开桶 `s3://openmeteo/`；通过匿名 HTTPS 下载完整文件到 `build/s3-samples/` 后测试。本问题只涉及本地文件兼容性；S3 URI 直接读取属于独立的远程 I/O 工作。
- 测试语句：`DESCRIBE SELECT * FROM read_om('本地文件路径');`。命令的非零退出码和错误信息用于判断当前实现是否接受文件。

以下命令用于记录修复前的复现方式；当前版本应能读取这两个样本。从仓库根目录执行，需先通过 `./scripts/build-version.sh v1.5.5` 构建扩展，并使用 v1.5.5 CLI：

```sh
mkdir -p build/s3-samples
curl -fL 'https://openmeteo.s3.amazonaws.com/data/chmi_aladin_cz_1km/precipitation/chunk_4125.om' \
  -o build/s3-samples/chmi-precipitation.om
curl -fL 'https://openmeteo.s3.amazonaws.com/data_run/chmi_aladin_cz_1km/2026/09/28/0000Z/precipitation.om' \
  -o build/s3-samples/chmi-run-precipitation.om

duckdb -unsigned :memory: "LOAD 'build/versions/v1.5.5/release/extension/duckomo/duckomo.duckdb_extension'; DESCRIBE SELECT * FROM read_om('build/s3-samples/chmi-precipitation.om');"
duckdb -unsigned :memory: "LOAD 'build/versions/v1.5.5/release/extension/duckomo/duckomo.duckdb_extension'; DESCRIBE SELECT * FROM read_om('build/s3-samples/chmi-run-precipitation.om');"
```

修复前，第一条查询报 `only FPX_XOR2D OM arrays are supported at ''`，第二条报 `OM array nodes with children are not supported at ''`。当前版本的复查结果见下文。

## 已测样本

以下路径均相对于 `s3://openmeteo/`，完整文件保存在本地 `build/s3-samples/<S3 key>`。大小来自 S3 列表，与下载后的文件大小一致。

| S3 key | 大小 | 修复前结果 |
| --- | ---: | --- |
| `data/chmi_aladin_cz_1km/precipitation/chunk_4125.om` | 213,512 B | 仅支持 FPX；根数组为 PFOR，无子节点 |
| `data/chmi_aladin_cz_1km/static/HSURF.om` | 141,504 B | 根数组有 2 个子节点 |
| `data/geosphere_arome_austria/precipitation/chunk_4605.om` | 387,680 B | 仅支持 FPX；根数组为 PFOR，无子节点 |
| `data/ncep_gfs025/cloud_cover_50hPa/chunk_1006.om` | 4,169,312 B | 仅支持 FPX；根数组为 PFOR，无子节点 |
| `data/ncep_gfs025/static/HSURF.om` | 408,440 B | 仅支持 FPX；根数组为 PFOR，无子节点 |
| `data_run/chmi_aladin_cz_1km/2026/09/28/0000Z/precipitation.om` | 174,744 B | 根数组有 6 个子节点 |
| `data_run/geosphere_arome_austria/2026/09/28/0000Z/snow_depth_water_equivalent.om` | 191,808 B | 根数组有 6 个子节点 |
| `data_run/ncep_gfs025/2026/09/28/0000Z/cloud_cover_50hPa.om` | 2,274,072 B | 根数组有 6 个子节点 |
| `data_spatial/chmi_aladin_cz_1km/2026/09/28/0000Z/2026-09-28T0000.om` | 643,568 B | `/temperature_2m` 数组带子节点 |
| `data_spatial/geosphere_arome_austria/2026/09/28/0000Z/2026-09-28T0000.om` | 2,083,080 B | `/temperature_2m` 数组带子节点 |
| `data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om` | 5,812,040 B | `/wave_height` 数组带子节点 |

目前只抽样了这些文件，不能据此判断整个公开桶的兼容率。`data_spatial/ncep_gfs025/` 的单文件约 113 MB，本次为了保持样本较小，选择了 GFS Wave 的空间文件。

## 期望与验收

- [x] 使用官方 OM reader 支持真实 Float32 数组所用的 `PFOR_DELTA2D_INT16` 压缩，并以官方解码结果核对值、缺测和缩放语义。
- [x] 遍历数组上的元数据子节点，识别值数组与附属元数据；合法的 `coordinates`、`time`、`unit` 等子节点不阻止读取数值，同时继续拒绝损坏的引用和不支持的数据布局。
- [x] 为 `data/`、`data_run/`、`data_spatial/` 各加入至少一个真实文件的可复现验证，覆盖 `DESCRIBE`、行数和实际数值读取；多变量文件还需验证变量对齐和列选择。
- [x] 对表中 11 个文件重新测试，记录每个文件的查询结果、官方 reader 对照及仍存在的明确限制。
- [x] 更新 SQL 接口契约和 README 中的格式支持说明。

本问题关注**单个本地 OM 文件的数值读取**。目录级自动发现、S3 直接读取以及 grid 坐标映射分别按现有 roadmap 处理。

## 修复与验证结果

`read_om` 现在使用官方解码器读取 Float32/`PFOR_DELTA2D_INT16`，遍历并校验值数组上的附属元数据。`data_run/` 中的一维整数 `time` 作为元数据保留，不误作与三维值数组对齐的 SQL 列。`data_spatial/` 中同形状的值数组依据一致的有序 `coordinates` 元数据自动对齐。SQL 列名去掉前导 `/`；内部变量路径仍保留 `/` 用于定位和指标归属。

在 v1.5.5 外部 DuckDB CLI 加载本次重建的扩展后，上表 11 个样本均通过 `DESCRIBE SELECT *`、`COUNT(*)` 和值列 `LIMIT 4` 查询。按上表从上到下的顺序，结果如下：

| 样本序号 | 值列数 | 行数 | 前四个值与官方 OM C 解码器 |
| ---: | ---: | ---: | --- |
| 1 | 1 | 17,434,800 | 一致，含 NULL |
| 2 | 1 | 145,290 | 一致 |
| 3 | 1 | 31,562,784 | 一致 |
| 4 | 1 | 499,393,440 | 一致 |
| 5 | 1 | 1,038,240 | 一致 |
| 6 | 1 | 10,606,170 | 一致，含 NULL |
| 7 | 1 | 17,827,128 | 一致 |
| 8 | 1 | 216,992,160 | 一致 |
| 9 | 21 | 145,290 | `temperature_2m` 一致；同时读取 `relative_humidity_2m` 成功 |
| 10 | 23 | 292,248 | `temperature_2m` 一致 |
| 11 | 15 | 1,038,240 | `wave_height` 一致，含 NULL |

对第 2 个样本还使用独立的官方 C 解码路径读取了全部 145,290 个值，并与 `read_om` 的完整 SQL 输出逐行比较，结果一致。前四个值使用 Float32 位模式比较；官方 NaN 对应 SQL NULL。仓库新增的 `pfor_attributes.om` 可由固定版本的官方写入器再生成，并在常规测试中验证 PFOR 解码、NULL、数组附属元数据和自动轴对齐。

复查真实文件时，可使用现有 S3 key 下载到本地，然后执行：

```sh
./scripts/build-version.sh v1.5.5
build/release/test/tools/duckomo_fixture_tool \
  --oracle-prefix build/s3-samples/data/chmi_aladin_cz_1km/static/HSURF.om \
  --variable / --count 4 --csv /tmp/duckomo-official-prefix.csv
duckdb -unsigned -csv :memory: "LOAD 'build/versions/v1.5.5/release/extension/duckomo/duckomo.duckdb_extension'; SELECT value FROM read_om('build/s3-samples/data/chmi_aladin_cz_1km/static/HSURF.om') LIMIT 4;"
```

`./scripts/validate.sh build/release` 已通过 SQL 测试、原生检查、fixture 再生成与投影指标验证。`read_om_raw` 仍是早期 FPX 根数组验证入口；真实文件请使用 `read_om`。
