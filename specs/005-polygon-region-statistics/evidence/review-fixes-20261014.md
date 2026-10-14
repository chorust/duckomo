# Review 修复验证（2026-10-14）

状态：三版本本地验证通过。本轮源码 SHA-256 为 `3be1fcf158a30bb19ed27f244904c1c8196501e42a0a5387d3e1e696be400a70`。

本轮修复普通构建的 spatial 前置条件、共享几何 payload 重复计费，以及区域输入与扫描工作内存混计。普通构建和 `--extension-only` 不要求 spatial 产物；区域 sanitizer 使用 `scripts/build-version.sh <version> --region-sanitizers`。

每版本完成 16/16 ASan/UBSan、19 个 SQL 文件共 1,028 个断言、普通 native/domain 参考回归、9 个 Python suite 共 93 项测试（逐文件运行无跳过），以及 P0–P4。domain 回归匹配全部 15 份官方参考；31 个区域、每个 32,768 顶点的大几何回归在普通与 sanitizer 路径通过。

P3/P4 固定相同区域输入、32×32 chunk 和窄结果，空间点数从 65,536 增至 1,048,576（16×）。工作容量从 ledger 排除 retained region input，仍包括 metadata、reader、decoder、selector、task、batch 和 worker evaluator 自有状态。共享几何 payload 由共享输入账户计费一次；副本仅计节点/描述符。

| DuckDB | P0–P4 | Sanitizer | P3 work / RSS 比例 | P4 work / RSS 比例 |
|---|---|---|---|---|
| v1.5.4 | pass | 16/16 | 1.000004 / 0.987384 | 1.000004 / 0.992064 |
| v1.5.5 | pass | 16/16 | 1.000004 / 0.989794 | 1.000004 / 0.985117 |
| v1.5.6 | pass | 16/16 | 1.000004 / 0.995524 | 1.000004 / 0.985848 |

所有 work/RSS 比例均满足 ≤2，exact/owned complete 为 true。spatial/GEOS 内部内存仍未知，RSS 是独立进程范围。

机器摘要：[summary.json](review-fixes-20261014/summary.json)，SHA-256 `e28f1348c2c5eb9d3205b191a02e6a669ed511c63d9e517b717d0bde1da3e1fa`。各版本 manifest、P3/P4 报告及完整命令/日志 hash 索引保存在同目录；审计重新核对当前源码、产物、输入、命令输出和报告 hash，三版本同源。完整日志和 bulk 输出位于 `build/evidence/regions/005-review-fixes-20261014/`。

本轮未重跑 P5、完整 P6 或独立 P7。2026-10-13 的固定窗口远程通过与独立 AI 复现保留为旧产物历史证据，不作为本轮产物的通过记录；本轮 hash 审计仅签本地验证范围。全域远程收益未验证，roadmap 保持 implemented。

## 构建与产物身份

| DuckDB | Build ID | DuckOMO SHA-256 | P0–P4 manifest SHA-256 |
|---|---|---|---|
| v1.5.4 | `ec24083e32cf3008368ee496a8847b5c4ba0f8e006008a7b363ead2c4ded2cc1` | `00288c10872b7eece42977759d20eaf2946b03c0ae3b67b4d1c64ccdc8bf559a` | `48d70c1534b252a50d7c1903932e4cd402a96bddcac46193ad2fb04ec04c9b50` |
| v1.5.5 | `1dbc3e6e32895af53d58ec0ded3319ec0bdf77b56f070b89f70d77e314a85937` | `aa9041641ed24f5d6823bb42625f20d4e0beca297304c0353044ac758b58ed7e` | `390ea8be67cf610e6966eb466423f835df662f1c1cd501a983511ee4d5c58e1b` |
| v1.5.6 | `657b1402e7f9a98e34ab595c54ef1eab19d2cee373f4093824e170e8fe77aa89` | `6564a764ad436cb82543655278193f8127143e5e9710e8fcc967d24914f0e723` | `1fe7cd88bc68b67380dc33af0e8c79a04ebe7063d9b658cf7123f1a0a587799f` |
