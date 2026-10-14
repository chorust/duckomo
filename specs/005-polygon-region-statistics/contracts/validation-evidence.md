# Validation and evidence contract

本文件定义 gate；执行状态以具备输入/产物身份的运行记录为准。2026-10-13 负责人取消全域远程扫描要求，现行 P5/P6/P7 基准与收益范围由 [固定局部窗口修订](bounded-remote-validation-20261013.md) 约束；下文保留的 full/local 旧用语指原始验收设计，不能用它扩大新窗口结果。源码研究、文档检查、旧 003/004 gate 和一般 spatial 冒烟均不等同本功能验收。

## 输入与矩阵

- 五个真实目标：ncep_gfswave025、gem_rdps_10km、gem_regional、aladin_central_europe_2km、ecmwf_ifs O1280。复用已有可追溯对象；在性能测试前固定适用对象、SHA256、大小、变量、轴/chunk、区域 WKB/hash、完整 SQL、读取列、官方值参考与 producer 坐标来源。不根据跑测收益事后挑区域。
- 版本采用 [research R7](../research.md) 三个 engine/spatial pin，平台 Linux AArch64。实际 CLI、DuckOMO、spatial、HTTPFS 包 URL、`duckdb_extensions()` extension_version/install_path、下载及解压 hash、函数签名 probes、编译选项均保存。matrix 冻结可下载 artifact 的 SHA；运行元数据一致性验证但不进入可重生 build ID。缺包或身份不匹配 fail closed，记录 blocked/not-run。
- 新增 `test/data/regions/manifest.json` 和 `runtime-matrix.json`（实施时生成）：前者固定 synthetic/real 分组，后者以既有矩阵为来源并增加 spatial artifact identity，不修改历史 004 manifest/hash。
- Synthetic 和真实证据分开；不增加 N-grid 样本要求，不由本 feature 提升整份 004 的状态。

## 核心教学 fixture

实施时扩展 fixture tool，生成 `test/data/regions/regions.om`：Float32 根数组 value，shape `[2,3,5,5]`，dimensions 为 member/time/lat/lon；member=[0,1]，time 从 2026-01-01 00:00 UTC 每小时3槽，lat=20+y、lon=100+x。值为 `1000*member+100*time+10*y+x`；time index=2 的全部值及所有时刻的 `(y=2,x=2)` 为 NaN。使用官方 writer/reader 导出精确值参考，不能用扫描器生成 oracle。

冻结10个区域，每个时刻/成员的空间记录数：A=[100,102]×[20,22]（9），B=[101,103]×[21,23]（9），Hole 外框[100,104]×[20,24]洞[101,103]×[21,23]（24，洞边界包含），Empty（0），Outside=[110,111]×[30,31]（0），Multi 两块[100,101]×[20,21]和[103,104]×[23,24]（8），CopyA（9），Narrow=[100,100.2]×[20,24]（5），Gap=[100.1,100.2]×[20.1,20.2]（0），Corner=[102,104]×[22,24]（9）。矩形按闭合环提供，Multi 部件不相交。

A、member0、time0：sample_count=9、valid_count=8、sum=77、mean=9.625、min=0、max=21；time2 同组 sample_count=9、valid_count=0、数值聚合 NULL。总原始区域记录数=(9+9+24+0+0+8+9+5+0+9)*2*3=438。额外样本覆盖 rank/轴序、多值列、重复坐标、长窗和非有限值；不得改动教学 fixture 来掩盖失败。

## Gate 分配

| Gate | 完整通过条件 | 关联 |
|---|---|---|
| P0 依赖/绑定 | 当前目标版本真实加载 spatial（全部三版本覆盖由P6汇总）；实际表达式 adapter 运行正常；无 spatial 的普通读取通过；新入口缺依赖诊断；字面量/getvariable/prepare/rebind 与精确类型检查 | FR-001/002/007/017 |
| P1 区域正确性 | 独立全量基准双向多重集合差异0；洞/边界/nextafter/重叠/同几何不同ID/MultiPolygon/EMPTY/非空间位置均覆盖，真实五类坐标和值对照 | SC-001/003 |
| P2 统计与语义 | 上述10×3×2统计与 oracle；计数精确、min/max精确，有限sum/avg满足 abs(error)≤max(1e-6,1e-6*abs(reference))；空组补齐正确，NaN/Inf行为单测 | SC-002/003 |
| P3 I/O与依赖 | 精确过滤先于值解码；值过滤未投影仍有效；coordinate/source/region-only/count/empty值index/data/decode为0；4096/4097 ranges回退不漏点 | SC-005 |
| P4 执行资源 | 1/2/4 worker结果一致且记录实际active workers；跨批/空中间批不提前EOF；10× owned work与隔离RSS实验、输入预算边界；准备/geometry/decode取消、故障恢复、LIMIT/多scan/多连接终态 | SC-006 |
| P5 真实远程与收益 | 五类同内容local/HTTP/HTTPS/签名S3的固定窗口基准/区域结果逐行一致；每类HTTP和签名S3冷窗口基准→区域的value bytes、decoded blocks、服务发送量严格下降；全部查询成功完整、审计含重试 | SC-004 |
| P6 版本回归 | 三版本相同输入分别执行P0–P5；汇总工具逐一重算产物/输入/证据 hash 并确认各版本请求范围全部通过。每个版本的本地构建至少执行全套现有普通读取 SQLLogicTest/native、当前16项 ASan/UBSan（13项既有检查与3项区域检查）、fixture/reference hash 和 region-specific tests；缺任一目标不以编译通过代替 | FR-017–019 |
| P7 独立复现 | 未参与实现者按quickstart完成多区域/分时统计/空缺测/一次固定窗口远程收益，环境命令hash及结论齐备 | SC-007/FR-020 |

P0 首先做，不先实现整个 scanner 后才检查空间依赖可行性。P1/P2/P3 可先本地 synthetic，但完整 gate 必须具备表中要求的覆盖。P5 不依赖历史 003 G5；采用当前官方 HTTPFS 契约。P7 可以准备指南，但完成判定依赖稳定可执行产物及相应 gate。

## Oracle 与几何独立性

1. 教学整数坐标区域用人工可核验的 expected membership/统计；边界与洞的 exact 期望不得由待测 ST_Covers adapter生成。
2. 一般 synthetic 几何用独立开发期 Python 点面算法生成 frozen oracle，不导入 DuckOMO 选择模块；生产者坐标参考沿用既有独立移植/官方值。
3. SQL 差分先完整物化普通 read_om 的位置和值，再 cross join regions + ST_Covers。它验证集成和候选无假阴性，但与 adapter 共用 spatial，不单独充当独立几何 oracle。
4. oracle 容差只比较坐标参考，不放宽 SQL 区域边界。原值 Float32/NULL 精确；聚合误差仅采用 P2 的冻结公式。

## 工具与证据产物（待实现）

新增 `test/tools/duckomo_region_validation.cpp`，CLI：

```text
--manifest PATH --duckdb PATH --extension PATH --spatial PATH
--output DIR --gates P0,P1,...,P6
[--httpfs PATH --remote-env PATH --server-log DIR]
[--runtime-matrix PATH --evidence-root DIR]
```

P6 必须额外提供 runtime-matrix 与 evidence-root：矩阵列明三个版本产物身份，evidence-root 按 `<version>/P0-P4/`、`<version>/P5/` 提供已完成记录。汇总逐一重算产物/输入/hash，确认同样本与各请求gate全部通过；缺任一记录为not-run，不替代运行。P7为独立人工复现报告，不由本工具自动签署。

必须使用新建的空 output 目录。`--gates` 请求范围缺输入或不完整为退出2，实际失败为1，全请求通过为0；本地子集不能将 P5/P6 写为 pass。远程配置只读已有授权 fixture 配置，日志脱敏，凭据不复制到证据。

每次保存 manifest、exact commands/exit codes、按scan的v4 metrics、full result/oracle hash、几何/对象/产物身份、服务端JSONL/hash、容量/峰值与取消延迟、逐gate pass/fail/not-run。存放 `specs/005-polygon-region-statistics/evidence/` 摘要，bulk artifacts 在忽略的 build/evidence/regions/，摘要保存路径/hash及外部复现所需取得方法。

补 `test/tools/region_evidence_test.py` 审计 requested/completed gate、来源/版本完整性、unknown非零/不计收益、exact终态约束。独立报告为 feature evidence/quickstart-review.md，最终覆盖表为 evidence/final.md（本阶段不创建假结果）。

## 内存与性能判读

按 execution contract 固定输入、chunk、线程、完整消费方式。既有 OM decoder bound 随 chunk 变化分列；选中窄结果的扫描 work peak 不以全量排序/结果物化内存冒充。Spatial 内部内存 unknown 保持 NULL；隔离 RSS 是附加增长证据，不替代 owned ledger 或宣称全部分配已计量。

所有窗口基准/区域比较先冻结对象、窗口和区域；失败、LIMIT、未知成本不能计收益。多区域基准是较大固定窗口读取一次、物化后再产生同区域多重集合；不能重复 N 次扫描制造收益。跨区域重读全部计入局部成本。服务端发送量不等于客户端收取量，HTTPFS transport保持NULL/false。缓存不可确认冷态则不出冷收益结论。
