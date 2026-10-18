# SQL contract: read_om_regions 的 points / nearest

状态：006 已实现 points/nearest 接口；points/nearest 的本地验收边界见 [006 evidence](../evidence/)，points 原有约束见 [005 契约](../../005-polygon-region-statistics/contracts/sql-interface.md)。本文件不注册新函数，不改变 read_om 的参数。

## 入口与参数

```text
read_om_regions(
  path VARCHAR,
  regions := STRUCT(id VARCHAR,wkb BLOB)[], region_crs := 'grid_lonlat',
  method := 'points', max_distance_m := <省略>, out_of_bounds := <省略>,
  dimensions := NULL, grid := NULL, spatial_axes := NULL, domain := NULL,
  valid_times := NULL, axes := NULL, include_source := false, variable := <省略>
)
```

- method：省略为 points；仅接受大小写精确的非NULL VARCHAR points/nearest。bilinear等未实现值绑定拒绝，不回退。使用ANY注册后严格验证实际类型，不能用数字/隐式cast冒充方法。
- max_distance_m：nearest-only；省略无额外阈值。显式参数接受能无损或常规数值转换为有限非负DOUBLE的数值类型，拒绝NULL、字符串、BOOLEAN、NaN、Inf或负数。最终比较 `distance_m <= max_distance_m`，0允许距离恰为0的匹配。
- out_of_bounds：nearest-only；省略固定为null，显式只接受非NULL VARCHAR null/error。只控制几何代表位置域外；不控制source_null、EMPTY、距离超限或I/O错误。
- points显式携带任一nearest-only参数，即使等于默认值，也拒绝。method=nearest不要求region_mode、representative、distance_metric或missing；这些未注册选项按未知参数拒绝。
- 所有参数绑定时确定，支持常量表达式/getvariable快照。既有path、domain/grid互斥、dimensions、axes、variable、include_source语义沿用；nearest省略variable仍绑定全部兼容FLOAT数组。
- 验证完整输入及文件声明后才可短路。空集合零行，仍检查method、网格能力、spatial依赖和原有参数。NULL集合/非法区域整体错误。
- 同一对象只有一个读取入口；既有read_om仍用于普通网格/矩形坐标筛选；已有polygon及区域方法使用read_om_regions。

## 输入几何与能力

沿用005受检2D标准WKB、唯一ID、grid_lonlat、局部经纬平面边、接缝/极区和预算规则。不能因nearest只使用代表点就放宽polygon合法性。有效EMPTY允许；无业务意义的“自动修复”或简化不执行。

nearest生成一个ST_PointOnSurface位置，再检查ST_Covers；点有限且合法，否则报[region_sampling]错误。对MultiPolygon取整体一个位置，不按部件输出。记录实际代表位置和spatial版本；不承诺跨版本内点相同。

本期仅规则经纬度、合法球面地球定义，坐标与布局均已绑定。固定radius来源：已有identity_definition的earth.radius_m；legacy explicit grid及规则domain当前为6371229m，沿用既有定义而非额外宣称生产者物理球体。WGS84椭球、投影、Gaussian、不规则、超纬度、重复wrap或无法证明资格的网格报[sampling_capability]，不影响points对这些网格的原支持。本次逐profile验收范围为合成规则球面网格和ncep_gfs025；其它登记domain不纳入验收声明。

周期/覆盖、距离与平局的准确规则见[numeric contract](numeric-and-capability.md)。nearest源点可在面及包围框外。整个集合先完成定位检查；out_of_bounds=error时任一区域域外均使绑定失败，不被WHERE、LIMIT或后续空轴条件隐藏。

## 输出schema

### points

与005完全相同：值FLOAT列→lat/lon DOUBLE→已声明时间/语义轴→region_id VARCHAR NOT NULL→可选om_source。省略method与显式points的DESCRIBE及原始结果多重集合一致。无点/EMPTY均无原始行；不存在sampling保留名。

### nearest

值列仍沿用原变量名与FLOAT类型，其后lat/lon、原有语义轴、region_id；新增sampling，再追加可选原om_source。sampling为非NULL STRUCT，顶层lat/lon是区域代表位置（EMPTY时NULL），不是真实源坐标。

```text
sampling STRUCT(
  method VARCHAR,
  algorithm VARCHAR,
  representative VARCHAR,
  spatial_version VARCHAR,
  grid_id VARCHAR,
  layout_id VARCHAR,
  distance_model VARCHAR,
  earth_radius_m DOUBLE,
  status VARCHAR,
  source_lon DOUBLE,
  source_lat DOUBLE,
  source_point_index UBIGINT,
  distance_m DOUBLE,
  axis_indices UBIGINT[],
  variables MAP(VARCHAR, STRUCT(
    status VARCHAR, source_count UBIGINT, valid_source_count UBIGINT
  ))
)
```

- method=nearest，algorithm=sphere_haversine_f64_v1，representative=point_on_surface_v1，distance_model=grid_sphere；spatial_version为实际已加载空间扩展的非空extension_version。其它依赖hash在验收manifest，不暴露私有GEOS字段。
- grid_id/layout_id复用既有规范身份，不因本算子添加字段而改写grid身份；算法不同由algorithm区分。
- status只描述几何定位：ok、empty_geometry、outside_domain、beyond_max_distance。
- source_lon/lat/point_index/distance_m：ok及beyond_max_distance有值；EMPTY/域外无候选则NULL。source_point_index是原始空间点索引，不能混用完整logical_index。
- axis_indices为原文件顺序去除空间轴后的完整位置列表，始终非NULL；无非空间轴时为[]，保留未映射轴及重复标签的不同索引。
- variables包含全部当前绑定变量，key是准确原SQL列名（编码路径规则不变），value非NULL。ok几何下变量status为ok/source_null，source_count=1，valid_source_count=1/0；其它几何状态下变量status等于几何status，source_count=0、valid_source_count=0。beyond_max_distance有候选但不算已采样贡献。
- ±Inf不是source_null；正常返回原值并valid_source_count=1。只做nearest、不进行新数值组合。
- include_source=true时om_source类型与字段顺序沿用005；仅几何ok有真实源记录（即使值缺测），否则整个STRUCT为NULL。om_source.logical_index/axis_indices包含真实空间与非空间位置。完整来源详情已在该列，本期不再复制sampling.sources。
- nearest专用sampling与原值/语义列发生DuckDB大小写无关冲突时绑定拒绝，即使未投影；points不预留此名称。
- 同一绑定下schema固定；切换method可能改变schema，PREPARE/EXECUTE沿用SupportStatementCache=false和显式rebind测试，不能用过期schema执行。

## 行数、状态及筛选

一个区域×一个原始非空间轴索引组合恰好一行。EMPTY、域外、超距也保留这类计算结果；没有原始源记录也能输出原始语义轴坐标。空区域集合为零行。非空间轴不按标签合并，未声明时间不输出valid_time、不从路径推断。

| 条件 | sampling.status | 变量status | 值 | 代表坐标 | 候选源坐标/距离 | om_source |
| --- | --- | --- | --- | --- | --- | --- |
| 有源值 | ok | ok | 原FLOAT | 有 | 有 | 真实记录，若启用 |
| 最近源缺测 | ok | source_null | NULL | 有 | 有 | 真实记录，若启用 |
| EMPTY | empty_geometry | empty_geometry | NULL | NULL | NULL | NULL |
| 域外默认策略 | outside_domain | outside_domain | NULL | 有 | NULL | NULL |
| 超最大距离 | beyond_max_distance | beyond_max_distance | NULL | 有 | 有 | NULL |

COUNT(*)计结果行，不计区内采样点；COUNT(变量)计非NULL结果。缺测不找更远点，不在无点时自动选择nearest。nearest结果不能命名成区域面积均值；按区域AVG(time/member)是用户显式的新聚合。

所有WHERE保留完整residual。nearest的lat/lon谓词作用于目标位置，值谓词作用于最终源值；禁止将polygon envelope或目标坐标条件裁到源网格。安全非空间轴必要条件可下推，但不能破坏原轴位置与标签。sampling几何字段/变量状态有不同值依赖，见[execution contract](execution-and-metrics.md)。

## 错误类别

| 类别 | 情况 |
| --- | --- |
| [region_method] | 非法/NULL/未知method、策略类型/枚举、points携带采样参数 |
| [region_sampling] | 内点生成、坐标或覆盖检查失败，非法距离，strict域外 |
| [sampling_capability] | 网格几何/地球/周期/数值资格不在支持范围 |
| 原[region_input]/[region_reference]/[region_input_limit] | 沿用005合法性/参考与容量错误 |
| 原[spatial_dependency] | 匹配空间包未加载或准确签名不存在 |
| 原OM/远程错误 | 权限、损坏、短读、对象变化、取消等，不转换为结果状态 |

错误不输出完整WKB、凭据或敏感URI。输入合法但nearest没有采样值属于上表状态，基础读取错误不属于缺测。

## 兼容与后续

普通read_om、raw、grid_info不接受这些区域method参数。points没有schema/新依赖变化。nearest-only要求的空间函数不会提升points的依赖门槛。sampling.units/布局与method始终可解释；未来bilinear/area/Line必须先有独立契约再注册，不能在006偷偷接受这些值。
