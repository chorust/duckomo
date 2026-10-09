# 官方 HTTPFS 迁移契约（2026-10-08）

本契约从本次尚未发布的 003/004 工作版本生效，替换历史专用 HTTPFS ABI、逐响应 observer 和 DuckOMO LRU 的要求。历史证据、任务勾选和失败状态保留原含义；旧 G5 为 superseded，历史失败并非通过。003/004 尚未完成的其它验收仍未完成。

## 读取与一致性

运行组合为官方 DuckDB、同版本同平台官方 HTTPFS、DuckOMO。使用标准 ClientContextFileOpener 和 FileSystem，遵守 CanAccessFile。每 worker 独立句柄和 decoder；ReadAtFile 成功必须满足请求长度。比较打开时可取得的 GetFileSize / GetVersionTag，冲突报错；空标签不能证明内容一致。对象须在扫描期间稳定。不承诺新鲜 HEAD、固定探测序列、逐响应强验证、扫描快照或缓存撤权立即生效。

opener 局部禁止 force_download、阈值下载及自动完整下载回退，启用 ETag 检查和可用 S3 version pinning。凭据、签名、重试及上游缓存归官方依赖；失败消息和指标必须脱敏。

## SQL 迁移

移除 duckomo_cache_enabled、duckomo_cache_capacity、duckomo_clear_cache()，旧调用明确报未知设置/函数。仓库实际消费者为示例、测试和验收工具，没有已发布版本的外部兼容承诺。本次工作版本不保留空开关。用户移除这些语句，按 DuckDB 文档管理上游设置；不能把官方缓存视为原 LRU。

read_om/read_om_raw/om_grid_info/include_source/duckomo_max_threads 的语义保持。连接状态继续发布 QueryEnd、多 scan、取消/失败及连接隔离的 metrics。

## 指标与验收

保留 v4 和 legacy JSON 结构。远程 transport body/attempts/responses 为 NULL、complete=false（unobserved）；本地为 0、complete=true（not applicable）。应用读取、解码、选择、任务与自有内存继续计量。自有 cache enabled=false、容量/命中/占用=0、原因 removed；官方缓存和内部 transport 内存未被 DuckOMO 计量，不能推导为总内存为零。

网络收益由受控服务日志审计，记录服务端已发送量，不冒充客户端接收量。局部冷查询小于全扫；完整结果跨本地/HTTP/HTTPS/签名 S3 一致；1/2/4 worker、LIMIT、短读/读取失败、权限、可观察版本冲突、取消/恢复、并发隔离继续验收。reader 零值读取不代表 HTTPFS 没有预取。

旧 G5 替换为 O5（官方冷/重复查询、稳定对象和访问变化），G3/G4/G6 和 H3–H6 根据此契约重跑；H8/H9 用真实官方产物加载与独立复现证明。不存在官方包的组合为 unavailable，不得回退自编 HTTPFS 或声称远程通过。独立验证者复现未取得前不得将 roadmap 提升为 verified。

性能比较预先固定于 performance-policy.json。执行记录、产物和样本 hash 另行保存；旧证据只证明旧版本。
