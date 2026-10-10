# SQL Interface Contract: 多网格声明、描述与源位置

> 当前开发版修订：SQL 地理列统一为 `lat/lon`；`dimensions` 支持共享 `VARCHAR[]` 或完整逐变量 MAP；远程错误输出脱敏类别与建议。迁移及完整规则见 [读取接口修订](../../../docs/interface-migration.md)。历史 evidence 仍对应原构建，不因本修订提升验收状态。

日期：2026-10-03；2026-10-06 与当前工作树实现核对。该文件是 004 的接口约束；version=1 grid、`include_source` 和 `om_grid_info` 已实现，但真实新网格验收、远程收益和版本矩阵仍未完成。继承 [002 SQL](../../002-spatial-pushdown/contracts/sql-interface.md) 与 [003 SQL](../../003-dimensions-remote-parallel/contracts/sql-interface.md) 的值/轴/缺测/类型及远程约束。

## 2026-10-10 O1280 登记与验收边界

`domain := 'ecmwf_ifs'` 登记完整 O1280 2560 行/6,599,680 点，使用一个展平空间轴。无坐标元数据时保留 `point` 声明（静态 `['y','point']`，时间序列 `['y','point','time']`）；真实冻结 HSURF 内嵌 `lat lon` 轴名，domain 明确登记 `flattened_axis_alias='lon'`，可保留推断轴直接读取或显式声明 `['lat','lon']`，不能用 `['y','point']` 覆盖。显式 grid 等价查询的 HSURF `spatial_axes := ['lon']`、chunk_817 则为 `['point']`。其他原始轴均保留位置；沿用完整行表与 f32 策略，不按 shape/路径猜轴。新增真实本地执行结果见 [O1280 记录](../evidence/baseline-local/o1280-real-local-20261010/final.md)，不替代独立原始 GRIB 点序和完整门禁。

[负责人批准的范围修订](gaussian-acceptance-20261010.md) 仅跳过本轮 N-grid 真实样本验收；O1280 与三个投影定义当前 coordinate-value-validated（见 [pinned producer 参考决定](pinned-producer-reference-20261010.md)），native/合成回归只是实现回归。当前 `read_om` 仍只接受单对象，Phase 8 series 只是待评审提案。

## 读取入口与兼容

```text
read_om(path, dimensions := NULL, grid := NULL, spatial_axes := NULL,
        domain := NULL, valid_times := NULL, axes := NULL,
        include_source BOOLEAN := false)
```

新增 include_source 必须为绑定期非 NULL BOOLEAN；其他参数显式 NULL 沿用旧规则。legacy 七字段 grid 原样接受；新 grid 依 version/type 严格验证实际 STRUCT，字段名/枚举区分大小写，拒绝未知、缺失、重复、内部 NULL 及有损轴整数转型。新结构仅 `subset_segments` 允许显式 NULL 表示全域。不能向旧结构随意添加 earth 字段。

grid/domain 互斥；显式 grid 必须提供 spatial_axes。新 domain 固定其适用轴 profile，不能用 spatial_axes 覆盖；dimensions 支持共享 VARCHAR[] 或完整逐变量 MAP，可补缺失全部轴身份，但不能覆盖文件已有不同证据。所有变量必须布局相容，空间轴不能再映射 time/level/member 等语义。无 grid/domain 的默认值读取不变；include_source 需要绑定网格，否则拒绝。read_om_raw 不扩展新接口。

## 新 version=1 grid

公共字段精确为 `version, type, numeric_policy, earth, layout, parameters`。version 为精确整数 1；numeric_policy 为 `float64_v1` 或 `openmeteo_f32_v1`。投影/旋转earth精确包含 `model='sphere', radius_m`，半径有限且为正。Gaussian可用此球面声明，或封闭来源描述`{'model':'wgs84','semi_major_m':6378137.0,'inverse_flattening':298.257223563}`，字段和值均校验；固定生产者Gaussian WKT使用后一模型。它只描述已有行坐标的来源，不引入任意椭球投影、datum转换或按椭球重算行纬度。长度单位统一m、角度统一degrees；球面来源不隐式宣称EPSG:4326 datum。

投影/旋转 layout 精确包含 nx、ny、order；order=`separate` 对应 spatial_axes=[native_y_axis,native_x_axis]，或 `x_fastest`/`y_fastest` 对应一个展平轴。nx/ny 正整数，空间点数 nx*ny 受检。native 原点和步长按 parameters 的含义求值，允许负方向但不能零步长。与源布局不同必须拒绝，不能隐式转置。

| type | parameters 精确字段 | 坐标语义 |
| --- | --- | --- |
| rotated_latlon | x0,y0,dx,dy,north_pole_latitude,north_pole_longitude,rotation | x/y 为旋转球面经纬度角度；北极约定，rotation 为绕旋转极的角度偏移；源南极约定由生成器显式规范化 |
| lambert_conformal_conic | x0,y0,dx,dy,central_meridian,latitude_of_origin,standard_parallel_1,standard_parallel_2 | x/y 为投影米坐标；投影原点处 x=y=0，无隐式 false easting/northing；原生第一个点为 x0/y0 |
| stereographic | x0,y0,dx,dy,central_meridian,latitude_of_origin,scale_factor | 球面中心投影米坐标，scale_factor>0；来源标准纬线规则须转换为等价显式比例因子，并核对尺度 |

每个角度/参数的有效域在内核 contract vectors 固定；无效、退化、无法输出有限地理坐标的组合在返回数据前拒绝。Lambert 等标准纬线是受支持单纬线特例；stereographic 原点采用受检解析极限。数值策略规范固定在生成输入和参考 manifest 中，不能依默认浮点隐式改变。

version=1固定各网格族的coordinate_rule_id，不接受任意自定义数学；numeric_policy选择同一数学的算术路径。rotated的北极约定如下：北极(φp,λp)对应三维单位向量`p=(cosφp cosλp,cosφp sinλp,sinφp)`；`ex=(sinφp cosλp,sinφp sinλp,-cosφp)`、`ey=(-sinλp,cosλp,0)`。原生角度`φ'=y0+j*dy`、`λ'=x0+i*dx+rotation`；地理向量`v=cosφ'cosλ'*ex+cosφ'sinλ'*ey+sinφ'*p`，再求地理latitude/longitude并规范化。rotation的正方向为此右手basis中native longitude增加，先加到native x再变换，dx符号表示该native方向；φp和全部原生φ'须在[-90,90]。φp=90、λp=0、rotation=0为数学identity。地理极点处经度无几何唯一性，内核固定返回0，oracle按此约定核对。

固定Open-Meteo inverse的`geo_lon=ϕ-atan2(...)`不能直接当作另一种`+atan2`实现。其参数`θ=(90+source_latitude)*pi/180, ϕ=source_longitude*pi/180`与本basis数学等价的规范映射为φp=source_latitude、λp=source_longitude、rotation=180°，原x0/dx及y0/dy保留；不能只按“南极/北极”字样机械变号。openmeteo_f32_v1在该规范输入上恢复`source_x=native_x+(rotation-180)`（offset为0时直接用native_x），执行固定源θ/ϕ及asin/atan2逐步表达式，保持原点/角度构造顺序；float64_v1使用上面的basis定义。转换及奇点约定必须由独立源/矩阵参考验证，不能用数学等价重结合宣称f32位模式相同。

Lambert为球面标准conformal-conic；φ1=φ2时`n=sinφ1`，否则采用两标准纬线的log-ratio；n=0、标准纬线极点及使坐标不有限的参数拒绝。stereographic使用球面中心投影、中心比例scale_factor，中心距离ρ=0返回声明中心；必须明确源尺度与此参数的等价换算。source-compatible策略按固定source profile规定运算顺序，f32近似和libm差异在验收前验证，不把换算误差视为可随意改写输出的依据。

### reduced_gaussian

layout 精确为 `{'order':'row_major'}`，spatial_axes 只有一个 point 轴。parameters 精确包含 `n,latitude_rule,rows,subset_segments`；n 正整数、rows 长度 2*n。latitude_rule 为 `openmeteo_approx_v1` / `legendre_roots_v1` / `explicit_v1`，其名声明来源，不替代实际行表；登记 N160/N320 和 O1280 固定为核对的生产者规则；相同 n 不代表相同行长或定义身份。

rows 为非空 STRUCT 列表，每项精确包含 `latitude,point_count,longitude_origin,longitude_step`，point_count 正整数。按给定行序连接点，先执行 numeric_policy 再 normalize longitude 到 [-180,180)。必须用全部行表和规则确定身份，不能仅靠 n 生成未提供行长。legendre_roots_v1 必须有独立根参考与固定容差验证，不覆盖生产者近似行表。

subset_segments=NULL 为完整网格；区域必须给非空 STRUCT 列表，各项精确含 `parent_row,parent_begin,count`，零起、在 parent 行内，列表顺序就是对象局部点序。跨经线行可拆为行尾及行首两段；不重排经纬度、不补矩形、不允许重复 parent 源点。实际 point 轴长为段 count 累加，区域 BBOX 不能替代这个列表。

## 默认输出与 om_source

默认列顺序沿用 003：值 → lat/lon → 启用的语义列。地理两列为非 NULL DOUBLE、有限，lat∈[-90,90]、lon∈[-180,180)。旧规则 domain 的已公布例外保留。

include_source=true 时仅在最后追加 `om_source STRUCT`：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| object_id | VARCHAR | 脱敏 opaque 对象身份；不公开带凭据 URI |
| object_version | VARCHAR nullable | 可用版本证据，缺失时 NULL |
| version_strength | VARCHAR | strong / weak / unverifiable，不冒充内容验证 |
| content_verified | BOOLEAN | 本次实际验证 manifest 内容 hash 才能 true |
| grid_id / layout_id | VARCHAR | canonical SHA-256 定义/绑定布局身份 |
| logical_index | UBIGINT | 原始数组逻辑位置，row-major 原始 strides |
| point_index | UBIGINT | 对象局部空间点位置 |
| parent_point_index | UBIGINT | 完整空间定义中的点位置 |
| axis_indices | UBIGINT[] | 全部原始轴的零起位置，含非空间轴 |

source 列也参与名称冲突校验及 projection pruning；请求 source 不创建值 decoder。source 信息对排序/过滤/并行稳定，不保证无 ORDER BY 的行序。源位置不能由经纬度去重恢复。

## om_grid_info

```text
om_grid_info(path, dimensions := NULL, grid := NULL, spatial_axes := NULL,
             domain := NULL, valid_times := NULL, axes := NULL)
```

要求有效 grid/domain，复用读取的全变量 metadata/CRS/轴 binder；输出一行，不读取值 LUT/data 或解码值，必要 coordinate evidence 单列计量。默认扫描不自动调用该函数。输出列：

| 列 | 类型 | 内容 |
| --- | --- | --- |
| descriptor_version | INTEGER | 1 |
| grid_id / parent_grid_id | VARCHAR / VARCHAR nullable | 区域与完整定义身份 |
| grid_type | VARCHAR | 公开网格族 |
| definition / layout / crs / capabilities / provenance | JSON | 完整规范定义、原始轴/strides/局部映射、earth/native/输出单位和参考系、能力、固定来源及覆盖等级 |
| object_id / object_version / version_strength | VARCHAR / VARCHAR nullable / VARCHAR | 与 source 相同对象口径 |
| content_verified | BOOLEAN | 与 source 相同证据口径 |

definition 包含完整 parameters/rows/subset_segments，而非摘要；provenance 与身份哈希分离。capabilities 的 point_semantics=`native_point_sample`；每项 adjacency/cell_boundary/area/distance/vector_orientation 给 `defined` / `unsupported` / `unknown` 及解释。仅已核对周期性接缝可 defined；Gaussian 不伪造矩形邻接。没有单元面积/边界证据不能标 defined，扫描不执行插值/regrid/向量旋转。

描述接口的 QueryEnd/metrics 隔离和读取入口一致，显示 operation=grid_info；不把描述成功视为新网格值扫描验收。

## 地理条件与示例

支持有限常量比较、BETWEEN、安全 AND 和下列两经度区间的完整安全 OR：

```sql
WHERE lat BETWEEN -10 AND 10
  AND (lon BETWEEN 170 AND 179.9 OR lon BETWEEN -180 AND -170)
```

按实际输出 DOUBLE 和开闭边界比较；普通反向 BETWEEN 仍为空。任一 OR 分支不能证明安全则整个 OR 回退，仍可使用独立安全 latitude 条件。完整 WHERE 由 DuckDB 精确执行。复杂函数、CAST、参数、未知投影界或预算超限均有具体回退原因。

以下小样本在实施期验证 rotated 网格的基本接口；它给没有地理元数据的原始 [2,3] 数组明确赋予坐标，不是生产 domain 样本：

```sql
SELECT value, lat, lon, om_source.logical_index
FROM read_om('test/data/raw.om',
  dimensions := ['y','x'],
  grid := {'version':1,'type':'rotated_latlon','numeric_policy':'float64_v1',
           'earth':{'model':'sphere','radius_m':6371229.0},
           'layout':{'nx':3,'ny':2,'order':'separate'},
           'parameters':{'x0':0.0,'y0':0.0,'dx':1.0,'dy':1.0,
                         'north_pole_latitude':39.25,'north_pole_longitude':-162.0,
                         'rotation':0.0}},
  spatial_axes := ['y','x'], include_source := true)
ORDER BY om_source.logical_index;
```

预期 logical_index=0…5，value=0…5；坐标用固定独立旋转参考比较，不能把本实现输出再作为 oracle。其他类型的生产查询由固定 manifest 生成 `sample-queries.sql`，包含完整显式定义、等价 domain、轴及区域条件。

## 证据冲突和错误

shape/轴/区域点数/参数/已识别 CRS 冲突在返回行前失败；错误定位字段或变量，不泄露 secrets。新网格存在不能核对的相关 CRS 时拒绝地理绑定，不忽略声明。固定源 WKT profile 只解析可证实字段，BBOX 不证明投影覆盖；任意 WKT/椭球自动识别不在此期范围。远程短读、范围异常、权限/对象变化、取消和配套能力不匹配沿用 [003 remote](../../003-dimensions-remote-parallel/contracts/remote-io.md)，不得静默全下载或成功返回残缺结果。
