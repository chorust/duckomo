# 连续矩形读取优化与验证（2026-10-20）

## 实现与边界

`BuildBatchSegments` / `BuildSelectedBatchSegments` 将严格连续的源位置按完整尾轴合为矩形，交给官方 OM reader。只有尾轴从0开始且被完整选中时才扩展前一轴；遇到缺口、未对齐起点或不完整尾轴仍拆段。输出顺序、源位置、多重性和选取值范围不变，不增加值行、物理planner或decoded缓存。单批最大行数、64KiB映射预算、reader容量账本与每次decoder调用前的取消检查保留。

## P5成本契约的后续决定

旧规则完整预检40次扫描的精简失败记录见[original-p5-failure.json](contiguous-rectangle-performance-20261020/original-p5-failure.json)。NCEP字节相等、解码下降，其余四目标三项均下降。负责人随后批准[各项不增加且至少一项下降](../contracts/non-increasing-cost-20261020.md)；新规则单独运行，不改写原失败。

## 已运行检查

- `batch_test` 穷举 rank1/2/3/4/8 小shape的全部合法起点/长度，逐坐标展开矩形，检查完整覆盖、源顺序、无额外行或重复。
- `axis_selection_test` 保留稀疏间隙、高rank预算和混合轴测试，新增跨行合并及跨外轴边界不错误合并。
- `reader_capacity_test` 对同一 `raw_large.om` 七行（chunk `[7,13]`）分别逐行/矩形调用官方reader，输出位级相同；decoded chunks **35→5**，scratch容量上限相同。

## 同源码A/B实验

开发产物仅将 `batch.cpp` 与 `axis_selection.cpp` 换回HEAD生成探索性基线，其它对象相同；两产物版本相同。构建命令、两源码文件、原始CSV、metrics与hash保留在 `build/evidence/regions/current-20261020/{ab-baseline,performance-ab}/`。该基线仅作受控实验，不作正式验收产物。

每项local/受控HTTP各5次交替运行，新CLI、客户端三类cache关闭，1 worker，完整消费并核对所有CSV hash。未清OS cache，构建同时运行；耗时只作诊断，不设普遍加速声明。

| 输入/查询 | 物理值data请求 baseline→优化 | 解码块 baseline→优化 | 本地耗时中位数 | HTTP耗时中位数 |
| --- | --- | --- | --- | --- |
| raw_large 完整读取 | 13→10 | 372→127 | 8.4→5.1ms | 26.6→25.5ms |
| gem_rdps_10km 固定窗口 | 6→6 | 32→32 | 698.6→738.7ms | 768.1→806.8ms |
| gem_regional 固定窗口 | 9→9 | 54→54 | 517.6→488.4ms | 578.5→537.6ms |
| aladin 固定窗口 | 14→14 | 180→180 | 678.4→650.5ms | 777.2→739.9ms |

raw_large 值data字节 **11,412→17,306**：矩形改变官方decoder请求合并及最近范围复用的命中序列，减少调用/解码/请求不保证减少物理字节。三个真实固定窗口的data字节和请求不变。结果行hash全部相同。

精简机器摘要与逐组产物/结果hash、请求/解码、耗时和内存完整性见[summary.json](contiguous-rectangle-performance-20261020/summary.json)。

结论：该变更对能覆盖完整尾轴的连续扫描减少重复解码，局部真实小窗口的I/O可能不变；保留字节增加和耗时混合结果，不宣称任意读取均加速。最终三版本P0–P6/P7验证另记其当前状态，不从本实验自动推断通过。
