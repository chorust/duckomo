# Open-Meteo AWS 规则网格 domain

`read_om(..., domain := '<名称>')` 的名称取 Open-Meteo AWS 对象键中 `data/`、`data_run/` 或 `data_spatial/` 后面的第一个 prefix。当前 registry 登记 68 个规则经纬度网格，定义固定于 Open-Meteo 上游提交 [`b06f4760fd1f997e5559bb380f64c5e496b4a509`](https://github.com/open-meteo/open-meteo/tree/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App)。每项的 `nx`、`ny`、起点、步长和 Swift 源码文件见 [domain_registry.cpp](../src/grid/domain_registry.cpp)。登记不从对象路径自动选择 domain，调用者仍需显式指定。

## 样本覆盖

2026-09-29 对三个目录各取首个可见 `.om` 对象进行审计。对象键、文件长度、shape、轴元数据和 WKT BBOX 见 [CSV 记录](../specs/002-spatial-pushdown/evidence/regular-domains.csv)。

| 状态 | 含义 |
|---|---|
| 可绑定 | 本地 OM v3 元数据尾部样本通过绑定；网格大小及已有轴/BBOX 与注册定义一致 |
| 无样本 | 当时未找到可审计对象 |
| 解析失败 | 首个样本未通过 OM v3 元数据解析，不能据此断言该 prefix 的所有文件不可用 |

这些状态只描述样本，不代表所有对象或全量值均已检查。Registry 中的 68 个名称仍全部可选。

| 目录 | 可绑定 | 无样本 | 解析失败 |
|---|---:|---:|---:|
| `data_spatial/` | 47 | 21 | 0 |
| `data_run/` | 39 | 29 | 0 |
| `data/` | 30 | 9 | 29 |

`data_spatial` 样本通常是 `[lat,lon]`；`data_run` 样本通常是 `[lat,lon,time]`，两者多数带完整 `coordinates` 和 WKT BBOX。

`data/` 的 30 个可绑定样本无 `coordinates`，调用时要通过 `dimensions` 为**每个值变量**提供完整有序轴名。29 个失败样本停在元数据解析阶段，其中有旧版 OM 对象；当前 reader 只支持 OM v3 Float32 子集。

UKMO `data_spatial` 样本的 218 个变量也缺少轴元数据，以显式 `dimensions` 声明 `[lat,lon]` 后完成绑定；它没有 BBOX，因此只完成来源定义、shape 和显式轴的核对。路径本身及相同 shape 都不能证明轴身份。

有 BBOX 的 `data_spatial` 样本为 46/47，`data_run` 为 39/39，均与注册网格匹配；`data/` 的 30 个可绑定样本都没有 BBOX，因而其坐标来源依赖固定的上游网格定义与调用者声明的轴。

## 使用与轴声明

真实 GFS Wave 样本的下载命令、固定哈希和查询示例见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md#真实-domain)。远程对象须先下载到本地，不能将 S3 URI 直接传给 `read_om`。

例如，将 `data/chmi_aladin_cz_1km/cape/chunk_4131.om` 下载到本地后，它的根数组列名是 `value`，shape 为 `[290,501,120]`，可这样声明缺失的轴身份：

```sql
SELECT value, latitude, longitude
FROM read_om('/path/to/chunk_4131.om',
  domain := 'chmi_aladin_cz_1km',
  dimensions := map(['value'], [['lat','lon','time']]))
WHERE latitude BETWEEN 49 AND 50;
```

绑定会检查所有值变量的有序轴、空间轴长度和文件存在时的 WKT BBOX。额外轴保留在原逻辑行序中，同一网格坐标会在不同时间位置重复；当前不会生成 `time` 等语义列。显式 `grid` 仍限制纬度在 `[-90,90]`。上游的 `meteofrance_wave`、`meteofrance_currents`、`meteofrance_sea_surface_temperature` 末行纬度约为 90.041664°，仅这三个登记 domain 按源数据定义保留该值。

## 审计方法与完整值对照

对象来自公开桶 `s3://openmeteo/`。审计按目录列出 domain prefix，记录一个对象键；通过 HTTP Range 取文件末尾的 OM 元数据，保持原文件长度，在本地稀疏文件中重建尾部供官方 OM C 元数据 API 与 DuckDB bind 读取。CSV 的 `local_metadata_tail_bytes` 是实际取得的尾部字节数。此方法检查 metadata 和绑定，不读取未下载的值数据；UKMO 样本因元数据较大，尾部扩大到 4 MiB 后绑定成功。

最初的 `ncep_gfswave025` 全域值校验见 [domain evidence](../specs/002-spatial-pushdown/evidence/domain.md)。另外，CHMI 145,290 行和 GeoSphere 292,248 行样本的坐标分别与独立 Swift Float 公式逐位置对照，最大绝对差约 `2.35e-6` 和 `3.72e-6` 度；`temperature_2m` 与官方 OM C reader 的全量 Float32 位模式逐行一致。这些全量对照是针对两个具体样本，其余登记项的证据范围如上表所述。

## 已登记名称与目录样本状态

`✓` 可绑定；`—` 无样本；`!` 首个样本元数据解析失败。完整样本对象键和 shape 见 CSV。

| Domain | `data_spatial/` | `data_run/` | `data/` |
|---|:---:|:---:|:---:|
| `bom_access_global` | — | — | ! |
| `bom_access_global_ensemble` | — | — | ! |
| `cams_europe` | ✓ | ✓ | ✓ |
| `cams_global` | ✓ | ✓ | ✓ |
| `cams_global_greenhouse_gases` | ✓ | ✓ | ✓ |
| `chmi_aladin_cz_1km` | ✓ | ✓ | ✓ |
| `cma_grapes_global` | ✓ | ✓ | ! |
| `cmc_gem_gdps` | — | — | ! |
| `cmc_gem_gdps_15km` | ✓ | ✓ | ✓ |
| `cmc_gem_gdps_15km_upper_level` | ✓ | ✓ | ✓ |
| `cmc_gem_geps` | ✓ | — | ! |
| `cmc_gem_geps_ensemble_mean` | — | — | — |
| `copernicus_era5` | — | — | ✓ |
| `copernicus_era5_ensemble` | — | — | ✓ |
| `copernicus_era5_land` | — | — | ✓ |
| `copernicus_era5_ocean` | — | ✓ | ✓ |
| `dwd_ewam` | ✓ | ✓ | ✓ |
| `dwd_gwam` | ✓ | ✓ | ✓ |
| `dwd_icon` | ✓ | ✓ | ! |
| `dwd_icon_d2` | ✓ | ✓ | ! |
| `dwd_icon_d2_15min` | — | ✓ | ! |
| `dwd_icon_d2_eps` | ✓ | — | ! |
| `dwd_icon_d2_eps_ensemble_mean` | — | — | — |
| `dwd_icon_eps` | ✓ | — | ! |
| `dwd_icon_eps_ensemble_mean` | — | — | — |
| `dwd_icon_eu` | ✓ | ✓ | ! |
| `dwd_icon_eu_eps` | ✓ | — | ! |
| `dwd_icon_eu_eps_ensemble_mean` | — | — | — |
| `ecmwf_aifs025_ensemble` | ✓ | — | ✓ |
| `ecmwf_aifs025_ensemble_mean` | — | — | — |
| `ecmwf_aifs025_single` | ✓ | ✓ | ✓ |
| `ecmwf_ifs025` | ✓ | ✓ | ! |
| `ecmwf_ifs025_ensemble` | ✓ | — | ! |
| `ecmwf_ifs025_ensemble_mean` | — | — | — |
| `ecmwf_wam025` | ✓ | ✓ | ✓ |
| `geosphere_arome_austria` | ✓ | ✓ | ✓ |
| `italia_meteo_arpae_icon_2i` | ✓ | ✓ | ✓ |
| `jma_gsm` | ✓ | ✓ | ! |
| `jma_msm` | ✓ | ✓ | ! |
| `jma_msm_upper_level` | ✓ | ✓ | ✓ |
| `kma_gdps` | — | — | ✓ |
| `knmi_harmonie_arome_netherlands` | ✓ | ✓ | ! |
| `meteofrance_arome_france0025` | ✓ | ✓ | ! |
| `meteofrance_arome_france0025_15min` | ✓ | ✓ | ! |
| `meteofrance_arome_france_hd` | ✓ | ✓ | ! |
| `meteofrance_arome_france_hd_15min` | ✓ | ✓ | ! |
| `meteofrance_arpege_europe` | ✓ | ✓ | ! |
| `meteofrance_arpege_europe_probabilities` | — | — | ! |
| `meteofrance_arpege_world025` | ✓ | ✓ | ! |
| `meteofrance_currents` | ✓ | ✓ | ✓ |
| `meteofrance_sea_surface_temperature` | ✓ | ✓ | ✓ |
| `meteofrance_wave` | ✓ | ✓ | ✓ |
| `ncep_aigefs025` | ✓ | — | ✓ |
| `ncep_aigefs025_ensemble_mean` | — | — | ✓ |
| `ncep_aigfs025` | ✓ | ✓ | ✓ |
| `ncep_gefs025` | ✓ | — | ! |
| `ncep_gefs025_ensemble_mean` | — | — | — |
| `ncep_gefs05` | ✓ | — | ! |
| `ncep_gefs05_ensemble_mean` | — | — | — |
| `ncep_gfs013` | ✓ | ✓ | ✓ |
| `ncep_gfs025` | ✓ | ✓ | ! |
| `ncep_gfs_graphcast025` | — | — | ! |
| `ncep_gfswave016` | ✓ | ✓ | ✓ |
| `ncep_gfswave025` | ✓ | ✓ | ✓ |
| `ncep_hgefs025_ensemble_mean` | ✓ | ✓ | ✓ |
| `ukmo_global_deterministic_10km` | ✓ | ✓ | ! |
| `ukmo_global_ensemble_20km` | ✓ | — | ✓ |
| `ukmo_global_ensemble_mean_20km` | — | — | — |
