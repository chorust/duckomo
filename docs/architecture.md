# DuckOMO 技术架构

当前工作树实现本地 OM 扫描、语义维度选择、列裁剪、规则/投影/Gaussian 网格、多区域 polygon 扫描、nearest 区域代表值采样、native-window 空间选择、opt-in source 身份、grid-info 描述、DuckDB 并行任务及 v4 指标。005 的 points 区域功能使用匹配版 spatial adapter、受检 WKB、惰性区域任务和逐 query owned-memory accounting；版本矩阵和 P6 审计状态见 [005 版本矩阵](../specs/005-polygon-region-statistics/evidence/version-matrix.md) 与 [P6 摘要](../specs/005-polygon-region-statistics/evidence/p6-summary.md)；P7 独立复现由未参与实现者执行。006 nearest 在既有 `read_om_regions` 内以 `method` 选择，points 默认兼容；006 已完成声明范围内的 N0–N7 验收：Linux AArch64、DuckDB v1.5.4/v1.5.5/v1.5.6、合成规则球面网格与固定 NCEP GFS025；真实输入仅私有留存、不公开分发，N7由独立用户执行。 [验收报告](../specs/006-region-nearest-sampling/evidence/final.md)。

## 模块职责

| 模块 | 职责 | 源码 |
|---|---|---|
| DuckDB table function | 绑定参数、稳定 schema、分析空间/语义条件、生成任务并输出 DataChunk | `src/scan/read_om.cpp` |
| Region input/geometry | 严格审计 WKB/ID/预算；逐区域运行官方 spatial `covers`，隔离 worker expression state 并轮询取消 | `src/scan/region_input.cpp`、`src/scan/region_geometry.cpp` |
| Schema / dimensions | 变量命名、共享 LIST/逐变量 MAP 归一化及共同的 shape/有序轴/语义坐标校验 | `src/scan/schema.cpp`、`dimensions.cpp`、`semantic_axes.cpp` |
| ProjectionPlan | 保留输出与过滤依赖，去重物理变量并保留输出顺序 | `src/scan/projection.cpp` |
| GridDefinition / SpatialLayout | 封闭 Regular/Rotated/Lambert/Stereographic/Gaussian 定义，映射原生坐标和真实 strides | `src/grid/`、`src/include/duckomo/grid_definition.hpp` |
| DomainRegistry | 固定 68 个规则 domain 和 version=1 定义；保留独立 provenance/evidence 状态，canonical grid identity 不含证据字段 | `src/grid/domain_registry.cpp`、`test/data/grids/definitions.json` |
| Spatial/axis selection cursor | 提取安全必要条件，按原生最快空间轴切成有界窗口；worker 在任务锁外做可取消 preflight | `src/scan/spatial_filter.cpp`、`spatial_selection.cpp`、`axis_filter.cpp`、`axis_selection.cpp` |
| 本地/远程 ReadAt 适配层 | 元数据遍历、标准文件接口受检读取及应用计量 | `src/om/local_file.cpp`、`remote_file.cpp` |
| 官方 HTTPFS | 凭据、签名、传输、重试与上游缓存 | DuckDB 官方安装的运行依赖 |
| 官方 OM C reader | 格式解析、物理 chunk 选择、LUT / 字节请求与解码 | `third_party/om-file-format/` |

## 查询流程

```text
DuckDB SQL
  → Bind：读取元数据，校验语义轴、变量、grid/domain、CRS profile、区域全集与布局，确定 schema
  → RemoteReadSession（远程）：经当前 ClientContext 打开官方文件系统句柄，检查可观察长度/版本身份
  → 优化：收集安全空间/语义必要条件，保留完整 WHERE
  → GlobalInit：计算受检记录/window 上界，初始化共享惰性 cursor
  → DuckDB workers：短锁领取不重叠 native window；锁外做坐标 preflight；每个 local state 持有独立句柄和 decoder
  → ReadAt：按 OM 请求范围读取，计量逻辑请求与标准文件接口成功读取；远程 transport 不可观测
  → 官方 OM C reader：请求 chunk 索引/数据范围并解码
  → DuckDB Vector/DataChunk
  → DuckDB 执行完整 WHERE 和上层 SQL 运算
  → QueryEnd：发布本 SQL 中每个 scan 的不可变 v4 终态快照
```

不带空间或语义条件时沿用完整 row-major 逻辑扫描；未映射轴仍参与行索引，不被折叠。task cursor 不物化全域行或任务表。查询成功与扫描完整是两个状态：下游 LIMIT 可以令 SQL 成功、`scan_complete=false`。

区域points扫描在绑定阶段先验证并保留整个 regions 输入，随后为每个区域和原始空间窗口惰性生成任务。worker只持有当前区域geometry与bounded point batch；官方 spatial 执行最终 `covers` 判断。nearest模式为每个区域×原始非空间轴组合生成一行，使用官方代表点函数取得面内目标，再由独立规则球面locator选取网格源。两种模式都在 SQL侧按region_id/time/member等分组，不在扫描器里预聚合。区域集合、geometry opaque copy、bound predicates、task/worker scratch均纳入DuckOMO query-owned memory accounting；spatial/GEOS内部内存仍单列unknown，进程RSS作为独立增长检查。取消和异常在task/QueryEnd汇合时发布不完整终态并清理状态。

## 条件分析与正确性

地理输出列固定为 `lat/lon`；内部输出角色仍用 Latitude/Longitude，过滤按列绑定和角色分析，不靠名称字符串推断轴。原生轴、网格参数及 provenance JSON 保持原有语义，迁移见 [接口修订](interface-migration.md)。

启用 projection pushdown；普通 `filter_pushdown` 和 `filter_prune` 关闭。filter callback 校验当前 `LogicalGet` 的表/列绑定和引用深度，仅提取可证明安全的经纬度与语义轴必要条件。它不删除或改写 `WHERE`，不跨生命周期保存表达式指针。

有限常量比较、`BETWEEN` 和安全 `AND` 可缩小候选。跨接缝最多两个完整安全经度区间可作并集；任一不安全 `OR` 分支会令整个 `OR` 回退。完整 DuckDB `WHERE` 始终保留。候选必须覆盖所有匹配行，精确过滤由 DuckDB 执行。

`SpatialLayout` 把逻辑索引映射到分离轴或明确存储顺序的展平轴。投影/旋转按与输出相同的坐标函数逐原生点筛选；Gaussian 使用逐行点数及区域 local 段，不从 BBOX 重建偏移。每个 native window 最多 65,536 点，selector 最多保留 4,096 ranges；超预算时在窗口发出候选前扩大该窗口并保留 residual。nearest 使用 `sphere_haversine_f64_v1` 对合格规则球面网格穷举候选，以距离、原始 point index 排序并周期检查取消；超距结果保留候选坐标/距离但不发布有效 source。多轴 stride 决定原始 logical positions，非空间组合中重复评估的坐标成本计入 `coordinate_evaluations`。points不物化全域坐标或任务表；nearest不物化所有空间坐标或 region×axis任务表。COUNT与几何leaf可零值I/O；值与变量状态投影按实际依赖读取值数组。

## I/O 与指标

网格/轴选择只提供逻辑切片。OM chunk 交集、LUT、字节范围和解码策略由官方 reader 负责。`ReadAtFile` 通过 DuckDB 标准文件系统读取。本地/远程 Adapter 携带 ClientContext；每 worker 独立句柄。远程使用 opener 局部配置禁止完整下载回退并保留 ETag 检查，使用标准 DIRECT_IO 让 OM reader 控制范围；比较打开时可观察长度/版本。HTTPFS 内部缓存和网络字节不由 DuckOMO 观测，远程 transport 为 NULL/false；服务端日志仅用于验收。

Spec 008 实现了远程读取请求合并与 DuckDB 2.0 原生异步 I/O 调度：
- **请求合并**：在 `OmV3Reader` 中将范围解码解耦为 `PlanSelection`（根据 `duckomo_remote_merge_gap` 与 `duckomo_remote_max_request` 规划物理读取范围与缓冲区）、`FetchSelection`（执行 I/O 读取到 planned 缓冲区）与 `DecodeSelectionPlan`（驱动解码器解码）。本地同步路径保留逐块读取和立即解码，复用既有容量受限缓冲，不分配远程计划载荷。历史 O1280 loopback 证据记录了 92.8% 的请求数下降；该记录来自修复前构建，不能替代当前构建的性能验收。指标按成功物理读取统计有用字节，预读与未选择 chunk 记为 gap，缓存复用不重复计数。
- **异步调度**：DuckDB 2.0 下开启 `duckomo_async_io = true` 时，`read_om` 将计划好的远程数据块封装为 `RemoteReadAsyncTask` 通过当前 pinned 2.0 的 `AsyncResult(tasks)` 返回 BLOCKED，由引擎 executor 调度、完成后唤醒。相同批次内完全相同且 useful_bytes 相同的待读范围共享一次取数及其载荷；请求分配给有限数量的顺序队列，每个 worker 的队列数量受 `duckomo_async_max_inflight` 与 BufferManager 动态剩余内存联合约束，每批重算。实验设置 `duckomo_async_prefetch_batches` 可把单变量无过滤全扫扩为最多4个输出批次的取数窗口，逐批解码输出；默认1关闭，因为HTTP/S3固定样本的请求数和耗时增加。该窗口不等于I/O与解码重叠的完整流水线，性能实测见 [prefetch report](../evidence/008/prefetch/report.md)。载荷缓冲通过共享所有权计入 Decoder 账本，任务控制存储计入 RemoteInflight；指标记录实际执行中的请求数。
- **并发与兼容安全**：2.0 使用 `PARALLEL_ACCESS | DIRECT_IO` 句柄执行并发 positional read，1.5.x 句柄保留串行互斥保护。异常与取消时任务共享首个错误并跳过剩余读取，引擎完成已提交任务后再销毁私有缓冲；DuckDB 1.5.x 保持同步读取路径不变，`async_applied` 始终报告 false。
- **可观测性**：`remote_io` 对象准确记录合并设置、生效预算、`async_applied`、`inflight_peak`、`inflight_budget_bytes`、各阶段请求数与有用/缺口字节数，本地扫描报告为 null。详细证据见 [success-criteria checklist](../specs/008-duckdb2-async-io/checklists/success-criteria.md)。

批内连续逻辑位置使用完整尾轴组成矩形读取；不完整尾轴、缺口和未对齐起点仍拆段，输出源顺序和选取集合保持不变。此优化减少可合并扫描的decoder调用及重复解码，既有64KiB映射预算与reader容量界限保留。decoder仅复用最近成功的index/data缓冲范围，不缓存解码值。请求合并可能增加读取字节，局部窗口也可能不受益，实测边界见[连续矩形性能记录](../specs/005-polygon-region-statistics/evidence/contiguous-rectangle-performance-20261020.md)。

远程打开和范围读取复用脱敏失败分类器；只从 DuckDB 公开异常类型及严格校验的 HTTP status 选择固定类别/建议，不转发异常正文、response body、reason 或 headers。不通过字符串猜测未知 HTTP 状态，不影响取消和成功读取计数。

自有 RangeCache、provider/observer、授权 HMAC、专用 ABI handshake 和缓存 SQL 已删除。HTTPFS 的凭据、签名、重试及内部缓存由官方依赖维护；DuckOMO 不承诺固定 HEAD/探测顺序、扫描快照、每次查询强制刷新或任意缓存即时撤权。固定版本/配置的同 URI 允许→拒绝→恢复、取消及隔离有受控验证，见 [官方 HTTPFS 记录](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md)。

每个 query 的 v4 profile 保留完整 `legacy_v3`（含 `legacy_v2`），逐 scan 记录 bind/scan metadata、coordinate、逐变量 index/data、未知 transport、已移除 cache、任务/worker、exact/observed/upper candidate、完整性和终态。`peak_rss_bytes` 单列为进程峰值；`peak_query_owned_bytes` 是当前接入账本的 DuckOMO-owned capacity 峰值，不含DuckDB output vectors 和引擎/httpfs 内存。当前冻结网格范围的 selector/task/batch/decoder 等 owned ledger、reader 释放和会话压力已通过 H5；这一结果不覆盖上游 HTTPFS 内部分配或全部查询工作集。QueryEnd 是最终发布点，失败/取消保留已观察成本；同 SQL 的多 scan 与不同连接分别隔离。详见 [004 selection/I/O 契约](../specs/004-multi-grid-selection/contracts/selection-and-io.md)。

性能比较要求同文件、同值列、同环境，完整消费结果并检查计数完整性。数据字节表示应用读取量，不代表磁盘物理 I/O。固定多块样本的 temperature 全扫 / 25 点窗口分别读取 **165,767 / 1,795 字节**，解码 **503 / 5 块**；仅坐标、纯空间 count 和空选择的值 index/data/decode 均为零。证据见 [US2](../specs/002-spatial-pushdown/evidence/us2.md) 和 [US3](../specs/002-spatial-pushdown/evidence/us3.md)。其他块布局不保证相同收益。

## 网格来源与验证

Registry 的定义注册不等于真实验收。004 的 rotated、Lambert、stereographic 与 O1280 四个 Open-Meteo 定义已在五冻结对象、Linux AArch64、v1.5.4/v1.5.5/v1.5.6 范围通过 H0–H9，登记 remote-benefit-validated，见[最终验收](../specs/004-multi-grid-selection/evidence/current-20261021/report.md)和[逐定义证据](grid-domains.md)。坐标采用 pinned producer 独立参考，不声明原生 GRIB 等价；canonical grid/layout IDs 不因证据等级改变。真实 N160/N320/N 区域按批准范围排除，合成回归保留；其余定义不继承验收。

`make test` 构建 release 并调用 `scripts/validate.sh`，覆盖 SQL/native、本地维度与指标以及 Python 契约/fixture 工具测试。004 当前范围的完整本地/远程、owned 内存、三版本匹配/拒绝及独立 AI H9 已通过，见[最终报告](../specs/004-multi-grid-selection/evidence/current-20261021/report.md)；实际验收产物来自变更 TU 重编译/relink，非 clean full build。真实 N-grid、2.0 和非 AArch64 平台不在范围。`make sanitizer-test` 检查边界与生命周期；性能结论取普通 release 构建。

原完整验收与独立复现在 Linux AArch64 通过，x86_64 支持与验证暂缓。步骤见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md)，结果见 [最终验收](../specs/002-spatial-pushdown/evidence/final.md)。后续维度、网格、远程读取和科学算子见 [Roadmap](roadmap.md)。
