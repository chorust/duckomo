# Validation Evidence Contract

此契约约束开发期验证工具的输出，不新增产品 SQL API。所有记录均须由实际执行产生；设计示例不得充当通过证据。

## 工具入口

实现提供以下原生构建目标与包装脚本，输出位置由 quickstart 固定：

- `duckomo_fixture_tool --output test/data`：调用固定上游官方 C writer/encoder 生成文件，由独立官方 reader 路径产生参考 CSV 和 manifest；生成器不依赖 scanner 的命名、展平或投影逻辑。
- `duckomo_validation --fixtures test/data --output build/evidence`：验证 fixture/manifest 与固定依赖身份；将四条投影性能场景分别放在独立 release CLI 子进程执行，消费完整输出并逐项对照独立参考 CSV；记录 exact SQL、实际读取/decode sidecar、耗时和子进程 peak RSS。它不替代 lifecycle/native 测试，也不使用 `COUNT` 包装值查询来测窄投影。
- `scripts/validate.sh build/release`：依次运行 SQLLogicTest、五个 native 检查、manifest 中 fixture/reference/negative asset 校验和 fixture 临时重生成比较，再运行四场景 release harness 并检查所需证据字段；任一缺项或失败返回非零。不会仅凭存在输出文件判定通过。
- `make sanitizer-test`：在单独 staging 目录构建 instrumented extension C++ 实现和 native 测试，执行边界、损坏输入、schema 与生命周期检查；不用于 release 性能采样。当前 ASan/UBSan 覆盖扩展自有 C++ 实现及测试，固定 OM C 依赖沿用常规构建。

fixture 生成命令属于测试资产开发；已提交且校验和通过的 fixture 可直接用于不含 Python/Swift 的查询环境。

## Fixture manifest

每个 fixture 条目至少记录：`fixture_id`、相对 `path`、`sha256`、来源/生成命令、上游固定 commit、格式版本、变量规范路径、类型、压缩、shape、chunk_shape、有序轴声明、预期 schema、缺测位置、参考结果文件及哈希、比较规则或拒绝理由。

必需覆盖：

| fixture | 约束与目的 |
| --- | --- |
| `raw.om` | 根 Float32/FPX，shape `[2,3]`，顺序值 0–5，作为最小演示 |
| `multi.om` | NONE 根下 temperature=0–5、humidity=100–105，均为 `[2,3]`，明确两轴声明，用于 SQL 正确性 |
| `projection.om` | 至少三个独立 payload 变量，非方形 shape、多 chunk、总行数大于两倍当前 DuckDB 标准批次大小；每个变量非空且可以独立读取，用于 I/O 比较 |
| `special.om` | 包含 NaN、±Inf、±0 和代表性有限 Float32 值；官方完整文件 roundtrip 后写参考结果 |
| `nested.om` | 不同层级同叶名及含需编码字符的名称，验证唯一命名与排序 |
| 负向集合 | 版本/类型/压缩不支持、空/零维数组、截断、非法引用、命名冲突、不兼容 shape；同 shape 不同轴和映射缺失也须拒绝 |

无法由官方 writer 生成的非法输入可在已知 fixture 上作可复现截断/变异，并记录变异方法；它们不能充当合法格式参考。

## 每次执行记录

JSON 至少含：

| 字段 | 含义 |
| --- | --- |
| `schema_version` | 证据格式版本，初值 1 |
| `query_id`, `scenario`, `sql` | 查询身份、用例名及精确 SQL/参数 |
| `fixture_id`, `fixture_sha256`, `dependency_commits` | 输入与构建身份 |
| `status`, `result_rows`, `comparison_passed` | 成功/失败/取消、行数、参考结果比较结果 |
| `metadata_bytes`, `metadata_requests` | bind/schema 所需读取，含共享 metadata |
| `variables` | 每个变量的 index_bytes/index_requests、data_bytes/data_requests、decoded_chunks |
| `bytes_fetched`, `read_requests` | 所有实际定位读取成功返回的字节和请求次数，与各分类总和一致 |
| `elapsed_ms` | 完整查询从提交到消费完结果的单调时钟耗时 |
| `peak_rss_bytes` | 独立查询子进程峰值常驻内存，明确包含运行引擎与结果消费开销 |
| `environment`, `cache_policy` | 平台、编译方式、线程数、应用缓存状态与OS缓存条件 |
| `error_category` | 非成功时记录错误类别；不得把此记录用于成功查询性能比较 |

metadata/index/data 分类来自读取所处的官方调用阶段；不为了分摊字节去重建 OM chunk 布局。一次 `om_decoder_decode_chunks` 成功返回，按该次实际 chunk 范围长度增加 `decoded_chunks`；重复解码算多次。若调用失败，该字段不声称涵盖函数内部已完成的部分块，失败记录显式标注 `decode_count_complete=false`。

## 比较与退出条件

1. 固定 `projection.om`、固定依赖、禁用应用层缓存、单线程，分别运行全变量、仅 temperature、temperature 输出并以 humidity 过滤、仅计数。逐次消费完整输出。
2. 数值、NULL 位置和行数对参考结果通过；非数值指标不替代正确性证据。
3. 单变量：未请求变量的 `data_bytes=0` 且 `decoded_chunks=0`；请求变量有实际解码；变量数据总字节严格少于全变量查询。metadata 可以仍有共享开销，须披露总 bytes。
4. 过滤依赖：temperature 和 humidity 均正确读取，无关第三变量无数据读取与解码；结果符合完整扫描基准。
5. 计数：所有变量 data/index bytes 和 decoded chunks 为零，允许读取 metadata；结果等于 shape 乘积。
6. 同一个进程执行至少100次有效/失败查询，释放查询对象后文件句柄不呈随次数增长趋势；取消后执行有效查询成功。失败/取消证据不参与性能比值。
7. sanitizer 用于 native 生命周期、越界及损坏输入检查；性能采样采用正常 release 构建，避免比较不同构建的耗时或内存。

不要求系统级磁盘缓存清空；应使用相同条件比较并准确称为“应用读取字节”。不将 request count、耗时、峰值内存强制设为一定更低，它们是解释性指标。
