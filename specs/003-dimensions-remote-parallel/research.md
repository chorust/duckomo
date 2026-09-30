# Research: Phase 4–5

日期：2026-09-30。维度、扫描生命周期与远程文件接口由三个 luna_worker 只读调研；主代理核对固定版本 httpfs 源码并完成取舍。本文件区分现有事实与待实施设计，不代表新能力已经运行通过。

## 1. 基线与时间兼容

**Decision**: 保留 `valid_time TIMESTAMP`、`valid_times` 参数、自动时间元数据与标量快照。新增 `axes` 参数，语义键 `time` 仍输出 `valid_time`，不增加重复的 `time` 列。

**Rationale**: 当前 HEAD 为 `c593b0e`，已经实现有效时间。`src/om/metadata.cpp` 的 ExtractTimeCoordinate/Walk 识别附着于数组的 Int64 `time` 一维数组和 Int64 `valid_time` 标量，单位为 UTC Unix 秒；`src/scan/read_om.cpp` 的 BindValidTime 检查轴长和多变量一致性。`README.md`、`test/native/time_test.cpp` 是现有兼容依据。`coordinates` 和 `dimensions` 只提供轴身份，不提供 level/member 等物理坐标。

**Alternatives considered**: 改名为 time 会破坏已有查询；同时输出 time 和 valid_time 会改变 SELECT * 且引入重复语义；从路径推导 run、time 会造成无证据坐标。

## 2. 坐标输入、类型与单位

**Decision**: `axes` 使用原始 STRUCT（绑定参数注册 ANY 后自行严格校验），键为 time/level/lead_time/member/run；每项指定轴名，加显式 values 或 regular 的 start/step。timestamp 保留微秒精度并归一 UTC；时长采用无月分量的 INTERVAL；member 保留 BIGINT/VARCHAR；level 采用有限 DOUBLE 并显式注明类型和单位。具体允许字段及单位封闭集合见 SQL 契约。

**Rationale**: 保留原始类型可避免先被 DuckDB 隐式舍入再校验。`ValidateAxisDeclarations`、BoundSchema 和 row-major stride 可复用；当前 ProjectionOutputSlot 的三个坐标布尔标记改成 typed output descriptor，避免继续累加固定偏移。现有 OM 证据只足以自动识别有效时间，其他四类本期明确采用显式映射，不宣称自动识别未核验格式。

**Alternatives considered**: JSON 字符串会损失 SQL 类型校验；统一 DOUBLE 会损坏大整数和微秒时间；为每种维度增加多个独立参数会重复结构和校验。

## 3. 联合选择与 residual

**Decision**: 推广 SpatialSelection 为 QuerySelection：每个原始轴保存不重叠半开下标区间或等价有界游标；空间与语义筛选取交集。规则坐标按受检算术生成，显式坐标按类型逐位置比较；不单调和重复坐标可形成多个区间，碎片超出预算时回退该轴全范围并记录原因。展平空间轴保留经纬相关性，以空间游标生成 point 区间，不把 lat/lon 当作两个独立物理轴。

**Rationale**: `src/scan/spatial_filter.cpp` 已正确保留 AND 中安全必要条件和完整 WHERE；`spatial_selection.cpp` 已提供跨额外轴的有界连续段。新比较器不能沿用全部转 DOUBLE 的空间做法。对每一轴扩大候选始终安全，误删匹配位置则不可接受。

**Alternatives considered**: 全域坐标笛卡尔积物化随行数增长；删除已提取的 WHERE 改变边界/NULL 语义；不安全 OR 分支下推可能漏行。

## 4. 远程依赖与严格读取边界

**Decision**: 复用 DuckDB v1.5.4 配套的 httpfs 提交 `c3f215ab360f04dc3d3d5305fa81849c0121f111`，将源码纳入固定构建依赖，并维护一个仅针对 duckomo range-session 的小型兼容补丁。补丁由显式 observer provider 启用，普通 httpfs 用户不改变行为；DuckOMO 远程读取只支持带该能力的配套构建。本地读取不依赖加载 httpfs。

**Rationale / 已核对证据**:

- DuckDB `.github/config/extensions/httpfs.cmake` 给出上述 pin；当前项目 `extension_config.cmake` 尚未加载 httpfs。
- 上游固定源码 [httpfs.cpp](https://github.com/duckdb/duckdb-httpfs/blob/c3f215ab360f04dc3d3d5305fa81849c0121f111/src/httpfs.cpp) 的 HTTPFSUtil::GetHTTPUtil、CreateHandle 和 GetRangeRequest 确认请求走 FileOpener/HTTPUtil；[s3fs.cpp](https://github.com/duckdb/duckdb-httpfs/blob/c3f215ab360f04dc3d3d5305fa81849c0121f111/src/s3fs.cpp) 的 S3FileSystem::GetRangeRequest 在签名时包含已知 versionId，再调用 HTTPFileSystem 的范围读取。
- 同版本默认允许 auto_fallback_to_full_download；GetRangeRequest 未强制精确 206/Content-Range；ETag 检查在响应缺少 ETag 时不会保证同版本。Initialize/LoadFileInfo 还能从 metadata 或完整文件缓存绕过新 HEAD。
- DuckDB `FileSystem::Read` 只保证 exact positional read；GetVersionTag 默认可为空，不能证明网络响应行为。
- `HTTPParams::http_util` 是构造时绑定的引用，httpfs 将其 Cast 为 HTTPFSParams。仅包装通用 HTTPUtil 并委托 InitializeParameters，不能保证返回参数的引用仍指向包装器；复制所有私有参数或更换全数据库 util 会引入维护及并发隔离风险。

补丁因此提供一个共享头定义的版本化 C++ observer/provider 接口（与固定 DuckDB/httpfs 一起构建），在 httpfs 内部、签名之前应用 range-session 策略，在实际响应边界记录事件。duckomo 的 opener 同时实现 provider 并委托原有 ClientContextFileOpener 的凭据、设置与权限；保留 OpenerFileSystem 的 CanAccessFile 校验。禁止用裸 GetLocal 打开 URI。

**Alternatives considered**: 普通 FileHandle 计数不能验收实际网络量；全局 HTTPUtil 替换影响其他查询；克隆内部参数结构依赖更多 ABI 细节；自行实现 S3 签名/凭据链重复上游职责；只接受预签名 URL 不满足既有 S3 凭据要求。

## 5. 版本、权限与远程缓存失效

**Decision**: 每个查询/扫描对象先以当前访问上下文执行新鲜 HEAD，确认长度和强 ETag 或非 null VersionId；S3 优先固定 VersionId，否则在签名前加入 If-Match。每次范围响应核对 206、Content-Range、总长、body 长度及可用版本。弱/缺版本仍允许扫描静态对象，但不跨查询复用；发生可检测变化就失败。对强版本已建立的会话，后续响应不能静默降级为无版本证据。

**Rationale**: 这实现 spec 对可检测变化的失败语义，同时保留其“无可靠版本则要求输入不变”的边界。热缓存也先验证当前对象和当前权限。缓存键不使用脱敏展示路径，而使用完整规范对象身份、端点、访问上下文分区和验证版本。

**Alternatives considered**: 路径+mtime/长度无法识别等长替换；只在第一次打开检查权限可能泄露已撤权的缓存；无 ETag 时把缓存设为短 TTL 仍违反 FR-016。

## 6. 并行状态所有权

**Decision**: 使用 table function 的 MaxThreads/init_local，global state 持有不可变绑定结果、受锁保护的惰性任务分配器、共享取消标记和查询指标；local state 持有独立文件句柄、decoder、index/data/scratch 和任务游标。上限=min(DuckDB threads、duckomo 上限、可用任务数)，不另建线程池。

**Rationale**: `duckdb/src/include/duckdb/function/table_function.hpp` 和 `physical_table_scan.cpp` 已定义 global/local 生命周期；当前 ReadOmGlobalState 集中持有可变 reader/cursor，不能直接并发。`OmDecoder_t` 借用传入 vector 的指针；`src/include/duckomo/om_reader.hpp` 要求 backing buffers 地址稳定。官方 decoder 无需改写，独占状态即可隔离并发。

**Alternatives considered**: 共享 decoder 加大锁实质串行且脆弱；每个微小 vector 都重建 reader 会重复元数据和分配；自建线程池会与 DuckDB 调度竞争。

## 7. 缓存的范围和容量

**Decision**: 使用 ClientContextState 拥有会话级 LRU 字节范围缓存，默认 64 MiB，允许禁用、设为零和清理。仅缓存官方 reader 已请求的准确范围，不读取对齐整页或预取值区；以 checked allocation budget 管理 payload、键和条目结构，单条超额则直读。首版不缓存解压数组，不合并并发 miss。

**Rationale**: DuckDB ExternalFileCache 是 DatabaseInstance 级而非会话级；其缺版本时的 mtime/时间窗口规则不满足本 feature。精确范围缓存不会因页对齐读取值数据而破坏空/坐标/count 门禁。采用 copy-out 使 LRU 内无外部 pin，占用上限可严格执行；工作者输出/decoder 缓冲计入另一个工作内存指标。

**Alternatives considered**: 全局缓存扩大访问上下文隔离负担；解码缓存与字节缓存叠加增加生命周期和预算复杂度；single-flight 不是首版正确性必需，不先引入。

## 8. 完成状态与可核验 profiling

**Decision**: ScanMetrics 升为 v3，保留 v2 字段含义，新增坐标读取、远程实际 body、缓存和任务计数；QueryEnd observer 作为权威终态。提供会话查询函数读取最近一次含 read_om 的完整记录，另保留 sidecar。扫描候选、scanner 输出和最终 SQL 结果行数区分，未知值写 NULL 及原因，不伪造零。

**Rationale**: 当前 metrics 已有互斥锁和每查询共享指针，可先复用；全局析构并不能判断下游 SQL 是否成功。进程 RSS 不是单查询独占内存：记录带 scope 的进程峰值及查询归属分配峰值，性能验收用独立子进程的 RSS。网络量只来自响应事件，重试/失败的已接收 body 同样累计。

**Alternatives considered**: 只看 EXPLAIN 或输出行数不能证明节省读取；合并所有工作者的全局快照会重复计数；将失败调用未计到的字节写为零会制造虚假收益。

## 9. 验证与构建

**Decision**: 继续 SQL/native、fixture tool、官方值 oracle、独立坐标表和 release harness；新建 dimensions/remote/parallel/cache 验收入口。HTTP 使用可控 range server；S3 使用固定版本的本地兼容服务及独立响应审计，另核对一个真实 OM 样本。实验 manifest 在计时前锁定样本、查询和缓存条件。

**Rationale**: 既有 `make test`、`scripts/validate.sh`、`test/CMakeLists.txt` 可接入；S3 仅测试预签名 HTTP 不能验证凭据与签名。上游 httpfs 使用的 TLS/HTTP 依赖由其固定构建配置承担，离线本地测试不得假装远程集成已通过。

**Alternatives considered**: 每次从公网可变路径取样不可复现；只有 mock 不足以说明真实 OM 兼容；规划时执行尚未实现的命令不能作为验收证据。

所有初始研究问题已形成明确设计决策；远程补丁的编译、跨扩展接口和服务端核验是实施阶段的首个必须通过的验证关卡，不是未选择的架构分支。
