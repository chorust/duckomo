# Data Model: 区域输入、扫描与统计

本模型为内存中单次绑定/查询的数据结构，不新增数据库表、持久缓存或迁移。用户的区域表是输入准备方式，不由扩展维护。

## RegionInput / RegionCollection

| 字段 | 类型与约束 | 所有权/意义 |
|---|---|---|
| id | VARCHAR，1–256 UTF-8 字节，集合内按字节唯一 | 持有副本，输出 region_id |
| ordinal | uint32，0-based，≤4095 | 绑定时顺序，仅内部调度，不替代业务 ID |
| wkb | owned bytes，标准 2D Polygon/MultiPolygon | 原始声明输入，不解析私有 GEOMETRY |
| geometry | owned opaque DuckDB GEOMETRY Value | spatial 校验后持有；不把 blob 内部布局当公共 ABI |
| envelope | optional {min_lon,max_lon,min_lat,max_lat} DOUBLE | 标准 WKB 顶点算得；EMPTY 为无 envelope |
| empty | bool | 有效空几何，不同于 NULL |
| vertex_count | uint64 | 含各环闭合重复点；受检加总 |
| owned_bytes | uint64 | WKB/ID/opaque 数据与描述符容量，不含第三方临时 heap |

集合字段：`regions`、`region_crs='grid_lonlat'`、总字节/顶点、empty_count、内容身份。不可变共享给 bind/global/local；普通扫描无此集合。Copy/Equals 必须比较内容/声明，不能只比较指针。限额见 [执行契约](contracts/execution-and-metrics.md)。

校验顺序：实际 STRUCT 类型/字段 → NULL/ID/总量 → 受检 WKB walker → 坐标/范围/闭合 → 转换 → ST_IsValid → owned capacity → 全集完成。每个非法输入导致整体失败，不因区域不被 WHERE 选中而跳过。绑定原有 metadata/grid 仍须完成，不以区域空隐藏 OM 错误。

## BoundRegionScan

组合现有 ReadOmBindData，而非派生 final 类：

- 原有 schema、SpatialBindConfiguration、SemanticAxes、用户必要谓词、来源会话与 metrics。
- 可选 immutable RegionCollection 和 `scan_mode = plain | regions`。
- region_id 输出槽位置；include_source 时源身份继续为末列。
- 已绑定 scalar expression 模板与准确签名。运行时状态不能存入共享模板。

默认 read_om 走 plain，不触发 spatial 查找。输出/过滤值依赖仍交 ProjectionPlan；新增 RegionId slot 不增加 RequiredVariableIds。

## RegionTaskCursor / RegionTask

Cursor 保存当前区域 ordinal、一个 NativeWindowCursor、已发行/完成任务计数；空区域跳过；只保存当前 cursor，不保存完整 region×window 清单。

Task 字段：`task_id`、`region_ordinal`、`NativeWindow`、选中轴上下文、任务位置向量与消费偏移。一个 task 只属于一个区域，位置严格递增。不同区域可以有同一 logical_index，但不在同一位置 batch 中混合。

并发不变式：领取 descriptor 在短锁内；坐标、envelope、spatial predicate 与 reader 在 worker 内锁外执行；任务终结成功/空/失败最多一次。全局当前区域改变不影响已发出的 task。

## RegionWorkerState

- 沿用 worker 独占 ReadAtFile/decoder、OM 缓冲和选中位置映射。
- `RegionPredicateEvaluator`：独占 ExpressionExecutor/local function state，输入 GEOMETRY constant vector + DOUBLE lon/lat，输出 BOOLEAN/selection vector。
- 当前 region ordinal、owned/参考安全的当前 geometry、≤256 点 scratch、当前批次 exact survivors。
- 仅保留一个区域的 evaluator 数据，不保存遍历过的区域或所有 prepared geometries；跨区域切换不泄漏状态。

区域关系先于 BuildSelectedBatchSegments / DecodeSelection；零 survivor 继续迭代，不能提前返回 EOF。计数查询同样经过 geometry evaluator，但不创建值 decoder。

## RegionSample / RegionStatistics

RegionSample 的身份是 `(region_id, object identity, original logical_index)`；坐标相等不合并记录。其余字段直接沿用读取的值、lat/lon、valid_time、level/lead_time/member/run，以及可选 om_source。

统计不是扩展内持久实体：SQL 按选择键聚合 RegionSample。总记录数与有效值数分别记录；空分组通过用户显式区域×时间（必要时×member）集合左连接补齐，不能 COUNT(*) 统计左连接占位行。默认不承诺一个没有时间证据的文件提供时间轴。

## 状态转换

```text
unbound → validating → bound → initialized → running → drained → succeeded
                 ↘ error       ↘ cancelled/failed       ↘ cancelled/failed
running → early_stopped (例如 LIMIT；SQL 可成功但 scan_complete=false)
```

- 全部区域和参数校验完毕之前不输出数据。
- drained 要求 cursor exhausted 且 outstanding_tasks=0。
- exact match count 只有完整成功或完整证明空时权威；错误、取消、提前停止为 NULL。
- 绑定失败有诊断，但若尚未创建 scan，不保证存在 last_scan_metrics 行；不得伪造运行期失败指标。
- QueryEnd 一次发布；读取 metrics 不覆盖最近一次扫描。第三方 GEOS 内存未计量不改变 NULL 的含义。
