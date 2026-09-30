# Quickstart 独立复核

日期：2026-09-29。复核于 08:11 UTC（Asia/Shanghai 16:11）完成。执行环境为 Linux AArch64，内核 `6.17.0-1029-nvidia`，DuckDB v1.5.4 release，64-bit；I/O 对照使用单线程。复核者未参与此功能实现。

本记录覆盖 [quickstart](../quickstart.md) 中的显式网格、真实 domain、区域结果和 I/O 对照。未运行完整 `make test`、`scripts/validate.sh` 或 `make sanitizer-test`，因为这些是独立的实现门禁；Linux x86_64 按当前范围暂缓。

## 样本身份

quickstart 中真实样本的 SHA-256 校验命令退出码为 0：

```sh
printf '%s  %s\n' \
  '0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd' \
  'build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om' \
  | sha256sum -c -
```

真实文件为 5,812,040 字节；domain manifest 的 SHA-256 为 `dfd954fa8059fbffe25cafd559d583d8383df4414c855711f710d2da88e79406`。样本定义来源固定为 Open-Meteo commit `34b9cea169395be9b4686f2b5b23eca26dfef7a2`；这标识网格定义来源，不表示部署中的生产者一定使用了该提交。

其他复核资产身份：`test/data/raw.om` 为 `6a34044749250c0de21d65d40d4f6c270c3ae31d16ac623626bc6b2f7a01ced3`，其值参考 `test/data/raw.reference.csv` 为 `d877c8234157eba79c072172708c89a5a54d87aa83bd8a3307e0731dd1492ad3`；固定 I/O fixture `test/data/projection.om` 为 `fb36284bf8f932c82b171c7bce28b6e767b95321d1d986dc0a1dbd3b84d88a43`，temperature 参考为 `00a0e2d2e27a2ecc4f59e74d1264830ba2bbfafa679ea2790fd4d7791b997aed`。

## 显式网格

按 [quickstart 的显式网格命令](../quickstart.md#显式网格和独立基准) 完整执行 SQL here-doc，退出码为 0。DESCRIBE 返回 `value FLOAT, latitude DOUBLE, longitude DOUBLE`；区域查询按 value 排序得到 `(3, 11, 100)`、`(4, 11, 102)`。查询与先完成物化的全量表作双向 `EXCEPT ALL` 比较，差异为 0 行；`latitude > 90` 和 `latitude >= 11` 的计数分别为 0 和 3。

## 真实 domain 和区域结果

先按上节固定样本及 SHA-256 校验，再执行 quickstart 中的 domain 查询。为核对区域的行多重集，额外执行下列命令；全域先物化为临时表，再与 domain 查询的过滤结果双向比较：

```sh
env -u DUCKOMO_METRICS_OUTPUT ./build/release/duckdb -unsigned -bail :memory: <<'SQL'
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
CREATE VIEW domain_grid AS SELECT * FROM read_om(
 'build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om',
 domain := 'ncep_gfswave025');
SELECT count(*), min(latitude), max(latitude), min(longitude), max(longitude)
FROM domain_grid;
CREATE TEMP TABLE domain_materialized AS
SELECT wave_height,latitude,longitude FROM domain_grid;
SELECT count(*), count(wave_height) FROM domain_grid
WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120;
SELECT count(*) FROM (
 (SELECT wave_height,latitude,longitude FROM domain_grid
  WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120
  EXCEPT ALL
  SELECT wave_height,latitude,longitude FROM domain_materialized
  WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120)
 UNION ALL
 (SELECT wave_height,latitude,longitude FROM domain_materialized
  WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120
  EXCEPT ALL
  SELECT wave_height,latitude,longitude FROM domain_grid
  WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120)
);
SELECT count(*) FROM domain_grid WHERE longitude >= 170 OR longitude < -170;
SELECT count(*) FROM domain_grid WHERE longitude BETWEEN 170 AND -170;
SQL
```

以上命令退出码为 0。全域为 1,038,240 行，范围纬度 `[-90, 90]`、经度 `[-180, 179.75]`。区域返回 1,681 行，其中 `wave_height` 非 NULL 的有 101 行；与全域物化后再过滤的双向差异为 0 行。跨接缝 OR 返回 57,680 行，反向 `BETWEEN 170 AND -170` 返回 0 行。

独立全域 oracle 命令退出码为 0：

```sh
./build/release/test/native/domain_reference_test \
  build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om \
  test/data/domain-manifest.json build/evidence/spatial/reference
```

显式网格和已注册 domain 均逐点核对 1,038,240 个位置。15 个值变量与固定 OM C reader 参考逐值一致，NULL 位置一致；坐标与独立标量公式的绝对误差不超过 `1e-9` 度。该命令使用 manifest 指定的 `build/evidence/spatial/reference` CSV。一次将其指向空临时目录的调用因目录中没有官方 CSV 而以退出码 1 结束；改为 manifest 中已有的参考目录后验证通过，属于调用路径设置错误。

## 读取指标

按 quickstart 的 `duckomo_spatial_validation` 命令运行，输出定向到 `/tmp/duckomo-spatial-review-independent/harness`，退出码为 0。它验证了固定 `projection.om` full/restricted 场景：temperature 查询结果为 10,541/25 行，data bytes 为 `165,767 → 1,795`，decoded chunks 为 `503 → 5`。相同摘要另见临时 sidecar `harness/summary.json`；每个场景为新 DuckDB 进程，应用层缓存关闭，OS page cache 未清除。

另外对真实 GFS Wave 样本的同一 `wave_height` 列，在两个独立 release DuckDB 进程中分别完整消费 full 与下列区域查询，并通过 `DUCKOMO_METRICS_OUTPUT` 收集 v2 sidecar：

```sh
env DUCKOMO_SCENARIO=domain_full DUCKOMO_FIXTURE_ID=ncep_gfswave025 \
  DUCKOMO_FIXTURE_SHA256=0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd \
  DUCKOMO_METRICS_OUTPUT=/tmp/duckomo-spatial-review-independent/domain-full.metrics.json \
  ./build/release/duckdb -unsigned -bail -no-stdin -csv -noheader -nullvalue NULL \
  -c "SET threads=1; LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension'; SELECT wave_height FROM read_om('build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om', domain := 'ncep_gfswave025')" \
  :memory: >/dev/null

env DUCKOMO_SCENARIO=domain_restricted DUCKOMO_FIXTURE_ID=ncep_gfswave025 \
  DUCKOMO_FIXTURE_SHA256=0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd \
  DUCKOMO_METRICS_OUTPUT=/tmp/duckomo-spatial-review-independent/domain-restricted.metrics.json \
  ./build/release/duckdb -unsigned -bail -no-stdin -csv -noheader -nullvalue NULL \
  -c "SET threads=1; LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension'; SELECT wave_height FROM read_om('build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om', domain := 'ncep_gfswave025') WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120" \
  :memory: >/dev/null
```

两个查询都以退出码 0 成功完成，`decode_count_complete=true`。full/restricted 分别为 1,038,240/1,681 个候选行；`wave_height` data bytes `10,595,401 → 28,426`、decoded chunks `32,445 → 82`、data requests `6,724 → 73`、index bytes `147,318 → 3,526`、总读取字节 `10,748,253 → 37,486`。临时 sidecar 位于 `/tmp/duckomo-spatial-review-independent/domain-full.metrics.json` 和 `domain-restricted.metrics.json`。

真实样本 full 的累计应用层 data bytes 大于文件大小，因为指标累加了重复数据块读取；这不是唯一文件字节数，也不代表设备 I/O。该对照证明此文件与该区域下应用层读取和解码工作减少，不推广到其他 OM chunk 布局。
