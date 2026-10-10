# SQL Interface Contract

> 当前开发版修订：SQL 地理列统一为 `lat/lon`；`dimensions` 支持共享 `VARCHAR[]` 或完整逐变量 MAP；远程错误输出脱敏类别与建议。迁移及完整规则见 [读取接口修订](../../../docs/interface-migration.md)。历史 evidence 仍对应原构建，不因本修订提升验收状态。

状态：实现已接入源码和配套构建；G3–G7 的外部验收及独立复现尚未全部完成。保留既有本地、空间及 `valid_time` 接口。

## 读取入口

```text
read_om(path, dimensions := NULL, grid := NULL,
        spatial_axes := NULL, domain := NULL,
        valid_times := NULL, axes := NULL)
```

path 为非 NULL 常量 VARCHAR，支持本地普通文件、http://、https://、s3:// 单对象；不接受目录、glob、列表或自动下载集合。已有五个空间/轴参数语义不变；valid_times 仍为非空、有限、无 NULL 的 TIMESTAMP 列表。read_om_raw 继续保持原始本地入口，不扩展远程或新坐标。

所有参数绑定时确定。axes 整体 NULL 或空声明等同省略；给定的语义项必须是非 NULL STRUCT。字段名和枚举值精确区分大小写，未知项、重复项及不允许的字段报错。

## axes 声明

顶层只允许 time、level、lead_time、member、run。每项包含 `axis VARCHAR`，取已验证的完整轴名；另选择以下一种形式：

- `values := [...]`：显式有类型坐标序列，长度与对应轴完全相同。
- `start := ... , step := ...`：规则坐标，长度来自轴；第 i 项=start+i×step，检查所有边界与溢出，不添加 count 参数。

不得混合 values 与 start/step，不得映射到已配置空间轴；不存在的轴、一轴多语义、多变量轴/坐标冲突在返回数据前失败。重复值、零步长、递减、不等间距和非单调序列均保留各自逻辑位置。空向量、NULL、无穷或不可表示的值拒绝。

| 语义键 | 输入 | 输出列及类型 | 附加字段与约定 |
| --- | --- | --- | --- |
| time | TIMESTAMP[] 或 TIMESTAMPTZ[]；regular start 同类型、step INTERVAL | valid_time TIMESTAMP | 微秒精度、归一 UTC；无时区 TIMESTAMP 按 UTC；不另输出 time |
| run | 同 time | run TIMESTAMP | 表示起报时刻，非有效时刻 |
| lead_time | INTERVAL[]；regular start/step INTERVAL | lead_time INTERVAL | 月分量必须为零；天按 24 小时转换；规范输出 months=days=0、微秒为有符号 int64 |
| level | 数值列表或数值 start/step | level DOUBLE | 必须有 kind 和 unit，见下表；转换为 DOUBLE 不得额外有损 |
| member | 整数列表或 VARCHAR[]；regular 仅整数 start/step | member BIGINT 或 VARCHAR | 整数受检转 int64；文本不转数字、不重编号，区分大小写 |

时间输入已经由 SQL 求值，若调用者先将更高精度转成 TIMESTAMP，扩展无法恢复此前丢失的精度。扩展本身不隐式接收字符串或 TIMESTAMP_NS 并截断；值必须为有限微秒时间。带偏移的字符串须先显式转 TIMESTAMPTZ，例如 `TIMESTAMPTZ '2026-09-30 08:00:00+08'`，输出为 UTC 的 `2026-09-30 00:00:00`。不支持月、年等日历步长；非 Gregorian 日历不被自动解释。

| level.kind | 允许 unit | 取值约束 |
| --- | --- | --- |
| pressure | Pa / hPa | 大于零，不自动换算 |
| height | m | 有限，可为负 |
| model | 1 | 可精确表示的整数层号 |

数值列表允许常规整数、FLOAT/DOUBLE 和 DECIMAL；绑定检查到 DOUBLE 的 round-trip，无法无损表达的整数/DECIMAL 拒绝。比较使用输出 DOUBLE 本身，不使用额外容差。文本 member 使用二进制相等语义；带不同 collation 的过滤保留原 SQL 并回退候选缩小。

### 时间兼容与证据

当前已识别文件格式仅为附着于值数组并按既有继承规则找到的 `time` 一维 Int64 UTC Unix 秒数组，以及 `valid_time` Int64 UTC Unix 秒标量。数组须匹配轴名 time；标量沿用现有快照规则。本期不新增 level/run/member/lead_time 文件格式猜测。

`axes.time` 可显式绑定任意已声明的非空间时间轴；如存在文件时间证据，必须与其实际轴及坐标逐位置一致。不能同时传 valid_times 和 axes.time（即使相等也报重复声明）。未传 axes.time 时保留自动时间和 valid_times 路径。无时间轴的一时刻快照继续通过既有标量或 valid_times 单元素路径表达，axes 不增加任意标量广播。

输出按值列 → lat/lon（若有）→ valid_time（若有）→ level → lead_time → member → run 排列，仅追加启用项。任何新增列与原值列按 DuckDB 名称比较规则冲突则拒绝。

## 示例

```sql
SELECT value, valid_time, member
FROM read_om('test/data/raw.om',
  dimensions := map(['value'], [['t','ensemble']]),
  axes := {
    'time': {'axis':'t', 'start':TIMESTAMP '2026-09-30 00:00:00',
             'step':INTERVAL '1 hour'},
    'member': {'axis':'ensemble', 'values':[10,20,30]}
  })
WHERE valid_time >= TIMESTAMP '2026-09-30 01:00:00' AND member=20;
```

raw.om 的 shape=[2,3]、值为 0–5，结果为 `(4, 2026-09-30 01:00:00, 20)`。语义配置不依赖地理网格。

## 过滤与列裁剪

time/run/lead_time/level 的 `= < <= > >= BETWEEN` 和 member 的 `=`，在类型精确相容时可生成必要条件；常量在左侧的比较可以反转。AND 中独立安全条件取交集；OR/NOT、函数、类型转换、不支持 collation、含月分量 INTERVAL、不可证明安全的表达式回退。保留 DuckDB 完整 WHERE 和输出/过滤所有依赖。

时间 WHERE 的 TIMESTAMP 按 UTC 解释；含隐式 TIMESTAMP↔TIMESTAMPTZ 转换的比较不强行提取。支持负时长。重复坐标不去重。无 ORDER BY 不承诺串行或并行的行序。

可证明空结果、仅坐标和无值依赖计数不读取值 index/data 或解码值块；绑定期间的 time 坐标数组读取单独计入 coordinate，不伪装成零总 I/O。

## 会话控制

以下为新增连接级设置；通过设置回调保存到当前 ClientContextState，禁止变成所有连接共享的可变开关。

| 设置 | 类型 / 默认值 | 规则 |
| --- | --- | --- |
| duckomo_max_threads | BIGINT / 0 | 0 使用 DuckDB threads；正数为上限，负数拒绝；实际受任务数与 DuckDB 调度限制 |
| duckomo_cache_enabled | BOOLEAN / true | false 时不查不写本扩展缓存；关闭即清空已有条目 |
| duckomo_cache_capacity | BIGINT / 67108864 | 字节数，非负；0 等同禁用存储；缩小时立即淘汰到上限 |

```sql
SET threads=4;
SET duckomo_max_threads=2;
SET duckomo_cache_capacity=67108864;
SET duckomo_cache_enabled=true;
CALL duckomo_clear_cache();
SELECT * FROM duckomo_last_scan_metrics();
```

clear_cache 返回一行 `cleared_entries UBIGINT, cleared_bytes UBIGINT`；仅清理本会话缓存。last_scan_metrics 返回最近一次已结束、含 read_om 的 SQL query 的每个扫描一行：`query_id VARCHAR, scan_id UBIGINT, metrics VARCHAR`。metrics 为 v3 JSON 文本，不依赖 JSON 扩展；首次扫描前为零行。读取或清理操作不覆盖最近扫描记录；失败和取消记录仍可读取。metrics 的 SQL/URI 始终脱敏；嵌套 `legacy_v2` 保留 v2 字段的原统计含义。

v3 JSON 包含 `scan_id`、`status`、`scan_complete`、axes/fallback、bind/scan metadata、coordinate、逐值变量逻辑/底层 index/data/decode、总逻辑/底层读取、transport response body/attempt/status/completeness、cache、task/worker、candidate/scanner/result rows、decode completeness、elapsed 和 memory scope。coordinate 单独包含 logical/physical index/data 请求与 `decoded_chunks`；`variables` 只列值数组。逻辑读取包含缓存命中；`physical_read_*` 只计缓存以下成功的 OM 读取；`response_body_bytes` 只来自 httpfs observer，未加载远程 observer 的本地结果为已知 0。旧版 `bytes_fetched` / `read_requests` 留在 `legacy_v2` 并维持 v2 含义。

`scan_complete=false` 与 `status=success` 可同时出现，表示 SQL 成功但下游消费者（例如 LIMIT）提前停止扫描。`peak_rss_bytes` 使用进程 scope。`peak_query_owned_bytes` 汇总 DuckOMO 元数据 payload、decoder 状态及其参数/index/data/scratch vector capacity、坐标向量、选择游标和活跃批次 position/segment capacity；`query_memory_scope` 为 `duckomo_owned_buffer_decoder_selection_capacities`，不含共享会话缓存、DuckDB 输出 vector、httpfs/引擎内部内存及 allocator bookkeeping。正常结束且计数完整时提供峰值；失败、取消或计数不完整时为 JSON `null`、`query_memory_count_complete=false`，表示未知而不是零。进程 RSS 和查询归属内存是两个独立字段。

## 错误

参数、坐标、列名或布局问题为绑定错误；远程能力缺失、访问拒绝、范围/版本不符、短读、超时和解码失败使 SQL 查询失败，取消保留 cancelled 状态。流式消费者可能已见部分批次，但没有成功完成的残缺结果；不将已见批次作为成功数据集。未知能力不得静默完整下载。
