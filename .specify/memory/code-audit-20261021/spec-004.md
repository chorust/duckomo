# DuckOMO spec 004 当前 HEAD 一致性审计

审计日期：2026-10-21（Asia/Shanghai）  
仓库：`/home/blizhan/repo/github/duckomo`  
审计 HEAD：`6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`（`perf: coalesce contiguous reads into rectangular batches`）

## 结论

在本轮已直接检查的网格定义、Gaussian/投影坐标、空间布局、选择/批次、reader/source-info、metrics 和相关测试范围内，**未确认可复现的代码缺陷**。这不等于 spec 004 验收通过：当前有效索引中的 C004-01、C004-02、C004-03、C004-04 全部仍未完成；H0/H1/H2/H3/H4/H5/H6/H7/H8/H9 的完整、当前范围闭环证据不足。故对可发布/完整支持声明的判断为 **高严重性：验收未闭环；代码缺陷：本次已审范围内未发现**。

当前有效范围以 `specs/004-multi-grid-selection/tasks.md:3-18`、`contracts/gaussian-acceptance-20261010.md:7-12` 和 `contracts/pinned-producer-reference-20261010.md:5-24` 为准：N160/N320/N 区域真实样本及依赖它们的验收经批准本轮跳过，定义/native/synthetic 保留但不算真实通过；本轮 Gaussian 目标为 O1280；四个定义只登记为 `coordinate-value-validated`，不是 `remote-benefit-validated`。DuckDB 2.0 已退出当前执行前置；当前 H8 是 v1.5.4/v1.5.5/v1.5.6 三版本完整当前范围审计。005 的通过结果、三版本产物/hash 与留存命令结果不会替代 004 的当前 H1–H9 gate。

HEAD 工作区检查显示相对 HEAD 没有已跟踪文件差异。状态中有其他 worker 的未跟踪目录/文件；本审计没有读取、修改或清理它们。协调者提供的源码 digest `886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e` 及其匹配 005 三版本 manifest 的结论作为父会话证据引用；本审计没有重复计算该 digest，也没有把它视作 004 H8 通过证据。

`specs/004-multi-grid-selection/evidence/final.md:3-7` 标记记录日期为 2026-10-08，并说明当时实现仍在进行、H0–H9 无完整 gate 通过。该 final 属于早于 10-10/10-20 有效修订的历史记录；当前范围状态应看当前 tasks、contracts 与 `docs/roadmap.md:103,110`，不可把旧 checkbox、部分验收或旧 2.0 matrix 结果拼成当前通过。

## 证据与状态口径

下文未带仓库根路径前缀的 `tasks.md`、`contracts/...`、`evidence/...` 均相对 `specs/004-multi-grid-selection/`；`src/...`、`test/...`、`docs/...`、`scripts/...` 相对仓库根。

- **实现/局部回归**：实现路径及相关 native/SQL 测试存在；只表示已审实现及这些测试支持该行为。
- **部分**：有代码或窄范围/合成/子检查，但完整 spec gate 未通过或缺当前范围证据。
- **未闭环/未核实**：未找到符合当前合同的完整证据；不代表代码一定失败。
- 对 10-20 HEAD 连续矩形读取变更，直接检查 `src/scan/batch.cpp:26`、`src/scan/axis_selection.cpp:385`、调用处 `src/scan/read_om.cpp:3402`、`src/scan/read_om.cpp:3510`。矩形只合并逻辑连续区间中完整 trailing axes；native 测试穷举小形状并验证无 gap/重复，reader 测试将官方解码输出逐位比较。已审范围未发现该改动破坏源位置或解码结果的证据。
- 最新官方 HTTPFS 契约 `evidence/official-httpfs-refactor/contract.md:3,7,19,21,23` 替代历史专用 ABI/LRU/逐响应 observer 要求：远程 transport body/attempts/responses 在 DuckOMO metrics 中为 `NULL`/`complete=false`，由受控服务日志核对服务端已发送字节；这不是客户端接收字节。契约同时明确逐网格 cold full/local、协议、恢复等验收仍须补齐。因此这是现行计量边界和未完成验收，不能写成已通过，也不能仅据此断言当前实现违反新契约。

## FR 逐项矩阵

| 需求 | 当前实现路径（当前 HEAD） | 测试/证据 | 判断 |
|---|---|---|---|
| FR-001 四族网格 | `src/grid/projected_grid.cpp:370`、`src/grid/gaussian_grid.cpp:46`、`src/grid/domain_registry.cpp:155`、`src/include/duckomo/generated_grid_registry.hpp` | `test/native/grid_identity_test.cpp:138`；`test/native/gaussian_grid_test.cpp:203`；`test/native/grid_selection_test.cpp:62` | **部分**：四族定义/注册和合成回归存在；N-grid 真实验收按当前修订跳过；O1280 与三个投影仅坐标/值等级，完整 H1 未闭环。 |
| FR-002 显式定义/domain | `src/scan/read_om.cpp:934`、`:1037`、`:1128`；`src/grid/domain_registry.cpp:155`；`src/grid/grid_definition.cpp:11` | `test/native/grid_identity_test.cpp:138`；`test/sql/multi_grid.test` | **实现/局部回归**：绑定配置分别处理显式 grid/domain、registry 身份；全量非法组合/真实对象 gate 未完整验证。 |
| FR-003 投影空间轴及其他轴 | `src/grid/spatial_layout.cpp:81`、`:161`、`:197`、`:224`；扫描输出 `src/scan/read_om.cpp:3510` | `test/native/spatial_layout_test.cpp`；`test/native/spatial_io_test.cpp`；`test/sql/spatial_composition.test:15` | **部分**：实际轴/stride 映射存在；当前完整真实布局与全语义轴 H1/H2 未闭环。 |
| FR-004 Gaussian 行表/局部映射 | `src/grid/gaussian_grid.cpp:46`、`:125`、`:136`；`src/grid/spatial_layout.cpp:201`、`:216`；生成器 `scripts/generate-grid-registry.py:489` | `test/native/gaussian_grid_test.cpp:203`；`test/native/grid_selection_test.cpp:135`；`test/native/grid_identity_test.cpp:138` | **部分**：完整行表、N320 合成区域 parent segments 和 O1280 规则实现存在；真实 N 样本跳过，O1280 完整时间值/远程 gate/H9 仍缺。 |
| FR-005 绑定前校验 | `src/scan/read_om.cpp:904`、`:934`、`:1037`、`:1128`；`src/grid/spatial_layout.cpp:44` | `test/native/schema_test.cpp`；`test/native/grid_identity_test.cpp:83`；`test/sql/grid_version_compat.test:16` | **部分**：参数、轴、布局及坐标校验代码存在；所有冲突证据/真实布局/错误恢复完整 gate 未通过证明。 |
| FR-006 坐标值域及规范化 | `src/grid/projected_grid.cpp:42`、`:386`；`src/grid/gaussian_grid.cpp:136`；`src/grid/grid_definition.cpp:42` | `test/native/projected_grid_test.cpp`；`test/native/gaussian_grid_test.cpp:117`；固定 producer 对照见 `contracts/pinned-producer-reference-20261010.md:11-14` | **部分**：规范化和有限坐标计算实现；四个定义的证据等级只到 coordinate-value，完整 H1/范围覆盖未闭环。 |
| FR-007 位置/值/语义轴兼容 | `src/grid/spatial_layout.cpp:81`、`:97`、`:197`；`src/scan/read_om.cpp:3402`、`:3510` | `test/sql/multi_grid.test`；`test/sql/spatial_composition.test:15`；`test/sql/parallel_scan.test:59` | **部分**：映射及保留轴路径有回归；所有布局/多变量/语义轴真实差分未构成完整 H1/H2。 |
| FR-008 安全谓词候选 | `src/scan/spatial_filter.cpp:498`；`src/scan/read_om.cpp:1643`；`src/scan/spatial_selection.cpp:469` | `test/native/spatial_selection_test.cpp:305`；`test/native/grid_selection_test.cpp:62`；`test/sql/grid_selection.test` | **部分**：安全必要条件、候选游标和 DuckDB residual filter 有实现；完整物化双向 EXCEPT ALL gate 仍 not-run。 |
| FR-009 投影曲线/局部覆盖保守选择 | `src/scan/spatial_selection.cpp:469`；坐标 `src/grid/spatial_layout.cpp:197` | `test/native/grid_selection_test.cpp:62`、`:118`（弯曲/四角外但内部相交） | **部分**：已审算法逐原生点判定并覆盖这些合成边界；全量冻结输入 H2 未闭环。 |
| FR-010 经度双区间并集/无重复 | `src/scan/spatial_filter.cpp:498`；`src/scan/spatial_selection.cpp:469` | `test/native/spatial_selection_test.cpp:305`、`:311`；`test/sql/grid_selection.test` | **部分**：seam union 与候选去重有合成回归；含不安全分支的完整 SQL gate 未完整运行。 |
| FR-011 边界精确、预算回退 | `src/scan/spatial_filter.cpp:498`；`src/scan/spatial_selection.cpp:469` | `test/native/grid_selection_test.cpp:174`；`test/native/spatial_selection_test.cpp:380` | **部分**：actual coordinate 判定、预算超限扩大候选及残留过滤测试存在；所有奇点/条件的完整冻结 H2 未通过证明。 |
| FR-012 过滤/投影/并行组合 | `src/scan/read_om.cpp:1639`、`:1643`、`:3402`、`:3510`；`src/scan/axis_selection.cpp:385` | `test/sql/spatial_composition.test:15`；`test/sql/parallel_scan.test:59`；`test/native/parallel_scan_test.cpp` | **部分**：依赖保留、批次和并行路径实现；当前所有多任务和组合 gate 仍未闭环。 |
| FR-013 坐标/计数/空集零值读取 | `src/scan/read_om.cpp:2903`；`src/scan/grid_info.cpp`；metrics `src/include/duckomo/metrics.hpp:921` | `test/sql/grid_zero_io.test:43`；`test/sql/grid_info.test:97`；`test/native/grid_zero_io_test.cpp` | **部分**：局部 SQL/native 覆盖明确检查 index/data/decode 为零；全当前范围真实对象 H3 仍 not-run。 |
| FR-014 有界内存与取消 | `src/scan/spatial_selection.cpp:469`；`src/scan/read_om.cpp:3339`、`:3556`；memory account `src/include/duckomo/metrics.hpp:1710` | `test/native/spatial_lifecycle_test.cpp`；`test/native/reader_capacity_test.cpp`；旧 H5 local 子检查见 `evidence/final.md:219-223` | **部分**：有窗口、预算、取消和内存计量实现/合成检查；完整 H5/远程 transport memory 不在 DuckOMO 账本、H4 生命周期验收未闭环。 |
| FR-015 同内容跨来源等价 | `src/om/remote_file.cpp:198`、`:258`；官方解码 `src/om/reader.cpp:233`；消费位置 `src/scan/read_om.cpp:3510` | `test/native/remote_session_test.cpp`（loopback/local 合成）；旧 HTTPS/S3 source/info 子检查在 `evidence/final.md:232-236` | **部分**：有本地/HTTP 合成与部分真实 source/info 记录；逐网格 HTTP/HTTPS/签名 S3、Gaussian local offsets 和完整值差分未闭环。 |
| FR-016 远程真实局部收益 | 候选/读取路径 `src/scan/read_om.cpp:3402`、`src/om/reader.cpp:243`；实际 HTTPFS 成本按契约由服务日志审计 | `contracts/validation-evidence.md:69,76,82`；`tasks.md:8` | **未闭环**：没有当前范围下每类网格、两个远端来源的冷态 full/local value bytes、decode 和完整响应 body 严格下降证据。005 的 P7 不满足此项。 |
| FR-017 官方 OM reader 物理读取 | `src/scan/read_om.cpp:3510`；`src/om/reader.cpp:243` | HEAD 新矩形官方解码对照 `test/native/reader_capacity_test.cpp:309`；段穷举 `test/native/batch_test.cpp:83` | **实现/局部回归**：调用官方 DecodeSelection，不自行规划压缩字节；完整地理 H2 和真实碎片选择验证未闭环。 |
| FR-018 权限/版本/缓存/取消边界 | `src/om/remote_file.cpp:198`、`:258`；官方 HTTPFS 迁移约束 `evidence/official-httpfs-refactor/contract.md:7` | `test/native/remote_session_test.cpp`；契约故障集 `contracts/validation-evidence.md:76` | **未闭环**：当前契约只保证打开时身份对比及扫描期间对象稳定，不保证逐响应快照/立即撤权；逐网格强 S3、权限变化、版本冲突及恢复完整证据未完成。 |
| FR-019 成本/完整性证据 | `src/include/duckomo/metrics.hpp:601`、`:838`、`:921`；`src/om/remote_file.cpp:269`；调用 `src/scan/read_om.cpp:3443` | `test/native/scan_metrics_v4_test.cpp:240`；HTTPFS 契约 `evidence/official-httpfs-refactor/contract.md:19,21` | **部分**：logical reads/decode/任务/内存及 completeness 有计量；远程 body/attempts 在扩展侧明确 unobserved，须以受控服务日志补齐。当前 004 逐网格完整对账未闭环。 |
| FR-020 grid info/source 身份 | `src/scan/read_om.cpp:2867`、`:2903`、`:3402`、`:3510`；`src/grid/grid_identity.cpp:274` | `test/sql/grid_info.test:39`；`test/sql/source_identity.test:19`；`test/native/source_identity_test.cpp` | **部分**：显式接口及对象/网格/布局/源位置身份已实现；真实对象版本强度和所有网格独立复现未完整通过。 |
| FR-021 定义身份稳定 | `src/grid/grid_identity.cpp:184`、`:260`、`:262`、`:274` | `test/native/grid_identity_test.cpp:57`、`:83` | **实现/局部回归**：canonical definition/hash、Gaussian parent/subset 与 layout ID 已覆盖；H0 全输入重生及现实布局证明仍开放。 |
| FR-022 点/区域关系交付 | `src/scan/region_geometry.cpp`、`src/scan/read_om.cpp:3678`；空间布局 `src/grid/spatial_layout.cpp:197` | `test/native/region_geometry_test.cpp:149`；`test/native/grid_selection_test.cpp:135`；旧 H7 synthetic 记录 `evidence/final.md:213-217` | **部分**：接口和合成 point/region 精确判断存在；真实四定义 H7 及独立复现未完成。 |
| FR-023 能力状态说明 | `src/scan/grid_info.cpp`；`src/scan/read_om.cpp:2867`、`:2903` | `test/sql/grid_info.test:64`、`:112` | **部分**：grid info 暴露 CRS/能力状态并标记 unsupported；所有定义公开文档/逐项实测与 H7/H9 未闭环。 |
| FR-024 DuckDB 2.0 目标 | 当前任务修订 `tasks.md:5,9` | 旧 `plan.md:23`、旧 version evidence 不能代表当前执行范围 | **按批准范围暂缓**：2.0 已退出本轮交付前置；不据此作当前支持声明，也不把历史预发布记录记成当前通过。 |
| FR-025 版本组合矩阵与 fail-closed | `src/compat/duckdb_api.cpp`；`src/scan/read_om.cpp:934`；测试入口 `test/native/grid_version_compat_test.cpp:5` | `test/sql/grid_version_compat.test:16`；C004-03 `tasks.md:9` | **部分/未完整核验**：版本兼容保护及局部测试存在；父会话提供的 005 digest/产物匹配仅为身份辅助。004 三版完整 H8 当前仍开放。 |
| FR-026 正式 2.0 支持门槛 | 当前范围 `tasks.md:5,9`；验收规则 `contracts/validation-evidence.md:71` | 当前无 2.0 正式支持证据；当前任务已把 2.0 移出本轮 | **按批准范围暂缓**：未声明 2.0 正式支持；正式版同套复验仍是未来条件，不判为当前实现缺陷。 |
| FR-027 每类真实样本坐标/官方值 | `scripts/generate-grid-registry.py:489`；sample/domain 实际绑定 `src/grid/domain_registry.cpp:155` | 四定义登记 `contracts/pinned-producer-reference-20261010.md:11-14`；O1280 本地记录；`contracts/validation-evidence.md:64` | **部分**：四定义 coordinate-value-validated；N-grid 本轮真实验收豁免。完整 H1、完整时间/布局范围及新 matrix 对应实测仍未闭环。 |
| FR-028 Registry 可重生且证据分级 | `scripts/generate-grid-registry.py:489`、`:597`；runtime identity 检查 `src/grid/domain_registry.cpp:155` | `test/tools/grid_registry_test.py:121`；`test/native/grid_identity_test.cpp:138`；H0 `contracts/validation-evidence.md:63` | **部分**：生成器/登记元数据/证据等级存在；当前固定输入的完整两次重生审计和 H0 未作为完整 gate 通过。 |
| FR-029 观测与脱敏 | `src/include/duckomo/metrics.hpp:601`、`:838`、`:921`；`src/scan/read_om.cpp:3443`、`:3556` | `test/native/scan_metrics_v4_test.cpp:14`、`:240`；`test/sql/grid_info.test:97` | **部分**：v4 区分 exact/observed/upper、阶段、任务、内存、终态并脱敏；远端 transport body 是契约规定的 unobserved，需外部服务端审计，不等同客户端实际 body。 |
| FR-030 固定证据/文档/H9 | README、`docs/roadmap.md:103,110`、spec contracts、quickstart 与 validation runner | C004-04 `tasks.md:10`；H9 `contracts/validation-evidence.md:72,80,82` | **未闭环**：文档已记录范围/边界，但独立验证者 H9 和最终 support status 尚缺；不得宣称完整交付。 |

## SC 逐项矩阵

| 成功标准 | 实现/验证依据 | 判断 |
|---|---|---|
| SC-001 四族真实坐标/值 100% 对照 | 三投影与 O1280 的 producer/value 等级见 `contracts/pinned-producer-reference-20261010.md:11-14`；H1 要求 `contracts/validation-evidence.md:64` | **部分**：四个定义登记 coordinate-value-validated；N-grid 真实样本豁免；完整 H1 和 HRES 完整时间/数组轴范围未闭环。 |
| SC-002 完整扫描差分及边界覆盖 | 合成测试 `test/native/grid_selection_test.cpp:62`、`test/native/spatial_selection_test.cpp:305`、SQL `test/sql/grid_selection.test`；H2 `contracts/validation-evidence.md:65` | **部分**：已审边界有实现/局部回归；完整冻结输入双向 EXCEPT ALL gate 未通过证明。 |
| SC-003 两远端冷态成本严格下降 | 当前条件见 `contracts/validation-evidence.md:69,76,82`；active C004-02 `tasks.md:8` | **未闭环**：缺每类当前范围远端 body、value bytes、decoded blocks 的 full/local 证据及服务端对账。 |
| SC-004 零值读取/坐标成本 | `test/sql/grid_zero_io.test:43`、`test/sql/grid_info.test:97`、`test/native/grid_zero_io_test.cpp`；H3 `contracts/validation-evidence.md:66` | **部分**：合成/有限真实 source-info 子检查通过；全范围 H3 未完成。 |
| SC-005 1/2/4 worker 及生命周期 | `test/sql/parallel_scan.test:59`、`test/native/parallel_scan_test.cpp`；H4 `contracts/validation-evidence.md:67` | **部分**：调度和局部并行差分已有；每类当前样本的 worker/跨批/取消/失败恢复完整证据未闭环。 |
| SC-006 四类 point/region 精确关系 | `test/native/region_geometry_test.cpp:149`、`test/native/grid_selection_test.cpp:135`；H7 `contracts/validation-evidence.md:70` | **部分**：四类合成关系测试有记录；真实四定义 H7 与独立复现未完成。 |
| SC-007 ≥10× 点数 work peak ≤2× 且有界 | 内存 accounts `src/include/duckomo/metrics.hpp:1710`；合成 local 记录 `evidence/final.md:219-223`；H5 `contracts/validation-evidence.md:68` | **部分**：合成 local 子检查历史通过；完整远程 H5、transport/control buffer 和当前固定范围 H5 未闭环。 |
| SC-008 registry 两次重生一致、证据等级清楚 | `scripts/generate-grid-registry.py:489`、`test/tools/grid_registry_test.py:121`；H0 `contracts/validation-evidence.md:63` | **部分**：generator/runtime checks 存在；本轮完整 current-input H0 仍未通过记录。 |
| SC-009 兼容版本同套 gate | 当前 H8 目标见 `tasks.md:9`；`test/sql/grid_version_compat.test:16` | **未闭环**：当前要求三版完整 004 H8；局部版本/005 检查和历史 1.5.4/2.0 not-run 记录不可替代。 |
| SC-010 非法/权限/版本/取消及恢复 | `test/native/grid_selection_test.cpp:174`、`test/native/scan_metrics_v4_test.cpp:191`；故障集 `contracts/validation-evidence.md:76` | **部分**：若干 synthetic invalid/budget/cancel/recovery 路径测试存在；逐网格远程、权限/版本边界和所有失败后恢复未完整验收。 |
| SC-011 未参与实现者独立复现 | H9 定义 `contracts/validation-evidence.md:72,80,82`；C004-04 `tasks.md:10` | **未完成**：当前索引要求缺独立记录保持未完成；本次未发现可作为 H9 的记录。 |

## 本轮检查和已有测试执行记录

直接检查过的实现族包括：`grid_definition.cpp`、`grid_identity.cpp`、`domain_registry.cpp`、`projected_grid.cpp`、`gaussian_grid.cpp`、`spatial_layout.cpp`、`spatial_filter.cpp`、`spatial_selection.cpp`、`axis_selection.cpp`、`batch.cpp`、`read_om.cpp`、`grid_info.cpp`、`om/reader.cpp`、`om/remote_file.cpp`、metrics 和相应 native/SQL 测试。重点直接审阅 HEAD 新增矩形 batch 算法及调用链；`test/native/batch_test.cpp:83` 逐个验证请求区间没有 gap/duplicate，`test/native/reader_capacity_test.cpp:309` 对七行矩形官方解码输出作 bitwise 对照并记录解码 chunk 35→5，`test/native/axis_selection_test.cpp:77` 覆盖稀疏区间保持。

前序已完成执行记录（本收敛步骤未重跑）：

- v1.5.4、v1.5.5、v1.5.6 各自的 `batch_test`、`axis_selection_test`、`reader_capacity_test` 通过。
- v1.5.4 focused native：`projected_grid_test`、`gaussian_grid_test`、`grid_identity_test`、`grid_selection_test`、`grid_zero_io_test`、`source_identity_test`、`grid_version_compat_test`、`spatial_selection_test`、`spatial_layout_test`、`spatial_io_test`、`parallel_scan_test`、`domain_reference_test`（1,038,240 positions × 15 variables）、`region_geometry_test`（固定 official spatial package hash）通过。
- focused SQL：`multi_grid.test`（155 assertions）、`grid_selection.test`（39）、`grid_info.test`（103）通过。
- `domain_reference_test` 与 `region_geometry_test` 曾有缺参数的初始非零调用，之后补齐固定本地参数/extension 后通过；初始调用不是实现失败，也不作为额外成功覆盖。

**命令身份限制**：保存的本轮交接摘要保留了测试 target 名与结果，但没有完整保存每个此前命令的 argv。当前 manifest 的 release_dir 可用已知路径形式定位，例如：

```text
/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release/test/native/{batch_test,axis_selection_test,reader_capacity_test}
/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.5/builds/10fb4c1331f717e97196b6fb6990cacbab48cd44646c6a51ea4aa371ae577f52/release/test/native/{batch_test,axis_selection_test,reader_capacity_test}
/home/blizhan/repo/github/duckomo/build/official-matrix/v1.5.6/builds/ab0257d665a469cae1f1e33da2449c91009137e745262de32b2ad447a00d3e5e/release/test/native/{batch_test,axis_selection_test,reader_capacity_test}
```

本次没有重建共享 build、启动大矩阵或外部服务，也未重跑 SQL/native 测试。父会话正在负责 005 的 336 路径/hash 与 306 条留存命令审计，本报告不复算、不借用其结果为 004 通过证据。报告外的验证限制：当前审计没有执行完整 H0–H9 runner；逐网格 HTTP(S)/签名 S3 full/local 成本及服务端 JSONL、完整三版本 H8 和 H9 均未在本轮独立验证。

## 明确缺口与严重性

1. **高：完整交付门禁未闭环。** C004-01–04 全部开放，直接影响完整范围/支持状态，不应宣布 spec 004 或 Phase 6 verified。证据：`tasks.md:7-10`、`contracts/validation-evidence.md:63-82`、`docs/roadmap.md:103,110`。
2. **高：逐网格远程收益、协议/权限恢复证据缺失。** 当前官方 HTTPFS 契约将 response body 指标定义为扩展侧 unobserved，须依靠受控服务端审计；004 的逐网格 full/local、签名 S3/TLS 和失败恢复仍待完成。证据：`evidence/official-httpfs-refactor/contract.md:19,21,23`、`tasks.md:8`。
3. **高：当前三版 H8 不可由 005 版本验收替代。** 父会话的源 digest/产物身份结论可佐证构建来源，但 004 当前范围 same-input H0–H7 与三版 H8 仍开放。证据：`tasks.md:9,12`、`contracts/validation-evidence.md:71,82`。
4. **中：当前 final 是历史快照。** `evidence/final.md` 记录日期 2026-10-08；范围修订发生于 10-10，当前任务索引于 10-20 更新。评估时必须以活动索引/合同为准，不可只读旧 final。
5. **代码缺陷：已检查部分未确认。** HEAD 矩形合并有区间穷举与官方 reader bitwise 对照；此结论严格局限于上述已审路径和局部测试。

