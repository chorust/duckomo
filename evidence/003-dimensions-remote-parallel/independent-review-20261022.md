# 003 严格归档补充验收（2026-10-22）

三个指定归档缺口已通过新增实际执行补齐：`strict_exact_command_archive_complete=true`，`independent_server_audit=true`。本结论仅指新增命令与切片档案完整，不倒填Luna旧命令。R21/G7/SC-008已有可供父级收口的完整独立AI证据；R22仍待父级完成映射及最终hash复核。本次没有更新任何tracked状态。

本次为Codex GPT-6独立AI（非人工），未参与实现。与root封存时只读审阅不同，本人亲自执行native、派生runner、远程成本补录和固定policy下唯一一轮1/2/4性能测量。继承Luna的48-case synthetic、access-change、四来源worker正确性，以及前任本地维度/缓存API原始证据；没有冒称这些旧命令由本人执行。

## 三个归档缺口

1. native实际补跑退出0，保存exact argv/cwd、二进制/CLI/官方v1.5.4 HTTPFS/扩展/源码digest、非敏感env、run.env和私有setup的hash，以及stdout/stderr/exit。清除大小写HTTP_PROXY/HTTPS_PROXY/ALL_PROXY。成功输出明确覆盖取消、恢复和连接隔离。
2. 原Luna runner逐字复制并记录原hash；首次派生仅修import argparse、固定ROOT，再加入slice wrapper及同列full/selected成本查询。保留diff、外层launch argv/cwd、child CLI argv/退出及公开SQL；私有setup只记hash。第一次执行性能门槛失败，原脚本及所有输出原样保留。后续独立completion派生只完成尚缺成本查询，未重跑择优性能，diff亦归档。
3. 27个远程查询均留实际日志begin/end offset、inode、不可变原始JSONL slice及hash。独立重读原byte interval，核验slice完全相同，重算body/request/response计数并与run_scan原audit逐项相等。HTTP全部started都complete；S3源格式只有finally完成记录，按complete-only schema统计并等待稳定，不伪造started事件。

HTTP与S3 full/selected各自和同列local aggregate参考hash相等；full为158337发送字节/252请求，selected为5058字节/19请求。HTTP与S3各5对同连接冷/重复查询，每条均5058发送字节，结果一致，没有重复查询网络字节节省。另3份slice来自第一轮实际NCEP远端窗口查询。这里计量服务端已发送量，不是客户端接收量；v4 remote transport仍为null/complete=false。

## 全部attempt与性能失败

- Luna历史attempt-01保留在原目录，旧runner路径失败解释见root报告；本次不修改或重新归类。
- 本补充首次launch：native退出0，remaining退出1，外层launch退出1。实际原因是1/4性能比较的parallel median not faster，不是归档脚本错误。`remaining-launch/stderr.txt`保留原traceback，10份aggregate CSV和10份metrics均保留。
- 原runner在性能assert之后才写summary，所以首次10个wall elapsed和精确中位数随进程退出丢失。`first-performance-failure-audit.json`列出全部已知结果hash、scan elapsed与active workers，并明确wall timing=null。scan耗时不能充当丢失的wall耗时，不能补造具体中位数。
- completion退出0只证明剩余日志/成本查询通过，不覆盖首次性能失败。
- 父级进一步要求后，先保存`performance-124/policy.json`，再唯一执行一轮1/2/4各5次。相同manifest fixture、sum(value)、完整aggregate COPY计时边界；排除CLI启动及metrics导出，OS cache不drop。每次先落盘wall elapsed/incremental summary，再采metrics和判断。全部原始timings、SQL、metrics、worker、RSS、hash与exact launch/child argv保留；无循环重试。

新固定policy轮结果：

| repetition | 1 worker ms | 2 workers ms | 4 workers ms |
|---|---:|---:|---:|
| 1 | 11.726840 | 6.017013 | 4.425455 |
| 2 | 8.174571 | 4.399583 | 5.620995 |
| 3 | 9.238975 | 6.363830 | 6.618567 |
| 4 | 8.415437 | 7.091368 | 6.801079 |
| 5 | 6.767831 | 6.309781 | 5.365427 |
| median | 8.415437 | 6.309781 | 5.620995 |

15个结果hash相同，完整消费；2和4 worker均观察到实际多worker参与，且中位数严格低于1，符合预先固定的“至少一种并行配置更快”规则。独立复算见performance-round-review.json。本轮证明固定样本这一轮有收益，第一轮失败同时说明性能存在波动；不声称旧Luna成功即本轮成功，也不把后一轮当作前一轮的替代记录。不对波动原因作未经验证的归因。

## 四部分覆盖与封存

维度：继承并已核验6行映射、精确混合值、双向EXCEPT ALL零行、empty零值读取/解码、三个已移除cache API未知错误。官方远程：继承48-case与access-change；新增native实际运行与有日志切片的真实NCEP窗口。并行：继承四来源1/2/4多重集合正确性，新增所有attempt公开的性能证据。冷/重复：新增HTTP/S3各5对、local参考及full/selected独立服务审计。R21的优化判断是固定窗口/样本有范围读取收益，重复查询没有字节节省；远程物理transport未知值没有补零。

root三份封存及全部335个旧证据文件hash复核未变。manifest包含本补充全部原始输出、脚本、policy、diff、失败、成功、命令、SQL和不可变slice；排除manifest自身及外部mutable服务日志。run.env/setup只留hash，不复制内容。父级可按manifest复核后进行R22映射收口。

适用范围与剩余局限：

- 独立AI执行与复核，非人工验收；模型名称不作服务端attestation。
- 构建为changed-TU rebuild/relink，非clean rebuild；native为父级提供的现有二进制，本次捕获其实际执行hash与当前源码digest，不追溯重建其编译过程。
- 仅v1.5.4 Linux AArch64；不扩大到其它版本/平台、004 H0–H9或完整发布门禁。
- 第一轮1/4并行性能门槛确实失败；10份结果/metrics和失败退出均保留。旧派生脚本在assert后才写summary，内存wall timings与精确中位数已丢失，不以scan elapsed替代。
- 唯一新预注册1/2/4轮各5次满足至少一种并行配置更快；它不覆盖或抹去前次失败，不承诺每轮、每个worker配置或任意查询均加速。OS cache未drop，未独立测冷磁盘条件。
- HTTP审计有started/complete；S3代理只有finally完成记录，未伪造started事件。切片完整性按实际schema验证；S3后端版本/digest仍为父级caller-supplied信息，independent_server_audit指本次独立核对日志字节与计数，不是独立认证后端服务镜像。
- 旧Luna/native命令档案没有被倒填；strict_exact_command_archive_complete指替代性新增执行及其完整当前命令档案。旧root报告与全部旧输出保持原样。
- 真实NCEP仍为25行固定窗口；48-case synthetic、前任维度/缓存拒绝等继承原独立证据，本次未重跑。
- 私有run.env/setup仅记录hash并用于执行/敏感值扫描，未复制SQL或凭据。命令记录只公开明确选定的非敏感env字段，不公开完整继承环境。
- R22交付映射与最终封存由父级复核；本次不改源码、checkbox、roadmap或停止服务。

父级整合复核（2026-10-22）：逐文件复核补充 manifest 的 300 项与原 manifest 的 357 条记录，原封存三文件未变；现行 FR/SC 与门禁映射完成后关闭 R21/R22。完整 build 原始目录保留；本报告与机器记录是可跟踪的摘要副本。
