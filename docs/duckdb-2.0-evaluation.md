# DuckDB 2.0 兼容性与预发布状态

## 状态

DuckDB 2.0.0 **已验证兼容，通过 v0.3.5-rc.1 预发布提供**。四版本 SQL、2.0 统一验证入口、官方 HTTPFS 48/48、ASan/UBSan 14/14 与系统分配器 TSan 已通过；四平台产物均通过匹配官方运行时校验并发布。参见 [发布流水线](https://github.com/chorust/duckomo/actions/runs/40178444705)、[Release](https://github.com/chorust/duckomo/releases/tag/v0.3.5-rc.1) 与[验证记录](../evidence/008/final/status.md)。这尚非稳定版发布；异步功能的性能目标与引擎兼容状态分别记录。

远程合并在固定 O1280 的受控 HTTP/S3 测量中将请求从 11,591 降到 840。useful_bytes 排除预读、未选 chunk 和缓存复用，不再宣称等于 physical_bytes。

**异步没有稳定加速，也未达到额外快 20% 的目标。** 实验跨批窗口 `duckomo_async_prefetch_batches` 默认1关闭；显式4批的 HTTP O1280 单 worker /256MB 中位数1351→1910ms，请求840→1523，结果相同。详见 [真实实验结果](../evidence/008/prefetch/report.md)。本地热查询大体接近基线，部分首次查询超过5%门槛，保留未达标结论。旧46.1%提升声明已撤销。Spec 008 性能目标尚未全部完成，不影响已验证的引擎兼容状态。

官方 [v2.0.0 release](https://github.com/duckdb/duckdb/releases/tag/v2.0.0) 与 tag 指向 `d54cbbe3df17e8e5ce68312f483b4155836112aa`。实验输入、官方 CLI/HTTPFS 解压后 SHA256 见 [独立矩阵](../test/data/engine-compatibility/version-matrix.json)。不替换现有稳定矩阵或 DuckDB 子模块。

## 已修复的接入差异

- 官方 CLI 发布格式由 ZIP 改为 tar.gz；下载器兼容两者，仅读取唯一 duckdb 普通文件，不展开归档路径，继续校验解压后哈希和版本。
- 2.0 ScalarFunction/BoundFunctionExpression 封装成员、签名与 catalog 查找 API；保留 1.x 分支。
- RegionPredicateEvaluator 析构移至完整 ExpressionExecutor 类型可见的实现文件。
- 2.0 Identifier 的比较不区分大小写；regions 的严格 `id`/`wkb` 校验改为比较原始字符串，保持原有契约。
- 2.0 输出需要同步子向量长度。普通 read_om 批次完成时用 SetChildCardinality；只设 chunk cardinality 在复用场景产生错误坐标 NULL。原有 multi_grid 回归可复现，修复后通过。重置 validity 的尝试无效，未保留。
- SQL 测试按名称/类型配对比较命名选项，避免依赖 catalog 枚举顺序；lambda 改为两版本都接受的 `lambda i:`。
- native `official_httpfs_test`：2.0 的 QueryResult 不再直接提供 `GetValue`，改为 `Fetch()->GetValue`；2.0 静态库已定义 `ExtensionHelper::LoadAllExtensions`，测试改为显式 `LoadStaticExtension<DuckomoExtension>()`；2.0 把动态扩展加载拆成单独链接的 capability，测试目标需 `link_extension_libraries`，否则无法 `LOAD` 官方 HTTPFS。

## 验证

匹配源码构建的 SQL runner 加载各自 DuckOMO，空间测试加载对应官方 spatial：

| 引擎 | SQL 回归 | 官方 CLI 本地验证 | 受控远程验收 | native official_httpfs_test |
|---|---|---|---|---|
| 1.5.4 | 21 cases / 1260 assertions 通过 | 9 项通过 | 本轮未跑 | 本轮未跑 |
| 1.5.6 | 21 cases / 1260 assertions 通过 | — | 48 项通过 | 通过 |
| 2.0.0 | 21 cases / 1260 assertions 通过（另经独立 worker 复验） | 9 项通过（独立复验） | 48 项通过 | 通过 |

受控远程验收为 `validate-official-httpfs.py` 非 `--local-only` 模式：本地/HTTP/HTTPS/S3 同内容结果、1/2/4 worker、局部读取的服务端字节收益、重复查询、LIMIT、403/404/短读/忽略 Range/版本变化的失败与同连接恢复、并发连接。服务均为 127.0.0.1 上的 fixture HTTP(S) 与 S3 审计代理（后端 SeaweedFS）。2.0 有两次前置失败已保留在 `build/duckdb-2-evaluation/`：第一次指向已退出的旧 HTTP 服务；第二次因旧 run.env 的 `DUCKOMO_REAL_FILE` 指向服务未提供的 `real.om`，前 48 项已通过。均为环境配置问题，非 DuckOMO 缺陷。native 测试覆盖官方 HTTPFS 错误分类与脱敏、取消后 QueryEnd 状态与同连接恢复、同进程两连接的指标隔离与并发结果。原始摘要见 [验证记录](../evidence/duckdb-2.0/validation.json) 与 `evidence/duckdb-2.0/remote-*.json`。

## 性能结果与限制

[可复跑脚本](../scripts/benchmark-engine-versions.py) 使用同一 `dimensions_perf.om`，固定线程/worker 为 1 或 4，两个查询、fresh-process/repeat 两阶段，每种组合 7 轮，交替版本顺序；所有完整 CSV 结果 SHA256 一致。计时包括 COPY/CSV 输出，不含进程启动及加载扩展。OS page cache 未清理，fresh-process **不是冷磁盘或冷 S3**。文件仅约 31 KB，查询约 1–27 ms。

最终运行中位数如下，完整观测、产物哈希、fixture 哈希与 SQL 见 [原始 JSON](../evidence/duckdb-2.0/local-benchmark.json)：

| 查询 | workers | 阶段 | 1.5.4 ms | 2.0.0 ms | 2.0/1.5.4 |
|---|---:|---|---:|---:|---:|
| 局部选择 | 1 | fresh-process | 1.12 | 1.70 | 1.52 |
| 局部选择 | 1 | repeat | 0.61 | 1.14 | 1.88 |
| 局部选择 | 4 | fresh-process | 1.78 | 1.78 | 1.00 |
| 局部选择 | 4 | repeat | 1.03 | 1.33 | 1.29 |
| 全扫 | 1 | fresh-process | 27.44 | 16.69 | 0.61 |
| 全扫 | 1 | repeat | 16.18 | 15.60 | 0.96 |
| 全扫 | 4 | fresh-process | 17.20 | 16.02 | 0.93 |
| 全扫 | 4 | repeat | 16.51 | 14.52 | 0.88 |

早一轮烟测曾全部略快，最终重跑却有小查询变慢，说明这个量级的噪声/固定开销不可忽略。**不能据此给出统一加速比或生产升级收益结论。**

复现：

```bash
scripts/build-version.sh --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0 --extension-only
python3 scripts/version_matrix.py fetch-runtime --root . --matrix test/data/engine-compatibility/version-matrix.json --pair v2.0.0
python3 scripts/benchmark-engine-versions.py --runtimes /path/to/runtimes.json --output build/engine-comparison
```

runtimes.json 是至少两个 `{ "name": "v2.0.0", "duckdb": "...", "httpfs": "...", "extension": "..." }` 对象的数组，每个 triple 必须对应同版本。历史实验 build 目录的 ID 是修复前输入计算的；由于就地增量编译，不能将其视为最终源码 build_id。基准记录的二进制 SHA256 才是本轮运行身份；正式矩阵重建会重新计算源码身份。

## 真实 OM 对象：1.5.6 对 2.0

对象为固定的 `ecmwf_ifs_static_hsurf_reduced_gaussian_o1280`（O1280 地形，4,087,256 bytes，6,599,680 个值，SHA256 `ac6564551b14…`），同一对象分别以本地文件、loopback HTTP、loopback S3（审计代理 → SeaweedFS）读取。查询 `count(*), count(value), min(value), max(value), sum(value::DOUBLE::DECIMAL(20,6))` 消费全部值；2 版本 × 3 来源 × workers 1/4 × fresh-process/repeat × 3 轮 = 72 条观测，结果 SHA256 全部一致。下表为 3 轮中位数，完整数据见 [原始 JSON](../evidence/duckdb-2.0/real-benchmark.json)：

| 来源 | workers | 阶段 | 1.5.6 s | 2.0.0 s | 2.0/1.5.6 | 峰值 RSS MiB（1.5.6 / 2.0） | 服务端请求 / 发送字节（两版相同） |
|---|---:|---|---:|---:|---:|---|---|
| local | 1 | fresh-process | 0.169 | 0.135 | 0.80 | 38 / 60 | — |
| local | 1 | repeat | 0.169 | 0.134 | 0.79 | 39 / 61 | — |
| local | 4 | fresh-process | 0.076 | 0.073 | 0.97 | 39 / 60 | — |
| local | 4 | repeat | 0.070 | 0.070 | 1.01 | 41 / 61 | — |
| http | 1 | fresh-process | 6.360 | 6.852 | 1.08 | 40 / 68 | 11,592 / 4,125,977 |
| http | 1 | repeat | 6.458 | 6.799 | 1.05 | 41 / 69 | 11,592 / 4,125,977 |
| http | 4 | fresh-process | 5.327 | 5.321 | 1.00 | 42 / 71 | 11,701 / 4,134,570 |
| http | 4 | repeat | 5.274 | 5.349 | 1.01 | 44 / 73 | 11,701 / 4,134,570 |
| s3 | 1 | fresh-process | 15.821 | 16.135 | 1.02 | 40 / 69 | 11,592 / 4,125,977 |
| s3 | 1 | repeat | 15.752 | 16.119 | 1.02 | 40 / 69 | 11,592 / 4,125,977 |
| s3 | 4 | fresh-process | 6.980 | 7.016 | 1.01 | 42 / 71 | 11,701 / 4,134,570 |
| s3 | 4 | repeat | 6.974 | 7.044 | 1.01 | 44 / 73 | 11,701 / 4,134,570 |

结论：

- **远程路径升级无收益**：HTTP/S3 下 2.0 与 1.5.6 持平或慢 0–8%，请求数与发送字节逐字节相同。这符合源码判断：同步 `FileSystem::Read` 不会因升级而改变请求模式。
- **本地单 worker 解码约快 20%**，4 worker 持平。原因未拆解（引擎执行路径或编译差异均可能），仅限该对象。
- **2.0 进程峰值 RSS 高约 20–29 MiB**（CLI + 扩展基线），对多实例部署需留意，未区分查询自有内存。
- **真正的瓶颈是请求放大**：4 MB 对象产生约 1.16 万次 range 请求，平均约 356 字节/请求。即使在 loopback 上，单 worker HTTP 耗时为本地的约 38 倍、S3 约 93 倍（约 1.4 ms/请求）；真实云端 RTT 会把差距进一步放大。
- **2.0 修正了一个 FLOAT→DECIMAL 精度问题**：最初用 `sum(value::DECIMAL(20,6))` 时 1.5.6 得 `-3285892932.974848`，2.0 得 `-3285892933.000000`。该对象全部值均为整数，`sum(value::BIGINT)` 两版一致；差异来自 1.5.6 的 float32 乘 1e6 溢出尾数（如 `1139.0::FLOAT::DECIMAL(20,6)` = `1139.000064`），2.0（PR #24814）已修复。`read_om` 解码一致。下游若对 FLOAT 列做 DECIMAL 转换，升级后结果会变（变正确）。

限制：loopback 网络，无云 RTT/带宽结论；OS 与服务端缓存未清理，fresh-process 不是冷读；聚合校验不是逐行等价证明；只覆盖全扫，单变量局部、polygon、nearest 尚未做真实对象对比。基准脚本是 `build/duckdb-2-evaluation/real-benchmark.py`，含本机服务端口等硬编码，属一次性评测工具，未纳入仓库脚本。

## 新特性的实际价值

### Async I/O：最值得单独立项，升级本身不启用

真实对象数据表明，远程耗时由小 range 请求的数量主导，而非带宽。因此优先级应是：先评估 OM reader 的相邻 range 合并（减少请求数，不依赖 2.0），再评估 2.0 async 让请求并行在途（隐藏延迟）。两者叠加才可能接近本地耗时；任一项都需要在真实云端点验证，并控制读放大与内存。

当前 `src/om/remote_file.cpp` 通过同步 FileSystem::Read 读取精确 range，并使用 DIRECT_IO/NO_CACHING。read_om 不是 Parquet/MultiFile 扫描器。2.0 的 Parquet read-ahead 不会自动覆盖 `.om`。

上游 v2.0.0 的 `src/include/duckdb/parallel/async_result.hpp`、`common/multi_file/base_file_reader.hpp` 与 `multi_file_function.hpp` 提供 AsyncTask/AsyncResult、ScheduleIO 与 read-ahead 协议。接入 OM 需要把任务拆为计划 range、持有 buffer、异步读取、恢复解码，并处理取消、异常脱敏、每 worker decoder/句柄寿命和内存预算。先验证 OM reader 能否安全枚举待读范围，再设计有上界的预取。不要仅增加 async_threads 并声称已有异步扫描。

### VARIANT：附属元数据候选，暂不改变数值主列

OM 输出本来就是稳定类型的科学变量、坐标及 STRUCT/MAP sampling 信息。VARIANT shredding 不会优化 OM chunk 解码。只有未来稀疏/异形 QC、来源和产品元数据确有动态 schema 时，才值得在侧表比较 STRUCT、JSON、VARIANT；测 checkpoint/导出成本、大小、热字段查询、结构漂移和消费者往返兼容。没有当前产品需求支持把主数值列转为 VARIANT。

### Parquet 与 spatial

OM → Parquet 可以作为独立实验，分开测 OM 输入、编码/写出、远程 Parquet 重读；只有后两段可能直接受新 Parquet 路径影响。不要把 Parquet 博客的 3x/20x 当成本项目收益。

官方 2.0 spatial 已用于本轮现有区域/nearest SQL 回归；这证明该覆盖范围的功能可用，未证明几何运算加速，也未覆盖 native sanitizer 和空间扩展的全部行为。

## 发布与功能验收分别记录

2.0 兼容性及四平台产物已验证，不再标记为 evaluation。稳定版发布仍待后续正式 tag；社区扩展登记仍为1.5.6，未随本预发布更改。

Spec 008 尚未全部完成：异步未稳定达到20%加速，部分本地首次查询超出5%门槛，完整跨批重叠流水线尚未完成。这些是实验功能与性能限制，不撤销已验证的2.0引擎兼容性。详见[实测报告](../evidence/008/prefetch/report.md)。
