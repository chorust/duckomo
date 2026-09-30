# US3: 组合过滤、回退与生命周期

日期：2026-09-29。Linux AArch64，DuckDB v1.5.4 release，单线程；每个 harness 查询使用新 DuckDB 进程，OS page cache 未清空。执行完整空间 harness 的命令和参数见 [US2 evidence](us2.md)，退出码 0。

## 混合依赖与回退

`mixed_baseline` 完整扫描 10,541 行 humidity/temperature/coordinates，再在本地按 humidity=96、latitude `[-41,-37]`、longitude `[50,70]` 计算多重集。`mixed` 扫描返回 1 行，与基准精确相同，selection 为 restricted，候选行 55。temperature 读取 3,140 字节/10 块，humidity 读取 1,660 字节/5 块；pressure 的 index/data 请求、字节和解码块全部为零。

跨接缝 OR `longitude >= 124 OR longitude <= -124` 产生 332 行，按 latitude/longitude/temperature 多重集与完整扫描的物化过滤相同；selection 标记 fallback，保留 10,541 个候选。它读取 temperature 165,767 字节并解码 503 块，未将正确性伪装成读取优化。

`mixed_baseline*`、`mixed*`、`fallback*` 的逐查询 JSON 和 v2 sidecar 位于本目录。记录保留真实命令、行数、逐变量 bytes/requests/chunks、elapsed、RSS、架构及 fallback 原因。

## 真实 domain 与独立 oracle

固定真实文件为 5,812,040 字节，SHA-256 `0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd`，Open-Meteo 定义提交 `34b9cea169395be9b4686f2b5b23eca26dfef7a2`。`domain_count` 得到 1,038,240 个逻辑位置且全部 15 个值变量的计数明确为零。随后 `domain_reference_test` 对显式网格和 registry domain 分别完整消费 1,038,240 行；两者均与独立标量坐标公式在 `1e-9` 度内匹配，与 15 个官方 OM C reader Float32 参考逐值精确匹配，NULL 位置一致。`domain_reference.json` 记录命令、样本身份、AArch64、退出码 0 和 elapsed/RSS。

## 证据拒绝和恢复

`spatial_metrics_test` 通过有效 restricted/empty/optimizer-empty sidecar，并拒绝缺 v2 字段、fixture hash 错误、失败状态、decode 不完整、缺 sidecar、expected mode 与 fallback 冲突、无 fallback reason，以及缺 optimizer `EMPTY_RESULT`、成功结果或 bind metadata 的记录。

`spatial_lifecycle_test` 通过 prepared statement 更换区域、同进程别名隔离和 100 轮成功/失败 bind 交替，文件描述符无增长。`projection_evidence_test` 在显式空间配置下覆盖扫描损坏、取消和之后成功重查，失败/取消记录不算成功测量。空间 SQL/native 回归分别通过 14 和 14 项断言；完整日志由 `make test` 生成。
