# duckomo 产品与接口 Spec

状态：2026-09-28。Phase 0–2 本地扫描器已实现，并在 Linux AArch64 验证；计划目标 Linux x86_64 尚无运行证据。以下区分现有接口与后续目标。详见 [最终验收记录](../specs/001-local-om-scanner/evidence/final.md)。

## 1. 目标

让 DuckDB 把 Open-Meteo OM 文件作为可局部读取的多维气象数据源。用户直接运行 SQL，无需先经 Python/NumPy 转成 NetCDF 或 Parquet。最终目标是 **query-aware scanner**：

```text
SQL 投影和谓词
  → 变量与维度选择
  → OM 逻辑索引/切片
  → 官方 OM reader 的 chunk/LUT/局部读取/解压
  → DuckDB Vector/DataChunk
```

目前变量投影已能减少物理读取和解压；地理区域、时间范围对应的局部读取仍属于后续阶段。

## 2. SQL API：当前实现与后续目标

当前入口为 `read_om(path VARCHAR, dimensions MAP(VARCHAR, VARCHAR[]) := NULL)`，另有单根数组验证入口 `read_om_raw(path VARCHAR)`。均只接受本地单个文件。下面是可在仓库 fixture 上执行的查询：

```sql
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;

SELECT "/temperature"
FROM read_om('test/data/multi.om', dimensions := map(
  ['/humidity', '/temperature'],
  [['row', 'column'], ['row', 'column']]
))
WHERE "/humidity" >= 103;
```

`domain` 参数和语义坐标列尚未实现。未来计划在可靠的映射可用后支持 `domain`、经纬度与时间筛选；远程路径属于 Phase 5。

### Phase 0–2 的首批接口子集

本地扫描器的实现与证据见 [实现计划](../specs/001-local-om-scanner/plan.md)、[SQL 契约](../specs/001-local-om-scanner/contracts/sql-interface.md) 和 [Phase 0–2 验收记录](../specs/001-local-om-scanner/evidence/)。当前支持 OM v3、Float32 数组及 FPX_XOR2D 压缩；旧版本、其他类型或压缩、非法布局及零长度轴均明确拒绝。NaN 映射为 NULL，±Inf 和 ±0 保留。

OM 基础数组元数据没有通用轴身份，因此多变量不能仅凭相同 shape 对齐。`read_om(path, dimensions := ...)` 使用显式 `dimensions MAP(VARCHAR, VARCHAR[])` 参数，逐变量声明有序轴标识；shape 与轴声明必须完全相同。单变量不要求该参数。该声明只提供逻辑对齐证据，不生成坐标；本地扫描子集不支持 `domain` 映射。根数组输出 `value`，层级数组使用规范路径作为列名，命名和拒绝规则详见 SQL 契约。

`read_om_raw(path)` 是 Phase 0 的单变量验证入口。`read_om(path, dimensions := NULL)` 可读取单数组；层级多变量必须提供完整的 `dimensions` 映射。查询只投影实际需要的值变量；由 DuckDB 执行普通过滤，过滤引用的变量仍会作为扫描依赖读取。`COUNT(*)` 可仅读取 metadata 来输出 cardinality，不读取索引或值数组。实际读取和解码计数以及限制见 [Phase 2 证据](../specs/001-local-om-scanner/evidence/phase2.md)。

### 当前行和列的语义

- 每行表示选中变量在同一组逻辑索引处的值，最后一轴变化最快；不承诺未指定 `ORDER BY` 时的 SQL 结果顺序。
- Bind 读取 OM hierarchy、变量名、类型、shape 和 chunk 元数据，产生稳定的 `DESCRIBE` 模式。根数组列名为 `value`；层级数组使用规范路径，如 `"/temperature"`。
- 目前不生成 `latitude`、`longitude`、`time`、`level` 等语义坐标列。引用不存在的坐标列会由 DuckDB 正常报绑定错误；`dimensions` 仅声明轴身份，不生成坐标。

### 后续目标用法

以下 SQL 仅用于说明后续接口方向，`domain` 和经纬度列尚未实现：

```sql
SELECT temperature_2m
FROM read_om('test.om', domain := 'example_domain')
WHERE latitude BETWEEN 30 AND 40
  AND longitude BETWEEN 110 AND 120;
```

### 下推的正确性：投影已实现，谓词仍在规划

- **Projection pushdown**：只解码查询需要的变量；用于过滤或构造输出的维度信息也必须保留。
- **Filter pushdown**：将可识别的经纬度、时间、层次、成员等谓词转成 OM 逻辑选择。无法安全转换的谓词交由 DuckDB 正常计算；不能因优化漏掉满足条件的行。
- 网格选择可以返回覆盖目标的候选索引；候选行仍需经过精确谓词检查，避免边界、浮点、跨经线、投影或 Gaussian 网格产生额外结果。
- 空选择不触发数据块读取。谓词下推不可用时，结果正确性优先于局部读取性能，且行为必须可观测。
- 对 `filter_pushdown`/`filter_prune` 的启用要以所锁定 DuckDB 版本的集成测试为准，尤其检查残余过滤是否保留以及过滤列是否被过早裁剪。

## 3. 后续 v1 范围

Phase 0–2 已完成本地扫描、层级变量和列裁剪。下一条纵向切片是：

```text
local OM file
  → DuckDB C++ table function
  → 已实现的 bind/scan 与列裁剪
  → RegularGrid 经纬度映射
  → SQL bbox 谓词下推
  → 官方 OM C reader partial read
  → DataChunk
```

之后逐步加入时间/层次/预报维度、Gaussian 与投影网格、远程 I/O 和并行扫描，顺序见 [Roadmap](roadmap.md)。

扫描器核心不重新解析 OM 二进制，不重写压缩或 chunk/byte-range 规划，也不自建 HTTP/S3 客户端。插值、重网格与科学算子属于独立的后续阶段。

## 4. 非功能要求与验收

每一阶段同时验证两件事：

1. **结果正确**：与官方 OM reader 或经人工确认的 fixture 对照，覆盖边界、空结果、维度顺序和缺测值。
2. **物理读取正确**：记录 bytes fetched、read requests、chunks decoded、耗时与峰值内存。窄区域/单变量查询应读取和解压明显少于全域/全部变量查询；不能仅在输出端过滤。

对于远程文件，指标应从实际文件系统请求或 reader instrumentation 获得。性能门槛在有稳定 fixture 后设定，不在此凭空指定数值。

## 5. Phase 7 的扩展方向

科学数组能力与扫描入口分离，候选接口为 `om_slice()`、`om_reduce()`、`om_interp()`、`om_regrid()`。xtensor 只作为已解码数据块的可选 N-D 计算后端；普通 `read_om()` 数据流直接进入 DuckDB Vector。重网格可蒸馏 `om-exporter` 的转换经验，但不进入 scanner core。
