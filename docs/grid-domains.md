# 投影与 Gaussian 网格证据表

本文记录 004 网格定义各自的来源和证据等级。定义注册、metadata 检查、完整坐标/值验证和读取收益是不同层级；一个 family 或相似网格的结果不能替另一个定义升级状态。

## 证据等级

| 等级 | 已证明的范围 |
|---|---|
| `definition-recorded` | 固定来源、参数和数值规则已登记；尚无相同定义的真实样本验证。 |
| `metadata-checked` | 固定真实 OM v3 对象已核验格式、root array metadata 和 shape/chunks；不代表坐标、轴点序或全量值正确。 |
| `coordinate-value-validated` | 相同定义的全部位置与独立坐标/局部映射及官方 OM C 全量值参考一致。 |
| `remote-benefit-validated` | 固定对象在冷态 local/HTTP/HTTPS/S3 查询中结果一致，值字节、decoded blocks、总 body 有严格下降并有服务端审计。 |

状态按每个定义单独登记。`metadata-checked` 不隐含坐标验证；`coordinate-value-validated` 不隐含远程收益；合成 fixture 不升级真实样本等级。

## 004 登记定义

| 定义 ID | 网格 | 当前等级 | 固定样本与已验证内容 | 尚缺范围 |
|---|---|---|---|---|
| `gem_rdps_10km` | Rotated lat/lon | `metadata-checked` | [真实 Open-Meteo OM v3 样本](../specs/004-multi-grid-selection/evidence/baseline-local/open-meteo-s3-sample-acquisition.md)，shape `[1045,1140,114]`、chunks `[1,26,114]`；匹配 baseline 的 H1 子比较覆盖全部坐标和官方值位置并通过，见 [H1 证据](../specs/004-multi-grid-selection/evidence/baseline-local/h1-20261008-5dac993/)。 | 独立 OM 源轴到 producer 点序证明、H1/H2 source/info 子检查、受控远程收益与签名 S3 审计。 |
| `gem_regional` | Stereographic | `metadata-checked` | 同记录中的真实 Open-Meteo OM v3 样本，shape `[824,935,114]`、chunks `[1,26,114]`；匹配 baseline 的 H1 子比较覆盖全部坐标和官方值位置并通过，见 [H1 证据](../specs/004-multi-grid-selection/evidence/baseline-local/h1-20261008-5dac993/)。 | 独立 OM 源轴到 producer 点序证明、H1/H2 source/info 子检查、远程收益与签名 S3 审计。 |
| `aladin_central_europe_2km` | Lambert conformal conic | `metadata-checked` | pinned `ChmiDomain.swift` 中真实 ALADIN domain；固定 CHMI OM v3 样本 shape `[837,1053,120]`、chunks `[1,25,120]`；匹配 baseline 的 H1 子比较覆盖全部坐标和官方值位置并通过，见 [H1 证据](../specs/004-multi-grid-selection/evidence/baseline-local/h1-20261008-5dac993/)。 | 独立 OM 源轴到 producer 点序证明、H1/H2 source/info 子检查和远程收益。 |
| `lambert_formula_vector` | Lambert conformal conic | `definition-recorded` | 固定的 3×2 公式 identity vector；它不是 producer domain。CHMI ALADIN 是独立生产定义 `aladin_central_europe_2km`。 | 本公式 identity vector 仅作数学实现向量，不作为 producer 支持声明。 |
| `n160` | Reduced Gaussian N160 | `definition-recorded` | pinned Open-Meteo N160 行表与 f32 数值规则。 | N160 原生 OM v3、逐行点序参考、独立坐标和值参考。 |
| `n320` | Reduced Gaussian N320 | `definition-recorded` | pinned 完整行表定义；当前固定来源没有 N320 全域 producer domain。 | 可追溯原生 OM v3 对象和独立坐标/值/点序参考。 |
| `n320_ecmwf_aifs_europe_ensemble` | Reduced Gaussian N320 区域 | `definition-recorded` | 14,747 点区域及 local-to-parent 段由固定 producer 算术派生。 | 原生 OM v3 区域对象、GRIB/生产归档局部点序和官方值映射。 |

这些状态及样本 URI/hash 保存在 [`definitions.json`](../test/data/grids/definitions.json)、[`sample-manifest.json`](../test/data/grids/sample-manifest.json) 和 [样本获取记录](../specs/004-multi-grid-selection/evidence/baseline-local/open-meteo-s3-sample-acquisition.md)。registry 的 evidence/provenance 字段不参与 canonical `grid_id` 或 `layout_id`。

另有与 H1 使用相同 baseline build 和 sample-manifest hash 的 source-only 子检查，覆盖三个投影样本的全部空间点（合成 `valid_time` 选择 time index 0），核对 explicit/domain source 身份、参考坐标及 source/info 零值读取，详见 [H3 all-position 记录](../specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-all-public-source-positions-5dac993-h3-local/h3-local/manifest.json)。该结果作为 H1 的 source 位置辅助证据，不独立证明 OM 数组轴到 producer 点序，也不替代 H1/H2 查询级 source 子检查或完整 gate；因此三个 producer domain 继续保持 `metadata-checked`。

## ECMWF HRES O1280 补充样本

Open-Meteo 公共 AWS bucket `s3://openmeteo` 中有真实 ECMWF IFS HRES O1280 reduced-Gaussian OM v3 对象。已登记的 `static/HSURF.om` 是 2,482,560 bytes、root shape `[1,6599680]`、chunks `[1,400]`；官方 OM C API 与 DuckOMO 的全量 logical-index 值比较为 6,599,680 个位置、零 mismatch。另有 `temperature_2m/chunk_817.om` 时间序列对象，shape `[1,6599680,504]`、chunks `[1,6,504]`，目前只核过 metadata 和读取绑定。

它是有效的真实 Gaussian-family OM v3 补充样本，纠正“公开桶没有 Gaussian 样本”的说法。但 O1280 不是本期冻结的 N160、N320 或 N320 区域；HSURF 的 O1280 行长与局部/parent 点序、坐标参考尚未验证，也没有空间 full/local 收益记录，因此不能替代 N-grid 门槛或标为本期 Gaussian domain 已验收。对象 hash、S3 ETag、日期和官方值 CSV hash 见样本获取记录及其链接的 [`value-reference-manifest.json`](../test/data/grids/value-reference-manifest.json)。

## 构建、来源和支持边界

004 的真实网格本地证据对应 DuckDB v1.5.4、Linux AArch64 的 baseline 构建。官方 HTTPFS 路径另已通过 v1.5.4/v1.5.5/v1.5.6 的受控远程 runtime 和固定样本验证，见 [迁移记录](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md)，但完整 004 H8 仍未通过；2.0 开发版已退出本轮范围，Linux x86_64 也不在当前验收平台范围。公开匿名 S3 能读不等于该网格的签名 S3 远程收益及服务端审计通过；当前 DuckOMO 不观测 HTTPFS transport attempts/body，网络证据由受控服务日志提供。

截至 2026-10-08，匹配 baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` 的 H1 子比较已对三类真实投影样本全量核对坐标与官方 OM C 值并通过；比较依赖预期 `[ny,nx,ntime]` producer profile，独立 OM 对象轴映射仍未验证。真实 Gaussian 样本清单包含 ECMWF HRES O1280；N160、N320、N320 区域样本和独立点序参考门槛仍未满足，O1280 不替代这些目标。H0/H1/H6 的完整验收范围保持未通过。状态只有在该定义自己的 manifest、oracle、构建组合和 gate 证据齐全后才可提升。
