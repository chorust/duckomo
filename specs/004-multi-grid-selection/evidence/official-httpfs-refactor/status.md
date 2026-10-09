# 官方 HTTPFS 重构完成记录（2026-10-08）

本轮用户指定范围：**v1.5.4、v1.5.5、v1.5.6 / Linux AArch64**。源码职责收敛和该范围的运行支持已完成。未提交 Git commit，原 003/004 在途工作保留。

## 实现

- 保留 ReadAtFile，移动到独立头文件；标准 FileSystem/ClientContextFileOpener 读取，遵守 CanAccessFile。
- 每 worker 独立句柄/decoder，可观察长度和版本标签冲突失败；标准 DIRECT_IO 控制范围，局部禁用完整下载及回退，保持 ETag/version pinning；敏感路径不进入文件读取日志。
- 删除自有 provider/observer、capability、RangeCache、授权 HMAC、缓存 SQL、HTTPFS 子模块/补丁 staging。
- v4/legacy 结构保留：远程 transport NULL/false、本地 0/true，自有 cache false/0/removed。连接 QueryEnd、多 scan、失败/取消/恢复和隔离保留。
- 三版本固定引擎和实际官方 CLI/HTTPFS 包 URL/hash；build-version/validate/matrix 工具更新，无 HTTPFS vcpkg/staging 要求。发行目录只含 DuckOMO。

## 验证

| 版本 | SQL + native 回归 | 官方远程 runtime | 固定真实样本 | 同 URI 访问变化 |
| --- | --- | --- | --- | --- |
| v1.5.4 | 46 项通过 | 48 项通过 | 本地/HTTP/HTTPS/签名 S3 一致 | 允许/拒绝/恢复通过 |
| v1.5.5 | 21 项通过 | 48 项通过 | 本地/HTTP/HTTPS/签名 S3 一致 | 允许/拒绝/恢复通过 |
| v1.5.6 | 21 项通过 | 48 项通过 | 本地/HTTP/HTTPS/签名 S3 一致 | 允许/拒绝/恢复通过 |

v1.5.4 含完整既有本地 native 和真实 domain reference；v1.5.5/1.5.6 含全部 16 SQL 与 5 个本轮关键 native。official_httpfs_test 实际动态加载官方包，验证连接取消/恢复及同进程并发隔离。48 runtime 用例验证 NULL/false，完整结果与 1/2/4 worker、局部冷读取、LIMIT、重复查询、短读/访问失败/可观察版本变化及每类错误后恢复。失败必须取得 QueryEnd 的 failure 记录，不以没有输出代替失败验收。

五个 Python 契约/fixture 工具测试集通过（grid evidence、reference、registry、fixture proxy、version matrix）。命令/退出码在 commands.json，各版本身份及结果在版本子目录；服务/样本身份在 service.json。更新后的 validate.sh --matrix ... --pair v1.5.4 --local-only 总流程通过；git diff --check 通过。完整服务审计保存在忽略的 build/community-preparation/audit/official-httpfs-refactor/ 下，未裁掉原记录；各摘要的 full_audit_artifact 提供仓库根目录相对路径和 SHA256。源码仓库不保存压缩审计文件；便于评审的 JSON 保存聚合值、事件数和对应哈希。

## 性能与能力边界

固定 dimensions_perf.om、value 列、1 worker、同连接两次完整局部查询，每模式 HTTP/S3 各五轮。performance-policy.json 在目标跑测前固定。冷查询耗时相对旧关闭缓存的中位数比例：HTTP 0.64/0.69/1.06，S3 0.77/0.81/0.82（依次三个版本），通过 <=2 门槛。服务端冷局部发送 5,058 字节，同列全扫约 1,107,017 字节，局部收益成立。

**缓存移除有代价**：旧热缓存服务端发送约 3 字节，官方重复查询约 5,058 字节，耗时也高于旧热路径；不宣称重复查询缓存优化。完整耗时、请求、服务已发送量和进程 RSS 在 performance.json。服务端已发送量不是客户端接收量，HTTPFS 内部分配不受 DuckOMO 自有内存账本约束。对象扫描期间必须稳定，不保证每查询新鲜探测、逐响应快照或任意缓存即时撤权。

## 保留的未完成项

R21 独立验证者复现尚未执行，需要未参与实现者按 docs/official-httpfs.md 复现。完整 004 生产者样本/真实网格 H0–H9 仍有原有缺口，不由本轮版本支持代替；full-h8-audit.json 正确报告 runtime identities pass、完整 H8 not-run。roadmap 未提升为 verified。

旧 G5 失败保持，当前契约为 superseded；历史矩阵/工作树身份在 baseline.json 和 historical-version-matrix.json，原 T049 不改为通过。2.0 开发版退出本轮范围，其无官方包的历史记录保留。

本地未签名发布产物：build/official-httpfs-refactor/dist/，仅三份 DuckOMO、SHA256SUMS 和 manifest.json。未向外部发布，未提交 commit。
