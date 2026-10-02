# duckomo 技术架构

实现包括本地 OM 扫描、语义维度选择、列裁剪、规则网格空间选择、配套 httpfs 远程范围读取、DuckDB 并行任务、连接级范围缓存及 v3 指标。G3–G7 外部验证和独立复现尚未全部完成。用户接口见 [接口说明](spec.md)，构建与验证命令见 [README](../README.md)。

## 模块职责

| 模块 | 职责 | 源码 |
|---|---|---|
| DuckDB table function | 绑定参数、稳定 schema、分析空间/语义条件、生成任务并输出 DataChunk | `src/scan/read_om.cpp` |
| Schema / dimensions | 变量命名、shape、有序轴身份及五类语义坐标校验 | `src/scan/schema.cpp`、`dimensions.cpp`、`semantic_axes.cpp` |
| ProjectionPlan | 保留输出与过滤依赖，去重物理变量并保留输出顺序 | `src/scan/projection.cpp` |
| RegularGrid / SpatialLayout | 生成坐标，核对分离或展平轴及逻辑 stride | `src/grid/` |
| DomainRegistry | 固定 68 个网格定义；绑定时核对轴长与 WKT BBOX | `src/grid/domain_registry.cpp`、`domain_bbox.cpp` |
| Spatial/axis selection cursor | 提取安全必要条件，将候选位置切成有界连续任务 | `src/scan/spatial_filter.cpp`、`axis_filter.cpp`、`axis_selection.cpp` |
| 本地/远程 ReadAt 适配层 | 元数据遍历、受检范围读取、缓存及 transport/read 计量 | `src/om/local_file.cpp`、`remote_file.cpp`、`range_cache.cpp` |
| httpfs range adapter | 配套 ABI、严格响应校验、取消、版本条件和每 attempt observer | `third_party/httpfs-patches/` |
| 官方 OM C reader | 格式解析、物理 chunk 选择、LUT / 字节请求与解码 | `third_party/om-file-format/` |

## 查询流程

```text
DuckDB SQL
  → Bind：读取元数据，校验语义轴、变量、网格与布局，确定 schema
  → RemoteReadSession（远程）：HEAD + bytes=0-0 授权/范围探测并绑定对象身份
  → 优化：收集安全空间/语义必要条件，保留完整 WHERE
  → GlobalInit：初始化 immutable selection 与惰性有界 ScanTask
  → DuckDB workers：领取不重叠任务；每个 local state 持有独立句柄和 decoder
  → SessionRangeCache / ReadAt：精确读取，计量逻辑、底层和每 attempt body
  → 官方 OM C reader：请求 chunk 索引/数据范围并解码
  → DuckDB Vector/DataChunk
  → DuckDB 执行完整 WHERE 和上层 SQL 运算
  → QueryEnd：发布本 SQL 中每个 scan 的不可变 v3 终态快照
```

不带空间或语义条件时沿用完整 row-major 逻辑扫描；未映射轴仍参与行索引，不被折叠。task cursor 不物化全域行或任务表。查询成功与扫描完整是两个状态：下游 LIMIT 可以令 SQL 成功、`scan_complete=false`。

## 条件分析与正确性

启用 projection pushdown；普通 `filter_pushdown` 和 `filter_prune` 关闭。filter callback 校验当前 `LogicalGet` 的表/列绑定和引用深度，仅提取可证明安全的经纬度与语义轴必要条件。它不删除或改写 `WHERE`，不跨生命周期保存表达式指针。

有限常量比较、`BETWEEN` 和安全 `AND` 可缩小候选。混合 `AND` 可保留独立安全子句；`OR`、`NOT` 和函数子树不单独缩窄读取，回退原因写入指标。候选必须覆盖所有匹配行，精确过滤由 DuckDB 执行。

`SpatialLayout` 把逻辑索引映射到分离轴或明确存储顺序的展平轴。semantic axes 把 time/run/level/lead_time/member 坐标绑定至原始轴位置。selection cursor 生成的任务窗口互不重叠且有界；不物化全域坐标表。空选择、仅坐标和无值依赖计数不读取值变量，值过滤依赖仍保留。

## I/O 与指标

网格/轴选择只提供逻辑切片。OM chunk 交集、LUT、字节范围和解码策略由官方 reader 负责。`ReadAtFile` 在本地通过 DuckDB 文件系统读取；远程由配套 httpfs provider 保留 ClientContext 凭据和授权设置，并强制 HEAD、精确 206/Content-Range、身份和重定向边界。httpfs observer 记录每个 request/attempt 的实际 response body，包括重试与失败；逻辑请求、缓存以下成功读取与网络响应字节相互独立。

RangeCache 属于 ClientContextState，强 ETag/S3 VersionId 与盐化访问分区参与 key。只复用精确或包含范围，先淘汰再分配；弱/无版本对象禁止跨查询复用，每次查询仍执行权限探测。

每个 query 的 v3 profile 保留嵌套 `legacy_v2`，逐 scan 记录 bind/scan metadata、coordinate、逐变量 index/data、transport attempts/body、cache、任务/worker、候选与输出行、完整性、耗时和 memory scope。`peak_rss_bytes` 取进程峰值；尚不能完整跟踪 decoder/selection/buffer query-owned 分配时，字段为 NULL 且完整性为 false。QueryEnd 是最终发布点，失败/取消保留已观察成本；同 SQL 的多 scan 与不同连接分别隔离。详见 [003 SQL 契约](../specs/003-dimensions-remote-parallel/contracts/sql-interface.md)。

性能比较要求同文件、同值列、同环境，完整消费结果并检查计数完整性。数据字节表示应用读取量，不代表磁盘物理 I/O。固定多块样本的 temperature 全扫 / 25 点窗口分别读取 **165,767 / 1,795 字节**，解码 **503 / 5 块**；仅坐标、纯空间 count 和空选择的值 index/data/decode 均为零。证据见 [US2](../specs/002-spatial-pushdown/evidence/us2.md) 和 [US3](../specs/002-spatial-pushdown/evidence/us3.md)。其他块布局不保证相同收益。

## 网格来源与验证

Registry 固定 68 个规则网格定义，不自动发现或扩展。上游版本、真实样本 shape、轴和 BBOX 核对见 [规则网格 domain](regular-domains.md)。最初的 `ncep_gfswave025` 有全域坐标与 15 个官方值参考；CHMI 和 GeoSphere 的具体样本另有独立 Swift Float 坐标公式及官方 C reader 值对照。其余登记项以文档记录的样本元数据覆盖为准。

`make test` 构建 release 并调用 `scripts/validate.sh`，覆盖 SQL/native、合成样本重生与哈希、本地维度与指标；设置 remote 环境后另运行 G3。G4–G6 的远程性能/缓存/观测场景及 G7 未参与者复现仍待补齐。设置 `DUCKOMO_DOMAIN_FILE` 后额外执行真实空间 harness。`make sanitizer-test` 检查边界与生命周期；性能结论取普通 release 构建。

原完整验收与独立复现在 Linux AArch64 通过，x86_64 支持与验证暂缓。步骤见 [空间查询指南](../specs/002-spatial-pushdown/quickstart.md)，结果见 [最终验收](../specs/002-spatial-pushdown/evidence/final.md)。后续维度、网格、远程读取和科学算子见 [Roadmap](roadmap.md)。
