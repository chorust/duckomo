# 独立 P7 跟进报告：test-fixed build（2026-10-20）

## 结论

**P7 通过。** 我以 fresh 上下文中的独立 AI 实现代理 Codex（GPT-6）身份，使用新 v1.5.4 build 独立重放本地合成 P7 与 NCEP HTTPS 固定窗口对照。此次报告中的 SQL、结果、指标和 HTTP audit 均来自本轮执行，不使用旧 P7/P5 的结果代替。

新 build manifest 标识 build ID `5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9`，源码 digest `886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e`。完整新产物位于权限受限的 [P7-independent-testfixed 目录](/home/blizhan/repo/github/duckomo/build/evidence/regions/current-20261020/P7-independent-testfixed)，其 manifest SHA-256 为 `353398cd3c5d99a01d8a986170fdb6b40596abe8807f2035279ee4e38932da8f`。

## Git diff 与输入身份

检查 `version_matrix_test.py` 的完整 diff，仅增加 `scripts` 导入路径、mock `sampling_build.record_build`，并断言 extension-only 与完整构建的调用次数。检查所有被源码 digest 纳入的输入后，将当前 `test/tools/version_matrix_test.py` 临时以 `git show HEAD:` 内容替换并重算摘要，结果恰为前一轮摘要 `a35f6cf8ad2808006f48114201740fd226ebb15968567c495b91b9f9eaa5b023`。因此前一轮后新增的 build input 差异仅为该 mock 测试修正。

当前工作树还含有协调器的并发工作：除上述 Python 测试外，还有此前已有的 C++ 实现与 native 测试改动，以及文档/脚本/证据变更。因此不能把整个工作树描述成“唯一改动仅测试”；本次二进制使用完整当前源码 snapshot，其 digest 与 build manifest 完全一致。新 DuckOMO extension SHA-256 为 `81a41da6aa5cdcef171ad27c470fd13e97c52e5bfc5fd22bc3f31b874f9e12fb`；新 build 中 DuckDB SHA-256 为 `63dcba392de4a949b0d2d02ea77d22a5c787febcc5a8e486e9b963de4e6e5b65`。官方 CLI、HTTPFS、spatial 与前轮核验过的 v1.5.4 官方 runtime 一致，SHA-256 分别为 `513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a`、`6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538`、`af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`。本轮不修改源码、测试、脚本、契约或已有证据。

## 合成多区域 quickstart 重放

用新 build DuckOMO extension 执行 A/B/Empty SQL，独立结果与 frozen membership CSV 逐行一致：108 原始行、12 组统计、18 行补齐。统计期望由独立 Python 分组计算，未调用待测区域读取内核生成期望。A/member 0/00:00 是 9 样本、8 有效、mean 9.625、min 0、max 21、sum 77。A/B 的 02:00 组各有 9 原始行、0 有效、聚合为 NULL；Empty 每组是 0 原始行、0 有效、mean 为 NULL。NULL 与空区域区别通过。

命令 `build/regions-runtime/v1.5.4/official/duckdb -unsigned < synthetic.sql` 退出码 0；`python3 independent_check.py` 退出码 0。实际 SQL、CSV、metrics、原始 stdout/stderr 和 reference check 输出均保存于新产物目录。

## HTTPS 固定窗口 baseline/region 对照

按冻结 `remote-windows.json` 执行 ncep_gfswave025 baseline 与 region SQL。仅将对象路径改为 HTTPS fixture URL，并追加本次唯一 `state_key`/`audit` 查询参数。每次查询前向 run manifest 指定的 loopback control POST normal 状态，均收到 HTTP 204。两条 SQL 分别使用 fresh 官方 CLI 进程；CA 校验与 server certificate verification 开启，external file、HTTP metadata 与 object cache 均关闭。无 LIMIT，完整消费。

两边均 `status=success`、`scan_complete=true`，各返回 9 行。完整有序 CSV 内容和 hash 完全相同：`12ccf18583770352b9efce66eb3a199b1a9914b5c395eec760e0c786fe2d7ea1`。另外将每行 15 个变量、source index 和 NULL 位置逐项与独立官方 OM C API CSV 对照；15 份参考均重新校验 manifest 中的 size/hash。核对命令 `python3 https-reference-check.py` 退出码 0。

| 冻结窗口成本 | Baseline | Region | Region 变化 |
|---|---:|---:|---:|
| Value data bytes | 60,340 | 8,620 | -51,720 |
| Decoded blocks | 135 | 45 | -90 |
| HTTPS 服务发送 body bytes | 79,907 | 18,757 | -61,150 |
| 输出行数 | 9 | 9 | 行内容完全一致 |
| Audit 请求 | 312 | 120 | 全部 started/complete 配对且成功 |

三项成本均不增加且全部严格下降，满足 `non-increasing-cost-20261020.md` 规则。使用本轮唯一 `p7-testfixed-` audit ID 过滤服务日志，没有复用 P5 日志摘要，也没有截断其他请求来冒充本次计数。每个 started request ID 均有 successful complete（HTTP 206 range response），failed/incomplete 为 0；逐请求日志文件、请求 ID、服务字节总数及其 SHA-256 保存在新产物目录。

服务 PID 核验：manifest 登记的 HTTP 与 S3 proxy 进程仍存活，命令行分别指向仓库的 `setup-remote-fixtures.py __http_server` 与 `__s3_proxy`。本轮只执行 HTTPS，S3 service identity 仍是 caller supplied、未独立验证。

## 输入与 fixture hash

- Build manifest：`build/official-matrix/v1.5.4/build-manifest.json`，build ID 如上，源码 digest 与当前重算结果相同。
- 合成 fixture `test/data/regions/regions.om` SHA-256：`264fe0905d0681d0fedd08d60a43bf7974cbd161319df77e919f08b4cb582e36`。
- membership CSV SHA-256：`5d2777324982402abfe6aad9f9dc1b8df4ec93c153dcfe45afe5f761108c59b4`。
- statistics CSV SHA-256：`ec9b46a79a521b5951087a1b4b6df55df8a0ec3f65c744ba2f776bf9d02b3321`。
- `remote-windows.json` SHA-256：`832114aa533191b3db26f2a99d0e147aad24a7339c0afa7457c37a8646c7c4c7`。
- NCEP 对象 SHA-256/大小：`0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd` / 5,812,040 bytes。
- run manifest SHA-256：`e79f8269ec6619b1c66d507174834e8cd464ae1f892d0177467f5a99562e04f1`；TLS CA SHA-256：`77a3b8dc3b32e6fbf70d50c7e9f1c0fca8e4463058cb84e57f19ac2e79d0ac0c`。
- 冻结 real-targets manifest SHA-256：`03c2bcc84b4a0081cbf636d0aace7347b5039eb45ad6bc6a132cf78d9988fd80`。

无 S3 凭据、run.env 文件内容、S3 setup SQL 或签名 URL 写入新产物。未启动其他 agent；未触碰 P5/协调器输出目录。

## 范围限制

这是新 build 上的独立 P7 复验，覆盖 quickstart 合成区域、完整 15 变量 HTTPS bounded-window baseline/region 对照及本轮服务审计。没有执行 P0–P6、S3、完整全域扫描或其他版本。结果证明该冻结窗口上的精确结果一致与本次三项成本下降，不据此声明全域吞吐或全域节省。
