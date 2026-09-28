# duckomo

duckomo 是规划中的 DuckDB C++ 扩展：直接扫描 Open-Meteo OM 文件，将 SQL 中的列选择和时空条件转换为 OM 逻辑数组切片，只读取所需数据。

> 当前仓库固定的是 **v1 产品与技术方案**，尚无可运行的扩展。下方 SQL 是目标接口，不代表已经实现。

```sql
SELECT time, latitude, longitude, temperature_2m
FROM read_om('temperature_2m.om', domain := 'example_domain')
WHERE latitude BETWEEN 30 AND 40
  AND longitude BETWEEN 110 AND 120
  AND time >= TIMESTAMP '2026-09-28';
```

`domain` 仅示意：OM 文件若没有足够的网格或时间语义元数据，调用者必须提供可解析的映射信息；duckomo 不猜测坐标。

## 方案文档

- [产品与接口 Spec](docs/spec.md)：目标、SQL 语义、范围和验收要求。
- [技术架构](docs/architecture.md)：职责、数据流、模块边界、参考项目与待验证假设。
- [Roadmap](docs/roadmap.md)：Phase 0–7 的交付物和退出条件。

这些文档整理自 2026-09-28 的“查看xtensor项目”对话最终方案，并采用本仓库名称 `duckomo`。实现时以仓库文档为基线；发生设计变更时更新对应文档及其依据。
