# 读取接口易用性：U1 / U3 / U4 实施验证

## 完成范围

按用户确认，本次完成两个高优先级改动和维度声明简化：

1. **U1：地理坐标列统一为 `lat/lon`**，不默认附加 `latitude/longitude` 别名。
2. **U3：远程错误诊断重构**，打开和范围读取共用脱敏分类器，提供固定的类别/行动建议。
3. **U4：`dimensions := ['lat','lon']` 共享轴简写**，保留现有逐变量 MAP，并共用完整对齐校验。

使用与迁移见 [接口修订](../interface-migration.md)。初次排查的 JSON 保留为改动前记录；当前验证见 [本次证据 JSON](read-interface-usability-implementation.evidence.json)。本次不新增文件/domain 检视函数，不改变有效时间命名，不自动创建或覆盖 secret。

## 实现与重构

| 项目 | 实现位置 | 保持的约束 |
|---|---|---|
| 地理输出及名称冲突 | `src/scan/schema.cpp` | 非 NULL DOUBLE，值列顺序不变，冲突检查包含大小写；无网格时不生成坐标 |
| 共享轴归一化 | `src/scan/dimensions.cpp` | LIST 与 MAP 共同调用 `ValidateAlignment()`，逐变量检查 rank、shape、有序轴和元数据；不转置/广播/猜轴 |
| 两个 SQL 入口接收声明 | `src/scan/read_om.cpp` | `read_om` 和 `om_grid_info` 均接受 LIST/MAP，经同一 binder 校验；不能用 ANY 绕过类型检查 |
| 安全远程分类 | `src/om/remote_file.cpp` | 只用公开 `ErrorData` 类型与严格校验的 HTTP status；不转发底层文本、headers、body 或 reason，不使用私有 HTTPFS ABI |
| 迁移验证工具 | `scripts/`、`test/tools/`、生成的 `sample-queries.sql` | SQL 引用改为短名；已有参考 CSV 的长字段名通过显式别名保留，数学参数/JSON/历史 evidence 不改名 |

本改动是当前开发版的输出模式变更：旧 SQL/客户端访问需改列名，或在自己的投影中显式写 `lat AS latitude`、`lon AS longitude`。README 中英文、当前接口说明、架构说明、001–004 SQL 契约和历史 quickstart 的迁移提示均已同步；不批量改写历史验收结果。

## 当前源码构建与验证

平台：**Linux AArch64**。三个版本都重新构建当前工作区源码，不复用旧 DuckOMO 扩展二进制。为避免全量重新编译无改动的引擎，复用已有的各版本 CMake build directory，并用 `version_matrix.py resolve` 的当前内容 hash 更新 `DUCKOMO_BUILD_ID`；原历史 build manifest 不提升状态。本次 JSON 单独记录实际命令、build ID 和新产物 SHA256，并在测试后再次确认输入 hash 未变。

| DuckDB / 官方 HTTPFS | 本地 SQL | 本地 native | Python 工具套件 | 实际官方 HTTPFS native | 公共 S3 / HTTPS |
|---|---|---|---|---|---|
| v1.5.4 | 17 文件 / 937 assertions，通过 | 28 项，通过 | 8 套，通过 | 通过 | 通过 |
| v1.5.5 | 17 文件 / 937 assertions，通过 | 28 项，通过 | 8 套，通过 | 通过 | 通过 |
| v1.5.6 | 17 文件 / 937 assertions，通过 | 28 项，通过 | 8 套，通过 | 通过 | 通过 |

### 覆盖的关键条件

- 新增 `test/sql/interface_usability.test`：输出 schema、LIST/MAP 双向差集、根路径/层级路径、显式旧别名、无空间配置不生成坐标、错误 rank/shape/type、重复/空/NULL 轴、元数据冲突、整体 NULL 与 grid-info 一致性。
- `schema_test`：后续变量的元数据与 rank 也必须校验；同 row count 不代表同 shape；`lat/LAT/lon/LoN` 的空间输出冲突，以及长源名不再冲突。
- 原有规则/投影/旋转/Gaussian、空间选择、语义轴、列裁剪、坐标-only/count/空选择、生命周期及 alias 回归均通过。
- `remote_session_test`：19 种异常输入分别覆盖打开、index read、data read；恶意 status 字符串和正文中的伪 403/404 不被当作状态；headers/body/reason/query/userinfo 中的测试标记不进入扩展错误；失败不增加成功字节，随后同 session 读取恢复；取消保持取消。
- `official_httpfs_test` 动态加载匹配的官方 HTTPFS：缺扩展、代理格式错误、404、GET-403、HEAD-403 丢失状态的诚实回退、QueryEnd、同连接恢复、取消和连接隔离均通过。
- 三个官方 CLI 加载新产物，在隔离 HOME、无已存 secret / AWS-S3 环境配置的情况下直接读取公共 `HSURF.om`。原始区域返回相同的 9 行；共享 LIST、MAP 和 HTTPS 的双向差集为 0。之后创建的用户桶级 secret 仍被选中，secret 总数仅为用户创建的 1 条。
- `git diff --check`、修改文档的本地链接及 JSON/产物 hash 一致性检查通过。

### 安全诊断边界

部分官方 HTTPFS 的 HEAD 拒绝后范围重试会丢失 HTTP 状态，当前按 `[transport]` 明确说明无结构化状态；不猜测 403/404。GET-403 和 404 有结构化状态时分别报告 `[access_denied]`、`[object_not_found]`。

引擎/客户端可能渲染原 SQL 文本，因此敏感 URI 在集成测试中通过会话变量传入，而不是把凭据写进出错 SQL 的字面量。扩展保证自己构造的错误脱敏，不承诺脱敏调用方或引擎自行记录的全部 SQL。公共桶测试只在子进程中规范化既有代理 URL 尾斜杠，不改变用户全局配置。

## 复现入口

标准构建与完整本地回归（每个版本重复）：

```sh
scripts/build-version.sh v1.5.4
scripts/validate.sh build/official-matrix/v1.5.4/release --local-only
```

受控远程服务按 [官方 HTTPFS 指南](../official-httpfs.md) 准备后，执行扩展后的实际 HTTPFS native 测试：

```sh
DUCKOMO_HTTPFS=/path/to/official/httpfs.duckdb_extension \
DUCKOMO_HTTP_BASE=http://127.0.0.1:PORT \
  build/official-matrix/v1.5.4/release/test/native/official_httpfs_test
```

直接使用 [接口迁移页](../interface-migration.md#最小公共桶查询) 的公共查询，即可验证短列名与共享轴声明。详细构建输入、SQL、完整 stdout、回归日志与 loopback 服务审计保存在忽略的 `build/interface-usability/`；可复核的关键结果与产物 hash 已保存到本次证据 JSON。

## 不扩大的验收声明

本次不是 004 全部科学/生产网格门禁，也不是独立验证者签字。`domain_reference_test` 依赖固定完整 domain 样本和独立参考，在 local-only 流程中保留 not-run；对应的实际公共 GFS 区域查询已在三版本验证，但不能冒充其他 domain 的全量独立核对。本次未运行 sanitizer，也不新增 x86_64/macOS/其他平台的验证声明。既有 Gaussian/N-grid、完整 memory ledger、逐网格远程收益和独立复现缺口不因接口改进被关闭。
