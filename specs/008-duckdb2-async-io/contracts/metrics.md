# Contract: 扫描指标扩展

`duckomo_last_scan_metrics()` 每行一个 JSON。现有字段保持不变（含 `legacy_v3` 与 `transport`）。新增顶层对象 `remote_io`；本地扫描时整体为 NULL。

```json
"remote_io": {
  "merge_enabled": true,
  "merge_gap_bytes": 4096,
  "max_request_bytes": 65536,
  "async_applied": false,
  "inflight_peak": 1,
  "inflight_budget_bytes": null,
  "requests": 412,
  "physical_bytes": 4190123,
  "useful_bytes": 4087256,
  "gap_bytes": 102867,
  "phase_requests": {"metadata": 3, "coordinate": 0, "index": 38, "data": 371}
}
```

| 字段 | 含义 | 不可得时 |
|---|---|---|
| `requests` / `physical_bytes` | 成功完成的远程读取次数与字节，与现有 physical 计数一致 | — |
| `useful_bytes` | 成功物理 data 读取中选择所需 chunk 的压缩字节之和（取自已解压 LUT）+ 成功 index/metadata/coordinate 请求字节；复用已缓存范围不重复计数，预读及未选择 chunk 不计入 | 远程扫描不允许 NULL |
| `gap_bytes` | `physical_bytes - useful_bytes`，≥ 0 | 远程扫描不允许 NULL |
| `inflight_peak` | 所有 worker 中单 worker 在途请求最大值；同步路径为 1 | — |
| `inflight_budget_bytes` | 异步路径本次扫描中生效的最小在途字节上限，随剩余内存收缩 | 同步路径为 NULL |
| `phase_requests` | 按读取阶段的成功请求数 | — |

合并开关可能改变重复读取与缓存复用次数，因此 `useful_bytes` 在两个模式间不要求相等；`physical_bytes = useful_bytes + gap_bytes` 必须成立。例中的 `async_applied=false` 表示同步路径。

终态规则沿用现状：失败或取消的查询仍发布指标，`outcome.status` 为 `failure`/`cancelled`；未完成的在途请求不计入 `requests`。字段只记录数量，不包含 URI、头部或响应内容。
