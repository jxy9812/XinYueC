# XGui ↔ Qt 6.8.3 对齐审计报告：Charts

- 审计日期：2026-09-15
- 审计范围（只读）：`Src/XGui/Charts/*.h`、`*.c`（14 个头文件、14 个实现文件，共 6920 行）
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtcharts/src/charts`（QChart/QChartView/QAbstractSeries/QXYSeries/QLineSeries/QSplineSeries/QScatterSeries/QAreaSeries/QAbstractBarSeries/QBarSeries/QBarSet/QPieSeries/QPieSlice/QAbstractAxis/QValueAxis/QCategoryAxis/QBarCategoryAxis + legend/themes）
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》（已核对字符串 API、XClass 虚表、init/deinit 成对、禁止 memcpy 复制对象等条款）；`XGui.md` 中**未检索到 Charts/图表 章节**（grep XChart/XChartView/XLineSeries/XValueAxis 均无命中），样式现状无图表相关说明，本报告以 Qt 源码为唯一行为基准。
- 结论等级：**模块存在 P0 级问题（渲染精简近似、生命周期违禁、字符串 API 违例），不建议按当前状态对外宣称与 Qt Charts 对齐。**

---

## 一、模块概览表

| X 类 | 对标 Qt 类 | Qt 头文件（相对 qtcharts/src/charts） | 继承链是否 1:1 | API 缺口 | 功能缺口 | 完整度 |
|---|---|---|---|---|---|---|
| XChart | QChart | qchart.h | 否 | 12 | 8 | 55% |
| XChartView | QChartView | qchartview.h | 否 | 1 | 5 | 70% |
| XAbstractSeries | QAbstractSeries | qabstractseries.h | 是 | 8 | 3 | 65% |
| XXYSeries | QXYSeries | xychart/qxyseries.h | 是 | 14 | 6 | 60% |
| XLineSeries | QLineSeries | linechart/qlineseries.h | 是 | 0 | 0 | 95% |
| XSplineSeries | QSplineSeries | splinechart/qsplineseries.h | 否 | 0 | 1 | 85% |
| XScatterSeries | QScatterSeries | scatterchart/qscatterseries.h | 是 | 4 | 3 | 60% |
| XAreaSeries | QAreaSeries | areachart/qareaseries.h | 是 | 8 | 5 | 55% |
| XAbstractBarSeries | QAbstractBarSeries | barchart/qabstractbarseries.h | 是 | 10 | 4 | 50% |
| XBarSeries | QBarSeries | barchart/vertical/bar/qbarseries.h | 是 | 1 | 2 | 70% |
| XPieSeries | QPieSeries | piechart/qpieseries.h | 是（虚表声明不一致） | 4 | 5 | 60% |
| XPieSlice | QPieSlice | piechart/qpieslice.h | 是 | 6 | 5 | 60% |
| XValueAxis | QValueAxis | axis/valueaxis/qvalueaxis.h | 否 | 15 | 5 | 30% |
| XCategoryAxis | QCategoryAxis（名义）/QBarCategoryAxis（实际语义） | axis/categoryaxis/qcategoryaxis.h、axis/barcategoryaxis/qbarcategoryaxis.h | 否 | 12 | 5 | 25% |

---

## 二、逐类对比

### 1. XChart ↔ QChart（`qchart.h`）

**继承**
- Qt：`QChart → QGraphicsWidget → QGraphicsObject → QObject → QPaintDevice`
- X：`XChart → XObject`（XChart.h:129, XChart.c:100）
- 结论：**不 1:1**，缺少 QGraphicsWidget 中间层。X 侧无 QGraphicsScene 体系，若模块内无法引入 QGraphicsWidget，需在报告中显式记录为“裁剪掉的中间基类”，并按硬约束 2 说明裁剪理由；当前未说明。

**API 缺口表**

| Qt 原型 | X 现状 |
|---|---|
| `QLegend *legend() const` | 无（仅 legendVisible bool；QLegend 类整体缺失） |
| `void zoomIn(const QRectF &rect)` | 无 |
| `void addAxis(QAbstractAxis *axis, Qt::Alignment alignment)` | `XChart_addAxis(XChart*, XValueAxis*)` 无 alignment，按 X/Y 槽位猜 |
| `QList<QAbstractAxis*> axes(Qt::Orientations, QAbstractSeries*)` | `XChart_axes(out, maxCount)` 无 orientation/series 过滤 |
| `QPointF mapToValue(const QPointF&, QAbstractSeries*)` / `mapToPosition(..., QAbstractSeries*)` | 无 series 参数 |
| `void setTitleFont(const QFont&)` / `QFont titleFont()` | 仅 family+pixelSize 子集 |
| `void setAnimationEasingCurve(const QEasingCurve&)` | 仅枚举子集，无自定义曲线 |
| `void setLocale(const QLocale&)` | BCP-47 字符串子集 |
| `void setPlotArea(const QRectF&)`（空矩形=恢复默认布局） | `XChart_setPlotArea(rect)` 不接受空矩形复位 |
| `QChart(QGraphicsItem*, Qt::WindowFlags)` | 无 parent/wFlags 构造（XObject 无 parent 体系） |
| `void plotAreaChanged(const QRectF &plotArea)` | `XChart_plotAreaChanged_signal(self)` **无载荷**（NULL args），载荷类型不符 |
| `setAxisX/setAxisY/axisX/axisY(series)`（deprecated） | 有但无 series 参数（可接受，deprecated） |

**功能缺口**
1. `createDefaultAxes()` 只把现有两轴 setRange 复位（XChart.c:487-501），Qt 按序列类型创建 QValueAxis/QBarCategoryAxis/QDateTimeAxis/QLogValueAxis 等；X 无法为柱状图建类别轴。
2. 主题：8 个主题色板为手写近似值。对比 `themes/chartthemelight_p.h`：Qt Light 序列色 `[0x209fdf,0x99ca53,0xf6a625,0x6d5fd5,0xbf593e]`，X `g_themeLight`（XChart.c:23-26）仅首色 `0x209ADF` 一致，其余全部不同；且 Qt 主题还含背景渐变、轴/网格/次网格画笔、标签画刷、阴影、outline pen（ChartThemeDark 等同样），X 只有 8 色缓存。**违反硬约束 3（样式不得精简近似）。**
3. `setTheme` 只更新色板缓存，Qt 会同步重建已有序列/轴颜色。
4. 动画（animationOptions/duration/easing）、投影（dropShadowEnabled）、本地化（localizeNumbers）均“只存属性不绘制/不生效”（XChart.h 注释自认“属性存储；渲染待后续批次”）。
5. `addSeries` 不把 `series->m_chart` 回写（XChart.c 全文无 `series->m_chart =` 赋值），`XAbstractSeries_chart()` 恒为 NULL；轴也未与序列挂接联动。
6. `removeAxis` 只置 NULL，不按 Qt 语义摘除序列挂接；`removeAllSeries` 循环 remove 可接受。
7. `scroll` 语义（域比例）与 Qt（基于 plotArea 的滚动比例）需对照 chartpresenter.cpp 复核，X 当前按轴域宽度直接乘 dx/dy。
8. 坐标映射未做 `plotArea` 为空、极坐标等边界；`chartType` 固定 Cartesian，极坐标枚举占位。

**违规**
- 主题色板近似（XChart.c:23-68 vs Qt themes/chartthemelight_p.h、chartthemedark_p.h 等）——违反硬约束 3。
- `VXChart_move` 用 `XMemcpy(self, other, sizeof(XChart))` 整对象拷贝（XChart.c:286）——违反硬约束 6（禁止 memcpy 复制对象），XObject 信号槽/虚表状态被浅拷贝。
- 字符串 API 主版本全部为 `const char*`（XChart.h:218 `XChart_setTitle(const char*)`、:299 `setTitleFont(const char*,int)`、:389 `setLocale(const char*)` 等）——违反硬约束 1/风格文档“字符串 API 主版本必须 XString*，_2 才是 UTF-8 const char* 重载”。
- 部分公开函数缺 `@return 无返回值。`（如 `XChart_addLineSeries`、`XChart_setPieSeries` 等单行注释无 @return）——违反硬约束 4（@brief/@param/@return 逐项）。

### 2. XChartView ↔ QChartView（`qchartview.h`）

**继承**
- Qt：`QChartView → QGraphicsView → QAbstractScrollArea → QFrame → QWidget`
- X：`XChartView → XWidget`
- 结论：**不 1:1**，缺 QGraphicsView/QAbstractScrollArea/QFrame 中间层（X 无滚动条语义）。

**API 缺口**
| Qt 原型 | X 现状 |
|---|---|
| `QChartView(QChart *chart, QWidget *parent)` | 无（只有默认建图表的 init/create_ex） |
| `resizeEvent(QResizeEvent*)`（protected） | 未覆写（布局在 paintEvent 现算，行为近似但非 1:1） |

**功能缺口**
1. 橡皮筋：Qt 区分 Vertical（锁定全宽只拉纵向）/Horizontal/Rectangle/ClickThrough（0x80 与前述位或，点击透传给图表项）。X 把任意非 NoRubberBand（含 0x80）当自由矩形（XChartView.c:751,765），ClickThrough 语义完全未实现。
2. 双击：Qt 双击会放大；X 的 mouseRelease 只发 clicked/released，无双击处理。
3. 命中测试 `xcv_hitTest` 用**全控件矩形**做坐标映射（XChartView.c:642-643），与绘制用 plotArea 不一致，边距/标题区存在时命中偏移。
4. 滚轮：Qt 仅在 macOS 处理滚轮（qchartview.h:52-56）；X 在所有平台启用滚轮滚动（XChartView.c:891-904）。
5. hovered 只覆盖 line/scatter/spline，饼图/柱状无悬停；pressed/clicked 同理只对 XY 序列。
6. 悬停退出发信号时载荷用 (0,0,false)（XChartView.c:799），与 Qt 的“最后一个悬停点坐标”不一致。

**违规**
- 滚轮行为与 Qt 平台语义不符（全平台生效）；`ClickThroughRubberBand` 未实现但枚举已声明“对标”——违反“行为 1:1”精神（硬约束 3）。
- `xcv_paintTitle` 用 `XStrlen(title)*4` 估算宽度居中（XChartView.c:74-75），标题字体 family/pixelSize 完全未参与渲染——渲染近似。

### 3. XAbstractSeries ↔ QAbstractSeries（`qabstractseries.h`）

**继承**：Qt `QAbstractSeries → QObject`；X `XAbstractSeries → XObject`。**1:1 ✓**（XAbstractSeries.c:20）。

**API 缺口**
| Qt 原型 | X 现状 |
|---|---|
| `void show()` / `void hide()` | 无 |
| `bool attachAxis(QAbstractAxis*)` / `bool detachAxis(QAbstractAxis*)` | X 返回 void |
| `QList<QAbstractAxis*> attachedAxes()` | 有 `attachedAxes(out,maxCount)`（可接受参数化） |
| 信号 `nameChanged() / visibleChanged() / opacityChanged() / useOpenGLChanged()` | 全部缺失（XAbstractSeries.c 未发射任何信号） |

**功能缺口**
1. `chart()` 恒 NULL：XChart_addSeries/registerSeries 不回写 `m_chart`（见 XChart 功能缺口 5）。
2. setVisible/setOpacity/setUseOpenGL/setName 均不触发对应信号。
3. attachAxis 只写 series 侧数组，axis 侧无反向登记、无图表联动。

**违规**
- 信号缺失违反硬约束 5（Qt 6.8 每个信号应有对应 *_signal 宏/回调）。
- `setName(const char*)` 主版本违反字符串 API 规则（同 XChart）。
- `XAbstractSeries_name()` 返回借用 `const char*`（内部 XString_toUtf8 缓存）作为主返回值，按风格文档应返回 `XString*` 或 `*_const` 借用接口。

### 4. XXYSeries ↔ QXYSeries（`xychart/qxyseries.h`）

**继承**：Qt `QXYSeries → QAbstractSeries`；X `XXYSeries → XAbstractSeries`。**1:1 ✓**。

**API 缺口表**（主要）
| Qt 原型 | X 现状 |
|---|---|
| `replace(const QPointF&, const QPointF&)`、`replace(int, const QPointF&)`、`replace(const QList<QPointF>&)` | 无（只有按坐标/按下标 double 版） |
| `remove(const QPointF&)`、`remove(int)` | 无（有 remove(x,y)、removeAt(index)） |
| `operator<<` | 无 |
| `enum class PointConfiguration { Color, Size, Visibility, LabelVisibility, LabelFormat }` | 无此枚举；X 仅 color/size 两键 |
| `clearPointConfiguration(int, PointConfiguration)`、`setPointConfiguration(int, PointConfiguration, QVariant)`、`clearPointsConfiguration(PointConfiguration)`、`pointConfiguration(int)`、`pointsConfiguration()` | 仅 color+size 参数化版本，缺 Visibility/LabelVisibility/LabelFormat 键与哈希语义 |
| 信号 `selectedColorChanged / pointsRemoved / penChanged / selectedPointsChanged / lightMarkerChanged / selectedLightMarkerChanged / bestFitLineVisibilityChanged / bestFitLinePenChanged / bestFitLineColorChanged / pointsConfigurationChanged / markerSizeChanged / pointLabelsFormatChanged / pointLabelsVisibilityChanged / pointLabelsFontChanged / pointLabelsColorChanged / pointLabelsClippingChanged` | 全部缺失（仅 clicked/hovered/pressed/released/doubleClicked/pointReplaced/pointRemoved/pointAdded/pointsReplaced/colorChanged 10 个） |
| `colorBy(sourceData, QLinearGradient)` | X 用双端颜色渐变参数化（可接受子集，但非 QLinearGradient） |

**功能缺口**
1. `removePoints` 不发射 Qt 的 `pointsRemoved(index,count)`（X 只对单点 remove 发 pointRemoved）。
2. `setPen/setBrush/setWidth/setMarkerSize/setPointsVisible/setPointLabels*` 等 setter 不发任何信号（Qt 均有对应 NOTIFY）。
3. 拷贝链缺陷：`VXAbstractSeries_copy` 固定 `m_type = Line`（XAbstractSeries.c:89），XXYSeries_copy 未恢复 m_type——**拷贝任意非折线派生类后 type() 变 Line**；`m_pointLabelsFontFamily` 仅 self 已有时才赋值，拷贝丢失字体族。
4. `setPointLabelsFont` 语义与 Qt 不同：Qt 整体替换 QFont，X 只在非 NULL/非 0 时部分覆盖，无法复位。
5. 渲染侧（XChartView.c）：pointsVisible 的点标记、点标签、最佳拟合线、lightMarker、selected 高亮、线宽全部未绘制——属性存了不画。
6. `colorChanged` 用 `xxy_emitIndex` 把 uint32_t 颜色转 int 载荷（XXYSeries.c:368），类型语义不符（Qt 为 QColor）。

**违规**
- 缺失信号数量最多，违反硬约束 5。
- `XXYSeries_setPointLabelsFormat(const char*)` 等字符串主版本违例（硬约束 1）。
- 拷贝不保 m_type / 丢字体族——违反硬约束 6（copy 虚函数安全）。

### 5. XLineSeries ↔ QLineSeries（`linechart/qlineseries.h`）

**继承**：Qt `QLineSeries → QXYSeries → QAbstractSeries`；X `XLineSeries → XXYSeries → XAbstractSeries`。**1:1 ✓**。
- Qt QLineSeries 自身仅构造/type()，X 以 `m_type=Line` 对齐（XLineSeries.c:33）。
- API 缺口 0，功能缺口 0。头文件缺 `@param/@return` 的单行注释问题见通用违规。
- 完整度 95%。

### 6. XSplineSeries ↔ QSplineSeries（`splinechart/qsplineseries.h`）

**继承**：Qt `QSplineSeries → QLineSeries → QXYSeries → QAbstractSeries`；X `XSplineSeries → XXYSeries → XAbstractSeries`。**不 1:1**：缺 XLineSeries 中间基类（硬约束 2 违规，XHeader 与 XSplineSeries.c:21 一致但链本身少一层）。
- 自有 API 无缺口；渲染用 Catmull-Rom 8 细分（XChartView.c:356-396），与 Qt SplineChartItem 插值思路一致但未比对具体张力。
- 完整度 85%（继承链扣分）。

### 7. XScatterSeries ↔ QScatterSeries（`scatterchart/qscatterseries.h`）

**继承**：Qt `QScatterSeries → QXYSeries`；X `XScatterSeries → XXYSeries`。**1:1 ✓**。

**API 缺口**
| Qt 原型 | X 现状 |
|---|---|
| `enum MarkerShape { Circle, Rectangle, RotatedRectangle, Triangle, Star, Pentagon }` | X 仅 Circle/Rectangle 2 值（XScatterSeries.h:23-27） |
| 信号 `borderColorChanged / markerShapeChanged / markerSizeChanged` | 无（colorChanged 由 XXYSeries 提供） |
| `setPen/setBrush/setColor` override 语义 | X 复用 XXYSeries 参数化版本，无独立覆写 |

**功能缺口**
1. 渲染：所有形状都用 `XPainter_fillRect` 画矩形（XChartView.c:296-298），Rectangle/Circle 不分，其余 4 形状无能力；`m_borderColor` 未参与绘制。
2. copy/move 未覆写：XScatterSeries 自有 `m_markerShape/m_borderColor` 不随父类拷贝/移动（XScatterSeries.c 无 VXScatterSeries_copy/move），且拷贝后 m_type 变 Line（见 XXYSeries 缺口 3）。
3. markerSize 存于 XXYSeries（Qt 在 QScatterSeries 声明），X 无 scatter 级 markerSizeChanged 信号。

**违规**：无新增（枚举/信号缺口属 API 缺口；copy 安全违反硬约束 6）。

### 8. XAreaSeries ↔ QAreaSeries（`areachart/qareaseries.h`）

**继承**：Qt `QAreaSeries → QAbstractSeries`；X `XAreaSeries → XAbstractSeries`。**1:1 ✓**。

**API 缺口**
| Qt 原型 | X 现状 |
|---|---|
| `QAreaSeries(QLineSeries *upper, QLineSeries *lower)` 构造 | 无（init 自动建 upper） |
| 信号 `clicked/hovered/pressed/released/doubleClicked/selected` | 全部缺失 |
| 信号 `colorChanged / borderColorChanged / pointLabelsFormatChanged / pointLabelsVisibilityChanged / pointLabelsFontChanged / pointLabelsColorChanged / pointLabelsClippingChanged` | 全部缺失 |

**功能缺口**
1. **copy/move 未覆写**：XAreaSeries 拥有 `m_upper`（堆上 XLineSeries）、`m_pointLabelsFormat`、`m_pointLabelsFontFamily`，父类 XAbstractSeries 的 copy/move 完全不管这些字段——拷贝共享 m_upper 指针（双释放/悬垂风险），且拷完 m_type=Line。
2. 渲染（XChartView.c:304-353）：下边界序列 `m_lower` 完全忽略；`m_brushColor` 忽略（固定用 color 填充）；每段用矩形近似梯形（Qt 绘制多边形/Path）；点标签/点标记未绘制。
3. `setPen` 同时改写 `m_color`（XAreaSeries.c:176-181），Qt pen 与 color 属性语义混叠。
4. 信号零发射。
5. 上边界序列在 `setUpperSeries` 内部释放旧对象（XAreaSeries.c:87-93），与 Qt“上边界序列由 series 拥有，替换时释放”一致，可接受。

**违规**：copy/move 未覆写导致所有权共享（硬约束 6）；渲染精简近似（硬约束 3）；信号缺失（硬约束 5）。

### 9. XAbstractBarSeries ↔ QAbstractBarSeries（`barchart/qabstractbarseries.h`）

**继承**：Qt `QAbstractBarSeries → QAbstractSeries`；X `XAbstractBarSeries → XAbstractSeries`。**1:1 ✓**。

**API 缺口表**（结构性）
| Qt 原型 | X 现状 |
|---|---|
| `bool append(QBarSet*)`、`append(QList<QBarSet*>)`、`insert(int, QBarSet*)`、`remove(QBarSet*)`、`take(QBarSet*)`、`QList<QBarSet*> barSets()` | X 改为 `append(label,value)`/`insert(index,label,value)`/`remove(index)`/`take(index,char**)`/`barSets(double* out)`——**QBarSet 对象语义整体缺失，多组柱无法表达** |
| `enum LabelsPosition { LabelsCenter=0, LabelsInsideEnd=1, LabelsInsideBase=2, LabelsOutsideEnd=3 }` | X 用 int 0/1/2，注释为“0 居中/1 外侧/2 内侧”，**数值语义与 Qt 不一致**（Qt 1=InsideEnd、2=InsideBase） |
| 信号 `clicked/hovered/pressed/released/doubleClicked(index, QBarSet*)`、`labelsPositionChanged/labelsAngleChanged/labelsPrecisionChanged`、`barsetsAdded/barsetsRemoved` | 全部缺失；X 仅有 countChanged/labelsVisibleChanged/labelsFormatChanged |

**功能缺口**
1. 单组柱模型：Qt 一序列 = 多 QBarSet（每组一值列），X 是一维 values+categories（等于 1 个 QBarSet 内嵌进 series），无法实现 Stacked/Percent/多组柱。
2. 柱标签：labelsVisible/labelsFormat/labelsAngle/labelsPosition/labelsPrecision 全部只存值，XChartView 绘制柱体时不画标签（XChartView.c:247-277）。
3. `take` 返回 `char*` 由调用方释放（XAbstractBarSeries.c:307-323），违反字符串返回规范（应 XString*）。
4. copy 不复制 labelsPosition/labelsPrecision；`labelsFormatChanged` 声明了但 setLabelsFormat 不发射。

**违规**：LabelsPosition 数值与 Qt 不符（硬约束 1 API 一致性）；QBarSet 语义缺失（硬约束 2 继承/结构 1:1 未达标）；信号缺失（硬约束 5）；`take` 的 char* 输出（硬约束 1）。

### 10. XBarSeries ↔ QBarSeries（`barchart/vertical/bar/qbarseries.h`）

**继承**：Qt `QBarSeries → QAbstractBarSeries`；X `XBarSeries → XAbstractBarSeries`。**1:1 ✓**。

**API 缺口**
- Qt QBarSeries 自有 API 仅构造/type()。X 多出 `XBarSeries_setColor/color`——**该 API 在 Qt 中属于 QBarSet 而非 QBarSeries**，命名与 Qt 语义冲突（硬约束 7：与 Qt 冲突的命名不应并存）。
- 信号：QBarSeries 无自有信号，OK。

**功能缺口**
1. copy/move 未覆写：`m_color` 不随拷贝/移动（XBarSeries.c 无 VXBarSeries_copy/move）。
2. `setColor` 不触发任何信号（Qt QBarSet::colorChanged 语义）。

**违规**：`XBarSeries_setColor` 与 Qt API 语义冲突（硬约束 7）；copy 安全（硬约束 6）。

### 11. XPieSeries ↔ QPieSeries（`piechart/qpieseries.h`）

**继承**：结构体 `XPieSeries → XAbstractSeries` 与 Qt 一致，**但 `XPieSeries_class_init` 声明 `XVTABLE_INHERIT_XCLASS(XObject)`**（XPieSeries.c:78），与头文件 `XCLASS_DEFINE_EXTEND_END(XPieSeries, XAbstractSeries)` 不一致——虚表父类与结构体布局不符（硬约束 2/虚表一致性违规）。

**API 缺口**
| Qt 原型 | X 现状 |
|---|---|
| `bool append(const QList<QPieSlice*>&)`、`operator<<` | 无 |
| 信号 `added(const QList<QPieSlice*>&)`、`removed(const QList<QPieSlice*>&)` | X 逐个切片发单指针载荷，类型不符 |
| `countChanged/sumChanged` | 有（空参，OK） |

**功能缺口**
1. **move 丢 name**：`VXPieSeries_move` 先 parent move（把 other->m_name 移入 self），随后 `VXPieSeries_deinit(self)` 把 self->m_name 释放（XPieSeries.c:156-177）——移动后目标序列名丢失、轴挂接丢失。
2. copy 深拷贝切片仅复制 label/value（XPieSeries.c:143-153），切片颜色/字体/explode/画笔全部丢失；且 parent copy 后 m_type=Line 未恢复。
3. 渲染（XChartView.c:158-230）：`m_holeSize`（环图）、`m_horizontalPosition/m_verticalPosition/m_pieSize`、`m_pieEndAngle`（固定按 360° 摊分）、labelPosition/labelArmLengthFactor 全部未参与绘制；标签只画在半径 0.65 处。
4. `XPieSeries_updateAngles` 直接调用切片信号函数发射（XPieSeries.c:409-413），绕过 series 自身信号体系，且依赖切片 m_signalSlot。
5. `setLabelsVisible/setLabelsPosition` 不回写 m_labelsVisible 之外的切片状态一致性（位置无查询）。

**违规**：虚表父类声明与结构体不一致（XPieSeries.c:78）；move 语义破坏（硬约束 6）；渲染近似（硬约束 3）。

### 12. XPieSlice ↔ QPieSlice（`piechart/qpieslice.h`）

**继承**：Qt `QPieSlice → QObject`；X `XPieSlice → XObject`。**1:1 ✓**。

**API 缺口**
| Qt 原型 | X 现状 |
|---|---|
| `QPieSeries *series() const` | 无 |
| 信号 `penChanged / brushChanged / labelBrushChanged / labelFontChanged / borderWidthChanged / labelColorChanged` | 缺失（labelColorChanged 是 Qt 信号但 X 无）；X 有 labelChanged/valueChanged/labelVisibleChanged/colorChanged/borderColorChanged/percentageChanged/startAngleChanged/angleSpanChanged/clicked/hovered/pressed/released/doubleClicked |

**功能缺口**
1. **`VXSlice_move` 用 `XMemcpy(self, other, sizeof(XPieSlice))` 整对象拷贝**（XPieSlice.c:137）——硬约束 6 明确禁止，XObject 与 XString 缓存被浅拷贝，双释放风险。
2. `XPieSlice_setValue` 顺带把 `m_penColor/m_brushColor/m_labelBrushColor` 重置、`m_labelFontFamily` 直接置 NULL（XPieSlice.c:161-171）——Qt 只改 value；且已分配的字体族 XString 被直接丢弃（**内存泄漏**）。
3. `setExploded/setLabelPosition/setBorderWidth/setLabelColor` 等不发信号（Qt 有 NOTIFY 的项缺）。
4. 渲染：explode 距离、labelArmLengthFactor、labelPosition 的 4 种布局、labelBrush、labelFont 未参与绘制（XChartView.c 只用 color/labelVisible/explode 距离）。
5. `XPieSlice_pen` 只输出 color/width，Qt pen 还有样式/透明度等——参数化可接受，但 `penColor/brushColor` 等无头文件声明（XPieSlice.c:382-395 实现未声明于头文件，属未公开实现泄漏）。

**违规**：memcpy 复制对象（XPieSlice.c:137，P0）；setValue 副作用重置样式并泄漏（XPieSlice.c:161-171，P0 功能）；信号缺失（硬约束 5）。

### 13. XValueAxis ↔ QValueAxis（`axis/valueaxis/qvalueaxis.h`）

**继承**
- Qt：`QValueAxis → QAbstractAxis → QObject`
- X：**裸 struct，无 XObject、无 XCLASS_DEFINE、无 class_init、无虚表、无 deinit**（XValueAxis.h:21-30）
- 结论：**不 1:1**，QAbstractAxis 中间基类缺失（用户重点提示项）。

**API 缺口**
| Qt 原型 | X 现状 |
|---|---|
| 全套 QAbstractAxis API（约 40 个：setVisible/show/hide/setLineVisible/setLinePen/网格/次网格/标签/标题/阴影/reverse/labelsEditable/truncate 等 + 24 个信号） | 全部缺失 |
| `setMin(qreal)` / `setMax(qreal)` | 无（仅 setRange） |
| `setMinorTickCount/minorTickCount` | 无 |
| `setTickAnchor/tickAnchor`、`setTickInterval/tickInterval`、`setTickType(TicksDynamic/TicksFixed)` | 无 |
| `applyNiceNumbers()` 槽 | 无 |
| 信号 `minChanged/maxChanged/rangeChanged/tickCountChanged/minorTickCountChanged/labelFormatChanged/tickIntervalChanged/tickAnchorChanged/tickTypeChanged` | 全部缺失 |

**功能缺口**
1. **无 deinit**：`XValueAxis_init` 分配 `m_labelFormat/m_titleText` 两个 XString，XChart_deinit 直接 `XFree_System` 释放结构体（XChart.c:208-209）——**XString 内存泄漏**，违反 init/deinit_base 成对（硬约束 6）。
2. 默认值不符：Qt 默认 min=0、max=0、tickCount=5、minorTickCount=0（qvalueaxis.cpp:421-424）；X 默认 0..10、tickCount=6（XValueAxis.c:12-15）。
3. 无任何信号。
4. labelFormat 用 printf `%g`（XValueAxis.c:16），Qt 用 QLocale/nice numbers 生成标签。
5. 渲染（XChartView.c:81-125）：轴标题 m_titleText 不画；m_visible 不参与；无次网格/阴影/标签角度/标签画刷/截断；网格仅在 tickCount>1 时画。

**违规**：init 无 deinit 成对（硬约束 6）；字符串 API const char* 主版本（硬约束 1）；QAbstractAxis 中间基类缺失（硬约束 2）；信号缺失（硬约束 5）。

### 14. XCategoryAxis ↔ QCategoryAxis（`axis/categoryaxis/qcategoryaxis.h`）

**对标裁决（用户重点）**：
- 头文件名义“对标 Qt Charts 6.8 QCategoryAxis”（XCategoryAxis.h:3）。
- **实际语义是 QBarCategoryAxis 的子集**：QCategoryAxis 的核心是 `append(label, categoryEndValue)` + `startValue/endValue/setStartValue`（每个类别带数值域，用于折线/面积图的类别轴，qcategoryaxis.cpp 的 append 需要 endValue 递增校验）；X 实现只有“标签字符串列表 + 下标即位置”（XCategoryAxis.c:16-36），这正是 QBarCategoryAxis（`append(category)` + `categories()/at()/count()`，axis/barcategoryaxis/qbarcategoryaxis.h）的语义。
- 同时 X 又缺 QBarCategoryAxis 的 `setCategories/min/max/setRange/countChanged` 等 API，且 **XChart 只接受 XValueAxis*，XCategoryAxis 无法挂到图表**（XChart.h:144-145、XChart.c:912-940），目前是孤立未接线类。
- 结论：**名义对标 QCategoryAxis，实现对标（且不完整地）QBarCategoryAxis；两者都不达标**。

**API 缺口**
| Qt 原型 | X 现状 |
|---|---|
| QAbstractAxis 全套（同 XValueAxis） | 缺失 |
| `append(const QString &label, qreal categoryEndValue)` | X 只有 append(label)，无 endValue |
| `startValue(const QString&)/setStartValue(qreal)`、`endValue(const QString&)` | 无 |
| `remove(const QString&)`、`replaceLabel(old,new)`、`categoriesLabels()` | 无 |
| `labelsPosition()` / `setLabelsPosition(AxisLabelsPosition)`（Center/OnValue） | 无 |
| 信号 `categoriesChanged()`、`labelsPositionChanged(...)` | 无 |
| （QBarCategoryAxis 视角）`setCategories/append(QStringList)/insert/replace/clear/min/max/setRange/countChanged` | 无 |

**功能缺口**
1. 类别无值域语义，无法表达刻度位置（QCategoryAxis 的核心能力缺失）。
2. 无 deinit：`m_categories` 的 XString 无释放路径（与 XValueAxis 同类泄漏）。
3. 未接入 XChart/XChartView，`m_gridVisible` 无 setter，渲染侧完全未使用。
4. 无信号。
5. 类别轴与柱状图（XBarSeries）无联动：柱状图渲染直接按 `i+0.5` 猜测类别位置（XChartView.c:264），没有类别轴刻度参与。

**违规**：继承缺失（硬约束 2）；init/deinit 不成对（硬约束 6）；信号缺失（硬约束 5）；名义/实际对标不一致（硬约束 1 命名与语义一致性）。

---

## 三、模块级硬约束合规核查

| # | 约束 | 状态 | 说明 |
|---|---|---|---|
| 1 | 拥有型字符串 XString*，API 主版本 XString*、_2 为 UTF-8 重载 | **违反** | 全部 14 个类的字符串入参/返回均以 `const char*` 为主版本（XChart_setTitle、XAbstractSeries_setName、XXYSeries_setPointLabelsFormat、XPieSlice_setLabel、XValueAxis_setTitleText、XAbstractBarSeries_append 等），无 XString* 主版本与 _2 转发；XAbstractBarSeries_take 还输出 char*。存储侧用 XString* 正确。 |
| 2 | 继承一比一含中间基类 | **违反** | QAbstractAxis 缺失（XValueAxis/XCategoryAxis 裸 struct）；QGraphicsWidget/QGraphicsView 中间层缺失（XChart/XChartView）；QSplineSeries 缺 XLineSeries 中间层；XPieSeries class_init 虚表父类与结构体不一致。 |
| 3 | 样式/绘制不得精简近似 | **违反** | 主题色板与 Qt 不符（仅首色相同）；标题宽度估算；散点全画矩形；面积矩形近似；柱标签/点标签/点标记/最佳拟合线/holeSize/labelsPosition/轴标题/次网格/阴影等“只存不画”。 |
| 4 | 公共头中文 Doxygen（@brief/@param/@return） | **部分违反** | 多数函数有 @brief，但大量单行注释缺 `@return 无返回值。`（如 XChart.h:214,217,432-457；XAbstractBarSeries.h:230-237 等）；XValueAxis.h/XCategoryAxis.h 无文件级 @file 注释（XCategoryAxis.h 有）。所有 .h/.c 均带 UTF-8 BOM ✓。 |
| 5 | 信号（空参 args=NULL；Qt 6.8 信号全覆盖） | **违反** | 空参信号部分用 args=NULL（XPieSeries/XPieSlice 正确），但 XXYSeries 的 xxy_emitVoid 用 `XVarList_create(0)` 非 NULL；Qt 信号大面积缺失（QAbstractSeries 4 个、QXYSeries 16 个、QAbstractBarSeries 9 个、QAreaSeries 13 个、QPieSlice 6 个、轴类全部）；plotAreaChanged 载荷缺失。 |
| 6 | 生命周期（init/deinit 成对；copy/move 安全；禁 memcpy 复制对象；禁直接 malloc/free/strdup） | **违反** | XValueAxis/XCategoryAxis 无 deinit（XString 泄漏）；VXChart_move、VXSlice_move 用 XMemcpy 整对象复制（P0）；XAreaSeries/XBarSeries/XScatterSeries 无自有 copy/move 覆写导致字段丢失/指针共享；XPieSeries move 丢 name；拷贝后 m_type 一律变 Line。内存 API 使用（XMemory_malloc/XMalloc_System/XFree_System/XRealloc_System）合规 ✓，未见裸 malloc/free/strdup。 |
| 7 | 旧 API 不保留 | **通过（需注意）** | 未见旧 API 双轨；但 `XBarSeries_setColor` 与 Qt 语义冲突（颜色属于 QBarSet），`XChart_series(index)` 与 Qt `series()` 返回列表的命名语义不同，建议评审。 |
| 8 | 新代码 C99 | **通过** | 复合字面量 `&(XRect){...}`、中块声明均为 C99 合法，未见 C++/C11 语法。 |

---

## 四、缺失类清单（Qt 范围内 XGui 完全没有）

| Qt 类 | Qt 头文件（相对 qtcharts/src/charts） | 建议 |
|---|---|---|
| QAbstractAxis | axis/qabstractaxis.h | **必须实现**（硬约束 2 的中间基类，XValueAxis/XCategoryAxis 改继承它） |
| QBarCategoryAxis | axis/barcategoryaxis/qbarcategoryaxis.h | 实现（柱状图类别轴；或与 XCategoryAxis 合并裁决） |
| QDateTimeAxis | axis/datetimeaxis/qdatetimeaxis.h | 实现 |
| QLogValueAxis | axis/logvalueaxis/qlogvalueaxis.h | 实现 |
| QColorAxis | axis/coloraxis/qcoloraxis.h | 暂缓（依赖 QXYSeries 点配置扩展） |
| QBarSet | barchart/qbarset.h | **必须实现**（QAbstractBarSeries 数据结构 1:1 的前提） |
| QStackedBarSeries | barchart/vertical/stacked/qstackedbarseries.h | 实现（在 QBarSet 之后） |
| QPercentBarSeries | barchart/vertical/percent/qpercentbarseries.h | 实现 |
| QHorizontalBarSeries | barchart/horizontal/bar/qhorizontalbarseries.h | 实现 |
| QHorizontalStackedBarSeries | barchart/horizontal/stacked/qhorizontalstackedbarseries.h | 实现 |
| QHorizontalPercentBarSeries | barchart/horizontal/percent/qhorizontalpercentbarseries.h | 实现 |
| QBoxPlotSeries | boxplotchart/qboxplotseries.h | 暂缓（二期） |
| QBoxSet | boxplotchart/qboxset.h | 暂缓（二期） |
| QCandlestickSeries | candlestickchart/qcandlestickseries.h | 暂缓（二期） |
| QCandlestickSet | candlestickchart/qcandlestickset.h | 暂缓（二期） |
| QPolarChart | qpolarchart.h | 暂缓（XChart 枚举已留位） |
| QLegend | legend/qlegend.h | 实现（XChart 目前只有 legendVisible bool；附 alignment/markers/detach 语义） |
| QLegendMarker 及派生（QXYLegendMarker/QAreaLegendMarker/QBarLegendMarker/QPieLegendMarker/QBoxPlotLegendMarker/QCandlestickLegendMarker） | legend/qxylegendmarker.h、legend/qarealegendmarker.h、legend/qbarlegendmarker.h、legend/qpielegendmarker.h、legend/qboxplotlegendmarker.h、legend/qcandlesticklegendmarker.h | 随 QLegend 实现 |
| QXYModelMapper / QHXYModelMapper / QVXYModelMapper | xychart/qxymodelmapper.h、xychart/qhxymodelmapper.h、xychart/qvxymodelmapper.h | 暂缓（数据绑定层） |
| QBarModelMapper / QHBarModelMapper / QVBarModelMapper | barchart/qbarmodelmapper.h、barchart/qhbarmodelmapper.h、barchart/qvbarmodelmapper.h | 暂缓 |
| QPieModelMapper / QHPieModelMapper / QVPieModelMapper | piechart/qpiemodelmapper.h、piechart/qhpiemodelmapper.h、piechart/qvpiemodelmapper.h | 暂缓 |
| QBoxPlotModelMapper / QHBoxPlotModelMapper / QVBoxPlotModelMapper | boxplotchart/qboxplotmodelmapper.h、boxplotchart/qhboxplotmodelmapper.h、boxplotchart/qvboxplotmodelmapper.h | 暂缓（随 BoxPlot） |
| QCandlestickModelMapper / QHCandlestickModelMapper / QVCandlestickModelMapper | candlestickchart/qcandlestickmodelmapper.h、candlestickchart/qhcandlestickmodelmapper.h、candlestickchart/qvcandlestickmodelmapper.h | 暂缓（随 Candlestick） |

注：Qt 主题类（ChartThemeLight/Dark/...）在 Qt 6.8.3 中均为私有类（themes/*_p.h），无公开 API，不列入缺失类；但其**色板/笔刷数据**必须 1:1 复刻（见 XChart 功能缺口 2）。QGraphicsWidget/QGraphicsView 属 QtWidgets/QtGui 范畴，不在本模块清单内，但 XChart/XChartView 的中间基类缺失需在模块设计中给出裁剪说明。

---

## 五、优先任务建议（按优先级）

1. **P0-1 补 XAbstractAxis 并重构轴体系**：新建 XAbstractAxis（XObject 派生，含 visible/line/grid/labels/title/shades/reverse 等 QAbstractAxis API 与信号），XValueAxis 改继承并补 deinit_base/信号/默认值（Qt 默认 0..0/5）；同时消除 XString 泄漏。
2. **P0-2 裁决 XCategoryAxis 并接线**：若服务柱状图 → 按 QBarCategoryAxis 对齐（categories/min/max/setRange/countChanged）并让 XChart_addAxis 接受；若服务折线/面积 → 按 QCategoryAxis 对齐（append(label,endValue)/startValue/endValue/labelsPosition）。当前“QBarCategoryAxis 语义 + QCategoryAxis 名义”必须二选一。
3. **P0-3 修生命周期违禁**：删除 VXChart_move/VXSlice_move 的 XMemcpy 整对象复制，改字段级 move；XAreaSeries/XBarSeries/XScatterSeries 补 copy/move 覆写；所有 copy 路径恢复 m_type 并深拷贝自有字段。
4. **P0-4 字符串 API 合规化**：全部 `const char*` 主版本改为 `XString*` 主版本 + `_2` UTF-8 转发（setTitle/setName/setPointLabelsFormat/setLabel/setTitleText/append 等），返回按 XString* 或 *_const 借用。
5. **P1-5 主题与渲染 1:1**：按 Qt themes/charttheme*_p.h 复刻 8 主题的序列色/背景渐变/轴网格笔刷/阴影/outline，setTheme 应用到已有序列；补齐点标记/点标签/最佳拟合线/线宽/柱标签/环图 holeSize/labelsPosition/轴标题/次网格等“只存不画”项。
6. **P1-6 补全信号**：QAbstractSeries 4 信号、QXYSeries 缺失 16 信号、QAbstractBarSeries/QAreaSeries/QPieSlice 交互与属性信号，setter 全部触发；plotAreaChanged 携带绘图区载荷。
7. **P1-7 数据模型 1:1**：引入 QBarSet（含 values/pen/brush/label/选择/信号），QAbstractBarSeries 改为 QBarSet 集合语义；LabelsPosition 枚举按 Qt 4 值对齐。
8. **P2-8 交互对齐**：橡皮筋 Vertical/Horizontal 锁轴与 ClickThrough 透传、双击缩放、命中测试改用 plotArea、滚轮按 Qt 平台语义；series 挂载回写 m_chart 与轴关联。
9. **P2-9 后续批次**：QBarSet 之后实现 Stacked/Percent/Horizontal 柱族；再实现 QDateTimeAxis/QLogValueAxis、QLegend/QLegendMarker、QBoxPlotSeries/QCandlestickSeries、QPolarChart；ModelMapper 族最后按需实现。
10. **P2-10 收尾**：Doxygen 补齐（@return 逐返回值）、XChart 继承链裁剪说明文档化、XChartView wheel 行为决策记录。

---

*本报告为只读审计，未修改 Src/、Test/ 任何文件，未执行 commit/push。*
