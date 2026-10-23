# Contract: 发布与 CI

## 日常 CI（`.github/workflows/community-build.yml`）

- 两个可复用构建 job：
  - `build-1-5`：`duckdb/extension-ci-tools/.github/workflows/_extension_distribution.yml@v1.5-variegata`，matrix `v1.5.4, v1.5.5, v1.5.6`。
  - `build-2-0`：同一工作流 `@v2.0-cyanoptera`，`duckdb_version: v2.0.0`，`vcpkg_commit: cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3`。
- 两者只构建 `linux_arm64`（exclude 列表按各自平台集合书写）。
- `arm64-runtime` 依赖两者，matrix 四个版本，各自用匹配官方 CLI/HTTPFS 运行 `verify-community-artifact.py`。
- 任一版本失败即 workflow 失败。

## Tag 发布（`.github/workflows/release.yml`）

- `prepare` 从 `test/data/release-runtimes.json` 输出两组版本：1.5 与 2.0。
- 1.5 构建 pin `3fd6109fc01555673b7c7123eba8d0d143fe8ce4`；2.0 构建 pin `b8d325c37e82160886579880707493bd358c0e62`（或实现时冻结的 `v2.0-cyanoptera` 提交）。
- 平台：linux_amd64、linux_arm64、osx_amd64、osx_arm64；2.0 传 `opt_in_archs: osx_amd64`。
- 每个 (版本, 平台) 产物在匹配运行时上验证后才进入发布；发布资产数 = 版本数 × 平台数（本特性完成后 4 × 4 = 16）。
- 资产命名沿用现有规则，版本段为 `v2.0.0`。

## 清单与脚本约定

- `release-runtimes.json` 的 `versions` 加入 `v2.0.0`；每条 runtime 的 `cli_url` 可为 `duckdb_cli-<plat>.zip`（1.5）或 `duckdb-cli-<plat>.tar.gz`（2.0），由 `release_tools.py` 按版本校验。
- `verify-community-artifact.py --version` 接受 `v2.0.0`。
- `test/data/grids/version-matrix.json` 不变（仍恰好三版 1.5）。

## 加载失败行为

不匹配引擎版本的产物由 DuckDB 标准扩展版本校验拒绝，错误信息来自引擎；DuckOMO 不添加自定义握手。
