# PR 切片 / 合入范围提案（2026-11-07）

## Snapshot

| Item | Value |
|---|---|
| Host `uname -m` | `aarch64`（linux_arm64） |
| HEAD = `origin/main` | `80ebe4bac2fa7b2fc29fd1c44cb55985baa6b9f9` |
| Tree | dirty, uncommitted；index 无 staged |
| Tracked diff | 38 files, +2080/−393 |
| Review baseline | [review-tree-r4.md](../review-tree-r4.md)：R3-01…04 已关；010 声明范围 PASS；008 **非** PASS；整脏树非无条件合入 |
| Evidence policy | [submission-scope.md](submission-scope.md) |
| 本文件 | **只写提案**；未 commit/push；未删/移/压/传 ~1.18G 证据；未建外部归档 |

`git status -sb` 摘要：`## main...origin/main`；tracked 含 README\*、docs、community、008 specs、shared `src/`/`test/`；untracked 含 010 series 源码/测试/脚本/规格、009 Draft、008 performance-gates、`.pi/`、大体积 `specs/010-read-om-series/evidence/**`。

---

## 推荐默认顺序（实用）

1. **PR-1（可选、可独立合入）community-only** — 社区登记回填，几乎无代码耦合。
2. **PR-2（主产品）008+010 合并** — reader/scan 混 hunk，**不要**三路拆代码；带瘦身证据。
3. **009 Draft、`.pi/`、nested roadmap、原始大证据** — 默认排除；归档另议。

若只要少 PR：把 PR-1 并入 PR-2 亦可；**不要**试图把 `read_om.cpp` / `om_reader.hpp` / `reader.cpp` 拆成 008-only 与 010-only 两个可编译中间 PR。

---

## 为何不能干净三路拆 reader

| 文件 | tracked Δ | 混杂内容 |
|---|---:|---|
| `src/scan/read_om.cpp` | +328/−142 | 008 planned-cache 窗口 / reuse 计量 **与** 010 series child bind、`series_statement_cacheable`、binding signature |
| `src/include/duckomo/om_reader.hpp` | +64/−2 | 008 `planned_cache` 作用域 RAII **与** 010 `ReaderErrorCategory` / `Stage` |
| `src/om/reader.cpp` | +57/−4 | cache lifetime / decode 路径与错误分类交织 |
| `src/scan/schema.cpp` 等 | 大 | 010 schema/registry 为主，但依赖同一 build/test 接线 |

`git add -p` 可挑行，但中间态很难单独通过 native/SQL；010 series 依赖新错误分类与 child-bind 语义。R4 已写明：**未来抽取必须整包保留这些依赖并复验抽取结果**。

---

## PR-1 — community publication sync（可选先合）

**Title:** `docs(community): sync published 0.3.5-rc.1 registration metadata`

**Include**
- `community/duckomo/description.yml.in`
- `community/validation.json`
- `docs/community-extensions.md`

**Exclude（留给 PR-2）**
- `docs/releases.md`、`docs/release-notes/v0.3.5-rc.1.md`（链到 008 performance-gates / 010 叙事）
- 一切 `src/`、`test/`、010/008 specs、证据树、`.pi/`

**Commit sketch（勿执行）**
```
docs(community): sync published 0.3.5-rc.1 registration metadata

Align local description.yml.in / validation.json with upstream community
registration. Artifact hashes remain TODO; no Spec 008 PASS claim.
```

**Risk / alone?** 低；可独立合入。不建立 signed-artifact 身份，不宣称 008/010 PASS。

---

## PR-2 — main product：008 async + 010 read_om_series（推荐合并）

**Title:** `feat: read_om_series (010) + async reader lifetime (008), linux_arm64 slim evidence`

### Include — 代码 / 构建 / 测试

- Build: `CMakeLists.txt`, `test/CMakeLists.txt`
- Compat / OM I/O（008 为主）: `src/compat/duckdb_api.cpp`, `src/include/duckomo/compat/duckdb_api.hpp`, `src/om/local_file.cpp`, `src/om/remote_file.cpp`, `src/om/reader.cpp`, `src/include/duckomo/om_reader.hpp`
- Shared scan（混 hunk，整文件进）: `src/scan/read_om.cpp`, `src/include/duckomo/read_om.hpp`, `src/include/duckomo/schema.hpp`, `src/scan/schema.cpp`, `src/om_extension.cpp`
- 010 series 新增: `src/scan/read_om_series.cpp`, `src/scan/series_plan.cpp`, `src/scan/series_registry.cpp`, `src/include/duckomo/{read_om_binding,series_plan,series_registry,generated_series_registry}.hpp`
- Scripts: `scripts/{generate-series-fixtures,generate-series-registry,series_remote,validate-series}.py`
- Tests: `test/native/{series_*,reader_capacity,remote_async,session_metrics}_test.cpp`, `test/sql/read_om_series*.test`, `test/tools/{duckomo_fixture_tool.cpp,series_*.py,validate_series_test.py}`, `test/data/series/`

### Include — 规格 / 顶层文档

- 010: `specs/010-read-om-series/{spec,plan,tasks,research,data-model,quickstart,review*.md,review-tree*.md,checklists/,contracts/}`
- 008 tracked 更新: `specs/008-duckdb2-async-io/{spec,plan,tasks,research,quickstart,checklists/success-criteria}.md` + `evidence/008/final/{index,status}.md`
- 008 小证据（untracked）: `evidence/008/performance-gates/**`（~656K）, `specs/008-duckdb2-async-io/evidence/review-fix-f008-01-linux-arm64/**`（~60K）
- 顶层: `README.md`, `README.en.md`, `docs/{spec,roadmap,architecture,duckdb-2.0-evaluation,releases,release-notes/v0.3.5-rc.1}.md`, `.specify/memory/roadmap.md`
- `.gitignore`（已有 `.pi/env.wake-parent.local`；见下方建议增补）

### Include — 010 瘦身证据（in-repo keepers）

保留可读指针与可追溯摘要，**不要**整棵 follow-up / run 树：

| Keep | 路径 |
|---|---|
| 策略 | `specs/010-read-om-series/evidence/submission-scope.md` |
| 本提案 | `…/evidence/pr-slice-proposal-20261107.md` |
| 当前指针 | `…/acceptance-20261106-followup-linux-arm64/report.md` |
| 顶层 manifest | `…/acceptance-20261106-followup-linux-arm64/manifest.json` |
| 小门禁 | `full-regression-status.json`, `official-httpfs-matrix.json`, `focused-python/commands.json` |
| 每 pair 小文件 | `corrected-source/<pair>/build/{build-manifest,runtime}.json`；`…/real/{report.md,manifest.json}`；`…/official-httpfs/result.json`；各 `*/report.md`（local / controlled-remote / local-corrected） |
| 历史指针（仅报告级） | `acceptance-20261106-linux-arm64/{report.md,manifest.json}`；`run-20260803-linux-arm64/report.md`（若存在且小） |
| Review / fix notes | `evidence/code-review/`（筛掉大 `.strace` 可再瘦）、`review-fix-*/fix-note.md`、`review-fix-r3-docs-p3/`、`review-fix-p2-docs-009/`；根目录 `review-tree*.md` |

**Indexed 44 文件合计约 113MB**，其中 4×~22MB `controlled-remote/manifest.json` + 4×~6MB `local(/-corrected)/manifest.json` 过重，不适合整包进产品 PR。推荐：**PR 内放 top report + 精简 evidence-index（路径/sha256/size 列表）+ 每 pair 小 report/build-manifest**；把完整 22MB/6MB manifest、`commands.json`、queries、strace、logs、OM 样例留给未来 checksummed 外部归档，并在 PR 的 evidence index 里留「归档位置 / SHA-256 / 检索说明」占位（归档未建则写 *TBD — not created yet*）。

### Exclude（硬排除）

| 排除 | 原因 |
|---|---|
| `specs/010-read-om-series/evidence/run-20260803-linux-arm64/` | ~1.2G / 5078 files — **禁止** raw 进产品 PR |
| follow-up 大宗 | `corrected-source/**/controlled-remote/{manifest.json,commands.json,queries,*.strace}`、`**/build.log`、outer `v1.5.x/` 与 corrected 重复树、`official-httpfs-fixture` 大件等 |
| `acceptance-20261106-linux-arm64/v1.5.*` 大宗 | ~55M 历史执行物 → 归档；仅留顶层 report/manifest |
| `.pi/` 全部 | 本地 agent；已 ignore `env.wake-parent.local`；**勿**提交 secrets |
| `.specify/extensions/roadmap` / nested `roadmap-config.yml` | 非产品 |
| `specs/009-point-site-nearest-sampling/` | Draft only（用户另选可再开 docs PR） |
| `docs/research/2026-11-06-enso-impact-scenario-coverage.md` | 研究笔记，非本切片必需 |

**Commit sketch（勿执行；可拆 2–3 个逻辑 commit，但仍同一 PR）**
```
feat(010): add read_om_series planner, binder, registry, tests
feat(008): planned-cache lifetime + compat/async reader updates (not PASS)
docs: align README/spec/roadmap; slim 010 follow-up evidence pointers
```

**Risk / alone?** 中高（共享 reader）；合入后应在 CI/linux_arm64 跑 series + 既有 native。**可单独作为产品 PR 合入**，前提是证据已瘦身且正文不宣称 008 PASS。不能在去掉 008 hunk 后只合 010。

**宣称边界（PR 描述必须写明）**
- 010：仅 **linux_arm64** × pinned v1.5.4/1.5.5/1.5.6/2.0.0 声明范围 follow-up PASS；真实数据仅 IFS025 两对象一点一窗；其余模型 synthetic equivalence；N160 合成；开发源码身份，非已发布包。
- 008：**非 PASS**；SC-006 relaxed / won’t meet；T042/T043 未勾。
- 社区产物 hash 仍 TODO；无新多平台执行证据。

---

## 证据：slim vs 未来外部归档

| 层 | 内容 | 何时 |
|---|---|---|
| **In-repo（本 PR）** | submission-scope、follow-up `report.md` + top `manifest.json`、精简 index（哈希表）、每 pair 小 report/build-manifest/runtime/real 摘要、008 performance-gates + F008-01 fix notes、review-tree/fix-note | 随 PR-2 |
| **External archive（尚未创建）** | 完整 `run-20260803-linux-arm64/`；follow-up/first-acceptance 的完整 controlled/local manifests、commands、queries、strace、logs、fixtures、重复 pair 树 | 用户批准后再做 checksummed 归档；**现在不做** |
| **本地保留** | 上述全部继续留在 spark 工作树；政策禁止删改历史 | 持续 |

归档清单占位字段：`archive_uri`、`archive_sha256`、`checksum_manifest_sha256`、`retrieve_instructions` — 未创建前标 TBD。

### 建议的 `.gitignore` 增补（提案文本；可选应用）

```
# Local pi agent tree (env secret already ignored)
.pi/

# Bulky 010 histories — keep on disk; ship via external archive, not product PR
specs/010-read-om-series/evidence/run-20260803-linux-arm64/
```

**不要** ignore follow-up 整树（否则连 keepers 一并挡住）；大宗用 `git add` 白名单，勿用 ignore 误伤 `report.md` / top `manifest.json`。当前已有 `.pi/env.wake-parent.local` 可保留。

---

## Do-NOT（执行约束）

- 不 auto-commit / push / amend / rebase / reset / 破坏性 stash
- 不删除、移动、压缩、上传 ~1.18G `run-20260803-linux-arm64/` 或其它历史证据
- 现在不创建外部归档
- 不宣称 Feature 008 PASS；不把 SC-006 放宽写成达标
- 不把 `.pi/`、env secrets、nested roadmap config 写入产品 PR
- 不把 010 PASS 外推到其它 arch / 已发布包 / 全 registry 真实网格

---

## 仍需用户拍板

1. **归档时机**：现在只提案 vs 批准后立刻做 checksummed 外部归档（位置/工具自选）。
2. **PR 形状**：community 先合再主 PR，还是单一合并 PR。
3. **009 Draft**：排除（默认）还是另开极小 docs-only PR。
4. **Follow-up 22MB manifest**：PR 内只留哈希指针（推荐）还是容忍 ~113MB indexed 全集进仓。
