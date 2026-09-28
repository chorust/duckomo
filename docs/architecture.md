# duckomo 技术架构

状态：v1 设计基线（2026-09-28）。目标实现为 **C++ DuckDB extension + 官方 Open-Meteo OM C library**。选择 C++ 的直接原因是 DuckDB C++ table function 提供投影与过滤下推入口；构建时须锁定兼容的 DuckDB 版本。

## 1. 职责边界

| 层 | 回答的问题 | 职责 |
|---|---|---|
| DuckDB table function | 查什么？ | Bind、SQL schema、projection/filter pushdown、执行调度、DataChunk 输出 |
| ScanPlan | 本次查询选什么？ | 所需变量、维度条件、逻辑选择、输出布局、任务划分 |
| GridMapping / DimensionMapping | 在数组哪里？ | 经纬度、时间、level、member、lead time 等语义坐标与 OM 逻辑索引互转 |
| 官方 OM C reader | 如何读取？ | hierarchy/metadata、逻辑切片到 chunk/LUT/byte range、解压 |
| I/O adapter | 从哪里读字节？ | 将 OM Sans-I/O 请求接到 DuckDB 文件系统；后续本地/HTTP/S3 共用扫描逻辑 |

```text
DuckDB SQL
  → TableFunction Bind/GlobalInit/LocalInit/Scan
  → ScanPlan ← GridMapping + DimensionMapping
  → OM logical offset/count 或等价 slice
  → official OM C reader
  → DuckDB filesystem adapter
  → decoded block → DuckDB Vector/DataChunk
```

特别明确：`GridMapping::select(bbox)` 返回的是**逻辑网格点/索引区间**。它不做 `get_range_list` 式 chunk ID、OM 文件 byte range 或解压规划；这些由官方 OM reader 负责。旧对话中把 `get_range_list()` 称作扫描核心的建议，已被后续讨论修正。本设计以该修正为准。

## 2. DuckDB 集成

`Bind` 读取足以确定 schema 的元数据，并固定变量与维度解释。`GlobalInit` 接收 `column_ids`/`projection_ids`/`filters`，构造 `ScanPlan`；必要时 `LocalInit` 为并行 worker 准备局部状态。Scan 将已解码块写入 DuckDB Vector，尽量避免 NumPy/xtensor/额外 vector 中转。

`projection_pushdown` 用于减少变量读取；`filter_pushdown` 用于提前选择维度；`filter_prune` 只有在精确过滤已由 scanner 执行且相关 DuckDB 行为已验证时才开启。复杂或不支持的谓词保留给 DuckDB。DuckDB 的过滤器传递细节及 table function 类型存在版本差异，Phase 0/3 必须在锁定版本上以 EXPLAIN 和物理 I/O 计数验证。

目前使用 C++ API 是**版本相关的设计决策**：若将来稳定 C API 具备同等 filter pushdown 能力，可以重新评估，不能把当前决定写成永久限制。

## 3. 空间和维度映射

### GridMapping

建议最小接口（示意，非固定 ABI）：

```cpp
struct GridMapping {
    GridSelection select(BoundingBox bbox) const;
    Coordinate coordinate(uint64_t logical_index) const;
    GridPoint nearest(double latitude, double longitude) const;
};
```

`GridSelection` 可表示一个矩形逻辑切片，也可表示多个扁平索引区间，视实际数组布局而定。跨 180° 经线、不同经度约定、反向纬度、网格边界和非矩形 bbox 覆盖必须有明确测试；返回候选点后执行精确地理谓词。

- **RegularGrid**：从 `om-exporter` 提炼 `nx/ny/origin/dx/dy`、坐标互转和 bbox→索引区间。避免先生成全域经纬度数组再过滤。
- **Reduced Gaussian Grid**：优先复用 Open-Meteo 上游算法；`om-exporter` 的 `nx_of(row)`、`integral(row)`、`lat_of(row)` 是 O320/O1280 研究起点。每行点数不同，bbox 可对应多个扁平逻辑区间。旧 N160/N320 简化公式是 TODO，不能直接移植。
- **ProjectionGrid**：参考 Open-Meteo 上游投影实现，补全 WGS84→投影坐标→逻辑索引。`om-exporter` 中的 Lambert、rotated lat/lon、stereographic 等定义可作模型/参数线索，不视为已完成坐标变换。
- **Domain registry**：先用 `om-exporter` 的 `DOMAIN_GRIDS` 作 seed，核对 Open-Meteo 上游定义与真实文件。长期目标是从上游生成 `grids.json` 或等价资产，避免手工维护大块 C++ domain 判断。

Open-Meteo 主项目已有 `grid.findBox(boundingBox:)` 一类网格选择逻辑。实现前先核对其覆盖的网格类型及代码所属层：若存在可直接调用的 C 能力则优先复用；若仅在 Swift 层，duckomo 保留必要的 C++ 映射层。不要默认把整个旧 exporter 移植进来。

### DimensionMapping

独立处理 `timestamp ↔ time index`、`pressure level ↔ level index`、`ensemble member ↔ member index`、`forecast run/lead_time ↔ index`。不能把所有文件硬编码为 `time × grid`：实际可能是 `run × lead_time × level × grid`，并且轴语义可能依赖文件外的 domain/run 信息。映射必须由可验证元数据或显式参数构造。

## 4. OM reader 与 I/O

官方 OM C implementation 是格式读写和局部数组读取的唯一格式核心。duckomo 传入逻辑选择；OM reader 负责 chunk 交集、LUT、压缩和解码。Sans-I/O 设计使 duckomo 能将字节读取请求适配到 DuckDB 文件系统。先跑通本地，Phase 5 再引入 HTTP/S3、缓存、并行任务。不能根据“OM 天然 chunked”就假定扫描已并行或远程读取已高效，需实测。

## 5. 建议目录（实施时创建）

```text
src/
  om_extension.cpp
  scan/           # bind、table function、ScanPlan、predicates
  grid/           # GridMapping、Regular/Gaussian/Projection、registry
  dimensions/     # time、level、forecast/member
  om/             # 官方 C reader 适配、DuckDB I/O adapter
  generated/      # 由上游生成的网格定义
third_party/      # 锁定版本的 OM 依赖（若构建方案采用 vendoring）
scripts/          # registry 生成/校验
test/sql/         # SQL 行为和 EXPLAIN
test/grid/        # 映射边界与上游对照
test/data/        # 小型、可复现 OM fixtures
```

构建骨架参考 DuckDB extension-template；实际依赖引入方式在 Phase 0 确定，不预先承诺 vendoring。

## 6. 实现参考与验证点

| 部分 | 首要参考 | 要验证的点 |
|---|---|---|
| 扩展骨架、测试 | [DuckDB extension-template](https://github.com/duckdb/extension-template) | 构建、版本锁定、SQL 测试 |
| TableFunction、下推、DataChunk | [DuckDB `table_function.hpp`](https://github.com/duckdb/duckdb/blob/main/src/include/duckdb/function/table_function.hpp) 及内建 scanner | 所锁定版本的 filters、projection、filter_prune 语义 |
| C 与 C++ API 取舍 | [DuckDB C API filter pushdown 议题](https://github.com/duckdb/duckdb/issues/25163) | 实施时重新检查 C API 能力 |
| OM metadata/partial read | [Open-Meteo OM file format](https://github.com/open-meteo/om-file-format) | C API、Sans-I/O 适配、文件版本和切片语义 |
| OM 行为对照 | [Open-Meteo python-omfiles](https://github.com/open-meteo/python-omfiles) | 用其切片读取验证值、shape 与局部访问；运行时不依赖 Python |
| 上游网格/domain | [Open-Meteo 主项目](https://github.com/open-meteo/open-meteo) | `Grid.findBox` 覆盖范围、投影与 domain 定义 |
| 旧网格原型 | [`blizhan/om-exporter`](https://github.com/blizhan/om-exporter) | Regular/O-grid 公式、registry seed、未完成的 N-grid 与 projection |
| 远程 I/O | [DuckDB 文件系统/httpfs](https://duckdb.org/docs/stable/core_extensions/httpfs/overview.html) | 所锁定版本的 range read、缓存和凭据行为 |
| 后续 N-D 计算 | [xtensor](https://github.com/xtensor-stack/xtensor) | 只适配已解码数据块；不承担 lazy I/O |
| 后续 SIMD | [xsimd](https://github.com/xtensor-stack/xsimd) 与 OM 自身实现 | 仅在 profiling 证明必要时引入额外向量化代码 |

以上链接是实施参考，不是对未来 API 稳定性的承诺。Phase 0 必须锁定具体 DuckDB/OM 版本，并记录样本文件与测试结果。
