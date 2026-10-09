# Data Model: 多类型网格与源位置

日期：2026-10-03。设计契约，尚未实现。SQL 字段见 [sql-interface](contracts/sql-interface.md)，预算及生命周期见 [selection-and-io](contracts/selection-and-io.md)。

## GridDefinition / GridIdentity

封闭 variant：LegacyRegularGrid、RotatedLatLonGrid、LambertGrid、StereographicGrid、ReducedGaussianGrid。新定义包含 version、type、numeric_policy、earth、layout、parameters 和坐标来源规则。native x/y 的长度、单位、方向与地理 latitude/longitude 分开；投影/旋转采用球面，Gaussian支持封闭WGS84来源描述或明确球面行表。球面半径不能默认为WGS84 datum，Gaussian的来源CRS声明不意味着椭球投影或静默替换近似行纬度。

`grid_id` 是版本化 canonical definition 的 SHA-256；不含 domain 名、对象路径、支持等级、注释或测量结果。包含影响坐标的每个数值/算术规则、earth、点序/布局及区域映射；相同参数下 separate 与 flattened 是不同布局身份。额外 time/member 等轴在 `layout_id` 中，不改变空间定义。完整网格parent_grid_id=NULL；区域parent_grid_id为相同earth/numeric_policy/latitude_rule/rows但subset_segments=NULL的完整Gaussian canonical grid_id，区域自身grid_id引入局部映射。显式与 domain 规范化后相同才是等价身份。生成器的源原点构造历史保存在provenance，进入canonical的是按固定数值规则构造后的x0/y0及实际运行规则；不可仅因相同x0/y0来自不同脚本而给不同身份，也不可因表达式不同却实际算术不同而宣称等价。

Canonical 格式 `duckomo-grid-v1`：固定字段顺序及 UTF-8 枚举，整数采用十进制无前导零；浮点使用确定的 IEEE-754 binary64 十六进制位串（统一 -0 为 +0，拒绝 NaN/Inf），数组保留原始顺序；f32 来源先按已固定规则量化，再编码 widen 后的位串。哈希输入使用带长度的字段/数组编码，禁止依赖 JSON 空格、宿主字节序、locale 或 unordered_map 次序。生成器与运行内核共用 golden serialization vectors。

校验：正轴长/半径、有限角度/步长/原点、受检乘积/前缀和、受支持单位、投影有效参数域、latitude∈[-90,90]、longitude∈[-180,180)。Lambert 重合标准纬线用解析极限，反号/近退化导致无法定义的组合拒绝；stereographic 中心单独处理，反投影奇点和旋转极点依发布的确定性规则处理。对 bounded affine native rectangle 的参数域校验及解析界先排除未定义点；无法证明全域有效时进行可取消、有界、无值读取的验证预遍历，完成后才输出数据，其 CPU 成本计入准备。不把参考容差用于 WHERE。

## GaussianRow / RegionSegment

GaussianRow 按生产者源行顺序存储：latitude DOUBLE、point_count UBIGINT、longitude_origin DOUBLE、longitude_step DOUBLE。每行经度按 numeric_policy 求值再规范化。全域 row_prefix 长度 rows+1、首项 0、逐行累加，末项为总点数；N160/N320 必须来自固定行表，不从 N 猜测 row_count。生产者 f32 纬度逐行生成整个 2N 表，不能强制南北镜像。

RegionSegment：parent_row、parent_begin、count，均为零起原始位置；有序列表的累积长度定义对象局部 point 顺序。接缝行可用两个连续片段表达，如先行尾再行首。检查每段在 parent 行内、非空、受检局部前缀和，以及同一 parent 源点未被重复声明；重叠源段拒绝，不按规范化经度排序。未声明区域时局部映射为全域 identity。

关系：local_point → 段定位 → parent_row/parent_x → parent_point。对象 shape 的 point 长度必须等于局部总点数。名义 BBOX 只作诊断；生产者行取整后点可以略超 BBOX，不以此否定已验证的局部点序。

## SpatialLayout / BoundObject

SpatialLayout 保存全部原始 axis_names、shape、受检 strides、空间轴位置与顺序；Projected 支持两个轴或一个有明确定序的展平轴；Gaussian 只有一个 ragged point 轴。非空间轴可交错、反向语义或任意位置，不通过二维 shape 猜测空间身份。

BoundObject 关联 grid_id、layout_id、全变量有序轴、semantic mappings、坐标/CRS 证据及 RemoteReadSession/LocalFile。验证覆盖所有变量，而非仅输出列；不能用调用者声明覆盖已有冲突 coordinates、time 或已识别 CRS。CRS profile 只解读明确支持的源格式；新网格存在相关但不可核对 CRS 时拒绝地理绑定，纯值读取沿用旧契约。

对象身份采用已有来源规范化/访问上下文与可用版本证据；公开 object_id 为脱敏 opaque 标识，不含凭据、带签名 URI 或 raw cache key。version_strength 是 strong / weak / unverifiable，弱 token 不冒充内容哈希。local stat token 也不称加密内容证明；仅在 manifest 内容 hash 实际已验证时声明 content_verified。新调用重新绑定/授权，身份本身不授予访问权。

## SourcePosition

每条逻辑记录保存或按需构造原始 logical_index、axis_indices，空间 local_point_index、parent_point_index 及对象/grid/layout 证据。logical_index 按源 shape row-major strides 计算，不是输出序号；非空间重复点产生不同记录。Projected parent point 按声明展平顺序；separate 的空间 point 约定为 y*nx+x。Gaussian 用 parent_prefix+parent_x，区域同时保留 local index。

排序、WHERE、并行、batch 和 LIMIT 不改身份；相同经纬度可以有不同 local/parent 点。跨对象比较至少使用 object_id+version evidence+layout_id+logical_index。无法验证对象版本时明确不可据此声称跨查询内容稳定。

## GeographicPredicate / NativeWindow / ScanTask

GeographicPredicate 为复制后的引擎无关必要条件：latitude 区间、longitude 区间或至多两个安全区间并集、开闭边界及 unsupported 原因。只有所有 OR 分支均安全时提取整个 OR；完整 engine expression 留在 DuckDB。语义轴选择保持精确类型。

NativeWindow 描述一条原生空间行/列/point 子线、最多 65536 点及一个确定的非空间位置组合。任务不重叠，descriptor 为 O(rank)；全局惰性生成并受检推进，worker 在窗口内完成坐标/谓词准备。原生步进选择存储中最快空间轴；有额外轴时依真实 stride 生成 source logical positions，不能直接套二维 flatten。最多 4096 片段，发位置前超限则全窗口回退；没有必要空间条件直接输出整窗口，不遍历整条超长线计算单区间。

ScanTask 的 source positions/window → bounded logical segments → official rectangle selections；position map 对应解码及输出。候选为原记录集合，不是二维轴区间乘积；exact count为所有窗口preflight候选逻辑记录总数，cursor exhausted且全部窗口preflight完成才可算，权威快照仅在最终成功完整扫描时发布；取消/失败/LIMIT保持NULL。observed_candidate_records为已完成preflight的候选记录累计，不是已发descriptor的原生点数上界；scanner_rows是成功提交给DuckDB的记录。upper bound、native evaluations和最终WHERE结果数口径独立。纯轴/全域路径可提前通过受检乘积知道exact count，并明确此解析来源。

## DomainEntry / SampleEvidence / VersionCombination

DomainEntry 保存 name、canonical definition、适用布局 profile、upstream commit+各输入 hash、generator version/hash、来源构造规则及支持等级。新 domain 不能重解释旧名称；现有 registry commit 与旧真实参考 commit 分别保留。

SampleEvidence 包含真实/合成、对象内容 hash/版本、大小、axes/chunks、配套定义、official value oracle、独立 coordinate oracle、预定容差、冻结时间、完整运行命令/退出码与门禁状态。source-derived 区域映射不能晋级 real-object-validated。

VersionCombination 包含 engine/httpfs/OM/ci-tools 精确提交、官方 overlay 及自有补丁 hash、extension binary/build_id、capability ABI、平台和执行范围；状态为 target / build-verified / prerelease-validated / released-validated / failed。未知组合 fail closed，不能跨版本复用产物。

## QueryEvidence 生命周期

registered → binding → initialized → scanning → success / early_stopped / failed / cancelled；QueryEnd 单次发布权威终态。每个 scan 用独立 scan_id，多次引用不合并成一份伪完整结果。绑定/坐标预遍历/排队/解码均可取消，失败后释放当前所有权并允许有效新查询。

metrics v4 包含 selector、definition、task/batch、decoder、shared cache 的分别峰值/范围，实际 transport body 包含所有 attempt，count_complete/decode_complete/transport_complete/memory_complete 分别标识。任何未知数值为 NULL，不以零代替；提前停止不能作为完整收益证据。
