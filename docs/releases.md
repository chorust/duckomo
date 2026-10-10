# GitHub tag 自动发布

[Release workflow](../.github/workflows/release.yml) 将同一源码 tag 构建为多个 DuckDB 版本/平台的**未签名**开发发行包。它独立于 DuckDB Community Extensions 登记，不要求先被社区收录，也不会替社区签名。

## 矩阵与当前验证边界

| DuckDB | Linux glibc x86_64 | Linux glibc ARM64 | macOS Intel | macOS Apple Silicon |
|---|---|---|---|---|
| v1.5.4 | `linux_amd64` | `linux_arm64` | `osx_amd64` | `osx_arm64` |
| v1.5.5 | `linux_amd64` | `linux_arm64` | `osx_amd64` | `osx_arm64` |
| v1.5.6 | `linux_amd64` | `linux_arm64` | `osx_amd64` | `osx_arm64` |

x86_64 产物的 OM C 库要求 CPU 支持 **SSSE3**；不要求 AVX2，也不使用构建机特定的 `-march=native`。ARM64 保持原有 NEON 路径。

每次发布必须产生全部 12 份 ZIP；不发布部分成功的矩阵。Windows、musl、Wasm 和 DuckDB 2.0 不在此流程范围。社区登记仍只申请 v1.5.6 / Linux ARM64，不能把 GitHub 发布矩阵当作社区支持矩阵。

原有深入验证记录主要来自 Linux ARM64。新增 Linux x86_64/macOS 平台在各自工作流实际通过前，仅是配置目标，不是已验证的支持声明。Release 的本地/HTTP 冒烟测试不替代完整 SQL/native 回归、HTTPS/签名 S3、真实网格、完整内存审计和独立验证者复现；既有 003/004 未完成门禁保持原状。未完成完整验收时，建议使用 `v0.1.0-rc.1` 等预发布 tag 供试用，不将 GitHub 包发布等同于生产就绪或 R21 通过。

## 触发和失败行为

- 推送 `vMAJOR.MINOR.PATCH` tag 自动构建并发布；如 `v0.1.0`。
- 带后缀的 tag（如 `v0.1.0-rc.1`）发布为 GitHub **prerelease**。
- `v*` 是触发过滤器，脚本进一步拒绝不符合版本格式的 tag。
- 手动 `workflow_dispatch` 跑完整构建、验证、打包和聚合，但**不发布 Release**。在分支上预演时，包名使用 `v0.0.0-dry-run`。
- 涉及发布脚本/配置、CMake 或 OM 子模块的 PR 运行发布契约检查及四平台 OM C 独立编译检查，不构建完整发布包；现有 community CI 继续运行，不授予 PR 发布权限。
- 构建、原生运行验证或聚合检查任一失败，不运行发布步骤。
- 先创建 draft 并上传全部资产，成功后才公开。已有 Release（含失败遗留 draft）明确拒绝覆盖；维护者核对 draft 后手动处理，再重跑。不会删除或移动 tag。
- 只有最后的 `publish` job 有 `contents: write`；无需额外 PAT，使用仓库 `GITHUB_TOKEN`。仓库/组织策略须允许 Actions 创建 Release。

先将工作流和脚本提交到仓库默认分支，再预演：

```sh
gh workflow run release.yml --ref main --repo chorust/duckomo
# 在 Actions 页面查看本次运行及 release-assets artifact。
```

确认预演通过后，由维护者为包含该工作流的提交打 tag。以下命令会触发真实发布，请勿用来做本地测试：

```sh
git tag -a v0.1.0-rc.1 -m 'DuckOMO 0.1.0 release candidate 1'
git push origin v0.1.0-rc.1
```

## 构建、验证和资产

先在四个平台独立构建实际 OM C target，尽早发现架构编译问题；通过后再复用固定提交的 DuckDB 官方 extension-ci-tools 分发工作流，保持上游测试开启。所有候选产物还必须在匹配架构的 runner 中加载**实际官方 CLI + HTTPFS**：检查引擎版本、平台、二进制 SHA256、本地 6 行/总和 15、HTTP/local 局部 4 行一致，以及服务端 Range 发送量小于完整对象。原生运行检查补充了上游跳过 Linux ARM64/macOS Intel SQL 测试的缺口，但不声称等价于完整回归。

官方运行包地址和解压后哈希固定在 [`release-runtimes.json`](../test/data/release-runtimes.json)，不在每次发布时从“最新版本”推断。更新矩阵时须获取实际官方包并重新固定哈希。

发布资产：

```text
duckomo-v0.1.0-rc.1-duckdb-v1.5.6-linux_arm64.zip
...另外 11 个版本/平台组合...
duckomo-v0.1.0-rc.1-duckdb-v1.5.6-linux_arm64.duckdb_extension.gz
...另外 11 个版本/平台组合的 gzip 资产...
manifest.json
SHA256SUMS
```

`.zip` 资产供下载、校验和解压；`.duckdb_extension.gz` 是同一份已验证二进制的确定性 gzip，
供不解开 ZIP 时直接下载、解压后本地 `INSTALL`（聚合时会校验其解压结果与 ZIP 内二进制一致）。

每个 ZIP 包含：

- `duckomo.duckdb_extension`：保留标准文件名，未签名。
- `manifest.json`：源码 commit、扩展 tag、DuckDB 版本、平台、扩展 SHA256、签名状态。
- `verification.json`：该二进制的原生运行检查及官方运行包哈希。
- `LICENSE`。

聚合时再次检查完整矩阵、源码身份、通过状态及实际 ZIP 内二进制的哈希。根 `SHA256SUMS` 覆盖全部 ZIP、`.duckdb_extension.gz` 资产和根 manifest；发布前再次校验。哈希用于检测损坏和错配，不等于扩展签名。

## 下载与安装

以下示例在**对应 Release 成功发布后**才可用。先在你的 DuckDB 中运行 `SELECT version(); PRAGMA platform;`，选择精确匹配的包。macOS 使用 `osx_*`，不能加载 Linux ARM64 包。

资产文件名带有 DuckOMO 版本、DuckDB 版本和平台，不能把 Release URL 直接传给 `INSTALL`：DuckDB 按 URL basename 第一个 `.` 之前的部分推导扩展名（会存成 `duckomo-v0.duckdb_extension`），且加载时要求入口函数名与文件名一致（本扩展为 `duckomo_duckdb_cpp_init`），改名后 `LOAD` 失败。因此先下载 `.duckdb_extension.gz`、解压为标准文件名，再本地安装（未签名，仍需 `-unsigned`）：

```sh
TAG=v0.1.0-rc.1
DUCKDB_VERSION=v1.5.6
PLATFORM=linux_arm64  # 按实际环境改成 linux_amd64 / osx_amd64 / osx_arm64
ASSET="duckomo-${TAG}-duckdb-${DUCKDB_VERSION}-${PLATFORM}.duckdb_extension.gz"

curl -sfL "https://github.com/chorust/duckomo/releases/download/${TAG}/${ASSET}" | gunzip > duckomo.duckdb_extension
```

```sql
INSTALL './duckomo.duckdb_extension';
LOAD duckomo;
```

HTTP(S) 下载由本机 `curl` 完成，不需要 DuckDB 侧 HTTPFS；读取远程数据才需要官方 HTTPFS。

下载 ZIP 校验解压的方式仍然有效：

```sh
TAG=v0.1.0-rc.1
DUCKDB_VERSION=v1.5.6
PLATFORM=linux_arm64  # 按实际环境改成 linux_amd64 / osx_amd64 / osx_arm64
ASSET="duckomo-${TAG}-duckdb-${DUCKDB_VERSION}-${PLATFORM}.zip"
BASE="https://github.com/chorust/duckomo/releases/download/${TAG}"

mkdir -p duckomo-install
cd duckomo-install
curl -fL "$BASE/$ASSET" -o "$ASSET"
curl -fL "$BASE/SHA256SUMS" -o SHA256SUMS
# Linux：只校验下载的这一份包；macOS 改用 shasum -a 256 --check -
grep -F "  $ASSET" SHA256SUMS | sha256sum --check -
unzip "$ASSET"

duckdb -unsigned :memory:
```

也可使用 `gh release download "$TAG" --repo chorust/duckomo --pattern "$ASSET" --pattern SHA256SUMS` 下载。

在上面的安装目录启动 DuckDB 后执行：

```sql
INSTALL './duckomo.duckdb_extension';
LOAD duckomo;
-- HTTP(S)/S3 数据读取另需官方 HTTPFS：
INSTALL httpfs;
LOAD httpfs;
SELECT * FROM read_om('/path/to/weather.om') LIMIT 10;
```

每次启动加载未签名包的 DuckDB 都需要 `-unsigned`；Python 客户端使用 `duckdb.connect(config={'allow_unsigned_extensions': 'true'})`。仅加载可信来源的原生代码。

GitHub Release ZIP 不是 DuckDB 扩展仓库，不能写 `INSTALL duckomo FROM 'https://github.com/chorust/duckomo'`，ZIP 资产也不能直接 `INSTALL`。需要下载留档或校验链时采用 HTTP 下载、校验、解压、本地 `INSTALL` 的方式。社区收录后的签名安装仍见 [社区说明](community-extensions.md)。

## 本地检查与复现单个候选包

不创建 tag，也不调用 GitHub 发布 API：

```sh
python3 test/tools/release_tools_test.py
python3 scripts/release_tools.py matrix --tag v0.1.0-rc.1

# 以下在与 PLATFORM 匹配的本机执行，候选扩展须来自同一版本/平台构建。
python3 scripts/release_tools.py fetch-runtime \
  --version v1.5.6 --platform linux_arm64 --output build/release-runtime
python3 scripts/verify-community-artifact.py \
  --version v1.5.6 --platform linux_arm64 \
  --runtime-manifest test/data/release-runtimes.json \
  --duckdb build/release-runtime/duckdb \
  --httpfs build/release-runtime/httpfs.duckdb_extension \
  --extension /path/to/duckomo.duckdb_extension \
  --output build/release-verification.json
```
