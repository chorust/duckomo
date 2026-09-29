# duckomo 技术架构

状态：2026-09-28。Phase 0–2 已实现为 **C++ DuckDB extension + 官方 Open-Meteo OM C library**，并在 Linux AArch64 验证；Linux x86_64 尚无运行证据。网格、坐标和谓词下推仍是后续设计。当前实现边界及运行证据见 [SQL 契约](../specs/001-local-om-scanner/contracts/sql-interface.md) 与 [最终验收记录](../specs/001-local-om-scanner/evidence/final.md)。

## 1. 当前实现与后续职责

| 层 | 回答的问题 | 职责 |
|---|---|---|
| DuckDB table function（已实现） | 查什么？ | Bind、SQL schema、projection pushdown、执行调度、DataChunk 输出 |
| ProjectionPlan（已实现） | 本次查询读哪些变量？ | 根据 `column_ids` 保留输出与过滤依赖，去重物理变量并维持输出顺序 |
| GridMapping / DimensionMapping（规划中） | 在数组哪里？ | 经纬度、时间、level、member、lead time 等语义坐标与 OM 逻辑索引互转 |
| 官方 OM C reader（已接入） | 如何读取？ | hierarchy/metadata、chunk/LUT/byte range、解压 |
| I/O adapter（本地已实现） | 从哪里读字节？ | 将 OM Sans-I/O 请求接到 DuckDB 文件系统；HTTP/S3 尚未实现 |

```text
DuckDB SQL
  → read_om Bind：本地 OM metadata、schema、dimensions 对齐
  → GlobalInit：ProjectionPlan（输出列和过滤依赖）
  → Scan：OM C reader → DuckDB 本地文件系统适配器
  → decoded values → DuckDB Vector/DataChunk → DuckDB WHERE
```

后续 `GridMapping::select(bbox)` 应返回**逻辑网格点/索引区间**，不规划 chunk ID、OM 文件 byte range 或解压；这些仍由官方 OM reader 负责。

## 2. DuckDB 集成

`Bind` 读取足以确定 schema 的元数据，并固定变量与维度解释。`GlobalInit` 根据 DuckDB 的 `column_ids` 构造 `ProjectionPlan`；查询需要的输出列和过滤列都保留，重复列映射到去重后的物理变量，输出顺序仍按 SQL 请求保留。`COLUMN_IDENTIFIER_EMPTY` 作为只提供行数的内部 cardinality slot；`COUNT(*)` 不读取数组索引或数据。Scan 将官方 decoder 解出的值直接放入 DuckDB Vector。当前扫描单线程运行，本地读由 DuckDB 文件系统适配器完成。

`projection_pushdown` 用于减少变量读取；本阶段 `filter_pushdown` 和 `filter_prune` 均关闭，SQL 过滤由 DuckDB 正常执行，谓词所需变量仍参与扫描。DuckDB 的列裁剪行为已在固定版本上通过结果对照和实际 I/O 指标验证；计数查询的 cardinality 路径也已单独检查。未来只有在精确谓词执行和过滤依赖保留经过集成验证后，才考虑增加维度选择下推。

Phase 0–2 的 [实现计划](../specs/001-local-om-scanner/plan.md) 固定 DuckDB v1.5.4 和官方 OM 源码提交（完整版本见 [研究记录](../specs/001-local-om-scanner/research.md)）。多变量轴身份通过一致的 `coordinates` 元数据或显式 `dimensions` 参数验证，不凭 shape 单独推断；这一逻辑只处理对齐，不实现后续坐标映射。扫描采用本地定位读取与有界批次，官方 reader 仍拥有 chunk 和字节请求规划权。支持的 OM v3、Float32、FPX/PFOR 子集和文件格式拒绝规则见 [SQL 契约](../specs/001-local-om-scanner/contracts/sql-interface.md)。

目前使用 C++ API 是**版本相关的设计决策**：若将来稳定 C API 具备同等 filter pushdown 能力，可以重新评估，不能把当前决定写成永久限制。

## 3. 空间和维度映射（尚未实现）

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

官方 OM C implementation 是格式读取核心。当前扫描将本地文件的字节请求接入 DuckDB 文件系统，由官方 reader 负责 chunk 交集、LUT、压缩和解码；应用层按变量列裁剪并分有界批次输出。尚无地理或时间逻辑切片。Phase 5 计划引入 HTTP/S3、缓存和并行任务，届时需实测远程局部读取的效果。

## 5. 代码目录与后续模块

```text
src/
  om_extension.cpp  # 注册 read_om 和 read_om_raw
  scan/             # bind、schema、projection、batch、table functions
  om/               # OM reader、metadata、本地文件适配
third_party/om-file-format/  # 固定的官方 OM 源码
test/sql/           # SQLLogicTests
test/native/        # 原生检查
test/data/          # 可复现 OM fixtures 和参考结果
scripts/validate.sh # 综合验证
```

后续的 grid、dimensions 和 registry 模块将在对应阶段建立，目前没有这些目录。

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

以上链接是实施参考，不是对未来 API 稳定性的承诺。Phase 0–2 已锁定 DuckDB v1.5.4、OM C 源码提交与 fixture，并记录验证结果；具体提交见 [研究记录](../specs/001-local-om-scanner/research.md)。
