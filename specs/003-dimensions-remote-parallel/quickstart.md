> 2026-10-08：当前未发布版本使用 [官方 HTTPFS 迁移契约](../004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)。下文专用 ABI、LRU 或 observer 的条款/命令为历史约定，已被本次契约替换。历史证据状态保持，当前复现请见 [官方流程](../../docs/official-httpfs.md)。

# Quickstart: Phase 4–5 验证指南

状态：本地维度、并行、缓存和 v3 指标入口已实现；G3–G6 的自动验收程序也已实现，但完整门禁必须在固定 S3-compatible 服务和真实 OM 样本可用时运行。G7 还需要未参与实现者按本指南独立复现。所有命令从仓库根目录执行，Linux AArch64 为验收平台；代码已实现不代表对应外部门禁通过。

## 前提和构建

需要 C11/C++17、CMake/Make、Git、Python 3、jq、sha256sum，以及配套 httpfs 的 TLS/HTTP 依赖。准备受控 HTTP range 服务和固定版本的 S3-compatible 测试服务，两个来源均提供与本地 fixtures 完全相同的字节，开启本次运行专用审计日志；远程协议要求见 [remote-io](contracts/remote-io.md)。服务版本/digest、测试桶、endpoint、region 与 TLS 设置写入运行 manifest。设置 `DUCKOMO_S3_SERVICE_VERSION` 为服务端固定版本标识，并通过环境提供 S3 凭据；临时凭据可同时设置 `AWS_SESSION_TOKEN`。

构建身份由 DuckDB v1.5.4 commit `08e34c447bae34eaee3723cac61f2878b6bdf787`、OM C commit `d8855e418e2231ae8439f0c7e840fa3f93b371e3`、extension-ci-tools commit `b777c70d30942cca5bef62d6d4fa23a13362f398` 固定。配套 httpfs 使用上游 commit `c3f215ab360f04dc3d3d5305fa81849c0121f111` 和 `duckomo-httpfs-range-v2`；补丁 SHA-256 与共享 ABI 头 SHA-256 见 [`third_party/httpfs-patches/manifest.json`](../../third_party/httpfs-patches/manifest.json)。性能 fixture `dimensions_perf.om` SHA-256 为 `47a803a4769ab5a8c6622f9be5cc05b53b96152443917b7603d78dab94e1ec13`。固定真实样本身份见 `test/data/domain-manifest.json`；没有该文件时真实样本 gate 不通过。

加载扩展时，CLI 和两个扩展必须来自同一构建目录、使用同一 DuckDB 版本。构建完成后先检查 CLI：

```sh
./build/release/duckdb -version
# 若使用 vcpkg 构建，则检查并使用这一套：
./build/release-vcpkg/duckdb -version
```

预期版本为 `v1.5.4 (Variegata)`。后续命令里的 `build/release/...` 或 `build/release-vcpkg/...` 必须成套替换；不要用 PATH 中的 `duckdb` 替代构建目录里的 CLI。用 v1.5.5 CLI 加载 v1.5.4 扩展会因 DuckDB 版本不匹配而失败。

`scripts/setup-remote-fixtures.py` 启动只绑定 loopback 的 HTTP range 服务和 S3 响应审计代理，把 fixtures 上传到已经准备好的测试 S3 服务，并生成 `run.env`、`s3-setup.sql` 和服务日志目录。S3 密钥从当前环境读取，不写入 evidence；生成的 `run.env` 与含 secret 的 SQL 文件权限为当前用户可读写。脚本不会创建外部云基础设施。服务版本由显式 `--s3-service-version` 写入运行 manifest。

```sh
git submodule update --init --recursive
make release
make test
make sanitizer-test
DUCKOMO_REAL_FILE="$(jq -r '.sample.path' test/data/domain-manifest.json)"
python3 scripts/setup-remote-fixtures.py \
  --fixtures test/data --real-file "$DUCKOMO_REAL_FILE" \
  --s3-endpoint "$DUCKOMO_TEST_S3_ENDPOINT" \
  --s3-bucket "$DUCKOMO_TEST_S3_BUCKET" \
  --s3-service-version "$DUCKOMO_S3_SERVICE_VERSION" \
  --output build/remote-fixtures
. build/remote-fixtures/run.env
```

DUCKOMO_REAL_FILE 指向 domain-manifest 里的固定真实 OM；脚本启动前校验 hash。run.env 输出 `DUCKOMO_HTTP_BASE`、`DUCKOMO_S3_BASE`、`DUCKOMO_S3_SETUP`、`DUCKOMO_SERVER_LOG`、`DUCKOMO_HTTPFS`、`DUCKOMO_REAL_FILE` 和 `DUCKOMO_REAL_MANIFEST` 的 shell 安全引用值。HTTP range 服务和 S3 审计代理只绑定 loopback；S3 上传直连测试服务，DuckDB 对审计代理使用 HTTP，代理按需用 TLS 连接上游。DUCKOMO_HTTPFS 是本次配套构建的扩展绝对路径；不要 INSTALL 任意版本替代。

## 1. 维度映射与混合筛选

```sh
./build/release/duckdb -unsigned :memory: <<'SQL'
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
CREATE VIEW dimension_demo AS
SELECT * FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['t','ensemble']]),
  axes := {
    'time': {'axis':'t', 'start':TIMESTAMP '2026-09-30 00:00:00',
             'step':INTERVAL '1 hour'},
    'member': {'axis':'ensemble', 'values':[10,20,30]}
  });
DESCRIBE SELECT * FROM dimension_demo;
CREATE TEMP TABLE baseline AS SELECT * FROM dimension_demo;
SELECT * FROM dimension_demo
WHERE valid_time=TIMESTAMP '2026-09-30 01:00:00' AND member=20;
SELECT * FROM (
  (SELECT * FROM dimension_demo WHERE member=20
   EXCEPT ALL SELECT * FROM baseline WHERE member=20)
  UNION ALL
  (SELECT * FROM baseline WHERE member=20
   EXCEPT ALL SELECT * FROM dimension_demo WHERE member=20)
);
SELECT count(*) FROM dimension_demo WHERE member=99;
SELECT * FROM duckomo_last_scan_metrics();
SQL
```

预期列为 value FLOAT、valid_time TIMESTAMP、member BIGINT；混合查询返回 value=4、有效时刻 01:00、member=20；双向差分零行；最后 count=0。原始独立参考为每三个连续值共享一个时刻，成员依次为 10、20、30。最后一条含 read_om 查询的 v3 指标为零值 index/data/decode，但允许 bind 元数据。不要用这个小样本验证成本收益。

其余三类维度及组合/布局边界由完整 harness 验证：

```sh
./build/release/test/tools/duckomo_dimensions_validation \
  --root "$PWD" --fixtures "$PWD/test/data" \
  --output "$PWD/build/evidence/dimensions" \
  --duckdb "$PWD/build/release/duckdb" \
  --extension "$PWD/build/release/extension/duckomo/duckomo.duckdb_extension"
```

预期 summary 所有本地维度 gate 通过，固定局部样本的 data_bytes 与 decoded_chunks 均严格小于 full，无关变量及坐标/count/empty 场景符合零值 I/O。具体 fixtures、查询及参考要求见 [观测契约](contracts/validation-evidence.md)。

## 2. 远程局部读取

先核对配套能力；下面是常规默认输出路径，若构建输出不同使用 run.env 中 DUCKOMO_HTTPFS 的路径加载。

```sql
LOAD 'build/release/extension/httpfs/httpfs.duckdb_extension';
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
SELECT * FROM httpfs_om_range_capabilities();
```

预期 ABI=2、upstream_commit=c3f215ab360f04dc3d3d5305fa81849c0121f111，patch_revision 与本次 manifest 一致。普通未适配 httpfs 必须被 read_om 的远程绑定明确拒绝。

G3 harness 比较固定 synthetic fixture 和固定真实 OM 的本地/HTTP/S3 完整结果，核对 HTTP 与 S3 response body，并注入 HTTP 403、404、无 HEAD、忽略范围、错误范围、短读、超时和版本替换，随后验证新的有效 HTTP 查询可以恢复：

```sh
./build/release/test/tools/duckomo_remote_validation \
  --root "$PWD" --fixtures "$PWD/test/data" \
  --output "$PWD/build/evidence/remote" \
  --duckdb "$PWD/build/release/duckdb" \
  --extension "$PWD/build/release/extension/duckomo/duckomo.duckdb_extension" \
  --httpfs "$DUCKOMO_HTTPFS" \
  --http-base "$DUCKOMO_HTTP_BASE" --s3-base "$DUCKOMO_S3_BASE" \
  --s3-setup "$DUCKOMO_S3_SETUP" --server-log "$DUCKOMO_SERVER_LOG" \
  --real-file "$DUCKOMO_REAL_FILE" --real-manifest "$DUCKOMO_REAL_MANIFEST"
```

预期三类来源结果完全一致，HTTP/S3 body 与审计日志一致；HTTP/S3 冷局部查询的 body 应小于同源 full scan。故障查询必须非零退出，之后有效 HTTP 查询应恢复。缺任一来源、真实样本或审计日志时命令非零退出。这个 harness 会连续执行 G3–G6：G3 写入三来源结果差分和 HTTP 故障恢复证据；G4 写入 local/HTTP/S3 的 1/2/4 worker 五次完整结果、耗时及独立进程 RSS；G5 写入同连接冷/热/禁用/清理/小容量、弱版本、等长替换和撤权结果；G6 写入 HTTP 并发、取消、失败成本和恢复证据。输出目录必须是新建空目录。任何 gate 条件不满足时 harness 返回非零。

### 公共 Open-Meteo 对象的只读冒烟检查

`setup-remote-fixtures.py` 向**自有的测试 S3 桶**写入合成 OM fixtures，目的是让后续查询有固定、可审计、可控的对象；它不验证 DuckOMO 上传能力，产品接口仍是只读 `read_om`。如只想确认公开真实样本的匿名 HTTPS 读取，可以跳过 fixture setup，执行下面的补充检查：

```sh
./build/release-vcpkg/duckdb -unsigned :memory: <<'SQL'
LOAD 'build/release-vcpkg/extension/httpfs/httpfs.duckdb_extension';
LOAD 'build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension';
WITH local_rows AS MATERIALIZED (
  SELECT latitude, longitude, wave_height
  FROM read_om('build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om',
               domain := 'ncep_gfswave025')
), public_rows AS MATERIALIZED (
  SELECT latitude, longitude, wave_height
  FROM read_om('https://openmeteo.s3.amazonaws.com/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om',
               domain := 'ncep_gfswave025')
)
SELECT COUNT(*) AS differing_rows FROM (
  (SELECT * FROM local_rows EXCEPT ALL SELECT * FROM public_rows)
  UNION ALL
  (SELECT * FROM public_rows EXCEPT ALL SELECT * FROM local_rows)
);
SELECT json_extract_string(metrics, '$.status') AS status,
       json_extract_string(metrics, '$.scan_complete') AS scan_complete,
       json_extract_string(metrics, '$.response_body_bytes') AS response_body_bytes
FROM duckomo_last_scan_metrics();
SQL
```

预期 `differing_rows=0`，两个 scan 均为 success 且完整。此项只验证当前公开 HTTPS 对象和单个变量的本地/远端结果，不是 `s3://`、合成 fixtures、独立服务端审计或 G3–G6 通过证据。公开对象可能按上游保留策略删除；需要稳定复现时仍使用 manifest 固定的本地字节和受控测试服务。

## 3. 并行比较

本地并行生命周期由 `parallel_scan_test` 和 `test/sql/parallel_scan.test` 覆盖线程上限、完整结果多重集合、取消、错误及恢复。上面的远程 harness 会对本地、HTTP、S3 各执行 1/2/4 worker 限制，每种各五次；所有运行关闭扩展缓存、完整消费固定性能 fixture，并保存全部耗时、独立进程 RSS 和 `tasks.max_active_workers`。每隔一轮反转 1/2/4 的测量顺序，并将顺序写入 `g4-performance.json`，减少固定先后次序造成的系统缓存偏差。结果有遗漏/重复、worker 不足或 2/4 worker 的中位数均未低于串行时，G4 失败。单独执行 G3–G6 命令即可重现，不要用本地性能结果替代远程 G4。

```sql
SET threads=4;
SET duckomo_max_threads=1;
-- 完整消费值查询并保存 metrics；随后切换上限重跑同一查询。
SET duckomo_max_threads=4;
```

查询的 `tasks.max_active_workers` 和 `tasks.active_workers` 可帮助检查参与者；完整 G4 仍须对 1/2/4 结果作多重集合比较并保存五次耗时与独立进程 RSS。仅观察到多个 local state 不足以判定并行通过。

## 4. 冷热缓存比较

缓存必须在同一个连接内验证，不能每条查询重新启动 CLI。远程 harness 使用交互式 DuckDB 连接检查 HTTP/S3 冷热和禁用查询，清理后确认最近一次 metrics 快照不变，小容量场景核对实际淘汰和容量；HTTP 场景还以同 URI 的等长内容替换和 403 撤权验证旧缓存不可用，并在恢复访问后再次查询。G6 另检查两个并发查询各自的服务端 body 与 v3 profile、超时请求的取消状态及后续查询恢复。所有这些都要以 harness 保存的逐查询 JSON 和服务端 JSONL 为证据，不能只看 SQL 结果。

```sql
SET duckomo_max_threads=1;
SET duckomo_cache_enabled=true;
SET duckomo_cache_capacity=67108864;
CALL duckomo_clear_cache();
-- 执行并完整消费固定远程值查询：cold。
-- 在同连接再次完整执行同一查询：warm。
SELECT * FROM duckomo_last_scan_metrics();
CALL duckomo_clear_cache();
SET duckomo_cache_enabled=false;
-- 再次执行同一查询：disabled。
```

自动 G5/G6 gate 仍要求真实服务运行结果；缺少 S3 服务或真实样本时不可将该 gate 标成通过。HTTP 弱 ETag 场景可验证不得跨查询复用；固定 S3 服务也必须在 manifest 中记录服务版本，并核对服务端 body 与每个 scan 的 v3 数据。

## 证据和收尾

检查两个输出目录的 summary.json、逐查询 v3 JSON、server comparison、结果差分与 manifests；独立验证者按四项任务复现后记录命令和退出码。G0–G7 全通过才更新路线图，失败或服务缺失必须保留为未通过，不用文档检查代替执行结果。

独立复现者请分别记录四项结果：①本地构建、SQL/native/sanitizer 和 G0–G2；②配套 httpfs 能力检查、G3 三来源完整结果及故障恢复；③G4 local/HTTP/S3 的 1/2/4 worker 结果、耗时和 RSS；④G5/G6 同连接缓存、版本/撤权、并发/取消和逐查询服务端对账。对每项保存实际命令、退出码、build/fixture/service 身份和 evidence 路径。尚未提供的服务或固定真实样本写为“未通过（未执行）”，不可推定成功。

```sh
python3 scripts/setup-remote-fixtures.py --stop --output build/remote-fixtures
```

stop 仅关闭本脚本启动的 loopback 服务/代理；不删除真实样本或外部测试桶。
