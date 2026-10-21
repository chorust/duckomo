# P7 独立 AI 验证报告（2026-10-20）

## 结论

P7 本轮验证通过。验证者是 fresh 上下文中的独立 AI 实现代理 Codex（GPT-6），未参与被测功能实现，也未使用 P5 或历史执行结果作为本次通过证据。实际运行的源码身份为 `a35f6cf8ad2808006f48114201740fd226ebb15968567c495b91b9f9eaa5b023`，与指定当前源码 digest 一致。最终 v1.5.4 build ID 为 `7c1c9ba30da2c7e4fd606e67fefa848e2f54f697d26798feb8ab41937f7e5151`。

所有本轮脚本、SQL、输出、逐请求审计、指标摘要和文件哈希 manifest 都保存在 [P7-independent 产物目录](/home/blizhan/repo/github/duckomo/build/evidence/regions/current-20261020/P7-independent)。该目录权限为 0700。没有修改 src、test、scripts、规格、roadmap 或已有证据；没有读取或输出 S3 setup SQL、凭据或签名 URL，也没有操作协调器进程/目录。

## 合成多区域验证

用匹配构建运行 quickstart 的 A、B、Empty 3 区域 SQL。`raw-108.csv` 有 108 条数据行，逐行匹配冻结 membership CSV；期望行由独立 CSV 与 Python 对照构造，没有调用待测区域内核生成 expected。`statistics-actual.csv` 的 12 个统计组逐字段匹配独立 Python 分组计算；`completed-18.csv` 有 18 行，Empty 的 6 组为 0/0/NULL，A/B 在 02:00 的组为 9/0/NULL。由此区分空区域没有原始行与有 9 条但值全为 NULL 的组。

A、member 0、00:00 观测为 9 样本、8 有效、均值 9.625、min 0、max 21、sum 77，与 quickstart 相符。独立校验命令 `python3 independent_check.py` 退出码 0。

## HTTPS 固定窗口对照

从 `remote-windows.json` 原样读取 `ncep_gfswave025` 两条冻结 SQL；仅替换本地 OM 路径为 HTTPS fixture URI，并附加每次请求唯一的 `state_key` 与 `audit` 参数。固定窗口为 lon/lat 均 `[-1.2000000000000002, 1.2000000000000002]`，预期 9 行。基准为物化窗口 `read_om` 后以原始 polygon `ST_Covers` 筛选；区域查询为 `read_om_regions`。两条 SQL 均完整消费，没有 LIMIT。

每次查询前向 manifest 的 loopback control POST 正常状态，响应均为 HTTP 204。基准和区域分别使用新的官方 v1.5.4 CLI 进程；匹配的官方 HTTPFS、DuckOMO 和 spatial 已加载，HTTPS CA 校验开启，server certificate verification 开启，external file、HTTP metadata、object 三类客户端缓存均关闭。两条查询均成功且 `scan_complete=true`，各输出 9 行。输出按完整有序 CSV 逐字段相同，SHA-256 均为 `12ccf18583770352b9efce66eb3a199b1a9914b5c395eec760e0c786fe2d7ea1`。15 个变量的所有行值、source index、NULL 位置均另外匹配冻结且逐项验证哈希的官方 OM C API 参考 CSV；`https-reference-check.py` 退出码 0。

| 指标 | 冻结窗口基准 | 区域读取 | 区域相对变化 |
|---|---:|---:|---:|
| value data bytes | 60,340 | 8,620 | 减少 51,720 |
| decoded blocks | 135 | 45 | 减少 90 |
| HTTPS 服务发送 body bytes | 79,907 | 18,757 | 减少 61,150 |
| 完整输出行 | 9 | 9 | 完全一致 |
| audit 请求数 | 312 | 120 | 均完整配对且成功 |

三项成本均下降，满足 2026-10-20 非递增规则（三项均不增加且至少一项严格下降）。服务 bytes 来自每个唯一 audit ID 的本轮逐行 `started`/`complete` 日志配对计数；失败请求为 0。HTTPS 执行器和官方参考核对均退出 0。`service-pids.json` 中登记的 HTTP PID 仍存活，命令行指向仓库内 `setup-remote-fixtures.py __http_server`；S3 proxy PID 也仍存活。

本次仅证明冻结的 bounded window 内的结果一致性及成本变化，不声称全域扫描吞吐或全域 polygon 节省。S3 服务身份未作独立验证，本轮未执行 S3。

## 环境与身份核验

- 平台：Linux AArch64，DuckDB v1.5.4。
- 官方 CLI SHA-256：`513b573b5ad3a94ad2c0668b1ccefb5f281b6f7aa2a5e2ff057825473848e69a`。
- 官方 HTTPFS SHA-256：`6d30a487968cbe5553b7272fd5bad0e4485c4117db96e21fd1e5e2a225a5a538`。
- 官方 spatial SHA-256：`af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a`。
- DuckOMO extension SHA-256：`f661c2fa5221f925d3cdbf16ea5b9382cef4bcb8cc38005316c3d99f478ee636`。
- Build manifest 的 extension source SHA-256 与重新计算的源码摘要一致：`a35f6cf8ad2808006f48114201740fd226ebb15968567c495b91b9f9eaa5b023`。
- 合成 OM SHA-256：`264fe0905d0681d0fedd08d60a43bf7974cbd161319df77e919f08b4cb582e36`。
- NCEP 对象 SHA-256/大小：`0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd` / 5,812,040 bytes，与 fixture manifest 一致。
- `remote-windows.json` SHA-256：`832114aa533191b3db26f2a99d0e147aad24a7339c0afa7457c37a8646c7c4c7`；五目标 manifest SHA-256：`03c2bcc84b4a0081cbf636d0aace7347b5039eb45ad6bc6a132cf78d9988fd80`。
- Fixture `run-manifest.json` SHA-256：`e79f8269ec6619b1c66d507174834e8cd464ae1f892d0177467f5a99562e04f1`；loopback CA SHA-256：`77a3b8dc3b32e6fbf70d50c7e9f1c0fca8e4463058cb84e57f19ac2e79d0ac0c`。
- S3 service identity 按 fixture manifest 标记为 caller supplied、未独立验证；未使用其凭据。

fixture 与源码原始身份参见 build/regions-remote/current-20261020-tls-fixed/run-manifest.json 和指定 build manifest。最终本轮全部文件 SHA-256 及大小收录于私有目录 `manifest.json`（本 manifest 自身 SHA-256：`411f12b6ec79793369cec659f41b27810d39c320351823c806fa7b1256459197`）。

## 失败与范围

执行时遇到并修正两项验证脚本问题：合成完成查询起初使用 DuckDB 不接受的 `WITH ... COPY` 形式（该次 CLI exit 1），改成 `COPY (WITH ... SELECT ...)` 后最终 SQL exit 0；HTTPS 日志审计器起初只接受 HTTP 200，误拒绝成功的 range 请求 HTTP 206，修正后使用新的唯一 state/audit key 重跑两条完整查询并通过。被拒绝审计器尝试未作为本次最终结果证据。

本轮没有运行 P0–P6、全构建/全门禁、HTTP 非 TLS 对照或 S3 对照。P7 的独立范围按要求只含合成 quickstart 与一次 NCEP HTTPS baseline/region 冷对照；不以历史或 P5 结果替代本次执行。
