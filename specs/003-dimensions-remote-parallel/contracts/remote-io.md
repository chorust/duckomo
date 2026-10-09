> 2026-10-08：当前未发布版本使用 [官方 HTTPFS 迁移契约](../../004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md)。下文专用 ABI、LRU 或 observer 的条款/命令为历史约定，已被本次契约替换。历史证据状态保持，当前复现请见 [官方流程](../../../docs/official-httpfs.md)。

# Remote I/O and HTTPFS Integration Contract

状态：实现已接入源码和配套构建，适用于 FR-010–012、FR-016–018。上游固定为 [httpfs c3f215ab](https://github.com/duckdb/duckdb-httpfs/tree/c3f215ab360f04dc3d3d5305fa81849c0121f111)，配套 DuckDB v1.5.4。真实 HTTP/S3 服务上的 G3 及 G5/G6 body/缓存审计尚未执行；不能把上游未修改构建或任意 INSTALL httpfs 版本视为具备本契约。

## 构建和能力协商

- 新增固定依赖目录 `third_party/duckdb-httpfs`，保留上游源码提交；补丁存入 `third_party/httpfs-patches/`，构建在 build 下复制源树并应用补丁，不修改已固定的上游 checkout。
- 根 extension_config 增加配套 httpfs 构建；保留其 TLS、HTTP client 和 S3 signing 依赖配置。CI 记录 upstream commit、补丁 hash、DuckDB commit 和生成的扩展 hash。
- 共享 `httpfs_om_range.hpp` 定义 `HTTPFSOmRangeProviderV2` 与 observer。Duckomo 的 context opener 实现 provider；httpfs 在打开时识别该接口。observer 仅存活于绑定/扫描的 RemoteReadSession，所有句柄先于 observer 析构。
- 配套 httpfs 注册 `httpfs_om_range_capabilities()`，返回 `abi_version INTEGER, upstream_commit VARCHAR, patch_revision VARCHAR` 一行。其 table function info 使用共享 capability 描述符；duckomo 通过 catalog 读取描述符，不在绑定过程中嵌套执行 SQL。ABI 必须为 2、commit 必须匹配、revision 必须与构建 manifest 一致，否则远程绑定直接失败。
- observer 未启用时保留普通 httpfs 行为；read_om_raw 和本地 read_om 不要求加载 httpfs。远程用户显式 LOAD 配套扩展，错误给出此操作提示。

## 窄适配职责

Provider 向 httpfs 提供本次扫描的 object session、终止状态及每句柄 observer。observer 的事件包含 request_id、attempt、method、range、response status/headers、body chunk 长度和完成/错误；不得记录签名或凭据。相同请求重试是不同 attempt。

在 httpfs 请求构建及签名之前：

1. 强制关闭 force_download、force_download_threshold、auto_fallback_to_full_download 及 unsafe_disable_etag_checks 的危险行为；不改用户的全局设置。
2. 本 range-session 绕过 httpfs 元数据/完整文件缓存与读前预取；DuckDB 打开标志使用 NO_CACHING，避免数据库级 ExternalFileCache 绕过会话控制。保留连接复用但不缓存对象内容。
3. 在发请求前检查取消；将 expected VersionId 或 If-Match 加入待签名请求。S3 凭据发现、端点解析、region 与签名仍由 httpfs 完成。
4. 响应回调先做协议/版本验证，再把数据传给 OM reader；记录实际接收 body，不用请求长度估算。错误被统一脱敏再向上抛出。

共享 C++ ABI 和多扩展装载已有 native 检查及配套产物路径；官方同版本 DuckDB、真实服务完整结果与 body 对账仍由 G3 gate 补证。

## 对象打开与授权

每次 SQL 查询在元数据解析前执行新鲜 HEAD，必须取得正文件长度；继而执行一个 `bytes=0-0` 范围探测，确认 GET 权限与范围支持。探测属于 OM header/metadata 成本，不是值变量读取。即使自有缓存完全命中也不能省略这两步。无 HEAD 支持的服务本期明确报错，不用全文件 GET 代替。

强 ETag（非 W/）或 S3 非空、非字面 null 的 VersionId 可用于跨查询缓存验证。只有弱或没有 token 时可读，但对象须在整个扫描期间不变、跨查询缓存禁用；输出 profile 标记 `version_strength=unverified`。不能从相同长度推断相同内容。

每个 worker 新句柄都与绑定对象的长度及已知版本核对。每个新的查询按当前权限重新验证；签名 URL 的授权变化、secret 更换、endpoint/region 改变进入不同的 access_partition。访问分区标识使用会话内盐化摘要，不能把密钥本身写入缓存统计。

## 范围请求成功条件

对 offset/length 先做非负、加法溢出和 EOF 检查，再发准确范围：

- GET 必须最终返回 206；`Content-Range` 必须恰好等于所请求起止及已知对象总长。即使请求覆盖整个对象，也不能接受 200。
- Content-Length 若存在必须等于所请求长度；最终累计 body 也必须完全相等。压缩传输不允许改变 OM 字节位置：请求 identity 编码，拒绝不符合的 Content-Encoding。
- VersionId 已固定时请求和响应保持同一版本；使用 ETag 时条件请求与每次响应 ETag 均须一致。已建立强 token 的响应突然缺少 token 视为失败；无强 token 会话仍检查长度及任何新出现的冲突证据。
- 412/版本冲突、200 回退、范围错位、截断、多余 body 和超时均使整次查询失败；不得向缓存插入不完整范围。
- 不跨对象重定向携带访问凭据。无认证 HTTP 允许有限重定向（最多 5 次），最终对象身份固定；认证跳转、端点或版本变化不能证明安全时拒绝。S3 的正常 region 处理留给 httpfs 并重新签名。
- 网络重试沿用已配置的有限次数，每次继续携带同一版本条件；禁止在已经提交行后重跑整个任务。所有 attempt 的已收 body 均计入成本。

这里的网络字节表示应用接收的 HTTP response body，排除 TLS/TCP/HTTP header；成功场景与可控服务端记录交叉核对，故障中止时另标完整性，不要求服务端已发送而客户端未消费的字节相等。

## 范围缓存

缓存发生在 OM reader 请求与 range-session 之间。优先精确/包含范围命中；miss 读取准确范围后插入。禁止缓存层主动扩大到含值数据的整页，从而保留空/坐标/count 的零值 I/O。bind 元数据与 worker 数据可共享同一会话对象版本的条目，但每次查询授权验证独立完成。

## 验证服务器与 S3 环境

HTTP 验证服务器记录 request_id、URI 的非敏感身份、range、响应状态、token 和 body 长度；可注入 200、错误范围、短读、超时、缺 token、版本替换及权限拒绝。S3 使用真实 S3-compatible 服务验证签名与权限，前置不改写请求的审计代理记录 body；单独的协议 mock 覆盖难以由服务注入的错误响应。

固定服务版本或容器 digest 写入实验 manifest；测试中的 S3 凭据只访问临时测试桶，不放入仓库。完整验收同时覆盖 HTTP、TLS HTTP、本地 S3-compatible 服务和至少一个真实 OM 内容；不以真实云服务可用性替代可复现故障测试。

## 2026-10-08 官方 HTTPFS 契约修订

当前未发布版本以 [官方 HTTPFS 迁移契约](../../004-multi-grid-selection/evidence/official-httpfs-refactor/contract.md) 为准。历史专用 ABI、LRU 与 G5 证据保留历史状态；新 gate 单独记录。
