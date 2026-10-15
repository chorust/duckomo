# Nino 读取：decoder 范围复用

问题：SEAS5 O320 月度 SST 为 `[1,421120,7]`，压缩块 `[1,146,7]`。扫描按每个点的 time 轴切段，相邻点重复请求相同索引和压缩数据。两个 Nino 区域 7,556 个点、七个月共 52,892 条匹配记录，解码调用 7,652 次。此前相同文件/SQL 的受控 HTTP 回放产生 15,325 请求；完整真实 S3 查询在 300 秒内未完成。

修复：保留每个 worker/变量 decoder 的现有 index/data buffer 对应的有效 offset/size。仅当请求完全被最近成功读入的范围包含时直接使用对应字节偏移。替换前清空有效范围，I/O 成功后发布；失败重试不会用到半成品。没有全局缓存、跨变量共享或额外 payload buffer。复用生命周期与 decoder 相同，查询期间对象须稳定。仍通过官方 OM decoder 解码。

指标：复用命中仍累计 logical index/data 请求；successful/physical reads 只计真实位置读取。重复解压仍计入 decoded_chunks，不将 I/O 减少冒充解码消除。四个 uint64 状态字段纳入 decoder sizeof 与现有容量计数。

## 实测

对象：`s3://openmeteo/data_run/ecmwf_seas5_monthly/2026/10/01/0000Z/sea_surface_temperature_mean.om`，1,104,504 bytes。相同 Nino3.4/Nino1+2 polygons、AVG/count 按 region/time 分组，threads=2，完整消费 14 组结果。未启用全树重验证。

| 指标 | 原实现：受控 HTTP 完整回放 | 优化：真实 S3 完整查询 |
|---|---:|---:|
| HTTP 请求 | 15,325 | 157（HEAD 1 + Range GET 156） |
| 值索引读取 | 7,652 / 643,333 B | 13 / 1,245 B |
| 值数据读取 | 7,652 / 1,869,392 B | 123 / 31,179 B |
| 解码块计数 | 7,652 | 7,652 |
| 时间 | 原 S3 超过 300 秒，未取得完成时间 | 24.38 秒；另一次 26.04 秒 |

优化后的真实 S3 请求来自官方 HTTPFS HTTP logger，200/206 全部成功；没有把受控回放当作原 S3 网络请求。优化后受控 HTTP 为 156–157 请求，worker 调度会改变少量重复范围。所有本地/S3 结果相同，query-owned memory complete 且 released_at_terminal=true。OS/page cache 未清空，不宣称普遍冷吞吐。

验证：reader_capacity_test 覆盖相邻点/重复选择、包含子范围的字节偏移、独立 decoder 隔离、index/data 读失败后的失效与重试。季节 domain 回归检查请求下降、logical/physical 区分及内存释放；区域回归覆盖取消、对象变化、损坏恢复与多连接。

精简指标与源码/二进制 hash 见 [summary.json](decoder-range-reuse/summary.json)；原始 SQL、完整 metrics、受控服务器逐 Range 记录和真实 S3 HTTP 日志位于忽略目录 `build/seas5-domain-audit/nino-requests/`。新增 domain 与真实样本范围另见 [季节预测指南](../../../docs/seasonal-domains.md)。
