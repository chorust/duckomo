# Data model: 方法、空间计划和区域采样结果

这是单次绑定/查询内的数据结构，全部为拟议实现；不新增持久表或迁移。公共字段准确类型见[SQL契约](contracts/sql-interface.md)。

## RegionMethod / RegionSamplingOptions

- RegionMethod：Points / Nearest；只在区域入口解析，省略固定为Points。
- Options：optional max_distance_m、out_of_bounds Null/Error；记录参数是否显式存在，以便Points拒绝不适用参数。固定representative=point_on_surface_v1、distance_model=grid_sphere、algorithm=sphere_haversine_f64_v1。
- 字符串/NULL/类型/范围完整验证；Copy/Equals包含方法和策略，不比较临时指针。方法、schema和输入快照绑定后不可变，沿用SupportStatementCache=false。

## RegionCollection / RegionTarget

复用005 RegionCollection的受检ID/WKB、envelope、拥有内存、EMPTY和顶点计数。NearestPlan不另复制全集合或保留每个历史区域的worker GEOMETRY。

RegionTarget字段：

| 字段 | 类型 | 不变式 |
| --- | --- | --- |
| region_ordinal | uint32 | 指向immutable集合，0..count-1 |
| target | optional GridCoordinate | EMPTY无位置，其它必须有限且被原面覆盖 |
| status | GeometryStatus | Ok / EmptyGeometry / OutsideDomain / BeyondMaxDistance |
| candidate | optional SourcePoint | Ok/超距有候选，EMPTY/域外无候选 |
| representative_id | 固定版本标识 | point_on_surface_v1 |

SourcePoint：x/y、point_index、parent_point_index、实际source_lon/lat、distance_m。这里没有logical_index，因为不同非空间轴组合需要不同源记录。超距候选只供解释，不生成读取/成功source身份。

## RegularSamplingCapability

- geometry/numeric_rule、sphere radius、longitude periodicity、coverage_kind=Global/LatitudeBand/Regional、原始unwrapped端点、layout/grid ID。
- qualification/provenance：规则验证版本、登记domain的证据引用或explicit-caller-declared；提供计算能力不等于全部domain都通过真实验收。
- 校验stream逐轴，无全域坐标数组；拒绝超纬、数值坍缩、重复wrap、不支持模型。准确规则见[numeric contract](contracts/numeric-and-capability.md)。

## NearestPlan

immutable shared对象：Options、Capability、RegionTarget数组、spatial_version、算法身份、input/result上界及owned容量。绑定阶段完整区域验证后逐区域生成target并定位，strict域外在绑定阶段拒绝；不随projection、WHERE或LIMIT改变结果。

- 至多4096目标，容量≤8MiB；ID/几何继续引用RegionCollection，不能按region×time复制。
- scalar method/version/model身份共享，结果输出时引用或以DataChunk生命周期安全方式构造。
- NearestPlan包含与结果相关的全部内容；BindData Copy共享不可变plan并为所拥有的副本计量，Equals比较规范内容或稳定内容身份，不能只比较shared_ptr。
- 绑定阶段坐标/定位成本被记录；即使扫描后来提前停止，已完成定位的观察成本仍真实保留，不能等同完整结果。

## 非空间轴计划与 logical_index

复用SpatialLayout的shape、axes、strides、non_spatial_axes，按原文件顺序提取non_spatial_shape。语义轴和TemporalBindConfiguration仍使用原轴位置，不能用删除空间轴后的索引去访问原语义映射。

新增受检映射：

```text
source_logical_index = sum(original_axis_index[a] * original_stride[a])
```

空间axis_indices由SourcePoint的x/y或flattened point确定，非空间indices由cursor给出；每项检查长度、乘加和schema.row_count。对不可采样结果没有source index，用独立的原轴indices向量生成semantic output，禁止为EMPTY捏造源点0。

NearestAxisCursor仅迭代非空间组合，复用安全轴necessary intervals / NextSelectedAxisIndex的逻辑；不枚举空间shape乘积。可在原AxisSelectionCursor增加“维度投影”模式或提取内部axis iterator helper，旧构造行为不变。某原轴未做语义映射仍保留其索引并参与结果基数。

## NearestTask / NearestBatch

- task descriptor：task_id、region_ordinal、非空间组合区间/迭代器快照，不持有全结果位置表；单task不跨区域。
- global cursor：当前区域+非空间cursor、outstanding/completed计数和停止状态，短锁只领取描述符。
- worker：当前descriptor、有限结果rows和source mapping、reader/decoder、值缓冲与输出scatter；没有source的行也在结果批次中。
- 结果row：region_ordinal、non_spatial_indices、optional logical_source_index、target/candidate只引用计划。
- read mapping：有效source逻辑位置sort/unique→BuildSelectedBatchSegments→DecodeSelection→scatter回原结果行。重复源不能输入既有严格递增segments；首版单区域递增时可省sort但必须证明顺序。
- EMPTY/域外/超距不加入读取mapping。一个批次全部无source仍输出rows，不误报EOF。

## SamplingProjectionPlan

新增内部OutputColumnKind::Sampling；同时增加nested leaf output descriptor，不注册新SQL列/虚拟采样函数。

- input是完整TableFunctionInitInput.column_indexes，保留primary+child路径和FULL_READ/PUSHDOWN_EXTRACT类型。
- 一个sampling leaf描述输出目标（整个STRUCT或提取叶）和依赖（GeometryOnly / AllBoundVariableStates）。output类型必须符合宿主PhysicalTableScan期待，不是构造整STRUCT后塞到标量slot。
- requested source列与变量状态依赖合并去重为RequiredVariableIds；重复投影保留多个output slot，但只创建一个decoder。
- variables为MAP，不假设其key提取能裁剪依赖。整个sampling/variables或其任意key状态首版保守读取全部当前绑定变量；variable参数能限制绑定范围。
- GeometryOnly涵盖method/version/grid/layout/distance/status/source coords/axis_indices；没有值依赖。unknown path/full-read保守按全部状态读，不能输出假NULL。
- points和read_om沿用旧顶层projection行为；supports_pushdown_extract只对nearest的sampling列返回true，不扩大旧om_source语义。

## 每变量状态和输出

NearestVariableResult字段：status、source_count、valid_source_count及FLOAT value。几何Ok时需要观察所需变量：NaN→NULL/source_null，±Inf为有效原值，source_count=1。几何不Ok时状态由plan已知，计数均0，无需值读取。

schema里的variables始终定义全部绑定变量；若未投影该MAP可完全不构造它。完整STRUCT投影必须生成真实所有状态，不能用未读取状态占位。source_count不是COUNT(*)，超距候选也不是成功贡献。

include_source true且几何Ok时调用既有MakeOmSourceValue(actual_logical_index)；源缺测仍有来源。其余NULL。顶层lat/lon是target，sampling.source_lon/lat才是源。

## 终态与计量

```text
unbound → validating → targets_located → bound → initialized → running → drained
                 ↘ bind_error              ↘ failed/cancelled     → succeeded
running → early_stopped (SQL可成功但scan_complete=false)
```

- 全集输入、网格能力、strict域外和target定位在bound前完成，绑定错误不保证出现scan metrics行。
- 只有cursor exhausted且outstanding=0才能drained；NULL结果行也计处理完成，不是“无匹配任务”。
- plan几何计数按target单位，运行结果计数按region_axis_result，值状态按变量×结果单位。字段不能混用。
- 取消/失败停止发任务，已领取任务一次结算，QueryEnd发布immutable snapshot；exact结果计数/成功memory完整性遵守既有NULL规则。
- 输入几何、plan、task/scratch、reader/decoder账户分列；DuckDB output/GEOS/httpfs内部未知不假设为零。具体上限和计量见[执行契约](contracts/execution-and-metrics.md)。
