# duckomo 技术架构

当前实现包括本地 OM 扫描、列裁剪和规则网格空间选择。用户接口见 [接口说明](spec.md)，构建与验证命令见 [README](../README.md)。

## 模块职责

| 模块 | 职责 | 源码 |
|---|---|---|
| DuckDB table function | 绑定参数、稳定 schema、分析空间条件、输出 DataChunk | `src/scan/read_om.cpp` |
| Schema / dimensions | 变量命名、shape 和有序轴身份校验 | `src/scan/schema.cpp`、`dimensions.cpp` |
| ProjectionPlan | 保留输出与过滤依赖，去重物理变量并保留输出顺序 | `src/scan/projection.cpp` |
| RegularGrid / SpatialLayout | 生成坐标，核对分离或展平轴及逻辑 stride | `src/grid/` |
| DomainRegistry | 固定 68 个网格定义；绑定时核对轴长与 WKT BBOX | `src/grid/domain_registry.cpp`、`domain_bbox.cpp` |
| SpatialPredicate / selection cursor | 提取安全条件，将候选位置切成有界连续段 | `src/scan/spatial_filter.cpp`、`spatial_selection.cpp` |
| 本地 OM 适配层 | 元数据遍历、文件读取、解码生命周期和计量 | `src/om/` |
| 官方 OM C reader | 格式解析、物理 chunk 选择、LUT / 字节请求与解码 | `third_party/om-file-format/` |

## 查询流程

```text
DuckDB SQL
  → Bind：读取元数据，校验参数、变量、网格与布局，确定 schema
  → 优化：complex-filter callback 收集安全空间条件，保留完整 WHERE
  → GlobalInit：规划输出与过滤依赖，初始化 selection 和扫描状态
  → SpatialSelection：逻辑位置转换为有界连续 DecodeSelection 段
  → 官方 OM C reader + 本地适配器：请求字节并解码所需变量
  → DuckDB Vector/DataChunk
  → DuckDB 执行完整 WHERE 和上层 SQL 运算
```

当前扫描为单线程。无空间配置时沿用普通逻辑扫描；额外非空间轴仍参与行索引，不被折叠。

## 条件分析与正确性

启用 projection pushdown；普通 `filter_pushdown` 和 `filter_prune` 关闭。空间 callback 校验当前 `LogicalGet` 的表/列绑定和引用深度，仅提取可证明安全的经纬度必要条件。它不删除或改写 `WHERE`，不跨生命周期保存表达式指针。

有限常量比较、`BETWEEN` 和安全 `AND` 可缩小候选。混合 `AND` 可保留独立安全子句；`OR`、`NOT` 和函数子树不单独缩窄读取，回退原因写入指标。候选必须覆盖所有匹配行，精确过滤由 DuckDB 执行。

`SpatialLayout` 把逻辑索引映射到分离轴或明确存储顺序的展平轴。selection cursor 生成的段位于最终连续轴内，长度非零且不超过批次上限；不物化全域坐标表。空选择、仅坐标和仅行数查询不构造值 decoder；值过滤所需变量仍保留。

## I/O 与指标

网格选择只提供逻辑切片。OM chunk 交集、LUT、字节范围和解码策略由官方 reader 负责；本地适配器通过 DuckDB 文件系统执行读取，并在实际读取与 decoder 边界计数。

每次查询的 v2 指标区分 bind/scan 元数据和逐变量 index/data，记录读取字节、请求数、解码块数、候选行数、选择模式、回退原因及完整性。扫描绑定状态相互隔离，prepared statement 禁用 statement cache；query-end observer 记录整个 SQL 查询的成功、失败或取消。计量字段见 [观测契约](../specs/002-spatial-pushdown/contracts/validation-evidence.md)。

性能比较要求同文件、同值列、同环境，完整消费结果并检查计数完整性。数据字节表示应用读取量，不代表磁盘物理 I/O。固定多块样本的 temperature 全扫 / 25 点窗口分别读取 **165,767 / 1,795 字节**，解码 **503 / 5 块**；仅坐标、纯空间 count 和空选择的值 index/data/decode 均为零。证据见 [US2](../specs/002-spatial-pushdown/evidence/us2.md) 和 [US3](../specs/002-spatial-pushdown/evidence/us3.md)。其他块布局不保证相同收益。

## 网格来源与验证

Registry 固定 68 个规则网格定义，不自动发现或扩展。上游版本、真实样本 shape、轴和 BBOX 核对见 [规则网格 domain](regular-domains.md)。最初的 `ncep_gfswave025` 有全域坐标与 15 个官方值参考；CHMI 和 GeoSphere 的具体样本另有独立 Swift Float 坐标公式及官方 C reader 值对照。其余登记项以文档记录的样本元数据覆盖为准。

`make test` 构建 release 并调用 `scripts/validate.sh`，覆盖 SQL/native、合成样本重生与哈希、投影指标。设置 `DUCKOMO_DOMAIN_FILE` 后额外执行真实样本的完整空间 harness，缺失文件或哈希不符会失败。`make sanitizer-test` 检查边界与生命周期；性能结论取普通 release 构建。

原完整验收与独立复现在 Linux AArch64 通过，x86_64 支持与验证暂缓。步骤见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md)，结果见 [最终验收](../specs/002-spatial-pushdown/evidence/final.md)。后续维度、网格、远程读取和科学算子见 [Roadmap](roadmap.md)。
