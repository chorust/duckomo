# SQL Interface Contract: Phase 3

> 当前开发版修订：SQL 地理列统一为 `lat/lon`；`dimensions` 支持共享 `VARCHAR[]` 或完整逐变量 MAP；远程错误输出脱敏类别与建议。迁移及完整规则见 [读取接口修订](../../../docs/interface-migration.md)。历史 evidence 仍对应原构建，不因本修订提升验收状态。

状态：已实现并在 Linux AArch64 独立验证；Linux x86_64 支持与验证暂缓，不属于本期范围。继承 [Phase 0–2 契约](../../001-local-om-scanner/contracts/sql-interface.md) 的本地文件、类型、缺测、对齐、命名和错误约束。完整结果见 [US1](../evidence/us1.md)、[US2](../evidence/us2.md)、[US3](../evidence/us3.md) 与 [独立 quickstart 复核](../evidence/quickstart-review.md)。

## 参数

```text
read_om(path VARCHAR,
        dimensions ANY := NULL, -- VARCHAR[] 或 MAP(VARCHAR,VARCHAR[])
        grid STRUCT(nx BIGINT, ny BIGINT,
                    lat0 DOUBLE, lon0 DOUBLE, dlat DOUBLE, dlon DOUBLE,
                    order VARCHAR) := NULL,
        spatial_axes VARCHAR[] := NULL,
        domain VARCHAR := NULL)
```

上列 STRUCT 为公开字段契约；内部以原始结构值校验后转换，避免隐式 BIGINT 转型提前舍入 nx/ny。参数均在 bind 确定；显式 NULL 等同缺省。grid STRUCT 必须精确包含所列字段，不接受未知或缺失字段，内部字段不能 NULL。nx/ny 必须为正整数且转换无损，不允许浮点小数经隐式舍入成为轴长。domain 名称精确、区分大小写，空或未知名称拒绝。

- 无 grid/domain：保留既有值 schema；spatial_axes 单独提供时报配置不完整错误。
- grid 与 domain 同时非 NULL：拒绝。
- grid：必须提供 spatial_axes，且通过 dimensions 或一致 coordinates 元数据得到全部轴身份。单数组缺轴身份时也必须提供 dimensions。
- `order='separate'`：spatial_axes 长度为 2，顺序是纬度轴、经度轴。
- `order='lon_fastest'` 或 `'lat_fastest'`：长度为 1，表示展平轴内变化最快方向。
- domain：名称取 Open-Meteo AWS `data/`、`data_run/`、`data_spatial/` 后的 domain prefix，使用 registry 固定规则网格；禁止额外 spatial_axes 覆盖。文件必须提供完整有序的 `lat`、`lon` 轴身份，可来自一致的 coordinates 元数据，或由调用者通过 dimensions 为每个值变量完整声明。dimensions 不得覆盖文件已有的不同 coordinates。额外轴（如 `time`）按原顺序保留；shape 中 `lat`/`lon` 轴长必须匹配 registry；存在 WKT BBOX 时还须匹配网格边界。

校验必须覆盖全部值变量，不能因投影绕过 shape/轴不兼容或格式错误。网格数值与布局规则见 [data model](../data-model.md)。

## 输出模式

原值列及顺序不变，随后追加 `lat DOUBLE`、`lon DOUBLE`，两列非 NULL。按 DuckDB 标识符等价规则检测与源列的冲突（含大小写），坐标模式冲突即拒绝；不重命名已有列。没有网格时不生成坐标，引用不存在列仍报绑定错误。

lat 通常在 [-90,90]；仅 `meteofrance_wave`、`meteofrance_currents`、`meteofrance_sea_surface_temperature` 的上游规则网格末行达到约 90.041664°，按源定义输出。显式 grid 仍要求纬度在 [-90,90]。lon 在 [-180,180)。负步长不改变逻辑记录身份；两个接缝端点若为两个源位置则保留两条记录。额外轴位置不被折叠，不生成 time/level/member 等列。不承诺无 ORDER BY 的 SQL 行序。

## 条件与回退

有限数值常量的 `=,<,<=,>,>=,BETWEEN` 和 AND 可缩小扫描，反向常量比较等价支持；单轴条件保持另一轴全范围。比较依据输出 DOUBLE 值，不使用 epsilon。所有 WHERE 仍由 DuckDB 执行，候选范围不得排除匹配行。

`lon BETWEEN 170 AND -170` 按普通 SQL 返回空，不环绕。跨接缝写法：

```sql
WHERE lon >= 170 OR lon < -170
```

OR 首版允许全域回退；不得仅采用其中一个区间。坐标函数/cast、复杂表达式、非有限/NULL 比较、安全性不能证明的条件回退并正常执行。混合 AND 可以采用独立的安全必要条件，保留其他条件；混合 OR 整棵子树回退。

可证明空选择：零值 index/data 读取、零解码。仅坐标/纯空间 count：同样零值读取与解码，允许 metadata 开销。混合值过滤保留过滤变量，无关变量解码为零。未命中物理块边界不承诺节省。

## 示例：明确两轴

```sql
SELECT value, lat, lon
FROM read_om('test/data/raw.om',
  dimensions := ['lat','lon'],
  grid := {'nx':3, 'ny':2, 'lat0':10.0, 'lon0':100.0,
           'dlat':1.0, 'dlon':2.0, 'order':'separate'},
  spatial_axes := ['lat','lon'])
WHERE lat >= 11 AND lon < 104;
```

预期为 value=3/4，对应 (11,100)/(11,102)。raw.om 没有地理身份，这是调用者显式赋予的演示网格。

## 错误与恢复

参数不完整、零轴/零步长、非有限网格、纬度越界、坐标溢出、乘积溢出、shape/轴冲突、未知 domain、重复空间轴、坐标列冲突均在返回行前拒绝，报告参数或变量及原因。损坏和取消使整个查询失败；释放资源后有效查询仍可执行。失败批次不得计为成功完整结果。

## 命名 domain

Registry 登记 68 个 Open-Meteo AWS 规则网格 domain，完整名称、来源和样本覆盖见 [规则网格 domain](../../../docs/regular-domains.md)。`domain := 'ncep_gfswave025'` 选择 ny=721、nx=1440、纬度 -90 起/经度 -180 起、步长均 0.25° 的规则网格。二维 `[lat,lon]` 和带额外轴的 `[lat,lon,time]` 均可绑定；其他轴顺序只要有完整身份且空间轴长度正确也可绑定。缺少 coordinates 的文件必须显式提供完整 dimensions，不从路径或 shape 推断轴。

```sql
SELECT wave_height, lat, lon
FROM read_om('build/s3-samples/data_spatial/ncep_gfswave025/2026/09/28/0000Z/2026-10-02T0300.om',
             domain := 'ncep_gfswave025')
WHERE lat BETWEEN 30 AND 40 AND lon BETWEEN 110 AND 120;
```

`data_run` 示例使用文件自带的 `coordinates = 'lat lon time'`；`data/` 旧式时序文件若无此 metadata，需为每个值变量声明 `dimensions`。本地 OM v3、Float32 和受支持压缩格式的限制仍然适用；注册 domain 不保证任意同 prefix 文件可读。最初 `ncep_gfswave025` 全量值对照见 [domain manifest](../../../test/data/domain-manifest.json) 和 [domain evidence](../evidence/domain.md)；本次扩展的样本元数据记录见 [规则网格 domain](../../../docs/regular-domains.md)。
