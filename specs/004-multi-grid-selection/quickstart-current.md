# 当前多网格复现指南

适用范围：Linux AArch64，官方 DuckDB v1.5.4、v1.5.5、v1.5.6 及同版本官方 HTTPFS；三个固定投影定义与 `ecmwf_ifs` O1280。N160/N320/N 区域的真实输入本轮按[范围修订](contracts/gaussian-acceptance-20261010.md)跳过，定义及合成回归保留。Open-Meteo 自产对象的坐标参考遵循[pinned producer 决定](contracts/pinned-producer-reference-20261010.md)，不声称原生 GRIB 等价。

当前批准范围 H0–H9 已通过，C004-01–04 关闭，见[最终报告](evidence/current-20261021/report.md)和[独立 AI 验收](evidence/current-20261021/independent-h9-final.md)。四类预览与完整 HSURF 空间差分示例已在三个版本实际通过，精确命令及结果哈希见[示例验证记录](evidence/current-20261021/guide-check.json)。本次仅更新状态文字，SQL 示例与当时执行版本一致；旧记录中的 guide_sha256 对应更新前全文。历史指南在 [quickstart.md](quickstart.md) 保留。

从仓库根目录执行。编译工具和参考生成可使用 Python；查询运行时使用 DuckDB CLI、扩展和原始 OM 对象。

## 固定版本与本地产物

[test/data/grids/version-matrix.json](../../test/data/grids/version-matrix.json) 固定三个官方组合，构建入口为：

```sh
scripts/build-version.sh v1.5.4 --output-root build/official-matrix
scripts/build-version.sh v1.5.5 --output-root build/official-matrix
scripts/build-version.sh v1.5.6 --output-root build/official-matrix
```

当前审计后的验证产物位于 `build/audit-remediation-20261021/<version>/duckomo.duckdb_extension`，逐版本 `identity.json` 绑定源摘要和产物哈希；CLI/官方 HTTPFS 位于 `build/official-matrix/<version>/official/`。这些扩展由变更 translation units 重编译并与固定基础对象重链接，非 clean rebuild。重新构建产生新身份时，验收记录必须绑定新产物，不能直接继承旧包的通过结论。

真实对象须与 [sample manifest](../../test/data/grids/sample-manifest.json) 的路径、大小及 SHA-256 一致。它们在本机留存在 `build/s3-samples/openmeteo-v3/`；公开地址可能失效。独立坐标/值参考的来源及 SHA-256 另见 [coordinate-reference.json](../../test/data/grids/coordinate-reference.json) 与 [value-reference-manifest.json](../../test/data/grids/value-reference-manifest.json)。禁止用待测扫描器输出生成其自身 oracle。

## 四类读取与空间关系

以下以当前 v1.5.4 产物为例；每次切换版本须同时切换 CLI 和扩展。SQL 的额外轴显式声明只表示原始轴顺序；未声明时间坐标时不把原始 time 下标称为生产者时间。

```sh
build/official-matrix/v1.5.4/official/duckdb -unsigned -batch -bail :memory: <<'SQL'
LOAD 'build/audit-remediation-20261021/v1.5.4/duckomo.duckdb_extension';
SET threads=1;
SET duckomo_max_threads=1;

CREATE TEMP VIEW rotated AS SELECT * FROM read_om(
  'build/s3-samples/openmeteo-v3/cmc-gem-rdps-10km-cape-chunk-4337.om',
  dimensions := ['y','x','time'], domain := 'gem_rdps_10km', include_source := true);
CREATE TEMP VIEW stereographic AS SELECT * FROM read_om(
  'build/s3-samples/openmeteo-v3/cmc-gem-rdps-cape-chunk-4337.om',
  dimensions := ['y','x','time'], domain := 'gem_regional', include_source := true);
CREATE TEMP VIEW lambert AS SELECT * FROM read_om(
  'build/s3-samples/openmeteo-v3/chmi-aladin-ce-cape-chunk-4135.om',
  dimensions := ['y','x','time'], domain := 'aladin_central_europe_2km', include_source := true);
CREATE TEMP VIEW gaussian AS SELECT * FROM read_om(
  'build/s3-samples/openmeteo-v3/ecmwf-ifs-hsurf-o1280.om',
  dimensions := ['lat','lon'], domain := 'ecmwf_ifs', include_source := true);

DESCRIBE rotated;
SELECT value,lat,lon,om_source FROM rotated LIMIT 4;
SELECT value,lat,lon,om_source FROM stereographic LIMIT 4;
SELECT value,lat,lon,om_source FROM lambert LIMIT 4;
SELECT value,lat,lon,om_source FROM gaussian LIMIT 4;

-- HSURF 是完整单空间平面；先完整物化，再施加相同平面三角形条件。
CREATE TEMP TABLE baseline AS
  SELECT value,lat,lon,om_source.logical_index AS logical_index FROM gaussian;
WITH actual AS MATERIALIZED (
  SELECT value,lat,lon,om_source.logical_index AS logical_index
  FROM gaussian WHERE lon>=7 AND lat>=0 AND lon+lat<=7.3
), expected AS MATERIALIZED (
  SELECT * FROM baseline WHERE lon>=7 AND lat>=0 AND lon+lat<=7.3
), diff AS (
  (SELECT * FROM actual EXCEPT ALL SELECT * FROM expected)
  UNION ALL
  (SELECT * FROM expected EXCEPT ALL SELECT * FROM actual)
)
SELECT CASE WHEN count(*)=0 THEN true ELSE error('spatial mismatch') END AS equal FROM diff;
SELECT metrics FROM duckomo_last_scan_metrics();
SQL
```

四行预览只检查加载和 schema。完整 H1 需要每个值位置的 Float32 位值/NaN→NULL、每个空间点坐标及原始 source 轴映射；完整 H2/H7 还需独立 producer 参考的点/面判定。`grid-engineering/` 工程包中的 `values.py`、`coordinates.py`、`selection.py` 保存这些当前执行入口、精确编译/查询命令和结果。完整 O1280 temperature 对象有 3,326,238,720 个值，不能用预览、时间零切片或少量区域代替全值检查。上述三角形使用 lon 为 x、lat 为 y 的平面角坐标，不表示物理面积或球面关系。

## 官方 HTTPFS 与严格成本

远程服务、TLS、当前凭据及私有 S3 setup 依照[官方 HTTPFS 指南](../../docs/official-httpfs.md)准备。受控服务须提供相同冻结对象及每次请求的服务端审计；私有 `run.env`、S3 SQL 和 TLS 密钥仅保存在 ignored build 目录。

每个版本加载同版本官方 HTTPFS，启用证书校验，并固定线程和缓存策略：

```sql
LOAD 'build/official-matrix/v1.5.4/official/httpfs.duckdb_extension';
LOAD 'build/audit-remediation-20261021/v1.5.4/duckomo.duckdb_extension';
SET threads=1;
SET duckomo_max_threads=1;
SET enable_external_file_cache=false;
SET enable_http_metadata_cache=false;
SET enable_object_cache=false;
SET enable_server_cert_verification=true;
-- 受控 HTTPS 另设置其 CA；S3 使用当前连接的私有 secret setup。
```

将同一本地查询的 path 替换为 HTTP、HTTPS 或签名 S3 URI，固定完全相同的列依赖，完整消费全扫和局部查询。每个结果与独立本地 oracle 比较；所有 bind、探测、准备和 retry 的服务端发送 body 都纳入审计。仅当服务发送 body、值 data bytes、decoded chunks **全部严格下降**时，才通过 004 收益门槛。005 的非递增成本修订不适用于此处。

远程 metrics v4 的 transport 值为 NULL、complete=false，不能补零；服务端发送量不等于客户端接收量。DuckOMO 自有缓存已移除，不调用 `duckomo_cache_enabled`、`duckomo_cache_capacity` 或 `duckomo_clear_cache()`。具体边界以[官方 HTTPFS 契约](evidence/official-httpfs-refactor/contract.md)为准。

H5/H6 还要求远程至少 10× 空间点数的同 chunk/线程窄区内存配对、重复会话、失败/取消/恢复、可观察版本冲突、同连接签名 S3 撤权/恢复及 secret/endpoint/region 变化。HTTPFS 内部分配不属于 DuckOMO owned-memory 账本，不能从自有终态释放推导总进程内存为零。

H8 汇总三版本的实际身份、匹配加载、跨版本拒绝和同输入 H0–H7。H9 由未参与实现者实际执行四类读取、一次远程全/局部成本与一次完整空间关系，并封存命令、退出码、参考、指标、服务日志切片及哈希；当前范围允许明确标识的独立 AI 验证。本轮当前范围的 H8/H9 已完成；其他对象/平台不能继承此结论。
