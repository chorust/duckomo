# Data Model: 维度、读取会话与并行状态

状态：规划设计。现有文件名为复用点，其余类型是实施目标；不新增持久数据库。

## SemanticAxis

| 字段 | 类型 / 约束 |
| --- | --- |
| semantic | time / level / lead_time / member / run |
| output_name / logical_type | time → valid_time TIMESTAMP；其余见 SQL 契约 |
| axis_name / axis_index / axis_length / stride | 从已验证有序轴身份绑定；长度为正，乘法检查溢出 |
| coordinate_source | explicit_values / regular / om_time / legacy_valid_times / snapshot |
| values 或 start/step | 二选一；强类型，不同语义不共用 DOUBLE 容器 |
| unit / level_kind | 封闭允许集合；时间统一微秒 UTC，时长为有符号微秒 |
| evidence | 文件节点路径或参数来源、必要格式版本 |
| ordering | increasing / decreasing / constant / unordered |

普通映射必须对应一个原始轴；time 的既有标量快照单独表示为 ConstantCoordinate，不能被新功能推广为任意维度广播。一个语义至多一个轴，一个轴至多一种语义；配置空间轴不能再赋非空间语义。向量坐标逐位置保留重复值。regular 可零步长表示重复坐标，所有生成值必须可表示；正负步长都允许。源时间数组与显式声明必须逐位置一致。

## BoundScanSchema 与 OutputColumn

BoundScanSchema 复用现有变量、shape、有序轴、网格布局和不可变元数据 owner，新增 semantic_axes 和 output_columns。OutputColumn 为 `{name,type,kind,source_index}`，kind 为 Value/Latitude/Longitude/SemanticCoordinate/ConstantCoordinate；ProjectionPlan 引用描述符，不再根据固定列偏移推断含义。

输出顺序：现有值列、可选 latitude/longitude、可选 valid_time，再按 level/lead_time/member/run 追加已启用列。无新声明的既有 schema 保持不变。列名按 DuckDB 标识符等价规则检查冲突。值输出和过滤依赖共同构成 value_dependencies；坐标来源读取不能混入值变量计数。

## TypedPredicate 与 QuerySelection

- TypedPredicate：绑定表/列身份、受支持比较操作、已验证精确类型常量；只保留安全 AND 子句。原 WHERE 不被删除或修改。
- AxisSelection：axis_index、半开区间序列 `[begin,end)`，范围内按原索引递增、区间合并且互不重叠；未约束为全轴。
- QuerySelection：shape、受检 strides、各轴选择、展平空间游标、mode、fallback_reasons、候选行数上界、fragment_budget。
- 常量快照条件为真时不缩小位置，为假时整个选择为空。member 的非二进制 collation、隐式转换和非固定时长谓词安全回退。
- 显式向量按原位置比较后合并；默认每轴至多 65,536 个区间，超出则整轴回退，不截掉尾部。规则坐标与整轴选择保留紧凑描述，不分配全域点表。
- 展平空间选择由原 SpatialLayout 把地理范围映射到同一 point 轴的区间流，联合其他轴而不丢掉空间相关性。

SelectionCursor 惰性生成与原 row-major 索引对应的有界 DecodeSelection；任一轴为空立即进入 empty。空选择和没有值依赖的路径不创建值 decoder。

## ScanTask 与 Global/LocalState

ScanTask `{id, ordinal_begin, ordinal_end}` 描述选择枚举序列中不重叠的候选位置窗口，ordinal 不等于原文件线性位置。任务以 65,536 个候选位置为默认目标；内部 cursor 再切成不跨最内连续轴、至多 STANDARD_VECTOR_SIZE 的读取段。窗口边界通过每轴选中数量和前缀计数解码，展平空间区间流使用紧凑行前缀，不生成全域任务列表。

GlobalState：不可变 schema/selection/metadata、有界任务分配器、最大工作者数、共享 termination、首个错误和 ScanMetrics。任务 `pending → claimed → completed` 或 `failed/cancelled`，失败后不重排已输出任务；仅可重试尚未提交结果的网络请求。

LocalState：worker_id、独占文件会话/句柄、每依赖变量的 decoder、稳定参数 vector、scratch/index/data buffers、本任务局部 cursor。至少成功领取并处理一个非空任务才计 active_worker。同一向量的全部依赖列成功后才提交输出。

终止流程：首个错误或中断设置共享停止状态 → 不再领取任务 → 工作者在任务/请求/解码边界检查终止 → 释放本地资源 → QueryEnd 确认最终 SQL 状态。成功 worker 不能覆盖失败状态。LIMIT 提前结束可为查询成功但 scan_complete=false，不能作为完整扫描性能证据。

## ObjectIdentity 与 RemoteReadSession

ObjectIdentity：scheme、原始规范 URI（仅内部）、endpoint、size、version_kind(strong_etag/version_id/unverified)、version_token、access_partition、redacted_display_id。URI 解析必须保留会影响对象或授权的查询参数，不能按展示脱敏结果生成缓存键。

RemoteReadSession：scan_id、权限保留的 opener、observer、对象身份、读策略、transport counters、共享终止标记。状态为 `new → opened_and_revalidated → reading → closed`，任何步骤可 `failed/cancelled`；元数据与值读取使用相同对象版本。多个工作者新开句柄必须匹配绑定时的对象身份。

每次查询重新执行授权元数据请求；强版本一经建立则用于后续条件请求。无可靠版本时仅本次查询可复用，跨查询缓存关闭。响应协议或版本错误使整个扫描失败，失败范围不进入缓存。

## SessionRangeCache

连接的 ClientContextState 拥有唯一缓存；键为 `{object_identity,access_partition,version,offset,length}`，值为不可变字节与 LRU 信息。精确命中或一个已有范围完全包含请求时可 copy-out；不在首版拼接多个条目或扩大网络读取。并发 miss 可以重复读取，统计真实发生的请求。

容量默认 67,108,864 字节。预算包括缓存拥有的 payload 容量、对象键、条目与索引/LRU 分配，使用受限 allocator 统一核算；固定缓存控制对象单独报告。复制给 caller 的临时和 decoder 缓冲不归缓存占用，计入 query working memory。分配前淘汰，不足或单条超预算则旁路，不先分配再补偿；失败插入不能丢失可用查询结果。清理使所有条目失效并返回释放量，设置容量减小立即淘汰到新上限。

本地文件首版仅查询内复用（无跨查询强版本假设）；HTTP/S3 强版本可跨查询复用。连接销毁清空缓存和观测历史。

## QueryProfile

SessionState 保存最近一个包含 read_om 的 SQL query 及其每个 scan 的不可变结果快照；SQL query_id 与 scan_id 区分同一语句内多次扫描。QueryEnd 是唯一权威发布者，析构只负责资源释放和未发布状态的失败兜底，不重复覆盖。

profile 状态：binding/running → success/failure/cancelled；完整性另有 scan_complete、transport_count_complete、decode_count_complete。包含 typed axes 描述、selection、每阶段逻辑读取、底层实际读取、响应 body、cache、task/worker、耗时及内存 scope。完整字段见 [观测契约](contracts/validation-evidence.md)。SQL 文本和对象路径对凭据脱敏；profile 读取本身不替换最近扫描记录。
