---

description: "Task list for Phase 4–5 dimension semantics, remote and parallel OM reads"
---

# Tasks: Phase 4–5 维度语义、远程与并行读取

**Input**: `specs/003-dimensions-remote-parallel/{spec.md,plan.md,research.md,data-model.md,quickstart.md,contracts/}`

**Prerequisites**: 现有 Phase 0–3、DuckDB v1.5.4、固定 OM C 依赖；Linux AArch64 为完整验收平台。

**Tests**: 规格 FR-019、SC-001–008 和 `contracts/validation-evidence.md` 明确要求正确性、故障及成本证据，因此下列测试和验收任务属于交付范围。无需按 TDD 顺序实施。

**Organization**: Setup 和 Foundational 建立共用依赖；随后按 P1/P2 用户故事实施。所有路径相对仓库根目录；`[P]` 仅表示在所列前置任务完成后可与同一阶段其他 `[P]` 任务并行。

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: 固定可复现的远程构建依赖和验收资产入口；不改变现有查询行为。

- [X] T001 在 `.gitmodules` 和 `third_party/duckdb-httpfs/` 固定 httpfs 上游提交 `c3f215ab360f04dc3d3d5305fa81849c0121f111`，记录取得源码的方式及其许可证。
- [X] T002 在 `third_party/httpfs-patches/README.md` 和 `third_party/httpfs-patches/manifest.json` 记录 DuckDB/httpfs 固定提交、补丁版本、hash、应用顺序与两个扩展的配套发布约束。
- [X] T003 [P] 扩展 `test/tools/duckomo_fixture_tool.cpp`，生成带独立原始值参考的 `dimensions.om`、轴顺序变体和 `dimensions_perf.om`，并在 `test/data/manifest.json` 固定 shape、chunk、hash 与样本身份。
- [X] T004 [P] 在 `test/data/dimensions-coordinates.csv` 和 `test/data/dimensions-perf-manifest.json` 保存独立于待实现映射/选择器的五类坐标参考、缺测位置、查询与预定性能条件；在 `test/data/domain-manifest.json` 核对真实 OM 的固定 hash。

**Checkpoint**: 上游版本、样本和独立参考在实施及计时前固定。

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: 先证明配套 httpfs 的共享 ABI 与严格范围事件可用，并为后续故事提供统一读取边界。

- [X] T005 在 `third_party/httpfs-patches/httpfs_om_range.hpp` 定义版本化 `HTTPFSOmRangeProviderV2`、逐 attempt 响应 observer、取消/版本策略及 capability 描述符；保持接口仅用于本次 range-session。
- [X] T006 在 `third_party/httpfs-patches/0001-om-range-session.patch` 实现 httpfs 签名前条件请求、禁用隐式完整下载/内部内容缓存/预取、逐 body 事件和严格 206/范围/版本检查；未启用 provider 的普通 httpfs 行为保持原样。
- [X] T007 在 `third_party/httpfs-patches/0002-om-capabilities.patch` 实现 `httpfs_om_range_capabilities()` 的 ABI、upstream commit、patch revision 输出和可由 duckomo catalog 读取的共享描述符。
- [X] T008 在 `extension_config.cmake`、`CMakeLists.txt` 和 `scripts/stage-httpfs.sh` 配置固定源码的 build 目录复制/打补丁/构建，复用上游 TLS、HTTP、S3 依赖，并产出配套 duckomo/httpfs 加载产物。
- [X] T009 [P] 在 `test/native/httpfs_abi_test.cpp` 和 `test/CMakeLists.txt` 验证共享 ABI 生命周期、静态与可加载扩展的 LOAD 顺序、capability 不匹配时的拒绝以及普通本地入口独立于 httpfs。
- [X] T010 [P] 在 `test/native/httpfs_range_test.cpp` 和 `test/CMakeLists.txt` 验证真实 observer 收到响应 body/attempt，且 200 回退、短读和版本变化失败；不使用应用层请求长度替代响应字节。
- [X] T011 在 `src/include/duckomo/reader.hpp`、`src/om/reader.cpp` 和 `src/om/local_file.cpp` 抽出带读取类别的受检 ReadAt/文件会话边界，保留官方 OM reader 的块选择与解码职责及现有本地行为。

**Checkpoint**: T009–T010 在固定构建上通过后，才允许开放远程入口；T011 后可开始各故事的读取实现。

---

## Phase 3: User Story 1 - 按时间、层次和预报身份理解数据 (Priority: P1) 🎯 MVP

**Goal**: 精确绑定五类语义坐标与原始轴，保留现有 `valid_time`、`valid_times` 和未映射查询行为。

**Independent Test**: G0/G1：对固定多变量样本的两种轴顺序，逐逻辑位置比较独立坐标和值参考；错误映射在出数前失败，未声明时旧 SQL 回归通过。

- [X] T012 [US1] 在 `src/include/duckomo/semantic_axes.hpp` 和 `src/scan/semantic_axes.cpp` 实现 `SemanticAxis`/`ConstantCoordinate` 强类型模型、受检 stride/轴长、regular 与 explicit values 存储及按逻辑位置取值。
- [X] T013 [US1] 在 `src/scan/semantic_axes.cpp` 严格解析 `axes` 的五个键和允许字段，校验轴身份、互斥输入形式、正轴长、长度、空间轴占用、重复语义及未知字段，错误指明语义和轴。
- [X] T014 [US1] 在 `src/scan/semantic_axes.cpp` 实现 UTC 微秒 time/run、无月 INTERVAL lead_time、有限且无损 DOUBLE level（kind/unit 封闭集合）和精确保留 BIGINT/VARCHAR member 的取值及溢出校验。
- [X] T015 [US1] 在 `src/om/metadata.cpp` 和 `src/scan/semantic_axes.cpp` 将现有 `time` 一维 Unix 秒/`valid_time` 标量证据与显式 time 逐位置核对，拒绝与 `valid_times` 同时声明或多变量证据冲突，不推断其他四类元数据。
- [X] T016 [US1] 在 `src/include/duckomo/schema.hpp` 和 `src/scan/schema.cpp` 给 `BoundSchema` 增加不可变语义映射和 typed `OutputColumn`，按值列、空间列、`valid_time`、level、lead_time、member、run 顺序建 schema 并按 DuckDB 标识符规则拒绝列名冲突。
- [X] T017 [US1] 在 `src/include/duckomo/projection.hpp` 和 `src/scan/projection.cpp` 以 typed output descriptor 取代固定坐标布尔位/偏移，确保值列投影、重排和语义列能共用正确的源索引。
- [X] T018 [US1] 在 `src/scan/read_om.cpp` 注册 `axes := ANY` 并于 bind 前完成 T013–T016 的布局/证据验证；无 axes 时保持原 `valid_time`、空间和多变量 schema。
- [X] T019 [US1] 在 `src/scan/read_om.cpp` 和 `src/scan/batch.cpp` 按 row-major 逻辑位置输出五类语义坐标，跨批次、单元素及重复坐标不去重或转置，保留未映射轴位置。
- [X] T020 [P] [US1] 在 `test/native/semantic_axes_test.cpp` 验证微秒/时区、负和零时长、level 无损转换、文本/大整数成员、非单调/单点/重复坐标以及非法映射拒绝。
- [X] T021 [P] [US1] 在 `test/sql/semantic_axes.test` 验证五类输出类型/列序、显式与文件 time 一致性、`valid_times`/快照兼容、多变量与列冲突绑定错误。
- [X] T022 [US1] 在 `test/tools/duckomo_dimensions_validation.cpp` 增加 G0/G1：使用 `test/data/dimensions-coordinates.csv` 和官方 reader 值参考逐逻辑位置、缺测及两种轴顺序比对，并输出可复查的差分。
- [X] T023 [US1] 在 `test/CMakeLists.txt` 注册 T020–T022 的 native、SQL、fixture/harness 目标和现有 `time_test.cpp` 回归入口。

**Checkpoint**: G0/G1 通过后，语义坐标可独立查询；仅做 US1 即为维度映射 MVP。

---

## Phase 4: User Story 2 - 联合筛选空间和其他维度 (Priority: P1)

**Goal**: 从安全的 typed 必要条件缩小候选逻辑位置，同时由 DuckDB 保留完整 WHERE，并使空/无值依赖查询保持零值 I/O。

**Independent Test**: G2：对全扫后独立过滤的基准做双向差分；检查局部值字节与解码块严格下降，空/坐标/count 的值索引、值数据、值解码均为零。

- [X] T024 [US2] 在 `src/include/duckomo/axis_filter.hpp` 和 `src/scan/axis_filter.cpp` 提取 time/run/lead_time/level 的类型相容常量 `= < <= > >= BETWEEN` 及 member 二进制等值，支持左右常量反转与安全 AND，保留完整 residual WHERE。
- [X] T025 [US2] 在 `src/scan/axis_filter.cpp` 对 OR/NOT、函数、转换、NULL、非二进制 collation、含月 INTERVAL 与不精确类型比较安全回退并记录具体原因，不从 OR 的单一分支删行。
- [X] T026 [US2] 在 `src/include/duckomo/axis_selection.hpp` 和 `src/scan/axis_selection.cpp` 实现 per-axis 合并半开区间、regular 受检比较及 explicit 坐标逐位置比较；重复/递减/非单调坐标保留全部逻辑位置，超过 65,536 区间时回退整轴。
- [X] T027 [US2] 在 `src/scan/axis_selection.cpp` 将各语义轴与 `src/scan/spatial_selection.cpp` 的展平 point 轴选择取交集，计算受检候选数与空选择，使用惰性 ordinal cursor 生成不跨连续轴的有界解码段。
- [X] T028 [US2] 在 `src/scan/read_om.cpp` 把 typed filter 回调、QuerySelection 与扫描接通；可证明空选择在创建值 reader 前返回，未知/回退条件交由完整 WHERE 精确过滤。
- [X] T029 [US2] 在 `src/scan/projection.cpp` 和 `src/scan/read_om.cpp` 合并输出列与 residual 过滤依赖，正确处理值过滤而不输出该列、表达式/聚合/列重排，并避免读取无关值变量。
- [X] T030 [US2] 在 `src/scan/read_om.cpp` 和 `src/om/reader.cpp` 增加仅坐标及无值依赖 count 的门禁，允许必要 metadata/coordinate 读取而不创建值 decoder 或读取值 index/data。
- [X] T031 [P] [US2] 在 `test/native/axis_selection_test.cpp` 验证开闭边界、矛盾条件、重复/非单调、碎片预算、展平空间交集及 ordinal cursor 对每个位置恰好覆盖一次。
- [X] T032 [P] [US2] 在 `test/sql/axis_filter.test` 验证单维/空间/值混合、OR/函数回退、NULL、输出重排、聚合、空选择与无值依赖 count 的结果。
- [X] T033 [US2] 在 `test/tools/duckomo_dimensions_validation.cpp` 增加 G2 full/restricted/empty/coordinate/count 的结果差分和按变量 index/data/decode 门禁，使用 `test/data/dimensions-perf-manifest.json` 的固定局部查询判定双重成本下降。
- [X] T034 [US2] 在 `test/CMakeLists.txt` 和 `scripts/validate.sh` 注册 G2/native/SQL 的无外部服务执行入口，并保留现有空间回归。

**Checkpoint**: G2 独立通过；局部筛选有正确性和真实值读取收益证据。

---

## Phase 5: User Story 3 - 直接查询远程 OM 文件 (Priority: P1)

**Goal**: 让同一 `read_om` 查询严格按需读取 HTTP(S)/S3 单对象，并在协议、权限或版本异常时失败。

**Independent Test**: G3：同一字节本地/HTTP/S3（含真实 OM）按逻辑位置和值及缺测比对；两远端冷局部响应 body 均少于同列全扫；故障后可成功重查。

- [X] T035 [US3] 在 `src/include/duckomo/remote_file.hpp` 和 `src/om/remote_file.cpp` 定义规范 URI、endpoint、访问分区、脱敏展示名、大小及强/弱版本的 `ObjectIdentity` 和查询级 `RemoteReadSession`。
- [X] T036 [US3] 在 `src/om/remote_file.cpp` 实现保留 ClientContext 凭据/设置/权限的 opener-provider、httpfs capability catalog 校验及远程缺配套构建时的明确拒绝；本地无需加载 httpfs。
- [X] T037 [US3] 在 `src/om/remote_file.cpp` 于每查询读取元数据前执行新鲜 HEAD 和 `bytes=0-0` GET 探测，要求正长度及精确范围权限；多个 worker 打开时核对绑定对象身份。
- [X] T038 [US3] 在 `src/om/remote_file.cpp` 实现受检 offset/length、206/Content-Range/Content-Length/body/identity 编码校验、条件版本请求与重定向边界；短/长读、超时、412 或 token 变化令整查询失败且错误脱敏。
- [X] T039 [US3] 在 `src/om/remote_file.cpp` 通过 observer 汇总每 attempt 实收 body、状态与请求数（含探测、重试、失败），保持 metadata、coordinate、value index/data 分类，不把请求长度算作网络字节。
- [X] T040 [US3] 在 `src/om/reader.cpp` 和 `src/scan/read_om.cpp` 将 HTTP(S)/S3 单 URI 送入统一 ReadAt 与官方 reader，维度、空间、投影及缺测路径沿用本地，`read_om_raw` 仍为本地入口。
- [X] T041 [P] [US3] 在 `scripts/setup-remote-fixtures.py` 实现仅绑定 loopback 的 HTTP range/故障服务、固定 S3-compatible 服务的 fixture 上传与响应审计代理编排，输出安全引用的 run.env、s3-setup.sql、日志目录及 `--stop`。
- [X] T042 [P] [US3] 在 `test/native/remote_session_test.cpp` 验证 HTTP/S3 会话身份、取消、范围和强版本协议错误，错误信息与事件均不包含密钥/签名。
- [X] T043 [US3] 在 `test/tools/duckomo_remote_validation.cpp` 实现 G3 的三来源固定样本/真实 OM 完整结果差分、HTTP/S3 服务端 body 交叉核对及 403/404/无 HEAD/200/错范围/短读/超时/替换故障恢复。
- [X] T044 [US3] 在 `test/CMakeLists.txt` 注册 T042–T043 和配套 httpfs 加载测试；缺远程服务、真实样本或审计日志时 G3 必须非零退出。

**Checkpoint**: G3 通过且网络计量由服务端证实；远程串行查询已可独立使用。

---

## Phase 6: User Story 4 - 并行完成大范围扫描 (Priority: P2)

**Goal**: 让 DuckDB 调度多个独立扫描任务，限制工作者上限，并在本地和远程保持结果、失败与取消语义。

**Independent Test**: G4：同一固定多块样本用 1/2/4 上限完整消费并按多重集合比较；任务无遗漏重复、实际至少两 worker 工作；固定五次 release 测量的并行中位数更低。

- [X] T045 [US4] 在 `src/scan/read_om.cpp` 注册连接级 `duckomo_max_threads`（0 为 DuckDB 上限，负数拒绝），实现 table function `MaxThreads`/`init_local` 并受可用任务数限制。
- [X] T046 [US4] 在 `src/include/duckomo/axis_selection.hpp` 和 `src/scan/axis_selection.cpp` 由候选 ordinal 构建默认 65,536 位置的惰性非重叠 `ScanTask` 窗口，不物化全域行或任务表。
- [X] T047 [US4] 在 `src/scan/read_om.cpp` 将 global state 改为不可变 schema/selection、加锁任务领取、共享停止/首错和计数；local state 持有独立句柄、decoder、稳定参数 vector、缓冲和游标。
- [X] T048 [US4] 在 `src/om/reader.cpp` 和 `src/scan/read_om.cpp` 让每个 worker 仅处理已领取窗口、在一批全部依赖列成功后提交输出；任务/请求/解码边界检查中断，失败后不再分配任务。
- [X] T049 [US4] 在 `src/scan/read_om.cpp` 实现 QueryEnd 与 local/global 析构的终止顺序、首错保留、资源释放和再次查询恢复；LIMIT 可成功但标记 scan 未完成。
- [X] T050 [P] [US4] 在 `test/native/parallel_scan_test.cpp` 覆盖任务少于上限、空选择、跨批次、实际 worker 身份、故障和取消后的再次有效查询。
- [X] T051 [P] [US4] 在 `test/sql/parallel_scan.test` 用 `EXCEPT ALL` 比较 1/2/4 上限的无序本地结果、坐标/值/NULL 与空/窄选择，不假设输出顺序。
- [X] T052 [US4] 在 `test/tools/duckomo_remote_validation.cpp` 扩展 G4 到本地/HTTP/S3 的任务覆盖及 1/2/4 工作者，并在固定 release 样本、相同缓存条件下各测五次、记录全部耗时及独立进程 RSS。
- [X] T053 [US4] 在 `test/CMakeLists.txt` 注册 T050–T052 与 sanitizer 并行生命周期入口；中位数未下降或实际 worker 不足时 G4 不通过。

**Checkpoint**: G4 证明任务真实并行且结果等价，性能收益有预先固定条件下的原始记录。

---

## Phase 7: User Story 5 - 复用读取并解释查询成本 (Priority: P2)

**Goal**: 在当前连接内有界复用已请求范围，并给每次查询提供可审计、隔离且不泄密的 v3 记录。

**Independent Test**: G5/G6：同连接冷/热/禁用/清理/淘汰结果相同且热缓存远程成本下降；版本/权限变化不命中旧数据；失败/取消/并发扫描记录隔离并与服务端日志核对。

- [X] T054 [US5] 在 `src/include/duckomo/range_cache.hpp` 和 `src/om/range_cache.cpp` 实现 ClientContextState 拥有的精确/包含范围 LRU、copy-out、先淘汰后分配及 payload/键/条目/索引统一 checked 容量计费；超额直读。
- [X] T055 [US5] 在 `src/om/range_cache.cpp` 以完整对象身份、强版本和盐化访问分区作 key；弱/无版本、本地首版或撤权查询不得跨查询复用，失败响应不得插入。
- [X] T056 [US5] 在 `src/scan/read_om.cpp` 注册会话级 `duckomo_cache_enabled`、`duckomo_cache_capacity` 与 `duckomo_clear_cache()`；关闭即清空、减容立即淘汰、0 禁存储，清理仅影响当前连接。
- [X] T057 [US5] 在 `src/om/reader.cpp` 和 `src/om/remote_file.cpp` 将缓存置于 OM reader 准确 ReadAt 与 range-session 之间，命中仍执行每查询 HEAD/范围授权探测，miss 不扩大或预取值范围。
- [X] T058 [US5] 在 `src/include/duckomo/metrics.hpp` 将 v2 扩为 v3 的 scan_id、coordinate、逻辑/底层/响应字节、缓存、任务/worker、候选/扫描行数、完整性、耗时与内存 scope；保留 v2 字段含义并在证据读取器分版。
- [X] T059 [US5] 在 `src/scan/read_om.cpp` 把 bind/scan metadata、坐标及各值变量的 index/data/decode、cache 和每 attempt 网络事件计入所属 scan；测量查询归属缓冲/decoder/selection 分配峰值及标为 process 的 RSS，失败/取消保留已发生成本与未知字段的 NULL。
- [X] T060 [US5] 在 `src/scan/read_om.cpp` 以 QueryEnd 唯一发布同一 SQL 的各 scan 最终状态和不可变快照，支持下游错误/LIMIT/多 scan/多连接隔离，析构只做未发布兜底与资源释放。
- [X] T061 [US5] 在 `src/scan/read_om.cpp` 实现 `duckomo_last_scan_metrics()` 每 scan 一行的 v3 JSON 查询；读取/清理函数不覆盖最近扫描记录，SQL/URI、凭据和签名脱敏。
- [X] T062 [P] [US5] 在 `test/native/range_cache_test.cpp` 验证精确/包含命中、并发 miss、容量上限/淘汰/单条旁路、禁用/清理、强弱版本及访问分区隔离。
- [X] T063 [P] [US5] 在 `test/native/scan_metrics_v3_test.cpp` 验证类别总量、每 worker/attempt 去重、失败完整性、LIMIT、QueryEnd 一次发布、双 scan/双连接隔离和脱敏。
- [X] T064 [US5] 在 `test/sql/cache_metrics.test` 验证三项会话设置、clear/last_scan_metrics 的行与类型契约、无扫描零行及错误后可读的终态。
- [X] T065 [US5] 在 `test/tools/duckomo_remote_validation.cpp` 扩展 G5/G6：同连接冷/热/禁用/清理、小容量/淘汰、等长替换/撤权/弱版本、失败/取消/并发，并核对服务器 body 与逐查询 v3 JSON。
- [X] T066 [US5] 在 `test/CMakeLists.txt` 和 `scripts/validate.sh` 注册 T062–T065、sanitizer 资源回收及本地 v3 回归入口。

**Checkpoint**: G5/G6 通过；缓存收益、访问隔离和每次查询成本均有独立证据。

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: 完整验收、文档和发布证据；仅在对应真实门禁通过后更新路线图状态。

- [X] T067 在 `README.md` 和 `README.en.md` 更新五类轴含义、UTC/单位与现有 valid_time 兼容、HTTP(S)/S3 配套加载、并行/缓存控制及最小可运行 SQL 示例。
- [X] T068 [P] 在 `docs/spec.md` 和 `specs/003-dimensions-remote-parallel/contracts/sql-interface.md` 同步最终字段/类型、拒绝规则、过滤回退、设置和指标函数的实际行为。
- [X] T069 [P] 在 `docs/architecture.md` 和 `specs/003-dimensions-remote-parallel/contracts/remote-io.md` 记录 httpfs 补丁 ABI、范围/版本/授权边界、global/local 生命周期和缓存所有权。
- [ ] T070 在 `scripts/setup-remote-fixtures.py` 和 `specs/003-dimensions-remote-parallel/quickstart.md` 核对四项可复现操作的实际命令、依赖 hash、服务启动/停止与预期结果，交给未参与实现者执行。
- [X] T071 在 `scripts/validate.sh` 和 `test/CMakeLists.txt` 串接 G0–G2 的 `make test`、关键 native ASan/UBSan，以及外部服务就绪时独立运行 G3–G6 的明确非零门禁。
- [X] T072 在 `evidence/003-dimensions-remote-parallel/final.md` 保存 G0–G7 的命令、退出码、固定样本/服务/构建身份、结果差分、服务器对账、五次计时及内存原始值，逐项对应 FR-001–020、SC-001–008；未执行项明确标未通过。
- [ ] T073 在 `docs/roadmap.md` 依据 T072 的真实 G0–G7 结果更新 Phase 4–5 状态；G7 需有未参与实现者的 quickstart 四项复现记录。

**Checkpoint**: 完整发布声明仅在全部 G0–G7、真实样本和独立复现证据齐备时成立。

---

## Dependencies & Execution Order

### Phase Dependencies

```text
Setup T001–T004
  → Foundational T005–T011 (T009/T010 先验证配套 ABI)
  → US1 T012–T023 (G0/G1，维度 MVP)
  → US2 T024–T034 (G2，依赖语义轴与投影)
  → US3 T035–T044 (G3，依赖统一读取/选择与配套 httpfs)
  → US4 T045–T053 (G4，依赖本地/远程串行扫描)
  → US5 T054–T066 (G5/G6，依赖远程会话与 worker 生命周期)
  → Polish T067–T073 (G7 与最终证据)
```

### User Story Dependencies

- **US1**: 依赖 Foundational；完成后即可独立交付维度映射 MVP。
- **US2**: 依赖 US1 的语义坐标、descriptor 和列依赖；G2 可用本地样本独立验收。
- **US3**: 远程会话开发可在 Foundational 后与 US1/US2 并行；G3 必须待 US2 的查询路径完成，再验证本地/远程相同语义。
- **US4**: 任务分配器可在 US2 后开发；本地和远程完整 G4 依赖 US3。
- **US5**: 范围缓存模型可在 US3 后开发；G5/G6 的并发观测与最终集成依赖 US4。

### Within Each User Story

- 先完成模型和接口，再接通 bind/scan；独立 oracle、SQL/native 测试与对应 gate 在故事结束前完成。
- 测试以 `contracts/sql-interface.md`、`contracts/remote-io.md`、`contracts/validation-evidence.md` 为判定依据；不以旧实现生成的结果或请求长度自证。
- 每个 gate 的结果和成本均从固定样本、完整消费结果及原始记录判定；失败门禁不写成已完成。

### Parallel Opportunities

- Setup：T003 的 OM 值 fixture 与 T004 的独立坐标/实验 manifest 可并行，合并时核对相同样本身份。
- Foundational：T009 ABI/LOAD 测试与 T010 范围/事件测试在 T005–T008 后可并行。
- US1：T020 native 轴边界与 T021 SQL 接口测试在 T012–T019 后可并行。
- US2：T031 选择游标 native 测试与 T032 SQL 组合测试在 T024–T030 后可并行。
- US3：T041 受控服务编排与 T042 会话 native 测试在 T035–T040 后可并行。
- US4：T050 任务生命周期与 T051 SQL 结果测试在 T045–T049 后可并行。
- US5：T062 缓存边界与 T063 指标隔离测试在 T054–T061 后可并行。
- Polish：T068 接口文档与 T069 架构文档在各自实现稳定后可并行。

### Parallel Example: User Story 1

```text
After T012–T019: T020 test/native/semantic_axes_test.cpp || T021 test/sql/semantic_axes.test
Then: T022 independent oracle → T023 register gates
```

### Parallel Example: User Story 2

```text
After T024–T030: T031 test/native/axis_selection_test.cpp || T032 test/sql/axis_filter.test
Then: T033 G2 evidence → T034 register gates
```

### Parallel Example: User Story 3

```text
After T035–T040: T041 scripts/setup-remote-fixtures.py || T042 test/native/remote_session_test.cpp
Then: T043 G3 three-source comparison → T044 register gates
```

### Parallel Example: User Story 4

```text
After T045–T049: T050 test/native/parallel_scan_test.cpp || T051 test/sql/parallel_scan.test
Then: T052 G4 worker/performance evidence → T053 register gates
```

### Parallel Example: User Story 5

```text
After T054–T061: T062 test/native/range_cache_test.cpp || T063 test/native/scan_metrics_v3_test.cpp
Then: T064 SQL contract → T065 G5/G6 evidence → T066 register gates
```

---

## Implementation Strategy

### MVP First (User Story 1)

1. 完成 T001–T011 并通过配套 ABI 基础门禁；固定样本与独立参考。
2. 完成 T012–T023，通过 G0/G1，单独演示五类坐标与现有时间接口兼容。
3. 再加入 US2 的局部筛选、US3 的远程读取、US4 的并行和 US5 的缓存/观测；每阶段以对应 G2–G6 独立判定。

### Incremental Delivery

1. 每一故事先锁定其契约/样本和真实依赖，再完成实现与 native/SQL/harness 差分。
2. US2、US3、US4、US5 均保留前面故事的 G0–G(n-1) 回归；成本声明只采纳该门禁要求的固定样本和完整记录。
3. G7 执行独立 quickstart 复现与文档对照；T072 完成前不把 Phase 4–5 标为已交付。

## Notes

- `[P]` 指完成列出的前置任务后，可在不同文件上同时执行的工作。
- 本期只处理单个 OM 对象；不加入目录发现、跨文件拼接、科学插值或新格式。
- HTTP/S3 没有可靠版本时仍可扫描静态对象，但不得跨查询缓存；缺少服务或真实样本时远程完整门禁未通过。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。

### 官方 HTTPFS 重构执行状态

R01–R19 已执行（性能门槛见独立报告）；R20 的三版本冻结产物验收已保存；完整 004 门禁的原有缺口保留。R21 独立验证者复现仍未执行。旧 T049/G5 失败保留且 superseded，不因缓存删除标为通过；004 完整真实网格 H0–H9 的未闭环项仍未完成。
