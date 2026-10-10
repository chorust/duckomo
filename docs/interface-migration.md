# 读取接口易用性修订与迁移

本页描述当前开发版的三项接口变更：地理坐标列统一为 `lat/lon`、`dimensions` 共享轴简写，以及脱敏的远程失败分类。它修订既有 001–004 SQL 契约中的相应条款；不改变文件支持子集、网格数学、公开桶授权行为或既有验收等级。

## 1. 地理坐标输出：`lat` / `lon`

绑定 `grid` 或 `domain` 后，值列后追加非 NULL 的 `lat DOUBLE`、`lon DOUBLE`。这两个名字固定表示地理纬度、经度；投影/旋转/Gaussian 网格也使用相同输出列名，不照搬原生轴名。

这是**破坏性输出模式变更**：

- 将既有 SQL 的输出列引用 `latitude` / `longitude` 改为 `lat` / `lon`，包括 `WHERE`、`ORDER BY`、视图和客户端字段访问。
- 不默认输出旧别名，不增加命名开关。需要保留下游旧字段名时可显式写 `SELECT lat AS latitude, lon AS longitude ...`。
- 空间绑定禁止源值列占用 `lat/lon`，冲突按 DuckDB 标识符规则检测，包含 `LAT`、`LoN` 等大小写变体。没有空间配置时，源列不因这项规则被重命名或拒绝。
- 原有源值列 `Latitude/Longitude` 不再与生成的空间列冲突；不是把源数组重命名成坐标。
- 原生轴名、投影参数（例如 `latitude_of_origin`）、Gaussian `rows.latitude`、内部类型和网格/provenance JSON 不改名。`valid_time`、`run`、`lead_time` 的时间语义不变。

CF 的标准语义名与变量名是不同层次；短列名不妨碍将其解释为 `latitude/longitude`，但不据此声称整个 SQL 接口符合 CF。

## 2. `dimensions`：共享列表或精确 MAP

支持两种原生 DuckDB 值，不接受 JSON 字符串或 STRUCT 代替：

```sql
-- 对每个值变量声明同一份完整有序轴列表，包括单数组和层级多变量。
dimensions := ['lat', 'lon']

-- 现有逐变量 MAP 仍可使用；键可以是 SQL 列名或内部绝对路径。
dimensions := map(['humidity', 'temperature'], [['lat', 'lon'], ['lat', 'lon']])
```

共享列表只是减少重复 SQL，**不是自动猜轴、广播、转置或放宽校验**：

- 每个变量均须有相同 shape、row count 和有序轴身份；轴数量必须逐变量匹配 rank。
- 列表必须是 `VARCHAR[]`，每个名字非空、非 NULL、唯一。数值列表、空列表、重复轴、NULL 元素、错误 rank 均拒绝。
- 声明不能覆盖任意变量已有的不同 `coordinates` 元数据。同 shape 本身仍不能证明轴身份；调用者对显式声明负责。
- MAP 仍须精确覆盖全部值变量，不接受只为投影列声明轴；两种形式最终执行相同的对齐校验。
- 省略或整体 `NULL` 沿用既有规则：单数组可只读数值，多变量需要一致的文件轴元数据；完整元数据存在时无需重复声明。
- `om_grid_info` 使用相同的两种声明形式；仍要求绑定 grid/domain，不是任意文件的 metadata inspector。

### 最小公共桶查询

使用官方 HTTPFS；无自定义 S3 配置时，不需要 AWS 密钥或预先创建 secret。

```sql
LOAD duckomo;
INSTALL httpfs;
LOAD httpfs; -- 已安装且允许自动加载时可省略这一句

SELECT value AS elevation, lat, lon
FROM read_om('s3://openmeteo/data/ncep_gfs025/static/HSURF.om',
  domain := 'ncep_gfs025',
  dimensions := ['lat', 'lon'])
WHERE lat BETWEEN 30 AND 30.5
  AND lon BETWEEN 110 AND 110.5
ORDER BY lat, lon;
```

`dimensions` 只描述数组轴，不提供实际坐标值。只读值可省略空间配置；显式 `grid` 仍须提供 `spatial_axes`。规则 domain 要求 `lat/lon` 身份，新增网格按各自登记的空间轴 profile 校验，不从路径或 shape 自动选择网格。

## 3. 远程诊断：类别、行动建议与脱敏

远程打开和范围读取共用一个分类器，只使用 DuckDB 公开异常类型和 HTTPException 的结构化 `status_code`。错误保留打开/index/data 阶段的原有 reader code，不改变失败读取计数、QueryEnd、取消或恢复语义。

| 可见类别 | 依据 | 建议 |
|---|---|---|
| `extension_missing` | 缺少扩展的异常类型 | 安装/加载匹配版本与平台的官方 HTTPFS，或允许自动加载 |
| `extension_load` | 自动加载失败的异常类型 | 检查扩展安装、版本/平台与扩展下载访问 |
| `configuration` | invalid-input / invalid-configuration / settings 类型 | 检查代理 URL 的 host/port（不要带尾部路径）、endpoint、region 及显式 HTTP/S3 配置 |
| `access_denied` | permission 类型，或结构化 HTTP 401/403 | 检查外部访问限制、适用 secret 与对象/桶/端点权限 |
| `object_not_found` | 结构化 HTTP 404 | 检查准确对象键，滚动预测对象可能已过期 |
| `timeout` | 结构化 HTTP 408/504 | 检查网络、代理和超时/重试配置 |
| `object_changed` | 结构化 HTTP 412 | 使用稳定对象/版本后重试 |
| `range_request` | 结构化 HTTP 416 | 检查对象稳定性与服务器 Range 支持 |
| `rate_limited` | 结构化 HTTP 429 | 降低请求并发，等待服务限流解除 |
| `http` | 其他有效的结构化 HTTP 状态 | 检查状态、端点和服务可用性 |
| `transport` | 没有可信的结构化状态/配置类型 | 检查网络、代理、TLS、超时；明确说明状态未知，不猜测 403/404 |

例如代理格式错误现在提示 `[configuration]`，而不是让用户反复安装已加载的 HTTPFS。部分官方 HTTPFS 的 HEAD 拒绝后重试路径会丢失状态；此时保留 `transport`，不通过异常文本猜原因，也不使用私有 HTTPFS ABI。

### 安全与边界

- 不转发底层异常正文、response body、reason 或 headers；只允许输出严格校验的三位 HTTP 状态、固定类别和固定建议。
- 扩展生成的路径继续删除 userinfo，并将整个 query 替换成 `?<redacted>`；不输出代理凭据、Authorization、session token 或 secret 值。
- **DuckDB/客户端仍可能渲染原 SQL 的错误位置或记录查询文本。** 不把带凭据 URI 写进共享 SQL；使用绑定参数或会话变量，使出错语句引用变量而非凭据字面量。扩展不能承诺脱敏引擎/客户端自行记录的全部 SQL 文本。
- 不创建/覆盖 secret、不修改全局访问配置；显式配置错误不静默改走匿名、HTTPS 或其他凭据。
- 取消仍是取消，不包装成网络错误；失败后同连接可继续查询。

## 验证与历史记录

实现及回归入口：`test/sql/interface_usability.test`、既有空间/多网格/语义轴 SQL、`schema_test`、`remote_session_test`、`official_httpfs_test`。后者加载实际官方 HTTPFS，检查 403/404、代理配置、扩展缺失、脱敏、失败恢复、取消和连接隔离。

本轮验证结果另见 [实施验证记录](issues/read-interface-usability-implementation.md)；先前的 [排查记录](issues/read-interface-usability.md) 和 JSON 保留为改动前证据，不批量重写历史结果。
