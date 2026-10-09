# duckomo 技术架构

当前工作树实现本地 OM 扫描、语义维度选择、列裁剪、规则/投影/Gaussian 网格、native-window 空间选择、opt-in source 身份、grid-info 描述、DuckDB 并行任务及 v4 指标。004 的真实坐标/值、远程收益、完整内存 ledger、2.0 版本矩阵和独立复现仍未闭环。用户接口见 [接口说明](spec.md)，定义与证据等级见 [多网格证据表](grid-domains.md)。

## 模块职责

| 模块 | 职责 | 源码 |
|---|---|---|
| DuckDB table function | 绑定参数、稳定 schema、分析空间/语义条件、生成任务并输出 DataChunk | `src/scan/read_om.cpp` |
| Schema / dimensions | 变量命名、shape、有序轴身份及五类语义坐标校验 | `src/scan/schema.cpp`、`dimensions.cpp`、`semantic_axes.cpp` |
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
  → Bind：读取元数据，校验语义轴、变量、grid/domain、CRS profile 与布局，确定 schema
  → RemoteReadSession（远程）：HEAD + bytes=0-0 授权/范围探测并绑定对象身份
  → 优化：收集安全空间/语义必要条件，保留完整 WHERE
  → GlobalInit：计算受检记录/window 上界，初始化共享惰性 cursor
  → DuckDB workers：短锁领取不重叠 native window；锁外做坐标 preflight；每个 local state 持有独立句柄和 decoder
  → SessionRangeCache / ReadAt：精确读取，计量逻辑、底层和每 attempt body
  → 官方 OM C reader：请求 chunk 索引/数据范围并解码
  → DuckDB Vector/DataChunk
  → DuckDB 执行完整 WHERE 和上层 SQL 运算
  → QueryEnd：发布本 SQL 中每个 scan 的不可变 v4 终态快照
```

不带空间或语义条件时沿用完整 row-major 逻辑扫描；未映射轴仍参与行索引，不被折叠。task cursor 不物化全域行或任务表。查询成功与扫描完整是两个状态：下游 LIMIT 可以令 SQL 成功、`scan_complete=false`。

## 条件分析与正确性

启用 projection pushdown；普通 `filter_pushdown` 和 `filter_prune` 关闭。filter callback 校验当前 `LogicalGet` 的表/列绑定和引用深度，仅提取可证明安全的经纬度与语义轴必要条件。它不删除或改写 `WHERE`，不跨生命周期保存表达式指针。

有限常量比较、`BETWEEN` 和安全 `AND` 可缩小候选。跨接缝最多两个完整安全经度区间可作并集；任一不安全 `OR` 分支会令整个 `OR` 回退。完整 DuckDB `WHERE` 始终保留。候选必须覆盖所有匹配行，精确过滤由 DuckDB 执行。

`SpatialLayout` 把逻辑索引映射到分离轴或明确存储顺序的展平轴。投影/旋转按与输出相同的坐标函数逐原生点筛选；Gaussian 使用逐行点数及区域 local 段，不从 BBOX 重建偏移。每个 native window 最多 65,536 点，selector 最多保留 4,096 ranges；超预算时在窗口发出候选前扩大该窗口并保留 residual。多轴 stride 决定原始 logical positions，非空间组合中重复评估的坐标成本计入 `coordinate_evaluations`。不物化全域坐标或任务表。空选择、仅坐标和无值依赖计数不读取值变量，值过滤依赖仍保留。

## I/O 与指标

网格/轴选择只提供逻辑切片。OM chunk 交集、LUT、字节范围和解码策略由官方 reader 负责。`ReadAtFile` 通过 DuckDB 标准文件系统读取。本地/远程 Adapter 携带 ClientContext；每 worker 独立句柄。远程使用 opener 局部配置禁止完整下载回退并保留 ETag 检查，使用标准 DIRECT_IO 让 OM reader 控制范围；比较打开时可观察长度/版本。HTTPFS 内部缓存和网络字节不由 DuckOMO 观测，远程 transport 为 NULL/false；服务端日志仅用于验收。

RangeCache 属于 ClientContextState，强 ETag/S3 VersionId 与访问分区参与 key。只复用精确或包含范围，先淘汰再分配；弱/无版本对象禁止跨查询复用，每次查询仍执行权限探测。当前 004 尚未完成按每次绑定的 secret/endpoint/region fingerprint 变更失效和 ABI3 provider handshake 验收，不把这些能力描述为已关闭门禁。

每个 query 的 v4 profile 保留完整 `legacy_v3`（含 `legacy_v2`），逐 scan 记录 bind/scan metadata、coordinate、逐变量 index/data、未知 transport、已移除 cache、任务/worker、exact/observed/upper candidate、完整性和终态。`peak_rss_bytes` 单列为进程峰值；`peak_query_owned_bytes` 是当前接入账本的 DuckOMO-owned capacity 峰值，不含DuckDB output vectors 和引擎/httpfs 内存。selector/task/batch/decoder 等的完整逐项 bound ledger、瞬时 reader buffer 及 transport-control 峰值仍在核对，不能据总数宣称全部查询工作集已闭环。QueryEnd 是最终发布点，失败/取消保留已观察成本；同 SQL 的多 scan 与不同连接分别隔离。详见 [004 selection/I/O 契约](../specs/004-multi-grid-selection/contracts/selection-and-io.md)。

性能比较要求同文件、同值列、同环境，完整消费结果并检查计数完整性。数据字节表示应用读取量，不代表磁盘物理 I/O。固定多块样本的 temperature 全扫 / 25 点窗口分别读取 **165,767 / 1,795 字节**，解码 **503 / 5 块**；仅坐标、纯空间 count 和空选择的值 index/data/decode 均为零。证据见 [US2](../specs/002-spatial-pushdown/evidence/us2.md) 和 [US3](../specs/002-spatial-pushdown/evidence/us3.md)。其他块布局不保证相同收益。

## 网格来源与验证

Registry 固定 68 个规则网格定义和 6 个 004 source-derived definition，不自动发现或扩展。004 目前有 rotated 与 stereographic 的真实 Open-Meteo OM v3 metadata-checked 样本；N160、N320、N320 区域仍为 definition-recorded。Open-Meteo 公共 bucket 确实有 ECMWF HRES O1280 reduced-Gaussian OM v3 对象，HSURF 全量值按 logical index 与官方 OM C reader 一致，但 O1280 行长/坐标点序未映射，不能替代 N-grid 覆盖。逐对象范围见 [多网格证据表](grid-domains.md) 和[样本获取记录](../specs/004-multi-grid-selection/evidence/baseline-local/open-meteo-s3-sample-acquisition.md)。

`make test` 构建 release 并调用 `scripts/validate.sh`，覆盖既有 SQL/native、合成样本重生与哈希、本地维度与指标。004 所需真实 Gaussian/N-grid、签名远程服务端审计、完整内存 gate、2.0 矩阵和独立复现仍待补齐；设置 remote 环境不能替代缺失对象/oracle。`make sanitizer-test` 检查边界与生命周期；性能结论取普通 release 构建。

原完整验收与独立复现在 Linux AArch64 通过，x86_64 支持与验证暂缓。步骤见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md)，结果见 [最终验收](../specs/002-spatial-pushdown/evidence/final.md)。后续维度、网格、远程读取和科学算子见 [Roadmap](roadmap.md)。
