# Quickstart: 验证 2.0 支持、请求合并与异步 I/O

前提：Linux AArch64；子模块已初始化；受控远程服务按 `scripts/setup-remote-fixtures.py --help` 启动并生成私有 `run.env`（凭据不入日志）。官方运行时由 `scripts/version_matrix.py fetch-runtime` 获取并校验 SHA256。

## 1. 2.0 构建与回归（US1）

```bash
scripts/build-version.sh --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0
python3 scripts/version_matrix.py fetch-runtime --root . \
  --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0
DUCKOMO_SPATIAL_EXTENSION=<官方 2.0 spatial> build/official-matrix/v2.0.0/release/test/unittest 'test/sql/*'
source <run.env> && scripts/validate.sh --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0
```

期望：SQL 全部通过；`validate-official-httpfs.py` 48 项 pass；`official_httpfs_test` 打印 passed；sanitizer 目标通过。对 v1.5.4/v1.5.5/v1.5.6 重复同样步骤（使用 `test/data/grids/version-matrix.json`），结果一致。

## 2. CI 与发布（US1）

- 在分支上 `workflow_dispatch` 运行 `release.yml`（dry-run tag `v0.0.0-dry-run`）：期望 16 个产物全部通过 verify，未发布。
- 提交 PR 触发 `community-build.yml`：四个版本 linux_arm64 构建与 runtime 检查全绿。
- 发布契约见 [contracts/release.md](contracts/release.md)。

## 3. 请求合并（US2）

```bash
python3 scripts/benchmark-remote-io.py \
  --runtime 'v2.0.0=<official-duckdb>|<duckomo-extension>' \
  --httpfs 'v2.0.0=<official-httpfs>' --run-env <run.env> \
  --object 'o1280=<HTTP-or-S3-object-URI>' \
  --query-file <queries.json> --queries full,selected \
  --modes merge-off,merge-on --workers 1,4 --repeats 3 \
  --output build/remote-io/merge.json
```

`queries.json` 将查询名称映射到带 `{object}` 占位符的 SQL。O1280 空间选择需显式 `domain := 'ecmwf_ifs'` 才有 lat/lon 列，例如 `SELECT value FROM read_om('{object}', domain := 'ecmwf_ifs') WHERE lat BETWEEN 39.8 AND 40.2 AND lon BETWEEN 116.2 AND 116.6 ORDER BY value`。值 residual 过滤不能代替局部空间读取。S3 凭据由私有 run.env 引用的 `DUCKOMO_S3_SETUP` 文件加载。

runner 的 workers 设置 `duckomo_max_threads`；executor 保留至少 4 个线程供 AsyncResult 执行，并记录 process peak RSS 与扩展 SHA256。性能比较应停止并行编译等竞争负载后按相同配置复测。

期望（O1280 全扫，单 worker，受控 S3）：
- `merge=off` 复现基线约 11,592 次请求 / 4,125,977 字节。
- `merge=on` 请求数 ≤ 1,160（−90%），发送字节 ≤ 4,538,575（110%），耗时 ≤ 4.75 s（−70%）。
- 局部选择发送字节 ≤ 基线 125%，且小于全扫。
- 所有模式结果 SHA256 相同；`remote_io` 指标字段见 [contracts/metrics.md](contracts/metrics.md)。
- `SET duckomo_remote_merge=false` 后请求模式与基线一致。

## 4. 异步 I/O（US3，仅 2.0）

```bash
python3 scripts/benchmark-remote-io.py <同上 runtime/httpfs/run-env/object 参数> \
  --modes async-off,async-on --query-file <queries.json> \
  --queries full,selected,coordinates,polygon,nearest \
  --workers 1,4 --repeats 3 --output build/remote-io/async.json
# 对同一组合另加 --memory-limit 256MB，写入独立输出文件。
# 实验跨批窗口另加 --prefetch-batches 4，写入独立文件；默认1关闭。
# 当前固定样本中该窗口增加请求并变慢，不预期所有模式加速。
```

期望：
- `async=on` 时 `remote_io.inflight_peak > 1`，耗时比 `async=off` 至少低 20%，结果一致。
- `SET memory_limit='256MB'` 下全扫不 OOM，峰值 RSS ≤ 关闭异步时 1.5 倍。
- 取消：`official_httpfs_test` 与 `validate-official-httpfs.py` 的取消/恢复用例在 `async=on` 下通过，取消后 1 秒内结束，指标 `cancelled`，无残留内存账本。
- 1.5.x 上同一命令 `async_applied=false`，结果一致。

## 5. 本地无回退

`scripts/benchmark-engine-versions.py` 与真实对象本地模式：中位数相对当前版本差异 ≤ 5%。

## 6. 真实云端点（FR-018）

使用 `test/data/grids/sample-manifest.json` 中公开 Open-Meteo 对象的 `s3_uri`/`https_url`，匿名只读，记录首读与重复读、请求数与耗时。报告必须注明端点区域与客户端位置，不与 loopback 结果混算。
