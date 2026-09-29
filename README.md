# duckomo

中文 | [English](README.en.md)

duckomo 是一个 DuckDB C++ 扩展，可直接读取本地 [Open-Meteo OM](https://github.com/open-meteo/om-file-format) 文件中的 Float32 数组，并将其作为 SQL 表查询。当前 `read_om` 支持 OM v3、FPX_XOR2D 与 PFOR_DELTA2D_INT16 压缩、带附属元数据的层级变量和列裁剪；只读取查询需要的值变量。`read_om_raw` 是用于单根数组的早期验证入口，常规查询请使用 `read_om`。

目前仅支持本地文件，不生成经纬度或时间坐标列；空间、时间条件下推、远程读取和并行扫描仍在规划中。完整的文件格式和 SQL 限制见 [SQL 接口契约](specs/001-local-om-scanner/contracts/sql-interface.md)。

## 安装

目前从源码构建并加载扩展。需要支持 C11 和 C++17 的编译器、CMake、Make、Git，以及构建 DuckDB 所需的依赖。仓库通过 submodule 固定 DuckDB、extension-ci-tools 和 OM C 库版本；请在仓库根目录执行：

```sh
git submodule update --init --recursive
make release
```

构建产物为 `build/release/duckdb` 和 `build/release/extension/duckomo/duckomo.duckdb_extension`。该扩展针对仓库固定的 DuckDB v1.5.4 构建，也可加载到同版本、同平台的官方 DuckDB 安装中；本地构建的扩展未签名，CLI 需使用 `-unsigned`。随构建生成的 CLI 只是方便核对版本的开发工具。

如需为指定的 DuckDB 正式版本单独构建并运行 SQL 用例，执行（以 v1.5.5 为例）：

```sh
./scripts/build-version.sh v1.5.5
duckdb -unsigned :memory:
```

上述示例要求安装的 `duckdb` 是 v1.5.5。在 CLI 中执行 `LOAD 'build/versions/v1.5.5/release/extension/duckomo/duckomo.duckdb_extension';` 即可加载对应扩展。脚本不设置版本白名单：对指定的 `vX.Y.Z` tag，分别获取源码、构建扩展并运行 SQL 用例。源码和产物放在 `build/versions/<版本>/`，不会切换仓库固定的子模块。C++ 扩展需与目标 DuckDB 版本分别编译；遇到实际不兼容问题时再修复或明确限制。

## 使用

从仓库根目录启动同版本的 DuckDB CLI（官方安装或随构建生成的均可）。`-unsigned` 允许加载本地构建的未签名扩展；以下示例使用仓库固定的 v1.5.4：

```sh
./build/release/duckdb -unsigned :memory:
```

加载扩展并读取单根数组：

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';

DESCRIBE SELECT * FROM read_om('test/data/raw.om');
SELECT value FROM read_om('test/data/raw.om') ORDER BY value;
```

多变量文件若有一致的 `coordinates` 元数据且形状相同，可直接读取；其他文件需要为**每个变量**声明完整且一致的有序轴名。列名不带前导 `/`，嵌套列名中的 `/` 仍需用双引号引用：

```sql
SELECT temperature
FROM read_om(
  'test/data/multi.om',
  dimensions := map(
    ['humidity', 'temperature'],
    [['row', 'column'], ['row', 'column']]
  )
)
WHERE humidity >= 103
ORDER BY temperature;
```

上例返回 `3`、`4`、`5`。`dimensions` 中的轴名用于核对数组对齐，不会生成 `row` 或 `column` 列。真实 `data_spatial` 文件的 `coordinates = 'lat lon'` 可用于同形状变量的自动对齐；它也不会生成坐标列。列裁剪会减少未使用变量的读取；普通 `WHERE` 过滤仍由 DuckDB 执行，不会缩小数组的读取范围。NaN 映射为 SQL `NULL`。也可用 `read_om_raw('test/data/raw.om')` 读取单根数组。

## 架构

```text
SQL → read_om table function → 元数据与 schema / 列选择
    → 本地文件适配器 → 官方 OM C reader → DuckDB DataChunk
```

- `src/scan/`：绑定参数、校验变量及维度、规划列选择并输出 DuckDB 数据块。
- `src/om/`：通过 DuckDB 文件系统读取本地文件，调用官方 OM C reader 解析元数据和解码数组。
- `third_party/om-file-format/`：固定版本的官方 OM 格式实现。

当前扫描为单线程。OM reader 负责 chunk、字节请求和解码；DuckDB 负责 SQL 过滤。设计与后续阶段见 [技术架构](docs/architecture.md) 和 [Roadmap](docs/roadmap.md)。

## 开发

在仓库根目录使用以下命令：

```sh
make release                         # 构建 CLI、扩展和开发工具
make test                            # SQL、原生检查和投影验证
./scripts/validate.sh build/release  # SQL、fixture、校验和及读取指标的完整验证
make sanitizer-test                  # ASan/UBSan 检查
```

`validate.sh` 额外需要 `jq`、`sha256sum`、`diff` 和 `mktemp`，验证证据写入 `build/evidence/`。SQL 用例在 `test/sql/`，原生检查在 `test/native/`，可复现的 OM 样本在 `test/data/`。构建和验证细节见 [Quickstart](specs/001-local-om-scanner/quickstart.md)；已有验证记录见 [evidence](specs/001-local-om-scanner/evidence/)。
