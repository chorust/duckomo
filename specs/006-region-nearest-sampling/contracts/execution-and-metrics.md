# Execution, projection and metrics contract

## 1. 方法分流与既有接点

公共注册仍只有GetReadOmRegionsFunction。read_om/raw/grid_info不新增区域采样选项；nearest使用同一BindReadOmInternal、RemoteReadSession、OmV3Reader、ScanReadOm错误处理与QueryEnd。内部Points/Nearest路径语义分开，不复制整个scanner。

1. **参数绑定**：method解析→新增选项合法性→既有完整metadata/shape/axes/grid/domain/变量/区域校验。points保持当前binding及spatial准确签名要求。
2. **nearest绑定**：构造RegularSamplingCapability→记录spatial实际版本→逐区域验证并生成目标→覆盖域与nearest定位→阈值/strict检查→冻结NearestPlan、schema和owned账本。全集定位不得被WHERE/LIMIT取消合法性验证；显式外部取消仍中止。
3. **filter callback**：method-aware。points复用现在ExtractSpatialPredicate；nearest仅复用非空间轴安全必要条件，不向源spatial.predicate写入target lat/lon条件。全部表达式保留为宿主residual。
4. **init/projection**：接收完整column_indexes，分析sampling nested leaf与变量依赖；初始化非空间cursor和有限worker/decoder，只对RequiredVariableIds创建reader状态。nearest target空间映射不受值投影影响。
5. **claim**：短锁领取region_ordinal+非空间轴组合范围。单task单region、最多2048结果（且按内存预算进一步缩批），不预建全部region×axis任务。
6. **mapping/read**：只对plan几何Ok结果映射真实logical_index；按递增unique源位置调用既有BuildSelectedBatchSegments和DecodeSelection，必要时scatter。NULL结果不参与值读取。
7. **output**：target lat/lon、原语义轴、region_id、所需值与sampling leaf/STRUCT、可选真实om_source。整个chunk所有所需数组成功后才提交cardinality；全部无源结果也可产出非空chunk。
8. **completion**：沿用失败、取消、LIMIT、outstanding和QueryEnd清理；完整结果要求cursor耗尽且全部task已结算。

### 代表位置adapter

RegionGeometryAdapter新增nearest-only调用，不让grid层依赖DuckDB/spatial。使用系统catalog准确签名和ExpressionExecutor；生成点后提取X/Y并ST_Covers检查。不能依赖私有serialization、直接GEOS ABI或静默加载spatial。所有nearest-only临时geometry逐区域释放，计划仅存double坐标和标识；points的bound covers表达式保留现状，nearest不需要为源点构建covers predicate。

spatial.extension_version通过宿主公开扩展目录获取，并由validator验证hash匹配；不可伪造GEOS版本。没有可靠版本时按依赖错误拒绝nearest，不扩大points门槛。

## 2. Nested projection与值依赖

新增sampling叶输出需要duckdb::ColumnIndex，不允许继续仅用input.column_ids判断需求。为nearest的sampling声明supports_pushdown_extract，读取GetChildIndexes/IsPushdownExtract/叶类型，严格返回宿主所期待的叶vector或整个STRUCT。旧Value/lat/lon/axis/Source/RegionId/Cardinality槽保持。

| 请求 | RequiredVariableIds | 备注 |
| --- | --- | --- |
| COUNT(*)，region_id、target lat/lon、语义轴、om_source | 空 | metadata/geometry仍有成本；om_source无需值 |
| sampling.method/status/source_lon/source_lat/distance_m/axis_indices等几何字段 | 空 | PUSHDOWN_EXTRACT路径时直接输出真实叶 |
| 某个原值列 | 该值变量 | 过滤列即使不投影也由宿主列依赖保留 |
| sampling.variables 或其MAP key状态 | 全部当前绑定变量 | 首版不做MAP key表达式重写 |
| sampling整个STRUCT | 全部当前绑定变量 | 全部逐变量状态均真实 |
| unknown/full-read回退 | 保守全状态依赖 | 不用假NULL填未读取状态 |

同一变量直接值列+状态重复请求只解码一次。variable参数限制绑定数组后，状态MAP仅包含这个数组。无源任务可证明所有状态无需读值，仍不得用该事实忽略其它task的依赖。

只访问几何叶的优化路径必须在三版本SQL测试中证实，包括简单投影与residual WHERE。若某个宿主查询形态不能保留nested path，则以真实完整STRUCT读取回退并在成本中说明；不得将该形态算作已验证零值路径，不能将限制偷偷从N3验收中删除。标准几何leaf查询无法实现零值I/O时是006实现阻塞，不能另添公共函数绕过。

## 3. 资源预算

| 对象 | 上限与所有权 | 超限 |
| --- | --- | --- |
| RegionCollection / WKB / geometry | 沿005：4096区域、1MiB单WKB、16MiB总WKB、64MiB自有输入、2MiB单opaque几何 | 绑定拒绝 |
| NearestPlan | 8MiB自有capacity，含target数组/options/能力/字符串和必要副本 | 绑定拒绝，先计量再保留 |
| grid资格/定位scratch | O(rank)+一个candidate，新增≤64KiB | 受检失败，不物化全域表 |
| 共享axis/selection必要条件 | 沿现有1MiB总体及子预算 | 安全扩大非空间必要候选，不改residual |
| 全局nearest调度 | ≤8MiB自有capacity；只current cursor+O(workers) descriptors | 降task并发/缩批；不创建结果总表 |
| task结果批次 | ≤2048行，单region；实际由新scratch预算缩小 | 缩批，最小批不满足则明确错误 |
| worker新增结果/索引/scatter/value scratch | 每worker≤512KiB，按所需变量数和rank动态受检 | 缩批；一次一行仍过限则allocation错误 |
| reader/decoder buffers | 沿既有OmV3Reader容量账本和块限制，逐required变量计量 | 不隐藏大chunk成本，不计入虚假512KiB总量 |
| 第三方/宿主输出 | GEOS、DuckDB vectors、HTTPFS等单列未知/外部观测 | 不声明受上述自有预算涵盖 |

结果基数R×nonspatial product按signed64受检；上界不等于已完成计数。nearest不分配与nx×ny成正比的坐标或任务buffer。源索引vector sort/unique/scatter有实际capacity计量；保留一次批次即可，不跨区域保留所有历史source。

定位循环、axis cursor、结果构造和批次间每≤256项检查取消；I/O沿原reader检查。GEOS单次调用不可中途抢占，不承诺固定毫秒取消上界，测量依赖延迟。

## 4. 线程与来源读取

使用DuckDB线程及duckomo_max_threads，不另起线程池。NearestPlan全局只读，任务indices和decoder为worker独占。严格相同输入在1/2/4worker的多重集合相同，无ORDER BY不承诺输出顺序。

首版单task单region可重复读取多个区域共享块；批内相同源位置只解码一次。跨区域物理块共享不作为本期承诺，不新增缓存。官方OM reader继续规划物理块/LUT，位置计划不能自己解析文件字节偏移。HTTPFS官方签名/读取与对象稳定政策不变。

## 5. 指标additive规则

duckomo_last_scan_metrics表结构与v4/legacy字段保留，operation始终read_om_regions。points的regions区段、候选/匹配与region_record_pair单位原义不变。

nearest新增sampling对象：

- method、algorithm、representative、distance_model、grid_id/layout_id、spatial_version、earth_radius_m。
- count_unit='region_axis_result'；region_target_count、empty/outside/beyond_distance_target_count按区域数；bound_result_upper为受检区域×非空间轴上界。
- coordinate_evaluations、distance_evaluations、qualification_evaluations、target_prepare_ns、locator_ns：实际观察；不拿源点数推测读取量。
- result_rows_observed/result_rows_exact、result_count_complete；exact仅所有task完整结束为整数，否则NULL/false。它们是必要轴条件后、完整SQL residual前的结果。
- source_rows_requested、distinct_sources_in_batches、per_variable_status_observed；后者仅实际有值状态依赖时计数，没读取的值不得报0个NULL或全有效。状态计数可带observed/dependency_complete明确范围。
- required_state_variables及projection_fallback原因；只记录规范变量路径，不记录WKB或敏感URI。
- plan_owned_peak_bytes、work_owned_peak_bytes/bound、query-owned完整性；绑定、输入、selector/task/worker、reader/decoder各项进入既有ledger。

nearest不执行region原始点covers扫描；其旧regions区段增加applicable=false、not_applicable_reason='nearest_uses_target_sampling'，旧regions运行候选/geometry-match及matched_pairs_exact字段为NULL而非0；输入region/WKB计数仍实际填写。points原区段无此行为变化。不重定义目标数塞给旧matched_pairs_exact。该分支新增字段在metrics序列化/审计测试中固定，旧消费者忽略新sampling字段仍能读原v4。

远程transport仍unknown/null且complete=false；应用字节、decoder块与独立服务端发送量分开。LIMIT可SQL成功但scan_complete=false；失败/取消有观察成本、exact为空。Bind阶段失败若尚未创建scan则不承诺last_scan行。

## 6. 集成文件边界

- src/scan/read_om.cpp：method/options绑定、nearest plan集成、method-aware filter/global/local/scan分支、schema和源值输出。
- src/scan/region_geometry.cpp/.hpp：公开函数内点adapter与版本记录；旧points函数签名不变。
- src/grid/nearest_sampling.cpp/.hpp：资格/覆盖/距离及streaming定位，纯grid逻辑。
- src/scan/region_sampling.cpp/.hpp：options/target计划、非空间cursor、source mapping及projection helper；不注册SQL factory。
- src/scan/projection.cpp/.hpp：sampling子字段输出和依赖；现有调用以缺省参数保持旧行为。
- src/grid/spatial_layout.cpp/.hpp：受检反向logical映射，原映射不变。
- src/include/duckomo/metrics.hpp：sampling additive字段、计数单位和memory accounts。
- src/compat/duckdb_api.cpp/.hpp：确有三版本差异才封装ColumnIndex/struct output，不模拟不支持的2.0。
- CMakeLists/test/CMakeLists及validate.sh：新source/native/SQL/fixture/验证注册；运行矩阵探针扩展，旧历史manifest不改写。

不改OM格式、不新增public SQL函数、不做bilinear和area实现。共享read_om/projection/metrics和build文件由后续任务按依赖顺序集成，避免并发writer重叠。
