# Numeric and grid-capability contract

状态：006 当前数值与能力契约，已实现并在 [最终验收声明](../evidence/final.md) 所列有限范围验证。准确源索引、域判断和平局须保持本契约；不能以点坐标容差改变输入几何或默认 points 模式，声明范围外不自动继承验收。

## 支持资格

输入已完成既有grid/domain、布局和shape校验，nearest再检查：

1. geometry为RegularGrid；identity_definition.earth.kind=Sphere，radius_m有限且>0。legacy显式grid采用其现有identity_definition默认球体，不推断任意外部CRS。
2. nx/ny正整数、有限非零步长，乘积和逻辑记录受既有signed-64上界保护；原始latitude轴端点在[-90,90]且序列严格单调、不坍缩；allows_out_of_range_latitude=true拒绝。
3. 使用现有RegularGrid::Coordinate产生的binary64坐标，不改成producer投影Float32规则或另一个规范化公式。原始经纬轴运算若相邻值因极端origin/step或index超出精确double整数域导致重复，拒绝能力。资格校验以流式逐轴检查保证唯一/单调，O(nx+ny)时间、O(1)空间，可取消。
4. 经度区分完整周期与非周期，二者都要求无重复规范化经度、unwrapped原始序列有限且严格单调。一圈以上、多重wrap、步长模360为0且nx>1、跨度≥360（非周期）拒绝。
5. grid definition version必须为1，earth.kind必须为Sphere且radius_m有限正值，geometry必须是RegularGrid，out-of-range latitude policy必须为false；否则以[sampling_capability]拒绝。
6. capability对象记录来源、范围规则、坐标规则与periodicity；不把“登记domain”或“可读取”当成nearest已验收。本次验收声明限定为合成规则球面网格和ncep_gfs025；其它domain不纳入006逐profile验收声明。运行时仍按同一资格条件处理显式grid/domain，登记或读取成功本身不构成该domain的验收证据。

### 周期与覆盖

- 周期经度：nx≥2，binary64计算的nx×abs(dlon)恰为360.0，原始首末跨度<360，规范化经度互不重复。该等式是版本化资格条件，不是几何容差。不满足该精确条件时仅能按合法非周期区域闭域处理，不能因为“看起来接近360”升级为周期；若注册profile声明全球周期而参数不满足则能力拒绝。
- 完全球面覆盖：周期经度且latitude端点分别恰为-90与+90；合法目标纬度仍受005约束严格处于(-90,90)，源极点允许。
- 周期经度、局部纬度的规则网格：经度全周期，纬度使用原始端点闭域，能力记录为latitude_band，不冒充全球域。
- 非周期经度：在原始unwrapped经度轴端点区间检查目标。对规范化目标lon寻找整数k使lon+360k落入闭区间；区间跨度<360保证至多一个合法lift（端点重复资格已排除）。没有lift为域外。纬度使用端点闭区间，不扩半格。
- 单行/单列可以是退化闭域；代表位置须正好落在固定坐标上，否则域外。不创造半格面积范围。
- 覆盖检查先于距离定位。outside_domain无源候选；out_of_bounds=error使整个绑定失败。区域合法性不因目标落在全球域就放宽。

## 距离核 sphere_haversine_f64_v1

坐标角度转弧度使用固定binary64常量pi（最近可表示值）；earth_radius来自当前grid identity。按以下次序显式求中间值，关闭fast-math和FMA contraction：

```text
phi_t = target_lat * (pi / 180)
phi_s = source_lat * (pi / 180)
delta_phi = phi_s - phi_t
delta_lon_degrees = remainder(source_lon - target_lon, 360)
delta_lambda = delta_lon_degrees * (pi / 180)
s1 = sin(delta_phi / 2)
s2 = sin(delta_lambda / 2)
a = (s1*s1) + ((cos(phi_t)*cos(phi_s))*(s2*s2))
a = clamp(a, 0, 1)
c = 2*atan2(sqrt(a), sqrt(1-a))
distance_m = earth_radius_m*c
```

特殊分支：

- 同一源/目标lat和规范化lon相同，距离=0.0；正负0等同。
- 源lat=+90：c=(90-target_lat)*(pi/180)；源lat=-90：c=(90+target_lat)*(pi/180)。不运行cos(pi/2)，所有同极点经度距离一致。
- distance计算须有限；异常数值为能力/计算错误，不产生随机winner。

以返回的distance_m binary64值升序，再以最小原始point_index升序选择。半径乘法后的舍入可能产生平局，比较的是distance_m而非未乘半径的中心角；测试要覆盖这一区别。不采用epsilon等同、容差夹取或对小差距“选大坐标”。

源point_index复用SpatialLayout既有规则：lat_fastest为x*ny+y，其它规则空间序为y*nx+x；不能将文件中lat/lon轴排列误当空间point顺序。parent_point_index在规则网格等于point_index；完整logical_index必须再加入非空间轴位置。

不同libm/编译平台不能默许所有近等距距离位模式一致。006先冻结Linux AArch64与匹配工具链的算法/构建身份；同一平台与构建规则下线程结果须一致，跨DuckDB版本使用同一距离实现。其它平台单独验证近等距参考后再扩展声明。

## 初版正确性路径与优化门槛

首版locator流式遍历全部源(y,x)并调用上述坐标/距离规则，持有当前最优一个候选。输入≤4096区域，各区域只定位一次，复用所有非空间轴/变量。资格检查与定位都不读取值或持有全域坐标表。每≤256个候选检查取消，坐标比较数与定位耗时独立计量。

定位复杂度O(R×nx×ny)，内存O(R)+O(rank)；ncep_gfs025每目标1,038,240个空间位置。该profile与合成规则球面网格构成本次N1/N2验收范围；其它登记domain不要求逐一测试或在验收声明中列为已验收。此CPU成本公开记录，4096目标可能昂贵，不能承诺固定延迟。批量候选优化不属于验收捷径：实现者可增加按行候选/剪枝，须证明相同f64距离平局下不会漏掉更小point_index并对冻结参考全过，否则用全坐标基线。

已知风险：

- 数学上一个纬度行的最近经度可由圆周距离定位，但浮点距离舍入可能使多个更远经度与最优值相等；只检查floor/ceil无法自动满足最小point_index。
- 极点/接近极点和接近对跖点对公式精度、平局和候选界特别敏感。
- 纬度行剪枝需要可审计的下界与舍入误差处理；数学下界不是自动成立的浮点下界。

不以任意容差修复这些问题。可以优化坐标生成/批内复用，不改变值/距离语义；不新增全域KD-tree或跨查询索引缓存。

## 独立数值参考

1. 开发期Python或单独C++参考生成器独立枚举，不能导入生产locator/selection或直接调用其distance函数。
2. compatibility oracle：独立表达相同binary64公式、极点及point索引排序；验证winner和阈值结论精确相同。临界tie若libm差异无法冻结，显式记录平台与参考，不用宽容差放过源索引错误。
3. scientific oracle：高精度球面公式验证距离合理性。普通固定局部目标distance误差≤max(1e-6m, reference_distance×1e-12)。接近对跖点/极点采用独立分类与预先冻结的更宽误差界≤2m；超出则修订数值核并提升算法版本，不能测试后放宽不留记录。
4. 相等阈值测试使用已返回distance_m作为同绑定下一次输入，0阈值用精确网格点；阈值两侧用nextafter，非十进制打印截断制造边界误判。
5. 固定目标：原网格点、四点对称平局、只在纬度轴上较近但球面较远反例、近等距、全球seam目标、源极点、双方向步长和两种展平序。矩形polygon中心为已知目标，另测复杂面内点合法性但不强求跨spatial版本同点。

高精度仅用于开发/验收，查询运行时不依赖Python。该数值契约不实现椭球、插值、面积或最近有效值搜索。
