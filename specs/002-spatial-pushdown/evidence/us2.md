# US2: 空间条件缩小实际读取

日期：2026-09-29。环境：Linux AArch64，DuckDB v1.5.4 release，单线程；OS page cache 未清空，每个查询使用新的 DuckDB 进程。验证命令退出码 0：

```sh
./build/release/test/tools/duckomo_spatial_validation \
  --root "$PWD" --fixtures "$PWD/test/data" \
  --output "$PWD/build/evidence/spatial" \
  --duckdb "$PWD/build/release/duckdb" \
  --extension "$PWD/build/release/extension/duckomo/duckomo.duckdb_extension" \
  --domain-file "$PWD/build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om"
```

固定样本 `test/data/projection.om` SHA-256 为 `fb36284bf8f932c82b171c7bce28b6e767b95321d1d986dc0a1dbd3b84d88a43`；值参考 `projection.temperature.reference.csv` SHA-256 为 `00a0e2d2e27a2ecc4f59e74d1264830ba2bbfafa679ea2790fd4d7791b997aed`。grid 为 127×83，latitude=`-41+y`，longitude=`normalize_180(-126+2*x)`。区域在验收前固定为 latitude `[-2,2]`、longitude `[-4,4]`。full 查询完整消费相同 temperature 列，逐位置对官方 reader 值和独立坐标公式核对；restricted 查询与 full 物化行集合差分。

| 场景 | 返回/候选行 | temperature data bytes | 解码块 | 结果 |
|---|---:|---:|---:|---|
| full | 10,541 / 10,541 | 165,767 | 503 | oracle 全位置通过 |
| restricted | 25 / 25 | 1,795 | 5 | 与 full 物化过滤完全一致 |
| empty | 0 / 0 | 0 | 0 | 所有值变量 index/data/decode 为显式零 |
| coordinates | 25 / 25 | 0 | 0 | 坐标来自 full 位置集且满足精确 WHERE |
| count | 1 标量 / 25 | 0 | 0 | count 为 25，无值依赖 |
| optimizer_empty | 1 个成功的 0 / 0 | 0 | 0 | `EXPLAIN EMPTY_RESULT` 与 bind metadata sidecar 均在 |

full 与 restricted 的 `data_bytes` 和 `decoded_chunks` 都严格下降。此结论仅适用于这个固定多块 fixture 和区域，不外推至其他布局。元数据读取独立计量，不计入表中值数据字节。

查询 JSON、schema v2 metrics sidecar 及 optimizer 计划保存在本目录：`full*`、`restricted*`、`empty*`、`coordinates*`、`count*`、`optimizer_empty*`、`summary.json`。每个 sidecar 含完整命令、fixture hash、依赖提交、bytes/requests/chunks、耗时、RSS、架构、缓存策略和结果状态。
