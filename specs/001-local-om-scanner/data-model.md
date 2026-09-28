# Data Model: 本地 OM 扫描

本模型为进程内只读模型，不引入持久化数据库。产品接口见 [SQL contract](contracts/sql-interface.md)，证据格式见 [validation evidence](contracts/validation-evidence.md)。

## 实体及关系

| 实体 | 字段 | 所有权与约束 |
| --- | --- | --- |
| FileSource | canonical_path、file_size、format_version、handle | 仅一个本地普通文件；scan 生命周期持有定位读取句柄；拒绝远程协议、glob、目录、NULL 路径 |
| MetadataNode | node_range、owned_bytes、node_kind、name、children | 自有缓冲区支撑官方 variable 借用指针；检查范围/大小溢出、文件边界和层级循环；不自行解析压缩内容 |
| VariableDescriptor | canonical_path、column_name、data_type、compression、shape[]、chunk_shape[]、metadata_owner | 仅 v3 Float32/FPX；rank 1–8、shape/chunk 元素为正；计数乘法有溢出检查；稳定路径唯一 |
| AxisDeclaration | variable_path、ordered_axis_ids[] | 来自显式 dimensions 参数；键精确覆盖全部数组；每列表长度等于 rank，轴名非空且不重复；仅声明索引身份，不携带坐标 |
| BoundSchema | variables[]、column_names[]、sql_types[]、shape[]、row_count、alignment_evidence | bind 后不可变；多变量 shape 和有序轴声明相同；列名按 DuckDB 标识符比较规则无冲突 |
| ProjectionPlan | output_slots[]、required_variable_ids[]、cardinality_only | output slot 映射到变量或内部 COLUMN_IDENTIFIER_EMPTY 空投影标识；去重变量解码；包括 SQL 执行器要求的过滤/表达式/排序/聚合依赖 |
| ScanState | next_linear_index、batch_count、selection_offsets[]、selection_counts[]、decoder、scratch、file_owner | 单线程；所有借用参数和 metadata 存活至解码结束；一次输出不超过 DuckDB 标准批次大小 |
| ScanMetrics | query_id、phase、variable_id、bytes、requests、decoded_chunks、elapsed、peak_rss | 查询隔离；bind 元数据与 scan 的索引/数据请求分开；成功解码次数含重复，不当作唯一块数 |
| FixtureManifest | fixture_id、checksum、format、shape、axes、chunk_shape、expected_schema、null_positions、oracle、tool_versions | 固定输入、生成方法、支持/拒绝理由、容差和参考数据来源；不得只存 scanner 自己导出的“真值” |

关系：FileSource 包含 metadata 树；叶数组映射为 VariableDescriptor；BoundSchema 引用全部变量与对齐证据；ProjectionPlan 是 schema 的查询依赖子集；ScanState 按同一个逻辑范围推进所有被请求变量。所有权方向保证 metadata 在 decoder 之前销毁不得发生。

## 逻辑行与数值

- 最后一维最快。shape `[d0,...,dn]` 对应的行数是各轴长度乘积；线性索引逐轴除余得到有序索引元组。
- 每批将线性范围拆成最后一轴的连续逻辑段，通过 OM offset/count 读取。拆分只涉及逻辑数组，不推导压缩块地址。
- 初始输出只含值列；不额外暴露时间、经纬度、轴名或行索引列。直接 scanner 的顺序用于原生测试；一般 SQL 结果顺序只有显式 ORDER BY 才保证。
- Float32 有限值与官方 FPX 解码输出精确对应，NaN 写入 validity mask 为 NULL，±Inf 保留；不根据任意数值哨兵或文件名判缺测。
- `row_count` 经过溢出校验后供计数和结束条件使用；COLUMN_IDENTIFIER_EMPTY 通过 get_virtual_columns 注册为 BOOLEAN 虚拟列，只供优化器空投影选择，是内部执行标识，不是产品可查询列。

## 状态转换

1. **Unbound → Binding**：打开本地文件，读取 header/trailer 与必要变量 metadata；所有请求受文件边界和整数溢出检查。
2. **Binding → Bound**：支持性、命名、shape 和显式轴声明全部通过；可提供 DESCRIBE，不解码值数组。
3. **Bound → Scanning**：获取执行器实际依赖列，建立投影和查询独立的 I/O/解码状态；持有必要资源。
4. **Scanning → Scanning**：读取各依赖变量的同一范围，全部成功后提交一个 DataChunk，推进 next_linear_index；仅计数路径只设置 cardinality。
5. **Scanning → Exhausted**：累计行数达到 row_count，输出零行终止并释放句柄/缓冲区。
6. **任意运行状态 → Failed/Cancelled → Released**：错误上抛，当前批次不作为成功结果提交；RAII 释放资源；后续独立查询可重新打开文件。

不共享跨查询可变 decoder 或计数器；本阶段不引入缓存、并行任务、预取或文件变更一致性状态机。

## 关键不变量与拒绝策略

- 多变量同 shape 不是对齐证据；无完整 dimensions 声明则失败。
- 模式校验覆盖全部数组，不能只检查最终 SELECT 的变量；不支持的数组、标量数据节点、压缩或版本使 bind 失败。NONE 容器仅参与遍历，不输出列。
- 没有数组的文件、零长轴、重复路径、标识符冲突、无效 UTF-8 名称、轴重复/缺失/额外映射都明确拒绝。
- bind 可发现的截断和非法引用在返回数据前报错；未选 payload 的潜在损坏不会通过预先全解码发现，实际读取发现损坏时整次查询失败。
- 数据缓冲区和 metadata 缓冲区分离；decode 所需 scratch 大小由官方 reader 查询并做可分配性检查，不能承诺内存只等于输出批次。
