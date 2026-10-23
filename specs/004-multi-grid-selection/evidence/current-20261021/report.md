# 004 当前范围最终验收（2026-10-22）

**H0–H9 全部通过，C004-01–04 关闭，当前批准范围为 verified。** 本报告替代[工程阶段快照](engineering-snapshot-20261022.md)及[独立 H9 中间检查点](independent-h9-checkpoint.md)的当前状态；历史 pass/fail/not-run 与旧 96 项任务不追溯改写。[机器报告](report.json)记录精确输入、官方运行时、扩展/build identity、清单哈希、逐对成本及限制；[独立最终报告](independent-h9-final.md)、[独立结论](independent-h9-final.json)与[原始独立清单副本](independent-h9-manifest.json)保留验证者原文。

## 身份与范围

- 源码 SHA-256：`5e73cfe7ca159159c61cc8bab98cc2a84f02a9e18439e051596a9f4b57d51f4c`；6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1：`6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`。本次集成未改源码。冻结清单中的 validation-evidence/quickstart/tasks 三份文档已有独立审计记录的父级状态更新；本次继续同步当前状态，冻结清单保留原哈希，源码、样本、参考和二进制输入不变。三版为 changed-TU rebuild/relink，未宣称 clean full build。
- Linux AArch64，DuckDB v1.5.4/v1.5.5/v1.5.6；各版实际官方 CLI、官方 HTTPFS 与对应 DuckOMO 哈希和匹配加载绑定，六次跨版本加载 fail-closed。证据见 [H0–H8 审计原件副本](h0-h8-audit.json)；其中 H9 pending 是移交独立审阅前的历史状态，最终 H9 以上述独立结论为准。
- `gem_rdps_10km`、`gem_regional`、`aladin_central_europe_2km`、`ecmwf_ifs` 四定义、五冻结 OM 对象。前三者分别覆盖 rotated、stereographic、Lambert；O1280 包含全部 HSURF 及 temperature `chunk_817.om`。只在此对象/版本/平台范围登记 `remote-benefit-validated`，不推定 68 个规则 domain 全量通过，不改 producer canonical IDs。
- 坐标参考为 pinned Open-Meteo Swift producer 的独立移植，容差 1e-4 度；O1280 采用 producer Float32 近似纬度规则。不宣称 ECMWF 原生 GRIB 等价、面积/距离或 datum 转换。

## 门禁与任务映射

| 门禁 | 当前证据与结果 | 任务 |
|---|---|---|
| H0/H1/H2/H3/H7 | 4,076 份工程产物；三版本/五对象 30 组完整值比较，21,973,440,480 个位置；96,254,766 个坐标位置、30 组空间/source 比较、15 组完整空间物化差分；source/info/count/empty 零值读取，合成 N-grid 回归 | C004-01 已关闭 |
| H4 | 工程 48 个 native 和 24 个 SQL 运行、真实对象 1/2/4 worker 与生命周期；独立审计通过 | H8 前置 |
| H5 | 本地固定对与远程 24 对 ≥10× 空间点数变化；612 次 success/failure/cancel/recovery 终态、8,352 次服务请求，owned capacity 有界且释放、描述符稳定 | C004-02 关闭 |
| H6 | 45 对完整真实值/成本；36 SQL 套件/3,261 断言；39 次 S3 配置变化、9 次服务代理权限变化；HTTP/HTTPS 30 组、签名 S3 15 组真实网格生命周期（S3 75 终态/1,455 请求）；003 独立前置已接受 | C004-02 关闭 |
| H8 | 相同源码、冻结输入、三版独立产物及完整 H0–H7，匹配加载与六次跨版本拒绝 | C004-03 关闭 |
| H9 | 未参与产品实现的独立 AI 实际执行四类网格、两种执行器的完整远程对与空间差分；亲自完成最终矩阵、命令溯源及门禁审计 | C004-04 关闭 |

## 真实远程完整矩阵

[Canonical attempt-03](remote-matrix-attempt-03.json)：五对象 × HTTP/验证 TLS 的 HTTPS/签名 S3 × 三版本 = **45/45 对**。同投影、完整消费、冷态配置一致，每对三项成本均严格下降（135/135），没有套用 005 的非递增规则。

| 对象 | 完成对数 | 服务发送 body bytes | 值 data bytes | decoded chunks |
|---|---:|---:|---:|---:|
| `aladin_central_europe_2km` | 9/9 | 49,447,014 → 37,093 | 49,334,169 → 35,348 | 638,728 → 273 |
| `ecmwf_ifs` | 9/9 | 355,323,256 → 5,443 | 352,676,190 → 4,559 | 13,133,299 → 44 |
| `ecmwf_ifs_hsurf` | 9/9 | 4,125,977 → 2,069 | 4,083,294 → 1,452 | 27,649 → 7 |
| `gem_rdps_10km` | 9/9 | 69,099,752 → 5,835 | 68,955,707 → 5,090 | 820,972 → 13 |
| `gem_regional` | 9/9 | 25,821,745 → 3,596 | 25,728,700 → 2,951 | 530,897 → 17 |

九次 O1280 temperature 全扫描各 **3,326,238,720** 个位置，合计 **29,936,148,480**；不是抽样或截断扫描。初始 setup-only 失败报告保留。attempt-02 曾数值通过但独立审计发现九次旧 selected 命令与封存 metrics 路径不一致，不能作为最终严格溯源通过；attempt-03 实际重跑全部 12 对旧继承项，与 33 对逐字节不变的 attempt-02 结果组成最终矩阵。旧失败、旧独立 closure 和原始中断的 O1280 工程记录均保留，未重写为通过。

## 独立 AI 实际执行与继承边界

线程 `01a1614e-925c-76b0-a5f8-32c9bfe8fdb9` 未参与产品实现。最初 runner 配置记录 `opencode-go/gemini-4-argon-contributor`，默认阶段遇到 429；同一独立线程显式以 `codex/gpt-6-astra` 恢复。独立报告将先前 serving model 记为未知；这些都是配置/声明，**无服务端模型身份独立证明，非人工验收**。

独立新执行限 v1.5.4/Linux AArch64：672,002,720 个完整值位置、18,885,562 个坐标/source 位置，重新生成四套官方值参考，四个完整空间平面及双向 EXCEPT ALL/独立三角形比较。HSURF HTTP 全/局部对分别用实际官方 CLI 与独立编译 comparator 执行：6,599,680 → 18 行；body 4,125,977 → 1,591，值 data 4,083,294 → 1,056，decode 27,649 → 4。三版本完整工程矩阵为继承证据，由其独立核对，不冒称独立重跑全部矩阵。

修复溯源后，独立线程亲自执行最终 matrix/pair-policy/final-gates 审计；零未解决发现，`current_scope_can_close=true`。最终清单 **486 输出 + 89 输入**，SHA-256 `52e92838cfdf650481aa99ea684bd2ba86af00aac4960626f2efafac06c96957`。集成方再次实际核对 **6,076 个唯一文件 / 26,913,559,848 bytes**、所有矩阵成本和独立执行日志，未只依据父级文字声明关闭。

## 保留的范围限制

真实 N160/N320/N-region、DuckDB 2.0 与非 AArch64 平台不在批准范围；合成 N-grid 回归保留。H5 只约束已接入的 DuckOMO-owned capacity，不含上游 HTTPFS heap/cache、DuckDB output vectors；没有清 OS cache。服务端 SENT bytes 不等于客户端接收字节，DuckOMO transport 仍为 NULL/complete=false。S3 撤权/恢复由专用受控代理在有效签名保持不变时改变服务权限，并非修改 SeaweedFS IAM；错误签名/secret、endpoint/region 变化另有独立测试。

[当前指南](../../quickstart-current.md)的四类预览与完整 HSURF 空间差分已在三版通过，见[既有示例执行记录](guide-check.json)。本次仅同步状态说明，不改 SQL 示例，不重跑已通过的大矩阵。原始 SQL、环境、TLS 私钥及凭据保留 ignored build，不进入公开材料。没有 commit、push 或发布。
