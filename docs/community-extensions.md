# DuckDB Community Extensions 接入

状态：**已收录并公开发布**。[社区扩展页](https://duckdb.org/community_extensions/extensions/duckomo) 提供 `INSTALL duckomo FROM community;`。上游登记见 [`duckdb/community-extensions` 的 `extensions/duckomo/description.yml`](https://github.com/duckdb/community-extensions/blob/main/extensions/duckomo/description.yml)：版本 **`0.3.5-rc.1`**，维护者 **`blizhan`**，源码 `repo.ref` **`55d66f3fbd3199d7471b8cd569c43cc91a8cec7e`**（与本仓库历史提交一致，不必等于当前 `main` tip）。排除平台为 musl / Windows / Wasm；Linux glibc x86_64/ARM64 与 macOS Intel/Apple Silicon 在登记范围内。

本仓库 [GitHub tag 发布流程](releases.md) 仍独立提供开发/预发布产物（含 DuckDB 1.5.4–1.5.6 与 2.0.0）。社区页说明：2.0.0 兼容与四平台产物已验证；异步远程 I/O 仍为实验性且无稳定加速承诺；网格验收限于文档化对象，不外推全部 OM。本地 [`community/duckomo/description.yml.in`](../community/duckomo/description.yml.in) 与 [`community/validation.json`](../community/validation.json) 已于 2026-11-04 按已发布登记回填（`0.3.5-rc.1` / `blizhan` / ref `55d66f3…` / `published-in-community`）；`validation.json` 内仍嵌套保留 2026-10-08 筹备快照供审计。社区产物 hash 未在本轮重新拉取（见 validation TODO）。

GitHub 开发包仍未签名；社区签名产物正常加载不需要 `-unsigned`。收录不等于 Spec 008 性能门禁完成，也不扩大超出文档范围的网格验收声明。

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

社区签名产物正常加载不需要 `-unsigned`。本地构建和 GitHub Actions 构建检查的产物仍未签名，开发验证才使用 `-unsigned`。`INSTALL duckomo FROM community` 已可用于公开安装；它证明社区分发可用，不单独充当 Spec 门禁或全网格验收证据。

DuckOMO 已移除配套 HTTPFS capability、补丁和源码子模块，远程入口使用官方 FileSystem API。自有缓存的移除属于复杂度取舍，并非社区收录要求；重复查询的网络读取代价见 [官方 HTTPFS 说明](official-httpfs.md)。

## 版本与首发平台

| 项目 | 范围 |
|---|---|
| 已发布社区版本 | `0.3.5-rc.1`（预发布标识） |
| 已发布源码 ref | `55d66f3fbd3199d7471b8cd569c43cc91a8cec7e` |
| 登记平台 | Linux glibc x86_64/ARM64、macOS Intel/Apple Silicon（见上游 `excluded_platforms`） |
| 仍排除 | musl、Windows、Wasm |
| 源码兼容验证 | 仍覆盖 v1.5.4、v1.5.5、v1.5.6；社区页亦列 2.0.0 |
| 开发子模块 | 本地开发树仍可固定 v1.5.4；社区/发布 CI 按目标版本切换 |

平台范围以已发布 `description.yml` 的 `excluded_platforms` 为准。新增平台要先构建并验证，再改上游登记。仓库内 `description.yml.in` 已与上游已发布登记对齐；后续升级时再改 version / repo.ref 后提交 community-extensions PR。

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

后续升级社区登记时：先完成源码提交并推送到公开的 `chorust/duckomo`，确认相关 CI 成功。设置目标社区版本；脚本会从当前 checkout 读取待登记的源码 commit。模板把 `@DUCKOMO_VERSION@` 和 `@DUCKOMO_SOURCE_REF@` 放在 YAML 的实际字段中。生成时用 PyYAML 解析结果，并断言 `extension.version` 与 `repo.ref` 都等于本次目标值，再提交生成文件：

```sh
# Example only: set the community version selected for this PR.
DUCKOMO_COMMUNITY_VERSION=0.3.5-rc.2 python3 - <<'PY'
from pathlib import Path
import os
import subprocess
import yaml

ref = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
version = os.environ['DUCKOMO_COMMUNITY_VERSION']
template = Path('community/duckomo/description.yml.in').read_text()
assert template.count('@DUCKOMO_SOURCE_REF@') == 1
assert template.count('@DUCKOMO_VERSION@') == 1
rendered = template.replace('@DUCKOMO_SOURCE_REF@', ref).replace('@DUCKOMO_VERSION@', version)
assert '@DUCKOMO_' not in rendered
descriptor = yaml.safe_load(rendered)
assert descriptor['repo']['ref'] == ref
assert descriptor['extension']['version'] == version
output = Path('build/community-submission/extensions/duckomo/description.yml')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(rendered)
print(f'{output}: version={version} repo.ref={ref}')
PY
```

将生成文件以 PR 更新 [community-extensions](https://github.com/duckdb/community-extensions) 中已有的 `extensions/duckomo/description.yml`，等待维护者审查、社区构建、签名和发布。未来不兼容的新引擎适配可使用 `repo.ref_next`。`description.yml.in` 的版本/ref 占位符必须位于 YAML 字段本身；其余登记字段仍需与上游核对。DuckDB 2.0 开发包仍可由本仓库 GitHub tag release 矩阵提供。构建 CI 与发布 CI 的 2.0 工具链 pin（如 `v2.0-cyanoptera`）服务于本仓库矩阵，不等于社区页上的全部可用引擎组合。

社区已签名发布，但**不**自动关闭本仓库未完成的 Spec 门禁或历史独立复现条件。GitHub 开发发行包仍可按 tag 流程供试用；预发布标识应保留到稳定版 tag。登记描述不把投影/Gaussian 或异步性能目标等同于已全部验收完成。

## 核对来源

### 已发布登记（2026-11-04 核对）

- [社区扩展页 duckomo](https://duckdb.org/community_extensions/extensions/duckomo)
- [已发布 description.yml](https://github.com/duckdb/community-extensions/blob/main/extensions/duckomo/description.yml)（`0.3.5-rc.1` / `blizhan` / ref `55d66f3…`）

### 筹备期快照（2026-10-08，历史）

- [官方开发与登记说明](https://duckdb.org/community_extensions/development)
- [官方发布流程：构建、签名和分发](https://duckdb.org/2024/07/05/community-extensions)
- [apart 登记示例](https://github.com/duckdb/community-extensions/blob/main/extensions/apart/description.yml)
- [社区构建工作流](https://github.com/duckdb/community-extensions/blob/cce4b98b34256fc465d259153049fa5b557e1758/.github/workflows/build.yml)，默认 v1.5.6
- [登记解析器](https://github.com/duckdb/community-extensions/blob/cce4b98b34256fc465d259153049fa5b557e1758/scripts/build.py)
- [分发工具链](https://github.com/duckdb/extension-ci-tools/blob/3fd6109fc01555673b7c7123eba8d0d143fe8ce4/.github/workflows/_extension_distribution.yml) 与同提交的平台矩阵

上游版本、平台集合和工具链会变化；扩展升级时重新核对，尤其是 ARM64 测试跳过策略与 `excluded_platforms` 集合。
