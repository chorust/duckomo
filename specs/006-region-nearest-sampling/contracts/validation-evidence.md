# Validation and evidence contract: N0–N7

所有gate按N0–N7契约范围判断；本文没有把旧005结果、局部SQL/native命令或self-check推断成完整006交付通过。最新N1/N2运行包见 [n1-n2-acceptance-refresh-20261019-final-7](../evidence/n1-n2-acceptance-refresh-20261019-final-7/)，其两门通过；N0/N3/N4历史包记录对应gate结果，N5部分真实/传输子检查见N5-dongcheng.md，N6/N7未完成。源/数值/值/布局/依赖/命令/结果绑定后才能判断。planned、pass、fail、blocked、not-run、early-stopped分开，不用样例输出冒充运行记录。

## 范围与运行身份

- **006 N1/N2逐profile验收声明：合成规则球面网格与NCEP GFS025。** 其它已登记domain不列入逐profile验收声明；登记、可绑定或可读取本身不代表已验收。合成矩阵是显式冻结的有限案例，不宣称穷尽所有binary64网格、目标点或任意规则球面profile。运行时对任何输入仍按通用资格规则检查。
- 平台：Linux AArch64/linux_arm64；DuckDB v1.5.4/v1.5.5/v1.5.6，匹配spatial/官方HTTPFS。pins继承test/data/grids/version-matrix.json和test/data/regions/runtime-matrix.json，不要求DuckDB2.0。
- 每次记录extension source HEAD+dirty patch hash、DuckDB/OM/tooling pins、编译器/浮点flags、CLI/duckomo/spatial/httpfs SHA256与实际version。历史manifest只读，006使用新manifest记录探针与结果。
- 基准与采样使用同一文件、变量和非空间窗口，全部消费结果。full baseline确实解码同变量完整对象，不用COUNT(*)元数据路径假装全值读取。
- 数值核和grid资格见[numeric contract](numeric-and-capability.md)，SQL语义见[SQL contract](sql-interface.md)。

## 固定合成输入

当前 `test/data/sampling/manifest.json` 由fixture tool的`--sampling-output DIR`模式重生；独立 `generate-sampling-reference.py` 生成几何/坐标/数值/轴位置及预期结果。已注册fixture检查确认9个几何、62条nearest参考、16个OM fixture及29个变量；官方reader值与locator位置oracle分开。

| fixture | 冻结要求 |
| --- | --- |
| small-separate | 5×5源点、member×time×level=2×3×2，两个变量、缺测位置不同、时间标签重复；axes interleaved使空间轴不总在最后 |
| small-flat-lon / small-flat-lat | 相同坐标场，lon_fastest/lat_fastest，验证point与logical index区别 |
| reversed | dlat/dlon各自与同时为负，坐标映射保持独立oracle可核对 |
| status | Gap小面、源点精确位置的对称面、EMPTY、Outside、超距、同source不同region ID、hole/MultiPolygon |
| numeric | 周期全球小网格、单行/单列、等距、近等距、极点source、seam目标、轴最近/球面最近反例；重复wrap/超纬/异常模型拒绝 |
| invalid | sampling同名值、非法method/距离/策略、无spatial/错误签名、非法区域在WHERE/LIMIT后仍拒绝 |
| memory-small / memory-large | 空间shape为64×64与256×256（16×），相同空间extent与固定chunk shape[1,1,1,16,16]、同member/time/level、相同区域/线程及近似窄输出；由相应间距提供同域合法网格 |
| skippable-values | 足够多OM块、需要选择一个源点的完整对象；N3完整读取与采样值字节/解码严格下降 |

合成区域使用完整受检标准WKB，不调用生产代表点算法生成参考。矩形的代表位置用解析中点；复杂形状只核对ST_Covers和记录实际point，locator独立oracle可以用实际目标坐标但不能用待测内核推断winner。参考值来自官方OM C reader；原始索引/轴顺序用独立参考重新计算，不从结果回填expected。

## 真实东城区输入的可交付性

目前可用探索性文件：GFS112068504字节，SHA256 `676742c9fe81dd7af7472c7f41d3651d8bd3ff5bcaf1c47b965d415310472375`；本地北京18feature文件SHA256 `0dc3f3a146b5c12fc5380f0360e287a0d5298e27280415b8729ad97fab3fd6f0`。这些位于ignored build目录，不能成为唯一复现路径。

N5前置工作：

1. 从现有文件按properties.id='东城区'精确提取52顶点单Polygon，保存其标准WKB与输入引用/来源声明，记录原JSON hash、规范化子feature hash和WKB hash。单feature紧凑UTF-8 JSON（无末尾换行）预期hash `5407f1d6f29ff475400be7da4af220d8f9df7191ec4721ecabcda2a3960febd6`。
2. 原区界的外部来源、datum、许可目前不由文件证明。冻结记录必须诚实注明caller-declared grid_lonlat、未认证实时行政区。未解决可再分发条件时不把几何入公共仓库，使用外部已授权archive+hash+导出命令。不能把README中GADM青海下载流程误标为这份北京几何的来源。
3. 冻结GFS原对象键 `data_spatial/ncep_gfs025/2026/10/14/0000Z/2026-10-14T0000.om`、可用获取位置、长度与SHA256，支持用户提供既有本地文件路径。滚动对象过期或不可下载时用已保存的合法artifact archive；无输入时N5 blocked，不静默换日期/变量。
4. 发布不依赖ignored路径的006真实manifest与验证入口。一个小型合成网格可复现“区域位于点间”，不能代替真实GFS文件证据。
5. 在每个实际spatial版本重新生成代表点、检查面内和最近源；固定本地边界预期源(116.50,40.00)，point_index以原layout独立计算；值为直接读取该记录的FLOAT（探索记录约13.45°C），不以四舍五入数值代替位一致。

## Gates

| gate | 内容与通过条件 | 需求映射 |
| --- | --- | --- |
| N0 dependency/baseline | 三版本准确ST_PointOnSurface/ST_X/Y/ST_Covers签名、实际包身份，普通read_om无spatial回归；旧points差分基线冻结 | FR001–003/014，SC003 |
| N1 locator | 在声明scope内验证独立源index/阈值/平局oracle、高精度距离误差、冻结的能力边界类、覆盖/周期、双向步长与布局；有限case不等同所有binary64输入或目标空间穷尽。条款到案例映射和reference derivation已复核；377项能力参考包含构造值有限但经度周期乘积溢出的拒绝案例。所列代表性有限矩阵通过三版本验证 | FR004–007，SC002 |
| N2 SQL/schema/state | 在声明scope内验证points省略/显式schema与多重集零差异；nearest多轴/多变量/EMPTY/域外/阈值/NULL/source、参数绑定、共享region input parser、residual、rebind。共享parser覆盖可达的count、WKB字节、单区域顶点/geometry预算边界；总顶点与owned-input上限被更严格的总WKB限制支配，属于防御检查。SQL覆盖四条组合无效输入优先级探针；parser排列cross-product和完整优先级矩阵作为明确有限样例边界，不要求穷尽。所列三版本SQL/native矩阵通过 | FR001–014，SC001/003–005 |
| N3 projection/I/O | COUNT、目标与sampling几何叶、source路径零值index/data/decode；MAP状态依赖诚实；最终WHERE与独立采样后筛选相同；完整可跳块对象值字节/解码下降 | FR015–017，SC006 |
| N4 resources/lifecycle | 1/2/4workers多重集相同；16×空间样本工作峰≤2×；容量ledger/checked overflow，LIMIT/取消/短读/decoder/object变更后的恢复 | FR018/019，SC005/007 |
| N5 real/remote | 固定真实东城区和GFS，points零点/nearest源与直接读取一致；同内容本地/HTTP/HTTPS/签名S3多维合成样本完整差分；远程错误不降级NULL | FR001–019，SC001/004–006 |
| N6 matrix/audit | 三版本N0–N5证据正确绑定、源码/产物/input/SQL/output hash完整、未知指标不补零、误用schema/版本/失败结果被审计拒绝 | FR014/018/019，SC002–007 |
| N7 independent guide | 未参与实现者按quickstart选择入口、切换method、辨别状态并核对来源，保留命令/输出/身份与结论；不要求用户执行私有脚本 | FR020，SC008 |

纯数学locator在N1可先于SQL接入，完整N2/N3依赖schema/projection接好。普通make test不是N2–N7替代；新SQL/native须显式登记为有spatial运行的验证，缺运行包/真实文件不能跳过后算pass。

## 远程与资源细节

- HTTP/HTTPS/签名S3测试复用受控服务和已有授权/对象错误设施；使用完全相同合成文件，所有变量/状态和source位置双向EXCEPT ALL，URI对象ID可不同。
- 本期不要求对112MB真实GFS做远程全域收益扫描。N5真实nearest与源记录局部读取对照即可；严格值读取收益用事前固定合成可跳块对象的本地完整消费执行，不据此宣称真实公网全扫吞吐。
- 读取量以自有应用成功字节、OM index/data/decode分列；HTTPFS真实网络body为unknown。若采集服务端发送量，独立证据归档，不填为客户端transport。
- 内存实验在隔离进程，同输入区域/线程/非空间轴/chunk，只扩大空间点数；完整nearest输出相近。peak_work包括既有selector/task/reader/decoder和新增scratch，输入/plan分别报告，不能只计新模块让比值过关。绑定定位时间可随点数变长，CPU不算内存失败但必须记录。
- 取消/错误/早停exact result计数NULL；仅坐标计数无值读取仍要完整绑定定位，不能绕过能力检查。

## 执行工具约定

当前 `scripts/validate-sampling.py` 复用标准库与现有runtime/validation支撑，不复制HTTPFS实现。接口和可运行命令见 quickstart；N1/N2 最新包为 pass。完整006验收仍需完成N5–N7；旧的N0/N3/N4/N1/N2快照保留在历史包中：

```text
validate-sampling.py --root ROOT --build BUILD --duckdb CLI --extension EXT
  --spatial SPATIAL --manifest MANIFEST --output NEW_DIR --gates N0,N1,...
  [--httpfs HTTPFS --remote-env FILE --server-log DIR]
  [--real-file OM --real-regions WKB_OR_GEOJSON]
  [--evidence-root DIR] [--self-check]
```

--output必须新目录或空目录，不覆盖历史证据；--gates只能N0–N7。N0–N4本地、N5需对应真实/远程输入、N6审计evidence-root、N7记录独立执行者类型和运行范围。--self-check只验证参数与审计契约，不伪装运行gate。工具包含单元测试，自动识别缺依赖/缺输入=blocked或not-run并非0退出假成功。

一次gate包记录inputs.json、commands.jsonl（脱敏argv/cwd/exit/time）、SQL原文/hash、结果多重集/CSV hash、metrics原文/hash、gate结论和residual risks。定位数学参考、旧points兼容与性能执行结果分别保存。计划阶段可以探测依赖，不生成虚假的通过manifest。
