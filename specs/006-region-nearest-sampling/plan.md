> Final acceptance: N0–N7 passed for the declared Linux AArch64/three-version synthetic and fixed GFS025 scope. See [final evidence](evidence/final.md). Earlier progress descriptions below are historical. Raw GFS is privately retained with owner authorization; no public redistribution.

# Implementation Plan: 统一区域入口的最近邻采样

**Branch**: `main`（实际Git分支，未切换） | **Date**: 2026-10-15 | **Spec**: [spec.md](spec.md)

**Feature directory**: specs/006-region-nearest-sampling。setup-plan返回BRANCH=006-region-nearest-sampling是feature key，不代表创建了同名Git分支。

**Input**: 用户确认将points/nearest保留在现有read_om_regions，避免新增函数；仅006最近邻范围，不实施长期提案中的bilinear/area/Line。

## Summary

在GetReadOmRegionsFunction注册method，缺省/points严格保留005的原始选点/schema，nearest使用polygon的point_on_surface位置、已验证规则球面网格上的真实距离最近源点，保留原变量名/FLOAT及所有非空间轴，每个区域×原始轴组合产生一行。新增sampling STRUCT解释方法、目标/源位置、距离和逐变量状态；include_source复用原参数及真实源记录身份。

内部复用既有metadata/binder、RegionCollection、SpatialLayout、OM官方reader、HTTPFS、worker/QueryEnd，增加nearest-only目标计划与非空间任务游标。定位层不读值，初版以流式全坐标穷举确保f64平局正确且O(1)定位scratch；每区域只定位一次，值读取仅访问选定源。子字段投影通过DuckDB ColumnIndex及supports_pushdown_extract区分sampling几何信息与值状态。无新SQL函数、缓存、物理块规划器或通用计算DSL。

## Technical Context

**Language/Version**: C++17、GNU C11；Python3仅开发期fixture/reference/验证，运行查询不依赖Python/Swift。

**Primary Dependencies**: DuckDB v1.5.4/v1.5.5/v1.5.6及匹配spatial，远程官方HTTPFS；OM C reader `d8855e418e2231ae8439f0c7e840fa3f93b371e3`，extension-ci-tools `b777c70d30942cca5bef62d6d4fa23a13362f398`。准确版本manifest沿现有test/data/grids/version-matrix.json；三版本内点函数实际探测、nearest本地实现/投影/资源gate及声明范围内N1/N2 gate已通过；远程/真实输入、全矩阵审计和独立复现仍未完成。

**Storage**: 单个稳定OM v3本地/HTTP(S)/S3对象，输入几何及计划只在绑定/查询生命周期，无数据库迁移或持久索引。

**Testing**: 独立全坐标数值/官方原值oracle、C++native、SQLLogicTest、Python验证审计、匹配spatial的sanitizer、受控HTTP/HTTPS/签名S3与独立复现；N0–N7分项。

**Target Platform**: Linux AArch64/linux_arm64三版本；不扩展x86/WASM/DuckDB2.0声明。

**Project Type**: 既有DuckDB C++扩展的单入口功能增强，不新增公共table function。

**Performance Goals**: 坐标/无值计数零值index/data/decode；固定可跳块样本的局部值字节/解码严格少于同变量完整读取；16×空间源点固定其它配置的自有工作峰≤2×。定位CPU可线性增长并独立报告，不承诺固定p95或任意布局收益。

**Constraints**: 全输入先校验、points兼容、目标筛选与源位置分离、未知不补零、无全域坐标/任务表、官方reader负责物理块、DuckDB调度线程、实际源身份和结果坐标不混同。

**Scale/Scope**: 继承4096区域、1MiB单WKB/16MiB总WKB/64MiB自有输入；plan≤8MiB、全局调度≤8MiB、worker新增scratch≤512KiB；nearest结果任务最多2048行并动态缩小；完整reader/output/第三方内存分别计量。仅规则球面nearest和默认points。

## Constitution Check

.constitution仍为未填写模板，不把示例当成批准原则。下列gates来自当前项目契约、006spec及用户决定；不虚构ratification。

| gate | 约束 | 当前证据与边界 |
| --- | --- | --- |
| 同一read_om_regions入口、默认points兼容 | 必需 | method-aware分支，旧schema/依赖不变；N0及US2兼容范围通过 |
| 不引入新公共函数/第二个主要mode | 必需 | 只增加method及必要阈值/域外参数；接口和catalog检查通过 |
| 普通read_om无新增spatial依赖 | 必需 | 空间依赖隔离SQL测试通过 |
| 轴/grid/位置不能凭shape/path猜测 | 必需 | 合成布局、独立locator/value参考通过；N1声明范围内能力profile已通过 |
| 目标WHERE不能裁掉源点 | 必需 | 三版本nearest仅下推安全非空间条件，完整residual双向差分通过；N3 gate通过 |
| 官方OM reader/HTTPFS职责不替换 | 必需 | 选定逻辑位置由官方reader读取，nearest不实现LUT/transport替代 |
| 内存/取消/终态有界，unknown诚实 | 必需 | 本地预算/生命周期及N4注册范围通过；受控远程与后续全矩阵审计未闭环 |
| 正确性与I/O收益独立，源与值可追溯 | 必需 | points基线、声明范围内locator/value oracle及N0–N3证据已通过；真实输入仍待N5 |
| 本轮只生成设计，未来算子不偷入006 | 必需 | 实现仅限既有区域入口points/nearest；无新公共函数或未来算子 |

当前实现与验证状态见 [tasks](tasks.md)、[historical N0–N4 package](evidence/run-20261017-sampling-validation-final/) 和 [current N1/N2 scoped package](evidence/n1-n2-acceptance-refresh-20261019-final-7/)；006整体因N5 gate尚未完整、N6/N7未完成而未验收。真实Dongcheng几何已有固定ODbL来源，GFS原始对象的再分发许可和持久archive仍是完整N5的前置；缺失时不得宣称raw数据可公开再分发。

## Project Structure

### Documentation (this feature)

```text
specs/006-region-nearest-sampling/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── checklists/requirements.md
├── contracts/
│   ├── sql-interface.md
│   ├── numeric-and-capability.md
│   ├── execution-and-metrics.md
│   └── validation-evidence.md
└── tasks.md                         # 65项实施/验收任务；56项有对应证据
```

### Source Code (repository root; implemented feature layout)

```text
src/grid/
├── nearest_sampling.cpp             # 新：资格、覆盖、球面距离和流式定位
└── spatial_layout.cpp               # 加：nonspatial+x/y到真实logical index受检映射
src/scan/
├── region_sampling.cpp              # 新：options/immutable计划、非空间cursor/mapping
├── region_geometry.cpp              # 加：nearest-only内点公开函数adapter
├── read_om.cpp                      # method绑定/schema/filter/init/scan路径
└── projection.cpp                   # sampling nested leaf与状态依赖
src/include/duckomo/
├── nearest_sampling.hpp             # 新：无SQL/decoder的grid接口
├── region_sampling.hpp              # 新：目标/任务与轴映射
├── spatial_layout.hpp
├── region_geometry.hpp
├── projection.hpp
└── metrics.hpp                     # sampling additive/单位/内存
src/compat/duckdb_api.cpp/.hpp        # 仅实际宿主版本差异
scripts/
├── generate-sampling-reference.py   # 新：独立数值/布局/状态参考
├── validate-sampling.py             # 新：N0–N7执行与证据审计
├── fetch-region-runtime.py          # 扩准确内点探测，不改历史记录
└── validate.sh                     # 注册需要spatial的采样测试，不允许静默跳过
test/native/
├── nearest_sampling_test.cpp        # 新：纯定位与独立oracle
└── region_sampling_test.cpp         # 新：真实扩展的schema/轴/状态/I/O/lifecycle
test/sql/
├── region_sampling.test             # 新：006接入/投影/过滤/错误
└── regions.test                     # 保留并增省略/points差分
test/tools/
├── duckomo_fixture_tool.cpp         # 增sampling-output，无网络fixture重生
└── sampling_evidence_test.py        # 新：validator和证据拒绝矩阵
test/data/sampling/                 # 新：合成OM/WKB/reference/manifest/来源描述
CMakeLists.txt / test/CMakeLists.txt / Makefile
```

**Structure Decision**: 纯grid定位、region目标/轴任务和SQL adapter分开，但只通过既有区域factory暴露。read_om.cpp原匿名namespace状态不外泄成通用框架；新增数据模型只解决nearest的真实复用缝。共享projection/metrics/CMake按依赖顺序集成，禁止多个writer并发改同一文件。没有引入xtensor/通用算子注册系统。

## Phase 0 — Research outcomes

[research.md](research.md)已将未知项整理为10个Decision/Rationale/Alternatives：

1. 同入口method、最小参数面与points兼容。
2. nearest任务必须是区域×非空间轴，不能沿用polygon covers源点游标。
3. 公开spatial内点与X/Y签名；三版本本地依赖探测成功。
4. 规则球面资格、unwrapped区域覆盖、显式周期证明和当前sphere默认来源。
5. 流式全坐标基线与f64距离/最小point_index平局，正确性优先，不先承诺候选剪枝。
6. 固定sampling结构、原变量名、候选/成功来源和混合缺测状态。
7. 完整ColumnIndex/STRUCT叶投影；MAP状态保守依赖全部所选变量。
8. method-aware residual与无源结果行生成。
9. 资源预算、CPU与I/O分列及诚实终态。
10. 无网络合成fixture、真实输入来源缺口、三版本与N0–N7。

没有未决产品选择或技术占位。研究证据含用户指定GPT-6 Luna max的两路只读收集、父代理源码核对、三版本依赖实际探测和小规模数学实验。研究不是实现验收。

## Design artifacts

- [data-model.md](data-model.md)：options、capability、targets/plan、非空间轴cursor、源映射、nested projection及状态/生命周期。
- [contracts/sql-interface.md](contracts/sql-interface.md)：准确参数类型、schema、NULL/状态/来源、默认兼容及错误。
- [contracts/numeric-and-capability.md](contracts/numeric-and-capability.md)：资格/覆盖、数值核、极点/平局、基线算法和独立参考。
- [contracts/execution-and-metrics.md](contracts/execution-and-metrics.md)：现有符号接入、投影依赖、内存、线程、取消与v4 additive字段。
- [contracts/validation-evidence.md](contracts/validation-evidence.md)：合成/真实fixture、N0–N7、版本与运行证据/阻塞政策。
- [quickstart.md](quickstart.md)：现有准备与sampling-validation命令、入口选择、method切换及来源/状态复现。

## Original implementation sequence (now tracked in tasks.md)

1. **N0/参考冻结**：保存旧points完整schema/多重集与变量成本基线，扩三版本公开签名探测；建立006合成manifest和独立oracle。先验证规范，不改reader。
2. **纯定位**：规则资格/unwrapped覆盖、sphere_haversine_f64_v1/极点/point tie、streaming locator及布局反向映射，N1穷举参考和拒绝案例。为优化另设证明，不阻塞正确性基线。
3. **绑定与schema**：method/options、nearest-only内点adapter、全集定位/strict检查、Copy/Equals/owned预算、sampling准确类型和冲突；points分支保持不变。
4. **非空间输出与解码**：nearest cursor、无源状态行、真实logical/source输出、原FLOAT/NaN语义、多变量与多轴组合；批内sort/unique/scatter复用原DecodeSelection。
5. **投影/filter/指标**：完整ColumnIndex叶输出和MAP状态依赖，nearest只安全轴条件下推；value/target/source/状态组合测试与N3成本；method/单位/owned ledger和QueryEnd完整性。
6. **资源与错误回归**：N2–N4、1/2/4workers、16×内存、取消/短读/损坏/对象变化/重绑定、无spatial普通读取和旧005兼容，ASan/UBSan。
7. **真实/远程/矩阵**：先冻结真实区界来源与GFS archive；N5同源直接读取和受控远程完整差分；N6三版本证据审计，N7独立指南复现。更新README中英文、docs/spec/architecture/roadmap，不把未来方法写成已实现。

上述顺序已落实为 [65项tasks](tasks.md)。这是设计阶段记录的依赖顺序，不表示所有gate均已验收。当前实现和局部gate状态记录在 [运行证据](evidence/run-20261017-sampling-validation-final/)；真实/远程N5、全矩阵N6和独立复现N7仍待执行。

## Compatibility and migration

无需数据迁移。旧SQL不加method时schema/数值/来源完全不变；nearest是显式新语义，其lat/lon为目标而非源，COUNT计目标结果，sampling说明来源。方法绑定时确定，宿主rebind需重做schema。采样元数据字段名只对nearest冲突；旧points保留原保留名集合。

metrics保留operation和原points单位，nearest新增sampling区段；旧v4/legacy字段不重新解释成另一种计数。没有把未来bilinear多源结构提前塞进本期，来源只用既有om_source。

## Risks and stop conditions

- 任一三版本无法提供准确公开函数或nested leaf输出时，相应gate阻塞；不引入新SQL函数/私有GEOS绕过。
- 旧points schema/边界/结果变化，或WHERE目标筛选裁掉源位置时先修复，不降低兼容条件。
- 全坐标定位CPU在大量区域可能高；明确计时/比较数，不能靠无证明舍入或剪枝冒充同契约。后续优化以证据决定。
- 内存预算不是全查询/进程上限；未计第三方或reader大块不可假称受512KiB限制。16×增长无法解释则N4不通过。
- 本地北京几何来源与可再分发条件未认证；真实原文件不可获取时N5 blocked，不能静默换区界或日期通过固定案例。
- source NULL与未定位、源缺测、超距不混同，权限/损坏不能降为NULL。
- 后续004/005历史完整gate不因本计划自动升级；006只按自身冻结输入与实际运行记录验收。

## Complexity Tracking

无治理例外。增加纯grid locator、region非空间cursor及STRUCT子字段投影有具体必要：满足真实地表nearest、无源结果多维基数和几何详情零值I/O。更简单的逐轴舍入、原points过滤和顶层STRUCT全读分别违反准确性、目标语义及成本要求。未新增公共函数、通用算子DSL、KD-tree、缓存或第二套物理解码。
