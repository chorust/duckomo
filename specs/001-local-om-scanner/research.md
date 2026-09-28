# Research: Phase 0–2 本地 OM 可用扫描器

日期：2026-09-28。研究对象为固定上游源码及当前仓库文档；源码核验不等于构建或运行测试通过。实施 Phase 0 必须补上实际读取、类型与资源生命周期验证。

## 已确认的维度语义决策

**Decision**: 单数组默认按文件维度顺序解释逻辑索引；多个数组只有在调用者逐变量显式声明相同有序轴标识、且文件 shape 相同时才对齐。新增 `dimensions` 命名参数，类型为变量路径到轴名称列表的映射。首批不读取上游尚未完整支持的 String-Array 轴名称约定。

**Rationale**: OM `OmVariableArrayV3_t` 保存维度/分块长度，没有通用 axis identity。C getter 能提供 dimensions/chunks，不能证明两个 shape 相同的数组含义相同。README 的 `dimension_names` String-Array 示例不等于 C reader 对该类型具备完整支持。

**Alternatives considered**: 仅比较 shape 会违反 FR-005；自动猜测 time/grid 会违反 FR-007；侧车文件和 domain registry 增加本阶段不需要的文件管理与网格语义。显式参数只声明索引对齐，不承诺任何物理坐标。

**Evidence**: [OM variable layout](https://github.com/open-meteo/om-file-format/blob/d8855e418e2231ae8439f0c7e840fa3f93b371e3/c/include/om_variable.h)、[OM common implementation](https://github.com/open-meteo/om-file-format/blob/d8855e418e2231ae8439f0c7e840fa3f93b371e3/c/src/om_common.c)。固定提交 `d8855e418e2231ae8439f0c7e840fa3f93b371e3`，提交日 2026-09-09。

## 扫描与投影的职责

**Decision**: bind 校验全部可查询数组的模式/对齐，scan 初始化再接收实际依赖列。启用列裁剪，关闭过滤下推与提前删除过滤列。每个逻辑批次对所需变量请求相同范围，只通过官方 reader 提交逻辑切片；逐最后一轴连续段推进，跨行/高维轴时重新构造 offset/count。

**Rationale**: bind 的 schema 不应随 SELECT 列表变化；正常 SQL 执行器负责过滤、聚合、排序和表达式，避免本阶段意外引入时空谓词规划。限制逻辑批次可避免把整变量物化到内存，但底层压缩块仍由 OM 决定。

**Alternatives considered**: 扫完所有变量再选列不能证明物理读取减少；提前移除过滤所需变量会改变结果；按字节手工规划会越过既有职责边界；先引入并行增加可观测性和生命周期复杂度。

## 证据与性能门槛

**Decision**: 原生集成测试 harness 按单个查询进程输出 JSON 证据；记录真实请求字节、请求次数、官方解码次数、墙钟耗时和进程峰值内存。成功的 `om_decoder_decode_chunks` 调用实际处理其 chunk 范围，按返回成功的范围长度累加解码块次数，重复解码重复计数；失败查询不进入性能对比，也不声称该计数包含失败调用内部已经完成的部分块。无需修改上游解码算法。

**Rationale**: `EXPLAIN` 与列列表只能证明规划，不能证明读取减少。数据请求和元数据开销必须分开，读取量指应用通过本地文件系统取得的字节，不声称是磁盘设备实际 I/O。指标记录不暴露新的产品 SQL 函数，不使用会串查询的全局计数器。

**Alternatives considered**: 只记录总时间易受缓存影响；按请求的变量数量推算 decoded chunks 不可信；引入产品级 profiling UI 或指标持久库超出范围。

## 验证策略与错误边界

**Decision**: 以官方 writer + 独立官方读取路径生成参考结果，测试已知值公式、非方形数组、缺测位置、跨批次和层级多变量。正向初始子集外的格式/类型在 bind 拒绝；扫描期间发现的数据损坏则让整个查询失败，释放资源。结果消费方已看到的流式批次无法撤回，因此不承诺先验证全文件再返回第一行。

**Rationale**: 扫描全部 payload 预校验会破坏列裁剪；错误必须是失败状态，不能返回“成功的部分结果”。100 次混合有效/失败查询与取消后成功查询验证生命周期，关键边界可在 sanitizer 构建复查。

**Alternatives considered**: 仅与 scanner 自己的全读路径比较会共享错误；预先完整解码既损耗性能又违背目标。Python 官方 bindings 可作额外交叉验证，但不是查询运行时或必需验收环境。

## 固定构建基线

**Decision**: 采用 C++17 扩展、以 GNU C11 模式构建上游 C 核心；Linux x86_64 首发。DuckDB 固定 `v1.5.4` / `08e34c447bae34eaee3723cac61f2878b6bdf787`，扩展模板固定 `cfaf3e236008e782d27f4341b0ee036002d0a449`，OM 固定 `d8855e418e2231ae8439f0c7e840fa3f93b371e3`。构建依赖采用 git submodule 锁定，模板用于迁移骨架而非运行时依赖；extension-ci-tools 固定模板 gitlink `b777c70d30942cca5bef62d6d4fa23a13362f398`，不跟随主分支。

**Rationale**: 模板已采用 C++17，并与 v1.5.4 同代。先前核对的 v1.2.2 接口仅作历史对照，不选择它作为新项目构建基线。OM 本身以 C 源码参与构建，避免引入 Swift/Python 查询运行时。C 编译模式为项目设计选择，实际构建兼容性是实施 Phase 0 的硬门槛。

**Alternatives considered**: 浮动 main 不可复现；无理由固定旧版会增加模板适配工作；保留模板示例的 OpenSSL 依赖与本地 OM 扫描无关，移除；不启用本阶段不需要的 vcpkg 依赖。

**Evidence**: [DuckDB v1.5.4](https://github.com/duckdb/duckdb/tree/08e34c447bae34eaee3723cac61f2878b6bdf787)、[模板 CMake](https://github.com/duckdb/extension-template/blob/cfaf3e236008e782d27f4341b0ee036002d0a449/CMakeLists.txt)、[OM C API](https://github.com/open-meteo/om-file-format/tree/d8855e418e2231ae8439f0c7e840fa3f93b371e3/c)。DuckDB tag 的 SHA 已用远端 refs 核实。

## 首批支持子集

**Decision**: 正向验收固定 OM v3、float32 数组和 FPX_XOR2D 无损压缩；支持根数组以及仅包含容器和受支持数组的层级。数组 rank 为 1–8，轴长度/分块长度为正，shape 连乘不超过有符号 64 位计数范围。零长度轴在此子集中明确拒绝。float32 映射 SQL FLOAT；NaN 映射 NULL，有限值与官方解码结果精确比较（容差 0），正负无穷保留。其他类型、压缩与旧版布局先拒绝，不因上游可能支持而自动承诺。

**Rationale**: 上游 `om_common.h` 明确提供 FLOAT_ARRAY 和 FPX_XOR2D（无损 float/double）；限定一个可复现子集可独立证明数据路径和投影收益。NaN 作为 NULL 便于 SQL 缺测处理，不能将合法零值当缺测。rank 上限是项目限制，不声称是格式限制。

**Alternatives considered**: 立即支持所有整数/浮点/标量/字符串及旧格式会扩大类型和布局测试；有损编码需要单独约定缩放和容差；在没有样本证据时声称通用支持会违背规格。

**Evidence**: [OM data type / compression enums](https://github.com/open-meteo/om-file-format/blob/d8855e418e2231ae8439f0c7e840fa3f93b371e3/c/include/om_common.h)。实际 fixture 生成、官方 roundtrip 与非有限值测试在实施 Phase 0 执行；若 FPX 实测不能满足该契约，则修改契约及基线后再进入正式 scanner 阶段，不能静默改值。

## 研究收敛与实施验证边界

技术选择均已给出，不保留需要用户决策的澄清项。未执行的工作是明确的实现验收：固定版本构建、官方 writer/reader roundtrip、实际 SQL 推送行为、MAP 参数绑定、解码观测和取消资源回收。这些不能在规划阶段报告为通过。

## OM 读取生命周期与构建补充证据

**Decision**: 变量 metadata 原始缓冲区、decoder 引用的 shape/chunk 及 offset/count 数组必须活到解码完成；用 C++ RAII 包裹所有权，不能把 decoder 指针指向短期栈变量。OM 读取由 index-read/data-read 迭代驱动，宿主执行官方返回的字节请求。上游 C 文件集通过项目 CMake 编译为 PIC 静态库并同时链接静态/动态扩展；不复制上游 `-march=native` 到发布构建，首发目标的指令集要求在 Phase 0 验证并记录。

**Rationale**: `om_variable_init` 借用输入内存，decoder 继续借用维度和选择参数；库没有替调用方管理这些缓冲区。上游没有独立 CMake，官方 Swift Package 与 Windows CI 都直接编译 `c/src/*.c`。源码支持 Float32/FPX，但上游 NaN FPX 用例被注释，因此本计划特别把完整文件 NaN roundtrip 设为 Phase 0 阻断性验证，不能借源码推断已通过。

**Alternatives considered**: 全文件 mmap 让读取字节指标混入页缓存/缺页语义；直接本地定位读取更易统计；返回临时 metadata 指针会造成生命周期错误；按 native CPU 编译会使产物无明确可移植范围。

**Evidence**: [decoder 声明](https://github.com/open-meteo/om-file-format/blob/d8855e418e2231ae8439f0c7e840fa3f93b371e3/c/include/om_decoder.h)、[decoder 实现](https://github.com/open-meteo/om-file-format/blob/d8855e418e2231ae8439f0c7e840fa3f93b371e3/c/src/om_decoder.c)、[官方测试](https://github.com/open-meteo/om-file-format/blob/d8855e418e2231ae8439f0c7e840fa3f93b371e3/Tests/OmFileFormatTests/OmFileFormatTests.swift)、[Package.swift](https://github.com/open-meteo/om-file-format/blob/d8855e418e2231ae8439f0c7e840fa3f93b371e3/Package.swift)。

## DuckDB v1.5.4 接口复核

**Decision**: 显式设置 `projection_pushdown=true`、`filter_pushdown=false`、`filter_prune=false`，通过 `TableFunctionInitInput.column_ids` 得到实际依赖列。注册 `get_virtual_columns`，为 `COLUMN_IDENTIFIER_EMPTY` 返回 `TableColumn("", BOOLEAN)`；无值列查询按批次仅设置 cardinality，不触碰值变量。不要把 EMPTY 当物理列索引，亦不依赖旧版本 ROW_ID 的回退行为。

**Rationale**: v1.5.4 的 `LogicalGet::GetAnyColumn()` 优先选择 EMPTY；若未暴露它，会回退到 ROW_ID 或物理首列，使 count 误读变量。该版本的 named parameter binder 支持将输入 Value 转为声明的 `MAP(VARCHAR, LIST(VARCHAR))`，因此逐变量 dimensions 参数无需新增 JSON parser。

**Alternatives considered**: 老版 v1.2.2 的 ROW_ID 行为只作对照；省略 virtual column 注册不能保证 metadata-only count；打开 filter pushdown 会带入本阶段未实现的过滤责任。

**Evidence**: [table function](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/include/duckdb/function/table_function.hpp)、[EMPTY 选择逻辑](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/planner/operator/logical_get.cpp)、[官方 virtual-column 示例](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/extension/json/json_functions/json_table_in_out.cpp)、[named parameter cast](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/planner/binder/tableref/bind_named_parameters.cpp)。上述均已在固定版本源码核对；精确 MAP 表达式和 SQL 行为仍须实际集成测试。

**Local I/O decision**: 使用 DuckDB 本地文件系统实例 `FileSystem::GetLocal(*context.db)` 做定位读取，直接 OpenFile，不调用 glob/list 发现文件；适配器执行 OM Sans-I/O 产生的 offset/size。路径先校验为单个本地文件，并遵守引擎对外部文件访问的配置。后续远程接入时再设计 VFS 行为，不绕过本阶段 local-only 限制。

**Evidence**: [FileSystem API](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/include/duckdb/common/file_system.hpp)、[local filesystem accessor](https://github.com/duckdb/duckdb/blob/08e34c447bae34eaee3723cac61f2878b6bdf787/src/main/database.cpp)。
