# Data Model: DuckDB 2.0 正式支持、远程读取请求合并与异步 I/O

实体对应 [spec.md](spec.md) 的 Key Entities。字段为逻辑字段，具体存储位置见括注。

## EngineTarget（版本清单条目）

存放：`test/data/engine-compatibility/version-matrix.json`（2.0）、`test/data/grids/version-matrix.json`（1.5.x，不变）、`test/data/release-runtimes.json`（发布）。

| 字段 | 说明 | 校验 |
|---|---|---|
| `pair_id` | 如 `v2.0.0` | 匹配 `^v[0-9]+\.[0-9]+\.[0-9]+$`；清单内唯一 |
| `release_state` | `evaluation` → `target` | 只有 `target` 进入发布 |
| `duckdb.commit` / `version` | 引擎 tag 与 40 位 SHA | 与官方 tag 一致 |
| `extension_ci_tools.commit` | 本地构建身份中的工具链子模块提交 | 必须等于本地 `extension-ci-tools` 子模块 HEAD（构建脚本强制校验） |
| `ci_toolchain`（可选） | CI/发布使用的工具链 `ref`、`commit`、`vcpkg_commit` | 2.0：`v2.0-cyanoptera` / `b8d325c3…` / `cd61e1e2…`；SHA 为 40 位十六进制；不进入 build_id |
| `platforms[]` | 平台名、runner、是否 opt-in | 2.0 的 `osx_amd64` 标记 opt-in |
| `official_runtime[platform]` | CLI/HTTPFS URL 与解压后 SHA256 | CLI URL 允许 `.zip`（1.5）或 `.tar.gz`（2.0）；SHA 为 64 位十六进制 |

状态迁移：`evaluation` →（通过全部门禁）→ `target`。回退到 `evaluation` 时从发布矩阵移除。

## ReleaseArtifact

存放：GitHub release 资产与 `build/official-matrix/<pair>/build-manifest.json`。

| 字段 | 说明 |
|---|---|
| `engine_target` | 指向 EngineTarget（pair_id + platform） |
| `file` / `sha256` | 扩展文件与摘要 |
| `build_id` | 源码输入计算出的构建身份 |
| `verification` | 匹配运行时上的加载与运行时检查结果（pass/fail + 证据路径） |

约束：一个产物只对应一个 (pair_id, platform)；验证未通过的产物不得发布。

## ReadBudget（会话设置）

见 [contracts/settings.md](contracts/settings.md)。

| 字段 | 默认 | 作用域 |
|---|---|---|
| `remote_merge` | true | 远程读取 |
| `remote_merge_gap` | 待校准（候选 64 KiB） | 远程读取 |
| `remote_max_request` | 待校准（候选 4 MiB） | 远程读取 |
| `async_io` | 2.0 上 true；1.5.x 不生效 | 远程读取 |
| `async_max_inflight` | 8（每 worker） | 远程读取 |

校验：`remote_max_request ≥ 512` 且 ≥ 对象 LUT chunk 长度；`remote_merge_gap ≥ 0`；`async_max_inflight ∈ [1, 256]`。非法值在 `SET` 时报错。本地文件忽略全部字段。

## ReadPlan（扫描期内部状态，每 worker）

| 字段 | 说明 |
|---|---|
| `index_range` | 当前 index LUT 读取范围 |
| `data_requests[]` | 合并后的 (offset, length)，每项带有用字节数 |
| `state` | `planned` → `submitted` → `fetched` → `decoded`；任一阶段可转 `failed` 或 `cancelled` |
| `buffers` | 每个请求的私有缓冲，计入查询内存账本 |

状态规则：`submitted` 后函数返回空 chunk；恢复调用若处于 `fetched` 则先检查取消再解码；`failed`/`cancelled` 时释放所有缓冲且不得再提交。

## ScanMetrics 扩展

在现有 `duckomo_last_scan_metrics()` JSON 中新增（见 [contracts/metrics.md](contracts/metrics.md)）：

| 字段 | 说明 |
|---|---|
| `remote_io.merge_enabled` / `merge_gap_bytes` / `max_request_bytes` | 本次生效配置 |
| `remote_io.useful_bytes` | 选择所需 chunk 的压缩字节之和（取自已解压 LUT）加 index/metadata/coordinate 请求字节；远程扫描必填 |
| `remote_io.gap_bytes` | 物理字节 − 有用字节；远程扫描必填 |
| `remote_io.inflight_budget_bytes` | 异步路径本次扫描中生效的最小在途字节上限；同步路径为 NULL |
| `remote_io.async_applied` | 是否走异步路径 |
| `remote_io.inflight_peak` | 单 worker 同时在途请求峰值 |
| `remote_io.phase_requests` | index/data/metadata/coordinate 各自请求数（已有分桶，统一输出） |
