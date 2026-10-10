# 读取接口易用性：命名决策与待办

## 状态与范围

本记录保留读取 Open-Meteo OM 数据时的初次排查及设计方向。**U1（lat/lon）、U3（远程诊断）、U4（共享轴列表）已在后续实施**，见 [接口迁移](../interface-migration.md) 与 [实施验证](read-interface-usability-implementation.md)。下文排查 JSON 和初次验证条目是改动前证据，不代表当前运行时。当前接口以 [接口说明](../spec.md) 为准；既有网格验收等级不变。

## 已确认方向：地理坐标列统一为 `lat` / `lon`

- 将 SQL 地理坐标输出列从 `latitude` / `longitude` 统一为 `lat` / `lon`，与 Open-Meteo 常见 `coordinates = "lat lon"` 命名一致，减少用户需要记忆的两套名称。
- 固定输出真实地理纬度、经度；不是数组索引，也不是让输出名动态跟随原生轴。投影、旋转和 Gaussian 网格仍输出相同地理坐标列。
- 原生轴身份保留来源含义，可能是 `lat/lon`、`y/x` 或点轴；`dimensions` 声明轴身份与顺序，网格定义负责映射，两者不混同。
- CF 的变量名与 `standard_name` 是两个层次。CF 官方示例使用 `lat` / `lon` 变量名和 `latitude` / `longitude` 标准语义名，因此短列名与 CF 语义并不冲突。仅改列名不能声称整个 SQL 接口符合 CF。
- 不为命名引入额外配置开关或默认同时输出两套同义列。内部 `Latitude/Longitude` 类型和字段、投影参数、既有 provenance JSON 不做机械替换。
- 实施时明确破坏性变更与迁移方式，同步名称冲突检查、绑定/过滤/列裁剪回归、README 中英文、当前接口说明及相应 SQL 契约；历史 evidence 不批量改写。

证据：

- [`src/scan/schema.cpp`](../../src/scan/schema.cpp)：排查时 `AppendSpatialOutputColumns()` 固定追加 `latitude` / `longitude`；后续已改为 `lat/lon` 并同步检查短名称冲突。
- [`src/scan/read_om.cpp`](../../src/scan/read_om.cpp)：规则 domain 按 `lat` / `lon` 查找空间轴；扫描按输出角色计算地理坐标。
- [`regular-domains.csv`](../../specs/002-spatial-pushdown/evidence/regular-domains.csv)：真实 Open-Meteo 样本的 `coordinates` 常为 `lat lon` 或 `lat lon time`。
- [CF 1.7 §5.6，Example 5.7](https://cfconventions.org/Data/cf-conventions/cf-conventions-1.7/build/ch05s06.html)：`lat:standard_name = "latitude"`、`lon:standard_name = "longitude"`。

## 用户提出的重点：公共桶免手动 secret

目标：访问 `s3://openmeteo/` 时，用户无需先创建匿名 secret；显式匹配的用户 secret / 访问配置优先。

**排查结论：无需内置 secret 就已能实现普通环境下的免配置匿名读取。** 官方 HTTPFS 会在没有密钥时匿名请求；本轮在隔离 HOME、无 AWS/S3 配置环境变量、无已存 secret 的进程中，用官方 v1.5.4 / v1.5.5 / v1.5.6 CLI + HTTPFS 和既有官方迁移版 DuckOMO 产物验证了该行为。三个版本均得到 `secret_count=0`、`COUNT(*)=1038240`，并成功读取实际值。

v1.5.5 对最初 SQL 中的区域进行完整排序查询：不建 secret 与显式创建桶级匿名 secret 均返回相同的 9 行坐标/地形值。无 `ORDER BY` 的 `LIMIT 1` 只用于确认值解码成功，不作为跨查询值一致性证据。

因此，优先删除使用指南中“不建 secret 就不能开始”的暗示，而不是在 `LOAD duckomo` 或扫描期间自动执行 `CREATE SECRET`。README 中英文已将 secret 移至可选配置；运行时没有新增 secret 或修改全局 S3 设置。

### 为什么不能简单“内置一条，用户创建就覆盖”

DuckDB Secret Manager 按 scope 匹配，不按创建先后选择；同存储、相同 scope 的候选还按名称比较。v1.5.5 的无网络实验：

| 预先存在的桶级匿名项 | 后创建的用户项 | 实际被选中的项 |
|---|---|---|
| `openmeteo_public`，`s3://openmeteo/` | `user_default`，`s3://` | `openmeteo_public` |
| `openmeteo_public`，`s3://openmeteo/` | `z_user`，`s3://openmeteo/` | `openmeteo_public` |
| `openmeteo_public`，`s3://openmeteo/` | `z_user`，`s3://openmeteo/data/` | `z_user` |

自动注册桶级 secret 可能压过用户已有的通用 S3 secret，也不能保证后创建的同 scope 用户项获胜。相关实现见 [`secret_storage.cpp`](../../duckdb/src/main/secret/secret_storage.cpp) 的 `SelectBestMatch()` 与 [`secret.cpp`](../../duckdb/src/main/secret/secret.cpp) 的 `KeyValueSecretReader`。

后续若确实需要公共桶专用默认值，应采用**仅在不存在适用用户访问配置时生效的局部默认配置**，而不是往用户 Secret Manager 注入普通优先级项；方案需先确认官方 HTTPFS 的公开接口与三版本兼容性。约束：

- 只精确匹配 `s3://openmeteo/`，不影响其他桶、私有 endpoint 或用户授权范围。
- 用户适用的 secret、显式 endpoint/region/访问设置优先；不能通过替换 URI 绕过这些配置或外部访问限制。
- 显式配置失败时不静默改走匿名请求、HTTPS 或别的凭据。
- 不在扩展加载时创建持久 secret，不在扫描期间执行全局 `SET`。
- 验证同 scope/宽 scope/窄 scope、多个连接、显式配置错误和私有桶不受影响；不能仅测试同名 `CREATE OR REPLACE`。

目前没有证据证明必须增加该 fallback；公共数据的首次使用已经可以免 secret。自定义访问配置下不能承诺“无条件匿名成功”。

## 其他发现与优先级

后续已批准并实施 U1/U3/U4；U2/U7/U8 是现有行为的文档修正。U5/U6/U9 仍是待评估建议，本次不新增文件/domain 检视函数或改变时间命名。

| 编号 / 优先级 | 用户疑惑或额外操作 | 证据 / 现状 | 建议与状态 |
|---|---|---|---|
| U1 / P1 | 轴名 `lat/lon`，输出却是 `latitude/longitude` | `AppendSpatialOutputColumns()` 和规则 domain binder 使用两套名称 | **已实施**；SQL 地理列统一为 `lat/lon`，不改原生轴语义 |
| U2 / P1 | 公共桶示例要求手动 secret，容易被误认为需要 AWS 凭据 | 三版本无 secret 匿名读取通过；内置项会改变用户 secret 优先级 | **文档已修正**；保留无 secret 默认路径，桶级 region 配置可选，不注入内置 secret |
| U3 / P1 | 各类远程错误均提示“安装/加载 HTTPFS、检查访问配置” | `RemoteReadFile` 捕获异常后抹去类别；本轮代理格式错误和不存在对象都变成同一提示 | **已实施**公开异常类型/HTTP status 分类、固定建议与脱敏；未知状态明确回退，不误报扩展缺失 |
| U4 / P2 | 单数组仍要写 `map(['value'], [['lat','lon']])`；共享轴的多变量需重复列表 | `dimensions` 只注册为 MAP，`ValidateAxisDeclarations()` 要求完整覆盖；UKMO 样本有 218 个缺轴元数据变量 | **已实施** `dimensions := ['lat','lon']` 共享轴简写；仍逐变量校验 rank、shape、有序轴和已有元数据冲突，保留 MAP 精确声明 |
| U5 / P2 | 想先知道文件有什么变量、shape、轴，却先被要求知道轴 | `DESCRIBE read_om(multi.om)` 可因缺轴声明失败；`om_grid_info` 复用完整 binder，且要求已绑定 grid/domain | **建议**提供不依赖 grid/domain 和多变量对齐的 metadata-only 检视入口，例如 `om_file_info(path)`；按变量返回路径、SQL 名、类型、shape、chunks、轴/时间证据及缺失项，不猜测、不解码值 |
| U6 / P2 | domain 名只能查文档，拼写错误只报 unknown/not verified | `FindVerifiedDomain()` 精确匹配；当前 SQL 注册没有 registry 列表函数 | **建议**可查询的登记表（如 `om_domains()`）及少量拼写候选；列明 grid 类型、空间轴 profile 和证据等级，建议不自动选择，不把登记等同于已验收 |
| U7 / P2 | 用户被要求始终手动 `LOAD httpfs`，但标准文件系统已支持自动加载 | 三版本均在 HTTPFS 已安装但未加载、自动安装关闭时完成首次 S3 读取，`loaded` 从 false 变 true | **文档已修正**；解释自动加载条件与离线/禁用时的显式准备步骤，不额外创建自动安装机制 |
| U8 / P2 | 接口总说明称“可选参数 NULL 等同省略”，实际 `include_source := NULL` 报错；domain 说明又笼统要求 `lat/lon` | `BindReadOm()` 明确拒绝 NULL source；新增网格按 `expected_axis_order` 绑定 | **文档已修正**；列出 NULL 例外，区分规则 domain 与新增网格的轴 profile |
| U9 / P3 | `dimensions`、`spatial_axes`、`axes.time`、`valid_times`、`valid_time` 名称相近，易误认为都是重命名参数 | `BindSpatialConfiguration()`、`BindSemanticAxes()`、`BindTemporalConfiguration()` 分工不同 | **建议**在教程增加下表式选参说明，不机械把 `valid_time` 改成 `time`，不增加同义参数 |

### 选参语义应明确展示

| 用户意图 | 当前接口 | 不负责什么 |
|---|---|---|
| 声明数组每一维的身份与顺序 | `dimensions`；有完整元数据时省略 | 不提供坐标值，不自动转置 |
| 显式选择来源网格 | `domain` 或 `grid` | 不从路径/shape 猜测；不补缺失时间 |
| 把显式网格的空间角色映射到原生轴 | `spatial_axes`，只与 `grid` 使用 | 不是 SQL 地理输出列的重命名开关 |
| 映射时间、层次、成员等语义坐标 | `axes` | 不是另一套 `dimensions`；不能与冲突的文件证据并存 |
| 补充缺失的有效时间 | `valid_times`（既有接口）或 `axes.time` | 两者不可同时声明；不从文件名推断 |
| 查询有效时间 | 输出列 `valid_time`，当前 UTC `TIMESTAMP` | 不等同于 `run` 起报时间或 `lead_time` 预报时效；声明了一个名为 `time` 的维度不代表已有时间坐标 |

`time` → `valid_time` 与 `lat` → `latitude` 不应机械类比：前者显式表达气象有效时间、区别于起报时间和预报时效。是否改变时间接口需独立讨论，本轮没有确认改名。

### U3 的具体诊断改进边界

本轮原始环境的代理 URL 带尾斜杠，官方 HTTPFS 明确报代理端口解析失败；DuckOMO 只报通用远程对象打开失败。仅在探测子进程中规范化代理 URL 后，同一对象立即可读。该问题不应被解释为“公共桶需要 secret”。

建议在可获得稳定结构化信息时保留 `extension_missing`、`access_denied`、`object_not_found`、`proxy_configuration`、`timeout`、`tls` 等类别，并给出对应下一步。不能为了易读而原样输出异常文本：URL query、userinfo、Authorization、session token、secret 值、带认证信息的 proxy 均须继续脱敏。若官方公开接口不给出可靠状态，保留通用 transport 类别并提供安全诊断路径，不靠脆弱字符串猜测 403/404，也不引入私有 HTTPFS ABI。

## 初次排查证据与验证边界（改动前）

汇总见 [可复核证据 JSON](read-interface-usability.evidence.json)。本轮使用已有官方 HTTPFS 迁移产物，**没有重建或修改 C++**；二进制 SHA256 与来源路径记录在 JSON 中，不能将其宣称为新的全量实现验收。

- 三版本公共对象的无 secret 元数据与实际值读取通过。
- v1.5.5 无 secret / 桶级匿名 secret 的区域完整有序结果一致（9 行）。
- 三版本已安装 HTTPFS 的按需自动加载通过；探测关闭自动安装，因此没有验证联网自动下载。
- v1.5.5 三种 secret scope 优先级的结果符合上表；仅创建无密钥临时测试项。
- v1.5.5 的 LIST dimensions、NULL source、未知 domain、缺轴多变量、无 grid 的 grid-info 均复现预期拒绝，作为现状证据，不是本轮引入的测试失败。
- 本轮修正文档，不将 U1/U3/U4/U5/U6/U9 标记为实现完成；不修改原有 specs 的历史 evidence 或 roadmap 验收状态。
- 文档本地链接、JSON 中 18 项观察的预期退出码、三版本产物 SHA256、区域结果一致性、自动加载状态和 secret 优先级检查通过；`git diff --check` 通过。
- `python3 test/tools/release_tools_test.py` 的 10 项测试通过。本轮未运行完整 SQL/native 或逐网格验收。

## 后续实施验收清单

- [x] U1：明确改名迁移政策，同步输出冲突检查、直接谓词/别名谓词、列裁剪、坐标-only/count、全部网格与 SQL 契约。
- [x] U2：将公共桶 secret 从入门前置步骤改为可选，并说明用户配置遵循官方匹配规则。
- [x] U3：脱敏且可行动的错误类别；覆盖已加载扩展的代理错误、无权限/不存在对象、取消和恢复，不能泄露敏感输入。
- [x] U4：实现共享轴简写，保持已有 MAP 与所有语义校验，不把同 shape 当作来源轴身份证明。
- [ ] U5/U6：评估独立 metadata / domain 检视接口，覆盖无空间配置、缺轴/多变量、无值解码和证据等级展示。
- [x] U7/U8：修正自动加载、NULL 例外和规则/新增网格 domain 的文档说明。
- [ ] U9：将选参说明整理进面向用户的完整教程，保留时间语义区别。
