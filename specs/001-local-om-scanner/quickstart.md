# Quickstart: Build and validate the local OM scanner

**验证状态（2026-09-28）**：Phase 0–2 构建、SQL/native 检查、fixture 重生成、validation harness 和 sanitizer 已在 Linux AArch64 上执行。计划目标是 Linux x86_64；当前环境没有 x86_64 执行验证。查询进程只需要 DuckDB CLI、扩展和 fixture，不调用 Python 或 Swift。命令与结果见 [phase0 evidence](evidence/phase0.md)、[phase1 evidence](evidence/phase1.md)、[phase2 evidence](evidence/phase2.md) 和 [sanitizer evidence](evidence/sanitizer.md)。

## Build and test

先在仓库根目录初始化固定依赖并构建 release：

```sh
git submodule update --init --recursive
make release
```

主要产物：

- `build/release/duckdb`
- `build/release/extension/duckomo/duckomo.duckdb_extension`
- `build/release/test/tools/duckomo_fixture_tool`
- `build/release/test/tools/duckomo_validation`
- `build/release/test/native/*_test`

`make test` 运行五个 native 检查和 projection validation harness，并把 harness 证据写到 `build/evidence/`。完整集成门禁还会运行 SQLLogicTest、校验 fixture hash 并独立重生成 fixture：

```sh
./scripts/validate.sh build/release
```

该脚本需要 `jq`、`sha256sum`、`diff` 和 `mktemp`。如需单独验证 sanitizer 插桩后的扩展及生命周期/边界/损坏输入检查：

```sh
make sanitizer-test
```

## Reproduce and compare fixtures

生成器使用固定的官方 OM writer，并通过单独的官方 reader 路径写出参考 CSV 与 manifest。用临时目录生成，再与提交的 fixture 逐文件比较：

```sh
repro_dir="$(mktemp -d "${TMPDIR:-/tmp}/duckomo-fixture-repro.XXXXXX")"
trap 'rm -rf -- "$repro_dir"' EXIT
./build/release/test/tools/duckomo_fixture_tool --output "$repro_dir"
diff -qr -- test/data "$repro_dir"
```

退出码 0 表示 OM 文件、参考 CSV、负向样本和 manifest 均与提交资产一致。`scripts/validate.sh build/release` 也会在独立临时目录重生成并比较这些资产，同时逐项检查 manifest 中的 SHA-256。

## Python/Swift-free CLI smoke test

从仓库根目录运行下列命令。`-unsigned` 允许 DuckDB 加载本地开发扩展；它不表示扩展已签名。

```sh
./build/release/duckdb -unsigned :memory: <<'SQL'
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
DESCRIBE SELECT * FROM read_om_raw('test/data/raw.om');
SELECT * FROM read_om_raw('test/data/raw.om') ORDER BY value;
SELECT count(*) FROM read_om('test/data/raw.om');
SQL
```

预期：`LOAD` 成功；`DESCRIBE` 返回 `value FLOAT`；完整扫描返回 `0` 到 `5`；计数为 `6`。这四项分别覆盖扩展加载、模式读取、全量读取和仅计数。对应的 SQLLogicTest 在 `test/sql/raw.test` 与 `test/sql/read_om.test`。

## Multi-variable full and narrow scans

多变量文件必须为每个 canonical variable path 显式提供有序轴名。DuckDB MAP 的实际写法如下；这与已通过的 `test/sql/read_om.test` 相同：

```sh
./build/release/duckdb -unsigned :memory: <<'SQL'
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
DESCRIBE SELECT * FROM read_om(
  'test/data/multi.om',
  dimensions := map(
    ['/humidity', '/temperature'],
    [['row', 'column'], ['row', 'column']]
  )
);
SELECT "/humidity", "/temperature"
FROM read_om(
  'test/data/multi.om',
  dimensions := map(
    ['/humidity', '/temperature'],
    [['row', 'column'], ['row', 'column']]
  )
)
ORDER BY "/humidity";
SELECT "/temperature"
FROM read_om(
  'test/data/multi.om',
  dimensions := map(
    ['/humidity', '/temperature'],
    [['row', 'column'], ['row', 'column']]
  )
)
ORDER BY "/temperature";
SELECT "/temperature"
FROM read_om(
  'test/data/multi.om',
  dimensions := map(
    ['/humidity', '/temperature'],
    [['row', 'column'], ['row', 'column']]
  )
)
WHERE "/humidity" >= 103
ORDER BY "/temperature";
SQL
```

预期：schema 按路径排序为 `/humidity FLOAT`、`/temperature FLOAT`；全量扫描返回 `(100,0)` 到 `(105,5)`；窄查询仍返回六行 `0` 到 `5`；过滤查询返回 `3`、`4`、`5`。即使湿度列没有出现在结果中，它仍是过滤依赖，必须参与扫描。`COUNT(*)` 的值数组读取成本和 projection fixture 上全量/窄查询的实际字节、decoder 计数由 validation harness 检查，不能用 `EXPLAIN` 代替指标。

## Projection and evidence

`test/data/projection.om` 包含 `/humidity`、`/pressure`、`/temperature` 三个变量，形状为 `[83,127]`，共 10,541 行。完整扫描读取全部变量；只选择一个变量时其余变量不读取或解码；输出 temperature 并按 humidity 过滤时，两者都读取；`COUNT(*)` 不读取 index 或 data。四个场景由以下命令完整消费查询结果并写入 `build/evidence/summary.json` 与场景 JSON：

```sh
./build/release/test/tools/duckomo_validation \
  --root "$PWD" \
  --fixtures "$PWD/test/data" \
  --output "$PWD/build/evidence" \
  --duckdb "$PWD/build/release/duckdb" \
  --extension "$PWD/build/release/extension/duckomo/duckomo.duckdb_extension"
```

建议通过 `./scripts/validate.sh build/release` 执行完整门禁。指标策略固定为单线程、无 DuckDB 应用层查询缓存；证据区分 metadata、index、data 的成功读取字节/请求数，并记录成功 decoder 调用处理的 chunk 数。操作系统页缓存由 evidence 明确记录，数据字节表示应用读取量，不代表设备物理 I/O。字段约定见 [validation evidence contract](contracts/validation-evidence.md)。

已记录的 release 数据字节如下：

| 场景 | 数据字节 | 已观测行为 |
| --- | ---: | --- |
| 全量扫描 | 628,703 | 三个变量均读取并解码 |
| 仅 temperature | 165,767 | humidity 和 pressure 无数据读取或解码；比全量少 73.6% |
| temperature 输出、humidity 过滤 | 360,619 | humidity 和 temperature 读取；pressure 无数据读取或解码 |
| `COUNT(*)` | 0 | 只读取 660 metadata 字节，无 index/data 读取或解码 |

截至 Phase 2 记录，`make test` 退出码为 0（144 项 SQL assertions、五个 native 检查和 harness 通过），`./scripts/validate.sh build/release` 退出码为 0（26 个 fixture/reference/negative 资产 SHA-256、fixture 重生成比较和四个场景证据通过），`make sanitizer-test` 退出码为 0（ASan/UBSan 检查 4/4 通过）。这些结果均来自 Linux AArch64；耗时和 RSS 等完整测量值见 [phase2 evidence](evidence/phase2.md)。

## Supported behavior and current platform

- `read_om_raw(path)` 扫描单个根 Float32/FPX 数组，暴露 `value FLOAT`。
- `read_om(path, dimensions := map(VARCHAR, VARCHAR[]))` 支持 OM v3 层级容器中的 Float32/FPX 数组；多变量需要完整且一致的有序轴声明。schema 列按 canonical 路径排序。
- NaN 映射为 SQL NULL，正负无穷及正负零保留。缺失文件、截断数据、未知版本、不支持的类型/压缩、缺少或不一致的轴声明会报错。
- 当前只支持本地文件。latitude、longitude、time 等坐标列和物理坐标映射尚未实现。

测试记录对应主机为 Linux AArch64；计划定义的首发目标为 Linux x86_64。SQL smoke command 只需 DuckDB CLI、扩展和 fixture，不运行 Python 或 Swift。最终验收中的隔离运行环境结果和未完成的平台检查见 [final evidence](evidence/final.md)；不能从 AArch64 结果推断 x86_64 已通过。
