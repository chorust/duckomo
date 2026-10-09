# US5 本地进度

记录日期：2026-10-06。以下是固定合成输入上的实现检查；US5/H7 尚未完成。

## Source 身份检查

- `source_identity_test` 退出码 0：检查 separate、x-fastest、y-fastest 和 Gaussian 区域的原始 logical/local/parent/axis positions；过滤、降序排序以及 262145 点并行长线扫描都未重编号。长线查询确认至少两个 worker 同时执行。
- 该测试检查本地对象 ID 为不透明 SHA-256 字符串，缺少版本时 `object_version=NULL`、`version_strength=unverifiable`、`content_verified=false`；source-only 投影的值 index/data/decode 全为零。
- `test/sql/source_identity.test` 退出码 0，40 assertions 通过：默认输出仍只有原列、opt-in `om_source` 的 STRUCT 字段类型和末尾位置固定、遗漏网格/同名源列时拒绝 source 请求、过滤/排序身份和非空间重复记录保持原样。
- 区域 parent grid identity 的规范化已有 `grid_identity_test` 向量覆盖；Gaussian source 行另核对局部点到 parent point 映射。

## Grid descriptor 实现检查

- `cmake --build build/release-vcpkg --target duckomo_loadable_extension` 退出码 0；扩展注册 `om_grid_info`，同 `read_om` 共用全变量 metadata、grid/domain、CRS、axis 和 temporal binder，描述扫描不创建 value decoder。
- `test/sql/grid_info.test` 退出码 0，100 assertions 通过：固定 13 列及 JSON 类型、一行输出、定义参数/布局 shape 与 strides/CRS/能力/provenance、opaque object evidence、无 grid 拒绝和 v4 `operation=grid_info` 终态；指标检查值 data/decode 为零。
- `test/data/source-conflict.om` 由官方 OM C encoder 生成，SHA-256 `e6ff6ad3a001cc3ae13ddcfa46aec1b27cd54a80cc6849af12573812d69171a9`，登记在 `test/data/manifest.json`；生成器重复输出与 manifest/文件逐字节一致。
- `test/sql/multi_grid.test` 退出码 0，84 assertions 通过；与 `source_identity.test`、`grid_info.test` 一起验证旧绑定与新增接口并存。
- `bash -n scripts/validate.sh` 和 `git diff --check` 均退出码 0。

## 未完成范围

T069 的独立四类点/polygon oracle 及 T070/T071 的 H7 验收仍未完成；缺少真实对象与冻结参考输入时不将合成绑定/描述检查提升为完整 US5 验收。


## 2026-10-08 H1/H2 公共 Open-Meteo OM source 子检查

在固定 baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` 上，仅用已发布且 hash-pinned 的三类 Open-Meteo `.om` 投影样本运行公开 source validator，退出码 0。全空间位置逐行 identity/坐标对照通过（1,191,300、770,440、881,361 点），显式与 domain source/info/identity 的值 index/data/decode 读取为零；闭合 0.2° 子集分别为 15、12、247 点，与全扫描基准相同。合成 `valid_time` 只选择 time-axis index 0，不代表真实 producer 时间或独立证明 OM 数组轴映射。完整 H1/H2 与 T070 仍未通过；Gaussian N 样本、H6 公共跨 URI/远程身份和 OM 轴到 producer 点序证明仍缺。详细报告见 [`us5-20261008-h1-public-source-info-5dac993`](us5-20261008-h1-public-source-info-5dac993/)。


## 2026-10-08 公共 HTTPS/S3 source 子检查诊断

已为 `grid_spatial_reference.py` 增加 manifest 驱动的 `--public-source-uri-kind https|s3`、配套 `--httpfs` 加载、按 Open-Meteo HTTPS endpoint 推导 S3 region，以及不要求 object_id 相等的流式原始位置比较；默认 local 行为保留。工具单测 15 项通过。实际配套 baseline 在 HTTPS 和 `s3://openmeteo` 查询时均于扫描前报 `could not establish a strict range session`（S3 又按 `us-west-2` 重试仍失败）。独立只取 16 bytes 的公开 HTTPS range 返回 206、对象 ETag/长度与 manifest 快照一致，确认现有 `.om` endpoint 支持 Range；未获取完整对象或天气值。远程 H6 仍未通过，也未把 ETag 当作内容 hash。日志和范围响应证据见 [`us5-20261008-public-remote-source-attempts-5dac993`](us5-20261008-public-remote-source-attempts-5dac993/)。


## 2026-10-08 公共 HTTPS/S3 source/info 子检查：代理解析已定位

GDB 回溯确认先前 HTTPS 与 S3 的 `strict range session` 失败发生在网络请求前：DuckDB 的 `http_proxy` 当前值为 `http://127.0.0.1:7890/`，`HTTPUtil::ParseHTTPProxyHost` 将尾斜杠并入端口后抛错。移除代理环境变量，以及把代理值规范化为不带尾斜杠的 `http://127.0.0.1:7890`，两种最小 metadata/range 打开均成功。

之后在固定 baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` 上，以 `HTTP_PROXY=http://127.0.0.1:7890` 重跑全位置公开 source/info validator：HTTPS 与 S3（`us-west-2`）均 exit 0，状态分别为 `public_source_full_https_pass` 和 `public_source_full_s3_pass`。三个 hash-pinned 投影 OM v3 样本共 2,843,101 个空间位置逐行匹配本地副本；显式/domain source 一致；空间子集为 15、12、247 点并与完整扫描平面相同；source/info 值 index/data/decode 为零。完整结果和输出 hash 见 [proxy diagnosis](us5-20261008-public-source-proxy-diagnosis.md) 及对应的 [HTTPS](us5-20261008-public-remote-source-corrected-proxy-5dac993-https/manifest.json) / [S3](us5-20261008-public-remote-source-corrected-proxy-5dac993-s3/manifest.json) 记录。

这仍是投影对象的 source/info 子检查：validator 合成 `valid_time` 标签只选择 time index 0，未证明 OM 数组轴到 producer 点序映射；远端对象版本不可验证、天气值没有对照、无服务端 body 对账；N160/full N320/N320-region OM 样本仍缺。完整 H1/H2/H6/H7 与 T070 保持未完成。
