> 2026-10-08：当前未发布版本使用 [官方 HTTPFS 迁移契约](evidence/official-httpfs-refactor/contract.md)。下文专用 ABI、LRU 或 observer 的条款/命令为历史约定，已被本次契约替换。历史证据状态保持，当前复现请见 [官方流程](../../docs/official-httpfs.md)。

> 本页含历史阶段的构建/复现示例。当前开发版地理列已改为 `lat/lon`，并支持共享轴 `dimensions := [轴名...]`；旧 `latitude/longitude` 引用需迁移，详见 [接口修订](../../docs/interface-migration.md)。不改写历史验证结果。

# Quickstart: 多网格验证指南

日期：2026-10-03；2026-10-08 核对实现状态。version=1 grid/source/info、registry/sample-query generator、sample view binding audit、两套固定 matrix build 和 H0–H8 harness 已存在；H8 配套身份/证据汇总器已实现并按当前矩阵运行。匹配 baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` 上，H1 可用的三类投影坐标/值子比较通过；full H1 仍 not-run。真实独立 Gaussian/N-grid 参考、远程 fixture/审计及多个 gate case 仍未完成。以下完整命令是 gate 的复现接口，不代表它们当前可全部通过；缺输入/未实现用例必须返回 not-run/failure。所有命令从仓库根执行，Linux AArch64 为本期验收平台；状态见 [validation-evidence](contracts/validation-evidence.md) 与 [逐网格证据表](../../docs/grid-domains.md)。

## 前提与固定构建

需要C11/C++17、CMake/Make、Git、Python3、jq、sha256sum、配套TLS/HTTP构建依赖及两套独立构建目录。先按manifest取得/冻结真实原生OM v3和参考；公开对象可能删除，不能把调查候选URL写成永远可用的下载源。当前三类投影坐标参考采用 `scripts/generate-grid-coordinate-reference.py --method pinned_producer_coordinates`，逐步模拟固定 Open-Meteo commit 的 Float32 投影运算；默认 `independent_math` 输出保留为诊断，在接近投影奇点时不能代表 producer Float32 输出。具体命令、输入和产物 hash 见 [`pinned-f32-coordinate-refresh-20261008`](evidence/development-local/pinned-f32-coordinate-refresh-20261008.md)。匹配 baseline build `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` 的 H1 部分结果见 [H1 evidence](evidence/baseline-local/h1-20261008-5dac993/)：2,843,101 个坐标点及 329,401,680 个官方值位置通过。比较使用预期 `[ny,nx,ntime]` profile 和合成 `valid_time` 只选择 time index 0；它仍不证明 OM 对象轴到 producer 点序的映射。所有完整样本、定义、oracle、坐标容差和版本身份在测量前确定。缺真实 Gaussian 类型、对象轴映射或区域点序证据时，H0/H1及完整交付保持not-run。

registry 校验、版本矩阵和 sanitizer 的命令形式：

矩阵的固定 `arm64-linux` triplet 要求设置 `DUCKOMO_VCPKG_ROOT`，指向包含 `scripts/buildsystems/vcpkg.cmake` 的干净 vcpkg checkout。当前本机已有 `/tmp/duckomo-vcpkg`；其他机器应使用自己的对应目录。构建失败时停止后续检查，避免读取旧 `release` 别名的产物。`build-version.sh` 构建 CLI、扩展和 SQL runner；网格验收还需显式构建下面列出的 tool、core_functions 和 native targets。

```sh
export DUCKOMO_VCPKG_ROOT=/tmp/duckomo-vcpkg
python3 scripts/generate-grid-registry.py \
  --input test/data/grids/definitions.json --check
python3 scripts/generate-grid-registry.py \
  --input test/data/grids/definitions.json \
  --sample-manifest test/data/grids/sample-manifest.json \
  --sample-queries-output test/data/grids/sample-queries.sql
python3 scripts/generate-grid-registry.py \
  --input test/data/grids/definitions.json \
  --sample-manifest test/data/grids/sample-manifest.json \
  --sample-queries-output test/data/grids/sample-queries.sql --check
scripts/build-version.sh --matrix test/data/grids/version-matrix.json \
  --pair baseline-1.5.4 --output-root build/grid-matrix
scripts/build-version.sh --matrix test/data/grids/version-matrix.json \
  --pair prerelease-2.0-dev --output-root build/grid-matrix
scripts/validate.sh --matrix test/data/grids/version-matrix.json \
  --pair baseline-1.5.4 --output-root build/grid-matrix
make sanitizer-test
```

构建成功后，在每个待检查的版本组合中补齐本地 H2–H5/H7 所需目标；下面以 baseline 为例：

```sh
cmake --build build/grid-matrix/baseline-1.5.4/release --parallel 4 \
  --target duckomo_grid_validation core_functions_loadable_extension \
  grid_selection_test parallel_scan_test spatial_lifecycle_test \
  reader_capacity_test grid_zero_io_test source_identity_test
```

`generate-grid-registry.py --check` 会临时生成两次并与 checked-in C++ 定义逐字节核对；带 `--sample-queries-output` 时也会核对 generated SQL。查询文件按 manifest 中有兼容输入的条目生成显式/domain view，并先创建完整物化基准；当前没有 N320 或 N320 区域 OM v3 对象，所以这些条目明确保留 `NOT RUN`。真实 ECMWF HRES O1280 可运行值读取补充查询，但没有 verified N-grid 身份/点序，不生成错配的 N160/N320 地理 view。点/面 smoke 查询采用固定矩形边界，不是生产网格的独立空间关系 oracle。`duckomo_grid_validation --cases H0` 会保存两次 SQL 重生、hash、精确命令/退出码和 checked-in 审计到 `h0-query-regeneration/`；查询生成可通过仍不代表完整 H0 坐标/值门禁通过。

当前可复现的本地开发 CLI 与扩展来自同一 `build/release-vcpkg` 构建。H0 先绑定生成的 view 定义（只创建 view，不扫描网格值），再单独报告 oracle 门禁；不要混用 `build/release` 的旧 CLI/扩展。以下路径是本地开发验证身份，不是已经完成的 baseline matrix build：

```sh
python3 scripts/validate-grid-sample-queries.py \
  --duckdb build/release-vcpkg/duckdb \
  --extension build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension
build/release-vcpkg/test/tools/duckomo_grid_validation \
  --root "$PWD" --manifest "$PWD/test/data/grids/sample-manifest.json" --cases H0 \
  --duckdb "$PWD/build/release-vcpkg/duckdb" \
  --extension "$PWD/build/release-vcpkg/extension/duckomo/duckomo.duckdb_extension" \
  --output "$PWD/specs/004-multi-grid-selection/evidence/baseline-local/h0-query-regeneration-20261007-audited-final"
```

本地开发构建审计记录在 `evidence/baseline-local/h0-query-regeneration-20261007-audited-final/`。当前 9 个显式/domain/补充值 view 均能绑定，包含真实 CHMI ALADIN domain；旧 evidence 只记录 view binding。新的匹配 baseline 矩阵 CLI/扩展审计在 `evidence/baseline-local/h0-query-regeneration-20261007-matched-schema/`，额外通过了默认输出与 opt-in `om_source` schema、显式/domain `grid_id`/`layout` 等价检查（12 项 schema/source 记录）。其中 N160 是 synthetic identity fixture。相同的 schema/source 子检查也在 prerelease pair 上返回 exit 0，详见 [`schema-source-smoke-20261007.json`](evidence/version-matrix/schema-source-smoke-20261007.json)；这不是 H8 验收。生成与绑定子检查通过，完整 H0 坐标和值 oracle 仍为 not-run；总 gate 因此返回 exit 2。输出目录必须是空目录。使用旧 `build/release` CLI/扩展组合会使 ALADIN domain 报 unknown；运行门禁时使用同一固定 pair 的 CLI 与扩展。

两套固定组合均已有隔离 build manifest，状态为 `build-verified`：baseline ID `15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8`（DuckDB `v1.5.4 (Variegata) 08e34c447b`），prerelease ID `9a7aff37d7cc7c910cc0d29fc08a0fb51d6f199f3115791731d71eb00630312b`（DuckDB `v2.0.0-dev86261 (Development Version) 7264a9f0e5`）。这只确认配套构建身份及定向 ABI/兼容检查；不表示 H0–H8 网格、远程或正式版 gate 已通过。详细命令和产物 hash 见 [matrix build evidence](evidence/version-matrix/builds.md)。不能修改共享 submodule 来串行覆盖两套身份。

`validate.sh --matrix ... --pair ...` 只接受 resolver 当前算出的 build_id 与 build manifest 一致、stage manifest 同 pair/build_id、且 DuckDB/httpfs/DuckOMO 三个二进制 hash 均匹配的隔离目录；它不能替代矩阵构建或 gate 运行。Makefile 的 `matrix-release` / `matrix-test` 使用同一接口，默认 pair 为 `baseline-1.5.4`，可通过 `DUCKOMO_MATRIX_PAIR` 选择另一固定组合。

```sh
build/grid-matrix/baseline-1.5.4/release/duckdb -version
build/grid-matrix/prerelease-2.0-dev/release/duckdb -version
```

预期与各build-manifest相符；每条SQL使用同目录CLI/duckomo/httpfs。stock或跨组合httpfs必须在远程绑定前拒绝。matrix/profile中的hardcoded旧版本常量不得残留。

## 1. 本地坐标、身份及过滤

先用 [SQL contract 的 raw.om 旋转声明](contracts/sql-interface.md#地理条件与示例) 核对 value 和 logical_index=0…5、独立坐标参考。该小样本不用于读取收益。`test/data/grids/sample-queries.sql` 当前生成了与冻结样本/定义匹配的三类真实投影对象和 N160 合成 identity fixture；缺少来源对象的 N320/区域 view 保持 not-run。查询模板包括：

- `explicit_<id>` view：完整显式grid+axes+include_source，路径来自冻结样本。
- `domain_<id>` view：等价命名domain，元数据缺轴时显式完整dimensions。
- `baseline_<id>` TEMP TABLE：**先完整物化**grid view，再使用同一条件；禁止基准也由同一优化路径局部扫描。
- 固定窄区域/接缝/空/回退/点面 smoke 查询；官方值及独立坐标 oracle 尚待 T004。

对每个生成view执行双向多重集合差分；以下以生成的`grid_rotated`为例，具体边界使用manifest预定区域：

```sql
CREATE TEMP TABLE baseline_rotated AS SELECT * FROM grid_rotated;
SELECT count(*) AS differing_rows FROM (
  (SELECT * FROM grid_rotated
   WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120
   EXCEPT ALL
   SELECT * FROM baseline_rotated
   WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120)
  UNION ALL
  (SELECT * FROM baseline_rotated
   WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120
   EXCEPT ALL
   SELECT * FROM grid_rotated
   WHERE latitude BETWEEN 30 AND 40 AND longitude BETWEEN 110 AND 120)
);
```

预期differing_rows=0；用source位置核对重复经纬度、多时次与局部Gaussian，不按坐标去重。source显式/domain的grid_id/layout_id相等；两对象不同的version/object evidence不能随意忽略。坐标容差只用于oracle，SQL边界包括输出值及其nextafter邻值，严格比较。

完整输入及对应 gate 实现齐备后，H0–H5/H7 本地入口形式如下。当前工具/输入缺项时会返回 nonzero/not-run，不得把这种结果记作 gate pass：

```sh
build/grid-matrix/baseline-1.5.4/release/test/tools/duckomo_grid_validation \
  --root "$PWD" --manifest "$PWD/test/data/grids/sample-manifest.json" \
  --cases H0,H1,H2,H3,H4,H5,H7 \
  --duckdb "$PWD/build/grid-matrix/baseline-1.5.4/release/duckdb" \
  --extension "$PWD/build/grid-matrix/baseline-1.5.4/release/extension/duckomo/duckomo.duckdb_extension" \
  --output "$PWD/specs/004-multi-grid-selection/evidence/baseline-local"
```

输出目录为本次空目录；退出 0 仅当全部指定 gate 通过，缺样本、未运行或统计不完整应非零。三类投影坐标参考与 DuckOMO 坐标输出在 `1e-4°` 内全点一致，见 [`pinned-f32-coordinate-refresh-20261008`](evidence/development-local/pinned-f32-coordinate-refresh-20261008.md)；匹配 baseline 的 H1 子比较进一步确认 2,843,101 个坐标点及 329,401,680 个值位置通过，坐标最大误差为 `3.0517578125e-5°`，见 [H1 部分证据](evidence/baseline-local/h1-20261008-5dac993/)。完整 H1 仍因缺少 Gaussian 参考和独立 OM 数组轴到 producer 点序证明而 not-run；H0、H5/部分 H7 和 H6 也仍有缺口，不能由部分结果升级完整 gate。

当前匹配 baseline 的 H1 部分比较可用以下命令复现。它需完整扫描三个现有投影对象；输出目录使用新的时间戳。退出 2 表示 partial reference comparison 已记录，但 full H1 仍 not-run：

```sh
H1_BUILD_ID=5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d
H1_RELEASE="$PWD/build/grid-matrix/baseline-1.5.4/builds/$H1_BUILD_ID/release"
H1_OUTPUT="$PWD/specs/004-multi-grid-selection/evidence/baseline-local/h1-$(date +%Y%m%d-%H%M%S)"
"$H1_RELEASE/test/tools/duckomo_grid_validation" \
  --root "$PWD" --manifest "$PWD/test/data/grids/sample-manifest.json" \
  --cases H1 \
  --duckdb "$H1_RELEASE/duckdb" \
  --extension "$H1_RELEASE/extension/duckomo/duckomo.duckdb_extension" \
  --output "$H1_OUTPUT"
```

## 2. 零值读取与有界执行

每类运行全域/局部坐标、source/info、坐标COUNT、纯COUNT、矛盾条件及可证明空区域；预期value_index/value_data/decode=0。含值过滤对照只解码输出与过滤变量。坐标COUNT依旧执行DuckDB残余WHERE，原生重复非空间记录保留。

H4使用1/2/4 worker上限、足够多任务和跨batch查询，比较完整source多重集合；LIMIT/不足任务/取消/失败另验终态和恢复，不进入收益。H5固定相同chunk形状、最大物理块界、线程、缓存及近似同窄区域结果，把原生点数扩大≥10倍，work peak≤2倍且不超[已发布预算](contracts/selection-and-io.md#内存预算)。必须包含4096/4097碎片、超长单线、长时间轴及大量远程attempts；不能把工作控制集归入定义或只测RSS。

## 3. 远程基线与每类收益

先按[003 quickstart](../003-dimensions-remote-parallel/quickstart.md)补齐G3/G5/G6、完整远程G4与G7记录，并完成权限分区和统计控制验证。004当前实现会按路径/secret配置隔离 access fingerprint 并在 worker 打开时重核；HTTP 指纹还包含 `TYPE BEARER` token、TLS 验证开关和 CA 路径。T043 变更/失效测试及签名 S3 撤权、恢复、secret/endpoint/region 切换仍未执行。受控HTTP、HTTPS和签名S3须提供与本地**同内容版本**的全部固定样本。服务版本/digest、桶、endpoint、region/TLS及逐attempt审计写run-manifest，凭据通过当前环境/secret提供，不写evidence。

baseline DuckDB v1.5.4 远程命令启动前，检查 HTTPFS 的 `http_proxy` 设置值。其代理解析器接受 `http://host:port`，但尾部 `/` 会被并入端口并在发出网络请求前失败；例如本机代理应写成 `HTTP_PROXY=http://proxy-host:proxy-port`。遇到 `could not establish a strict range session` 时，先区分本地代理初始化错误和实际 HTTP/S3 响应；代理设置的凭据继续通过 DuckDB secret/config 提供，不写入命令记录或 evidence。此项只是客户端前置检查，不构成远程 gate 或 Range 支持证据。

本地 fixture 的 HTTP、HTTPS 和 S3 审计代理都绑定 loopback。DuckDB 1.5.4 HTTPFS 在此环境中即使 `NO_PROXY` 包含 `127.0.0.1`，仍会尝试使用继承的 `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY`，导致 S3 Range session 在请求到达 fixture 前失败。对 `setup-remote-fixtures.py` 生成的本地 `run.env`，运行 gate 时仅为 gate 进程清除这些代理变量；不要为外部 S3 endpoint 无条件清除代理变量。`scripts/validate.sh` 会根据同目录 `run-manifest.json` 中的 loopback bind 自动应用此设置。

S3 审计代理跨客户端复用最多 16 条上游连接，避免大量小 Range 请求耗尽 Docker 转发层的临时端口而返回 502。修改代理后须重启 fixture 服务，使进程加载新代码；`python3 test/tools/remote_fixture_proxy_test.py` 可独立验证连接复用、并发上限和失败响应体计量。审计只记录异常类型与系统错误码，不保存认证头或异常原文。

T048 已实现 grid-manifest 对象上传、调用者提供的 S3 service version/digest 记录、HTTP/HTTPS fixture 服务和逐请求审计日志；所有输入对象在上传前重验 OM v3 magic、大小及 SHA-256。manifest 明确标记服务身份为 caller-supplied 且未经独立核验。上传前要求 `--s3-bucket` 与 `--authorized-s3-bucket`（或 `DUCKOMO_AUTHORIZED_S3_BUCKET`）完全相同，以防误写其他桶；这只记录操作者选定的测试桶，不验证服务端身份。grid-manifest 模式要求提供 TLS 证书/私钥，CA 路径通过 `DUCKOMO_HTTPS_CA_CERT` 导出；run.env/secret SQL 从创建时即为当前用户可读。下列为目标 CLI 调用形式，真实服务审计、签名 S3 授权与 T049–T055 门禁仍未执行。

```sh
python3 scripts/setup-remote-fixtures.py \
  --fixtures test/data --grid-manifest test/data/grids/sample-manifest.json \
  --s3-endpoint "$DUCKOMO_TEST_S3_ENDPOINT" \
  --s3-bucket "$DUCKOMO_TEST_S3_BUCKET" \
  --authorized-s3-bucket "$DUCKOMO_TEST_S3_BUCKET" \
  --s3-service-version "$DUCKOMO_S3_SERVICE_VERSION" \
  --s3-service-digest "$DUCKOMO_S3_SERVICE_DIGEST" \
  --http-tls-cert "$DUCKOMO_TEST_TLS_CERT" --http-tls-key "$DUCKOMO_TEST_TLS_KEY" \
  --output build/grid-remote-fixtures
. build/grid-remote-fixtures/run.env
env -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY -u http_proxy -u https_proxy -u all_proxy \
  "$PWD/build/grid-matrix/baseline-1.5.4/builds/5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d/release/test/tools/duckomo_grid_validation" \
  --root "$PWD" --manifest "$PWD/test/data/grids/sample-manifest.json" --cases H6 \
  --duckdb "$PWD/build/grid-matrix/baseline-1.5.4/builds/5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d/release/duckdb" \
  --extension "$PWD/build/grid-matrix/baseline-1.5.4/builds/5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d/release/extension/duckomo/duckomo.duckdb_extension" \
  --httpfs "$PWD/build/grid-matrix/baseline-1.5.4/builds/5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d/release/extension/httpfs/httpfs.duckdb_extension" \
  --http-base "$DUCKOMO_HTTP_BASE" --https-base "$DUCKOMO_HTTPS_BASE" \
  --s3-base "$DUCKOMO_S3_BASE" --s3-setup "$DUCKOMO_S3_SETUP" \
  --server-log "$DUCKOMO_SERVER_LOG" \
  --output "$PWD/specs/004-multi-grid-selection/evidence/baseline-remote"
```

2026-10-08 本地 S3 Range smoke 对 pinned Open-Meteo O1280 static `.om` 成功：匹配 baseline DuckDB/HTTPFS/DuckOMO 读取一行返回 `-999.0`，本地服务审计确认 Range 请求为精确 `206`；相同调用在继承代理环境下失败，在 gate 进程清除 proxy 环境变量后通过。它只证明该对象的远程读取路径可用，不证明 O1280 网格映射或 full/local 收益。记录见 [`local-s3-range-proxy-bypass-20261008.md`](evidence/development-local/local-s3-range-proxy-bypass-20261008.md)。

生成run.env/secret SQL仅当前用户可读；harness负责SET cache_enabled=false、清会话/配套缓存、记录cold设置、完整消费结果并在同列依赖下比较full/local。预期四类local/HTTP(S)/S3完全一致；value data、decoded chunks及两个远程来源的**包含bind/HEAD/探测/坐标准备/全部retry的总已收body**均严格下降，服务端独立对账。公开HTTPS冒烟不替代S3/真实服务审计。

`duckomo_grid_validation --cases H6` 会先运行本地 synthetic/loopback 子检查，并将 exact local/HTTP result CSV、两端 v4 metrics、opaque `object_id` 的分别存在性检查、服务端 body bytes 对账及六次 Gaussian transport stress 保存到 `h6-local/`。runner 检查成功扫描的两端 v4 metrics 和取消查询 metrics 均包含 `memory.query_owned_released_at_terminal=true`。2026-10-08 的更新证据位于 [`h6-loopback-protocol-final-20261008`](evidence/baseline-local/h6-loopback-protocol-final-20261008/)：64 行 local/HTTP 结果匹配、HTTP body 与服务端均为 3,270 bytes；6 次 stress 共 3,462 attempts；弱 ETag 不跨查询复用，同 URI 等长强版本替换与短读后恢复均匹配完整参考，且没有 un-ranged GET fallback。该子检查通过仍会让完整 H6 保持 `not-run`，直到受控 HTTPS/S3、真实 Gaussian region offsets 和 003 依赖具备。`scripts/validate.sh` 仅在设置 `DUCKOMO_HTTPS_BASE` 且远程配置齐备时运行 H5/H6 harness；exit 2 仅在 gate 明确 `not-run` 且 evidence audit 通过时作为未完成状态记录。

同连接另验cache cold/hot/disabled/clear/eviction、强弱token、同URI等长替换、实际签名S3撤权/恢复/secret及endpoint/region变化；再注入协议错误/短读/200/取消并验证新查询恢复。若以失败或提前停止得到较少字节，H6失败。

## 4. 后续空间计算输入

`om_grid_info`必须列出完整定义、地球模型/坐标来源、源轴/单位/局部映射与能力状态；按om_source.axis_indices/point_index重新生成位置并核对。H7给每类固定polygon，在其明确地理角度来源的**同一平面角坐标空间**中做point/polygon关系；lon为x、lat为y。bbox只是候选，最终关系采用完整判定并与全域基准差分为零。

2026-10-07 对当前 baseline build ID `15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8` 重跑了可复现的本地子检查。匹配产物的调用和记录路径：

```sh
build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release/test/tools/duckomo_grid_validation \
  --root "$PWD" --manifest "$PWD/test/data/grids/sample-manifest.json" \
  --cases H3,H7 \
  --duckdb "$PWD/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release/duckdb" \
  --extension "$PWD/build/grid-matrix/baseline-1.5.4/builds/15f751c1929b88adebbfe2a150a8c7987f92de29af7fc0b806a6af1cd80388c8/release/extension/duckomo/duckomo.duckdb_extension" \
  --output "$PWD/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261007-current-15f751-final"
```

H7 子检查对四种合成小网格逐点比较独立坐标参考、原始 logical/point/parent/axis 位置以及完整 point/polygon 关系多重集合；source 与 `om_grid_info` 的值 index/data/decode 成本均为零。它还对三个 hash 固定的本地真实投影 OM v3 文件各检查前四个空间位置的 explicit/domain source 身份及 source/info 零值读取。当前样本的 `[ny,nx,ntime]` 轴顺序来自固定生产者 README，未由独立 GRIB/归档点序证据确认；这些子检查因此不构成生产者坐标/轴序验收。缺少 N160/N320/N320-region 对象、独立 Gaussian 点序和 H6 跨 URI/受控服务证据，整体 H3/H7 仍记录为 `not-run`，harness 按预期返回 2；本次证据 manifest 审计通过。匹配 baseline 的 H3/H7 子检查及限制见 [`us5.md`](evidence/baseline-local/us5.md) 与 [本次证据目录](evidence/baseline-local/us5-20261007-current-15f751-final/)。

2026-10-08 在同一 baseline build ID 上重建 runner 后，更新的 H7 本地子检查将公开样本查询限制为自然扫描前四行，并逐行核对 logical/axis/point 位置；这避免为了取四点而排序完整网格。三个样本的 pinned Float32 坐标参考均通过固定 `1e-4°` 容差，显式/domain 的 source/info 身份相同，值 index/data/decode 读取为零。四类合成全关系检查也通过。Rotated、stereographic、Lambert 的最大误差依次为 `1.52587890625e-5°`、`1.52587890625e-5°`、`5.841255187988281e-6°`。运行命令与逐项哈希见 [本次 H7 子检查](evidence/baseline-local/us5-20261008-bounded-source-prefix/h7-local/manifest.json)；顶层 harness 因完整 H7 条件仍缺失返回 2，证据 manifest audit 通过。首次带完整 `ORDER BY logical_index` 的尝试因扫描全网格而中断，单独记录在 [中断说明](evidence/baseline-local/us5-20261008-pinned-source-positions/README.md)，不属于验收证据。此部分仍未独立证明完整 OM axis→producer point mapping，也不替代所需 Gaussian/远程证据。

同一 baseline runner 随后加入 H3 公开 source/info 检查；[合并证据目录](evidence/baseline-local/us5-20261008-h3-public-source-identity-sql/)中的三个 hash-pinned 投影 OM v3 样本各核对前四个自然 source positions，显式/domain 的 SQL `FULL OUTER JOIN` 对每个样本得到 4 joined、0 unmatched、0 mismatch、`all_equal=true`。15 份 source/info/identity v4 metrics 的 value index/data/decode 均为零。H3 与 H7 的本地子检查通过，完整 gate 均为 `not-run`，harness 按预期返回 2；顶层 evidence manifest audit 通过。该检查仍未闭环 Gaussian N 网格、完整 OM 轴到 producer 点序、H1/H2 完整公开 source、公开远程 source 和跨 URI 同内容映射。

后续 validator 增加 `--all-public-source-positions`。这三个冻结的公开 OM v3 对象没有可绑定的 `valid_time` 坐标元数据，所以 validator 按 manifest 中的 `ntime` 显式声明一组仅用于选择轴下标的递增合成 `valid_times`，并在不带网格定义的同对象 `read_om(...dimensions..., valid_times...)` 上用 `LIMIT 1` 取得 index 0 标签。随后以该标签过滤显式/domain 网格读取，令语义轴选择只保留 time index 0；该标签不是生产者时间值，也不形成时间坐标证据。查询不排序、不投影值列；压缩 CSV 逐行与全部 pinned producer 坐标比较，也逐行核验 explicit/domain 的位置身份与坐标一致，并要求完整扫描候选数恰为 `ny*nx`、所有 value/index/decode 计量为零。H3 runner 已接入此模式。

2026-10-08 在匹配 baseline build ID `5dac993fc31d45def31d02b87c7d23ced7858782fb9a6546b0e22e1a1b95f81d` 上运行 H3/H7 后，全点公开 source 子检查对 rotated GEM、stereographic GEM、Lambert 三个 hash-pinned 样本分别核对 1,191,300、770,440、881,361 个空间位置；显式/domain source 身份逐行一致，pinned Float32 坐标最大误差为 `3.0517578125e-5°`，低于冻结的 `1e-4°`，source/info/value/index/decode 计量均为零。全 H3 本地命令及四类 synthetic H7 完整关系/source 子检查退出 0。完整 H3/H7 仍为 `not-run`，组合 runner 按预期 exit 2，manifest audit 通过；完整证据见 [`all-public-source H3/H7 run`](evidence/baseline-local/us5-20261008-all-public-source-positions-5dac993-h3-local/)。这些对象未嵌入轴名，`[ny,nx,ntime]` 顺序仍依赖固定 producer README，故全点结果不构成独立 OM array-axis→producer point-order 证明，也不补齐 Gaussian N 网格、H1/H2/H6 完整公开 source、远程或跨 URI 证据。

```sh
build/grid-matrix/baseline-1.5.4/release/test/tools/duckomo_grid_validation \
  --root "$PWD" --manifest "$PWD/test/data/grids/sample-manifest.json" \
  --cases H7 \
  --duckdb "$PWD/build/grid-matrix/baseline-1.5.4/release/duckdb" \
  --extension "$PWD/build/grid-matrix/baseline-1.5.4/release/extension/duckomo/duckomo.duckdb_extension" \
  --output "$PWD/specs/004-multi-grid-selection/evidence/baseline-local/us5-20261008-bounded-source-prefix"
```

投影/旋转使用其公布球面来源；Gaussian使用描述中的WGS84来源或明确球面行表，同一示例双方必须采用同一声明坐标空间，不能把球面点暗改EPSG标签。空间扩展仅此显式示例需要，普通read_om独立。1.5空间组合和2.0几何/CRS函数须按对应精确版本验证并记录；也可由独立planar参考工具完成，不能把未知2.0 CRS函数直接写成已可运行。示例不声称球面距离、面积或datum变换，也不实现om_interp/regrid。

## 5. 版本复验与独立复现

用对应固定组合的 CLI/扩展运行 H8。每次输出必须是新的空目录；`--matrix-evidence-root` 指向包含两套 H0–H7 run manifests 的证据根。H8 会校验两套 build manifest、DuckDB/DuckOMO/HTTPFS artifact hashes、配套 ABI/compatibility 记录，并要求每个 pair 存在同一冻结 input-set 上完整通过且已审计的 H0–H7 manifest：

```sh
H8_OUTPUT="$PWD/specs/004-multi-grid-selection/evidence/version-matrix/h8-prerelease-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir "$H8_OUTPUT"
build/grid-matrix/prerelease-2.0-dev/release/test/tools/duckomo_grid_validation \
  --root "$PWD" --manifest "$PWD/test/data/grids/sample-manifest.json" \
  --matrix "$PWD/test/data/grids/version-matrix.json" \
  --matrix-evidence-root "$PWD/specs/004-multi-grid-selection/evidence" --cases H8 \
  --duckdb "$PWD/build/grid-matrix/prerelease-2.0-dev/release/duckdb" \
  --extension "$PWD/build/grid-matrix/prerelease-2.0-dev/release/extension/duckomo/duckomo.duckdb_extension" \
  --httpfs "$PWD/build/grid-matrix/prerelease-2.0-dev/release/extension/httpfs/httpfs.duckdb_extension" \
  --output "$H8_OUTPUT"
```

`h8-version-matrix/h8-summary.json` 单独报告 `identity_status` 和完整 gate `status`。身份正确但任一 H0–H7 run、输入快照或证据审计缺失时，H8 为 `not-run` 并返回 2；身份/hash 不匹配为 `fail`。当前两套 build 身份核验通过，但 H0–H7 完整同输入记录仍缺失，因此 H8 仍为 `not-run`。正式 2.0 发布后另加固定正式组合重复全部步骤，只在通过后声明正式支持。

H9由未参与实现者取得冻结输入和本指南，独立完成四类读取、一次远端全/局部成本及一次完整空间关系；保存其环境、命令、exit codes及对gate的判断。final.md逐项列H0–H9及SC-001–011的pass/fail/not-run和样本/组合范围；实际通过后再更新公开支持清单与roadmap，不由计划状态代替。
