# Quickstart validation: 多区域读取与分时统计

**历史验收：2026-10-14，源码快照 `77b79d279b73…` 的三版本回归/domain参考、每版16项sanitizer、P0–P6及 v1.5.4 的独立 AI P7通过，见 [历史验证记录](evidence/artifact-binding-77b79d279b73.md)。变量选择与元数据修复的三个新构建、v1.5.5 回归/16项 sanitizer/P0–P4及真实 S3 同查询对照见 [元数据修复验证](evidence/metadata-selection-fixes.md)；历史结果不证明当前源码通过。P5/P7限冻结局部窗口，P7不是人工验收。**

## 1. 准备匹配构建与输入

从仓库根运行。环境固定为 Linux AArch64 / v1.5.4；另外两个版本使用各自独立产物重复验证，不跨版本加载。

```bash
python3 scripts/fetch-region-runtime.py
scripts/build-version.sh --matrix test/data/grids/version-matrix.json \
  --pair v1.5.4 --output-root build/regions-matrix --region-sanitizers
scripts/validate.sh --matrix test/data/grids/version-matrix.json \
  --pair v1.5.4 --output-root build/regions-matrix --local-only \
  --regions --region-gates P0,P1,P2,P3,P4
```

按生成的 build manifest 选择 CLI/扩展路径，CLI 与扩展必须匹配版本。若复用本轮产物，读取 `build/official-matrix/<version>/build-manifest.json` 的 `release_dir` 和 `artifact_hashes`，并核对磁盘 hash；官方 CLI、HTTPFS 和 spatial 身份分别来自 `test/data/regions/runtime-matrix.json`。下文 SQL 的 DuckOMO 加载路径须替换为该 `release_dir/extension/duckomo/duckomo.duckdb_extension`。

三个 metadata 入口 `read_om`、`read_om_regions`、`om_grid_info` 支持 `variable := '编码后的列名'`；省略则绑定全部数组，选择时 dimensions MAP 只覆盖所选数组。默认复用绑定元数据，`SET duckomo_revalidate_metadata=true` 启用全树重读检查。青海与 Nino 的可复现真实对象示例见 [README Usage](../../README.md)。

区域入口要求调用者显式加载匹配版本 spatial；运行时不自动联网安装。合成 fixture、独立 membership/统计参考及 manifest 已在 `test/data/regions/`，缺 fixture 或 spatial 时停止，不替换为任意文件。

启动匹配 DuckDB CLI（本地 DuckOMO 未签名时使用 `-unsigned`），加载实际路径：

```sql
-- 将此路径替换为刚构建且 hash 已核对的 DuckOMO 文件。
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
LOAD 'build/regions-runtime/v1.5.4/official/spatial.duckdb_extension';
SELECT version();
PRAGMA platform;
SELECT function_name, parameter_types, return_type
FROM duckdb_functions()
WHERE lower(function_name) IN ('st_geomfromwkb','st_isvalid','st_point','st_covers');
SELECT ST_Covers(ST_GeomFromText('POLYGON((0 0,2 0,2 2,0 2,0 0))'), ST_Point(0,1));
-- 最后一项应为 true；C++ adapter 与 worker 状态还由 native gate 检查。
```

## 2. 定义区域和单对象参数

```sql
CREATE TEMP TABLE regions(region_id VARCHAR, geom GEOMETRY);
INSERT INTO regions VALUES
 ('A', ST_GeomFromText('POLYGON((100 20,102 20,102 22,100 22,100 20))')),
 ('B', ST_GeomFromText('POLYGON((101 21,103 21,103 23,101 23,101 21))')),
 ('Empty', ST_GeomFromText('POLYGON EMPTY'));

SET VARIABLE region_input = (
 SELECT COALESCE(list({'id':region_id, 'wkb':ST_AsWKB(geom)} ORDER BY region_id),
                 []::STRUCT(id VARCHAR,wkb BLOB)[]) FROM regions
);
SET VARIABLE demo_grid = {'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,
                          'dlat':1.0,'dlon':1.0,'order':'separate'};
SET VARIABLE demo_axes = {
 'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},
 'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}
};

CREATE TEMP VIEW region_samples AS
SELECT * FROM read_om_regions('test/data/regions/regions.om',
 regions := getvariable('region_input'), region_crs := 'grid_lonlat',
 dimensions := ['member','time','lat','lon'], grid := getvariable('demo_grid'),
 spatial_axes := ['lat','lon'], axes := getvariable('demo_axes'), include_source := true);

SELECT region_id, valid_time, member, count(*) AS sample_count,
       count(value) AS valid_count, avg(value) AS mean_value,
       min(value) AS min_value, max(value) AS max_value, sum(value) AS sum_value
FROM region_samples GROUP BY region_id, valid_time, member
ORDER BY region_id, valid_time, member;
```

A、member0、00:00 应为 9 样本/8 有效、均值9.625、min0/max21/sum77。02:00 仍有9样本、0有效、所有数值聚合NULL。Empty 无原始行。此3区域演示共108条原始记录；完整10区域fixture由 validator 检查438条。

## 3. 显式补齐空区域—时间—成员组合

```sql
WITH timeline AS (
 SELECT TIMESTAMP '2026-01-01 00:00:00' + i * INTERVAL '1 hour' AS valid_time
 FROM range(3) AS t(i)
), members AS (SELECT i::BIGINT AS member FROM range(2) AS t(i)),
summary AS (
 SELECT region_id,valid_time,member,count(*) AS sample_count,
        count(value) AS valid_count,avg(value) AS mean_value
 FROM region_samples GROUP BY region_id,valid_time,member
)
SELECT r.region_id,t.valid_time,m.member,
       COALESCE(s.sample_count,0) AS sample_count,
       COALESCE(s.valid_count,0) AS valid_count,s.mean_value
FROM regions r CROSS JOIN timeline t CROSS JOIN members m
LEFT JOIN summary s USING(region_id,valid_time,member)
ORDER BY r.region_id,t.valid_time,m.member;
```

预期18行：Empty 的6组为0/0/NULL；A/B 的02:00组为9/0/NULL。先聚合再左连接，不以左连接占位行做 COUNT(*)。timeline 来自本例明确声明，不代表系统猜测文件时间。

## 4. 全量物化基准与双向差分

```sql
CREATE TEMP TABLE full_baseline AS
SELECT * FROM read_om('test/data/regions/regions.om',
 dimensions := ['member','time','lat','lon'],grid := getvariable('demo_grid'),
 spatial_axes := ['lat','lon'],axes := getvariable('demo_axes'),include_source := true);

CREATE TEMP TABLE expected AS
SELECT r.region_id,b.om_source.logical_index AS logical_index,
       b.value,b.lat,b.lon,b.valid_time,b.member
FROM full_baseline b CROSS JOIN regions r
WHERE ST_Covers(r.geom,ST_Point(b.lon,b.lat));
CREATE TEMP TABLE actual AS
SELECT region_id,om_source.logical_index AS logical_index,
       value,lat,lon,valid_time,member FROM region_samples;
SELECT count(*) FROM (SELECT * FROM expected EXCEPT ALL SELECT * FROM actual);
SELECT count(*) FROM (SELECT * FROM actual EXCEPT ALL SELECT * FROM expected);
```

两项均应为0；此 SQL 参考仍复用了 spatial，还须 validator 的人工/独立几何 oracle，不能只靠这一项判断 geometry kernel 正确。

## 5. 零值读取、错误和恢复

```sql
SELECT region_id,count(*) FROM region_samples GROUP BY region_id;
SELECT query_id,scan_id,metrics::JSON FROM duckomo_last_scan_metrics();
```

即使 view 声明全部列，只查询 region_id/count 时所有 value index/data/decode 应为0。增加 `WHERE value > 0` 后必须保留值依赖。另开未加载 spatial 的连接验证普通 read_om 可用、新入口拒绝；重复ID、NULL、非法洞、晚出现非法区域加 LIMIT、越过预算和接缝输入均应明确失败。完整取消/多连接/线程集由 validator/native tests 执行。

NaN 转为 SQL `NULL`，±Inf 保留；混合正负 Inf 时，SQL `sum`/`avg` 按 DuckDB 聚合行为得到 NaN。全 NULL 组的 `count(*)` 仍计原始记录，`count(value)` 为0，`sum`/`avg` 为 NULL。输入上限为4096区域、单 WKB 1 MiB、总 WKB 16 MiB、单区域65536顶点、全集合1048576顶点；超限拒绝且不截断。`grid_lonlat` 只是调用者声明坐标参考，不重投影或验证 datum；membership 使用平面 `covers`。取消在 WKB 遍历及每次 spatial 调用前后检查，单次 spatial 调用没有固定中断时限；下游 `LIMIT` 可让 SQL 成功但扫描终态为不完整。

## 6. 本地和版本矩阵验证

构建命令会生成版本隔离的 DuckDB/扩展，构建目标也会编译并运行当前16项 ASan/UBSan（13项既有检查、3项区域检查）。每个版本随后运行普通 SQL/native/Python 回归和 P0–P4；validator 会先核对 spatial 包 hash。按版本使用新的证据目录：

```bash
# 使用冻结 domain-manifest 中的对象和15份官方 CSV；缺输入时先取得并核对 hash。
export DUCKOMO_DOMAIN_FILE="$PWD/build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om"
export DUCKOMO_DOMAIN_REFERENCE="$PWD/build/evidence/spatial/reference"
for version in v1.5.4 v1.5.5 v1.5.6; do
  scripts/build-version.sh "$version" --region-sanitizers
  DUCKOMO_REGION_EVIDENCE_DIR="$PWD/build/evidence/regions/005/$version/P0-P4" \
    scripts/validate.sh --matrix test/data/grids/version-matrix.json \
      --pair "$version" --output-root build/official-matrix \
      --local-only --regions --region-gates P0,P1,P2,P3,P4
done
```

命令会创建新版本构建目录与证据目录，不覆盖既有 build ID。P0 验证 spatial adapter、无 spatial 的普通读取和区域绑定；P1 对照五个冻结真实对象的点和值；P2 对照60组独立统计 oracle；P3/P4 检查读取成本、独立扫描工作容量/RSS增长和生命周期，区域输入存储单列。缺匹配 spatial/fixture 返回2，实际失败返回1，所有请求 gate 通过返回0。`--region-sanitizers` 要求已取得并验证匹配 spatial runtime，构建阶段的 `duckomo_sanitizer_checks` 必须显示16/16通过。普通构建不要求该 runtime；`--extension-only` 仅构建扩展。

现行 P5 还需由授权的受控 HTTP/HTTPS/签名 S3 fixture 执行冷固定窗口基准/区域成对扫描。五目标的完整值列及时间维保持不变，窗口和两条SQL固定在 `test/data/regions/remote-windows.json`，不得使用 LIMIT 代替完整消费。普通窗口读取先物化，再 ST_Covers 筛出目标区域；结果须与区域入口逐行相同。执行前在当前 shell 安全设置 `AWS_ACCESS_KEY_ID`、`AWS_SECRET_ACCESS_KEY`、`DUCKOMO_AUTHORIZED_S3_BUCKET`、`DUCKOMO_S3_SERVICE_VERSION`、`DUCKOMO_S3_SERVICE_DIGEST`、`DUCKOMO_S3_TEST_ENDPOINT`，并准备 loopback HTTPS 测试证书；凭据不要写入证据目录或命令行。setup 会把冻结的五个对象上传到指定授权桶，并启动本地 HTTP/HTTPS 与 S3 审计代理：

```bash
VERSION=v1.5.4
FIXTURE_DIR="$PWD/build/regions-remote/005-p5-fixture"
scripts/setup-remote-fixtures.py \
  --region-manifest test/data/regions/real-targets.json \
  --s3-endpoint "$DUCKOMO_S3_TEST_ENDPOINT" \
  --s3-bucket "$DUCKOMO_AUTHORIZED_S3_BUCKET" \
  --authorized-s3-bucket "$DUCKOMO_AUTHORIZED_S3_BUCKET" \
  --s3-service-version "$DUCKOMO_S3_SERVICE_VERSION" \
  --s3-service-digest "$DUCKOMO_S3_SERVICE_DIGEST" \
  --httpfs "build/regions-runtime/$VERSION/official/httpfs.duckdb_extension" \
  --http-tls-cert /path/to/loopback.crt --http-tls-key /path/to/loopback.key \
  --http-tls-ca /path/to/loopback-ca.crt --output "$FIXTURE_DIR"
source "$FIXTURE_DIR/run.env"
export DUCKOMO_REMOTE_ENV="$FIXTURE_DIR/run.env"
```

每个版本使用匹配官方 HTTPFS 的生成配置。setup 的 `run.env` 和 `run-manifest.json` 记录 HTTPFS 路径；不能将 v1.5.4 的配置原样传给 v1.5.5/v1.5.6。复用本轮服务时，三个版本的私有配置位于 `build/regions-remote/005-completion-20261013/<version>/run.env`，服务日志位于 `build/regions-remote/005-completion-20261013/fixture/logs/`；传入相应 `--remote-env` / `--server-log`，或设置 `DUCKOMO_REMOTE_ENV` / `DUCKOMO_SERVER_LOG`。凭据只在权限600的 setup 文件中，不复制到证据。

随后对每个版本使用该 fixture、匹配版本的官方 HTTPFS 和全新的证据目录运行：

```bash
DUCKOMO_REGION_EVIDENCE_DIR="$PWD/build/evidence/regions/005/v1.5.4/P5" \
  scripts/validate.sh --matrix test/data/grids/version-matrix.json \
    --pair v1.5.4 --output-root build/official-matrix \
    --local-only --regions --region-gates P5
```

每个版本先记录本地完整回归（`--domain-file` 和 `--domain-reference` 使用该版本 P0–P4 的冻结输入；输出必须为空且与构建清单所在目录树分离）：

```bash
python3 scripts/record-region-regressions.py \
  --build-manifest build/official-matrix/v1.5.4/build-manifest.json \
  --spatial build/regions-runtime/v1.5.4/official/spatial.duckdb_extension \
  --domain-file "$DUCKOMO_DOMAIN_FILE" --domain-reference "$DUCKOMO_DOMAIN_REFERENCE" \
  --output build/evidence/regions/005/v1.5.4/regressions
```

记录器为普通回归绑定该版本的 CLI、DuckOMO 和 fixture tool，为 sanitizer 绑定隔离扩展；逐命令保存环境路径与依赖 hash。生命周期 sanitizer 的 JUnit 输出必须标识实际加载的隔离扩展，否则 P6 拒绝通过。旧记录缺少这些身份字段时需要重新生成。

对三个版本分别完成 P0–P4、完整回归和 P5 后，使用任一已构建版本的 validator 汇总，不复用历史或不同输入的 gate：

```bash
# 输出目录必须与证据输入树分离，且为空。
mkdir -p build/evidence/regions/005-P6-audit
build/official-matrix/v1.5.4/release/test/tools/duckomo_region_validation \
  --runtime-matrix test/data/regions/runtime-matrix.json \
  --evidence-root build/evidence/regions/005 \
  --output build/evidence/regions/005-P6-audit --gates P6 --root "$PWD"
```

P6 会重算三版本运行包、冻结输入、命令输出和 P5 原始服务日志的 SHA-256；缺少任一版本、gate 或来源时保持 `not-run`/失败。每个版本还必须在 `<version>/regressions/manifest.json` 中记录完整 SQL/native/Python 回归和 16 项 sanitizer 执行结果；P0–P4、回归和 P5 必须使用同一份当前源码对应的 DuckOMO 产物。缺少该记录时保持 `not-run`。大型 P5 报告设有64 MiB审计上限；服务请求逐行日志单独保存并用摘要 hash 核验，不把事件全文复制进汇总。

远程收益必须使用同一冻结对象、基准窗口和区域、相同投影及冷缓存条件完整消费结果；确认窗口基准/区域与 local/HTTP/HTTPS/S3 结果逐行一致，HTTP/S3 的 value bytes、decoded blocks 与服务发送量相对窗口基准严格下降。不执行全域远程扫描，不据此宣称全域吞吐或全域收益已验证。remote transport 为 NULL/false 正常，不能当作零字节证据。报告中应注明 S3 服务身份是否经过独立校验。

独立验证者至少选择 `ncep_gfswave025`，读取 `remote-windows.json` 中冻结的两条 SQL，只将对象 URI 从本地 OM 路径替换为授权受控 HTTPS fixture 的 `$DUCKOMO_HTTPS_BASE/ncep_gfswave025.om`。加载匹配 v1.5.4 的官方 HTTPFS/spatial/DuckOMO 和 `DUCKOMO_HTTPS_CA_CERT`；对新建的两个 CLI 进程分别输入 setup SQL（其中含 HTTPFS/DuckOMO/spatial LOAD、`SET ca_cert_file=...; SET enable_server_cert_verification=true;` 及 `SET enable_external_file_cache=false; SET enable_http_metadata_cache=false; SET enable_object_cache=false;`）。冷缓存设置要在同一 CLI 进程执行查询前生效。窗口/区域各由新的 CLI 进程完整执行，不加 LIMIT。

每条查询前 POST 到 `run-manifest.json` 的 `http_control`，JSON body 为 `{"state_key":"<unique>","state":"normal","object":"ncep_gfswave025.om"}`，并检查 HTTP 204。control 端点只绑定 loopback，不传凭据。随后为冻结 SQL 使用的 fixture URL 附加 `?state_key=<unique>&audit=<unique>`（保留所有查询参数）；从 `logs/http.jsonl` 仅筛选该唯一 audit ID 的 `event='complete'`，核对相应 request ID 同时有 started/complete 且成功，并累计 `body_bytes`。服务身份检查 `run-manifest.json` 中的 control/HTTP(S) loopback 端点以及 `service-pids.json` 登记的 PID 是否仍存活、命令行是否指向仓库内 `setup-remote-fixtures.py __http_server`。P5 摘要的全批量 audit 不能代替独立 P7 计数。

完整结果行与 `duckomo_last_scan_metrics()` 输出到权限受限的临时目录。核对 baseline 与 region 完整有序行逐行相同（包括 source_index、重复行和 NULL）、均为9行、终态 `status=success` / `scan_complete=true`；再核对 value data bytes、decoded chunks 和本次 audit ID 的服务发送 bytes 三项均严格下降。固定窗口字节/坐标范围应与冻结 JSON 完全一致，窗口只作成本基准，不改变 polygon 边界。安全读取生成的 private `run.env` 时可在 shell 中 source，但不要 `cat`、打印、复制或归档其内容、S3 setup SQL、访问凭据或签名 URL；服务请求与 URI 中不得带签名信息。完成后在报告记录准确版本、命令、输入及二进制非私密 hash、结果 hash、退出码和 P7 结论。

独立验证者最后在 `specs/005-polygon-region-statistics/evidence/quickstart-review.md` 记录环境、输入/产物hash、命令/退出码、上述判读与剩余问题。若验证者是 AI，必须明确标注其模型及 fresh 上下文，不冒充人类验收；实现者自测不签署 P7。
