# Quickstart: Phase 4–5 维度、官方远程与并行验证

当前接口采用官方 HTTPFS、`lat/lon` 和 metrics v4。执行边界以 [官方 HTTPFS 契约](../004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准；旧专用 ABI、observer、DuckOMO LRU、缓存设置/清理函数及旧 G5 已替换。原执行结果保留在 [历史 final](../../evidence/003-dimensions-remote-parallel/final.md)。当前交付状态见 [映射](../../evidence/003-dimensions-remote-parallel/current-delivery-20261021.md)，命令存在不代表门禁已经通过。

平台为 Linux AArch64，当前固定版本为 v1.5.4/v1.5.5/v1.5.6；各版本 CLI、HTTPFS、DuckOMO 必须匹配。查询不需要 Python；下文 Python 用于验证编排与保存证据。

## 准备匹配产物与受控服务

常规构建入口和官方 CLI/HTTPFS 下载、SHA256 校验见 [官方流程](../../docs/official-httpfs.md)。每个构建的实际目录以 build manifest 的 `release_dir` 为准；不要把不同版本或新旧源码产物混用。

```bash
scripts/build-version.sh v1.5.4
python3 scripts/version_matrix.py fetch-runtime --root . \
  --matrix test/data/grids/version-matrix.json --pair v1.5.4
```

以下使用三个环境变量指定已校验的匹配产物；可替换为独立验证包提供的绝对路径。独立验证者须自行记录这些文件和源码身份的 SHA256。重新链接的局部构建须明确说明其构建范围。

```bash
DUCKOMO_CLI="$PWD/build/official-matrix/v1.5.4/official/duckdb"
DUCKOMO_HTTPFS="$PWD/build/official-matrix/v1.5.4/official/httpfs.duckdb_extension"
DUCKOMO_EXTENSION='/absolute/path/from/build-manifest/duckomo.duckdb_extension'
"$DUCKOMO_CLI" -version
sha256sum "$DUCKOMO_CLI" "$DUCKOMO_HTTPFS" "$DUCKOMO_EXTENSION"
```

性能 fixture 为 `test/data/dimensions_perf.om`，SHA256 `47a803a4769ab5a8c6622f9be5cc05b53b96152443917b7603d78dab94e1ec13`。真实 NCEP 样本由 `test/data/domain-manifest.json` 固定。对象须在扫描期间稳定。

受控服务使用 `scripts/setup-remote-fixtures.py`，要求调用者已准备可签名 S3 测试服务、被授权的测试 bucket、私有环境中的凭据及 loopback TLS 证书/私钥。`--help` 列出全部参数；须传 `--authorized-s3-bucket`、固定服务 version/digest、`--http-tls-cert/--http-tls-key` 和匹配官方 `--httpfs`。`--real-file/--real-manifest` 会校验真实样本身份；`--grid-manifest` 可同时发布冻结网格对象。工具仅创建 loopback HTTP/HTTPS 和审计代理，不创建外部云基础设施。私钥、secret SQL、run.env 保留在忽略目录，不复制到发布证据。

```bash
# 使用 setup 工具实际生成的私有配置路径。
source /absolute/path/to/remote/run.env
```

配置包含 `DUCKOMO_HTTP_BASE`、`DUCKOMO_HTTPS_BASE`、`DUCKOMO_S3_BASE`、`DUCKOMO_S3_SETUP`、`DUCKOMO_SERVER_LOG`、`DUCKOMO_HTTPS_CA_CERT` 和真实样本路径。对 loopback 验证进程清除继承的 HTTP_PROXY/HTTPS_PROXY/ALL_PROXY，避免官方 HTTPFS 将本地请求发给外部代理；不为外部端点无条件移除代理。

## 1. 维度映射与混合筛选

用匹配 CLI 执行以下 SQL，先加载所校验的 DuckOMO 绝对路径：

```sql
LOAD '/absolute/path/to/duckomo.duckdb_extension';
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
```

预期 schema 为 value FLOAT、valid_time TIMESTAMP、member BIGINT；混合查询精确返回 `(4,2026-09-30 01:00:00,20)`；双向差分零行；最后 count=0。每三个连续原始值共享一个时刻，成员依次为 10/20/30，故可独立核对。最后扫描的 `reads.value_totals.index_bytes/data_bytes/decoded_chunks` 都为 0，允许 bind 元数据。JSON 解析可在验证进程完成，不要求查询安装 JSON 扩展。

五轴、布局、无关变量、投影/过滤依赖及零值读取还应通过匹配构建的 `duckomo_dimensions_validation` 和 SQL/native 检查；保存 exact argv、结果及对应产物 hash。

## 2. 官方远程局部读取与故障恢复

以下验证入口使用官方 CLI/HTTPFS，实际完整消费本地/HTTP/HTTPS/签名 S3 结果，比较 hash，运行 1/2/4 worker、冷局部/同列全扫、重复查询、LIMIT、403/404/短读/忽略 Range/可观察版本变化及失败后恢复。`--real-file` 增加固定真实对象的四来源完整对照。输出必须是本次独立的新目录。

```bash
python3 scripts/validate-official-httpfs.py \
  --duckdb "$DUCKOMO_CLI" --httpfs "$DUCKOMO_HTTPFS" \
  --extension "$DUCKOMO_EXTENSION" \
  --real-file "$DUCKOMO_REAL_FILE" \
  --output build/independent-003/runtime
```

使用默认 `--workers 1,2,4` 时，上述命令预期 `summary.json` 为 pass，包含 52 条实际 case（不传 `--real-file` 时为 48 条）；固定真实结果跨来源完全一致。HTTP/S3 冷局部服务端发送量严格小于同列全扫。服务端日志是已发送字节，不能称客户端接收字节；远程 `transport.response_body_bytes/attempts/responses` 为 null、complete=false，本地为 0/true。应用读取字节、解码块、任务/终态与自有内存仍实际计量。未知或不完整不能补零或计收益。

连接取消、恢复、同进程多连接隔离由匹配且包含当前源码的 `official_httpfs_test` 加载官方包验证，需要 `DUCKOMO_HTTPFS` 和 `DUCKOMO_HTTP_BASE`。保留实际 QueryEnd cancelled/failure/success 证据；不能用终止 CLI 信号代替连接取消。访问变化用例见 [access-change.py](../004-multi-grid-selection/evidence/official-httpfs-refactor/access-change.py)，须独占本次服务控制入口运行并恢复 normal 状态。

不要求或调用 `httpfs_om_range_capabilities()`。普通官方 HTTPFS 使用标准扩展版本/平台校验；删除的 DuckOMO 缓存设置和函数应明确报未知设置/函数。

## 3. 并行比较

对固定 `dimensions_perf.om`，分别限制 DuckDB threads 和 `duckomo_max_threads` 为 1/2/4，完整消费相同查询并保存结果与 `legacy_v3.tasks`/worker 字段。无 ORDER BY 时不承诺行序，应按多重集合比较。官方 runtime 已覆盖本地和远端 1/2/4 正确性；native/SQL 还覆盖任务少于 worker、跨 batch、取消与失败恢复。

SC-005 要求至少一个预先选定的代表样本各测 5 次串行与并行，保存全部耗时、进程 RSS、实际 active workers 和中位数，并行中位数须更低。固定原 `dimensions_perf.om` 与 sum(value) 查询可用于该比较；明确计时范围、OS 缓存条件和完整消费方式。未新增“远程五轮必须更快”的条件；远端正确性和本地五轮收益各自举证。启动开销不能冒充扫描耗时。

```sql
SET threads=4;
SET duckomo_max_threads=1;
-- 完整消费固定值查询并保存 metrics；随后同条件切换上限重跑。
SET duckomo_max_threads=4;
```

## 4. 冷/重复查询与未知指标

同一连接内完整执行固定查询两次，核对结果相等，分别记录 elapsed、应用读取、解码及服务端发送成本。官方 runtime 的 `repeated-<source>-0/1` 保存这两次结果和指标；不能每条查询新启动 CLI 冒充同连接重复查询。

DuckOMO 自有缓存已删除；不调用 `duckomo_cache_enabled`、`duckomo_cache_capacity` 或 `duckomo_clear_cache()`，不要求已撤销的容量/淘汰/热缓存收益。标准 DIRECT_IO 路径可能重复读取，性能变化据实公开。官方缓存的内部内存、即时撤权、新鲜探测和逐响应快照不在承诺范围。可观察打开身份冲突和标准访问错误须明确失败，恢复后有效查询应成功。

进程 RSS 与 DuckOMO 自有容量分别记录；HTTPFS/引擎内部分配不纳入虚假自有总量。LIMIT 的 SQL success 可以伴随 scan_complete=false，不作为完整成本验收。

## 独立验收与收尾

未参与实现的验证者逐项执行上述四项，核对预期结果并保存：身份与环境、源码/实际产物/输入/服务 manifest 的 hash、完整 SQL/argv、原始输出、退出码、结果/oracle hash、逐 scan v4 及适用服务日志配对。独立 AI 允许，但须明确模型/新上下文且非人工验收。实现者可准备服务与指南，不能代签独立执行。

失败、缺输入或未执行均记录为未通过；不得仅因 exit 0、文件存在、复用旧摘要或 task checkbox 而通过。R21 需要独立复现包；R22 将当前证据映射到现行 FR/SC 后才收口。004 逐网格 H0–H9 继续由其自己的验收负责。

仅在所有验证者完成后停止本次创建的 loopback 服务，保留证据和后端对象：

```bash
python3 scripts/setup-remote-fixtures.py --stop --output /absolute/path/to/remote
```
