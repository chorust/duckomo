# 当前源码与验收身份复核

日期：2026-10-21。协调者实际执行的只读检查，不代表重新运行历史命令。

- HEAD：`6b34e523fe9f636aec1cabdf4b14be6bc47a6ec1`。
- 使用现有 `scripts/version_matrix.py::extension_source_digest` 重算源码摘要：`886e5f8e27e6ecdf66f217bab28e8726ecd7ce9aab0519815387f6c2cc34677e`，与 005 最新验收相符。
- 三版本 v1.5.4/v1.5.5/v1.5.6 的 build-manifest 输入摘要全部匹配；实际 DuckOMO 扩展、DuckDB、unittest 共九个产物 SHA256 与构建记录全部匹配，三个扩展也与 P6 中的摘要匹配。
- 三版本 P0–P4、P5、regressions 共九份 manifest 的 SHA256 与 `specs/005-polygon-region-statistics/evidence/current-20261020/P6.json` 全部匹配。
- 递归检查上述九份 manifest 中具有对应 `_sha256` 的 `_path` 字段，绝对路径按原路径、相对路径按 manifest 所在目录解析：共 336 对存在且 SHA256 匹配，无错误。此检查不是对所有 JSON 字段或整个证据依赖图的完整审计。
- 检查上述 manifest 中 306 条 `exit_code`，全部为 0；这些是历史留存结果，并非本轮重新执行。
- 进一步核对 regressions 命令 `artifacts` 列表中的 `path`/`sha256`：157 个去重产物全部匹配，包含测试命令引用产物。此数不与前述 336 对直接相加作为去重总数。
- `git diff --check` 通过。未修改源码、测试或生命周期状态。

证据根目录：`build/evidence/regions/current-20261020/matrix-testfixed/`。该目录是本机留存产物，不能据此宣称另一台机器已可重现。

此结果证明当前纳入摘要的输入与留存构建、部分证据文件身份一致。它不证明实现没有缺陷，不替代逐需求源码审计，也不将 005 的回归升级为 003/004/006 全部门禁的新一轮验收。
