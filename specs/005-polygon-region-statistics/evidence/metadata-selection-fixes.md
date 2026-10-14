# 元数据选择回归修复

修复范围：普通单值嵌套/编码列投影、空目录崩溃、共享属性顺序依赖、稀疏目录拒绝、显式选择与全树重验证不兼容，以及新增范围/目录/临时 schema 的内存计数。扫描默认共享绑定元数据；紧凑根目录最多预取 1 MiB，稀疏目录回退为普通读取。完整与选择读取共用时间/坐标继承逻辑。

三个 metadata 入口 `read_om`、`read_om_regions`、`om_grid_info` 均接受 `variable`。名称使用编码 SQL 列路径，可有前导 `/`；根数组接受 `value` 或 `/`。维度 MAP 仅覆盖所选数组。NULL/空串、不存在、非值节点拒绝。嵌套选择保留全树遍历，再筛选目标；不宣称其具有紧凑根目录的绑定收益。

验证源码 SHA-256：`e4730a700267447e983e7a5b82503d343ada66e84f886886bff6a75355e69c91`。本轮构建使用新目录，不重建历史 build ID。

| DuckDB | 最终 build ID |
|---|---|
| v1.5.4 | `2f10f119845898b38ffc59a6c181bf2012c2b06a17e0dd1df3a07810a02fef14` |
| v1.5.5 | `1a84594332f50ebbcd0394102995902f65426cccbc020450b84781043db38819` |
| v1.5.6 | `cf0e88531eb34e8a357a8926a2d54e841245a6ffb6833ca783d15e9216620125` |

复现真实聚合：先按 README 准备 GADM CRS84 青海边界、固定 GFS 对象的本地副本，再运行 [verify-aggregates.py](metadata-selection-fixes/verify-aggregates.py)：

```sh
python3 specs/005-polygon-region-statistics/evidence/metadata-selection-fixes/verify-aggregates.py \
  --version v1.5.5 --remote \
  --output build/evidence/regions/metadata-selection-fixes-e4730a700267/v1.5.5
```

脚本检查当前源码/产物 hash、三个本地绑定模式、相同 SQL 的 S3 全变量/选择变量结果，以及确有值解码和绑定位置读取减少。使用 4 个 DuckDB 线程、DuckOMO 上限 2；指标读取 `reads.metadata`、`reads.value_totals` 的精确路径。这里的请求数是 DuckOMO 成功位置读取，HTTPFS 内部/实际 S3 HTTP 请求数未知；未清除 OS/page cache，不宣称冷吞吐。

真实青海输入为 GADM 4.1（文件声明 CRS84，非实时行政区划）；与之前 DataV 示例的几何不同，不跨边界比较结果。8 个区域共 1,146 条匹配点记录。全变量、选择变量、选择变量严格模式结果相同，均解码 139 个值块。v1.5.4/v1.5.6 的本地绑定元数据请求 638→4，扫描元数据请求 9；严格模式扫描请求 1,914，体现可选检查成本。内存指标报告 query-owned 完整并在终态释放，范围 buffer 计入 definitions；不将这些指标视为全进程内存。

Nino 的本地 `chunk_969` 按固定 producer 约定从 2025-09-18 开始，第一天两个区域各 24 组；Nino1+2 每小时 1,681 点，Nino3.4 每小时 8,241 点。README `chunk_987` 已修正为 2026-10-01。显式 axis labels 不等同于从文件发现真实时间。

状态：最终三个版本构建与本地聚合对照通过；v1.5.5 完整 SQL/native/Python 验证、全部 domain 参考、16/16 ASan/UBSan 和 P0–P4 通过。真实 S3 同一 AVG 查询全绑定 208.155 秒、显式选择 39.454 秒（约 5.28 倍），结果完全一致；绑定 metadata 47,256 bytes/638 reads → 485,770 bytes/4 reads，扫描 metadata 均为 15,465 bytes/9 reads。所有模式实际解码 139 个值块。合并读取以额外元数据字节换取更少位置读取；单次实测不推定普遍吞吐。

精简可审计摘要见 [summary.json](metadata-selection-fixes/summary.json)。最终产物、查询/完整指标、原评审复现与日志位于 `build/evidence/regions/metadata-selection-fixes-e4730a700267/`。v1.5.5 的 aggregate summary hash 与最终 build manifest 已核对；运行时预检名称不代表另一个源码。回归记录器的完整产物绑定报告已通过，写入其 `regressions/`：32 native、19 SQL、12 Python suite 与 16 项 sanitizer，全部命令成功。本轮没有重跑旧五目标/四来源 P5/P6/P7；[77b79d279b73 历史记录](artifact-binding-77b79d279b73.md)只属于其历史源码，不证明当前优化。
