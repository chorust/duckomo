# 官方 HTTPFS 支持与复现

本轮支持范围：DuckDB **v1.5.4、v1.5.5、v1.5.6 / Linux AArch64**。其它平台及 2.0 开发版未通过本轮验证。每个 DuckOMO 产物对应自己的引擎版本，不允许把三个扩展文件互换。版本和官方产物 SHA256 固定在 `test/data/grids/version-matrix.json`。

## 安装和构建

社区登记与发布准备见 [Community Extensions 接入](community-extensions.md)。当前尚未收录；社区发布后使用 `INSTALL duckomo FROM community; LOAD duckomo;`，签名产物无需 `-unsigned`。以下路径加载用于本地开发验证：


```sql
INSTALL httpfs;
LOAD httpfs;
LOAD '/path/to/duckomo.duckdb_extension';
```

未签名的 DuckOMO 需 `duckdb -unsigned`。发行物只有 DuckOMO；本地读取不需要 HTTPFS。源码构建保留 DuckDB、OM C 和 extension-ci-tools 子模块，HTTPFS 不再是源码依赖。无需为 HTTPFS 配置 `DUCKOMO_VCPKG_ROOT`。

```bash
git submodule update --init --recursive
scripts/build-version.sh v1.5.4
# 同样支持 v1.5.5、v1.5.6。构建记录与产物在 build/official-matrix/<version>/。
python3 scripts/version_matrix.py fetch-runtime --root . \
  --matrix test/data/grids/version-matrix.json --pair v1.5.4
scripts/validate.sh --matrix test/data/grids/version-matrix.json --pair v1.5.4 --local-only
```

构建输入 hash 与输出 hash 分开。`fetch-runtime` 从官方地址取 CLI/HTTPFS，校验解压后 SHA256 和 CLI 版本；缺包明确失败，不回退自编 HTTPFS。HTTPFS 使用 DuckDB 标准扩展版本/平台校验，没有 DuckOMO 专用 handshake。

## 受控远程验证

复用 `scripts/setup-remote-fixtures.py` 建立 HTTP、带 CA 的 HTTPS、带签名凭据的 S3 及服务日志。先准备兼容 S3 的测试服务、TLS 证书/私钥及固定样本，再按 `--help` 传入端点、bucket、凭据文件和 TLS 路径。凭据 SQL、私钥及 run.env 应保留在忽略的 build 目录，不作为发布证据。不要把私有 SQL 写进终端日志。

加载 setup 工具生成的私有 `run.env`（含 DUCKOMO_HTTP_BASE、DUCKOMO_HTTPS_BASE、DUCKOMO_S3_BASE、DUCKOMO_S3_SETUP、DUCKOMO_SERVER_LOG、DUCKOMO_HTTPS_CA_CERT），然后执行：

```bash
source build/remote-fixtures/run.env
scripts/validate.sh --matrix test/data/grids/version-matrix.json --pair v1.5.4
```

也可直接运行官方 CLI 验证（每个版本重复）：

```bash
python3 scripts/validate-official-httpfs.py \
  --duckdb build/official-matrix/v1.5.4/official/duckdb \
  --httpfs build/official-matrix/v1.5.4/official/httpfs.duckdb_extension \
  --extension build/official-matrix/v1.5.4/release/extension/duckomo/duckomo.duckdb_extension \
  --output build/official-validation/v1.5.4
```

工具完整消费、排序并 hash 结果，验证本地/HTTP/HTTPS/S3、冷局部收益、1/2/4 worker、LIMIT、重复查询、短读/访问失败/可观察版本变化后的恢复和并发。每条 CLI 命令默认允许执行 120 秒，超时终止。`--real-file` 可加入固定真实样本的同内容对照。取消及同进程连接隔离由匹配引擎的 `test/native/official_httpfs_test` 加载官方 HTTPFS 执行，需要 DUCKOMO_HTTPFS/DUCKOMO_HTTP_BASE；不是以 CLI 的退出信号代替连接取消。纯 `remote_session_test` 通过标准 FileSystem 实现验证句柄、边界、权限、取消与失败恢复。

报告保存官方 URL/hash、引擎版本、样本身份、完整结果 hash、耗时、进程 RSS 和独立服务审计。服务日志是服务端已发送量，失败/取消时不等于客户端消费量。性能阈值在 `specs/004-multi-grid-selection/evidence/official-httpfs-refactor/performance-policy.json` 中提前固定。

## 能力边界和 SQL 迁移

对象须在扫描期间稳定。打开时比较标准文件长度/版本标签；空标签不是内容验证。使用当前 ClientContext 和文件访问限制；每 worker 独立句柄/decoder。opener 局部禁用强制全下载、下载阈值及全下载回退，保持 ETag 检查并启用可用 S3 version pinning，标准 DIRECT_IO 禁止 HTTPFS 读取缓冲扩大范围。不会在扫描中修改全局设置。

不承诺固定 HEAD/探测顺序、逐响应强保证、扫描快照或上游缓存即时撤权。HTTPFS 的内部缓存、凭据、签名、重试及分配由官方依赖维护。

移除自有缓存是本次复杂度取舍，并非社区收录要求。

删除 `duckomo_cache_enabled`、`duckomo_cache_capacity`、`duckomo_clear_cache()`，旧 SQL 会明确报未知设置/函数。v4/legacy 结构保留：远程 transport 为 NULL/complete=false，本地为 0/true；原自有 cache false/0、原因 removed。应用成功读取字节不等于物理网络字节；HTTPFS 总内存不由 DuckOMO 自有账本约束。

旧 G5 为 superseded，原失败记录保留。三版本 runtime 通过不代表 004 全部真实网格门禁 H0–H9 完成。独立验证者复现仍是单独的发布条件，不由实现者代签。

同一 URI 的访问变化可复现于迁移证据目录内的 `access-change.py`：通过现有 fixture service 的控制入口切换 normal → denied → normal，在同一连接检查失败与恢复。该用例仅证明固定官方版本/配置的受控行为，不扩展为任意上游缓存的即时撤权承诺。

固定样本的重复查询是此次缓存移除的明确取舍：旧自有热缓存减少网络请求，而标准 DIRECT_IO 官方路径会重新读取应用请求范围。不能将迁移描述为热缓存优化。冷查询耗时以预设 <=2x 旧关闭缓存的五轮中位数门槛审查，启动与 RSS 分开记录。
