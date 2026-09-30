# Data Model: 规则网格空间下推

本阶段均为内存对象/构建期资产，无持久化数据库。字段是设计职责，不固定 C++ ABI。

## RegularGridDefinition

字段：`nx,ny`（正整数），`lat0,lon0,dlat,dlon`（有限 DOUBLE），`order`（separate/lon_fastest/lat_fastest）。点 `(y,x)` 坐标由首点加索引乘步长给出，经度统一到 [-180,180)。两步长均不得为零，包括单点方向；显式 grid 的纬度每点必须在 [-90,90]，三个 MeteoFrance 海洋 domain 按固定上游定义允许末行约 90.041664°；坐标中间及最终运算不能溢出；所有计数及 stride 运算检查溢出，行数不超过 INT64_MAX。

## SpatialLayout

字段：已经验证的 `shape`、`ordered_axes`、空间轴位置、展平顺序、行优先 strides、非空间轴位置。引用一个网格和所有 BoundVariable。

- separate：spatial_axes 按纬度、经度顺序指定两个不同轴名，对应轴长分别为 ny/nx；实际文件轴顺序不受此列表顺序限制。
- lon_fastest：一个空间轴长 ny×nx，`s=y*nx+x`。
- lat_fastest：一个空间轴长 ny×nx，`s=x*ny+y`。
- 每个变量的 shape 和有序轴身份一致；元数据与显式 dimensions 冲突时拒绝。无转置、广播、路径推断。
- 额外轴逐位置完整遍历；输出不生成其语义坐标。

## DomainDefinition

字段：`name`、固定规则网格定义、上游 commit/源码路径及部分样本 SHA-256。Domain 不下载文件、不识别路径、不证明任意同 shape 文件来自该 domain；调用者选择 domain 即声明其来源，binder 仍核对每个值变量的完整有序轴、空间轴长和文件存在时的 WKT BBOX。额外轴保留，缺少 coordinates 时由调用者通过完整 dimensions 声明。逐目录样本证据见 [规则网格 domain](../../docs/regular-domains.md)。

当前 registry 的全部名称可由 SQL 选择；每项固定到同一上游提交。样本覆盖因目录和对象可得性不同，必须随条目区分记录，不能把没有样本的定义称为真实文件已验证。若上游或布局变化则更新来源与样本证据，不静默覆盖既有记录。最初条目设计见 research；当前条目与样本覆盖见 SQL contract 和规则网格 domain 文档。

## SpatialPredicate / GridSelection

SpatialPredicate 保存安全抽取的坐标比较的值副本；按轴合取，包含上下界开闭性和等值。不保存借用 Expression 指针。未经支持的子树只留下诊断原因，完整表达式继续由 DuckDB 持有。

GridSelection 字段：模式 full/restricted/empty/fallback，安全比较集合，残余条件存在标志，回退原因，一维命中区间游标。区间使用半开逻辑索引 [begin,end)，有序、不重叠。独立 AND 可部分缩小，mode=restricted 同时标明 residual/fallback 子树；完全不能提取时为 fallback。empty 优先，无 decoder。

## SpatialBatchCursor / ProjectionPlan

Cursor 保存额外轴索引、当前空间区间和原始逻辑位置；每次输出最多 STANDARD_VECTOR_SIZE 行的 BatchSegment/逻辑位置映射。每个原始位置恰输出一次，不按坐标去重。ProjectionPlan 的槽位扩展为 value(variable_id)、latitude、longitude、cardinality；只对唯一 value ID 建 decoder，过滤依赖仍包含在请求列中。

生命周期：bound → selection_planned → initialized → emitting → exhausted；任一步失败进入 failed/cancelled 并释放文件、decoder/scratch/selection 所有权。空选择直接 exhausted；纯坐标路径不创建值 decoder。Copy/Equals 必须覆盖网格、布局、选择相关状态，执行期 cursor 不与其他查询共享；变更 bind state 的实现关闭 statement cache。

## SpatialValidationRecord

继承既有 metrics，schema_version=2；增加 grid/layout/source、selection_mode、fallback_reasons、候选与实际输出行数、预设容差、参考身份及逻辑位置核对结果。计数溢出报错，失败解码标记不完整；字段详见 [观测契约](contracts/validation-evidence.md)。文件可存放 build/evidence/spatial，最终可审阅报告整理到本 feature 的 evidence 目录。
