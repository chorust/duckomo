# SQL Contract: read_om_regions

状态：当前工作树已实现的开发版接口，验收证据按各记录的源码与产物身份限定。继承当前 `read_om` 的 OM/轴/值/授权语义，不继承被官方 HTTPFS 修订替代的旧缓存接口。

## 入口

```text
read_om_regions(
  path VARCHAR,
  regions := STRUCT(id VARCHAR, wkb BLOB)[],
  region_crs := 'grid_lonlat',
  dimensions := NULL, grid := NULL, spatial_axes := NULL, domain := NULL,
  valid_times := NULL, axes := NULL, include_source := false, variable := <省略>
)
```

- `variable` 省略时绑定全部值数组；显式非空 VARCHAR 按编码 SQL 列名或前导 `/` 的规范路径选择一个值数组，名称区分大小写。根数组接受 `value` 或 `/`；NULL、空串、不存在或非值节点拒绝。`dimensions` MAP 只覆盖所选数组。`read_om` 和 `om_grid_info` 提供相同选择语义。嵌套名称中的 `/`、`%` 字节分别编码为 `%2F`、`%25`。
- 单个本地、HTTP(S) 或 S3 对象。`regions`、`region_crs` 必填且非 NULL，无隐式默认 CRS。
- 所有参数绑定时确定；支持常量表达式、`getvariable` 快照，不支持相关参数、直接传关系/表名/SQL 字符串、扫描过程中变化的区域输入。
- `regions` 注册为 ANY 后按实际值严格检查：LIST 的 STRUCT 元素恰含 `id VARCHAR`、`wkb BLOB`，字段名大小写精确，字段顺序无关；未知/重复字段、NULL 元素或字段拒绝，不隐式转 ID 类型、不把 WKT 字符串当 WKB。
- ID 为 1–256 字节非空 UTF-8 文本，按原始字节唯一，不 trim、不大小写折叠。重复 ID 整体拒绝，即使几何相同。业务数值 ID 由用户显式转 VARCHAR。
- 空集合必须显式类型，例如 `[]::STRUCT(id VARCHAR, wkb BLOB)[]`；NULL 列表不等同空集合。
- `region_crs` 只接受大小写精确的 `grid_lonlat`：调用者声明区域已经处于与该网格输出相同的经纬度参考、x=lon/y=lat。不是 EPSG 自动识别、重投影或 datum 等价验证。WKB 的来源坐标系由调用者负责，系统不能从数字证明其真实性。
- 必须有合法 `grid` 或 `domain`。二者互斥及完整轴声明等规则不变。没有时间证据时不输出或猜测 valid_time。
- `include_source` 省略为 false，显式 NULL 拒绝；true 时复用原始 om_source。原有参数的显式 NULL 行为保持不变。

## 几何输入

使用 spatial 的 `ST_AsWKB(geom)` 生成标准 2D WKB。允许标准 Polygon(type=3)、MultiPolygon(type=6) 及任一 endian。只允许 2D，不接受 Z/M、EWKB/SRID flag、尾随字节或非面集合。截断、非法计数、未闭合环、非有限坐标在第三方解析前拒绝；随后 ST_IsValid 必须为 true。禁止自动 repair、simplify 或 transform。

有效 EMPTY 允许且匹配零点。MultiPolygon 按整体几何判断，不拆部件输出；非法重叠部件不静默去重修复。经度 [-180,180]、纬度严格 (-90,90)，非空区域整体经度跨度 <180°；逐环相邻点（含闭合边）的经度跳变 <180°。边解释为经纬度平面直线，不解释为大圆或球面内部。

+180 与 -180 不自动等同，不按周期 wrap；需要接缝/极区语义的区域首版拒绝或由调用者显式拆分成不同 ID 的局部区域。网格坐标保持既有 [-180,180) 输出规范。

边界策略：`covers(region, sample_point)`。外环和洞边界包含、洞内部排除；不得用坐标 oracle 容差扩大区域。Whole envelope 只是候选范围，不是最终选点。

输入容量硬限制：最多 4096 区域；单 WKB≤1 MiB、总 WKB≤16 MiB；单区域≤65536 顶点、全集合≤1048576 顶点（含重复闭合点），超限使用 `[region_input_limit]` 拒绝，不静默截断。更大集合由用户明确拆查询，首版不自动批处理。

## 输出与统计

- 默认输出为对应 `read_om` 在 `include_source=false` 下的所有列，末尾追加 `region_id VARCHAR NOT NULL`；若 `include_source=true`，最后再追加既有 `om_source STRUCT`。
- `region_id` 与任何原有值列/语义列按 DuckDB 名称比较规则冲突时绑定拒绝，即使该值列不在最终投影中。原 `om_source` 冲突规则不变。
- 每个区域—原始逻辑记录至多一行。不同区域间允许同源位置重复；不按坐标、值或有效时间去重。无 ORDER BY 不承诺顺序。
- `region_id`、lat/lon、om_source 属于合成/位置列，单独查询它们不创建值依赖；COUNT(*) 仍需精确区域判断，不能从候选数推定。
- 空区域不产生原始行，不输出占位值。`COUNT(*)` 计选中记录，`COUNT(value)` 计非 NULL 值；AVG/SUM 忽略 NULL；全 NULL 组 AVG/MIN/MAX/SUM 为 NULL。NaN→NULL、±Inf 及浮点聚合沿用宿主既有语义，错误不吞掉。
- 按区域/time/member/level 等分组由 SQL 显式表达，系统不提前聚合。重复时间标签只有用户按标签 GROUP BY 时才合并；若需要逐轴位置区别，使用 om_source。
- WHERE 保留完整 residual；安全空间/轴必要条件可缩小候选。region_id 条件首版不下推，不能因 WHERE 或 LIMIT 绕过集合合法性检查。

## 使用形态

```sql
LOAD spatial;
CREATE TEMP TABLE regions(region_id VARCHAR, geom GEOMETRY);
INSERT INTO regions VALUES
 ('A', ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))')),
 ('B', ST_GeomFromText('POLYGON((101 21,103 21,103 23,101 23,101 21))'));
SET VARIABLE region_input = (
 SELECT COALESCE(list({'id':region_id, 'wkb':ST_AsWKB(geom)} ORDER BY region_id),
                 []::STRUCT(id VARCHAR,wkb BLOB)[]) FROM regions
);
-- 将 getvariable('region_input') 传给 regions；完整单对象示例见 quickstart。
```

区域输入是语句绑定时的快照；改变变量后的新语句使用新集合，不允许旧 prepared/bind data 意外复用旧区域。继承 `SupportStatementCache=false`，PREPARE/EXECUTE 情况以显式 rebind 测试锁定同样语义；不能依赖可变引用。

## 错误与依赖

| 类别 | 条件 | 行为 |
|---|---|---|
| `[spatial_dependency]` | spatial 未加载、系统函数签名不匹配 | 绑定失败，提示显式加载匹配版本；不自动安装/联网 |
| `[region_input]` | 类型/NULL/ID/几何格式或拓扑错误 | 整体绑定失败；可指出 ordinal/字段，不输出完整几何或凭据 |
| `[region_reference]` | 声明缺失、未知参考或接缝/极区超范围 | 明确拒绝，不猜测或转换 |
| `[region_input_limit]` | 区域/顶点/WKB 或拥有容量超预算 | 明确拒绝并给上限 |
| 既有 OM/远程错误 | schema/对象/权限/短读/版本变化/解码 | 沿用现有类别，查询失败或取消；已有流式批次不代表成功完整结果 |

验证全部区域、依赖、原有参数及 OM 绑定后，空集合可以零值 I/O 结束；不保证零元数据/远程请求。请求已绑定后的精确判断和解码异常进入现有 QueryEnd 终态。扫描默认复用绑定元数据；连接设置 `duckomo_revalidate_metadata=true` 启用全树重读并比较绑定视图，显式选择不会与全变量 schema 错误比较。读取期间对象须稳定。`read_om_raw` 不接受 `variable`，其根数组接口保持不变。
