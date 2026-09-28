# SQL Interface Contract

状态：Phase 0–2 已验证实现契约。固定依赖与实际通过证据见 [research](../research.md) 及 [evidence](../evidence/)；本契约仅覆盖当前可执行子集。

## 入口

```sql
read_om_raw(path VARCHAR)
read_om(path VARCHAR, dimensions MAP(VARCHAR, VARCHAR[]) := NULL)
```

- 参数在 bind 时为非 NULL 的常量路径；dimensions 默认缺省，显式 NULL 等同缺省。只接受本地单个普通文件，不支持 glob、文件列表、目录和远程 URI。
- `read_om_raw` 为 Phase 0 验证入口，仅接受 v3 根 Float32 数组，返回 `value FLOAT`。不承诺该验证入口长期兼容。
- `read_om` 接受下表支持子集。单数组不需要 dimensions；两个及以上数组必须声明全部变量的有序轴标识。
- dimensions 类型是原生 MAP，不是 JSON 文件。键为下文定义的变量规范路径。若传入，键必须精确覆盖所有数组，每个数组的轴列表非空、元素不得为 NULL、轴名非空且唯一、长度等于 rank；轴名按 UTF-8 字节精确比较。所有数组的 shape 和轴列表必须逐位置完全相同，否则失败；不自动转置、广播或连接。
- `domain`、bbox、time range 等命名参数尚不支持；本阶段没有坐标映射能力。

```sql
SELECT * FROM read_om('test/data/raw.om');
DESCRIBE SELECT * FROM read_om('test/data/raw.om');

SELECT "/temperature"
FROM read_om('test/data/multi.om', dimensions := map(
  ['/temperature', '/humidity'],
  [['row', 'column'], ['row', 'column']]
));
```

这些声明由调用者负责反映真实轴身份。名称 `row`/`column` 只是该文件内的身份，不表示某种网格。

## 支持矩阵

| 项目 | 首批行为 |
| --- | --- |
| 文件版本 | OM v3；v1/v2 和未知版本拒绝 |
| 布局 | 根数组，或 NONE 容器组成的树和 Float32 数组；数组带子节点、其他标量/数组类型拒绝 |
| 数据类型 | FLOAT_ARRAY → DuckDB FLOAT |
| 压缩 | FPX_XOR2D；其他压缩拒绝 |
| 维度 | rank 1–8；shape 和 chunk shape 每项正；shape 乘积不超过 INT64_MAX |
| 空数组 | 当前子集拒绝零长度维度；无数组文件也拒绝；过滤产生零行正常支持 |
| 缺测 | NaN → NULL；普通零、负数及 ±Inf 保留；无隐式 nodata 哨兵 |
| 精度 | 相对官方 FPX Float32 解码结果容差 0；NaN 位置单独比较，非有限值单独比较 |
| 多变量 | shape、rank、有序显式轴身份都相等；缺失映射或不一致即拒绝 |
| 坐标列 | 不生成；引用 latitude/longitude/time 等不存在列由正常绑定报错，说明中引导查看实际 schema |

NaN/Inf 和完整 FPX 文件 roundtrip 必须在实施 Phase 0 实测通过后才可声称支持；若失败，先修订契约和基线，不能无声切换有损编码或抹掉缺测。

## 命名和模式稳定性

- 根数组忽略其标签，规范路径为 `/`、输出列固定为 `value`。
- 层级数组的列名等于从根容器以下开始的绝对规范路径，如 `/temperature`、`/surface/temperature`；根容器自身名称不参与。
- 路径分段中的 `%` 编码为 `%25`，`/` 编码为 `%2F`，保持大小写；拒绝空名称、NUL、无效 UTF-8。使用双引号引用带 `/` 的 SQL 列名。
- 层级数组按规范路径 UTF-8 字节序排序；生成列名必须在 DuckDB 标识符比较规则下唯一，不能靠遍历时追加序号掩盖重复。冲突时报告路径并失败。
- 列描述只读必要 metadata；在同一文件内容与参数下不因 SELECT 列表变化而改变名称或类型。

## 行、投影与 SQL 语义

- 最后一轴最快的逻辑展平顺序；所有变量同一行来自同一个索引元组。不承诺无 ORDER BY 的一般 SQL 结果顺序。
- scanner 仅启用列裁剪；普通过滤由 DuckDB 保留并执行。不启用 filter pushdown/filter prune，不承诺条件减少读取行范围。
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
