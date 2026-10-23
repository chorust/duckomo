# Research: DuckDB 2.0 正式支持、远程读取请求合并与异步 I/O

依据：三份只读调研（OM 读模式、2.0 异步 API、CI/发布路径，2026-10-23，模型 grok-4.7 medium）与 [2.0 评估](../../docs/duckdb-2.0-evaluation.md)。上游源码 `build/official-matrix/v2.0.0/source/duckdb`（tag v2.0.0 `d54cbbe3df`）。行号为调研时快照。

## R1. 2.0 构建与 API 兼容

- **Decision**: 沿用 `src/compat/duckdb_api.{hpp,cpp}` 以 `DUCKOMO_DUCKDB_API_GENERATION` 分支，一份源码同时编译 1.5.x 与 2.0；把评估阶段已验证的改动（函数签名/catalog、Identifier 大小写、`SetChildCardinality`、native 测试 `Fetch`/静态注册/`link_extension_libraries`、CLI tar.gz）作为本特性第一批提交。
- **Rationale**: 已在 1.5.4/1.5.6/2.0 通过 21 SQL 用例、48 项远程与 native 检查；分支数量可控。
- **Alternatives**: 独立 2.0 分支（维护双份代码，拒绝）；只支持 2.0（违反"继续发布 1.5.x"假设，拒绝）。
- **补充**：`read_om.cpp` 其余 `SetCardinality` 调用点（约 2956、3027–3048、3337、3348、3673 行与 `raw_scan.cpp`）仍是 deprecated 写法，需逐个改用兼容包装，避免同类子向量长度问题。

## R2. 版本清单与 CI 工具链

- **Decision**:
  - 2.0 pair 继续放在独立清单 `test/data/engine-compatibility/version-matrix.json`，升级 `release_state` 为 `target`。该 pair 的 `extension_ci_tools.commit` 保持等于本地子模块 HEAD（`b777c70…`）：`scripts/version_matrix.py` `build_pair` 要求二者相等，而本地 CMake 构建只把它计入构建身份、不使用其 workflow。CI 工具链另记在新增可选字段 `ci_toolchain`（ref `v2.0-cyanoptera`、commit `b8d325c37e82160886579880707493bd358c0e62`、vcpkg `cd61e1e2…`），不进入 build_id。不并入 `test/data/grids/version-matrix.json`（`audit-grid-version-matrix.py`、`fetch-region-runtime.py`、`version_matrix_test.py` 都断言其恰好为三版 1.5）。
  - `community-build.yml` 与 `release.yml` 各拆成 1.5 job（`v1.5-variegata` / pin `3fd6109…`）与 2.0 job（pin `b8d325c3…`），verify/publish 依赖两者。原因：`workflow_call` 的 `uses:` 不能矩阵化。
  - 2.0 vcpkg commit `cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3`（community-extensions main 对 2.0 的取值）。
  - 发布平台沿用 1.5.6：linux_amd64、linux_arm64、osx_amd64、osx_arm64；2.0 中 `osx_amd64` 是 opt-in，必须传 `opt_in_archs: osx_amd64`。2.0 矩阵无 `windows_amd64_rtools`，exclude 列表按 2.0 平台名重写。
  - `test/data/release-runtimes.json` 增加 v2.0.0 × 4 平台；`scripts/release_tools.py` 按版本接受 `duckdb-cli-<platform>.tar.gz`；`scripts/verify-community-artifact.py` 的 `--version` choices 与 `scripts/build-version.sh` 位置参数正则加入 v2.0.0。
  - 其余 3 个平台的官方 CLI/HTTPFS SHA256 由 `fetch-runtime` 首次下载后冻结进清单。
- **Rationale**: 与社区工具链一致；不打破 1.5 清单的既有断言。
- **Alternatives**: 跟随浮动分支名 `v2.0-cyanoptera`（发布不可复现，仅日常 CI 可接受；发布用 pin）；2.0 单独 workflow 文件（可行，但 verify/publish 汇总更复杂）。
- **未决风险**：社区 main 注释仍写 alpha；2.0 `osx_amd64` 在 `macos-15` 交叉构建，与本仓库 `macos-15-intel` verify runner 的兼容性需 CI 实跑确认；2.0 是否仍跳过 linux_arm64 测试以首次 CI 日志为准。社区登记（`community/duckomo/description.yml.in`）是否切到 2.0 属发布决策，计划中默认**不改登记**，只在 release 产出 2.0 资产。

## R3. 请求次数来源与合并

- **事实**：`DecodeSelection` 默认 `io_size_merge=512`、`io_size_max=65536`（`src/include/duckomo/reader.hpp:54-64`），read_om 全扫调用点未覆盖（`src/scan/read_om.cpp:3529-3533`）。官方 OM C 解码器按"间隙 ≤ io_size_merge 且总长 ≤ io_size_max"合并相邻 chunk（`om_decoder.c:470-477, 640`）。v3 LUT 以 64 个地址为一块。实测平均 356 字节/请求，与"间隙常大于 512，几乎不合并"一致（推断）。
- **Decision**: 合并在解码器规划层完成：为**远程**读取提供两个会话设置并传给 `DecodeSelection` 与元数据/坐标读取：
  - `duckomo_remote_merge_gap`（字节，默认待校准，候选 64 KiB；`0` 或关闭开关时退回 512）
  - `duckomo_remote_max_request`（字节，默认待校准，候选 4 MiB；下限 512，且需 ≥ LUT chunk 长度）
  - `duckomo_remote_merge`（BOOLEAN，默认 true；false 时使用 512/65536，复现当前基线）
  本地文件始终使用 512/65536。
- **Rationale**: 不新增合并层，复用官方规划器的正确性；改动面小；1.5.x 与 2.0 同时受益。
- **Alternatives**: 在 `ReadAtFile` 之上另建读计划合并器（可精确区分有用/间隙字节，但需提前枚举范围，而 data 范围要在 index 解压后才知道，复杂度高，暂不采用）。
- **默认值校准**：实现第一步先用现有分阶段指标拆分 11,592 次请求中 index/data 各占多少，再对 gap ∈ {512, 4K, 16K, 64K, 256K}、max ∈ {64K, 1M, 4M, 16M} 在 O1280 全扫与局部选择上扫参，取满足 SC-003/SC-004 的最小 gap。若 index 请求占主导，另需评估 index 读的合并（同一参数控制 `next_index_read`）。
- **读放大可观测（FR-011）**：解码器内合并后，逻辑与物理字节一起增大，因此必须另计"有用字节"。data 读的有用字节 = 本 batch 选择所需 chunk 的 [起点, 终点) 长度之和。chunk 边界来自已解压 LUT：解码器正是用相邻 chunk 的绝对偏移决定数据范围（`om_decoder.c:640`），且 index 缓冲在 reader 持有的 `OmDecoderState` 中（`om_reader.hpp:239-247`），故 reader 层可得。index/metadata/coordinate 读的有用字节等于其请求长度。该指标对远程扫描为必填，不设 NULL 退路。

## R4. 2.0 表函数异步接口

- **事实**：扫描回调签名不变；通过 `TableFunctionInput` 的 `async_result`、`results_execution_mode`、`interrupt_state` 进出。`AsyncResult::FromTasks(tasks, TaskSchedulerType::ASYNC)` + `data.HandleBlocked(result)`：TASK_EXECUTOR 模式返回 true 且调用方须以空 chunk 返回；SYNCHRONOUS 模式就地执行任务后返回 false。BLOCKED 时 output 必须为空。最后一个任务完成或抛错时恰好回调一次 `InterruptState`；`AsyncExecutionTask` 不感知查询取消。`table_scan.cpp:448-451` 与 `read_single_csv_file.cpp:397-404` 是非 MultiFile 范例。
- **Decision（阶段 A，批内并发）**：read_om 在每个 batch 中把读分为三段：
  1. **plan**：同步取得（或复用）index LUT，枚举当前 index 块内本 batch 需要的 data 范围（已含合并）。
  2. **fetch**：每个范围一个 `AsyncTask`，写入任务私有缓冲，`GetIOSize` 返回字节数，交给 ASYNC 池；`HandleBlocked` 后以空 chunk 返回，本地状态标记"I/O 已提交"。
  3. **decode**：恢复调用时检查取消，再用已取回缓冲驱动解码并输出 chunk。
  需要把 `OmV3Reader::DecodeSelection` 拆成"规划/喂入缓冲/解码"三步 API（当前严格交替 index→read→data→read→decode，`src/om/reader.cpp:370-435`）。
- **Decision（阶段 B，跨 batch 预取，可选）**：仅当阶段 A 未达 SC-006 时实施：每 worker 自建 `TaskExecutor(ASYNC)` 与 `ManagedAsyncMemoryGovernor`，在解码当前 batch 时预取下一 batch，析构 `CancelAndDrain`，用无任务 BLOCKED 停车。
- **Rationale**: 阶段 A 只依赖公开的表函数协议，生命周期简单（一次提交、一次恢复）；阶段 B 才引入内存治理与流水线。
- **Alternatives**: 伪装成 MultiFile 读取器以复用 `ScanReadAhead`（单对象语义不符、改动大，拒绝）；仅调大 `async_threads`（无效，read_om 不返回 async_result）。
- **1.5.x**：编译期关闭异步，始终同步路径；设置可读写但报告 `applied=false`。

## R5. 远程句柄并发安全

- **事实**：当前打开标志 `READ | DIRECT_IO | DISABLE_LOGGING` + `NO_CACHING`，无 `FILE_FLAGS_PARALLEL_ACCESS`（`src/om/remote_file.cpp:187-189`）。HTTPFS 在 `PARALLEL_ACCESS` 下 range 请求直写调用者缓冲、仅锁更新句柄字段；仅 DIRECT_IO 时不加锁更新字段，存在数据竞争。
- **Decision**: 2.0 异步路径对远程句柄追加 `FILE_FLAGS_PARALLEL_ACCESS`，一个 worker 一个句柄、多个在途范围；以 TSan/并发测试验证。若验证失败，改为每 worker 小型句柄池（每个在途槽一个句柄，各自校验对象身份）。
- **Rationale**: 与 DuckDB 自身并行读者（caching FS、CSV、数据库文件）一致，避免重复 HEAD 与身份校验。
- **未知**：HTTPFS `TryRangeRequest` 内部客户端缓存在 parallel 标志下是否有其他共享可变状态，需并发测试与 sanitizer 证实；本地文件路径不变（`pread` 本已安全）。

## R6. 内存与并发上限

- **Decision**: 阶段 A 就必须内存自适应（FR-013，不依赖条件阶段 B）。每 worker 每次提交前按缓冲管理器已用/上限计算剩余内存：在途字节上限 = `min(duckomo_async_max_inflight × duckomo_remote_max_request, 剩余内存 / (4 × worker 数))`；剩余内存低于 `memory_limit` 的 1/8 时在途请求降为 1，压力解除后恢复。在途字节计入现有 `ScanMemoryAccount`（新组件 `RemoteInflight`），当前上限输出到指标 `inflight_budget_bytes`。阶段 B 若实施，改用 `ManagedAsyncMemoryGovernor` 的预算作为同一上限。
- **Rationale**: 缓冲管理器用量在两版引擎都可读，实现简单，足以满足 US3 场景 2 与 SC-007；内存治理器仅在需要流水线预取时引入。
- **Alternatives**: 只用固定在途上限（不满足"内存紧张时收缩"，拒绝）；阶段 A 就引入内存治理器（需自建 TemporaryMemoryState 生命周期，归入阶段 B）。

## R7. 验证与基准

- **Decision**: 把一次性的 `build/duckdb-2-evaluation/real-benchmark.py` 提升为仓库脚本 `scripts/benchmark-remote-io.py`：参数化端点、对象清单、版本运行时、模式（merge on/off、async on/off）与查询集合（全扫、局部选择、坐标-only、polygon、nearest），断点续跑，输出中位数、请求数、发送字节、有用字节、峰值 RSS、在途峰值。受控 loopback 服务沿用 `scripts/setup-remote-fixtures.py`；真实云端点使用公开 Open-Meteo 对象（`s3://openmeteo/...`，见 `test/data/grids/sample-manifest.json`），只读、匿名访问。
- **Rationale**: 满足 FR-017/FR-018，且基准可在 CI 外复现。
