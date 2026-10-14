# 回归产物绑定修复验收（2026-10-14）

状态：历史源码快照 `77b79d279b73…` 的三版本完整回归、P0–P5 和 P6 汇总通过；独立 fresh-context AI P7 通过。后续变量选择与元数据优化已改变源码，本记录不能证明当前优化通过；v1.5.5 历史构建目录曾被重建，其现存二进制也不能替代此处记录的 hash。P5/P7 仅覆盖冻结局部窗口，不宣称全域远程收益或人工验收。

源码 SHA-256：`77b79d279b735731300342630de0ac52a3b87b3d107dd60a3c668a128c8248db`。完整日志与结果目录：`build/evidence/regions/005-artifact-binding-77b79d279b73`。

本轮修复回归记录中的产物选择：普通回归显式绑定版本构建目录、CLI、DuckOMO 与 fixture tool；sanitizer 使用隔离扩展，生命周期 JUnit 记录实际加载路径。P6 检查命令环境及依赖路径/hash，并与记录器共享 JUnit 判读。构建、回归、P0–P5 使用同一份源码及每版相应产物。

| DuckDB | Build ID | 回归 | P0–P4 | P5 | Sanitizer |
|---|---|---|---|---|---|
| v1.5.4 | `abc08d813e3b65de88306b9834d96029ba2b35c964c3d287a84b52662f48896d` | 32 native / 19 SQL / 12 Python | pass | 五目标 × local/HTTP/HTTPS/S3 pass | 16/16 |
| v1.5.5 | `8dbd4e0bfd5763f7b073893b403883ce13daf7d2b17f687b3a5783f75ef762cd` | 32 native / 19 SQL / 12 Python | pass | 五目标 × local/HTTP/HTTPS/S3 pass | 16/16 |
| v1.5.6 | `828b20ce97c7a0ad073f03034a851288cf4b25998ae75f1aadae18044def9478` | 32 native / 19 SQL / 12 Python | pass | 五目标 × local/HTTP/HTTPS/S3 pass | 16/16 |

每版 domain reference 使用冻结对象与全部15份官方参考；SQL 文件共1,028个断言。Python suite 无跳过。P3/P4 独立工作容量和进程 RSS 增长均满足 ≤2；spatial/GEOS 内部内存仍未知。

| DuckDB | P3 work / RSS 比例 | P4 work / RSS 比例 | DuckOMO SHA-256 |
|---|---|---|---|
| v1.5.4 | 1.000004 / 0.985469 | 1.000004 / 0.985472 | `171759d6a02e1e1924e2a68beaefeac03da208af066f6868bddc4d5163d59148` |
| v1.5.5 | 1.000004 / 0.986307 | 1.000004 / 0.990968 | `0f5128a54c9776aeffd6230abb9482873f44e8e9b6f9a2bb91eb860c625dc47b` |
| v1.5.6 | 1.000004 / 0.995756 | 1.000004 / 0.986104 | `12b18ac59a1c6eaedc047821f55b889beb046893c590760b752f72ddbdaa8348` |

机器摘要：[summary.json](artifact-binding-77b79d279b73/summary.json)；P6：[P6.json](artifact-binding-77b79d279b73/P6.json)。P6 重算三版本输入、产物、日志、报告、回归依赖身份及固定窗口来源覆盖，没有缺失或审计错误。

独立复现：[P7-independent.md](artifact-binding-77b79d279b73/P7-independent.md)，验证者为 `codex/gpt-6.1-sol`、fresh delegated context，未参与实现。v1.5.4 A/B/Empty 的108条原始记录、12个非空统计组及18个补齐组合均符合独立参考。ncep_gfswave025 的固定窗口 HTTPS 对照各9条完整记录、零差异；值读取字节77,580→25,860、解码块135→45、服务发送字节150,469→79,671，审计请求全部成对成功。原始CSV/metrics及按独立audit ID筛选的日志另保存在 bulk 目录的 `P7-artifacts/`，没有复制访问凭据或私有setup SQL。

P7 是独立 AI 验证，非人工验收；它不单独签署另外两个版本、其他四个目标或签名 S3。三版本五目标四来源由 P5/P6 记录覆盖。OS/page cache 未清除，不宣称冷服务端吞吐；HTTPFS 客户端传输计数未知时保持未知。重复小读取/跨调用合并的性能限制未在本轮改变。

先前 2026-10-13 和 review-fixes-20261014 记录作为旧源码/产物历史保留。本记录绑定上述历史 build ID 与记录 hash，不能与后续源码或重建产物混用。
