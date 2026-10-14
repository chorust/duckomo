# P7 前置审计与独立验证交接 — 2026-10-13

## 范围与状态

本记录只审阅已经落盘的执行日志和 gate manifests，并为 P7 独立验证者准备可复核的任务文本。没有启动 P7 查询，没有生成 `quickstart-review.md`，没有修改任务勾选。T049/T050 仍为未完成，P7 状态仍为 `not-run`。

此前停止的 Codex CLI 子任务日志只显示它读取了 quickstart/validation contract；进程已退出。没有发现它运行查询、读取凭据、写出 P7 报告或留下工作树修改。它不构成独立验证证据。

## 已执行回归记录

三版构建和验证由 `build/evidence/regions/005-completion-20261012/run-local.py` 顺序编排。它对每版记录 argv、退出码、运行时间和日志 SHA-256；`local-status.json` 为 `pass`，三版 `build` 与 `local-validation` 均退出 0。

三个版本实际使用同一命令结构：

```text
bash scripts/build-version.sh <version>
bash scripts/validate.sh --matrix test/data/grids/version-matrix.json \
  --pair <version> --output-root <repo>/build/official-matrix --local-only \
  --regions --region-gates P0,P1,P2,P3,P4
```

每版 `local-validation.log` 中可直接确认：

- DuckDB SQLLogicTest：18 个过滤文件全部通过，共942条断言；包括完整 `test/sql/regions.test` 的438行独立 CSV oracle 双向差分（两方向皆0）、60组统计 oracle，以及零值/空间依赖、负例和区域回归。
- Native 和其他脚本输出均包含通过行；进程日志末尾记载五个 region gates 状态为 pass。
- 当时 glob 实际运行的 Python `*_test.py` 有9个 suite、91个 test case。每版日志中 suite 数为 `[13,20,14,1,10,3,11,7,12]`，全部有 `OK`。其中官方 HTTPFS validator 为14项、region evidence auditor 为10项。这是归档日志的真实数量。

注意：当前工作树的官方 HTTPFS/evidence 两个 Python 测试文件分别已增至15项和11项，不能把当前数量回填成历史运行记录。本记录不触发补跑。

每个版本单独构建时，`duckomo_sanitizer_checks` 明确显示16/16 ASan/UBSan 测试通过。P0–P4 manifests 都是 pass，且所有记录命令退出码为0；P1 执行 `region_real_reference.py`，对五个真实目标的坐标与官方值 oracle 做逐位置比较。P0–P4 的具体命令和产物见各版 manifest。

### 本轮回归文件摘要

| 版本 | Build log SHA-256 | Local validation log SHA-256 | P0–P4 manifest SHA-256 |
|---|---|---|---|
| v1.5.4 | `452b0996c3c16c948a69bd6096cd0ab54853518b667aa6483d3168555609447f` | `a27de56c8305d43f02a1700cb7234a501323e126ba665298175fb918629b9013` | `4317d4650c9ab7011aba08b457ebb442b83ec85d4928687e802ee1ad8a669226` |
| v1.5.5 | `186a3c8adff8cdd0ff1e7d1743b9aa92f89f7c239dcff2a8458c0e7200cd0378` | `38d477b065e41f9c79e6bea962c4fba662924d499ab55f157401dcfb13696db0` | `e1335bec0c627ff9413f93a5bd31caa5d4d54c32571b1532c8c46cddb365c0e8` |
| v1.5.6 | `1cce1641159ce960c63b1a14f390dce2c3c24041079d015a1700131ce17cd865` | `14eb5567ed87db2d0b88896c77d0801752c567926393fefba50ef6d7be604c34` | `2dd60d38e89058cb4370577a38cd24aebee6ef3f0d606ed79706150c75785bd7` |

持久状态和运行命令记录：`build/evidence/regions/005-completion-20261012/local-status.json`（SHA-256 `1e19b094103f47746d5f5ecbb717ec0d9c27c5e973d2eefc192c3ef044172345`）。逐版目录为 `build/evidence/regions/005-completion-20261012/<version>/`。

### 固定局部窗口 P5 与 P6

三版 P5 manifests 的命令都是 `python3 scripts/validate-official-httpfs.py ... --region-manifest .../real-targets.json --remote-env <version>/run.env --server-log .../fixture/logs --output .../<version>/P5`，退出码0。每版 P5 都覆盖五个冻结目标与 local/HTTP/HTTPS/S3，40/40 扫描；完整行结果 hash 相同，baseline 与 region 行数等于冻结目标数，终态扫描成功完整。对 HTTP/S3，值数据字节、decoded blocks 和服务端发送字节都严格下降。

`test/data/regions/remote-windows.json` SHA-256 为 `832114aa533191b3db26f2a99d0e147aad24a7339c0afa7457c37a8646c7c4c7`。P5 的范围是固定窗口，不是全域扫描，也不支持全域吞吐或全域节省声明。五目标每版的 HTTPS 成本差（窗口基准减区域扫描）均一致：NCEP `51,720 bytes / 90 blocks / 59,730 service bytes`；RDPS `50,335 / 28 / 53,135`；regional `26,748 / 40 / 30,748`；ALADIN `404,099 / 174 / 421,499`；ECMWF `98 / 14 / 1,246`。各来源结果逐目标一致。

三个 P5 summary SHA-256：v1.5.4 `131fedf65c8a1d15b171e1d85bbe33a0d27b9bd0753d7882194dd3889e5da0c0`；v1.5.5 `d547d5c71b7b688794769f64988ecac6f54b462025904e0b502147610bc624bd`；v1.5.6 `89adb59a8fa6558dfbde5481ed0e3088180033ef9a96ed3d1e693cc2e33821d7`。对应的每版目录中另有摘要和 manifest。

`build/evidence/regions/005-bounded-audit-20261013/P6-summary.json` 状态 pass，覆盖 v1.5.4/v1.5.5/v1.5.6 的 P0–P5、五目标和 local/HTTP/HTTPS/S3；`not_run=[]`，文件 SHA-256 为 `baff855878e9bb77a7bbedda64b0c96a2fb86d8828476c256a8b1f279263dde9`。

## P7 独立验证任务文本

以下内容可交给未参与实现的独立验证者。它是任务草案，不代表本轮已启动。执行前由任务委派方另行明确授权并确认验证者身份。

> **任务：独立复核 DuckOMO spec 005 Quickstart（P7）**
>
> 你是未参与 spec 005 实现的独立验证者。开始前明确记录你的身份；如果你是 AI，写明模型/执行代理，并说明本次使用 fresh context。不得把 AI 执行写成人工验收。
>
> 只按 `specs/005-polygon-region-statistics/quickstart.md` 和仓库已冻结输入执行以下检查：
>
> 1. 使用已构建并核 hash 的 v1.5.4 官方 DuckDB CLI、HTTPFS、spatial 与 DuckOMO。核对 `test/data/regions/regions.om`、`manifest.json`、membership/statistics CSV oracle 的 SHA-256。完整执行 quickstart 的三地区多区域原始点值和按 valid_time/member 的统计查询；原始教学查询应完整返回108行（A/B各54行，Empty为0）。将 A/B 行按 `region_id + om_source.logical_index + member + time_index` 与冻结 membership CSV 中 A/B 子集作多重集合比较，保留重复行；不把三地区教学输出错当成十区域全量438行。核对A/00:00为9条、8个有效值、sum=77、avg=9.625、min=0、max=21；A/02:00为9条、0个有效值、聚合数值为NULL；显式补齐后，Empty 每个时间/成员组合为0样本/0有效值/NULL。对照冻结统计 CSV 中 A/B 的12组结果。零样本 Empty 与存在9条、但值全 NULL 的 A/02:00 必须明显区分。
>
> 2. 完整运行 P7 的一个远程固定窗口对照，目标用 `ncep_gfswave025`。准确使用 `test/data/regions/remote-windows.json` 中 baseline/region 查询，仅 retarget 对象 URI 到受控 HTTPS fixture。window/region 各使用新的官方 DuckDB CLI 进程，客户端文件、HTTP metadata、object caches 全关闭，完整消费输出并取终态 metrics，不使用 LIMIT。baseline 需将普通 read_om 固定窗口物化后用 ST_Covers 过滤；region 查询使用冻结 Polygon 与同一窗口条件。核对完整结果的每个有序字段、NULL、重复行、region_id 与 source_index，结果应各9行且完全一致。
>
> 3. 按 quickstart 检查受控 HTTP fixture 的来源身份与日志：读取非私密 run-manifest 字段确认 control/HTTPS 地址均为 loopback；检查 service-pids.json 中 HTTP server PID 存活且命令行为仓库的 `setup-remote-fixtures.py __http_server`。每次查询前生成唯一 state_key 和 audit ID，通过 loopback `http_control` 发送 state=normal/object=ncep_gfswave025.om，确认204；查询 URL 仅附 `state_key` 和 `audit`，不得包含签名或其他凭据。按唯一 audit ID 将 http.jsonl 的 started/complete request ID配对，只汇总成功 complete 事件的 body_bytes。不要使用整个P5日志累计值代替独立本次计数。
>
> 4. 确认 baseline 与 region 的成功/complete 终态；比较 metrics 中完整 value data bytes 和 decoded_chunks，以及各自独立 audit ID 的 service sent bytes。三项都必须严格下降。窗口外和 polygon边界不通过验收 SQL 改写；固定窗口、列投影和全部轴时间不变。
>
> 5. 在 `specs/005-polygon-region-statistics/evidence/quickstart-review.md` 记录验证者身份和 fresh-context 状态、系统/版本、每个输入与二进制 artifact 的非私密 SHA-256、确切命令和退出码、查询输出 hash/完整行数、oracle diff、终态 metrics、audit ID 请求数/bytes、问题与最终 pass/fail/not-run。保留可以独立审计的结果文件；不得保存、打印或提交私有 `run.env`、S3 setup SQL、access key/session token、签名 URL 或含凭据的 CLI stderr。
>
> 任何输入 hash 不符、受控服务无法确认、结果只做部分消费、metrics/audit 缺项或差异不为零，都要如实报告 failed/not-run，不得修订冻结输入、冒用旧 P5/P6、由委托者补签。

## 依赖和未完成项

- P7/ T049 未执行，仍须真正独立验证者运行上述步骤并亲自提交报告。
- T050 未开始：本记录不是最终汇总，不变更 roadmap、spec 和任务状态。
- 全域远程扫描按用户授权已取消；固定窗口 P5 不能说明全域性能。重复小远程读取的实现限制仍需另行优化。
- Quickstart 的远程 setup 依赖已有授权 fixture。任务执行者不得输出凭据或将签名 URI 放入归档。
