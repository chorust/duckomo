# 005 多边形区域统计 — 最终验收汇总（更新于 2026-10-20）

## 当前最终结论（优先于下文历史记录）

**当前声明范围P0–P7通过。** [本轮报告](current-20261020.md)及[机器摘要](current-20261020/summary.json)绑定源码 `886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e`。Linux AArch64、v1.5.4/v1.5.5/v1.5.6每版P0–P5、72条完整回归命令、17/17 sanitizer通过；P6 errors/not_run为空；[独立AI P7复验](P7-testfixed-20261020.md)通过，非人工验收。

负责人批准[成本修订](../contracts/non-increasing-cost-20261020.md)：固定五对象/区域/窗口/SQL不变，每HTTP/S3目标三项成本均不增加、至少一项严格下降。旧三项严格下降失败单独保留，不改为历史通过。全域远程收益仍未验证；003/004未完成项不被本次代签。[连续矩形读取优化](contiguous-rectangle-performance-20261020.md)记录减少重复解码的收益和物理字节可能增加的取舍。

下文2026-10-14及更早结论均为对应源码/产物的历史快照；其中“当前产物P5/P6/P7未重跑”已由本节新证据替代，原记录不改写。第7节“decoder跨调用不复用”亦为旧实现限制：当前同 decoder 可复用仍覆盖请求的 index/data buffer，连续矩形可减少重复解码；任意非相邻小窗口并不保证获益，详见本轮报告的残余限制。

2026-10-21 预算保护修复及三版本定向回归见 [审计修复记录](../../../evidence/code-audit-remediation-20261021.md)。该记录绑定新源码与新产物，不把 10-20 P0–P7 完整包自动重签为此次修改后的全量验收。

本文件汇总 spec 005 的 P0–P7 实际执行结果、覆盖范围、证据 hash 与未验证边界。所有结论仅基于落盘证据；不引用中断或未执行记录作为通过。

## 0. Review 修复后的当前验证范围（2026-10-14）

三项 review 问题已修复。三版本同源重建后，P0–P4、每版本16/16 sanitizer、19个SQL文件/1,028个断言、9个Python suite/93项测试以及普通native/domain回归全部通过，无跳过。独立work内存口径下，三版本P3/P4的work比例均为1.000004，RSS比例均小于1；31×32,768顶点共享payload回归通过。详细记录与产物hash见 [review-fixes-20261014.md](review-fixes-20261014.md)，机器摘要SHA-256 `e28f1348c2c5eb9d3205b191a02e6a669ed511c63d9e517b717d0bde1da3e1fa`。

以下第2–8节描述修复前的历史验收。当前产物的P5、完整P6、独立P7未重跑；不能将历史远程/独立记录签署为当前产物通过。roadmap保持implemented。

## 1. 范围修订（前置声明）

负责人于 2026-10-13 明确授权"可以修改，不用全域"，取消全域远程扫描。现行 P5/P6/P7 按 [固定局部窗口修订](../contracts/bounded-remote-validation-20261013.md) 执行：冻结较大局部窗口（`test/data/regions/remote-windows.json`，SHA-256 `832114aa533191b3db26f2a99d0e147aad24a7339c0afa7457c37a8646c7c4c7`）内做"普通读取物化 + ST_Covers 筛面"与 `read_om_regions` 的完整逐行对照，并要求 HTTP/S3 三项成本严格下降。**全域远程吞吐与全域收益未验证、未宣称**；已中断的全域扫描仅作历史记录。

## 2. 三版本本地回归与 P0–P4（通过）

执行记录：`build/evidence/regions/005-completion-20261012/`，编排脚本 `run-local.py`，持久状态 `local-status.json`（SHA-256 `1e19b094103f47746d5f5ecbb717ec0d9c27c5e973d2eefc192c3ef044172345`，status=pass）。

每版本（v1.5.4 / v1.5.5 / v1.5.6，Linux AArch64，同源 `extension_source_sha256 e3e3e7de…`）：

- `scripts/build-version.sh <version>` 隔离构建，`duckomo_sanitizer_checks` **16/16 通过**（13 项既有 + 3 项区域 ASan/UBSan）。
- `scripts/validate.sh --matrix … --local-only --regions --region-gates P0,P1,P2,P3,P4` 退出 0：
  - SQLLogicTest 18 个文件、942 断言（含 `regions.test` 438 行独立 CSV oracle 双向差分 0、60 组统计 oracle、负例与零值路径）。
  - 当轮归档日志中实际运行的 Python `*_test.py` 为 9 个 suite、91 个 test case，全部 OK。注意：当前工作树中官方 HTTPFS/evidence 两个测试文件后来分别增至 15/11 项，该增量未在归档日志中重跑，不得回填为历史运行数。
  - P0（spatial adapter、依赖隔离、区域绑定）、P1（五真实目标独立坐标/官方值 oracle 逐位置差分）、P2（60 组统计）、P3/P4（零值/资源/生命周期）全部 pass。
- domain 参考回归：`domain_reference_test` 对照 15 份官方 NCEP CSV（1,038,240 位置）通过。

P0–P4 manifest SHA-256：v1.5.4 `4317d4650c9ab7011aba08b457ebb442b83ec85d4928687e802ee1ad8a669226`；v1.5.5 `e1335bec0c627ff9413f93a5bd31caa5d4d54c32571b1532c8c46cddb365c0e8`；v1.5.6 `2dd60d38e89058cb4370577a38cd24aebee6ef3f0d606ed79706150c75785bd7`。

## 3. 固定窗口 P5（三版本通过）

证据：`build/evidence/regions/005-bounded-20261013/<version>/P5/`。每版 40/40 扫描（5 目标 × local/HTTP/HTTPS/S3 × 基准/区域），全部冷态（新 CLI 进程 + 三类客户端缓存禁用）、完整消费、无 LIMIT。

每目标每版本：基准与区域结果逐行相同（含 NULL/重复/source_index），行数等于冻结目标数；HTTP/S3 相对固定窗口的值字节/解码块/服务发送字节严格下降，差值三版一致：

| 目标 | 值字节差 | 解码块差 | 服务字节差 |
|---|---:|---:|---:|
| ncep_gfswave025 | 51,720 | 90 | 59,730 |
| gem_rdps_10km | 50,335 | 28 | 53,135 |
| gem_regional | 26,748 | 40 | 30,748 |
| aladin_central_europe_2km | 404,099 | 174 | 421,499 |
| ecmwf_ifs | 98 | 14 | 1,246 |

P5 summary 均声明 `comparison_scope=bounded_window`、`full_domain_scanned=false`、`full_domain_savings_claimed=false`。summary SHA-256：v1.5.4 `131fedf65c8a1d15b171e1d85bbe33a0d27b9bd0753d7882194dd3889e5da0c0`；v1.5.5 `d547d5c71b7b688794769f64988ecac6f54b462025904e0b502147610bc624bd`；v1.5.6 `89adb59a8fa6558dfbde5481ed0e3088180033ef9a96ed3d1e693cc2e33821d7`。

## 4. P6 审计（通过）

`build/evidence/regions/005-bounded-audit-20261013/P6-summary.json`（SHA-256 `baff855878e9bb77a7bbedda64b0c96a2fb86d8828476c256a8b1f279263dde9`）：status=pass，覆盖三版本 × P0–P5 × 5 目标 × 4 来源，`not_run=[]`，逐版本重算产物/输入/窗口 hash，拒绝旧全域或中断记录。早期 Markdown 审计 `specs/005-polygon-region-statistics/evidence/p6-summary.md`（SHA-256 `00639187b10b9ac0558a228caeeb7b1630870e79781a9e4ab4acf4f39d618212`）保留的是 2026-10-12 远程尚未执行时的 `not-run` 结论；2026-10-13 的后续通过结论以本段 JSON 为准，不能把早期 Markdown 当作通过报告。

## 5. 独立 P7（AI 验证通过；非人工验收）

报告：`specs/005-polygon-region-statistics/evidence/quickstart-review.md`（SHA-256 `445857b6f354b08fabdfcf8b2cbd3714939b1c9dd51e5723e5e4b0d2da4dde55`），含三次尝试完整留档：

1. 第一次（Codex CLI/GPT-6，fresh）：fixture PID 探测失败，按停止规则 not-run。
2. 第二次（GPT-6，fresh）：本地教学检查全过；远程 HTTPS 因子进程继承代理环境变量在发出请求前失败（P5 的 CliSession 会剔除这些变量），not-run。
3. 第三次（GPT-6，fresh，未参与实现）：剔除 `HTTP_PROXY/HTTPS_PROXY/ALL_PROXY`（全大小写）后完整通过：
   - 本地：108 行 A/B 原始记录与冻结 membership oracle 双向差分 0；12 组统计与 oracle 全对（A/m0/t0=9/8/77/9.625/0/21，A/m0/t2=9/0/NULL）；18 组网格区分 6 个空组与 4 个 9 样本/0 有效的全 NULL 组。
   - 远程：ncep_gfswave025 冻结窗口基准/区域各用独立 CLI 进程完整执行（CA/证书校验、三类缓存禁用、无 LIMIT）；两组 9 行结果逐行相同（SHA-256 `81f034a7c79f82bfc5f6790d3f4dcb00661cc557c6422aa1c4240e8ad9a2436b`，与 P5 一致）；各发布 1 条 success/complete 终态；值字节 77,580→25,860、解码块 135→45、按唯一 audit ID（`p7third-baseline-…`/`p7third-region-…`）配对的服务发送字节 150,469→79,671，均严格下降。

父会话独立复核（不依赖验证者转述）：重新比对 SQL 冻结体（retarget 仅替换对象 URI + 追加 state_key/audit 参数，无 LIMIT）、逐行 CSV 相等、108 行多重集双向差分 0、服务日志按 audit ID 配对 1,107/1,107 与 775/775 条 complete 无失败且字节累计与报告一致、fixture PID/命令行/TLS 证书 hash 复核通过。

**边界：P7 为 fresh-context AI 验证，明确标注非人工验收；仅覆盖教学 fixture 与 ncep_gfswave025 单目标固定窗口 HTTPS 对照。独立报告第三次结果段有一处 CSV SHA-256 转录错误：报告写作 `81f034a7c79f82bfc5f6790d3f4dcbffd9b384a49baebe2390f577b76cd0b00e`，但 baseline.csv 与 region.csv 实际文件 SHA-256 均为 `81f034a7c79f82bfc5f6790d3f4dcb00661cc557c6422aa1c4240e8ad9a2436b`；父会话重新计算并逐行比较，确认两文件相同且与 P5 hash 一致。独立报告原文保留，错误及更正由本最终审计记录，不把报告转录当作原始产物。**

## 6. FR/SC 与 gate 覆盖对照

| 需求 | gate/证据 | 状态 |
|---|---|---|
| FR-001–007/017（绑定/依赖隔离/输入校验） | P0、T006–T010、T035–T041 | 三版本通过 |
| FR-003–011/018（点值/统计/空组/NULL） | P1/P2、SQL 双向差分、P7 本地 | 通过 |
| FR-008/012–013（exact-before-decode、零值） | P3、native/SQL | 三版本通过 |
| FR-002/015–016（预算/并行/生命周期） | P4、16/16 sanitizer | 三版本通过 |
| FR-012/014/018–019 + SC-004（局部远程收益，修订范围） | 固定窗口 P5 三版本 | 通过（仅 bounded_window） |
| FR-017–019（三版本审计） | P6 | 通过 |
| FR-020 + SC-007（独立复现） | P7 | AI 验证通过，非人工 |
| 旧 SC-004 全域收益 | 已由 2026-10-13 修订取消 | 未执行、未宣称 |

## 7. 未执行/失败项与性能限制

- **全域远程基准/收益**：用户授权取消；中断记录不视为 pass。任何全域吞吐结论均未验证。
- **P7 人工验收**：无。验证者为 AI（GPT-6，fresh context），报告已如实标注；不能替代人工独立复核。
- **归档回归与当前测试数量的差异**：归档日志为 91 个 Python test case；当前官方 HTTPFS/evidence 测试各 +1（15/11），增量未在归档中重跑。该增量已在2026-10-14三版本回归中覆盖（每版93项），历史日志不回填。
- **跨扫描切片重复小远程读取（已知实现限制）**：`BuildSelectedBatchSegments` 沿逻辑轴切片、逐段调用 OM decoder；decoder 只能在单次调用内合并区间，跨调用不复用；远程 `DIRECT_IO+NO_CACHING` 使重复 chunk/LUT 读取直达服务端。NCEP 全域 44,969 个 GET 中仅 6,778 种不同范围、重复 38,191 次、24,210 对相邻请求范围连续；gem_rdps_10km 45,980 物理 chunk 对应全域 1,456,260 次解码。固定窗口验收不修复该问题；chunk 对齐批处理/合并需单独性能变更并自带正确性、内存与远程成本验证。详见 [bounded-remote-validation-20261013.md](../contracts/bounded-remote-validation-20261013.md)。
- **首次两次 P7 尝试的失败原因**（fixture PID 探测失败、代理环境继承）已留档，属验证环境问题，不影响实现正确性结论。

## 8. 生命周期与支持声明复核

- `test/data/grids/definitions.json`（SHA-256 `af3c1401c4e23dcfc02369c06e89bcc934868f97440b7115904cf8bf49045714`）与 `src/include/duckomo/generated_grid_registry.hpp`（SHA-256 `77e36d7d2ad029c3cc38a114ed3c7ee318f2ba106bbf1e12563ec3c0bef8c37f`）在 005 期间未被区域能力修改：evidence level 仍为 4 个 `coordinate-value-validated`（gem_rdps_10km/gem_regional/aladin_central_europe_2km/ecmwf_ifs）+ 4 个 `definition-recorded`，与 004 结束时一致。区域读取复用既有 domain 绑定，不产生新的网格支持声明。
- 任务状态：50 项中 50 项完成，T050 以本文件关闭。roadmap 005 条目更新为 `implemented`，严格记录负责人批准的固定局部窗口修订范围：P0–P6 完整 gate 通过，P7 有独立 fresh-context AI 复现记录但非人工验收；全域远程收益未验证，重复小远程读取限制仍开放。只读 roadmap debrief 建议治理方评估是否将该修订范围标为 `verified`，此建议未应用。
