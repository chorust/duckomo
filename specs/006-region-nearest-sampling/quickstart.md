> 当前结论：006 声明范围内 N0–N7 已通过，见 [最终验收报告](evidence/final.md)。以下历史运行包的 partial/blocked 描述不代表当前最终状态。

# Quickstart: 同一个区域入口的 points 与 nearest

状态：实现后的验证指南。`read_om_regions` 的 points/nearest 参数、合成fixture、独立locator参考与N0–N7 validator 已实现；本页示例可用匹配的 pinned runtime 执行。未完成的验收门仍须按 evidence 报告为 blocked/partial，不以示例查询替代。

## 1. 准备匹配构建

仓库根目录运行。下列构建/依赖命令已存在；下载步骤需要网络或预先准备的官方包，不要求在线查询时安装Python。

```bash
python3 scripts/fetch-region-runtime.py
make release
cmake --build build/release --target duckomo_developer_tools --parallel
```

默认build/release使用固定DuckDB v1.5.4。各版本的CLI、duckomo、spatial必须匹配，不能把另一版本发布包加载进来。设置工具参数：

```bash
export DUCKOMO_TEST_CLI="$PWD/build/release/duckdb"
export DUCKOMO_TEST_EXT="$PWD/build/release/extension/duckomo/duckomo.duckdb_extension"
export DUCKOMO_TEST_SPATIAL="$PWD/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension"
```

先探测依赖（当前即可运行；此项只验证匹配 spatial 的函数签名和几何结果）：

```bash
"$DUCKOMO_TEST_CLI" -unsigned :memory: -c \
  "LOAD '$DUCKOMO_TEST_SPATIAL'; SELECT ST_AsText(ST_PointOnSurface(ST_GeomFromText('POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))')));"
```

预期POINT (100.15 20.15)。若空间包加载失败先修复版本/身份，不改成隐式安装或私有几何实现。

## 2. 用现有小fixture切换方法

以下SQL可在当前实现上运行。`test/data/regions/regions.om` 是5×5规则网格，member×time=2×3；Gap区域不含任何网格点，适合对照 points 零行与 nearest 逐非空间轴组合输出。

将下面SQL保存到本地验证文件，替换两个扩展路径为第1节的准确值后，在仓库根目录通过CLI运行：

```sql
LOAD 'build/release/extension/duckomo/duckomo.duckdb_extension';
LOAD 'build/regions-runtime/v1.5.4/official/spatial.duckdb_extension';

SET VARIABLE demo_regions = [
  {'id':'Gap','wkb':ST_AsWKB(ST_GeomFromText(
    'POLYGON((100.1 20.1,100.2 20.1,100.2 20.2,100.1 20.2,100.1 20.1))'))},
  {'id':'Empty','wkb':ST_AsWKB(ST_GeomFromText('POLYGON EMPTY'))},
  {'id':'Outside','wkb':ST_AsWKB(ST_GeomFromText(
    'POLYGON((110 30,111 30,111 31,110 31,110 30))'))}
];
SET VARIABLE demo_grid = {'nx':5,'ny':5,'lat0':20.0,'lon0':100.0,
                          'dlat':1.0,'dlon':1.0,'order':'separate'};
SET VARIABLE demo_axes = {
  'time':{'axis':'time','start':TIMESTAMP '2026-01-01 00:00:00','step':INTERVAL '1 hour'},
  'member':{'axis':'member','values':[0::BIGINT,1::BIGINT]}
};

-- 查看文件或矩形范围：普通read_om。
SELECT lat,lon,value
FROM read_om('test/data/regions/regions.om',
  dimensions := ['member','time','lat','lon'], grid := getvariable('demo_grid'),
  spatial_axes := ['lat','lon'], axes := getvariable('demo_axes'))
WHERE member=0 AND valid_time=TIMESTAMP '2026-01-01 00:00:00'
  AND lat BETWEEN 20 AND 21 AND lon BETWEEN 100 AND 101;

-- 已有polygon：用区域入口。三个区域都没有区内原始点，总计0。
SELECT count(*) AS source_records
FROM read_om_regions('test/data/regions/regions.om',
  regions := getvariable('demo_regions'), region_crs := 'grid_lonlat',
  dimensions := ['member','time','lat','lon'], grid := getvariable('demo_grid'),
  spatial_axes := ['lat','lon'], axes := getvariable('demo_axes'), method := 'points');

-- 同一个函数，只切换method。
CREATE TEMP TABLE nearest_results AS
SELECT * FROM read_om_regions('test/data/regions/regions.om',
  regions := getvariable('demo_regions'), region_crs := 'grid_lonlat',
  dimensions := ['member','time','lat','lon'], grid := getvariable('demo_grid'),
  spatial_axes := ['lat','lon'], axes := getvariable('demo_axes'),
  method := 'nearest', include_source := true);

SELECT region_id,count(*) AS result_rows,
       min(sampling.status) AS location_status
FROM nearest_results GROUP BY region_id ORDER BY region_id;

SELECT region_id,lat,lon,sampling.source_lat,sampling.source_lon,
       sampling.distance_m,sampling.variables['value'].status AS value_status,
       value,om_source.point_index
FROM nearest_results
WHERE member=0 AND valid_time=TIMESTAMP '2026-01-01 00:00:00'
ORDER BY region_id;
```

预期每个区域6行，总18行：Gap几何status=ok、代表位置(100.15,20.15)、源位置(100,20)、point_index=0；Empty状态empty_geometry、无坐标/来源和值；Outside状态outside_domain、有代表坐标但无源候选/来源和值。Gap个别时间的值若缺测，变量status=source_null但来源不改变。COUNT(*)表示结果行，不是区内点数。

## 3. 检查阈值与来源

复用第2节SQL环境。将最近邻调用的max_distance_m设为0，Gap应为beyond_max_distance：保留候选(100,20)和distance_m，value/om_source为NULL；不应寻找另一个点。out_of_bounds='error'并包含Outside应使绑定失败，即使WHERE只留Gap或LIMIT 0。

正常Gap来源可直接用read_om的`lat=20 AND lon=100`、相同time/member对照，值/NULL和原始来源位置应一致。nearest顶层lat/lon是目标位置；真实源位置在sampling。

额外参数/状态的完整定义见[SQL契约](contracts/sql-interface.md)，数值阈值等号与平局见[数值契约](contracts/numeric-and-capability.md)。

## 4. 验证零值路径与详情依赖

不要用已物化nearest_results测试I/O；重新调用原表函数，再读取duckomo_last_scan_metrics。对第2节相同参数依次查询：

- COUNT(*)、region_id/lat/lon、sampling.source_lon/source_lat/distance_m、om_source：值index/data/decode应为0。
- value或WHERE value>0：只读取对应变量。
- sampling.variables或整个sampling：读取所有绑定变量以生成真实状态；可通过variable明确只绑定一个数组。

本页`CREATE TEMP TABLE ... SELECT *`会请求完整sampling和值，不是零值路径。不能由后续SELECT少量列推断原扫描少读。EXPLAIN仅帮助确认字段路径，实际metrics才是成本证据。

## 5. 执行自动验证

当前仓库提供 `sampling-validation` 入口；它重生并核对采样fixture，运行普通本地回归、采样 SQL/native 与 validator，并在已提供匹配 spatial 时构建/运行采样 ASan/UBSan。`region-validation` 仍验证005区域范围。普通 `make test` 的跳过行为如下：

```bash
make region-validation
make sanitizer-test
make sampling-validation \
  DUCKOMO_SPATIAL_EXTENSION="$PWD/build/regions-runtime/v1.5.4/official/spatial.duckdb_extension" \
  DUCKOMO_SAMPLING_DUCKDB="$PWD/build/regions-runtime/v1.5.4/official/duckdb"
```

普通 `make test` 会略过需要匹配 spatial 的区域 SQL/native 测试；它本身不能单独证明 006 通过。`sampling-validation` 会显式使用所选 spatial package 并把路径与 SHA-256 绑定到 sanitizer 配置。N2还会在DuckDB v1.5.4、v1.5.5、v1.5.6分别运行继承的 `region_input_test`，验证005区域输入类型、WKB和限额行为。验证器按 gate 区分本地通过、blocked、fail、not-run；即使命令整体返回2，也应查看新生成的manifest，不能将部分通过扩写为完整验收。

以下fixture/reference命令由验证入口调用，也可单独运行：

```bash
build/release/test/tools/duckomo_fixture_tool --sampling-output test/data/sampling
python3 scripts/generate-sampling-reference.py --output test/data/sampling
build/release/test/native/nearest_sampling_test
DUCKOMO_SPATIAL_EXTENSION="$DUCKOMO_TEST_SPATIAL" \
  build/release/test/native/region_sampling_test

python3 scripts/validate-sampling.py \
  --root "$PWD" --build "$PWD/build/release" \
  --duckdb "$DUCKOMO_TEST_CLI" --extension "$DUCKOMO_TEST_EXT" \
  --spatial "$DUCKOMO_TEST_SPATIAL" \
  --manifest "$PWD/test/data/sampling/manifest.json" \
  --output "$PWD/build/evidence/006/local-v1.5.4-run1" --gates N0,N1,N2,N3,N4
```

输出目录须新建/为空。N0逐行核对冻结结果及逻辑读取、值数据读取和解码指标；物理索引读取次数/字节及其legacy总读取成本允许因worker范围复用而低于基线，但不得增加。该比较不放宽结果、逻辑工作量或值数据成本。验证器会为满足对应注册范围的门记录 `pass`；N1/N2 最新三版本运行包为 [`n1-n2-acceptance-refresh-20261019-final-7`](evidence/n1-n2-acceptance-refresh-20261019-final-7/)，两门均通过。N1采用冻结能力边界、独立高精度距离及12个GFS025目标的全源网格比较；N2采用注册SQL/native/parser用例和四条组合无效参数优先级探针。两门的有限用例边界已记录，不声称穷尽任意binary64输入或全部解析器组合。N5完整gate、N6三版本全审计仍未完成；已有N7本地5×5复现由参与实现的worker完成，不满足独立执行者要求。完整状态见[`evidence/final.md`](evidence/final.md)。每个运行结论附命令、身份与输出hash；`--self-check` 不等同gate通过。N1/N2逐profile验收范围限定为合成规则球面网格和NCEP GFS025；其它登记domain不在本feature逐profile声明中。

## 6. 真实东城区与远程

真实输入冻结/许可边界见[验收契约](contracts/validation-evidence.md)。实施时先生成可交付manifest及有授权的几何来源，向工具传入原GFS文件和冻结东城区几何路径，不能依赖ignored build文件或静默替换过期公网对象。

```bash
# N5 在已确认私有使用授权、归档和受控远程服务后执行；不公开分发原始对象。
# DUCKOMO_PRIVATE_ARCHIVE 指向私有归档 manifest，DUCKOMO_REMOTE_ENV 指向本机 fixture 的 run.env。
python3 scripts/validate-sampling.py \
  --root "$PWD" --build "$PWD/build/release" \
  --duckdb "$DUCKOMO_TEST_CLI" --extension "$DUCKOMO_TEST_EXT" \
  --spatial "$DUCKOMO_TEST_SPATIAL" --manifest "$PWD/test/data/sampling/manifest.json" \
  --real-file "$DUCKOMO_GFS_FILE" --real-regions "$DUCKOMO_DONGCHENG_GEOMETRY" \
  --private-archive "$DUCKOMO_PRIVATE_ARCHIVE" --remote-env "$DUCKOMO_REMOTE_ENV" \
  --output "$PWD/build/evidence/006/real-local-run1" --gates N5
```

N5分真实本地与受控远程子场景：实际执行命令和已有的HTTP/HTTPS/签名S3结果见 [N5-dongcheng.md](evidence/N5-dongcheng.md)。当前 validator 的 N5 会校验真实文件/几何hash、私有归档及取回记录，使用 `--remote-env` 指定的loopback HTTP/HTTPS/签名S3服务运行三个版本的真实与合成差分、原始值对照和拒绝/恢复。HTTPFS由冻结版本矩阵校验，不从任意 `--httpfs` 参数替换。凭据只经进程标准输入，不写入SQL证据；服务日志是服务端应用记录，不冒充HTTPFS网络字节。N6使用 `--evidence-root` 指向同一当前源码的完整N0–N5包，审计命令、SQL、输出、版本及哈希；缺失/篡改证据必须拒绝。CLI和认证环境沿既有验证设施，不在SQL/日志写凭据。预期东城区points零点、nearest源(116.50,40.00)、值与原源记录一致。若原文件不可取得，明确blocked并保存原因。

## 7. 三版本与独立复现

现有隔离构建命令：

```bash
scripts/build-version.sh v1.5.4
scripts/build-version.sh v1.5.5
scripts/build-version.sh v1.5.6
```

脚本默认隔离build root与实际build_id路径按它生成的manifest解析，勿猜测另一个矩阵目录的旧产物。矩阵完整构建会记录 `sampling-build-manifest.json`，包含当前源码摘要、DuckDB commit和原生测试/扩展哈希；validator 会拒绝缺失、过期或被替换的产物。默认 `build/release` 在修改源码、测试或fixture后，先运行 `python3 scripts/sampling_build.py --build build/release --sanitizers` 重建并记录身份；N4需要已配置的匹配spatial sanitizer。自定义矩阵根目录使用 `--matrix-output-root`。每个版本各跑N0–N5，用N6审计全部证据；新执行不得覆盖旧005记录。未参与实现者再按本页执行选择入口、切换method、状态辨别和来源对照，作为N7独立报告；执行者为人工或AI必须如实记录。

完成判断以N0–N7契约和实际证据为准，不以文档生成或一个查询返回值作为全部功能验收。

## 独立 N7 留档

由未参与实现的用户运行 `bash scripts/run-n7-manual.sh --attested-by "你的姓名"`；署名表示确认本人为未参与实现的独立人工执行者。运行器保存身份、完整SQL/输出和退出码，成功后生成包含署名和文件哈希的 `manifest.json` 并自动审计；失败运行不会生成通过的 manifest。运行器需要 Python 3。正式接口使用 `--gates N7 --evidence-root <用户证据目录>` 校验留档。N7单独审计的是已接受的执行快照；后续文档/验证工具修改不要求用户重跑扫描器。
