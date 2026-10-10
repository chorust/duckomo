# SQL Interface Contract

> 当前开发版修订：SQL 地理列统一为 `lat/lon`；`dimensions` 支持共享 `VARCHAR[]` 或完整逐变量 MAP；远程错误输出脱敏类别与建议。迁移及完整规则见 [读取接口修订](../../../docs/interface-migration.md)。历史 evidence 仍对应原构建，不因本修订提升验收状态。

本契约描述 Phase 0–2 的基础读取行为。当前空间参数、坐标列和范围选择由 [Phase 3 契约](../../002-spatial-pushdown/contracts/sql-interface.md) 补充；下文的阶段限制不代表完整的当前接口。固定依赖与当时的验证结果见 [research](../research.md) 和 [evidence](../evidence/)。

## 入口

```sql
read_om_raw(path VARCHAR)
read_om(path VARCHAR, dimensions ANY := NULL)
```

- 参数在 bind 时为非 NULL 的常量路径；dimensions 默认缺省，显式 NULL 等同缺省。只接受本地单个普通文件，不支持 glob、文件列表、目录和远程 URI。
- `read_om_raw` 为 Phase 0 验证入口，仅接受 v3 根 Float32 数组，返回 `value FLOAT`。不承诺该验证入口长期兼容。
- `read_om` 接受下表支持子集。单数组不需要 dimensions；多数组在同形状且拥有相同有序 `coordinates` 元数据时自动对齐，否则必须显式声明全部值变量的有序轴标识。
- dimensions 支持共享的原生 `VARCHAR[]`，例如 `['row','column']` 对每个值变量声明同一份完整有序轴；也支持原生 MAP，不是 JSON 文件。MAP 的键可用对外列名（如 `humidity`）或内部绝对路径（如 `/humidity`）。若传入，键必须精确覆盖所有值数组，每个数组的轴列表非空、元素不得为 NULL、轴名非空且唯一、长度等于 rank；轴名按 UTF-8 字节精确比较。显式轴名与现有 `coordinates` 元数据冲突时拒绝。所有值数组的 shape 和轴列表必须逐位置完全相同，否则失败；不自动转置、广播或连接。
- Phase 0–2 不提供坐标映射。当前 `grid`、`spatial_axes`、`domain` 参数见 Phase 3 契约；独立 bbox、time range 参数仍未实现。

```sql
SELECT * FROM read_om('test/data/raw.om');
DESCRIBE SELECT * FROM read_om('test/data/raw.om');

SELECT temperature
FROM read_om('test/data/multi.om', dimensions := map(
  ['temperature', 'humidity'],
  [['row', 'column'], ['row', 'column']]
));
```

这些声明由调用者负责反映真实轴身份。名称 `row`/`column` 只是该文件内的身份，不表示某种网格。

## 支持矩阵

| 项目 | 基础读取行为（无 grid/domain） |
| --- | --- |
| 文件版本 | OM v3；v1/v2 和未知版本拒绝 |
| 布局 | 根数组，或 NONE 容器组成的树和 Float32 值数组；数组子节点及容器中的标量作为附属元数据遍历校验，不生成值列；其他顶层数组类型拒绝 |
| 数据类型 | FLOAT_ARRAY → DuckDB FLOAT |
| 压缩 | read_om 支持 FPX_XOR2D、PFOR_DELTA2D_INT16；其他压缩拒绝。read_om_raw 仍仅接受 FPX_XOR2D 根数组 |
| 维度 | rank 1–8；shape 和 chunk shape 每项正；shape 乘积不超过 INT64_MAX |
| 空数组 | 当前子集拒绝零长度维度；无数组文件也拒绝；过滤产生零行正常支持 |
| 缺测 | NaN → NULL；普通零、负数及 ±Inf 保留；无隐式 nodata 哨兵 |
| 精度 | 相对官方 OM Float32 解码结果容差 0；PFOR 为有损编码，比较的是解码后的值；NaN 位置单独比较 |
| 多变量 | shape、rank、有序轴身份都相等；轴身份可来自共同 `coordinates` 元数据或显式 dimensions，否则拒绝 |
| 坐标列 | 不生成；引用 lat/lon/time 等不存在列由正常绑定报错，说明中引导查看实际 schema |

NaN/Inf 和 FPX roundtrip 的已测结果见 [Phase 0 evidence](../evidence/phase0.md)；PFOR 精度以官方解码后的 Float32 值为基准。

## 命名和模式稳定性

- 根数组忽略其标签，规范路径为 `/`、输出列固定为 `value`。
- 层级数组的对外列名不带前导 `/`，如 `temperature`、`surface/temperature`；内部仍使用 `/temperature`、`/surface/temperature` 等绝对规范路径定位节点，根容器自身名称不参与。
- 路径分段中的 `%` 编码为 `%25`，`/` 编码为 `%2F`，保持大小写；拒绝空名称、NUL、无效 UTF-8。使用双引号引用带 `/` 的 SQL 列名。
- 层级数组按内部规范路径 UTF-8 字节序排序；生成列名必须在 DuckDB 标识符比较规则下唯一，不能靠遍历时追加序号掩盖重复。冲突时报告路径并失败。
- 列描述只读必要 metadata；在同一文件内容与参数下不因 SELECT 列表变化而改变名称或类型。

## 行、投影与 SQL 语义

- 最后一轴最快的逻辑展平顺序；所有变量同一行来自同一个索引元组。不承诺无 ORDER BY 的一般 SQL 结果顺序。
- 基础扫描启用列裁剪；普通过滤由 DuckDB 保留并执行，filter pushdown/filter prune 关闭。配置空间坐标后可通过 Phase 3 的 complex-filter callback 缩小候选，完整 WHERE 仍保留。
- 查询依赖包含 SELECT、WHERE、ORDER BY、GROUP BY、聚合及表达式使用的变量；重复引用不应导致重复解码整变量。
- `COUNT(*)` 只消费行数时，按 metadata 输出 cardinality，不读取或解码任何值数据；允许正常优化器不执行 scanner。
- `WHERE FALSE` 和无匹配条件返回零行；后者可以读取全部所需变量。
- 任一 bind/schema 错误在输出数据前报告；扫描期损坏/取消使查询失败，不把已产生的批次视为完整成功结果。

## 错误类别

| 类别 | 触发条件 | 可见信息要求 |
| --- | --- | --- |
| 输入错误 | NULL/非恒定路径、未知参数、非法 dimensions | 参数名及预期形态 |
| 文件访问错误 | 不存在、权限不足、目录/远程路径 | 路径及失败类别 |
| 不支持 | 版本、类型、压缩、布局、rank、零轴 | 实际属性和本阶段支持限制 |
| 对齐/模式错误 | 缺失轴声明、shape/轴不同、名称冲突 | 涉及变量路径和具体差异 |
| 数据损坏 | 截断、非法引用、官方 reader 错误 | 文件及尽可能可用的变量路径、官方错误说明 |
| 取消 | 用户中断查询 | 正常取消状态，资源释放后可再次查询 |

不固定完整错误字符串；测试断言类别及关键上下文，避免依赖上游措辞。
