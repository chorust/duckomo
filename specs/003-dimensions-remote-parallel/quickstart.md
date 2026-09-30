# Quickstart: Phase 4–5 验证指南

状态：拟实现功能的验收步骤，不是当前版本已可执行的功能说明。新增 axes、设置、指标函数、httpfs 配套能力及 validation 目标须在后续实施完成。本指南不包含实现代码。所有命令从仓库根目录执行，Linux AArch64 为验收平台。

## 前提和构建

需要 C11/C++17、CMake/Make、Git、Python 3、jq、sha256sum，以及配套 httpfs 的 TLS/HTTP 依赖。准备受控 HTTP range 服务和 S3-compatible 测试服务，两个来源均提供与本地 fixtures 完全相同的字节，开启本次运行专用审计日志；远程协议要求见 [remote-io](contracts/remote-io.md)。服务版本/digest、测试桶、endpoint、region 与 TLS 设置写入运行 manifest。

实施阶段应交付 `scripts/setup-remote-fixtures.py`，命令契约如下。它负责启动只绑定 loopback 的 range 服务、把 fixtures 上传到已经准备好的测试 S3 服务、启动 S3 响应审计代理，并生成 run.env、s3-setup.sql 和服务日志目录；S3 密钥从当前环境读取，不写入 evidence。它不是创建外部云基础设施的脚本。服务端二进制/容器版本由实施时的测试依赖锁文件固定。

```sh
git submodule update --init --recursive
make release
make test
make sanitizer-test
python3 scripts/setup-remote-fixtures.py \
  --fixtures test/data --real-file "$DUCKOMO_REAL_FILE" \
  --s3-endpoint "$DUCKOMO_TEST_S3_ENDPOINT" \
  --s3-bucket "$DUCKOMO_TEST_S3_BUCKET" \
  --output build/remote-fixtures
. build/remote-fixtures/run.env
```

DUCKOMO_REAL_FILE 指向 domain-manifest 里的固定真实 OM；脚本启动前校验 hash。run.env 输出 `DUCKOMO_HTTP_BASE`、`DUCKOMO_S3_BASE`、`DUCKOMO_S3_SETUP`、`DUCKOMO_SERVER_LOG`、`DUCKOMO_HTTPFS` 和 `DUCKOMO_REAL_MANIFEST` 的 shell 安全引用值。DUCKOMO_HTTPFS 是本次配套构建的扩展绝对路径；不要 INSTALL 任意版本替代。

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

预期 ABI=1、upstream_commit=c3f215ab360f04dc3d3d5305fa81849c0121f111，patch_revision 与本次 manifest 一致。普通未适配 httpfs 必须被 read_om 的远程绑定明确拒绝。

完整比较包含授权 S3、HTTP 范围、真实样本与协议负例：

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

预期三类来源结果完全一致；HTTP/S3 两者冷局部查询 response body 严格下降，与服务器日志核对。200 回退、短读、错误范围、权限和版本变化全部失败而不返回成功的残缺结果，错误后有效查询能继续。summary 缺任一来源或真实样本则命令非零退出。

## 3. 并行比较

harness 的 G4 对完整 perf 样本以 1、2、4 上限运行本地/HTTP/S3。手动控制形式：

```sql
SET threads=4;
SET duckomo_max_threads=1;
-- 完整消费值查询并保存 metrics；随后切换上限重跑同一查询。
SET duckomo_max_threads=4;
```

预期有足够任务时 active_workers 至少为 2；全部逻辑位置恰好覆盖一次。无 ORDER BY 时按多重集合比较，不按到达顺序比较。固定 release 样本串行/并行各 5 次完整运行的中位数应下降，全部原始耗时、缓存条件和 RSS scope 都被保存；只有启动多个 local state 不能判定为并行通过。

## 4. 冷热缓存比较

缓存必须在同一个连接内验证，不能每条查询重新启动 CLI。远程 harness 同会话执行下列控制序列，并在每次扫描后调用 last_scan_metrics：

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

完整可执行值查询及 URI 由 harness 的逐场景 SQL 文件输出（敏感输入脱敏）。预期稳定强版本的 warm body 或请求数严格下降，结果相同，仍有新鲜 HEAD 和一字节范围授权探测；charged_bytes 不越容量。替换内容、撤销权限、改为无版本响应时分别验证失效、拒绝访问及跨查询缓存禁用。极小容量/单条超预算仍正确完成。

## 证据和收尾

检查两个输出目录的 summary.json、逐查询 v3 JSON、server comparison、结果差分与 manifests；独立验证者按四项任务复现后记录命令和退出码。G0–G7 全通过才更新路线图，失败或服务缺失必须保留为未通过，不用文档检查代替执行结果。

```sh
python3 scripts/setup-remote-fixtures.py --stop --output build/remote-fixtures
```

stop 仅关闭本脚本启动的 loopback 服务/代理；不删除真实样本或外部测试桶。
