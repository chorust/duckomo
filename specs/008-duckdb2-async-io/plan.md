# Implementation Plan: DuckDB 2.0 正式支持、远程读取请求合并与异步 I/O

**Branch**: `008-duckdb2-async-io`（规格目录；当前未切换 git 分支） | **Date**: 2026-10-23 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/008-duckdb2-async-io/spec.md`

## Summary

把实验性的 DuckDB 2.0 兼容变为正式支持：提交已验证的兼容改动，2.0 产物进入日常 CI 与 tag 发布（1.5 与 2.0 拆分工具链 job）。远程读取慢的主因是请求过碎（O1280 全扫约 11,592 次、约 356 B/次），因此分两步优化：

1. **请求合并**：把官方 OM 解码器已有的 `io_size_merge`/`io_size_max` 暴露为远程专用会话设置并校准默认值，1.5.x 与 2.0 同时受益。
2. **2.0 异步 I/O**：read_om 通过 2.0 表函数的 `AsyncResult`/`HandleBlocked` 协议，把每个 batch 的合并后范围作为 ASYNC 池任务并发获取，再恢复解码；必要时再做跨 batch 预取与内存治理。

依据见 [research.md](research.md)。

## Technical Context

**Language/Version**: C++17（扩展），C11（OM C 编解码器），Python 3.11（验证/发布脚本），GitHub Actions YAML

**Primary Dependencies**: DuckDB v1.5.4/v1.5.5/v1.5.6 与 v2.0.0（`d54cbbe3df`）；官方 HTTPFS 与 spatial 扩展；open-meteo om-file-format（pin `d8855e41`）；duckdb/extension-ci-tools `v1.5-variegata`（`3fd6109`）与 `v2.0-cyanoptera`（`b8d325c3`）

**Storage**: 只读 OM v3 文件（本地、HTTP/HTTPS、S3）；无持久缓存

**Testing**: DuckDB sqllogictest runner（`test/sql/*.test`）、native 测试（`test/native/*`，含 `official_httpfs_test`、`remote_session_test`）、sanitizer 目标、`scripts/validate-official-httpfs.py` 受控远程验收、Python 工具测试（`test/tools/*_test.py`）、基准脚本

**Target Platform**: 发布 linux_amd64、linux_arm64、osx_amd64、osx_arm64；性能与远程验收以 Linux AArch64 为主

**Project Type**: DuckDB 可加载扩展（库）+ 构建/发布工具

**Performance Goals**: SC-003（请求数 −90%，字节 ≤110%）、SC-004（局部字节 ≤125%）、SC-005（S3 单 worker 全扫 −70%）、SC-006（异步再 −20%）、SC-009（本地 ±5%）

**Constraints**: 结果与 1.5.6 相同；错误分类与凭据脱敏不变；DIRECT_IO、禁止整体下载、对象身份校验不变；内存受查询账本约束（SC-007）；取消 1 s 内结束（SC-008）

**Scale/Scope**: 4 个引擎版本 × 4 平台 = 16 个发布产物；单对象数 MB 到数百 MB（如 354 MB 的 ECMWF 温度 chunk）

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

`.specify/memory/constitution.md` 仍是未填写的模板，没有可执行的原则或门禁。本计划改用项目已有的工作约定作为检查项（来源：`.specify/memory/roadmap.md`、`docs/official-httpfs.md`、`docs/duckdb-2.0-evaluation.md`）：

| 约定 | 本计划 | 状态 |
|---|---|---|
| 每个产物只对应一个引擎版本，官方运行时按 SHA256 固定 | 2.0 独立 pair 与 runtime 条目；发布前逐一 verify | PASS |
| 结论必须有证据，不外推上游数字 | 所有 SC 以本项目基准验证；云端与 loopback 分开报告 | PASS |
| 不引入自有远程缓存，保持官方 HTTPFS 策略 | 合并在解码器规划层；异步只改并发，不缓存 | PASS |
| 1.5 清单断言不被破坏 | 2.0 不并入 `test/data/grids/version-matrix.json` | PASS |
| 凭据与签名 URL 不入日志/证据 | 指标只含计数；基准沿用私有 run.env | PASS |

**Post-design re-check**：Phase 1 产物（settings/metrics/release 契约、数据模型）未引入新的违背项。建议另行用 `$speckit-constitution` 正式落地项目宪法。

## Project Structure

### Documentation (this feature)

```text
specs/008-duckdb2-async-io/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── settings.md
│   ├── metrics.md
│   └── release.md
├── checklists/requirements.md
└── tasks.md             # 由 $speckit-tasks 生成
```

### Source Code (repository root)

```text
src/
├── compat/duckdb_api.cpp           # 1.5/2.0 兼容包装（扩充 SetCardinality 等）
├── include/duckomo/
│   ├── compat/duckdb_api.hpp
│   ├── reader.hpp                  # DecodeSelection 拆分为 plan / fetch / decode
│   ├── read_at_file.hpp            # 异步读取接口（2.0）
│   ├── remote_file.hpp
│   └── metrics.hpp                 # remote_io 指标
├── om/
│   ├── reader.cpp                  # 合并参数传递、分阶段解码
│   ├── metadata.cpp                # 元数据/坐标读取使用远程合并参数
│   └── remote_file.cpp             # PARALLEL_ACCESS、在途请求、内存账本
├── scan/read_om.cpp                # 设置读取、AsyncResult/HandleBlocked 集成
└── om_extension.cpp                # 注册新会话设置

test/
├── sql/remote_io_settings.test     # 设置校验、本地无影响
├── native/remote_async_test.cpp    # 并发/取消/错误一致性（2.0）
├── native/official_httpfs_test.cpp
├── CMakeLists.txt
└── data/engine-compatibility/version-matrix.json

scripts/
├── benchmark-remote-io.py          # 由一次性脚本提升
├── release_tools.py                # tar.gz CLI、2.0 版本组
├── verify-community-artifact.py
├── build-version.sh
└── version_matrix.py

.github/workflows/
├── community-build.yml             # build-1-5 / build-2-0 拆分
└── release.yml                     # 同上

test/data/release-runtimes.json
docs/{releases.md,community-extensions.md,duckdb-2.0-evaluation.md,official-httpfs.md}
README.md / README.en.md
```

**Structure Decision**: 沿用现有单扩展仓库结构；新增一个 SQL 测试、一个 native 测试和一个基准脚本，其余为既有文件修改。

## Delivery Phases

| 阶段 | 内容 | 退出门禁 |
|---|---|---|
| P1-a 兼容提交 | 提交评估期改动；清理剩余 `SetCardinality`；1.5.4/1.5.5/1.5.6/2.0 全量回归与 sanitizer；最终源码重建 2.0 pair 并重算 build_id | SC-002 |
| P1-b CI/发布 | workflow 拆分、release-runtimes 增加 2.0、release_tools/verify/build-version 更新、三平台 SHA 冻结、文档与 README 更新；dry-run 发布 | SC-001 |
| P2 请求合并 | 指标拆分 index/data；会话设置与校验；扫参校准默认值；有用/间隙字节指标；基准脚本入库 | SC-003、SC-004、SC-005、SC-009 |
| P3-a 批内异步 | Reader 三段式 API；read_om 接入 HandleBlocked；PARALLEL_ACCESS 或句柄池；随剩余内存收缩的在途预算与内存账本；版本不匹配、取消/错误一致性测试 | SC-006（部分）、SC-007、SC-008 |
| P3-b 跨批预取（条件） | 仅当 P3-a 未达 SC-006：TaskExecutor(ASYNC) + ManagedAsyncMemoryGovernor 流水线 | SC-006、SC-007 |
| 收尾 | 真实云端点测量（FR-018）；评估文档改为正式支持；roadmap 更新 | 全部 SC |

P2 不依赖 P1-b，可与之并行；P3 依赖 P1-a 与 P2。

## Risks

| 风险 | 缓解 |
|---|---|
| 请求以 index 读为主，调大 data 合并无效 | P2 第一步先拆分 phase_requests，再决定是否同时放大 index 合并 |
| 合并读入大量间隙，局部查询变慢 | gap 上限 + SC-004 门禁；默认值以扫参数据选定 |
| HTTPFS 并发读共享状态问题 | TSan + 并发测试；失败则改句柄池 |
| 本地构建身份校验要求清单的 extension-ci-tools commit 等于子模块 HEAD | 清单该字段保持子模块值；CI 工具链另记 `ci_toolchain` 字段（research R2） |
| 2.0 工具链 osx_amd64 交叉构建与 verify runner 不兼容 | dry-run 首跑确认；必要时 2.0 暂缺该平台并在发布记录说明 |
| 社区 main 仍有 alpha 遗留 | 发布 pin 固定提交；登记文件本特性不改 |

## Complexity Tracking

无宪法违背项需要说明。唯一新增的结构复杂度是 Reader 三段式 API（P3-a），由异步协议要求"先提交 I/O、后恢复解码"决定；同步路径复用同一三段式按顺序执行，避免维护两套解码流程。
