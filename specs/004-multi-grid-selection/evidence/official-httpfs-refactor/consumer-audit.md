# 已移除接口的消费者（R03）

通过仓库文本引用核对，cache SQL 项用于 README 示例、SQL/native 测试、003/004 规划与旧验收工具。没有发现生产外部调用方或已发布版本承诺。当前 003/004 仍在未发布工作树中，因此本工作版本直接移除，旧调用明确报未知设置/函数，不保留空设置。

生产 RangeCache 消费者是 RemoteReadSession/RemoteReadFile 与 DuckomoSessionState；其参数、LRU、access_partition 和凭据 HMAC 均移除。连接状态的 QueryEnd/多 scan 发布职责保留。旧 cache/provider 测试退休前归档；新的 standard FileSystem 和官方动态加载测试覆盖读取、权限、取消、恢复及隔离。

metrics 的真实消费者为 SQL duckomo_last_scan_metrics、v2/v3/v4 sidecar、测试与验证工具。保留 JSON 结构，远程未知与本地不适用明确区分。服务端统计留在验收工具。迁移说明见 docs/official-httpfs.md。
