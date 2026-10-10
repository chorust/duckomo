# Quickstart: 空间查询与验收

> 本页含历史阶段的构建/复现示例。当前开发版地理列已改为 `lat/lon`，并支持共享轴 `dimensions := [轴名...]`；旧 `latitude/longitude` 引用需迁移，详见 [接口修订](../../docs/interface-migration.md)。不改写历史验证结果。

状态：Phase 3 实现、Linux AArch64 运行验证和独立 quickstart 复现指南。Linux x86_64 支持与验证按用户决策暂缓，不属于本期发布门禁。SQL 语义见 [接口契约](contracts/sql-interface.md)，指标见 [观测契约](contracts/validation-evidence.md)，布局见 [data model](data-model.md)。所有命令从仓库根目录执行。

## 构建与合成样本验证

前提：固定 git submodules、C/C++ 工具链、CMake/make、jq、sha256sum、diff；下载真实样本另需 curl。查询本身不需要 Python/Swift。

```sh
git submodule update --init --recursive
make release
./scripts/validate.sh build/release
make sanitizer-test
```

预期：旧 raw/schema/projection 测试及新增空间 SQL/native 检查全通过；fixture 哈希与临时重生成一致。`make test` 不要求下载真实 domain 样本。sanitizer 用于边界/生命周期，不用于性能比较。记录实际运行平台；本期验收仅覆盖 Linux AArch64，不能据此推断 x86_64 兼容性。

## 显式网格和独立基准

```sh
./build/release/duckdb -unsigned :memory: <<'SQL'
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
CREATE VIEW spatial_raw AS
SELECT * FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['lat','lon']]),
  grid := {'nx':3, 'ny':2, 'lat0':10.0, 'lon0':100.0,
           'dlat':1.0, 'dlon':2.0, 'order':'separate'},
  spatial_axes := ['lat','lon']);
DESCRIBE SELECT * FROM spatial_raw;
CREATE TEMP TABLE full_reference AS SELECT * FROM spatial_raw;
SELECT value,latitude,longitude FROM spatial_raw
WHERE latitude >= 11 AND longitude < 104 ORDER BY value;
SELECT * FROM (
  (SELECT * FROM spatial_raw WHERE latitude >= 11 AND longitude < 104
   EXCEPT ALL SELECT * FROM full_reference WHERE latitude >= 11 AND longitude < 104)
  UNION ALL
  (SELECT * FROM full_reference WHERE latitude >= 11 AND longitude < 104
   EXCEPT ALL SELECT * FROM spatial_raw WHERE latitude >= 11 AND longitude < 104)
);
SELECT count(*) FROM spatial_raw WHERE latitude > 90;
SELECT count(*) FROM spatial_raw WHERE latitude >= 11;
SQL
```

预期 schema 为 value FLOAT、latitude DOUBLE、longitude DOUBLE；区域输出 `(3,11,100)`、`(4,11,102)`；双向 EXCEPT ALL 返回零行；两个 count 分别为 0、3。全量人工坐标表：值 0/1/2 对应纬度 10、经度 100/102/104，值 3/4/5 对应纬度 11、同一经度序列。此表独立确认坐标，不只依赖两条 scanner 查询互相比较。

临时表必须先创建完成，避免子查询被优化器重新下推。小型 raw.om 只验证正确性，不作读取减少验收。

## 展平和额外轴

仓库提供可重生成的 `test/data/spatial_flat.om`：根 value shape `[2,6]`，调用者轴 `[sample,point]`，每个 sample 的 point 对应 2×3 网格，lon_fastest。原扫描有 12 行；下面的聚合查询返回六组，每个 `(latitude,longitude)` 的 count 为 2，证明 sample 未被折叠。

```sql
SELECT latitude, longitude, count(*)
FROM read_om('test/data/spatial_flat.om',
  dimensions := map(['value'], [['sample','point']]),
  grid := {'nx':3, 'ny':2, 'lat0':10.0, 'lon0':100.0,
           'dlat':1.0, 'dlon':2.0, 'order':'lon_fastest'},
  spatial_axes := ['point'])
GROUP BY latitude,longitude ORDER BY latitude,longitude;
```

输出为六组，每组 count=2；无 sample 语义列；带时间坐标的文件可输出 `valid_time`，见 [有效时间查询](../../README.md#有效时间查询)。lat_fastest、反向轴及额外轴在中/后位置的对照由 native/SQL 验收集覆盖。对于分离轴，spatial_axes 始终按纬度轴、经度轴填写，文件实际轴位置由 dimensions 确定。

## 真实 domain

优先使用已有本地文件；缺失时下载完整文件再进行本地验证：

```sh
spatial_sample='build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om'
mkdir -p "$(dirname "$spatial_sample")"
if [ ! -f "$spatial_sample" ]; then
  curl -fL 'https://openmeteo.s3.amazonaws.com/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om' -o "$spatial_sample"
fi
printf '%s  %s\n' '0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd' "$spatial_sample" | sha256sum -c -
```

哈希不符或对象不可获取时，完整 domain 验收失败；不能仅合成同 shape 文件替代真实样本。独立参考和来源固定步骤由 spatial harness 验证，禁止调用生产 GridMapping 生成参考坐标。

```sh
./build/release/duckdb -unsigned :memory: <<'SQL'
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
CREATE VIEW domain_grid AS SELECT * FROM read_om(
 'build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om',
 domain := 'ncep_gfswave025');
SELECT count(*), min(latitude), max(latitude), min(longitude), max(longitude)
FROM domain_grid;
SELECT wave_height,latitude,longitude FROM domain_grid
WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120;
SELECT count(*) FROM domain_grid WHERE longitude >= 170 OR longitude < -170;
SELECT count(*) FROM domain_grid WHERE longitude BETWEEN 170 AND -170;
SQL
```

预期全域 count=1,038,240，纬度 [-90,90]，经度 [-180,179.75]；矩形查询返回 1,681 行（值为 NULL 不会删除行）；接缝 OR count=57,680，反向 BETWEEN count=0。OR 结果正确但允许回退全读。domain 等价的显式 grid 为 nx=1440/ny=721/lat0=-90/lon0=-180/dlat=dlon=0.25/order=separate，spatial_axes=['lat','lon']，使用现有 coordinates 轴证据，无需手填 15 变量 dimensions。

## I/O 与发布门禁

```sh
./build/release/test/tools/duckomo_spatial_validation \
  --root "$PWD" --fixtures "$PWD/test/data" \
  --output "$PWD/build/evidence/spatial" \
  --duckdb "$PWD/build/release/duckdb" \
  --extension "$PWD/build/release/extension/duckomo/duckomo.duckdb_extension" \
  --domain-file "$PWD/build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om"
```

预期生成 `summary.json`、每个查询的 v2 sidecar，以及 full/mixed_baseline/restricted/empty/coordinates/count/mixed/fallback/optimizer_empty/domain_count 场景记录；`domain_reference.json` 记录全域显式配置、registry 和 15 个官方 reader 参考的逐位置比对。任一比较失败、必需输入缺失、哈希变化或指标不完整时返回非零。固定 projection.om 的区域、列集合和 oracle 见观测契约。至少验证：

- restricted 的实际 data_bytes 与 decoded_chunks 均严格小于相同值列 full。
- empty/coordinates/纯空间 count 的值 index/data/decode 均为零。
- mixed 只读输出与过滤依赖，pressure 等无关变量解码零。
- 全域物化过滤、独立坐标、官方值参考全部一致；domain 与显式网格完整核对。
- 负向配置、错误/取消后恢复通过；失败记录不作为性能成功证据。

固定 projection.om 上 full/restricted 使用同一 temperature 列，值数据字节为 `165,767 → 1,795`，解码块为 `503 → 5`。该结论只适用于固定多块样本和固定区域；其他文件需按自身 OM chunk 布局重新测量。

比较前固定坐标参考绝对容差 1e-9 度、值容差 0，NULL 和逻辑位置精确核对。记录单线程、release 构建、CPU 架构与 OS 缓存条件。运行后将逐查询 sidecar 和命令摘要归档到 [US2 evidence](evidence/us2.md)、[US3 evidence](evidence/us3.md)，最终记录在 [final evidence](evidence/final.md)。
