# 003 当前交付映射（2026-10-21）

状态：**现行官方 HTTPFS 声明范围 verified，R21/R22 已于 2026-10-22 收口**。依据实际执行、独立复核及身份绑定，范围限 Linux AArch64 / DuckDB v1.5.4–v1.5.6；不代表旧缓存 gate 或完整发布门禁通过。

[当前机器映射](current-engineering-20261021.json)绑定源码 `5e73cfe7ca159159c61cc8bab98cc2a84f02a9e18439e051596a9f4b57d51f4c`、三版本扩展及命令/结果。每版 48 项 runtime、四来源真实全对象（每来源 1,038,240 行）、权限转换、native 取消/恢复/隔离、维度筛选及五轮并行已核对；12 份真实 CSV 哈希相同。构建为变更 TU 重编译并复用固定基础对象的 relink，非 clean rebuild。

独立验收见[报告](independent-review-20261022.md)及[封存/父级复核](independent-review-20261022.json)：Luna 执行与未参与实现的 fresh GPT-6 最终审阅/补跑分别标识，均为 AI、非人工。首次 Luna 因 429 中断、历史路径错误、初次补跑 4-worker 性能失败均保留。最初只读接受缺 exact argv/启动 wrapper/服务切片，未据此关闭；新增实际执行补全当前命令档案及 27 份不可变切片，父级核对补充清单 300 文件、原清单 357 条记录与原封存未变。未倒填旧命令。

新增 native 退出 0。HTTP/S3 全扫与局部均匹配 local 参考，服务端已发送 body 为 158,337 → 5,058 字节；各五对冷/重复每条仍 5,058 字节，不宣称热缓存节省。独立真实 NCEP 是 25 行固定窗口，三版本工程全对象对照另列。初次性能 wall timings 因旧脚本 assert 前未落盘而丢失，失败退出与 10 份结果/metrics 保留。其后先固定 policy、仅执行一次 1/2/4 各五次，wall 中位数 8.415/6.310/5.621 ms，结果相同且实际多 worker；满足原 SC-005 至少一种配置更快，不承诺每轮或任意查询加速。

当前契约为 [官方 HTTPFS 修订](../../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)。自有 LRU、专用 ABI、observer、缓存 SQL 和旧 G5 已 superseded；保留历史失败，不重新实施这些要求。现行 SQL 为 `lat/lon`、连接级 `duckomo_max_threads` 和 metrics v4。

## 当前工程证据对旧门禁的覆盖

[历史 final](final.md)及[R20 原始包](../../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md)保留原产物含义。R20 build.json 同时出现 build_id/release_dir 不同身份，不能直接继承为本轮产物通过；以下新执行直接绑定当前扩展 SHA256，优先用于本轮适用性判断。原始包位于 `build/acceptance-closure-20261021/003/`，逐项摘要路径和 SHA256 见机器映射。

| 门禁 | 当前执行与适用边界 |
| --- | --- |
| G0/G1/G2 | 三版本 dimensions harness 完整五轴/轴序/混合选择与零值 I/O 检查成功；局部读取 data bytes 157,635 → 4,680、decoded chunks 3,704 → 8。并结合本轮 semantic_axes/axis_filter SQL；不扩展为 004 任意网格验收。 |
| G3/G6 | 每版 runtime-retry 48 项通过，覆盖 local/HTTP/HTTPS/签名 S3 一致性、故障恢复、1/2/4 worker、冷/重复及并发；每版 official_httpfs native 的取消/恢复与连接隔离通过。access/summary.json 为同 URI、同连接 allowed/denied/restored；真实 NCEP 四来源完整结果一致。初始失败和重试记录均保留。 |
| G4 / SC-005 | 固定 dimensions_perf.om、完整 sum(value)、每 worker 五轮。v1.5.4 的 1/2/4 中位数为 9.430/7.682/11.544 ms；v1.5.5 为 11.864/11.094/8.300 ms；v1.5.6 为 11.025/7.606/8.962 ms。每版至少一种并行配置快于串行；不声称每种配置都加速，也不新增远程五轮门槛。 |
| O5 / SC-006 | 当前 o5-performance/summary.json 按预先固定策略通过，对比 old-disabled/old-hot/official 的冷/重复成本；记录真实回退，不承诺官方重复读取具备旧热缓存收益。HTTP/S3 服务端 sent bytes 不能解释为客户端接收量；remote v4 transport 仍为 NULL/false。 |
| G7 / SC-008 / R21 | 已通过独立 AI 复现及补充严格归档；四部分、exact argv/身份/退出码与服务切片见独立报告。fresh 独立执行限 v1.5.4，三版本工程运行不冒充逐版独立复验。 |
| R22 | 已核对当前产物、完整命令/封存与本表 FR/SC；按现行契约收口，旧 G5 superseded，004 未完成项独立开放。 |

## 现行需求对应

| 需求 / 成功条件 | 当前证据入口 |
| --- | --- |
| FR-001–005 / SC-001：五轴、单位、轴序与拒绝规则 | 各版本 dimensions/summary.json，加修复记录中的 semantic_axes SQL；当前 SQL 契约与 quickstart 的映射定义。 |
| FR-006–009 / SC-002、SC-004：筛选、回退、零值 I/O、成本 | dimensions 的完整基准差分、上述 data/decode 对照；axis_filter、cache_metrics SQL；runtime 中空选择与投影记录。 |
| FR-010–012 / SC-003：远程来源、真实对象与错误 | runtime-retry 的同内容与冷全扫/局部对照；real-summary 四来源完整结果；故障/恢复原始记录与独立服务端审计。 |
| FR-013–014 / SC-005、SC-007：并行、取消与隔离 | runtime-retry 的 local/HTTP/HTTPS/S3 × 1/2/4，parallel-performance 五轮原始观测；三版 official_httpfs native。 |
| FR-015–018 / SC-006、SC-007：官方缓存边界、指标与访问变化 | 当前 v4/legacy cache 指标、access 同 URI 转换、runtime 冷/重复与故障恢复、O5 固定性能对比；历史 G5 为 superseded，非通过。 |
| FR-019：输入、参考与可复现性 | current-engineering-20261021.json 的各版本摘要/日志路径和 SHA256，摘要内实际命令/SQL/输入与服务身份；既有固定独立坐标/值参考不替换为 scanner 自产参考。 |
| FR-020 / SC-008：交付指南及独立复现 | 现行 quickstart、SQL 接口、docs/official-httpfs.md 与最终汇总入口；独立执行及封存已完成，SC-008/R21/R22 在明确 AI/版本范围内通过。 |

本次修改与验证清单见 [审计修复记录](../code-audit-remediation-20261021.md)。原 2026-10-02 final 表格保留历史含义。005/006 的通过不代签 003。
