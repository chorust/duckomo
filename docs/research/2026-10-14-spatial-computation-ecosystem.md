# DuckDB 栅格、Zarr 与空间计算生态调研

检索日期：2026-10-14 UTC。目标：核实小多边形在粗网格上返回空结果是否为共同问题，并为 DuckOMO 的代表点 nearest、插值、区域统计及未来计算建立设计依据。关联：[设计提案](../plans/2026-10-14-spatial-sampling-and-computation.md)。

本次由三个独立 researcher 子代理分别调查 TIFF/COG/raster、Zarr/多维数组、xarray/GDAL/rasterstats/exactextract，父代理读取全部报告并复核关键原始文档/源码。研究使用 [research 技能](/home/blizhan/.agents/skills/research/SKILL.md) 和 [pi-subagents 技能](/home/blizhan/.pi/agent/npm/node_modules/pi-subagents/skills/pi-subagents/SKILL.md)。没有安装或执行这些外部扩展，外部行为按文档/源码限定；本项目东城区案例另有真实 SQL 复现。

## 1. 核心结论

“小面没有采样中心”是采样和选择语义问题，与存 TIFF、Zarr 还是 OM 没有必然关系。不同工具采取不同规则：

| 工具 / 操作 | 已核对语义 | 不含采样中心的小面 | 权重 / 局限 |
| --- | --- | --- | --- |
| DuckOMO 当前 read_om_regions | ST_Covers 面覆盖源点 | 零原始行 | SQL 非 NULL 点等权平均 |
| DuckDB cog RS_ZonalStats | bbox/真正 polygon 按像元中心选取 | 可为空 | 无有效像元 count=0，其它统计 NULL；所核源码无 fractional coverage |
| DuckDB raster RT_Stats 几何重载 | 像元四角足迹与面相交 | 正常覆盖且有效时可选到单元 | 相交单元整权计入，无覆盖比例权重 |
| DuckDB raster RT_ReadCells + ST_Covers | 逐像元中心点过滤 | 可为空 | 不能与 RT_Stats 默认规则混同 |
| DuckDB zarr read_zarr | 多维数组展开为 SQL 行 | 自身没有 polygon 算子 | 另用 ST_Covers 仍可能为空；无内置 nearest/interp |
| xarray 常规 PandasIndex .sel(nearest) | 按独立一维坐标标签最近匹配 | 需先将 polygon 转成目标点 | 不是区域统计；不内置地理距离 |
| Rasterio mask 默认 / rasterstats 默认 | 栅格化二值选择，通常中心纳入，线另有 Bresenham 规则 | 可全掩码 | all_touched 是另一种二值选择 |
| exactextract | 像元交叠覆盖比例 | 有正交叠和有效值时可统计 | 默认 mean 按覆盖比例加权，不自动等于物理面积加权 |

因此，没有发现一个可直接照搬且自动统一这些含义的公共 `method` 契约。可以借鉴调用体验，但必须定义目标、距离/面积、缺测和结果身份。

## 2. 核查范围与版本

| 项目 | 固定范围 | 证据等级 |
| --- | --- | --- |
| cog | `st-layer/duckdb-cog` 提交 `08c283b50acbf9267b25a59427319cbf15eb3327` | 文档、源码与未执行上游测试 |
| raster | `ahuarte47/duckdb-raster` 提交 `2d980e77adf232c7f5bf046f2b1d755922c99fe5` | 文档、源码与未执行上游测试 |
| zarr 社区 | v0.1.3，提交 `d5ae255c4fd639d81e749c562415bc8eaad55ce3` | 社区 manifest、源码 |
| zarr main | `985396ff5976e5d4341d2afacf243fa9a82f2cbe` | 源码；新能力不算发布版能力 |
| xarray / pandas / scipy | 一手 stable 文档；关键行为另查 xarray v2025.01.2、pandas v2.2.3、SciPy v1.14.1 | 文档、固定源码；非本机安装版本证据 |
| Rasterio | 官方 stable 文档、1.4.3 mask.py | 文档、固定源码 |
| exactextract / rasterstats | 官方 operations / API；master 源码 | 未固定提交，置信边界明确 |
| DuckDB spatial | 官方 current GDAL 文档 | 文档；不扩大成所有历史/未来版本 |

cog/raster 的所读仓库提交没有与 community 当前安装制品绑定，故不承诺任意 `INSTALL ... FROM community` 已包含这些全部功能。负面结论限于核查版本注册接口与相关代码，不能推导所有分支/外部组合永远不支持。

## 3. DuckDB TIFF、COG 和 raster

### 3.1 官方 spatial 的 GDAL 不等于栅格读取

官方 GDAL 文档明确：GDAL integration 只支持 vector 驱动，不支持 raster 格式读写。[S1] 因而不能用“spatial 已内置 GDAL”推导 `ST_Read('file.tif')` 支持栅格计算。社区 raster/cog 是独立能力。

### 3.2 raster：tile、cell、足迹统计分别有不同粒度

`RT_Read` 返回一行一个 tile，包含 tile 足迹、中心和每个波段的 datacube BLOB；`RT_ReadCells` 才逐像元输出中心和波段值。[S2][S3]

对 `RT_Read` 的 tile geometry 做 ST_Intersects 是候选 tile 筛选，不代表 tile 内所有像元都属于区域。对 `RT_ReadCells` 点 geometry 做 ST_Covers 则有当前 DuckOMO 同样的小面零点现象。

`RT_Stats(path, band, geometry)` 源码逐像元构造四角，调用 `wrap_geom.Intersects(points)`，匹配后 `stats.Update(v)`。[S4] 它是像元足迹相交，整像元等权，不是交叠面积权重。只有边界接触也可能纳入，这与正面积交叠需要区分。

点值函数 `RT_CoordValue` 用逆仿射变换后 floor，读取 1×1 窗口；它是坐标所在像元访问，不能直接称为任意网格上的球面最近点。所核 API 波段/行列为 0-based；越界可返回用户默认值；点值路径与区域统计的 nodata 行为也不同。[S3][S5]

需警惕空统计：所核 raster 结构可输出零统计字段，应一起检查 valid_count；读取错误/越界默认值也不能都解释成真实观测零。[S4] 这是我们应通过 NULL + 状态避免的歧义。

### 3.3 cog：支持真正 polygon，但仍采用中心纳入

所核 cog 有 bbox 和 WKT Polygon/MultiPolygon（含洞）重载，并非仅 bbox。SQL 上游测试用同 envelope 的 bbox 与凹/带洞 polygon 得到不同计数，证明存在几何筛选。[S6][S7]

像元窗口核心公式是：

```rust
col_min = ((xmin - origin_x) / pixel_x - 0.5).ceil();
col_max = ((xmax - origin_x) / pixel_x - 0.5).floor();
```

然后对中心执行 polygon 测试。[S8] 不含任何中心的小多边形仍可为空。bbox 闭区间有定义，polygon 边界上的中心在其语义文档中没有稳定承诺，不能视为与 ST_Covers 完全相同。

`RS_Value` 固定 level 0、floor 索引，不做双线性插值；越界/nodata 返回 NULL，读取失败报错；区域无有效像元 count=0，其余统计 NULL。区域坐标必须是栅格原生 CRS，不自动重投影。[S7]

批量点/批量 polygon 可共享 tile fetch/decode。`RS_ZonalStats` 的正 `max_pixels` 预算可显式选择 overview 近似；默认 exact level 0。overview 会改变分辨率和统计，不能只作为无语义影响的性能开关。[S6][S9]

## 4. DuckDB Zarr 与多维数组职责

社区 v0.1.3 通过 read_zarr、read_zarr_metadata、read_zarr_groups 暴露 Zarr v2/v3 数组、维度和属性；main 另加 CF 时间解码，不能回填到社区版本。[S10][S11]

关键区别：xarray 的维度约定表示数组如何命名/对齐，不代表具备 xarray 的 .sel/.interp 计算接口。

- `dims` 选择有序维度组，`array_path` 选择数组，均不是目标坐标采样。[S11]
- 无一维坐标数组时合成 0..N-1 索引，这些不是经纬度。辅助二维经纬度、bounds、CRS 属性的存在，不自动建立曲线网格空间计算能力。[S12]
- 主扫描路径没有 nearest、polygon、距离或面积参数。定位和空间计算仍需上层提供。[S11]
- 已实现按投影列跳过无关变量解压；`build_work_units` 则枚举全部 chunk，没有在核查扫描路径看到坐标过滤/窗口剪枝。WHERE 返回很少行不代表读取很少块。[S11][S12]
- main 设计文档中的过滤下推、未来 ATTACH 等不能当成已实现接口。源码注册参数与实际测试优先于目标设计文档。

对我们最有价值的结论：应在读取值之前确定目标源索引和 stencil，不依赖全数组展开后 `ORDER BY distance LIMIT 1`。格式层负责元数据和解码，计算层负责定位与值重建。

## 5. xarray 的 nearest 可以借鉴哪些部分

常规独立一维 PandasIndex 的 `.sel(method='nearest', tolerance=...)` 分别找各轴最近标签。tolerance 使用标签单位，例如度或时间，而不是自动转换为米。无 tolerance 时域外可以选轴端点；最近点的值缺测时不自动寻找更远有效值。[S13][S14]

`.interp` 在目标坐标重建值，域外默认 NaN。它与 .sel 的输出坐标和边界定义不同。通过 SciPy interp1d 后端时，nearest 平局可选较小标签，而 pandas 最近索引偏好较大标签；不能把这两个同名方法的 tie-break 当成统一科学契约。[S15][S16]

本次另外执行了独立球面距离数值对照，说明逐轴最近与地表最近并非恒等：目标 (lon=5°,lat=44.95°)，源经度 [0°,10°]，纬度 [40°,50°]。逐轴最近纬度是 40°，但按 radius=6371229m 的大圆距离，40°行候选约 686.10km，50°行候选约 675.36km，真正距离最近的纬度为 50°。这是稀疏合成网格上的数学示例，不是运行 xarray 的实验，也不是对 GFS 此案例存在差异的断言。

因此，推荐借鉴 `method='nearest'` 的显式选择体验，但另固定 nearest 的距离定义。设计稿推荐地表距离 nearest，并将未来轴标签匹配命名为 axis_nearest；不能先做逐轴匹配，再仅计算距离就称为全局地理最近。

## 6. all_touched 和 exactextract

Rasterio mask 默认是中心/线栅格化二值掩码，all_touched 则纳入所有触及的像元；两者都不是部分覆盖权重。小区域可以有非空读取窗口却得到全掩码。GDAL 默认中心边界行为并非严格 ST_Covers，多个要素烧录同一个像元还涉及覆盖顺序。[S17][S18]

GDAL 的 AREA_OR_POINT 标识像元值按区域还是点解释，但仿射 georeference 仍以像元范围为基础，首像元中心在 (0.5,0.5)。[S19] 这提醒我们从采样点构造中点单元是一个显式支持范围约定，不是 OM 自带的原生面积平均证据。

exactextract 定义 c_i 为单元被面覆盖的比例：[S20]

```text
mean = Σ(value_i × c_i) / Σ(c_i)
weighted_mean = Σ(value_i × c_i × weight_i) / Σ(c_i × weight_i)
count = Σ(c_i)
```

所以 count 可以是小数，不能与原始记录 COUNT(*) 等同。纬度不同的经纬网格单元实际面积不同，默认 coverage fraction 权重不等于物理面积权重。

它提供 coverage_weight=area_cartesian 与 area_spherical_m2/km2 等选项。后者以覆盖比例乘球面整格面积近似，不承诺任意面在椭球上的精确交叠面积。[S20] CRS mismatch 的警告不等于自动重投影；缺测值/缺测权重也需按固定版本测试，不能只凭“忽略 nodata”的摘要选择分母。[S21]

## 7. 对 DuckOMO 的设计决定

本次建议已落实为独立[设计提案](../plans/2026-10-14-spatial-sampling-and-computation.md)：

1. 按用户确认收敛公共入口：沿用 read_om_regions，以 method=points（默认）保留原始选点，method=nearest 和后续 bilinear 表示区域代表点采样。不新增采样或区域权重函数；内部计算核心仍独立复用。
2. 保留原变量名；points 的 lat/lon 为源坐标，nearest/bilinear 的 lat/lon 为代表位置。采样结果用 sampling STRUCT 记录实际源位置/贡献者、方法与地球模型、距离和逐变量状态。
3. 最近点缺测保持 NULL；域外、阈值外、EMPTY、损坏文件分别处理，错误不能改成缺测。
4. 原始点选择、all_touched、fraction、物理面积分别定义；区域平均与 extensive 单元总量的公式不同。
5. 网格坐标可读取不代表已具备逆定位、邻接、单元边界或面积能力，按网格和算子分别开放并验证。
6. 定位/权重先于值读取，源窗口包含区外邻点/halo；不能把目标 polygon 的 bbox 当成插值的全部源窗口。
7. 空间计划可以跨非空间轴组合复用，缺测掩码和有效权重随变量/time/member 重算；未来跨文件复用须验证 grid/layout/support 身份。
8. 复用同一目标采样核心给 Line、时间/垂直插值和重网格；只在第二个真实消费者出现时抽取通用设施，不预建任意计算引擎。

## 8. 验证与缺口

- 外部项目：文档/源码核对，无外部安装、SQL、基准或网络请求统计。source_check 的自动语义评分不可用（unclear），不把它算作自动验证通过。
- 当前项目：匹配 DuckDB v1.5.5 的实际 GFS 文件和本地区界复现，区域与普通读取空间筛选的 18 feature 多重集合一致；源样本与探索性单元结果保存在忽略的 `build/small-polygon-investigation/`。
- 球面数值对照：使用 Python 标准库直接计算大圆距离，验证“轴最近不总等于地表最近”这一设计理由；没有依赖 xarray、SciPy 或新采样实现。
- 本地未安装 xarray/rasterio/exactextract，不声称运行这些工具验证外部行为。
- 未覆盖 DuckDB 所有 NetCDF/HDF5 扩展，也未核验 xESMF；Zarr 与 TIFF 已足以回答本次格式与计算职责问题，不据未研究工具承诺 conservative remapping。
- 后续实现必须固定依赖版本，专门构造亚像元、边界、洞、缺测、越界、反向轴、周期、浮点平局及不同纬度面积 fixture。I/O 与数值正确性分别验收。

## 9. 主要来源索引

- **S1**：[DuckDB GDAL Integration](https://duckdb.org/docs/current/core_extensions/spatial/gdal.html)。官方 vector-only 范围。
- **S2**：[raster 社区目录](https://duckdb.org/community_extensions/extensions/raster)。仓库入口及 tile/cell 描述。
- **S3**：[raster 读取实现](https://github.com/ahuarte47/duckdb-raster/blob/2d980e77adf232c7f5bf046f2b1d755922c99fe5/src/raster/table/raster_read_function.cpp)。
- **S4**：[raster 统计实现](https://github.com/ahuarte47/duckdb-raster/blob/2d980e77adf232c7f5bf046f2b1d755922c99fe5/src/raster/scalar/raster_stats_functions.cpp)。四角相交、统计更新、空统计结构。
- **S5**：[raster 仿射工具](https://github.com/ahuarte47/duckdb-raster/blob/2d980e77adf232c7f5bf046f2b1d755922c99fe5/src/raster/raster_utils.cpp)及[点值实现](https://github.com/ahuarte47/duckdb-raster/blob/2d980e77adf232c7f5bf046f2b1d755922c99fe5/src/raster/scalar/raster_values_functions.cpp)。
- **S6**：[cog 社区目录](https://duckdb.org/community_extensions/extensions/cog)及[SQL 实现](https://github.com/st-layer/duckdb-cog/blob/08c283b50acbf9267b25a59427319cbf15eb3327/src/rs_meta.rs)。
- **S7**：[cog 语义文档](https://github.com/st-layer/duckdb-cog/blob/08c283b50acbf9267b25a59427319cbf15eb3327/docs/sedona-semantics.md)。
- **S8**：[cog pixel engine](https://github.com/st-layer/duckdb-cog/blob/08c283b50acbf9267b25a59427319cbf15eb3327/crates/engine/src/pixel.rs)。中心窗口与区域像元路径。
- **S9**：[cog zonal SQL tests](https://github.com/st-layer/duckdb-cog/blob/08c283b50acbf9267b25a59427319cbf15eb3327/test/sql/rs_zonal.test)。本次未运行。
- **S10**：[zarr 社区目录](https://duckdb.org/community_extensions/extensions/zarr)及[community manifest](https://raw.githubusercontent.com/duckdb/community-extensions/main/extensions/zarr/description.yml)。
- **S11**：[zarr v0.1.3 扫描代码](https://github.com/xqlsystems/duckdb-zarr/blob/d5ae255c4fd639d81e749c562415bc8eaad55ce3/src/read_zarr.rs)及[main 扫描代码](https://github.com/xqlsystems/duckdb-zarr/blob/985396ff5976e5d4341d2afacf243fa9a82f2cbe/src/read_zarr.rs)。
- **S12**：[zarr 元数据与 work units](https://github.com/xqlsystems/duckdb-zarr/blob/985396ff5976e5d4341d2afacf243fa9a82f2cbe/src/zarr_reader/meta.rs)。
- **S13**：[xarray DataArray.sel](https://docs.xarray.dev/en/stable/generated/xarray.DataArray.sel.html)及[固定索引源码](https://github.com/pydata/xarray/blob/v2025.01.2/xarray/core/indexes.py)。
- **S14**：[pandas Index.get_indexer](https://pandas.pydata.org/docs/reference/api/pandas.Index.get_indexer.html)及[v2.2.3 索引代码](https://github.com/pandas-dev/pandas/blob/v2.2.3/pandas/core/indexes/base.py)。
- **S15**：[xarray DataArray.interp](https://docs.xarray.dev/en/stable/generated/xarray.DataArray.interp.html)。
- **S16**：[SciPy interp1d](https://docs.scipy.org/doc/scipy/reference/generated/scipy.interpolate.interp1d.html)及[v1.14.1 代码](https://github.com/scipy/scipy/blob/v1.14.1/scipy/interpolate/_interpolate.py)。
- **S17**：[Rasterio mask](https://rasterio.readthedocs.io/en/stable/api/rasterio.mask.html)及[1.4.3 mask.py](https://github.com/rasterio/rasterio/blob/1.4.3/rasterio/mask.py)。
- **S18**：[GDAL gdal_rasterize](https://gdal.org/en/stable/programs/gdal_rasterize.html)。
- **S19**：[GDAL Raster Data Model](https://gdal.org/en/stable/user/raster_data_model.html)。
- **S20**：[exactextract operations](https://isciences.github.io/exactextract/operations.html)。公式与 coverage_weight。
- **S21**：[exactextract raster_stats.h](https://github.com/isciences/exactextract/blob/master/src/raster_stats.h)、[Python API](https://isciences.github.io/exactextract/exactextract.html)及[rasterstats 代码](https://github.com/perrygeo/python-rasterstats/blob/master/src/rasterstats/main.py)。浮动分支作为受限证据。
- **S22**：[Rasterio windowed reading](https://rasterio.readthedocs.io/en/stable/topics/windowed-rw.html)。1×1 窗口仍有块级读取成本。

## 10. 子代理证据索引

工作流 `f5c744e9-8fa4-4b0d-9cff-e26a5e50a3ee`；三路均成功，无仓库写入。详细报告由运行系统管理保留，本文保存长期可用的来源和综合结论。

- raster/COG：child `5f651a40-c0eb-4b47-9ef4-3c0da8d2633a`。
- Zarr：child `d31fe21b-0efb-40f4-8516-fb8f784a7dcd`。
- 计算语义：child `fe575d21-8d84-425c-aee8-90bf457409c3`。

报告不是运行实验凭证，子代理完成也不是外部功能已安装或本项目新设计已实现的证明。
