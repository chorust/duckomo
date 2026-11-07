# DuckOMO 整棵脏树 code review

**结论：需要修改后再合入；不能宣称 010 或 008 里程碑 PASS。** 本轮确认 010 的 R1/R2 已修，新增最重要发现是 **008 同步远程读取的 `planned_cache` 没有批次释放边界**，已用当前 `reader.cpp` 在 linux_arm64 复现。其余发现涉及文档状态、community 升级命令、009 checklist 和证据提交体积。本轮只写报告，没有修生产代码，也没有启动后续修复。

## 评审基线与范围

- 仓库：`/home/blizhan/repo/github/duckomo`；HEAD：`80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9`。
- 比较对象：**HEAD 到整个当前工作树**，包括已跟踪改动、untracked，以及脏 submodule。不是某两个提交间的三点 diff。检查时 index 无 staged diff。
- 初始清单：37 个有文本 diff 的已跟踪文件、5,082 个 Git 未忽略的 untracked 文件；另有 `.specify/extensions/roadmap` 的 untracked 内部配置。报告自身不包含在这个初始计数里。
- 覆盖源码与共享 seams、CMake、SQL/native/Python 测试及 fixture 工具、010 规格/契约/计划/任务和实现/review-fix 证据、008 变更及 relaxed 状态、community 三文件、009 两个 Draft 文件、README/其他文档、`.pi/` 与证据目录。巨型证据按索引、manifest、最终报告、关键记录和文件体积审查，**没有逐行审阅全部 1.2G 日志**。
- 实际主机 `uname -m` 为 `aarch64`。本轮执行均为 linux_arm64；没有新建或假定其他架构 runner。
- 使用 code-review 技能的 Standards / Spec 两条审查线；工具集中没有 subagent 能力，故由本会话直接完成，不声称独立并行评审。遵照用户要求按功能分组，不把整棵树都归为 010。
- `.specify/memory/constitution.md` 仍是未填模板；未找到适用于本项目的额外正式编码规范。未将 vendor 规则或模板占位符当作已批准的标准。

## 必须保留的状态

| 分组 | 当前事实与限制 |
|---|---|
| 010 | **R1/R2 已修**；**S2 / SC-002 / SC-007 仍 Blocked**；FR-002/007/014 部分验证。42 个任务勾选不等于完整验收。 |
| 008 | **SC-006 relaxed**：原 ≥20% 吞吐硬门禁作废，只收结果正确性；实测约 0% 不能写成达到 20%。**T042/T043 未勾**，真正批间 I/O·decode overlap 未实现；其余矩阵/任务仍开放，不能称 008 完成。 |
| community | 本地 descriptor 与本轮获取的线上 `0.3.5-rc.1` YAML 语义一致；**签名产物 artifact hash 仍 TODO**。登记平台范围不等于本轮跨平台运行证据。 |
| 009 | Draft，仍有待澄清项；没有此次实现或验收证据。 |
| evidence | `run-20260803-linux-arm64/` 实际占用约 1.2G；应先规划精简提交/外置归档，不能直接整目录进产品 PR。本轮未删除或搬移。 |

## 010 — read_om_series

### F010-01 — P2 / Spec、文档：公开指南仍停在中间运行状态

**位置：** `README.md:19,25,175`，`README.en.md` 对应 series 范围段落，`docs/spec.md:5,15,122`。

README 仍写“当前只验证合成对象”“仅 Linux AArch64 / DuckDB v1.5.5”“跨引擎矩阵及真实 producer S7 仍待完成”；`docs/spec.md` 也保留该中间状态，并写 DuckDB 2.0 不在当前发布支持声明中。与 `specs/010-read-om-series/evidence/run-20260803-linux-arm64/report.md` 的最终来源矩阵、`tasks.md:176–178` 和 `evidence/review-fix-linux-arm64/fix-note.md` 不一致；2.0 发布说法还与当前 community/releases 文档冲突。

**影响：** 用户不能从主入口判断实际验证过的版本和对象；后续审查容易把早期 pending 当作最终结论，或把“修复完成”误作全门禁完成。

**建议：** 后续统一到有明确范围的当前表述：四个 pinned pair 的已执行 linux_arm64 案例、真实对象仅两块相邻 IFS025 chunk 的一个域点、N160 为合成验证；同时保留 S2/SC-002/SC-007 Blocked 和未发布的 series 身份。旧运行日志不应改写。这里不建议扩大平台或生产网格支持声明。

**修复状态：** 主指南已按最终来源矩阵更新，保留四个 linux_arm64 pinned pair 的范围、真实对象和 N160 限制以及 S2 / SC-002 / SC-007 Blocked 状态；见 [本轮 fix-note](evidence/review-fix-p2-docs-009/fix-note.md)。

### R1/R2 核对（已修，不作为本轮新增未修 finding）

- `src/scan/read_om_series.cpp:125` 在 child-open loop 前调用 `ValidateReadOmSeriesSpatialDeclaration`；共享实现位于 `src/scan/read_om.cpp:4108`，解析 grid/domain 并核对声明维度。
- `src/scan/read_om_series.cpp:143–155` 先绑定并验证候选，再跳过 `candidate.slots.empty`。strict 缺失检查及现存候选元数据检查不再被 slotless 窗口绕过。
- 本轮校验既有修复 CLI/extension 的 SHA-256 后，重跑 `review-fix-linux-arm64/probe-results.json` 中四版本各 17 个 SQL，共 68 次；退出码和预期诊断全部匹配。使用 `build/series-review-fix/<pair>/...` 的修复产物，**没有误用已恢复的旧 matrix extension**。
- 这次重跑没有 strace；48 个非法空间声明零对象打开的依据仍是已存修复 traces，不冒充本轮新采集。此次亦未重新执行完整 S0–S7。
- 原 `review.md` 是修复前历史评审，应从 `fix-note.md` 解读其状态，不能把历史 R1/R2 失败重复列为当前缺陷。

### 仍阻塞验收的已知覆盖缺口

`tasks.md:178`、`evidence/review-fix-linux-arm64/fix-note.md` 已诚实列出：嵌入时间元数据冲突（含 slotless）、边界/溢出/扩展上限零访问审计，以及实际对象的类型、shape、轴序、空间身份不匹配完整矩阵。`scripts/validate-series.py:2304` 仍将完整 S2 留作 blocked。这里是**已有 acceptance blocker**，并非本轮已复现这些输入有新的运行时缺陷。

## 008 — DuckDB 2 async I/O / range reuse

### F008-01 — P1 / Spec、实现：planned_cache 在同步远程路径随扫描增长

**位置：** `src/om/reader.cpp:570–571,636,786–799`；`src/include/duckomo/om_reader.hpp:276–281`；唯一清理点 `src/scan/read_om.cpp:3604–3606`。另需覆盖 inline async 同步执行分支 `src/scan/read_om.cpp:3874` 附近。

新代码在每次非复用的 planned read 上，把 payload 的 `shared_ptr` 加入 decoder 的 `planned_cache`。这不仅用于跨批 async，也用于所有远程 `DecodeSelectionImpl → PlanSelection → FetchSelection → DecodeSelectionPlan` 同步读取。成功返回时没有 clear/evict；FetchSelection 异常路径只重置 `data_cache`，也没有清理 `planned_cache`。当前唯一 `.clear()` 位于 generation-2 pending async 窗口全部输出后。

**触发：** DuckDB 1.5.x 远程读取，或 DuckDB 2 `duckomo_async_io=false`，用同一个 decoder 连续读取多个不同 range。旧 payload 在 plan 析构后仍被 `planned_cache` 强引用保留，直至 decoder 销毁；反向线性查找还会随着历史项数增长。DuckDB 2 的 `AsyncResultsExecutionMode::SYNCHRONOUS` 返回分支同样没有对应清理。

**本轮复现：** 在 linux_arm64 编译当前 `src/om/reader.cpp`，复用已有 v1.5.4 静态依赖；以 `test/native/reader_capacity_test.cpp` 的内存 ReadAtFile 为基础，将 Path 改为 `https://reader-cache-review/object.om` 以走远程计划路径（没有发网络请求）。读取 `test/data/raw_large.om`，shape 73×61，chunk 7×13，逐 7 行调用公开 `DecodeSelection`，每次调用返回后观察 decoder：

| 起始行 | planned_cache 项数 | 保留 payload 字节 |
|---:|---:|---:|
| 0 | 3 | 920 |
| 7 | 5 | 1,784 |
| 35 | 17 | 5,674 |
| 63 | 29 | 9,786 |
| 70 | 30 | 10,233 |

编译和探针退出码均为 0。这证明跨调用保留了所有已读取范围；**没有执行大对象 OOM 测试，不声称已经观测到进程 OOM**。对大对象全扫，该保留量受整个已读对象规模影响，而不再受单个活动窗口约束，可能造成显著内存回退；这些 `std::vector` payload 不是由 DuckDB BufferManager 直接限额的分配。已有小对象正确性/请求数相等结果不能排除这个问题，SC-006 放宽也没有放宽内存及同步回退契约。

**建议：** 后续给 planned-cache 明确的活动计划/窗口所有者和释放边界，覆盖同步成功、失败及 inline async 返回路径；保留有意的单个 `data_cache` 范围复用，避免破坏仍在途的共享 payload。增加连续非重叠远程同步批次的 retained-bytes 检查，再验取消/失败；本轮不修。

**复现材料：** 临时文件 `/tmp/duckomo-cache-review.cpp`、`/tmp/duckomo-cache-build.log`、`/tmp/duckomo-cache-review.log`；不作为需永久存在的仓库依赖。探针循环的关键操作是 `DecodeSelection(state, {row,0}, {min(7,73-row),61}, {0,0}, count, output, bytes, 512, 512)` 后求和 `state.planned_cache[*].buffer->size()`。当前 reader TU 直接参与编译，不是只运行旧测试二进制。

**定向修复状态（linux_arm64）：** F008-01 已按本 finding 修复并加入同步成功/失败、remote `data_cache` 复用、inline async `SYNCHRONOUS` 及 async 取消/错误验证。修复后的 retained-bytes 结果和构建身份见 [`specs/008-duckdb2-async-io/evidence/review-fix-f008-01-linux-arm64/fix-note.md`](../008-duckdb2-async-io/evidence/review-fix-f008-01-linux-arm64/fix-note.md)。这不代表 008 或 010 PASS；SC-006 仍为 relaxed，T042/T043 未勾。

### relaxed 状态审查

`specs/008-duckdb2-async-io/spec.md:136`、`tasks.md:126–127`、success-criteria、`evidence/008/performance-gates/report.md`、final status 和 roadmap 均明确保留 relaxed / won’t meet ≥20%，没有把约 0% 写成 20% PASS。T042/T043 仍未勾；SC-007/SC-008 全矩阵及其他收口任务仍缺。该范围的状态同步可单独评审，不能据此关闭整个 008。

`test/native/remote_async_test.cpp` 的请求上限调整保证小 fixture 可以产生并发读取，但不检验上述同步缓存生命周期；`session_metrics_test.cpp` 的 2.0 core-HTTP 诊断分支属于 008 兼容性工作，不应误归入 010。

## community — 对齐线上 0.3.5-rc.1

### FCOM-01 — P2 / Spec、发布文档：升级生成命令不再更新 repo.ref

**位置：** `docs/community-extensions.md:74–83`；`community/duckomo/description.yml.in:22`。

文档仍读取当前 HEAD 并用 `template.replace('@DUCKOMO_SOURCE_REF@', ref)` 生成待提交 descriptor，但模板的实际 `repo.ref` 已固定为 `55d66f3fbd3199d7471b8cd569c43cc91a8cec7e`。占位符现在只存在于注释中。因此照抄命令时不会报错，却仍生成旧源码 ref；新 ref 只可能替换进注释。

**本轮验证：** 将替换值设为 `REVIEW_NEW_REF` 后解析输出 YAML，`repo.ref` 仍是 `55d66f3…`。后续升级容易误发布旧源码。

**建议：** 后续把示例改为显式更新 YAML `repo.ref`，或强制先恢复真正的 ref 占位并断言替换后的字段等于目标 SHA；版本也须明确更新。当前已发布 ref 本身没有问题。

**修复状态：** 版本与 ref 现由模板的真实 YAML 字段占位符生成，并在提交文档中解析断言；验证结果见 [本轮 fix-note](evidence/review-fix-p2-docs-009/fix-note.md)。已发布的 `55d66f3…` ref 未被更改。

### 对齐与证据限制

本轮只读获取：

`https://raw.githubusercontent.com/duckdb/community-extensions/main/extensions/duckomo/description.yml`

返回 1,823 字节，SHA-256 为 `36e2bcdda0fad0a84c5a136dc8c744dfc20c4413d66342b484618ca5b1fccef7`。PyYAML 解析后与本地模板 **完全语义相等**（注释不同）。`validation.json` 与文档中的版本、维护者、源码 ref、排除平台一致；旧筹备记录被保存在嵌套 snapshot 中，没有被冒用为新签名安装证据。

这里的 hash 是 **descriptor 文本 hash，不是 extension artifact hash**。`community/validation.json:21` 的 artifact hashes TODO 必须保留；本轮未下载/加载社区签名扩展，也未跑四平台 release。四平台范围是上游登记/历史声明，不能称为本轮已验证。线上 `main` URL 是可变引用，正式发布审计宜另记上游 commit 和各引擎/平台产物身份。

## 009 — Draft

### F009-01 — P2 / Spec、清单：checklist 声称零待澄清项，但规格仍有未决选择

**位置：** `specs/009-point-site-nearest-sampling/checklists/requirements.md:16,37`；`specs/009-point-site-nearest-sampling/spec.md:197`。

checklist 勾选 “No [NEEDS CLARIFICATION] markers remain”，备注也写零个；但 User Story 10 的精简指标行为仍有 `[NEEDS CLARIFICATION: ... legacy_v2、legacy_v3 ...]`。这涉及保留字段、可关闭开关或移除的兼容性决定，不能由“Draft 清单全勾”替代。

**建议：** 独立的 009 文档 PR 保持 Draft，并使 readiness 清单承认未决项；待后续获授权时再澄清。此次没有创建 plan/tasks，也没有实现站点输入或摘要接口。009 的元数据性能故事仍依赖 008 的残余基线；008 relaxed 不等于依赖已完成。

**修复状态：** readiness checklist 现在明确保留一个未决 clarification，并将两项完整性判断留空；spec 仍为 Draft。细节见 [本轮 fix-note](evidence/review-fix-p2-docs-009/fix-note.md)。

## 杂项 — evidence、.pi、嵌套配置

### FMISC-01 — P2 / Standards（提交范围判断）：巨型运行证据不宜原样进入产品 PR

**位置：** `specs/010-read-om-series/evidence/run-20260803-linux-arm64/`。

本轮文件系统统计：5,078 个文件，1,181,103,584 字节（`du -sh` 为 1.2G）；其中 Git 未忽略的候选内容约 1,175,404,803 字节。多个 `controlled-*/manifest.json` 单份约 20MB，包含多轮中间运行和重复展开记录。这里主要问题是总量和重复，不是声称存在超过 GitHub 单文件限制的文件。

**影响：** 整目录加入使代码 PR 的 diff、审计与后续 clone 成本大幅增加。大量中间输出还会让最终结论难以定位。

**建议：** 产品 PR 留可读最终报告、精简 manifest、输入/源码/产物 hash、可复跑命令及有代表性的失败/修复证据；完整历史运行和大日志采用有校验和的独立归档/附件，并在索引中注明取回方式。精简后仍须保留失败/Blocked 记录及所有链接的可追溯性，不以删除失败掩盖结果。**本轮未删除、压缩、移动、上传或改写任何历史 evidence。**

**修复状态：** 已添加产品 PR evidence scope policy，要求报告/精简索引与校验和外部归档分开；历史 evidence tree 保持原位且未改写。见 [submission-scope note](evidence/submission-scope.md) 和 [本轮 fix-note](evidence/review-fix-p2-docs-009/fix-note.md)。

### `.pi/` 与 submodule 的去留（范围建议，不伪造运行缺陷）

- `.pi/extensions/wake-parent.ts`、其 README、`.pi/templates/speckit-milestone-phases.md` 属于本地 agent 协作，不是 DuckOMO 扩展或 010 SQL 功能。建议从 010/008/community 产品 PR 排除；若要共享，独立 tooling PR，并另外验证 pi 事件/API 和 webhook 行为。本轮没有运行 webhook 或完整 pi 扩展测试。
- `.pi/env.wake-parent.local` 已由 `.gitignore` 排除，`git check-ignore` 实测确认。未读取或在报告中复述其内容。Git 候选文件名扫描未发现明显 private-key/credentials 文件名；这不等于对全部大日志做过完整秘密审计。
- `.gitignore` 的三行新增只服务 `.pi/` 本地配置，可与 tooling 工作一起独立提交，或作为独立的小型本地配置保护变更；不要让它与 series 实现混成不可拆的提交。
- `.specify/extensions/roadmap` 的 `?` 来自内部 untracked `roadmap-config.yml`，没有新的 gitlink diff。父仓库 PR 不会自动包含这个文件。应作为工具配置另行处理，不声称已随父仓库完整交付。

## PR 拆分与混合 hunk 边界

| 建议 PR | 可归属的文件/变更 | 依赖与注意事项 |
|---|---|---|
| 008 range reuse + 对应回归 | `src/om/reader.cpp`、`om_reader.hpp` 中 CacheEntry/planned_cache、`read_om.cpp` 的 pending 清理与 reuse metrics、`remote_async_test.cpp`、`session_metrics_test.cpp` | **F008-01 未修前不建议合入该实现。** 同步与异步路径都需覆盖。 |
| 008 状态/性能证据同步 | `specs/008-*`、`evidence/008/{final,performance-gates}`、`.specify/memory/roadmap.md`、release/evaluation/roadmap 文档的 relaxed 段落 | 可先独立审查负责人放宽决定；引用实现/实测来源要清楚，不能把状态放宽当作实现正确性或全 008 完成。 |
| 010 实现与 R1/R2 | series planner/registry/binder 新文件、schema/compat helper、typed reader error、CMake/注册、010 fixtures/scripts/tests/spec/docs | 技术上不需要 planned_cache 才能组合 ordinary read_om；但**现有运行证据来自含 008 的混合树**。若从干净 HEAD 单独拆出 010，必须在那个实际拆分结果上重新验证受影响项。仍不标记 milestone PASS。 |
| community 回填 | `community/duckomo/description.yml.in`、`community/validation.json`、`docs/community-extensions.md` | 与生产代码独立；修正 FCOM-01，保留 artifact hash TODO；与 010 未发布状态分开。 |
| 009 Draft | `specs/009-point-site-nearest-sampling/{spec.md,checklists/requirements.md}` | 独立设计文档 PR，先纠正 checklist；不自动启动实现。 |
| 使用指南杂项 | README 中 FORCE INSTALL、2.0 release 版本提示、日期、列表命令、om_grid_info 说明 | 与 series 新段落分 hunk；这些不是 009 已实现证据，也不全属于 community。 |
| tooling / archive | `.pi/`、对应 ignore、嵌套 roadmap 配置；大 evidence 外置方案 | 单独范围。外置/上传需要后续明确动作，本轮仅建议。 |

### 不能整文件按功能分组的重点

1. **`src/scan/read_om.cpp`**：010 包含 binding 头文件、空间解析/预校验提取、schema 输出 helper、statement-cacheable 标志、`ReadOmChildBinding` 和签名导出；008 只在当前 diff 的 `ScanReadOmImpl` 两组 hunk 中加入 `planned_cache.clear()`（约 3604）和遍历所有 reused sub-ranges 的 logical-read 计数（约 3780）。整个文件归 010 或整个文件归 008 都不准确。
2. **`src/include/duckomo/om_reader.hpp`**：ReaderErrorCategory/Stage 是 010 missing-policy seam；CacheEntry/planned_cache 是 008。要分 hunk，不能整头文件 stage。
3. **README 中英版本**：series 文档、release 安装易用性、历史日期更新混在同一文件；`docs/spec.md` 的 series 新说明又碰到旧 2.0 支持措辞，需要同步裁决。
4. `src/om/{local_file,remote_file}.cpp` 的新增 typed errors 属于 010；`src/om/reader.cpp` 的本轮 range-reuse diff 属于 008。compat helper 虽有 generation-2 分支，新增用途是 010 逻辑计划组合，不能仅凭 “2.0 API” 标签归给 008。
5. `test/tools/duckomo_fixture_tool.cpp`、顶层 CMake、`test/CMakeLists.txt` 的当前新增主要为 010。现有 shared static artifacts 可供核查，但不构成每个拆分 PR 已重新构建的证据。

上述拆分仅为审查建议；本轮没有 stage、创建提交、改分支、reset、stash 或移动工作树文件。

## 本轮验证与证据边界

| 检查 | 实际结果 | 能证明什么 / 不能证明什么 |
|---|---|---|
| `python3 -m unittest discover -s test/tools -p '*series*test.py'` | 29 tests，exit 0 | unittest 类测试通过；不把未被 discovery 收集的顶层函数算入。 |
| `python3 test/tools/series_registry_test.py` | exit 0 | registry provenance 与确定性生成脚本检查通过。 |
| `python3 test/tools/series_fixtures_test.py` | exit 0，两次生成 17 对象/4 gap prefixes | 测试脚本覆盖 fixture 定义/独立预期和 stub writer 包装层复现；不是本轮重新运行全套真实 OM writer/引擎验收。 |
| 既有 R1/R2 68 个 SQL 的本轮重跑 | 四版本各 17/17 退出码/诊断符合预期；CLI/修复扩展 hash 相符 | 支持 targeted 修复状态；未重采零访问 trace，也未重新评定 S2。 |
| 当前 reader TU 的同步 cache 探针 | 成功复现 retained payload 持续增长 | F008-01 的直接证据；不是完整远程性能/RSS矩阵。 |
| 上游 descriptor GET + YAML 语义比较 | 相等；文本 hash 见上 | 确认登记内容对齐；不证明任何 extension 产物 hash、签名安装或异平台运行。 |
| 工作树/大证据/ignore/submodule 清单 | 已审查 | 覆盖脏树分组；非 1.2G 内容的逐行完整审计。 |

本轮临时输出在 `/tmp/duckomo-review-python-tests.log`、`/tmp/duckomo-review-tree-probes.json`、上述 cache probe 文件和 `/tmp/duckomo-upstream-description.yml`。报告中保留了关键命令、身份和观测，临时路径不被当成永久验收附件。未执行完整重建、ASan/UBSan、全部 SQL/native、真实 producer 重抓、008 全 benchmark 或新 CI。未用这些局部检查宣称任何里程碑 PASS。

**发现计数：** 010：1×P2；008：1×P1；community：1×P2；009：1×P2；杂项：1×P2。没有建立 P0 发现。按审查轴，Spec/行为/文档一致性 4 项（最重 P1）；Standards 正式规范违例 0 项，提交组织判断 1×P2（巨型 evidence）。共享文件承担多个特性的职责是拆分风险判断，不额外计一个“代码坏味道”缺陷。

## 会话边界

仓库内本轮只新增本报告 `specs/010-read-om-series/review-tree.md`；验证临时文件写入 `/tmp`。未修改生产源码、任务勾选、既有证据或其他未提交文件；未 commit / push / amend；未调用 implement、review-fix 或下一阶段 pi。报告完成后结束本轮，不轮询、不等待用户，也不手动伪造 wake-parent 的 settled 通知。
