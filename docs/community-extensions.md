# DuckDB Community Extensions 接入

状态：**现有 ARM64 GitHub CI 已通过；社区登记和签名发布待执行**，见 [main 构建记录](https://github.com/chorust/duckomo/actions/runs/37905479056)。登记草案在 [`community/duckomo/description.yml.in`](../community/duckomo/description.yml.in)，首次版本暂定 `0.1.0`，维护者按源码仓库 owner 填写为 `chorust`。本次检查记录见 [`community/validation.json`](../community/validation.json)：三版本既有产物检查、新编译 v1.5.6 生产配置产物、登记解析、平台过滤和 manifest 校验通过；完整社区 Docker 构建尚未执行。

新增的 [GitHub tag 发布流程](releases.md) 独立于社区流程，目标为三个 DuckDB 版本 × Linux/macOS 双架构；新增平台须在各自 CI 实际通过后才能形成验证记录。GitHub 包未签名，不扩大本文件的社区首发平台范围，也不替代完整生产验收和独立复现。

## 发布契约

向 [duckdb/community-extensions](https://github.com/duckdb/community-extensions) 提交 `extensions/duckomo/description.yml`。登记文件提供扩展信息、源码仓库和公开提交 SHA。社区工具链从该提交构建，社区负责签名和托管。源码仓库不保存扩展二进制或 `.gz`，也无需先创建 GitHub Release。

收录且发布完成后的使用方式：

```sql
INSTALL duckomo FROM community;
LOAD duckomo;
-- 本地 OM 无需 HTTPFS；HTTP(S)/S3 使用官方扩展：
INSTALL httpfs;
LOAD httpfs;
SELECT * FROM read_om('weather.om') LIMIT 10;
```

社区签名产物正常加载不需要 `-unsigned`。本地构建和 GitHub Actions 构建检查的产物仍未签名，开发验证才使用 `-unsigned`。当前不能以 `INSTALL duckomo FROM community` 的成功作为已完成证据。

DuckOMO 已移除配套 HTTPFS capability、补丁和源码子模块，远程入口使用官方 FileSystem API。自有缓存的移除属于复杂度取舍，并非社区收录要求；重复查询的网络读取代价见 [官方 HTTPFS 说明](official-httpfs.md)。

## 版本与首发平台

| 项目 | 范围 |
|---|---|
| 社区首次目标 | DuckDB v1.5.6 / `linux_arm64`，Linux AArch64、glibc |
| 源码兼容验证 | v1.5.4、v1.5.5、v1.5.6，每个版本独立产物 |
| 暂缓的平台 | Linux x86_64、musl、macOS、Windows、Wasm |
| 开发子模块 | 仍固定 v1.5.4，避免修改既有开发树；社区 CI 按目标版本切换 |

社区当前默认 v1.5.6，发布通常面向最新稳定版。三版本兼容不意味着本次登记会向旧版仓库回填产物。平台范围由真实支持的 `extension.excluded_platforms` 字段控制；`opt_in_platforms` 不启用。新增平台要先构建并验证，随后更新登记文件。

Makefile 从实际 DuckDB checkout 推导版本；社区传入的 `DUCKDB_GIT_VERSION` 稳定版 tag 可用于没有 tag 的源码 checkout。移除了固定 v1.5.4 的覆盖。`make matrix-release` 默认 v1.5.6；指定 `DUCKOMO_MATRIX_PAIR=v1.5.4` 或 `v1.5.5` 可构建对应版本。

社区构建关闭 DuckOMO 开发用 native/fixture 工具，不依赖私有服务或凭据。CMake 的 `DUCKOMO_BUILD_DEVELOPER_TOOLS` 默认 OFF，本地 Make 和固定矩阵显式启用。SQLLogicTests 仍通过 `extension_config.cmake` 的 `TEST_DIR` / `LOAD_TESTS` 登记。

根目录的 `vcpkg.json` 提供标准工具链所用的空依赖 manifest。OM C 来自固定子模块；HTTPFS 是运行时官方包，不通过 vcpkg 或配套源码编译。社区 CI 自行配置标准 vcpkg，无需使用者提供 `DUCKOMO_VCPKG_ROOT`。

## CI 与本地复现

[`community-build.yml`](../.github/workflows/community-build.yml) 调用与社区相同的 `v1.5-variegata` 分发工作流，三版本各自构建 `linux_arm64`，vcpkg pin 跟随社区配置。关闭 reduced CI，防止唯一的 ARM64 目标被过滤。

所核对的上游分发工作流跳过 Linux ARM64 测试。因此增加原生 `ubuntu-24.04-arm` job，下载刚构建的 DuckOMO 产物，用 hash 固定的实际官方 CLI 与 HTTPFS 验证：

- 本地 OM 固定样本返回 6 行、和为 15。
- 时间/member 局部条件的 HTTP 与本地结果逐行相同，共 4 行。
- HTTP 服务实际收到 Range 请求，发送字节数小于完整对象。

该检查使用自带公开 fixture 和临时 localhost HTTP 服务，无需 S3 密钥。它不替代已有的 SQL/native、HTTPS/S3、取消、并发和真实样本验收。现有三版本官方 HTTPFS 验证记录保留在 [迁移证据](../specs/004-multi-grid-selection/evidence/official-httpfs-refactor/status.md)；其 build ID/hash 对应当时冻结的源码，本次 CI 配置的检查另行记录。

本地运行同一个产物检查：

```sh
python3 scripts/verify-community-artifact.py --version v1.5.6 \
  --duckdb build/official-matrix/v1.5.6/official/duckdb \
  --httpfs build/official-matrix/v1.5.6/official/httpfs.duckdb_extension \
  --extension build/official-matrix/v1.5.6/release/extension/duckomo/duckomo.duckdb_extension \
  --output build/community-check/v1.5.6.json
```

完整压缩审计已移到忽略的 `build/community-preparation/audit/`，摘要和 manifest 保留路径及 hash。公开源码保留文本证据与小型 OM 测试输入。重新克隆时不附带旧服务日志；重跑记录的命令可生成新的审计。

## 提交登记

先完成源码提交并推送到公开的 `chorust/duckomo`，确认 CI 成功。当前工作树尚未发布，草案中的 `@DUCKOMO_SOURCE_REF@` 故意不填旧 HEAD，防止社区构建到不含本轮修改的源码。提交 SHA 固定后，在仓库根目录生成登记文件：

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess
ref = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
template = Path('community/duckomo/description.yml.in').read_text()
output = Path('build/community-submission/extensions/duckomo/description.yml')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(template.replace('@DUCKOMO_SOURCE_REF@', ref))
print(output)
PY
```

将生成文件加入 community-extensions 仓库的 PR，等待维护者审查、社区构建、签名和发布。维护更新时改 `repo.ref`；未来不兼容的新引擎适配可使用 `repo.ref_next`，此次不声明 DuckDB 2.0 支持。

社区签名发布/完整验收声明前仍需完成已有的 R21 独立复现条件。GitHub 开发发行包可按 tag 流程供试用，建议未完成验收时使用 prerelease；包发布不代表 R21 通过。004 的真实网格、完整内存账及 H0–H9 未完成部分保持原状态；登记描述不把投影/Gaussian 的实现等同于生产验收完成。

## 核对来源（2026-10-08）

- [官方开发与登记说明](https://duckdb.org/community_extensions/development)
- [官方发布流程：构建、签名和分发](https://duckdb.org/2024/07/05/community-extensions)
- [apart 登记示例](https://github.com/duckdb/community-extensions/blob/main/extensions/apart/description.yml)
- [社区构建工作流](https://github.com/duckdb/community-extensions/blob/cce4b98b34256fc465d259153049fa5b557e1758/.github/workflows/build.yml)，默认 v1.5.6
- [登记解析器](https://github.com/duckdb/community-extensions/blob/cce4b98b34256fc465d259153049fa5b557e1758/scripts/build.py)
- [分发工具链](https://github.com/duckdb/extension-ci-tools/blob/3fd6109fc01555673b7c7123eba8d0d143fe8ce4/.github/workflows/_extension_distribution.yml) 与同提交的平台矩阵

上游版本、平台集合和工具链会变化；扩展升级时重新核对，尤其是 ARM64 测试跳过策略与 `excluded_platforms` 集合。
