# DuckOMO spec 005 当前 HEAD 一致性审计

日期：2026-10-21  
范围：只读核实 `/home/blizhan/repo/github/duckomo` 中 spec 005 的当前有效需求与源码 HEAD；未修改源码、任务勾选、roadmap 或提交。唯一审计输出为本文件。

## 结论与证据身份

审计目标 HEAD 为 `6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`。在下列逐项代码路径和测试范围内，未确认高/中严重度的运行时行为与当前有效需求冲突。发现一项低严重度的 64 MiB 区域存储预算执行契约缺口，以及若干已被后续决定取代、但仍留在规范/任务/roadmap 文字中的状态漂移；详见“发现”。这些不构成对整个项目或全部验收 gate 的重新签署。

协调者提供并已复核的证据身份为当前源码摘要 `886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e`；协调者核对了三版本 336 对文件路径/哈希，及 306 条留存命令均为 `exit_code=0`。三版 build manifest 属 `build/evidence/regions/current-20261020/matrix-testfixed/<version>/build-manifest.json`，build ID 分别为 v1.5.4 `5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9`、v1.5.5 `10fb4c1331f717e97196b6fb6990cacbab48cd44646c6a51ea4aa371ae577f52`、v1.5.6 `ab0257d665a469cae1f1e33da2449c91009137e745262de32b2ad447a00d3e5e`。这些是现有证据的身份/留存命令核对，不是本审计重新运行的三版本矩阵。

当前验收应以 `specs/005-polygon-region-statistics/evidence/current-20261020.md:3`、`:5`、`:14`、`:24` 和 `contracts/non-increasing-cost-20261020.md:3`、`:8` 为准：只覆盖 Linux AArch64、v1.5.4–v1.5.6、五个冻结目标及固定较大局部窗口；每个 HTTP/S3 对照的三项成本都不得增加，且至少一项严格下降。旧 SC-004 全域远程收益未执行、未宣称。P7 是未参与实现的 fresh-context AI 复现，不是人工验收（`current-20261020.md:3`、`:40`、`:42`）。不能把较早快照中的验收文字当成当前源码证明。

## 逐需求对照矩阵

| 需求 | 当前实现（源码锚点） | 直接测试 / 当前证据 | 判断 |
|---|---|---|---|
| FR-001、FR-005–008；SC-001 | `src/scan/read_om.cpp:2457` 区分普通与区域绑定，`:2473`–`:2480` 验证必填区域参数并构造完整集合；`:2608`–`:2646` 为每个区域建 geometry/predicate；`src/scan/region_geometry.cpp:168` 绑定 `ST_Covers`；`src/scan/read_om.cpp:2980` 起按 task 区域做包围盒和精确谓词筛选，`:3385`–`:3402` 先精确筛选再为值列建 decoder segments，`:3464`–`:3469` 写出区域 ID，`:3510` 解码所选原始位置；`:2762` 起拒绝 `region_id` 冲突。 | `test/sql/regions.test:291` 共享空间集成对照，`:324` 使用独立 Python membership oracle 做双向 `EXCEPT ALL`，`:348` 覆盖不同 ID/边界/洞，`:473` 覆盖同坐标不同逻辑位置；`test/native/region_scan_test.cpp:172`、`:210`、`:245`、`:356` 覆盖重复坐标、空区、十区混合形状、跨 task 区域 ordinal 和多 value。 | 满足。读路径保留每个区域与原始逻辑位置的多重集合语义，geometry 精筛发生在值解码前。 |
| FR-002–004；SC-003 | `src/scan/region_input.cpp:78` 的 WKB reader 只接纳 Polygon/MultiPolygon；`:155` 检有限坐标、经纬度和相邻经度跨度；`:186` 检查 ring 结构、闭合和顶点限额；`:218` 验证 LIST/STRUCT 字段名、字段类型；`:242` 验证整个集合、唯一 ID、总限额；`:334` 精确要求 `grid_lonlat`。绑定阶段 `src/scan/read_om.cpp:2614`–`:2617` 调 spatial adapter 做拓扑有效性检查。 | `test/native/region_input_test.cpp:183`–`:223` 覆盖空值、ID、类型和 WKB 结构；`:223` 起覆盖非有限值、接缝、极区、非法类型/拓扑输入；`:259` 起测 4096/4097、每项/总 WKB、顶点、geometry 和取消边界。SQL 负例在 `test/sql/regions.test:180`、`:190`、`:209`。 | 满足首版约束：明确拒绝非受支持坐标/几何，不自动修复；合法 EMPTY 与空集合可表达。 |
| FR-009–011；SC-002、SC-003 | `src/scan/read_om.cpp:3422` 起从逻辑位置输出已有时间/语义轴，`:3431`–`:3454` 输出坐标及语义维度；统计由普通 SQL 聚合组合，不引入专用加权/缺测改写。 | `test/sql/regions.test:386` 起验证等权 SQL 聚合、样本数与有效值数，`:426` 校核 10 区×3 时刻×2 member 的冻结统计参考；`:499` 覆盖 NaN/Inf 宿主聚合语义；`:519` 验证无时间轴时不猜时间列。`quickstart.md:65` 起提供原始结果、聚合及空/全 NULL 补齐流程；当前独立复验摘要见 `current-20261020.md:42`。 | 满足。原始入口不造空样本；SQL 明确区分 0 样本与有样本但全 NULL。统计为等权点样本，不声称面积平均。 |
| FR-012、FR-014、FR-018、FR-019；SC-004 | `src/scan/spatial_selection.cpp:469` 起从必要空间条件构造受界候选；`:508`–`:515` 区分 empty/restricted/fallback/full；`src/scan/read_om.cpp:3054`–`:3078` 做候选包围盒、既有空间约束及精确几何判断；`:3510` 通过 OM reader 解码；远程入口在 `read_om.cpp:2493` 起复用 RemoteReadSession。 | 当前完整证据 `current-20261020.md:28`–`:38` 记录五目标×四来源×两查询、每版 40 次 P5 扫描和三项成本；NCEP 值字节/服务字节相等、解码下降，其余目标成本未增加。冻结窗口契约 `contracts/bounded-remote-validation-20261013.md:9`–`:17`；现行成本契约 `contracts/non-increasing-cost-20261020.md:3`–`:8`。 | 在现行修订范围内满足。SC-004 原文中的全域与“三项都严格下降”不再是当前 gate；全域性能不在通过声明中。 |
| FR-013；SC-005 | `read_om.cpp:3352`–`:3355` 根据投影依赖决定是否需要值变量；`:3385`–`:3398` 对区域精筛后空批次跳过值读取；`:3401` 在无值依赖时不建 decoder segments；绑定的 required variables 保留值过滤依赖。 | `test/native/region_scan_test.cpp:394`、`:539` 覆盖零值 I/O、空选和过滤路径；`test/sql/regions.test` 的投影/值过滤用例；当前 `current-20261020.md:22` 报告三版包含零值路径及投影/筛选回归。 | 满足。仅位置/归属/无值计数路径保持零值解码，值过滤列仍被要求。 |
| FR-015–016；SC-006 | `src/scan/read_om.cpp:1900`–`:1911` 限制 region worker 数；`:2128` 起惰性地逐区域取 bounded position task，`:2150`–`:2166` 限制 task position map 为 512 KiB；`FilterRegionCandidates` 的 `:2998`–`:3022` 限制 worker geometry scratch；`src/scan/region_geometry.cpp:193` 限制每批 256 点；异常/取消由 task 状态记录和扫描恢复路径处理。 | `test/native/region_scan_test.cpp:427`、`:461`–`:477` 验证区域输入与 scan work 分账及 owned peak；`:643` 覆盖 LIMIT 后精确状态和恢复；`:676`、`:714` 覆盖多连接隔离及对象变化失败恢复。当前 `current-20261020.md:18`–`:22` 记录每版 17/17 sanitizer；P6 `errors=[]`、`not_run=[]` 见 `:5`。 | 当前留存证据支持满足。第三方 GEOS/spatial 内部分配与 RSS 不伪装为精确自有内存；相关限制在 `contracts/execution-and-metrics.md:43`–`:45` 中明示。 |
| FR-017–019 | 普通/区域入口共用 scanner，但只有 `regions_mode` 分支构造 spatial adapter；`src/scan/read_om.cpp:2459`–`:2482` 与 `:2596`–`:2602` 显示依赖隔离；`GetReadOmRegionsFunction` 注册区域参数及 projection/filter 设置见 `:3678`–`:3700`。指标终态与未知值规则见 `src/include/duckomo/metrics.hpp:1705`–`:1725`。 | 无 spatial 的隔离 SQL 测试 `test/sql/regions_spatial_dependency.test:12`–`:54` 验证普通入口仍可用、区域入口明确失败；冻结输入及成本审计见 `current-20261020.md:5`、`:22`、`:28`–`:38`；三版本/四来源细节由协调者提供的路径-hash/命令留存核验支持。 | 满足声明范围。指标把服务端发送量与客户端接收量分开，未知/不完整不补零。 |
| FR-020；SC-007 | 用户复现步骤见 `specs/005-polygon-region-statistics/quickstart.md:11`–`:28`、`:45` 起的 SQL 示例；要求的局部窗口对照定义见 `contracts/bounded-remote-validation-20261013.md:19`–`:23`。 | 独立报告 `evidence/P7-testfixed-20261020.md:15`、`:21` 起；当前收口 `current-20261020.md:40`–`:44` 记录 AI fresh worker、108 原始行、统计/空值及一次 HTTPS 固定窗口对照，并说明核验包 hash。 | 在声明范围满足，验证者是独立 AI，非人工；P7 的单目标远程对照不意味着五目标 P5 的证据由 P7 替代。 |

## 发现

### 低严重度：64 MiB owned-region cap 没有在“区域集合 + opaque geometry”合计值上执行最终拒绝

冻结契约 `contracts/execution-and-metrics.md:33` 写明 64 MiB 已拥有区域存储包含 ID、WKB、opaque GEOMETRY 与 descriptor capacity，并要求持久化前检查。代码在 `src/scan/region_input.cpp:254`–`:280` 的预检只加 `Region` descriptor、ID 长度和 WKB 长度；`:316`–`:322` 保存的 `RegionCollection::owned_bytes` 也不加 `region.geometry_bytes`。后续 `src/scan/read_om.cpp:1529`–`:1534` 与 `:1537`–`:1547` 会把 geometry 估算另计；但该估算在 `:2653`–`:2662`、即所有 geometry/predicate 已构造之后才进入指标账本。`ScanMemoryAccount::Set` 在 `src/include/duckomo/metrics.hpp:1720`–`:1725` 只更新指标，没有 cap 检查或拒绝。

因此，契约要求的“合计 owned storage 超过 64 MiB 时在持久化前拒绝”没有由同一个最终合计值实现。现有 1 MiB/区域、16 MiB 总 WKB、1048576 总顶点和 2 MiB/区域 geometry 限额对当前模型提供了强约束；本次没有证明有一组仍合法的当前输入会实际越过 64 MiB，也没有观察到内存越界。`test/native/region_scan_test.cpp:461`–`:475` 只检查代表样本的计量和 `<=64MiB`，没有构造“合计 geometry 使存储越限”的拒绝用例。判为低严重度执行/回归保护缺口，而非已证实利用路径或现存超限事实。

### 低严重度：当前有效状态与历史任务/roadmap 文案互相矛盾

- `specs/005-polygon-region-statistics/spec.md:7` 仍标 `Draft`；同文件 `:136` 的 SC-004 原始成本文字未内嵌 2026-10-20 修订注记。现行规则由成本契约与当前验收报告优先解释。
- `specs/005-polygon-region-statistics/tasks.md:189` 仍要求 HTTP/S3 三项成本全部严格下降，`:190` 仍写 16 个 sanitizer；当前成本契约允许各项相等但至少一项下降，当前三版清单为 17 项（`current-20261020.md:10`、`:18`–`:22`）。任务主列表 50 项完成状态是记录信息，不能替代当前源码证明。
- `.specify/memory/roadmap.md:219`–`:225` 已写当前 `verified`、非递增成本决定及新证据，但 `:228` 仍写三项都严格下降，`:231` 仍复述 16 项与旧 P5 数值/未重跑状态。较早 `evidence/final.md:13`–`:17` 明确是 2026-10-14 历史段，后续 `:85`–`:96` 的遗留叙述需按 `current-20261020.md` 中“当前收口优先”的声明解读。
- `evidence/final.md:90` 仍概括 decoder 只能在一次调用内合并、跨独立调用不复用。当前 HEAD 的 `src/om/reader.cpp:379`–`:430` 支持同一 decoder 状态复用仍覆盖当前范围的 index/data buffer；`test/native/reader_capacity_test.cpp:256`–`:274` 明确检查相邻独立调用的复用。因此这段历史限制描述过宽，不能拿来描述当前 reader 行为。当前已知限制应结合 `current-20261020.md:48` 与实际 reader 范围命中条件解释：矩形批次减少部分连续读取的重复解码，但不承诺任意非相邻/小窗口远程访问都会减少。

## HEAD 6b34e52 定向风险核查

最新提交新增 `BuildContiguousBatchSegment`：`src/scan/batch.cpp:26`–`:51`，仅在完整选择尾轴且起点对齐时扩展到更早轴；否则返回当前精确连续段。普通 raw scanner 在 `src/scan/raw_scan.cpp:153`–`:165` 使用 `BuildBatchSegments`；区域/普通选中读取在 `src/scan/read_om.cpp:3401`–`:3402` 使用 `BuildSelectedBatchSegments`；空间 cursor 在 `src/scan/spatial_selection.cpp:660`–`:674` 使用批次段。所见调用链保持源逻辑位置顺序、输出 batch offset 及 gaps。

提交配套覆盖较小 shape 的所有合法区间起点/长度，并展开 rectangle 的每个坐标检查边界、无 gap/duplication（`test/native/batch_test.cpp:83`）；稀疏选中保留 gap 的用例在 `test/native/axis_selection_test.cpp:77`；官方 reader 位级对照与解码块 35→5 在 `test/native/reader_capacity_test.cpp:309`–`:335`。另有当前证据 `contiguous-rectangle-performance-20261020.md:13`–`:16`。在这些实现/测试范围内没有发现该提交增加或重复遗漏行的路径。真实固定小窗口可能无 I/O 改善、物理字节可能上升，性能证据已在 `current-20261020.md:38`、`:48` 及性能记录 `:23`–`:34` 保留，不能外推为普遍加速。

## 验证执行与限制

本审计前段执行过以下匹配 v1.5.4 既有 release 的小型 native 检查，没有重建共享 build，也没有启动外部服务或完整 SQL/版本矩阵：

```text
build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release/test/native/batch_test
build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release/test/native/axis_selection_test
build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release/test/native/reader_capacity_test
build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release/test/native/region_input_test
build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release/test/native/scan_metrics_v4_test
build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release/test/native/region_geometry_test build/regions-runtime/v1.5.4/official/spatial.duckdb_extension af2547875834496f3112261631cc8d8c96ae67912edab6b4e3d5f4f7545e547a
DUCKOMO_SPATIAL_EXTENSION=build/regions-runtime/v1.5.4/official/spatial.duckdb_extension build/official-matrix/v1.5.4/builds/5a8700f3eb073f9f753d5cc5dc5d75b20c22ba622281228bd2a5a5e39dd971d9/release/test/native/region_scan_test
```

以上 native 检查通过。区域扫描测试覆盖 task window、region ordinal、零值 I/O、metrics、取消、短读/decoder/object-change 故障后的恢复和多连接生命周期；reader 对照位级相同、解码块 35→5。region geometry/scan 两项若未提供匹配 spatial 参数会先打印 usage；补齐该参数后执行通过。

本次没有重新运行全部 SQLLogicTest、sanitizer 矩阵、P0–P7、五目标远程服务比较，也没有独立重算三版本 336 对路径/哈希或重放 306 条命令。全域远程比较按 2026-10-13 决定未运行。故矩阵中跨版本/服务/独立验收结论以当前摘要和协调者提供的留存证据身份为限；本报告的核心是源码调用链核查、HEAD 变更风险及少量已有 native 测试，不将历史验收视为无条件当前代码证明。
