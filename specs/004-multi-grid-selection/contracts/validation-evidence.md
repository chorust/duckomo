# Validation and Version Evidence Contract

日期：2026-10-03；2026-10-06 按当前 artifacts 复核。尚无完整 H0–H9 gate 可标为通过；已有合成、本地部分、真实对象 metadata 和 O1280 logical-index value 对照记录均按其窄范围引用，不拼接成完整 gate。主证据路径为 `specs/004-multi-grid-selection/evidence/`；003 既有记录仍引用仓库根 `evidence/003-dimensions-remote-parallel/final.md`。

## 2026-10-10 当前 Gaussian 范围

以 [负责人批准的 Gaussian 验收修订](gaussian-acceptance-20261010.md) 为准：N160/N320/N 区域真实样本及其依赖验收本轮跳过，不阻塞当前范围收口；定义及合成回归保留，不记为真实通过。O1280 是本轮 Gaussian 验收目标；其本地真实 domain/空间/抽样时间查询执行记录已补（见 [o1280-real-local-20261010](../evidence/baseline-local/o1280-real-local-20261010/final.md)），完整独立参考、完整时间值覆盖、远程与复现记录仍缺。下文历史表中的 N-grid 必选和“O1280 不替代”条件已由本修订调整，不降低其他门禁，也不追溯修改旧 run 的 pass/fail/not-run。

继续使用既有四种证据等级；按 [2026-10-10 pinned producer 参考决定](pinned-producer-reference-20261010.md)，`ecmwf_ifs` 与三个投影定义登记为 `coordinate-value-validated`：O1280 依据 2026-10-10 本地真实查询记录（官方值全量对照 + producer 坐标独立 Python 实现最大误差 0、显式/domain 等价、空间双向 EXCEPT ALL、22×504 时间值/NULL 官方切片对照），投影依据匹配 baseline 的 H1 全量坐标/值对照。该决定只对 Open-Meteo 自产对象放宽「独立 GRIB 参考」来源，不引入 value-validated，不升级为 remote-benefit-validated，也不扩展为非 Open-Meteo 对象的通用规则。

## 精确版本组合

| 输入 | baseline-1.5.4 | prerelease-2.0-dev |
| --- | --- | --- |
| DuckDB | 08e34c447bae34eaee3723cac61f2878b6bdf787 | 7264a9f0e5b487358100f408826b8ae9e868b031 |
| httpfs | c3f215ab360f04dc3d3d5305fa81849c0121f111 | 5e34903685e4d429cbb19b063406abdd8ce30591 |
| OM | d8855e418e2231ae8439f0c7e840fa3f93b371e3 | 同基线 |
| extension-ci-tools | b777c70d30942cca5bef62d6d4fa23a13362f398 | 9b1020499dc85966be3342541f98c8e8e4aada89 |
| 平台 | Linux AArch64 | Linux AArch64 |
| 本期状态 | 有本地 v1.5.4 development build 和部分 H3/选择记录；独立 matrix build manifest、H0–H9 完整 gate 未完成 | target；源码和身份已固定，尚未完成组合构建 |

2.0 是上述固定 `v2.0-cyanoptera` 源码提交，不能称已存在正式 alpha tag；截至本次核验无 `v2.0*` 正式 tag。移动分支 HEAD 不参与 reproducible build。httpfs 自带 DuckDB gitlink `ca15f79c32c52c4b51df81f11cf915311db950d1` 不替换顶层已选 engine SHA。

2.0 固定引擎的 `.github/config/extensions/httpfs.cmake` 要求对应 httpfs SHA 和 APPLY_PATCHES。官方 overlays 顺序：

| 文件 | SHA-256 |
| --- | --- |
| 0002-collection-get-value.patch | 3833250e557ee83c0a85e8fa93a1d11c04f72ea3a62277c0d2e560661bd9b289 |
| 0003-duplicate-secret-option-error.patch | 7e8b3efe0247b26a7c2e5ab5e978fe53de1a6b22bf8d4be50091f98793d3ce16 |

先应用官方 overlays，再应用该组合 DuckOMO strict-range 补丁；记录 applied hashes，不能和引擎 APPLY_PATCHES 重复应用。旧两份 DuckOMO 补丁 `git apply --check` 均失败，2.0 移植是新工作。1.5.5 既有真实样本记录保留为有限范围，不扩大为本期完整支持。2.0 正式版可用后新增精确 tag/full SHA 组合，独立重跑同一 H0–H9；预发布通过不删除正式验收目标。

matrix manifest 至少含 schema_version/pair_id/release_state、上述精确输入、official_overlays、duckomo_patchset、shared_header ABI3/hash、required_features、平台/compiler/options、生成身份常量、实际 CLI version/source_id、扩展二进制 hash、gate 命令/退出码/证据。ABI3 的 `header_sha256` 指向受版本矩阵固定的共享头文件模板；stage 会把 pair/build identity 替换进私有副本，并以其完整 stage tree hash 固定最终产物。能力描述同时包含模板 hash、patchset hash、目标/编译平台和实际 C++ 标准库 ABI。`build-version.sh` / `stage-httpfs.sh` 已暴露 `--matrix/--pair/--output-root` 并委托版本工具；baseline 与 prerelease ABI3 patchset 均已建立，prerelease 固定官方 overlays 后的自有补丁适用性检查已通过，但两个组合的完整隔离构建、CLI/产物证据和 `build-manifest.json` 尚未验证，不得由 patch 文件存在推定远程支持。

## 固定定义与样本

registry 来源为 Open-Meteo `b06f4760fd1f997e5559bb380f64c5e496b4a509`；旧规则真实参考来源 `34b9cea169395be9b4686f2b5b23eca26dfef7a2` 保留独立身份。生成输入须含全部参数、源 arithmetic/origin 规则、行表/区域映射及来源源码 hash，重生两次逐字节及定义身份一致。

每个 fixture manifest 冻结：object path/URI（脱敏）、bytes、SHA-256/版本、真实/合成标签、OM 格式、variable/axis/chunk shape、grid/layout 身份、官方值参考、独立坐标/局部映射参考、预定 tolerances、运行 query/依赖、cache/服务版本与冻结时间。freeze 先于验收运行，不能测后挑对象或放宽容差。

候选覆盖表，不是下载保证或通过证据：

| 类别 | 固定源/样本候选 | 尚需证据 |
| --- | --- | --- |
| rotated | `s3://openmeteo/data/cmc_gem_rdps_10km/cape/chunk_4337.om`；真实 OM v3，shape `[1045,1140,114]`，SHA-256 在 sample manifest | 全量 official 值、独立坐标和真实 source-axis mapping |
| Lambert | `s3://openmeteo/data/chmi_aladin_central_europe_2km/cape/chunk_4135.om`；真实 OM v3，shape `[837,1053,120]`；现已与 pinned CHMI `aladin_central_europe_2km` producer 定义对应，不能映射为 `lambert_formula_vector` | 独立完整坐标/值、实际轴映射和读取收益；另保留单/双纬线和南半球 synthetic 边界 |
| stereographic | `s3://openmeteo/data/cmc_gem_rdps/cape/chunk_4337.om`；真实 OM v3，shape `[824,935,114]`，SHA-256 在 sample manifest | 全量 official 值、独立坐标和真实 source-axis mapping |
| N160 | pinned `ecmwf_seas5_12hourly` / `ecmwf_seas5_monthly_upper_level` source definition | 原生 OM v3 和逐行点序；本次 public bucket 未获匹配对象 |
| N320 全域 | 固定 N320 行表 | 固定来源没有全域 producer domain；无真实 N320 OM v3 对象 |
| N320 区域 | `ecmwf_aifs_europe_ensemble[_mean]` 源派生 14,747 点 | 原生 OM v3、GRIB/生产归档局部行段与 OM 值点序独立对应 |
| HRES O1280（本轮 Gaussian 目标） | 公共 `s3://openmeteo/data/ecmwf_ifs/static/HSURF.om` 与 `temperature_2m/chunk_817.om`；real OM v3，6,599,680 点 | 行长/坐标/point order 按 pinned producer 参考决定已验证（见 o1280-real-local-20261010）；remote benefit 未验证；不替代 N160/N320/N-region |

四新类型各须真实 OM v3 坐标/值对照；N160/N320/一个 N 子集各有独立定义/位置对照（本轮跳过）。三个投影对象与 HRES O1280 的坐标/值对照已按 pinned producer 参考决定通过并登记 coordinate-value-validated；完整 gate 与远程验收仍开放。固定类型可跳块性能样本在 full/local 前冻结；synthetic 可补布局、奇点、故障和大规模缓冲门禁，不代替真实对照。精确 object metadata、hash、命令和有限 H3 状态见 [sample acquisition](../evidence/baseline-local/open-meteo-s3-sample-acquisition.md)、[H3 local record](../evidence/baseline-local/us4.md) 与 [US2 progress](../evidence/baseline-local/us2-progress.md)。

坐标参考最低策略：source-compatible f32 的逐点参考绝对误差≤1e-4 degree，float64 analytic 与独立 double 参考≤1e-8 degree；同一显式/domain 实现坐标要求精确一致。manifest 可在验收前设更严格的来源级阈值，不得为混淆不同 Gaussian 规则放宽；若默认阈值不成立，先定位算法/约定，必要的规格/阈值修订单独记录后重新冻结全套。标准与生产者 N 纬度差约 0.0087/0.0044 degree，不能视为浮点容差。值逐官方 Float32 位值/NaN→NULL、行数/源位置/缺测均精确核对。

Gaussian全行、行首尾/接缝、N320区域局部原始顺序必须核对。固定来源Gaussian WKT声明WGS84椭球6378137/298.257223563，保留为地理来源描述，不能强制sphere后忽略冲突；该声明不允许按标准纬度替换producer近似。源reduced_gg loader仅核总点数，不能作为真实局部点序证明；独立参考需原始GRIB/归档扫描flags、pl、每行首尾坐标及OM值对应。空间关系参考使用相同明确坐标来源的planar point/polygon，lon在前/lat在后，不能称物理面积/距离或未经证实datum转换。

## H0–H9 门禁

| 门禁 | 核验及成功条件 | 覆盖 |
| --- | --- | --- |
| H0 输入/重生 | 完整来源/hash、定义/局部映射、预定容差、两次 registry 重生一致；真实证据等级明确 | FR-002/004/027/028，SC-008 |
| H1 坐标/值/布局 | 当前范围四类真实（Gaussian 目标为 O1280，N160/N320/子集真实验收本轮跳过）；全源点、独立值/坐标、反向/展平/交错轴、多变量和所有语义轴 | FR-001–007/027，SC-001 |
| H2 选择正确性 | 与完整物化后相同 WHERE 双向 EXCEPT ALL=0，含曲线、域外四角、开闭/nextafter、极区/接缝/OR、片段预算回退和值过滤 | FR-008–012/015/017，SC-002 |
| H3 零值读取 | 全域/局部坐标和计数、可证空、纯 source/info：value index/data/decode=0；含值过滤对照必保依赖 | FR-012/013，SC-004 |
| H4 并行/生命周期 | 固定多任务 1/2/4 worker multiset 一致，实际 active worker 记录；LIMIT/不足任务/跨batch/取消/失败/恢复 | FR-012/014，SC-005/010 |
| H5 内存/取消 | ≥10×空间点数、同 chunk/线程/缓存、相近窄区域，work peak≤2×及各预定上界；超长线、transport attempts、瞬时 buffers 计量 | FR-014/029，SC-007 |
| H6 跨源/真实收益/协议 | 每类同内容 local/HTTP(S)/S3 一致；两个远端 cold total body、value data、decoded blocks 均严格下降；服务端所有attempt对账 | FR-015–019/029，SC-003/010 |
| H7 空间输入 | grid_info/source 可独立复原；四类 point/polygon 与全域精确基准一致，CRS/轴顺序/unsupported 能力明确 | FR-020–023，SC-006 |
| H8 版本矩阵 | 1.5.4 和固定2.0各配套构建，运行同一正确性/远程/取消/缓存/统计集；不匹配 fail closed；正式版另复验 | FR-024–026，SC-009 |
| H9 独立复现/发布 | 未参与实现者执行四类、一次远端全/局部成本及一次空间关系；逐domain等级和文档同步，有完整记录 | FR-030，SC-011 |

H6/H8 依赖可信 003 remote 基线：现 G0–G2 本地通过、G4 本地部分，G3/G5/G6/远程G4/G7 尚未完整通过，须补证，不能从 task 勾选或公开 HTTPS 冒烟推定完成。

协议/cache 故障集至少包括 403/404、无 HEAD、200忽略 Range、错误 Content-Range、短/多 body、非identity编码、token 丢失、412、强/弱版本、同 URI 等长替换、timeout/cancel、cache cold/hot/disabled/clear/eviction、S3真实签名撤权/恢复/secret及endpoint/region变化、并发 query/scan profile 隔离、下游错误后恢复。失败不能缓存半段或计为成功完整扫描；受控 HTTPS 必须有实际 TLS 执行记录。

## 完成与支持等级

每条 domain 分别标 definition-recorded / metadata-checked / coordinate-value-validated / remote-benefit-validated，附样本和组合范围；同类不继承验收。所有相关 H gate 通过和 H9 独立复现后才标对应范围 verified；真实样本缺任一类，完整 Phase 6 不能 verified。支持列表、README 双语、docs/spec、architecture、roadmap/台账及 SQL 契约实施后同步，不能在规划阶段提前宣布实现。

每次执行保存 manifest.json、exact commands/exit codes、逐 scan metrics-v4.json、完整 result/oracle hash、服务端 JSONL、memory bounds/peaks、各 H pass/fail/not-run 和 final.md。结果完整消费，列依赖一致，记录 cache 状态与 object version；失败/取消/提前停止或任一成本不完整不得计收益。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
