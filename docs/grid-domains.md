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
| `gem_rdps_10km` | Rotated lat/lon | `coordinate-value-validated`（2026-10-10 [pinned producer 参考决定](../specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)） | [真实 Open-Meteo OM v3 样本](../specs/004-multi-grid-selection/evidence/baseline-local/open-meteo-s3-sample-acquisition.md)，shape `[1045,1140,114]`、chunks `[1,26,114]`；匹配 baseline 的 H1 子比较覆盖全部坐标和官方值位置并通过，见 [H1 证据](../specs/004-multi-grid-selection/evidence/baseline-local/h1-20261008-5dac993/)。一致性对象是 Open-Meteo 发布对象，非原生 GRIB 网格。 | H1/H2 完整 gate 的 source/info 子检查、受控远程收益与签名 S3 审计、H9。 |
| `gem_regional` | Stereographic | `coordinate-value-validated`（2026-10-10 [pinned producer 参考决定](../specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)） | 同记录中的真实 Open-Meteo OM v3 样本，shape `[824,935,114]`、chunks `[1,26,114]`；匹配 baseline 的 H1 子比较覆盖全部坐标和官方值位置并通过，见 [H1 证据](../specs/004-multi-grid-selection/evidence/baseline-local/h1-20261008-5dac993/)。一致性对象是 Open-Meteo 发布对象，非原生 GRIB 网格。 | H1/H2 完整 gate 的 source/info 子检查、远程收益与签名 S3 审计、H9。 |
| `aladin_central_europe_2km` | Lambert conformal conic | `coordinate-value-validated`（2026-10-10 [pinned producer 参考决定](../specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)） | pinned `ChmiDomain.swift` 中真实 ALADIN domain；固定 CHMI OM v3 样本 shape `[837,1053,120]`、chunks `[1,25,120]`；匹配 baseline 的 H1 子比较覆盖全部坐标和官方值位置并通过，见 [H1 证据](../specs/004-multi-grid-selection/evidence/baseline-local/h1-20261008-5dac993/)。一致性对象是 Open-Meteo 发布对象，非原生 GRIB 网格。 | H1/H2 完整 gate 的 source/info 子检查和远程收益。 |
| `lambert_formula_vector` | Lambert conformal conic | `definition-recorded` | 固定的 3×2 公式 identity vector；它不是 producer domain。CHMI ALADIN 是独立生产定义 `aladin_central_europe_2km`。 | 本公式 identity vector 仅作数学实现向量，不作为 producer 支持声明。 |
| `ecmwf_ifs` | Reduced Gaussian O1280（ECMWF IFS HRES） | `coordinate-value-validated`（2026-10-10 [pinned producer 参考决定](../specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)） | [真实本地查询记录](../specs/004-multi-grid-selection/evidence/baseline-local/o1280-real-local-20261010/final.md)：全量 6,599,680 HSURF 值与官方解码零 mismatch，producer 坐标最大差值 0；显式/domain、空间筛选、22×504 时间值/NULL 通过。构建/SQL/结果/metrics 已冻结；坐标全量误差 0（producer 规则独立 Python 复算）。一致性对象是 Open-Meteo 发布对象；`openmeteo_approx_v1` 纬度为等距近似，非 ECMWF 原生高斯纬线。 | 完整时间值覆盖、受控远程 full/local/签名 S3、完整 gate 及 H9。 |
| `n160` | Reduced Gaussian N160 | `definition-recorded`；**本轮真实验收跳过** | pinned Open-Meteo N160 行表与 f32 数值规则；定义/native/synthetic 回归保留。 | 已调查公开来源未获匹配 OM 对象；负责人批准跳过，不阻塞本轮收口，不记为通过。 |
| `n320` | Reduced Gaussian N320 | `definition-recorded`；**本轮真实验收跳过** | pinned 完整行表定义；定义/native/synthetic 回归保留。 | 同上；不因等待 N-grid 样本阻塞本轮收口。 |
| `n320_ecmwf_aifs_europe_ensemble` | Reduced Gaussian N320 区域 | `definition-recorded`；**本轮真实验收跳过** | 14,747 点区域及 local-to-parent 段由固定 producer 算术派生；定义/native/synthetic 回归保留。 | 已调查公开来源未获匹配区域对象；跳过不证明真实区域点序。 |

这些状态及样本 URI/hash 保存在 [`definitions.json`](../test/data/grids/definitions.json)、[`sample-manifest.json`](../test/data/grids/sample-manifest.json) 和 [样本获取记录](../specs/004-multi-grid-selection/evidence/baseline-local/open-meteo-s3-sample-acquisition.md)。registry 的 evidence/provenance 字段不参与 canonical `grid_id` 或 `layout_id`。

另有与 H1 使用相同 baseline build 和 sample-manifest hash 的 source-only 子检查，覆盖三个投影样本的全部空间点（合成 `valid_time` 选择 time index 0），核对 explicit/domain source 身份、参考坐标及 source/info 零值读取，详见 [H3 all-position 记录](../specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-all-public-source-positions-5dac993-h3-local/h3-local/manifest.json)。该结果作为 H1 的 source 位置辅助证据，也不替代 H1/H2 查询级 source 子检查或完整 gate。按 [2026-10-10 pinned producer 参考决定](../specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md)，OM 轴到 producer 点序的一致性由 pinned 源码承担参考角色，三个 producer domain 已登记 `coordinate-value-validated`；完整 gate 与远程验收仍开放。

## ECMWF HRES O1280（`ecmwf_ifs` domain，2026-10-10 修订）

> **负责人批准的范围修订**：已调查的 Open-Meteo 公开来源未找到对应 N160/N320/N 区域 OM 对象，因此本轮跳过这些真实样本门禁，不阻塞 Phase 6 当前范围收口；保留定义与合成回归，不记为真实验证通过。`ecmwf_ifs` O1280 改为本轮 Gaussian 验收目标，不代表该目标已验收。原 spec FR-001/FR-027/SC-001、任务及 H0/H1/H6 的适用范围以 [2026-10-10 Gaussian 验收修订](../specs/004-multi-grid-selection/contracts/gaussian-acceptance-20261010.md) 为准；其他未完成门禁不豁免。

Open-Meteo 公共 AWS bucket `s3://openmeteo` 中的真实 ECMWF IFS HRES O1280 reduced-Gaussian OM v3 对象：

- `data/ecmwf_ifs/static/HSURF.om`（静态地形）：官方 OM C API 与 DuckOMO 全量 logical-index 值比较，6,599,680 个位置零 mismatch。
- `data/ecmwf_ifs/temperature_2m/chunk_817.om`（时间序列）：`[1,6599680,504]`、chunks `[1,6,504]`；本轮对 22 个点的完整 504 槽时间序列（11,088 值/NULL）、显式/domain 等价与半开时间过滤通过官方切片解码核对，不代表整个 33 亿值数组全量验收。未冻结的 chunk_987 不纳入证据。

`domain := 'ecmwf_ifs'` 已可用。真实 HSURF 内嵌 `lat lon` 轴名，使用 `read_om(path, domain := 'ecmwf_ifs')` 或显式声明 `['lat','lon']`；登记的 `lon` alias 指向展平点轴，不是规则经纬度网格，不能用 `['y','point']` 覆盖冲突元数据。chunk_817 使用 `dimensions := ['y','point','time']` 并显式提供 time 标签；无坐标元数据的既有 `point` 用法保留。完整行表由 [`scripts/generate-hres-o1280.py`](../scripts/generate-hres-o1280.py) 从 pinned `b06f4760` 源码规则生成，`--check` 校验定义行表一致性，另加 `--source-directory` 才校验下载源码 hash；坐标为 pinned producer Float32 规则复现（非 Legendre 根），与 `openmeteo_approx_v1` 数值策略一致。

对象 hash、S3 ETag、原始值比较命令/构建/CSV hash 见样本获取记录及其链接的 [`value-reference-manifest.json`](../test/data/grids/value-reference-manifest.json)。这些是历史无 domain 值证据；本轮新增 [真实本地验证](../specs/004-multi-grid-selection/evidence/baseline-local/o1280-real-local-20261010/final.md) 并修复 HSURF 轴绑定，但 pinned producer 的独立 Python 实现仍不是独立 GRIB 点序参考。历史 manifest 中 HSURF `names_embedded=false` 描述不完整，发现及实际轴名在新 freeze/query/evidence 中补充，不追溯改写旧 manifest。继续使用既有四级，不新增 `value-validated`。

## 构建、来源和支持边界

004 的真实网格本地证据对应 DuckDB v1.5.4、Linux AArch64 的 baseline 构建。官方 HTTPFS 路径另已通过 v1.5.4/v1.5.5/v1.5.6 的受控远程 runtime 和固定样本验证，见 [迁移记录](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md)，但完整 004 H8 仍未通过；2.0 开发版已退出本轮范围，Linux x86_64 也不在当前验收平台范围。公开匿名 S3 能读不等于该网格的签名 S3 远程收益及服务端审计通过；当前 DuckOMO 不观测 HTTPFS transport attempts/body，网络证据由受控服务日志提供。

截至 2026-10-08，匹配 baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` 的 H1 子比较已对三类真实投影样本全量核对坐标与官方 OM C 值并通过；比较依赖预期 `[ny,nx,ntime]` producer profile，独立 OM 对象轴映射仍未验证。**2026-10-10 修订**：N160、N320、N320 区域的真实样本验收经负责人批准本轮跳过，不阻塞当前范围收口，但不将历史 H0/H1/H6 改为通过。同日 [pinned producer 参考决定](../specs/004-multi-grid-selection/contracts/pinned-producer-reference-20261010.md) 后，`ecmwf_ifs` 与三个投影定义登记为 `coordinate-value-validated`（与 Open-Meteo 发布对象一致，非原生 GRIB 网格等价）；远程收益、完整 gate 与 H9 仍须补齐，等级不进一步提升。
