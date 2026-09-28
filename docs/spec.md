# duckomo v1 产品与接口 Spec

状态：设计基线（2026-09-28）；尚未实现。

## 1. 目标

让 DuckDB 把 Open-Meteo OM 文件作为可局部读取的多维气象数据源。用户直接运行 SQL，无需先经 Python/NumPy 转成 NetCDF 或 Parquet。核心价值是 **query-aware scanner**：

```text
SQL 投影和谓词
  → 变量与维度选择
  → OM 逻辑索引/切片
  → 官方 OM reader 的 chunk/LUT/局部读取/解压
  → DuckDB Vector/DataChunk
```

只返回正确行还不够。地理区域、时间范围、变量被筛选后，物理读取和解压工作也应随之减少。

## 2. 目标 SQL API

MVP 入口是 `read_om(path)`，在网格语义不能从文件或明确配置中可靠得到时，使用 `domain` 等显式参数。以下是**目标用法**，具体命名参数与 DuckDB 类型需在实现阶段以真实 OM 样本确认。

```sql
SELECT * FROM read_om('test.om');

SELECT temperature_2m
FROM read_om('test.om', domain := 'example_domain')
WHERE latitude BETWEEN 30 AND 40
  AND longitude BETWEEN 110 AND 120;

DESCRIBE SELECT * FROM read_om('test.om');
```

未来的远程路径沿用同一入口，例如 `read_om('s3://bucket/file.om', domain := '...')`。远程访问属于 Phase 5。

### 行和列的语义

- 每行表示选中变量在一组逻辑维度索引处的值。扫描顺序、维度展平顺序和多变量对齐规则应由 metadata 与网格定义确定，并用 fixture 锁定；不能从文件名推断。
- 已知维度可暴露为 `latitude`、`longitude`、`time`、`level`、`member`、`lead_time` 等 SQL 列；数据变量暴露为值列。只有能正确映射的维度才可暴露为对应语义列。
- Bind 读取 OM hierarchy、变量名、类型、维度、shape、chunk 等元数据，产生可供 `DESCRIBE` 使用的稳定列模式。纯数组 OM 与带 hierarchy 的文件都要在 Phase 0/1 用样本明确行为。
- 坐标和时间轴可能并不以数组形式存于 OM 文件中。若缺少足够的网格、起点、步长或 domain 信息，涉及这些列的查询须明确报错或要求显式映射；不得静默使用错误坐标。
- 初期支持的变量类型、缺测值与 null、时间类型、维度顺序由真实样本和官方 C API 决定，先实现并测试明确子集，再扩展。未经验证的文件布局不承诺通用支持。

### 下推的正确性

- **Projection pushdown**：只解码查询需要的变量；用于过滤或构造输出的维度信息也必须保留。
- **Filter pushdown**：将可识别的经纬度、时间、层次、成员等谓词转成 OM 逻辑选择。无法安全转换的谓词交由 DuckDB 正常计算；不能因优化漏掉满足条件的行。
- 网格选择可以返回覆盖目标的候选索引；候选行仍需经过精确谓词检查，避免边界、浮点、跨经线、投影或 Gaussian 网格产生额外结果。
- 空选择不触发数据块读取。谓词下推不可用时，结果正确性优先于局部读取性能，且行为必须可观测。
- 对 `filter_pushdown`/`filter_prune` 的启用要以所锁定 DuckDB 版本的集成测试为准，尤其检查残余过滤是否保留以及过滤列是否被过早裁剪。

## 3. v1 范围

第一条纵向切片：

```text
local OM file
  → DuckDB C++ table function
  → 单变量 bind/scan
  → RegularGrid 经纬度映射
  → SQL bbox 谓词下推
  → 官方 OM C reader partial read
  → DataChunk
```

之后逐步加入 OM hierarchy、多变量投影、时间/层次/预报维度、Gaussian 与投影网格、远程 I/O 和并行扫描，顺序见 [Roadmap](roadmap.md)。

MVP **不做**：重新解析 OM 二进制、重写压缩或 chunk/byte-range 规划、自建 HTTP/S3 客户端、xtensor scanner、KDTree、插值/重网格、NetCDF/xarray/PNG 导出、任意科学算子。

## 4. 非功能要求与验收

每一阶段同时验证两件事：

1. **结果正确**：与官方 OM reader 或经人工确认的 fixture 对照，覆盖边界、空结果、维度顺序和缺测值。
2. **物理读取正确**：记录 bytes fetched、read requests、chunks decoded、耗时与峰值内存。窄区域/单变量查询应读取和解压明显少于全域/全部变量查询；不能仅在输出端过滤。

对于远程文件，指标应从实际文件系统请求或 reader instrumentation 获得。性能门槛在有稳定 fixture 后设定，不在此凭空指定数值。

## 5. Phase 7 的扩展方向

科学数组能力与扫描入口分离，候选接口为 `om_slice()`、`om_reduce()`、`om_interp()`、`om_regrid()`。xtensor 只作为已解码数据块的可选 N-D 计算后端；普通 `read_om()` 数据流直接进入 DuckDB Vector。重网格可蒸馏 `om-exporter` 的转换经验，但不进入 scanner core。
