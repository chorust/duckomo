> 2026-10-08：当前未发布版本使用 [官方 HTTPFS 迁移契约](../../004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)。下文专用 ABI、LRU 或 observer 的条款/命令为历史约定，已被本次契约替换。历史证据状态保持，当前复现请见 [官方流程](../../../docs/official-httpfs.md)。

# Validation and Profiling Contract

状态：本地实现已接入；远程门禁仍待受控服务验证。继承现有 [v2 观测契约](../../002-spatial-pushdown/contracts/validation-evidence.md) 的字段含义，新增 schema_version=3；旧 v2 验收读取器保留并显式分版，不静默重释 bytes_fetched。

## 指标

| 字段 / 分组 | 计量规则 |
| --- | --- |
| schema_version, query_id, scan_id | v3；同一 SQL 多个 read_om 使用同 query_id、不同 scan_id |
| status, scan_complete | SQL success/failure/cancelled 由 QueryEnd 决定；LIMIT 等提前结束令 scan_complete=false |
| axes, selection_mode, fallback_reasons | 记录含义/单位/轴映射及 full/restricted/empty/fallback/optimizer_empty；不复制巨大坐标向量，使用来源 hash |
| metadata、coordinate、variables | metadata 分 bind/scan；coordinate 单列 logical/physical index/data 请求与 decoded_chunks；variables 只记录值数组各自的 index/data/decode |
| logical_requested_bytes/requests | reader 请求量，含缓存命中；不是网络量 |
| bytes_fetched/read_requests | 兼容 v2：实际从缓存以下文件适配器成功取得的范围量，按读取类别相加；命中不增加 |
| transport_body_bytes / attempts / responses | 响应事件实测，含 HEAD 探测、重定向和重试；本地为 0，能力不足不是 0 |
| transport_count_complete / decode_count_complete | 对部分失败如不能完整统计必须为 false；成功的必需证据必须完整 |
| cache | enabled、capacity、charged_bytes、peak_charged_bytes、control_bytes、hits、misses、hit_bytes、evictions、bypass_reason、version_strength |
| tasks | created、claimed、completed、failed、cancelled、active_workers、max_active_workers；同任务/worker 去重 |
| candidate_rows / scanner_rows | 读取候选位置与提交给 DuckDB 残余过滤前的行数；不要冒称最终 WHERE 后行数 |
| result_rows | 完整 SQL 结果行数，harness 完整消费后提供；产品路径无法取到时为 NULL 并注明 unobserved，不以 scanner_rows 替代 |
| elapsed_ms | 绑定至查询终止；阶段耗时另列，失败记录仍有耗时 |
| peak_query_owned_bytes / query_memory_count_complete | 汇总 DuckOMO bind/scan/worker 的 OM 元数据 payload、decoder 固定状态及参数/index/data/scratch vector capacity、坐标数组、选择游标和活跃批次 segment/position capacity。scope 字段固定为 `duckomo_owned_buffer_decoder_selection_capacities`；不含共享会话范围缓存、DuckDB 输出 vector、httpfs/引擎内部内存及 allocator bookkeeping。正常 SQL 结束时提供 high-water mark 并标完整；失败、取消、溢出或计数失效时写 NULL/false，不把未知写成 0 |
| peak_rss_bytes / memory_scope | `getrusage(RUSAGE_SELF)` 的进程历史 RSS 峰值并标 process；性能门禁采用独立子进程，不能称并发单查询独占 RSS |
| input_identity / environment | 内容哈希或版本、依赖版本/patch hash、架构、线程、缓存状态、服务版本；敏感 URI/SQL 脱敏 |

coordinate 与各变量的 index/data 类别相加后应与 `physical_read_bytes` / `physical_read_requests` 中的成功 OM 读取总量一致；`legacy_v2.bytes_fetched/read_requests` 继续保留原有 v2 范围，不用于替代 v3 总量。网络可能因探测/重试大于底层读取量。缓存命中影响底层读取，不把必需 reader 逻辑请求抹掉。并行使用同一 aggregate 或一次合并 local delta，不能对每 worker 重复发布 global snapshot。QueryEnd 发布一次；失败不混进成功平均值。

## 资产与入口

继续扩展 duckomo_fixture_tool，新增 `duckomo_dimensions_validation` 和 `duckomo_remote_validation` 两个验收可执行目标。共享参数为 `--root --fixtures --output --duckdb --extension`；remote 另要求 `--httpfs --http-base --s3-base --s3-setup --server-log --real-file --real-manifest`。输出目录每次独立，必须有 summary.json、逐查询 v3 JSON、结果差分、来源与环境 manifest。任一必需输入缺失、哈希不符、指标不完整或比较失败均非零退出。

s3-setup 为测试环境准备的 SQL 文件，仅包含本地服务凭据配置；harness 不复制其内容进入 evidence，记录文件存在及经脱敏配置摘要。server-log 为本次运行专用的审计日志目录，包含 HTTP 与 S3 代理记录；request_id/scan_id 由服务与客户端事件关联，不在签名完成后添加会改变签名的头。

fixture 规划（在计时前固定）：

- `dimensions.om`：三个值变量，shape `[2,3,4,8,16,9,11]`，轴 `[run,member,level,lead,time,lat,lon]`；值按语义下标独立生成，包含缺测。变体交换实际轴顺序但保持相同逻辑事实。
- 坐标基准：两个 UTC 起报时刻、三个精确成员标识、四个压力层、八个时效、十六个有效时刻；额外变体覆盖非单调/重复/反向/单点、文本成员与冲突。一个文件同时映射 run/lead/time 不代表三者存在推导关系。
- `dimensions_perf.om`：三个值变量，shape `[4,128,83,127]`，轴 `[member,time,lat,lon]`，chunk `[1,8,8,16]`。非空间局部查询固定 member 的第二项与 time 的第 16–23 项，混合空间查询另选 latitude/longitude 中央 5×5 点。若生成器验证此格式 chunk 不支持则在任何计时前修订 manifest，不事后挑选最好案例。
- 并行基准完整扫描上述 perf 样本全部三个值变量；先锁定文件 hash、列、线程、release 编译和缓存状态，再分别运行 1 与 4 工作者上限各 5 次。交替顺序防止系统缓存漂移，报告全部样本与中位数；实际至少两工作者工作才可判为并行收益。
- 真实样本首先复用已有 ncep_gfswave025 的固定文件和 hash `0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd`，依据 test/data/domain-manifest.json 核对。将同一字节内容提供为本地/HTTP/S3 输入，并按原 domain 做完整结果核对；真实样本不必同时具有五类维度。

坐标 oracle 使用独立轴表/公式，值 oracle 使用官方 reader，不调用待测 SemanticAxis/SelectionCursor 生成参考。先完整物化基准后再过滤，使用双向 EXCEPT ALL 或逐逻辑位置比较；不能只用 count 证明值读取正确。缺测与行数精确相同，值容差 0；时间/成员/时长精确相同；空间坐标参考沿用事先记录的 1e-9 度容差，level 基准以规范输出值精确比较。容差不用于 WHERE。

## 门禁矩阵

| Gate | 覆盖 | 必须满足 |
| --- | --- | --- |
| G0 既有兼容 | raw/schema/projection/spatial/time_test，无 axes | 现有结果、列名、valid_times、标量快照保持一致 |
| G1 维度 | FR-001–005，SC-001 | 五类、两种轴顺序、规则/显式、重复/非单调/单点、单位与冲突全部符合 SQL 契约 |
| G2 联合条件 | FR-006–009，SC-002/004 | 单维与混合、值依赖、回退结果 100% 一致；非空间局部 data_bytes 与 decoded_chunks 均下降；零值场景精确为零 |
| G3 远程 | FR-010–012，SC-003 | 三类来源同内容同结果；HTTP/S3 冷局部 response body 均下降；协议/版本/权限错误不被吞掉 |
| G4 并行 | FR-013–014，SC-005/007 | 1/2/4 上限、本地/HTTP/S3、少任务/空/跨批次无重复遗漏；实际多 worker；5 次中位数收益；失败取消恢复 |
| G5 缓存 | FR-015–016，SC-006 | 同连接热查询 body 或 requests 下降；禁用/清理/小容量/淘汰正确；版本和撤权不读旧内容；占用不越限 |
| G6 观测 | FR-017–019，SC-007 | 服务端交叉核对，重试成本、失败完整性、多 scan/多连接隔离，脱敏，计数与总量一致 |
| G7 交付 | FR-020，SC-008 | 文档同步、真实样本证据、未参与实现者独立完成 quickstart 四项任务 |

错误集包含：越界映射、非法单位/时间、重复声明、列冲突、403/404、无 HEAD、200 回退、错误 Content-Range、短/长 body、超时、强 token 丢失/变化、无 token 缓存禁用、并行中单任务失败、主动取消与恢复。小容量及单条超额必须仍可正确查询。

## 执行范围与成功声明

make test 纳入无外部服务的 SQL/native、fixture 重生和本地维度验证；remote gate 使用独立命令和具备配套 httpfs 的环境。缺外部服务只能写“远程未执行”，不能令完整发布 gate 为通过。ASan/UBSan 检查边界和资源回收，耗时只取 release。

最终 evidence/final.md 逐项映射 FR-001–020 与 SC-001–008，记录命令、退出码、平台和限制。本阶段仅生成设计，没有任何新功能实测结果。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../../004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
