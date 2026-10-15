# ECMWF SEAS5 / EC46 Gaussian domains

v0.3.1 新增以下 domain；v0.3.0 包不包含本次修复。所有入口 `read_om`、`read_om_regions`、`om_grid_info` 共享这些 domain。

| Domain | 网格 | 空间点数 | 时间/成员来源 |
|---|---|---:|---|
| `ecmwf_seas5` | O320 | 421,120 | producer 为 6 小时/51 成员；以文件实际轴及时间坐标为准 |
| `ecmwf_seas5_ensemble_mean` | O320 | 421,120 | 6 小时，均值/散度分别为值文件 |
| `ecmwf_seas5_daily` | O320 | 421,120 | 日/51 成员 |
| `ecmwf_seas5_daily_ensemble_mean` | O320 | 421,120 | 日，均值/散度分别为值文件 |
| `ecmwf_seas5_monthly` | O320 | 421,120 | 月度时间标签由内嵌 time 数组读取 |
| `ecmwf_ec46` | O320 | 421,120 | 6 小时/51 成员 |
| `ecmwf_ec46_ensemble_mean` | O320 | 421,120 | 6 小时 |
| `ecmwf_ec46_weekly` | O320 | 421,120 | 周度时间标签由文件读取 |
| `ecmwf_seas5_12hourly` | N160 | 138,346 | 12 小时/11 成员，以固定 producer 实现为准 |
| `ecmwf_seas5_monthly_upper_level` | N160 | 138,346 | 月度，上层变量 |

来源固定为 Open-Meteo `b06f4760fd1f997e5559bb380f64c5e496b4a509` 的 [EcmwfSeasDomain.swift](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/EcmwfSeas/EcmwfSeasDomain.swift) 与 [GaussianGrid.swift](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Domains/GaussianGrid.swift)。生成器是 [`generate-seasonal-domains.py`](../scripts/generate-seasonal-domains.py)，同网格产品引用共享定义，保持相同 `grid_id`；产品名不参与空间身份。

O320 与 N320 均有 640 行，但点数/行表不同，不能互换。O320 每半球第 y 行有 `20 + 4*y` 点，共 421,120 点；N320 有 542,080 点。坐标重现固定 Open-Meteo producer 的 Float32 近似纬度及经度计算，不声称等价于 ECMWF 原生 GRIB 的 Legendre 纬度。

## 使用

真实 OM 文件一般内嵌 `coordinates = 'lat lon time'`，其中 `lat` 是长度为 1 的非空间轴，`lon` 是整个展平点轴。domain 显式登记 `lon` 作为 `point` 的别名，不将文件当成规则经纬度矩形。没有轴元数据时声明原始完整 dimensions，例如 `[y,point,time]` 或 `[y,point,member,time]`；声明不能覆盖冲突元数据。ensemble/member/level 不被空间筛选折叠，启用其输出仍使用现有 `axes` 参数。

domain 只选择网格，不根据名称猜测时间起点、步长、成员标签或单位。月度与周度文件优先用内嵌 `time`；缺失时显式提供 `valid_times`/`axes.time.values`。不要用固定 30 天代替日历月。

```sql
-- 按 README 安装匹配的 v0.3.1 包，并以 duckdb -unsigned 启动。
LOAD duckomo;
INSTALL httpfs; LOAD httpfs;
INSTALL spatial; LOAD spatial;

SELECT region_id, valid_time, count(*) AS grid_points,
       avg(value) AS mean_raw
FROM read_om_regions(
  's3://openmeteo/data_run/ecmwf_seas5_monthly/2026/10/01/0000Z/sea_surface_temperature_mean.om',
  domain := 'ecmwf_seas5_monthly',
  regions := [
    {'id':'nino34','wkb':ST_AsWKB(ST_GeomFromText(
      'POLYGON((-170 -5,-120 -5,-120 5,-170 5,-170 -5))'))},
    {'id':'nino12','wkb':ST_AsWKB(ST_GeomFromText(
      'POLYGON((-90 -10,-80 -10,-80 0,-90 0,-90 -10))'))}
  ], region_crs := 'grid_lonlat')
GROUP BY region_id, valid_time
ORDER BY region_id, valid_time;
```

该对象 shape `[1,421120,7]`，时间为 2026-10-01 至 2027-04-01 的月初。对象可能过期，替换时重新核对 metadata。**此样本 unit 属性标记 °C，但 SST 原始值约 271–304，存在上游单位一致性疑点。**上例明确输出 `mean_raw`，扩展不自动做 Kelvin/Celsius 转换。结果是点等权平均，不是气候态距平或面积加权 Nino 指数。

## 验证与可用性

注册不保证公开桶包含每个产品。当前 `data_run/` 调查取得 SEAS5 monthly、ensemble_mean、daily_ensemble_mean、EC46 ensemble_mean、weekly 的真实 O320 metadata；未取得原始 ensemble 及 N160 产品的公开 run 文件，这些名称仅有固定 producer 定义和合成布局验证，不宣称真实 N160 验收通过。

生成与本地回归：

```sh
python3 scripts/generate-seasonal-domains.py --check
python3 test/tools/grid_registry_test.py
python3 test/tools/seasonal_domain_test.py
```

合成测试覆盖全部 10 名称、O320/N160 全尺寸、独立坐标参考、时间与 member 轴、错误网格拒绝；设置 `DUCKOMO_SPATIAL_EXTENSION` 可附加 polygon 回归。真实验证下载数据至忽略的 build 目录，不向仓库提交气象载荷：

```sh
mkdir -p build/seas5-domain-audit
curl -fL https://openmeteo.s3.us-west-2.amazonaws.com/data/ecmwf_seas5/static/HSURF.om \
  -o build/seas5-domain-audit/HSURF.om
curl -fL https://openmeteo.s3.us-west-2.amazonaws.com/data_run/ecmwf_seas5_monthly/2026/10/01/0000Z/sea_surface_temperature_mean.om \
  -o build/seas5-domain-audit/sst-monthly.om
curl -fL https://openmeteo.s3.us-west-2.amazonaws.com/data_run/ecmwf_ec46_weekly/2026/07/12/0000Z/cloud_cover_anomaly.om \
  -o build/seas5-domain-audit/ec46-weekly.om
python3 scripts/validate-seasonal-domains.py \
  --samples build/seas5-domain-audit --output build/seas5-domain-audit/validation \
  --spatial build/regions-runtime/v1.5.4/official/spatial.duckdb_extension \
  --httpfs build/regions-runtime/v1.5.4/official/httpfs.duckdb_extension --remote
```

验证器核对全部 421,120 个静态值/坐标、月度和周度 6 个点的全部时间槽（官方 OM 解码对照）、月度 Nino 时间/点数；`--remote` 另外尝试完整 S3/local 聚合一致性。官方 CSV 的十进制读回按 Float32 比较。

本次已通过完整本地 SQL/native/Python、O320 全位置坐标参考、10 domain 的时间/member/CRS 合成回归、空间选择 ASan/UBSan、v1.5.5 官方运行时冒烟。真实本地 Nino3.4/Nino1+2 每月点数分别为 6,310/1,246，七个月份共 14 组。最终二进制另通过极区多边形的 S3/local 七个月完整结果对照。原实现完整 Nino S3 首轮超过 300 秒；后续 decoder 范围复用优化后，相同查询真实 S3 实测约 26 秒完成，与本地全部 14 组结果一致。该优化的受控 HTTP 回放从约 15,325 请求降至 156–157，请求数会因 worker 调度变化；详细计数另记。证据见 [seasonal-domains/summary.json](../specs/004-multi-grid-selection/evidence/seasonal-domains/summary.json) 及同目录的逐样本/远程结果。远程收益、N160 真实对象、独立原生 GRIB 参考及完整跨平台矩阵保持未验收。
