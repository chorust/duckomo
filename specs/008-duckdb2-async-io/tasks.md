---

description: "Task list for DuckDB 2.0 support, remote request merging and async I/O"
---

# Tasks: DuckDB 2.0 正式支持、远程读取请求合并与异步 I/O

**Input**: Design documents from `specs/008-duckdb2-async-io/`

**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: 规格的验收场景、FR-002/FR-017 与 plan 明确要求回归、远程验收、native 与基准证据，因此包含测试任务。

**Organization**: 按用户故事分组；US1 = 2.0 支持与发布（P1），US2 = 请求合并（P2），US3 = 2.0 异步 I/O（P3）。

## Format: `[ID] [P?] [Story] Description`

- **[P]**: 可并行（不同文件、无未完成依赖）
- **[Story]**: 所属用户故事
- 路径均相对仓库根 `/home/blizhan/repo/github/duckomo`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: 固定已验证的 2.0 兼容基线，作为所有故事的起点

- [ ] T001 审查并提交评估期未提交改动（`scripts/version_matrix.py`、`src/scan/read_om.cpp`、`src/scan/region_geometry.cpp`、`src/include/duckomo/region_geometry.hpp`、`src/scan/region_input.cpp`、`test/sql/region_sampling.test`、`test/native/official_httpfs_test.cpp`、`test/CMakeLists.txt`、`scripts/benchmark-engine-versions.py`、`docs/duckdb-2.0-evaluation.md`、`evidence/duckdb-2.0/`、`test/data/engine-compatibility/version-matrix.json`）；排除 `build/` 与 `.specify/extensions/roadmap` 未跟踪内容
- [ ] T002 保持 `test/data/engine-compatibility/version-matrix.json` 中 v2.0.0 pair 的 `extension_ci_tools.commit` 等于本地子模块 HEAD（当前 `b777c70d30942cca5bef62d6d4fa23a13362f398`；`scripts/version_matrix.py` `build_pair` 约 279–281 行要求二者相等，本地 CMake 构建不使用该仓库的 workflow）。为该 pair 新增独立字段 `ci_toolchain: {ref: "v2.0-cyanoptera", commit: "b8d325c37e82160886579880707493bd358c0e62", vcpkg_commit: "cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3"}`，仅供 CI/发布引用，不进入 `input_payload`/build_id；在 `scripts/version_matrix.py` 的清单校验中允许并校验该可选字段（40 位 SHA）。`release_state` 保持 `evaluation`，直至 T020 通过
- [ ] T003 [P] 在 `scripts/build-version.sh` 位置参数正则与用法字符串中加入 `v2.0.0`（默认清单路径对 v2.0.0 指向 `test/data/engine-compatibility/version-matrix.json`）

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: 让全部 native/SQL/sanitizer 目标在 1.5.x 与 2.0 上都可编译，这是三个故事共同的门禁基础

**⚠️ CRITICAL**: 未完成前不得开始故事实现

- [ ] T004 在 `src/include/duckomo/compat/duckdb_api.hpp` 与 `src/compat/duckdb_api.cpp` 新增 `SetOutputCardinality(DataChunk&, idx_t)` 包装（2.0 用 `SetChildCardinality`，1.x 用 `SetCardinality`），并替换 `src/scan/read_om.cpp`（约 2956、3027–3048、3337、3348、3566、3673 行）与 `src/scan/raw_scan.cpp`（145、177 行）全部输出 chunk 的调用；中间态 `coordinates` chunk 按其语义选择包装或保留
- [ ] T005 使 `test/native/*.cpp` 全部在 2.0 上编译：复用/扩展 `test/native/query_result_compat.hpp` 处理 `QueryResult::GetValue` 移除，把各测试中的 `ExtensionHelper::LoadAllExtensions` 定义改为 1.x 条件编译并在 2.0 用 `LoadStaticExtension<DuckomoExtension>()`；必要时在 `test/CMakeLists.txt` 为需要动态加载官方扩展的测试统一调用 `link_extension_libraries`
- [ ] T006 构建 2.0 完整开发目标（`scripts/build-version.sh --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0`，含 `unittest`、`duckomo_developer_tools`、`duckomo_sanitizer_checks`），修复编译错误直至全部目标链接成功
- [ ] T007 [P] 在 1.5.4、1.5.5、1.5.6 上用 `scripts/build-version.sh vX` 重建并运行 `scripts/validate.sh --matrix test/data/grids/version-matrix.json --pair vX --local-only`，确认 T004–T005 未引入回退；日志写入 `build/008/regress-vX/`

**Checkpoint**: 四个引擎版本全部目标可编译并通过本地验证

---

## Phase 3: User Story 1 - 在 DuckDB 2.0 上安装并使用 DuckOMO (Priority: P1) 🎯 MVP

**Goal**: 2.0 产物通过完整门禁，并由日常 CI 与 tag 发布产出

**Independent Test**: 从 CI dry-run 发布产物取得 2.0 扩展，在匹配官方运行时上通过 SQL、48 项受控远程、native 取消/隔离与 sanitizer；结果与 1.5.6 一致（见 [quickstart.md §1–2](quickstart.md)）

### Tests for User Story 1

- [ ] T008 [P] [US1] 在 `test/tools/release_tools_test.py` 增加用例：v2.0.0 runtime 的 `duckdb-cli-<plat>.tar.gz` URL 被接受、v1.5.x 仍要求 zip、错误版本/平台组合被拒绝；发布资产总数为版本数 × 平台数
- [ ] T009 [P] [US1] 在 `test/tools/` 新增或扩展测试，断言 `scripts/verify-community-artifact.py --version v2.0.0` 参数被接受，且 `test/data/grids/version-matrix.json` 仍恰好三版（`test/tools/version_matrix_test.py` 不变）

### Implementation for User Story 1

- [ ] T010 [US1] 修改 `scripts/release_tools.py`（约 46–48 行 CLI URL 规则）：按版本选择 zip 或 tar.gz；`matrix` 子命令输出分组 `versions_1_5` 与 `versions_2_0`（保留原 `versions` 字段兼容）
- [ ] T011 [P] [US1] 在 `scripts/verify-community-artifact.py` 的 `--version` choices 加入 `v2.0.0`，并让其从 `test/data/engine-compatibility/version-matrix.json` 读取 2.0 runtime
- [ ] T012 [US1] 为 linux_amd64、osx_amd64、osx_arm64 下载官方 v2.0.0 CLI（tar.gz）与 HTTPFS，计算解压后 SHA256；与已知 linux_arm64 值一起写入 `test/data/release-runtimes.json`（`versions` 加 `v2.0.0`，新增 4 条 runtime）
- [ ] T013 [US1] 重写 `.github/workflows/community-build.yml`：拆分 `build-1-5`（`@v1.5-variegata`，matrix 三版 1.5）与 `build-2-0`（`@v2.0-cyanoptera`，`duckdb_version: v2.0.0`，`vcpkg_commit: cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3`，exclude 按 2.0 平台集合只留 linux_arm64）；`arm64-runtime` needs 两者，matrix 四版本，2.0 runtime 走 engine-compatibility 清单
- [ ] T014 [US1] 修改 `.github/workflows/release.yml`：`build` 拆为 1.5（pin `3fd6109fc01555673b7c7123eba8d0d143fe8ce4`）与 2.0（pin `b8d325c37e82160886579880707493bd358c0e62`，vcpkg `cd61e1e2…`，`opt_in_archs: osx_amd64`，exclude 去掉 `windows_amd64_rtools`）两 job；verify/package/publish job 依赖两者并消费 T010 的分组输出；本地用 `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12` 校验两个 workflow
- [ ] T015 [US1] 运行 2.0 全量门禁：`unittest 'test/sql/*'`（官方 2.0 spatial）、`scripts/validate.sh --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0`（受控远程，含 `official_httpfs_test`）与 `duckomo_sanitizer_checks`；另做版本不匹配检查：官方 2.0 CLI 加载 1.5.6 产物、官方 1.5.6 CLI 加载 2.0 产物均须失败并给出引擎的版本不匹配错误、进程不崩溃（spec US1 场景 3）；证据写入 `evidence/duckdb-2.0/final/`，记录产物 SHA256、最终源码 build_id 与不匹配错误文本
- [ ] T016 [US1] 用 T015 产物与 1.5.6 产物对 `test/data/grids/sample-manifest.json` 中可用真实对象执行同查询结果哈希比对（复用 `scripts/benchmark-engine-versions.py` 或 T026 脚本的正确性模式），结果写入 `evidence/duckdb-2.0/final/cross-version.json`
- [ ] T017 [US1] 在分支上 `workflow_dispatch` 运行 `release.yml` dry-run 与 `community-build.yml`；确认 16 个产物全部 verify 通过、四版本 linux_arm64 runtime 检查通过；记录 run URL 到 `evidence/duckdb-2.0/final/ci.json`；若 2.0 osx_amd64 交叉构建失败，按 plan 风险条目记录并暂缺该平台
- [ ] T018 [P] [US1] 更新文档：`docs/releases.md`（16 份产物、删除"2.0 不在此流程"）、`docs/community-extensions.md`（2.0 工具链与 pin；说明社区登记 `community/duckomo/description.yml.in` 本特性不改、仍面向 1.5.6，2.0 产物由本仓库 tag 发布提供）、`docs/official-httpfs.md` 支持范围、`README.md` 与 `README.en.md` 发布矩阵
- [ ] T019 [P] [US1] 在 `docs/releases.md` 或发布说明模板中记录 2.0 上游行为差异：FLOAT→DECIMAL 精度修复（PR #24814）与 2.0 进程基线 RSS 增加约 20–29 MiB
- [ ] T020 [US1] T015–T017 全部通过后，将 `test/data/engine-compatibility/version-matrix.json` 中 v2.0.0 `release_state` 改为 `target`，并把 `docs/duckdb-2.0-evaluation.md` 状态更新为正式支持

**Checkpoint**: 2.0 可通过发布渠道获取并正常使用（SC-001、SC-002）

---

## Phase 4: User Story 2 - 远程读取时显著减少请求次数 (Priority: P2)

**Goal**: 远程读取合并相邻 chunk，请求数大幅下降，读放大受控且可观测；1.5.x 与 2.0 均适用

**Independent Test**: 受控远程上对 O1280 全扫与局部选择，比较 merge off/on 的请求数、发送字节、耗时与结果（见 [quickstart.md §3](quickstart.md)）

### Tests for User Story 2

- [ ] T021 [P] [US2] 新建 `test/sql/remote_io_settings.test`：按 [contracts/settings.md](contracts/settings.md) 验证三项合并设置的默认值、`SET`/`RESET`、非法值报错信息不含 URI，以及本地 `read_om` 在任意设置下结果不变
- [ ] T022 [P] [US2] 在 `test/native/remote_session_test.cpp` 增加用例：远程合并开启时 `remote_io.requests` 少于关闭时、结果相同；`duckomo_remote_max_request` 小于 LUT chunk 长度时报 `[configuration]`；LIMIT 与取消路径多读量不超过一个合并请求上限
- [ ] T023 [P] [US2] 在 `test/native/scan_metrics_v4_test.cpp` 增加 `remote_io` 对象字段与本地扫描为 NULL 的断言（[contracts/metrics.md](contracts/metrics.md)）

### Implementation for User Story 2

- [ ] T024 [US2] 在 `src/include/duckomo/metrics.hpp` 统一输出按阶段（metadata/coordinate/index/data）的成功请求数到 `remote_io.phase_requests`，并在受控 S3 上对 O1280 全扫运行一次，记录 index/data 请求占比到 `build/008/merge/phase-split.json`，据此确认是否同时放大 index 合并（research R3）
- [ ] T025 [US2] 在 `src/om_extension.cpp` 注册 `duckomo_remote_merge`、`duckomo_remote_merge_gap`、`duckomo_remote_max_request`（含校验函数，风格同 `duckomo_max_threads`）
- [ ] T026 [US2] 将 `build/duckdb-2-evaluation/real-benchmark.py` 提升为 `scripts/benchmark-remote-io.py`：参数化 runtimes、run.env、对象 id、`--modes`（merge/async 组合）、`--queries`（full、selected、coordinates、polygon、nearest）、重复轮数，断点续跑，输出中位数、请求数、发送字节、`remote_io` 指标与结果 SHA256
- [ ] T027 [US2] 在 `src/include/duckomo/reader.hpp` / `src/om/reader.cpp` 增加读取预算参数结构（merge gap、max request），`read_om` 在远程会话中把设置值传入 `DecodeSelection`（`src/scan/read_om.cpp` 约 3529 行调用点），本地保持 512/65536
- [ ] T028 [US2] 在 `src/om/metadata.cpp`（约 216 行时间坐标读取）及其他元数据/坐标读取处对远程对象使用同一读取预算
- [ ] T029 [US2] 在 `src/om/reader.cpp` 为每次远程 data 读计算有用字节：从已解压 LUT 取本次请求覆盖的每个 chunk 的 [起点, 终点)，仅累加本 batch 选择需要的 chunk 长度；index/metadata/coordinate 读的有用字节等于其请求长度。通过 `src/include/duckomo/metrics.hpp` 输出 `useful_bytes` 与 `gap_bytes = physical_bytes - useful_bytes`，远程扫描时二者必须非 NULL。在 `test/native/remote_session_test.cpp` 断言：merge off 时 `gap_bytes` 等于基线间隙（解码器 512 B 合并带来的值，可为 0 或小正数），merge on 时 `gap_bytes ≥` merge off，且 `useful_bytes` 两种模式相同
- [ ] T030 [US2] 用 T026 脚本在受控 S3/HTTP 上扫参 gap ∈ {512, 4K, 16K, 64K, 256K} × max ∈ {64K, 1M, 4M, 16M}（O1280 全扫与局部选择，1/4 workers），选满足 SC-003/SC-004 的最小 gap 与对应 max，写入 `src/om_extension.cpp` 默认值与 [contracts/settings.md](contracts/settings.md)；扫参结果保存为 `evidence/008/merge-sweep.json`
- [ ] T031 [US2] 运行 T021–T023 与完整 SQL/受控远程回归（1.5.6 与 2.0），并用 T026 在默认值下产出 merge on/off 对比与本地无回退结果（SC-003/004/005/009），保存为 `evidence/008/merge-final.json`

**Checkpoint**: US2 可独立交付；1.5.x 与 2.0 用户都获得请求合并

---

## Phase 5: User Story 3 - 在 DuckDB 2.0 上让远程读取并发在途 (Priority: P3)

**Goal**: 2.0 上 read_om 把合并后的远程范围作为异步任务并发获取，等待期间释放计算线程；内存受控，取消/错误与同步路径一致

**Independent Test**: 2.0 上同一远程查询 async off/on 对比耗时、`inflight_peak`、内存峰值与结果；内存受限与取消用例（见 [quickstart.md §4](quickstart.md)）

### Tests for User Story 3

- [ ] T032 [P] [US3] 新建 `test/native/remote_async_test.cpp`（仅 2.0 编译，加入 `test/CMakeLists.txt` 并链接 `link_extension_libraries`）：async on/off 结果一致、`inflight_peak > 1`、403/404/短读/版本变化在异步路径的错误类别与脱敏一致、取消后 1 秒内结束且指标为 `cancelled`、同连接恢复
- [ ] T033 [P] [US3] 在 `test/sql/remote_io_settings.test` 增加 `duckomo_async_io`/`duckomo_async_max_inflight` 默认值与范围校验；用 `require` 或版本分支区分 1.5.x 上 `async_applied=false`

### Implementation for User Story 3

- [ ] T034 [US3] 在 `src/om_extension.cpp` 注册 `duckomo_async_io`（2.0 默认 true，1.5.x 默认 false）与 `duckomo_async_max_inflight`（默认 8，范围 1–256）
- [ ] T035 [US3] 将 `src/om/reader.cpp` / `src/include/duckomo/reader.hpp` 的 `DecodeSelection` 拆为三段式 API：plan（同步取得 index LUT 并枚举本 batch 合并后的 data 请求）、fetch 输入（接受外部缓冲）、decode（用已取回缓冲解码）；同步 `DecodeSelection` 改为按顺序调用三段，保持现有行为与指标
- [ ] T036 [US3] 运行 Phase 2 全部回归确认 T035 重构在 1.5.x 与 2.0 同步路径无回退（结果哈希、请求数与 `evidence/008/merge-final.json` 一致）
- [ ] T037 [US3] 在 `src/include/duckomo/read_at_file.hpp` 与 `src/om/remote_file.cpp` 增加 2.0 条件编译的并发读取能力：打开远程句柄时追加 `FILE_FLAGS_PARALLEL_ACCESS`，提供可在多个任务中对不同范围并发调用的读取方法；保持身份校验、访问检查与错误脱敏
- [ ] T038 [US3] 在 `src/om/remote_file.cpp` 实现异步读取任务（`AsyncTask` 子类，`Execute` 写入任务私有缓冲、`GetIOSize` 返回字节数），在途字节计入 `ScanMemoryAccount` 新组件 `RemoteInflight`（`src/include/duckomo/metrics.hpp`）。实现每 worker 的在途预算：每次提交前用 DuckDB 缓冲管理器的 `GetUsedMemory()`/`GetMaxMemory()` 计算剩余内存，在途字节上限 = `min(duckomo_async_max_inflight × duckomo_remote_max_request, 剩余内存 / (4 × worker 数))`；剩余内存低于 `memory_limit` 的 1/8 时在途请求降为 1（退化为同步节奏），压力解除后恢复。当前生效的上限记入指标 `remote_io.inflight_budget_bytes`
- [ ] T039 [US3] 在 `src/scan/read_om.cpp` 的 `ScanReadOmImpl`（约 3346 行）接入：远程 + 2.0 + `duckomo_async_io` 时，plan 后按 `duckomo_async_max_inflight` 分批构造任务，`AsyncResult::FromTasks(..., TaskSchedulerType::ASYNC)` + `input.HandleBlocked`，返回空 chunk并在 local state 标记 submitted；恢复时先检查 `IsInterrupted`/`stop_requested` 再 decode；SYNCHRONOUS 模式就地执行后继续
- [ ] T040 [US3] 在 `src/scan/read_om.cpp` 与 `src/om/remote_file.cpp` 实现失败/取消清理：任一任务失败时保留首个分类错误、释放全部缓冲与账本、不再提交；记录 `remote_io.async_applied` 与 `inflight_peak`
- [ ] T041 [US3] 用 TSan（`duckomo_sanitizer_checks` 中新增 remote_async 目标）与 T032 并发用例验证 `PARALLEL_ACCESS` 单句柄并发安全；若失败，改为每 worker 句柄池（每在途槽一个句柄、各自身份校验）并重跑
- [ ] T042 [US3] 用 `scripts/benchmark-remote-io.py` 在 2.0 上比较 `merge=on:async=off` 与 `merge=on:async=on`（全部五类查询、1/4 workers、受控 HTTP/S3），并在 `SET memory_limit='256MB'` 下复测，确认 `remote_io.inflight_peak` 与 `inflight_budget_bytes` 随内存限制下降（对照默认内存运行）；结果保存 `evidence/008/async-a.json`，判定 SC-006/SC-007/SC-008
- [ ] T043 [US3] 条件任务（仅当 T042 未达 SC-006）：在 `src/scan/read_om.cpp` 为每 worker 增加跨 batch 预取：自建 `TaskExecutor(TaskSchedulerType::ASYNC)` 与 `ManagedAsyncMemoryGovernor`（其预算取代 T038 的缓冲管理器估算，作为预取与在途的共同上限），解码当前 batch 时提交下一 batch，无任务 BLOCKED 停车，析构 `CancelAndDrain`；重跑 T032、T041、T042 并保存 `evidence/008/async-b.json`

**Checkpoint**: 2.0 用户获得异步读取；1.5.x 行为不变

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T044 [P] 在至少一个真实云端点（`test/data/grids/sample-manifest.json` 公开 Open-Meteo 对象，匿名只读）上对 1.5.6 与 2.0、merge/async 各模式记录首读与重复读，注明客户端位置与端点区域，保存 `evidence/008/cloud.json`（FR-018）
- [ ] T045 [P] 更新 `docs/duckdb-2.0-evaluation.md` 与 `docs/architecture.md`：远程读取预算、异步路径、设置与指标说明；README 设置表加入新设置
- [ ] T046 [P] 更新 `.specify/memory/roadmap.md`：新增 008 条目（范围、门禁、证据路径），移除 004 中"2.0 退出"的过时描述
- [ ] T047 按 [quickstart.md](quickstart.md) 全流程复跑一次，确认所有命令可执行、期望值与证据一致；在 `specs/008-duckdb2-async-io/checklists/` 记录 SC-001–SC-009 达成情况

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)** → **Foundational (Phase 2)** → 用户故事
- **US1 (Phase 3)**：依赖 Phase 2
- **US2 (Phase 4)**：依赖 Phase 2；与 US1 的 CI/文档任务（T010–T019）可并行，不依赖 US1 完成
- **US3 (Phase 5)**：依赖 Phase 2 与 US2 的 T027（读取预算结构）、T026（基准脚本）；建议在 US2 完成后开始
- **Polish (Phase 6)**：T044 依赖 US2、US3；其余依赖各自相关故事

### Within Stories

- US1：T010/T011/T012 → T013/T014 → T015/T016 → T017 → T020；T018/T019 可随时并行
- US2：T024 → T025 → T027/T028 → T029 → T030 → T031；T026 可与 T025 并行
- US3：T034 → T035 → T036 → T037 → T038 → T039 → T040 → T041 → T042 →（条件）T043

### Parallel Opportunities

- Phase 1：T003 与 T001/T002 并行
- Phase 2：T007（三个 1.5 版本）彼此并行，并与 T006 并行
- US1：T008、T009、T011、T018、T019 并行；T013 与 T014 改不同 workflow 文件，可在 T010 后并行
- US2：T021、T022、T023 并行；T026 与 T025 并行
- US3：T032、T033 并行

## Parallel Example: User Story 1

```text
Task: "T008 [P] [US1] release_tools_test 增加 v2.0.0 tar.gz 用例"
Task: "T009 [P] [US1] verify-community-artifact v2.0.0 参数测试"
Task: "T011 [P] [US1] verify-community-artifact choices 加 v2.0.0"
Task: "T018 [P] [US1] 更新 releases/community/README 文档"
```

## Parallel Example: User Story 2

```text
Task: "T021 [P] [US2] test/sql/remote_io_settings.test"
Task: "T022 [P] [US2] remote_session_test 合并用例"
Task: "T023 [P] [US2] scan_metrics_v4_test remote_io 断言"
Task: "T026 [US2] scripts/benchmark-remote-io.py"
```

## Parallel Example: User Story 3

```text
Task: "T032 [P] [US3] test/native/remote_async_test.cpp"
Task: "T033 [P] [US3] remote_io_settings.test 异步设置用例"
```

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phase 1 + Phase 2（固定兼容基线、四版本全部目标可编译）
2. Phase 3（2.0 门禁、CI 拆分、dry-run 发布）
3. **STOP and VALIDATE**：16 个产物 verify 通过、2.0 与 1.5.6 结果一致 → 可发布带 2.0 产物的版本

### Incremental Delivery

1. MVP（US1）→ 发布：用户可在 2.0 使用 DuckOMO
2. US2 → 发布：所有版本远程读取请求数下降
3. US3 阶段 A → 发布：2.0 远程读取并发在途；必要时 T043 阶段 B

### Parallel Team Strategy

- 一人负责 US1 的 CI/发布（T010–T019），另一人同时推进 US2 的指标与合并（T021–T030）
- US3 在 US2 的读取预算结构合入后开始，由熟悉 reader/decoder 的人负责

## Notes

- 每项完成后提交；证据目录 `evidence/008/` 与 `evidence/duckdb-2.0/final/` 不含凭据
- 受控远程服务端口以私有 run.env 为准，脚本不得硬编码
- 默认值（T030）若无法同时满足 SC-003 与 SC-004，先更新 spec 的 SC 阈值并说明原因，再继续
