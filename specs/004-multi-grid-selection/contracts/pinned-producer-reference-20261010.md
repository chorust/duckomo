# 2026-10-10 Pinned Producer 参考接受决定

## 决定

负责人在审查 O1280 本地真实验证后明确决定：**“可以认为一致，按照 Open-Meteo 自己的 Swift 项目就是这样的。”** 即：对 Open-Meteo 公开发布、且由 pinned `b06f4760fd1f997e5559bb380f64c5e496b4a509` Swift 源码自身产出的 OM v3 对象，pinned producer 源码（经独立于 DuckOMO 的移植逐点核对）接受为坐标/点序参考，满足升级 `coordinate-value-validated` 的参考条件。

适用范围（本轮）：

| 定义 | 对象 | 已通过的坐标/值对照 |
| --- | --- | --- |
| `ecmwf_ifs`（O1280） | `ecmwf_ifs/static/HSURF.om`、`temperature_2m/chunk_817.om` | 全量 6,599,680 坐标对 pinned producer 规则独立 Python 复算误差 0（≤1e-4°）；全量值与官方 OM C 解码 Float32 位级一致；显式/domain、空间筛选、22×504 时间值/NULL 对照通过。见 [o1280-real-local-20261010](../evidence/baseline-local/o1280-real-local-20261010/final.md)。 |
| `gem_rdps_10km`（rotated） | `cmc_gem_rdps_10km/cape/chunk_4337.om` | 匹配 baseline build `5dac993…` 的 H1 全量对照：坐标 ≤1e-4°（最大 3.05e-5°），值按原始 logical_index 与官方 OM C Float32/NaN 参考精确一致。见 [h1-20261008-5dac993](../evidence/baseline-local/h1-20261008-5dac993/)。 |
| `gem_regional`（stereographic） | `cmc_gem_rdps/cape/chunk_4337.om` | 同上。 |
| `aladin_central_europe_2km`（Lambert） | `chmi_aladin_central_europe_2km/cape/chunk_4135.om` | 同上。 |

本决定覆盖原 [validation-evidence.md](validation-evidence.md) “坐标参考最低策略”中对这四类对象要求的“独立原始 GRIB/归档扫描”参考来源；原要求对**非 Open-Meteo 自产对象**（如未来直接取得的 ECMWF GRIB、其他生产者对象）继续有效。

## 语义边界（不可由本决定推出）

1. **一致性对象是“Open-Meteo 发布对象”而非“ECMWF 原生 GRIB 网格”**。`openmeteo_approx_v1` 纬度规则是 producer 的有意等距近似（非 Legendre 根）；不得把该一致性宣传为与 ECMWF 归档高斯纬线等价。
2. 不替代远程验收：受控 HTTP(S)/S3 同内容、冷 full/local 收益、签名 S3/TLS 审计、完整内存 gate 与 H8 版本矩阵仍是 `remote-benefit-validated` 和完整 Phase 6 的前置；本轮四个定义**不**升级到 `remote-benefit-validated`。
3. 不替代 H9：独立复现仍须由未参与实现者执行。
4. 同类不继承：本决定只适用于上表四个定义与其冻结样本；`n160`/`n320`/N320 区域、其他 producer 对象、其他样本不得引用本决定升级。
5. 参考实现的独立性要求保留：坐标参考必须经不调用 DuckOMO 映射内核的独立移植/实现核对（O1280 的独立 Python 复算、投影的 pinned-f32 参考生成器），不允许用待测内核自身输出充当参考。

## 状态记录

- 四个定义升级为 `coordinate-value-validated`，逐定义 claims 记录样本、构建范围与本决定链接；`ecmwf_ifs` 的 `build_pair` 仍为 `null`（本轮为本地开发构建留证，非已验收 matrix pair）。
- 历史 manifests、hash 与 gate 结果不追溯改写；旧“独立 GRIB 参考未取得的 not-run”记录保留，其阻塞意义由本决定解除（限上表四个定义）。
- Phase 6 仍为 `in-progress`；远程、完整 gate 与独立复现缺口继续开放。
