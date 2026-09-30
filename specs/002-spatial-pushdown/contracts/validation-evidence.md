# Spatial Validation Evidence Contract

状态：已实施。继承 [已有观测契约](../../001-local-om-scanner/contracts/validation-evidence.md)，不新增产品 profiling SQL API。AArch64 实测和逐查询文件见 [US2 evidence](../evidence/us2.md) 与 [US3 evidence](../evidence/us3.md)。

## 工具与资产

扩展 `duckomo_fixture_tool` 与既有 validation harness；新增独立目标 `duckomo_spatial_validation`，参数为 `--root --fixtures --output --duckdb --extension`，并要求 `--domain-file` 指向真实本地文件。复用既有 CLI 子进程/metrics 设施。`make test` 与 `scripts/validate.sh build/release` 纳入空间 SQL/native、可重生合成 fixture 和哈希；完整发布门禁另运行带真实文件的 spatial harness，缺文件或哈希不符非零退出。未提供 domain 文件时，脚本只报告合成门禁完成，不将真实 domain gate 记为通过。

合成基准复用 `projection.om`（三个变量，shape [83,127]，多块、多批次），显式纬度 `-41+y`、经度 `-126+2*x`；固定小区域 latitude [-2,2]、longitude [-4,4]，在比较前锁定，不运行后挑选最好结果。其独立参考由官方 reader 值和不调用 GridMapping 的轴表组成。新增反向/单轴点、seam、分离轴换序、两种 flatten order、非空间轴在前/中/后、缺测与冲突 fixture。manifest 保存 shape/chunk、轴身份、grid、官方源码版本、参考哈希和预设容差。

真实文件 manifest 独立于可重生合成 fixture：保存 research 中的来源、哈希、全部变量信息、参考生成步骤和 grid 来源。保持大文件在 build 下，不将下载或远程扫描变为扩展运行时依赖。若上游对象更新，哈希失败必须重新选择并审阅证据，不静默接受新内容。

## schema_version=2 的新增字段

继承 sql、fixture_sha256、dependency_commits、status、result_rows、comparison_passed、逐变量 index/data bytes/requests 和 decoded_chunks、metadata 开销、elapsed_ms、peak_rss_bytes、environment/cache_policy。

| 新增字段 | 语义 |
| --- | --- |
| bind_metadata_bytes/requests, scan_metadata_bytes/requests | 分阶段实际 metadata 读取；相加为 metadata 总量，修正旧 sidecar 缺少 bind 计量的口径 |
| grid_definition, spatial_layout | 有效数值配置、轴名/位置、order、shape |
| grid_source | explicit 或 domain；后者包含上游 commit、文件哈希与参考来源 |
| selection_mode | full/restricted/empty/fallback；优化器直接消除扫描时 optimizer_empty |
| residual_filter_retained, fallback_reasons | 精确过滤保留情况，以及未能下推的表达式类别 |
| candidate_rows | 实际交给 DuckDB 残余过滤前的行数；未扫描时为 0 |
| reference_identity, coordinate_tolerance | 参考哈希及预先固定的 1e-9 度绝对容差；不是 WHERE 比较容差 |
| logical_positions_match, null_positions_match | 逐位置及缺测核对是否通过 |

optimizer_empty 在 sidecar 中使用 `selection_mode="empty"` 和 `optimizer_empty=true` 区分真实空扫描；必须另存 `EXPLAIN EMPTY_RESULT`、成功查询结果和非零 bind metadata 证据，不能以缺少 sidecar 推断为零。证据完整性 helper 校验 fixture hash、依赖提交、v2 字段、逐变量计数和 bytes/requests 加总，并拒绝失败状态、incomplete decode、缺少计划/结果的 optimizer-empty 或被错标为 restricted 的 fallback。指标仍为应用读取量，不声称底层设备 I/O。重复块解码累计，失败调用标记 decode_count_complete=false。

## 必需查询与断言

1. 同一文件、相同输出值列、相同环境的 full 与 restricted：结果与全域物化表过滤及独立参考一致；restricted 的 data_bytes 和 decoded_chunks 均严格少于 full。
2. empty、仅坐标、带纯空间条件的 count：所有值变量 index/data bytes 和 decoded_chunks 均为零；允许 metadata。
3. temperature 输出、humidity 过滤且带区域：结果一致，temperature/humidity 正确读取，pressure 无读取或解码。包含列重排、重复引用、表达式、排序、聚合、NULL。
4. 跨接缝 OR、含不支持分支 OR、坐标函数、NULL/Inf、混合 AND：结果一致；无安全必要条件时标记 fallback，不能把低输出行数当读取减少。
5. 边界涵盖 `< <= = >= > BETWEEN`、反向常量比较、点上/点间/nextafter、反向轴、单点轴、极点、接缝重复逻辑点；所有布局跨批次及额外轴逐位置核对。
6. 全部负向配置在行输出前拒绝；扫描损坏/取消失败后有效查询成功，资源检查无持续增长；失败记录不参与性能比值。
7. domain 与等价显式配置完整对照，官方值 oracle 与独立坐标参考全域核对；至少一个真实 domain 通过才允许完成 FR-014。

耗时/RSS/请求数不要求严格下降；记录编译和 CPU 架构、单线程及 OS 缓存条件。完整消费查询结果，不能用 COUNT 包装值扫描制造虚假的零读取。验收不以 EXPLAIN 代替 I/O 指标。
