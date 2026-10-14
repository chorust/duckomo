# Execution, resource and metrics contract

## 共享读取而非第二个 scanner

`GetReadOmRegionsFunction` 与 `GetReadOmFunction` 共用 `read_om.cpp` 内部 binder/global/local/scan 回调。ReadOmBindData 增加可选 `shared_ptr<const RegionCollection>`；Copy/Equals 含区域内容、声明和模式，不能借既有 shared predicate 修改当前区域。新几何模块位于 scan 层，不让 grid/OM reader 依赖 DuckDB spatial。

1. **Bind**：检查新参数、整体区域集合和容量；系统 catalog 查找并绑定所需函数；WKB structural audit → geometry conversion → topology validation；复用所有值变量/轴/grid/domain 绑定。完整集合均校验后才可短路。保留区域数据的所有权，不借用参数临时字符串。
2. **Init**：注册一次 scan metrics，复用轴和安全 WHERE 必要条件；构造惰性的 region/window cursor。空集合、全 EMPTY 或已证明轴空可结束，不创建值 decoder。普通模式保持原路径。
3. **Claim**：global mutex 只领取 `{region_ordinal, NativeWindow, task_id}` 并计 outstanding；全局只持有当前 region/window cursor，不物化区域×窗口或区域×点列表。
4. **Preflight**：worker 锁外用 immutable 用户条件与任务区域 whole envelope 求交。native window 上限不变；budget fallback 只扩大候选，不能删除 geometry relation。
5. **Exact**：每次≤256 个候选使用同一网格输出坐标函数，执行 ST_Covers；按原始递增位置稳定压缩，丢弃空子批并继续迭代。一个 task/batch 只属于一个区域。
6. **Read/output**：只对 exact survivors 调用既有 BuildSelectedBatchSegments/DecodeSelection；无值依赖不创建值 reader/segments。追加区域 ID，om_source 使用原始位置。保留完整 WHERE，exact geometry matches 不是最终 SQL 返回行数。
7. **Complete**：每任务成功/空/异常都一次结算；全部区域游标耗尽且 outstanding=0 才能 full-complete。失败、取消、LIMIT 使用既有终态处理并释放 worker expression/reader 状态；QueryEnd 只发布一次。

任务中的 region ordinal 必须跨部分输出持久保存；不能从已前进的 global cursor 获取区域。多区域重复源位置是有意输出，不许混入要求严格递增的单个位置向量。首版允许重叠区域重复解码，指标据实累加，不承诺共享读取。

## Spatial adapter

- `RegionGeometryAdapter` 负责 bind-time conversion/validity 与创建 worker evaluator；`RegionPredicateEvaluator::Filter(lon,lat,positions)` 返回保持顺序的选择结果。
- 仅走系统 catalog 的准确签名：ST_GeomFromWKB(BLOB)→GEOMETRY，ST_IsValid(GEOMETRY)→BOOLEAN，ST_Point(DOUBLE,DOUBLE)→GEOMETRY，ST_Covers(GEOMETRY,GEOMETRY)→BOOLEAN。拒绝临时同名覆盖或不匹配函数，确认 spatial 已加载，禁止隐式 auto-install/load。
- FunctionBinder 执行正常 bind；ExpressionExecutor 为每 worker 独占。immutable bound template 可 Copy，local state 不共享。ConstantVector 只 reference 生命周期足够长的 owned GEOMETRY Value。
- 不依赖 scalar function 裸指针、不使用私有 geometry serialization 或直接 GEOS ABI；标准 WKB walker 只做输入审计，不做最终 point-in-polygon。
- 同区域的 geometry 在每个 geometry batch 作为左常量传入；不能承诺 spatial 内部 prepared geometry 跨调用缓存。
- GEOS 单次调用中不保证立即响应取消；WKB 遍历每≤256 顶点、每≤256 点 predicate 调用前后及既有 I/O 检查取消。测试记录延迟，不编造固定毫秒上界。

## 冻结预算

| 对象 | 上限/策略 | 超出时 |
|---|---|---|
| 输入集合 | 4096 区域；ID 1–256 字节 | 拒绝 |
| WKB | 1 MiB/区域，16 MiB 总量 | 分配/复制前拒绝 |
| 顶点 | 65536/区域，1048576 总量，含闭合重复点 | 受检计数/遍历拒绝 |
| 已拥有区域存储 | 64 MiB，计 ID/WKB/opaque GEOMETRY/描述符 capacity | 持久化前检查；清理后报 input limit |
| bind transient | 同时仅解析一个≤1 MiB WKB；opaque conversion 结果采用后立即释放临时对象 | 第三方瞬时容量未知单列，不声称受64 MiB涵盖 |
| 全局任务 | 一个区域 cursor + 一个 window cursor + O(workers) outstanding | 惰性，不保存完整任务清单 |
| shared predicates | 沿用≤1 MiB | 安全扩大候选 |
| native window | ≤65536 位置；task position map≤512 KiB | 分窗口 |
| selector | ≤256 KiB / 4096 ranges | whole-window 候选回退，精确筛选仍做 |
| geometry batch | ≤256 点；DuckOMO 自有新增 scratch≤64 KiB/worker | 缩批，最小批仍不满足则失败 |
| geometry transient | 每 worker 只持有当前区域的一个 opaque copy（≤2 MiB）及 expression state | 更大 opaque region 在 bind 时拒绝；不缓存历史区域 |
| output/segment | 输出≤STANDARD_VECTOR_SIZE；既有 mapping≤64 KiB | 沿用受检缩批 |

64 MiB 区域存储是相对现有 metadata/grid 额外的输入预算，不是总查询内存上限。caller 的 list 聚合、DuckDB output vectors、expression/spatial/GEOS 内部分配、HTTPFS 与进程 RSS 分别记录，不能纳入虚假自有总量。受检乘加同时保护区域×记录的上界计数，溢出不得 wrap。

Memory gate 同时要求：固定相同区域输入、chunk、线程和相近窄结果，≥10×空间点数的自有 scan work peak≤2×；独立进程 total peak RSS 比例≤2×，并记录输入/启动基线、依赖内存不可细分的限制。RSS 是额外的外部增长约束，不是 query-owned 精确计量；任何无法解释的增长或缺测都不能标完整 gate pass。

## Metrics v4 additive contract

保持普通扫描字段、legacy_v3/v2 原义和 `duckomo_last_scan_metrics` schema。不改为每区域一条 scan；一次调用仍对应一个 scan。区域模式 `operation='read_om_regions'`，新增 `regions`：

- `input_region_count`、`empty_region_count`、`input_wkb_bytes`、`input_vertex_count`、`owned_input_peak_bytes`：受检非负整数。
- `count_unit='region_record_pair'`；不暗示 distinct source points。
- `candidate_pairs_observed`、`geometry_tested_pairs`、`matched_pairs_observed`、`geometry_batches`、`geometry_evaluations`、`envelope_rejected_pairs`：实际计数，不由输出量倒推。
- `matched_pairs_exact`：只有扫描成功且游标/所有 task 完整结束时为整数，否则 NULL；已证明空且完成时为0。它是安全轴/空间必要条件与精确 geometry 之后、完整 SQL residual 之前的计数。
- `match_count_complete`：对应 exact 的完整性。多个时间/member 计为不同 pair，同源点跨区域分别计。
- `geometry_prepare_ns`、`geometry_evaluate_ns`：实测，不从查询耗时估计。
- `owned_work_peak_bytes`、`owned_work_bound_bytes`：包括既有 scanner/reader/decoder/selector/batch 与新增 region scratch/task 的完整自有扫描工作容量；区域输入存储单列，不能只计新缓冲来制造≤2×结论。新增计量接入原 ledger；终态不完整时遵守既有 NULL/complete 规则。
- `spatial_internal_peak_bytes=null`、`spatial_internal_memory_complete=false`；不是零。进程 RSS 仍为 process scope。

原候选/任务字段在新 operation 下计 region-record pairs，并通过上述单位明确；普通 read_om 不改变。远程 transport 仍 NULL/complete=false，本地0/true，自有 cache removed。服务端发送量是独立 evidence artifact，不写成客户端 transport。指标和错误不记录原始 WKB、区域文本、secret 或完整敏感 URI。
