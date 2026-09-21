# Qt 对齐战役批次记录（14.120~14.126 + 串行批次 3~26 前置）

> 归档自 XGui.md §2026（2026-09-21 文档重构迁移，内容逐字保留）。后续更新见 XGui.md 当前版。

### 14.120 XLineControl 清单验收轮（2026-09-19 深夜）

**新增验收测试**:Test/XGuiTest/XLineControlAcceptance.{c,h} +
xgui_linecontrol_acceptance_test.c + CMake 目标
XLineControl_Acceptance_Test(ctest: XLineControlAcceptance),逐条对照
linecontrol-checklist.md 第 4 节 64 断言;信号断言为按序子序列+缺失
匹配,UTF-16 语义按 UTF-8 字节/字符口径换算并注明。

**验收驱动修复(控制器)**:①xlc_bufAssign 空文本初始化对 NULL 缓冲
解引用(零初始化对象 init(txt="") 即崩);②xlc_maskString 的
xlc_str fill 未 strInit 即 realloc(垃圾栈值,ASan 实证);③text()
掩码模式改返回剥离占位的 m_textReturn(对齐清单"剥 blank 留分隔符");
④copy/paste 平台剪贴板不可用时回退 XTextClipboard 共享层;⑤密码
passwordMaskDelay 明文窗口改 m_passwordEchoEditing 双承载(定时器+
标志,无事件循环环境独立成立),timerEvent 到期复位;⑥undo/redo
尾部提前发射 cursorPositionChanged 移除(由 finishChange 统一收尾,
对齐 Qt 次序);⑦internalInsert 两处 inputRejected 改挂起标志,
finishChange 在 text 系信号后补发;⑧keyboardScheme 缺省改 X11
(对齐部署平台 Qt 口径,原 Windows 值覆盖问题一并修正);⑨新增
XLineControl_displayText() 借用查询(验收与壳层同源需要)。

**验收驱动修复(测试)**:validator 令牌契约(非 NULL 才挂钩)、
"a\x01"+"b" 字面量拆分(\x 转义吞字符)、清单笔误对齐 Qt 实测
(B6 光标 1、E8 词跳 6/0、D3/D8/E13/E15 语义校正)、掩码统一 ";_"
显式 blank。

**现状**:44/64 断言通过;A 回显/B 撤销/D IME/E 杂项四组全绿,
主回归全绿不受影响;C 掩码组 20 项失败根因收敛为
internalInsert 掩码分支每次击键净增一空槽(替换语义缺失)+
stripString/clearString 槽位口径,为下一轮独立深挖项。

**合规自查**:按约束文档核对——内存统一 XMemory API、
init/deinit_base 成对、XCopy 深拷贝(2026-09-08 裁定)、setFont
条件发射符合"绘制期间 update 行为"审查底线、ac_open 守卫符合
"deinit+init 删除需已初始化"底线、C99、git diff --check 干净、
未提交 Git。

### 14.124 绘制分发脏区语义根修 + XLineControl 光标/度量对拍（2026-09-19 晚）

**§14.123 步骤①~④ 全部落地,14.122 残缺根因闭环。**

**④ 绘制分发根修(XWidget.c paintTree)**:机制坐实——
XPaintEvent_init 取脏区 boundingRect 作 PAINT 矩形,父级按自身
paint rect 涂写(样式面板/autoFillBackground/demo 静态 tile
memcpy)会把外接框内后代旧像素一并抹掉,而 paintTree 只按区域
各矩形重绘相交后代,夹缝像素被抹后无人重画(残缺持续到其后代
下次自我更新)。修复:paintTree 在遮罩裁剪后若区域仍为多矩形则
**逐矩形拆分递归派发**,每棵子树的 PAINT 恒携带单一矩形(外接==
矩形本身),父级可涂写范围与后代重绘范围(R∩childRect)严格闭合;
规范区域经 region_add_rect 折叠不重叠,拆分递归可证终止。

**① syncControlFont→redoTextLayout 链路**:链路本身健全
(paint/命中/cursorRect 前均同步,setFont 内恒经 redoTextLayout),
但 **XLineControl_setFont 强制发射 displayTextChanged** 与壳的
paintEvent 内同步叠加成 paint→update→PAINT 自激重绘风暴
(每帧必再投递)。修复:setFont 改 xlc_updateDisplayText(false)——
重排照做,显示文本真变化才发信号;对齐 XTextControl_setFont
已有的同型守卫(条件 emit)。

**② 光标相位焦点门(XLineControl_draw)**:blinkStatus 之外叠加
宿主经 Cursor 旗标下发的焦点门(cursorPhase = Cursor 旗标 &&
blinkStatus && !hideCursor),光标竖线、掩码反选格、反选字符
配色统一受门控——HEAD 语义"焦点内常显、失焦无光标/无掩码反
选"精确成立;壳 focusIn 置位/focusOut 旗标自动消失,无需新增
控制器状态。壳 focusIn"置位闪烁相位不启用定时器"保常态显不变。

**③ 行盒公式对拍(xlc_redoTextLayout)**:layoutLineHeight 由
XPainter_textHeight(表 m_height,outline 表含 lineGap 且整体取整)
改为 HEAD 公式 **ascent+descent 逐项度量相加(下限 14)**——消除
光标/选区比壳行盒高出一截的"光标不对"几何根源;layoutAscent
本就与 HEAD baseline=ty+ascent 同源,无需改。

**demo 临时缓解撤销**:14.122 的"交互后全窗标脏"两处写入点
(textChanged/滑块联动槽)已还原为 XLabel_setText_2 自身整块
update(静态场景不含标签文本,根 tile 只需按标签脏区恢复背景),
autotest 全绿证明④根修独立成立,每击键全窗重绘的开销消除。

**验证**:主回归 exit=0 全绿;XLineControl 验收 64/64;demo
autotest 9/9 断言+总判定 PASS(撤销缓解后复跑);实机 xdotool
复现 14.122 序列(resize 522x445→切输入演示页)微调框/滑块/
进度条全部完整,跨页无渗漏,交互后截图像素级巡检正常;构建零
错误,git diff --check 干净;未提交 Git。

**下轮建议**:textcontrol-checklist 58 断言脚手架;回归文件泄漏
族分批补删除;XPlainTextEdit 实机键入场景入 demo autotest。

**14.124 补充(同日晚,用户复验指认光标仍错,中文尤甚)**:HEAD
对拍定位两处独立几何缺陷。①xlc_layoutCursorToX 对布局位置**无
条件 prevBoundary**——恰在边界的位置也整体回退一字符,光标/选区
末端系统性偏左一字(中文双宽 16px 最显眼,ASCII 也错 8px;迁移前
displayWidth 逐字符累计无此病)。②组合光标**二次偏移**:
mapTextToLayout 已把 ≥ 插入点的位置整体后移 preeditLen(落到组合
串尾),draw/cursorToXCurrent/rectForPos 三处再叠加 m_preeditCursor,
越过组合串尾后被 prevBoundary 钳回倒数第一字符之前;且该字段默认
存字节长、事件 cursorPosition 存字符序号,单位混用(中文 3 字节/
字符放大漂移)。HEAD 基线无 preedit 显示(IME 确认后整串插入),
仅凭 displayWidth(charCountPrefix) 定位,故无此二病。

**修复**:①新增 xlc_layoutSnapBoundary(边界保位、仅字符中间回退,
at-or-before)替换无条件 prevBoundary;②新增 xlc_cursorLayoutPos
(组合中且光标在插入点 = preeditLayoutPos+组合内字节偏移,否则
mapTextToLayout),三处消费点统一换用;③m_preeditCursor 单位统一
为存储组合串内**字节偏移**:默认=存储串长(Password 读掩码转写串),
事件 cursorPosition 字符序号经 seqLen 逐步换算;头文档同步
(cursorToXCurrent 公式、字段注释)。组合期间点击命中(xToPos 返回
布局坐标)与 moveCursor(文本坐标)的口径差已知未修——组合中点击
通常先由 IME 提交,列为下轮项。

**验收扩充(D9~D12,64→68)**:D9 中文组合默认光标=组合串尾、
D10 cursorPosition=1 落首字符之后、D11 中文提交后端点=整串宽、
D12 ASCII 端点不回退——修复前四条全部失败,防回归。

**验证**:主回归 exit=0;验收 68/68;demo autotest 9/9 PASS;构建
零错误;未提交 Git。

**14.124 补充二(同日晚,多行编辑页进编辑段错误)**:实机 gdb 捕获
栈溢出式无限递归——VXTextControl_objectEvent(EXObject_Event 重载)
内部调 XObject_event_base,该助手按最派生虚表**重新取回本函数**,
自递归直至栈顶触底;进入编辑 focusIn 启动光标闪烁定时器,首个
定时器事件经 EXObject_Event 派发即引爆(多行编辑页此前无任何自动
覆盖)。修复:改经 XClass_Parent(XObject, EXObject_Event,…) 调父类
实现(与 XWidgetWindow/XLabel/VXFrame 等同槽位既有正确写法对齐);
全树排查其余 EXObject_Event 重载均无同型病,外部对其它对象调
XObject_event_base 属合法派发。实机复验(gdb 常驻):进多行编辑页
→点击进入编辑→闪烁定时器持续运行→页签往返,零崩溃(修复前同
路径秒崩);主回归 exit=0、验收 68/68 不受影响。下轮建议增列:
demo autotest 全页签遍历+编辑聚焦脚本(本缺陷类无覆盖)。

**14.124 补充三(同日晚,多行编辑 Delete 无效)**:用户指认"单行
正常、多行删除键无效"。gdb 四点追踪(shell 键值/eraseCodepoint/
控制器 Delete 行/editRemove)定位:**NumLock 经 Mod2Mask 全键表
泄漏 KeypadModifier**——posix 平台 translateModifiers 对任何键
逢 Mod2 即加 Keypad 位,NumLock 开启(桌面常态)时主键区
Backspace/Delete/方向键全带小键盘位;多行壳的码点补偿门
(`(mods & ~Shift)==0`)与控制器 xtc_plainMods 均判非"无修饰",
Delete 分支整体跳过、事件被静默忽略(Return/字母路径不查该位
故存活——与实测"Return 能插入、Delete 无效"完全吻合)。单行
"正常"系 XLineControl 快捷键路由个别分支已显式容忍
`mods==KeypadModifier`(3679-3681),非真正免疫。

**修复(三层)**:①平台根修(posix translateModifiers 增 keysym
参)——KeypadModifier 仅当键本身来自小键盘(keysym∈0xff80..0xffbd,
XK_KP_Space..XK_KP_Equal)时置位,鼠标路径传 NoSymbol;②控制器
兜底:xtc_plainMods 忽略 Keypad(对齐 Qt 编辑键判断口径);③壳
门控同步(XPlainTextEdit 键控位掩码加 Keypad)。win32 后端本就按
VK 小键盘键逐个置位,无需改。

**验证**:gdb 实机追踪修复后全链路——`[SHELL] key=0x01000007
(XKey_Delete) mods=0`→eraseCodepoint 进入→editRemove(pos=29,
len=3) 整码点删除,截图确认"第三行"→"第三";主回归 exit=0;
验收 68/68;git diff --check 干净;未提交 Git。下轮建议增列:
textcontrol 验收脚手架补 Backspace/Delete/行尾跨行拼接断言
(本轮经实机验证,控制器静态分支逻辑本身正确)。

**14.124 补充四(同日晚,右键菜单两缺陷)**:①单行右键菜单位置
错——dispatchPointerEvent 合成 CONTEXT_MENU 时 local 取传播循环
改写后的"最后接收者"坐标,global 又把该坐标按 target 本地系换算,
两套坐标系混用双重偏移,菜单弹在光标下方约一个页面位移处(实测
光标 (150,163)、菜单落 (190,320))。②多行右键无菜单——childAt
对多行页命中滚动区内部 viewport(无 ContextMenuEvent 槽的内部
XWidget),合成事件发给它即被静默吞掉,编辑器收不到;指针比对
实锤(dispatch self ≠ &m_plainEdit,vtable ≠ XPlainTextEdit 表)。
**修复**(XWidget.c 合成块重写):local 逐接收者换算(pos −
accumulateOffset(w)),global 统一 mapToGlobal(top, pos);ctx 事件
默认 ignore、未显式接受则沿父链继续投递(Qt 语义:被忽略的
QContextMenuEvent 交父级处理),viewport/页容器等无槽控件不再吞
事件,编辑器壳 contextMenuEvent(→XTextControl/XLineEdit_
createStandardContextMenu)可正常弹出。临时探针全部移除。

**验证**:实机截图——单行菜单弹在光标处(撤销/重做/剪切/复制/
粘贴/删除/全选全项)、多行右键菜单正常弹出;主回归 exit=0;验收
68/68;git diff --check 干净;未提交 Git。

**14.124 补充五(同日晚,弹出菜单点外部不关闭)**:用户实测多行
右键菜单弹出后,点击菜单外部有时不关闭。实机定位两个叠加因素:
①**XMenu_actionAt 只查 Y 不查 X**——凡点击的 y 落在某条目行高
之内(不管 x 在菜单宽度内外),都判为"点在条目上"→菜单保持打开。
配合弹出菜单与编辑器同屏重叠的布局,点击菜单左右两侧之外、高度
恰在条目行内的位置即复现"有时不关闭"(点击 y 在条目行外则正常
关闭,故呈随机性);②重定向坐标:跨顶层抓取redirect 按
mapToGlobal(top,pos)→mapFromGlobal(menuTop,global) 换算,实测
送达坐标正确(点击 (350,300)→菜单本地 (200,85)),不是根因。
**修复**:actionAt 补 X 范围检查(pos.x ∈ [rect.x, rect.x+width))
——x 越界即返回 NULL→VXMenu_mousePressEvent 判"菜单外点击"→
xmenu_close。悬停高亮(mouseMove 同用 actionAt)一并修正。

**验证**:实机两轮——弹出菜单→点菜单外 (350,300) 菜单正确关闭;
重新弹出→点"剪切"(无选区禁用项)菜单保持、无误触发;主回归
exit=0;验收 68/68;git diff --check 干净;未提交 Git。

**14.124 补充六(同日晚,远端帧数优化合并)**:拉取远端
codex/xdevice-file-platform 的 28c41c1c(绘制层性能专项:字形三级
缓存/线段预裁剪/半透明整段混合/零拷贝 DIB 上屏,增量口径
1.75x~3.90x)快进合并,本地 WIP(文本控制器迁移+本轮四项修复)先
stash 后恢复,未提交。合并处理:①XPlainTextEdit.c paintEvent
冲突——远端在旧自绘路径追加"滚动视口∩脏区"行范围限幅,本地迁移
已将正文/光标绘制委托 XTextControl_draw,旧行循环不存在,取本地
委托结构并注释标记(等效脏区限幅待在控制器绘制入口重移植);
②POSIX 链接补桩——远端新增 XPlatformNativeWindow_setWindowState
仅实现 Win32,XWindow.c 无条件引用致 POSIX 链接失败,补 no-op 桩
(注释注明 EWMH 实现待办)。合并后实机:多行右键菜单/点外关闭均
正常,FPS 3555(此前同页 ~750);主回归 exit=0;验收 68/68。
stash@{0} 保留未删(合并前工作区快照,确认无缺后可 drop)。

### 14.125 对齐扫描结论与修复计划（2026-09-19 晚）

**扫描口径**:只对比当前已实现部分与 Qt 6.8.3 的对齐度(API 面/
默认值/信号集合与发射时机/核心行为语义),不把从未计划实现的
Qt 能力计为缺陷。全量报告见 docs/xgui-audit/2026-09-19/
xgui-qt-alignment-scan.md。

**扫描结论**:80 个审计对象中——完整对齐 16(20%)、基本对齐
40(50%)、有差距 24(30%)。高严重度缺口约 20 项,集中五条主线:
条目视图族(滚动/模型信号/键盘导航/多选/role 体系)、对话框族
(exec 不阻塞/模态不生效/静态函数不弹窗)、文本兼容层(XTextEdit
8 信号死、XTextDocument 全局撤销栈)、交互接线(Tab 遍历/XShortcut
未接入/XCheckBox 命中区)、杂项(XDockWidget 浮动、XComboBox 可
编辑路径、XDateTimeEdit 格式引擎)。

**继承关系扫描(同晚补充)**:74 类继承树逐类对照 Qt 6.8.3——
71 类完全一致(按钮/滑块/SpinBox/对话框/条目视图/文本/容器全链
与 Qt 同构);3 处文档化结构简化:XHeaderView←XWidget(Qt 为
QAbstractItemView)、XStackedWidget←XFrame(Qt 为 QWidget)、
XWidget←XObject 内嵌窗口语义(C 单继承适配,全库一致)。初轮误判
XMenuBar/XToolBar/XDialogButtonBox 为 XObject 派生系文件内桥接类
干扰的提取误差,精确提取后三者均为 XWidget ✓。详见
docs/xgui-audit/2026-09-19/xgui-qt-alignment-scan.md §六。

**阶段四补充（剪贴板 X11 Selection 后端 + 平台集成）**:
- `XClipboard.h` 新增 `XClipboardBackend` 挂载点（text/setText/clear
  回调 + ud），`XClipboard_installBackend()` 注册；未注册时使用
  进程内存储（嵌入式零依赖即用）。
- `XPlatformNativeWindow_posix.c` 实现 X11 Selection 协议后端：
  `XSetSelectionOwner` 认领 CLIPBOARD、SelectionRequest 响应服务、
  SelectionClear 通知失去所有权。经
  `XPlatformNativeWindow_installClipboardBackend()` 注入。
- `XGuiApplication_clipboard()` 惰性创建剪贴板单例时自动调用后端
  安装，使复制粘贴经 X11 Selection 与其他应用互通。
- XTextControl 复制/粘贴统一到 XGuiApplication_clipboard()（此前
  仅走 XTextClipboard 静态缓冲），XLineControl 同步双写。

**双向剪贴板验证 ✓**:XGui 应用内复制的文本可粘贴到外部编辑器,
外部编辑器复制的文本也可粘贴到 XGui 输入框——X11 Selection 协议
双向互通已生效。

**阶段四加固(2026-09-19,跨进程剪贴板实测暴露缺陷批次,已全部落地)**:
1. **原子初始化守卫缺陷(跨进程粘贴失败根因)**:`xpw_clipEnsureAtoms`
   以 `g_xpwnClipboard == None` 作为整块初始化守卫,但 CLIPBOARD/
   XIN_YUE_CLIP_DATA 原子在连接初始化处已提前 intern,守卫永不成立,
   导致 TARGETS/TIMESTAMP 原子保持 None——所有 TARGETS 询问被回
   `property=None` 拒绝,剪贴板管理器(dde-clipboard 等)拿不到目标
   列表即中止取数。修复:各原子独立判空 intern。
2. **SelectionRequest 必回 Notify**:无论能否满足必须回复
   SelectionNotify(不能满足时 property=None 表拒绝),否则请求方
   阻塞等待直至超时;TARGETS 询问返回 {UTF8_STRING, STRING}
   (不广告 TIMESTAMP——实测部分管理器遇未知目标会中止取数,
   但直接请求 TIMESTAMP 仍可服务)。
3. **专用剪贴板窗口 + 真实时间戳(对标 QXcbClipboard::m_window)**:
   所有权挂在 1×1 永不映射的专用窗口上,与业务窗口生命周期解耦;
   认领时对专用窗口做零长度属性变更,经 PropertyNotify 取真实
   服务器时间戳(避免 CurrentTime 歧义),同时作为 TIMESTAMP
   目标的应答内容。
4. **读取路径快慢分离**:`XGetSelectionOwner` 快速判空(无所有者
   立即返回,消除粘贴空剪贴板 1 秒阻塞);自己持有所有权时直接
   返回本地镜像(免协议往返);外部所有者先请求 UTF8_STRING,
   被拒(property=None)回退 XA_STRING 重试。
5. **分配器配对修复**:镜像缓冲 `XRealloc_System`/`XFree_System`
   配对(此前 XFree_Hybrid 错配);`XGetWindowProperty` 返回的
   Xlib 缓冲经 `xpwn_xFree`(还原宏后真实 XFree)释放(此前
   XMemory_free 错配,存在堆破坏风险)。
6. **事件泵健壮性**:SelectionNotify 等待泵(50ms×20)将非剪贴板
   事件交回框架分派,双 XGui 应用互拷互贴时不死锁。

**跨进程三向验证 ✓**(xtrace 协议轨迹 + 独立 Xlib 探针实测):
①外部进程→XGui 读取(含中文 UTF-8);②XGui 复制→存活期内
外部读取;③XGui 退出后 dde-clipboard 正确采信并继续提供内容
(此前因缺陷 1+2 永远采信失败,只回旧缓存)。

**修复计划(四阶段,按影响面×严重度排序)**:

**阶段一:交互正确性小项批次**(✅ 2026-09-19 完成,回归/验收全绿)
1. XCheckBox hitButton 扩为 indicator∪文本区(对齐
   SE_CheckBoxClickRect;高)。
2. XButtonGroup_addButton 回写按钮 m_group(使 group() 生效)+
   removeButton 清写;exclusive 组内选中项禁止反选(基类
   nextCheckState 查组)。
3. XGroupBox_setCheckable(true)→setChecked(true)+toggled+
   StrongFocus;false→恢复子控件启用;isChecked=checkable&&checked。
4. XLineEdit:失焦 editingFinished 补 hasAcceptableInput||fixup
   门禁;selectionStart/End 统一为字符口径;hasAcceptableInput 空
   文本=acceptable(对齐 Qt)。
5. XTextEdit 内嵌编辑器信号接线(8 个死信号转真发射)。
6. XTextDocument 撤销栈去全局化(实例持有)+isUndo/isRedoAvailable
   语义修正。
7. 头文件注释失实批次修正(XRadioButton hitButton、XPushButton
   autoDefault、XLineEdit 单位声明、XGroupBox 命中区描述)。

**阶段二:对话框与键盘可用性**(✅ 2026-09-19 完成:exec 阻塞+
应用模态登记+Escape→reject;Tab/Backtab 焦点遍历接线;XShortcut
接入按键分发并完善 context 语义;XTabBar closable 绘制+命中+
tabCloseRequested——movable 拖拽换位除外,余项遗留阶段四)
8. XDialog.exec 阻塞循环(复用 XMessageBox 的 while+processEvents
   模式)+setModal/open 接窗口系统模态;Escape→reject。
9. XWidget Tab/Backtab 键盘焦点遍历接线(dispatchKeyEvent 无焦点
   命中时调 focusNextChild/focusPreviousChild)。
10. XShortcut match/activate 接入按键分发路径;XTabBar closable/
    movable 交互接线(tabCloseRequested/tabMoved)。

**阶段三:条目视图族级三件**(✅ 2026-09-19 全部完成:11 基类接
模型四信号(断旧连新+索引收敛);12 滚动偏移全族接入——XListView/
XListWidget/XTableView/XTreeView/XTreeWidget 绘制/命中/visualRect
含偏移+滚动条范围维护+scrollContentsBy 重绘;13 方向键/翻页/Home/
End 导航+Ctrl 仅移动+Shift 锚点扩选+行为展开)
11. XAbstractItemView_setModel 连接模型信号(dataChanged/
    rowsInserted/rowsRemoved/modelReset→视图刷新)。
12. 派生视图接滚动偏移(visualRect/indexAt/绘制含
    scrollContentsBy,基类 scrollContentsBy 落地)。
13. 键盘导航(方向键/Home/End/PageUp 移动当前项)+选择模式语义
    (Ctrl/Shift 多选、SelectRows/Columns)。
14. 模型 role 体系(DisplayRole 之外的 CheckState 等,按需分批)。

**阶段四:行为对齐批次**(✅ 2026-09-19 大部分完成)
15. XComboBox:editTextChanged 真发射(桥接内嵌编辑框)+Up/Down/
    Home/End 键盘导航 ✓
16. XDateTimeEdit:currentSectionIndex(序号)与 currentSection
    (枚举码)分离,setter 按显示格式映射 ✓;calendarPopup 默认 false ✓
17. XDial:wrapping 拖拽整圆回绕+stepBy 键盘回绕 ✓
18. XAbstractSlider:滚轮经 triggerAction(发射 actionTriggered)✓
19. XProgressBar:setRange 收敛语义(与库内 XSpinBox 一致)✓
20. XWizard:completeChanged 真发射→Next/Finish 使能实时刷新;
    validatePage 默认 true ✓
21. XMessageBox:setTitle 落原生标题;Enter→defaultButton、
    Esc→escapeButton(缺省回退 reject)✓
22. XLineEdit:双击选词(替换简化 selectAll)✓

**阶段一~四收尾轮(2026-09-19 深夜,心跳巡检暴露的 6 条失败断言
根修,回归/验收全绿)**:
1. **is_app_closing 永久拦截(根因,波及面最大)**:
   VXCoreApplication_deinit 置位 is_app_closing 后永不复位,应用
   实例销毁后(回归多套件先后建/删应用、真实应用关闭首窗场景)
   所有经 notify 的事件被永久吞掉——XGroupBox 点击不切换、
   CHILD_ADDED 分派"合并回归与独立运行不一致"(XGroupBoxTest
   已知问题注释的真正根源)皆源于此。修复:deinit 完成时复位,
   标志仅保护析构窗口期;对齐 Qt 的 sendEvent 不依赖应用实例语义。
2. XWidgetWindow_event 补 SHOW/HIDE 显式分支不再转发控件:
   showEvent/hideEvent 由 XWidget_sendShowHide 按可见性翻转恰好
   发射一次;桥接窗口自身的映射事件转发曾致 visibilityChanged
   双次发射(此前被缺陷 1 掩盖)。
3. XGroupBox 对齐 Qt 收尾:setCheckable(false) 取消选中并发射
   toggled(false);标题区点击改 Qt 释放语义(mousePress 仅按压
   待命,mouseRelease 在标题区内切换+clicked,新增 m_pressed 位);
   XGroupBoxTest 计数口径修正并补 reconnect+press/release 事件对。
4. XButtonGroup 互斥取消死锁:setChecked(false) 的反选守卫改为
   只保护组 tracked 的 checkedButton(此前无条件拒绝反选,使
   applyExclusive 永远取消不掉前一按钮);bridgeToggledSlot 先
   转移 tracked 再互斥取消(对标 Qt notifyChecked 次序)。
5. XWizard_validateCurrentPage 补 isComplete 门禁:当前页
   complete=false 时直接判负(先于 validatePage 虚槽),与 Next/
   Finish 使能逻辑一致(对标 QWizard::validateCurrentPage)。

**遗留(大体量/已声明边界,另行安排)**:XGraphicsEffect 渲染
生态、XErrorMessage done-shown 机制、XDockWidget 浮动/特性位、
XMainWindow 停靠几何、XTabBar movable 已做/样式形状未做;
Graphics(XPainter)/Charts/Input/Platform 与 Qt 对齐扫描另轮。

**阶段一~四全部完成 ✅(2026-09-19 收尾轮后,回归零失败断言、
验收 68/68、构建零错误、git diff --check 干净、未提交 Git)**。
§14.125 计划执行完毕,后续仅剩下表"未对齐项"按排期另行处理。
**遗留清单(未对齐项,后续排期)**:
| 项 | 现状 | 类型 |
|---|---|---|
| XDockWidget toggleViewAction 恒 NULL / setFloating 无真实浮动 | 未动 | 结构改造 |
| XInputDialog/XFileDialog/XColorDialog 静态函数不弹窗 | 未动 | 需真实 UI |
| XPlainTextEdit 换行(WidgetWidth 声明不生效) | 未动 | 平铺模型架构边界 |
| XTextEdit/XTextBrowser 富文本渲染子集 | 未动 | 独立引擎工程 |
| XDateTimeEdit 格式引擎仅 6 占位符(缺 AM/PM/毫秒/星期) | ✅ 已完成 | 见 §14.126 批次一 |
| XComboBox completer 接入、insertPolicy | ✅ 已完成 | 见 §14.126 批次二 |
| 条目视图编辑闭环(delegate/commitData)与 role 体系 | 未动 | 基类扩展 |
| XGraphicsEffect 渲染生态、XErrorMessage done-shown | 未动 | 大体量 |
| XMainWindow Top/Bottom 停靠几何、saveState 含 dock | 未动 | 中 |
| XWidget windowIcon 公开 API、saveGeometry/restoreGeometry | ✅ 已完成 | 见 §14.126 批次三 |
| Graphics(XPainter)/Charts/Input/Platform 对齐扫描 | ✅ 扫描完成 | 见 §14.126 两份差距报告(含 P0-P2 排期);修复另轮 |
| POSIX 平台 X11 CLIPBOARD/PRIMARY 选择区集成(外部复制→粘贴) | 部分完成 | CLIPBOARD 已落地(Drive/Posix 直接实现 XPlatformNativeWindow_installClipboardBackend,见"阶段四加固")。剩余:①PRIMARY 选择区(中键粘贴)未接(另见 §14.126 输入域报告 P0-3);②INCR 大数据传输未实现(>窗口属性上限的文本会被截断);③按三件套惯例归位 Src/XGui/Platform 抽象层可再议 |

**验证约定**:每阶段完成后跑主回归+验收 68/68;涉及行为修正的
同步补验收断言;实机 demo 巡检。

### 14.126 遗留清单并行批次轮（2026-09-19 深夜,子代理并行开工）

**组织方式**:3 个实现子代理按文件域并行(XDateTimeEdit/XComboBox/
XWidget 各自只改本域文件,不碰共享测试与文档),2 个只读扫描子代理
出差距报告;主代理统一构建、补回归断言、跑双套件。

**批次一:XDateTimeEdit 格式引擎扩展(6→11 种记号)**:
- 新增 ddd/dddd(星期,周一起始,中文文案)、h/hh(12 小时制)、
  z/zz/zzz(毫秒,z/zz 截尾零语义对标 qlocale dateTimeToString)、
  AP/A/ap/a(上下午,固定中文「上午/下午」,无 locale 环境下等价
  zh_CN amText/pmText)。
- 枚举扩 AmPmSection(0x0001)/MSecSection(0x0002),数值对标 Qt
  6.8 Section;分段容量 8→16;stepBy 新增毫秒(1ms/步)与上下午
  (±12h 翻转)分支;keyPressEvent 重载实现 Left/Right 跨段导航
  (整段反选,端点停驻)。
- 三处重复的 6 占位符解析合并为唯一 tokenizer(xdt_tokenize)+
  xdt_renderToken/xdt_render/xdt_selectCurrentSection。
- 已知裁剪边界(注释已标注):单字母 d/M/H/m/s 仍为字面输出;
  hh 不依赖 AP 固定折算 12h(Qt 含 AP 才折算)。

**批次二:XComboBox 补全与插入策略**:
- `XComboBox_setCompleterMode/isCompleterMode`(默认关,仅
  PopupCompletion):可编辑时 textEdited 触发前缀过滤(大小写
  不敏感),复用下拉弹层按行隐藏承载,Enter 采纳高亮行(回填
  文本+置当前项+activated),Esc 收层不改文本,Up/Down 层内导航。
- insertPolicy 七值真实结算(此前只存字段):Enter 主路径+
  失焦路径,公共守卫对齐 Qt returnPressed(空文本不结算、
  maxCount 钳制、duplicates 查重口径随补全开关)。
- 修复外部 setLineEdit 安装编辑框时 editTextChanged 无发射点
  的缺口;弹层释放命中改按可见行序换算,修过滤后伪行问题。
- 有据偏差(注释注明):Qt 6.8.3 失焦不插入仅同步,本实现失焦
  也执行策略插入;如需严格 6.8.3 删 editingFinished 槽末行即可。

**批次三:XWidget windowIcon/saveGeometry/restoreGeometry**:
- `XWidget_windowIcon/setWindowIcon`:值语义(XIcon 引用计数
  共享副本);子控件 set 只记录自身,get 沿父链向顶层解析,链上
  全空回落应用图标(对标 QApplication::windowIcon);顶层 set 即
  时刷新桥接窗口图标,变化经 cacheKey 判定后发既有
  windowIconChanged 信号。
- `XWidget_saveGeometry/restoreGeometry`(XByteArray 承载):
  "XWG1 frameX frameY frameW frameH normX normY normW normH
  stateFlags" 文本格式(带符号变长十进制,对标 Qt 的
  magic+frameGeometry+normalGeometry+savedState 五元组);
  逐字段校验(魔数/字段数/数值/尾垃圾/宽高>0/stateFlags 位域),
  全部通过才落地,失败零副作用;仅顶层有效。

**根修(新回归断言抓获):XDate_dayOfWeek 整体偏移一天**:
旧式 `(jd+1)%7+1` 使 2024-03-05(周二)返回周三。改为 `jd%7+1`
(带负数归一)。0001-01-01(jd=1721426,周一)因恰在边界未暴露。
全量回归确认 XCalendarWidget 等既有使用方零回归。

**回归断言**:新增 test_datetimeedit_format_ext /
test_combobox_completer_policy / test_xwidget_icon_geometry
三个套件(分段映射/渲染语义/截尾零/进位翻转/段导航;补全过滤
弹层/Enter 采纳/Esc/无匹配/InsertAtBottom/NoInsert;
图标传播/几何往返/逐字节一致/损坏拒绝/非顶层不支持)。
**验证**:构建 0 错误、主回归 exit=0 零失败断言、验收 68/68、
未提交 Git。

**串行批次轮(2026-09-20 晨,单子代理逐批推进,对应扫描报告
P0 消化)**:

**串行批次一:XPainter P0 三条**(Src/XGui/Graphics/XPainter.c/.h):
1. 画笔宽度随非 cosmetic 变换缩放:新增 painterPenWidthScale,
   scale=(|M·(1,0)|+|M·(0,1)|)/2,设备线宽=round(pen×scale)
   下限 1;单位/平移变换 scale≡1 零回归;penWidth<1 或退化矩阵
   保持 1px cosmetic(对标 Qt 非 cosmetic 语义,修 scale(3,1)
   下 2px 线仍 1px 的问题)。
2. XPainterPath 填充规则:新增 m_fillRule + setFillRule/fillRule
   (OddEven/Winding,值对齐 Qt FillRule),fillPath/drawPath 改用
   路径规则(此前硬编码 OddEven);Winding 非零环绕管线复用既有
   painterBuildFillSpans;默认零回归。
3. 消除静默截断:drawPolyline/drawPolygon 去掉 128 点上限
   (>128 切堆缓冲,XMalloc_System/XFree_System 配对);
   drawTextRect 行缓冲栈 64 槽起步动态翻倍扩容,OOM 路径统一
   释放+回滚裁剪零副作用。
- 已知范围外:XPicture 录制流不持久化 fillRule(后续批次)。
- 自验证:构建 0 错误、回归/验收全绿;/tmp 13 项行为断言
  (单位厚度不变、scale(3,1) 厚度 4、OddEven/Winding 重叠差异、
  200 点不截断、100 行文本第 99 行可见)全 PASS。

**串行批次二:输入法查询接线**(Input/Platform 扫描 P0-1):
- XGuiApplication_inputMethod 惰性创建时自动注册内置桥接
  XInputMethod_defaultQueryHandler → 焦点控件
  XWidget_inputMethodQuery 虚槽(新增 EXWidget_InputMethodQuery
  槽位,子类可重载),无焦点/非控件返回 NULL。
- 取值映射:ImCursorRectangle=(w/2,0,1,h) 经 inputItemTransform
  映射、ImInputItemClipRectangle=控件矩形、ImEnabled=true、
  ImHints=设置值;ImSurroundingText 等文本类返回 NULL(文本控件
  重载虚槽属 P1 未扩散)。
- 生命周期:应用析构先删 inputMethod,其后查询因 instance()==NULL
  直接 NULL,实测无悬挂;集成方仍可 setQueryHandler 覆盖。
- 自验证:构建 0 错误、回归/验收全绿;端到端 11/11(注册生效、
  焦点实时值、平移映射 (50,0,1,50)→(60,20,1,50)、清焦点回落、
  销毁后无悬挂)。
- 主代理终验:双批次后全量构建 0 错误、回归 exit=0 零 FAIL、
  验收 68/68、diff --check 干净。

**扫描报告 P0 消化进度**:XPainter P0 三条 ✅;输入法查询接线 ✅;
余:TOUCH/TABLET WSI 入口与合成、剪贴板后端 mime 多格式(见上文
两份扫描报告排期)。

**串行批次三:剪贴板 PRIMARY 选择区 + 外部变更通知**(2026-09-20,
Input 域 P0-3 + P1-4):
- PRIMARY 服务与 CLIPBOARD 同构:5 个单选择区全局收敛为
  XpwClipOwnerState[2](CLIPBOARD/PRIMARY 各自镜像/所有权/
  时间戳),SelectionRequest/TARGETS/TIMESTAMP/UTF8 回退全链路
  按事件 selection 原子分流;专用窗口与时间戳路径两选择区共用。
- `XClipboard_supportsSelection()` 按后端能力位返回(X11 true);
  Selection 模式 setText/text/clear 走 PRIMARY;FindBuffer 后端
  显式拒绝仅留进程内镜像。
- 后端契约尾部追加 supportsSelection 能力位与可选
  selectionRevoked 回调(位置初始化兼容);X11 SelectionClear 时
  通知 → owns 复位 + 清该模式数据 + 发射信号(Clipboard→
  dataChanged,Selection→selectionChanged,顺序先专用后 changed
  对齐 Qt);未注册零回归。
- 自验证:构建 0 错误、回归/验收全绿;真机 X server 独立探针
  23/23(双选择区分流、互不串扰、外部认领复位、双向读回)。
- 遗留:控件侧中键粘贴(鼠标 Button2→粘贴 Selection)下一批;
  clear 不主动释放所有权(沿用既有)。

**串行批次四:XPainter P1 双线性 + 浮点重载族**(Graphics 域
P1-B8 + A6/A7):
- SmoothPixmapTransform 落地:hint 开启时缩放/变换图像采样改
  双线性(2x2 邻域加权、逐轴边界钳位、经 XImage_pixel 直通
  ARGB 空间插值);恒等/平移 1:1 blit 仍走 memcpy 快路径
  (drawTiledPixmap 等 1:1 热路径零回归);GPU 无需改(复杂路径
  本就局部软件提交)。
- 浮点/9 参重载族:drawImage_3/drawPixmap_3(9 参,Qt 负目标
  尺寸/越界裁剪规则)、drawLine_3(float,scale(10,1) 下 0.55
  落设备 x=6 的亚像素精度)、drawRect_2(float,QRectF normalized)、
  drawEllipse_2(中心半径版);全部复用既有变换/裁剪/opacity/
  合成管线;拆出 painterRaster_drawLineDevice 供设备坐标重入。
- 自验证:构建 0 错误、回归/验收全绿;/tmp 29/29(2x 放大插值
  0xbf、hint 关闭最近邻、1:1 逐字节一致、浮点精度、Picture
  录制冒烟)。
- 已知取舍:直通 ARGB 空间插值(Qt 为预乘空间,透明边缘抗晕
  略优,注释已注明);采样每像素 4 次 XImage_pixel,P2 可加
  constBits 快速路径。

**串行批次五:控件侧中键粘贴**(2026-09-20,PRIMARY 落地闭环,
对标 Qt X11 中键粘贴语义):
- XLineEdit:Button2 按下且 supportsSelection() 时坐标平移
  (textStartX+viewOffset,与左键同口径)→控制器 xToPos 落光标
  →复用 XLineControl_paste(Selection) 既有通道(UTF-8 字节
  口径未动);处理则 setFocus+accept。
- XTextControl:FuzzyHit hitTest→setCursorPos(MoveAnchor)→
  复用 xtc_pasteFromMode(Selection)(XTextControl_paste 原体
  重构为模式参数化通道,公开入口变 Clipboard 便捷入口,对标
  QWidgetTextControl::paste(Mode));XPlainTextEdit 经既有
  xpe_forwardMouseEvent 转发无需改动。
- 只读/不可编辑门禁先行;中键不写剪贴板、CLIPBOARD 不串台。
- 自验证:构建 0 错误、回归/验收全绿;真机探针 10/10(三处
  点击位置插入+光标落点、中文 12 字节无损、只读不粘贴、
  CLIPBOARD 不变、多行通道同样生效)。
- 顺带发现(既有问题,非本批引入,未修):XLineEdit 以未实例化
  XWindow 直接作父控件时创建期 updateSizeHints 路径会踩垃圾
  m_layout 指针(真实用法 NULL 父或容器不触发,待排期);
  Selection 为空时 paste 回退共享层 XTextClipboard_getText
  为既有行为,如需严格 Qt 语义(空则不动)可后续按模式收紧。

**串行批次六:窗口 flags 运行时同步 EWMH**(2026-09-20,平台域
P1-6):平台契约新增 XPlatformNativeWindow_setWindowFlags(位掩码
同 XWindowType 值),XWindow_setFlags 在已挂接平台窗口时转发,
setFlag 转调 setFlags(对标 QWindow::setFlag→setFlags);X11 落地:
StaysOnTop→_NET_WM_STATE_ABOVE、StaysOnBottom→BELOW、
BypassWindowManager→SKIP_TASKBAR+SKIP_PAGER(EWMH 近似,Qt xcb
实为 re-create 窗口,注释注明)、DoesNotAcceptFocus→
_NET_WM_HINTS.input=False;未映射窗口读-改-写属性(保留 WM 管理的
原子),已映射窗口发 _NET_WM_STATE ClientMessage 由 WM 回写
(EWMH 规定映射态属性归 WM,实测直改会被冲掉);装饰提示留
_MOTIF_WM_HINTS TODO。win32/unsupported 兜底 no-op 保链接。
自验证:构建 0 错误、回归/验收全绿;Xlib 探针 15/15(ABOVE/
BELOW/SKIP_*/input 位增删、无残留、创建前仅存值零回归)。
已知 WM 时序:映射后数百 ms 内 ClientMessage 可能被 WM 丢弃
(WM 侧行为,应用 show 后稍晚设置不受影响)。

**串行批次七:裸父控件悬挂指针根修**(2026-09-20,批次五探针
发现的框架创建期 bug):
- 根因:XWindow 是 XObject 非 widget 子类(仅 m_class+m_data,
  is_widget 恒 0),`XWidget_init` 无条件接受父指针挂链后,
  `XWidget_parentWidget` 把裸 XWindow* 盲转 XWidget* 读 m_layout
  ——读到分配块之外堆内存(实测读到已释放文本 "<double"),
  非零垃圾传入 XLayout_activate 段错误(gdb 回栈逐帧证实:
  XLineEdit_init→updateSizeHints→updateGeometry→Layout_activate)。
- 修复:XWidget_init 与 XWidget_setParent 两个建链入口顶部校验
  `((const XObject*)parent)->is_widget`,非控件父归一化为 NULL
  (按顶层处理),覆盖全部控件子类的创建与重挂;真实控件父
  恒通过零变化。
- 自验证:构建 0 错误、回归/验收全绿;探针修复前 SIGSEGV(139)
  →修复后 exit=0,创建/setText/sizeHint 正常,重挂父场景同安全。
- 残留风险面(未修):绕过控件 API 直接 XObject_setParent 后再
  盲转的 API 误用,可在 XWidget_parentWidget 补 is_widget 校验
  (方向 b,后续小改)。

**串行批次八:TOUCH/TABLET WSI 入口与控件派发**(2026-09-20,
Input/Platform 域 P0-2,扫描报告最后一条 P0 消化):
- WSI 入口:handleTouchEvent(TOUCH_BEGIN/UPDATE/END/CANCEL,
  主点+pointCount)与 handleTabletEvent(PRESS/RELEASE/MOVE,
  压力+指针类型),自发同步投递,风格对齐相邻 handleMouseEvent。
- 投递链:VXWidgetWindow_event 补分支(模态拦截表纳入)→
  XWidget_dispatchTouchEvent/TabletEvent(共用命中/坐标平移/
  父链传播循环)→ 新增 EXWidget_TouchEvent/TabletEvent 虚槽
  (默认 ignore,VT_DISPATCH 接通);disabled 丢弃分支继续生效。
- 隐式抓取对标 QGuiApplicationPrivate:BEGIN 被接受即抓取,
  UPDATE/END 直达(含跨顶层坐标转投),END/CANCEL 清理,
  控件销毁/隐藏摘除;accept 语义与鼠标一致。
- 自验证:构建 0 错误、回归/验收全绿;探针 25/25(三连计数、
  命中平移、抓取与清理、压力透传、非法类型拒绝)。
- 未尽:XI2 触摸合成(入口即统一注入点)、touch→mouse 仿真、
  XTouchEvent 完整多点列表(Task 2.20 既有偏差)。

**串行批次九:线条抗锯齿**(2026-09-20,Graphics 域 P1-B5):
- painterRaster_drawLineAntialiased:线段沿法线偏移半线宽构
  封闭四边形,交既有填充 AA 通道 4x4 面积子采样生成覆盖图;
  合成完全复用 putPixel 状态管线(裁剪/clipRegion/opacity/
  compositionMode 不新写混合器),GPU 会话自动走
  drawAlphaBitmap 提交。
- 笔帽对标 Qt 默认 SquareCap(两端延伸半线宽,折线拐角自然
  填补);透明度只施加一次(传原始 penColor 防双重缩放);
  Liang-Barsky 裁剪护栏防超大坐标申请巨缓冲。
- 受益图元:drawLine(含浮点入口)/drawPolyline,及椭圆/圆角
  矩形/圆弧/饼形轮廓(离散折线天然受益);虚线拆段后逐段 AA。
- 门控:仅 Antialiasing 开且 dx≠0 且 dy≠0 进 AA 分支;hint 关、
  轴向线、零长点、GPU 轴线快速路径逐字节零回归;drawPoint
  保持硬边(图表标记锐利,记录为约定)。
- 自验证:构建 0 错 0 警告、回归/验收全绿、XGuiGpu_Test 通过;
  /tmp 9 项(斜线灰度、轴向逐位一致、opacity 混合值与硬边
  参考逐位相等、虚线间隙纯背景、出界零像素)全 PASS。
- 未尽:AA 分支 RoundCap 近似方帽、无 join 几何(strokePath
  独立描边引擎仍缺,依赖 drawPolyline 落地);覆盖图按包围盒
  整块分配,分片优化后续。

**串行批次十:Input 域小项组合**(2026-09-20):
- text_subtype 升级 in/out(对标 QClipboard::text(QString&,Mode)):
  空请求按 formats 顺序 plain→html 回退,显式 "html" 只查
  text/html 不回退 plain;subtype 原地复用防调用方泄漏。
- XMimeData_removeFormat 新增:四个内置格式分别清理对应存储,
  自定义条目整条移除,大小写不敏感与 hasFormat 一致。
- filterEvent 接入:XGuiApplication 重载 EXCoreApplication_Notify,
  KEY_PRESS/RELEASE 派发前问输入上下文 FilterEvent 虚槽
  (新增,默认恒 false 零回归),位于 IME consumed 之后控件派发
  之前,对标 Qt4 notify 的 filterEvent 遗产语义。
- 自验证:构建 0 错误、回归/验收全绿;探针三组全 PASS。

**串行批次十一:XMimeData 自定义条目损坏根修**(2026-09-20,
批次十探针发现的既有 SEGV):
- 根因:m_custom 是存条目指针的 XVector,XVector_at_base 返回
  槽位地址(元素类型为指针时应为二级指针),七处直接强转成
  条目指针使用——读到指针本身当 XString*、越过槽位越界,
  setData 后首次 hasFormat 即 SEGV。
- 修复:新增 mime_customAt 帮助(槽位地址解引用,NULL/越界安全),
  七处全部改走;条目生命周期本就自带深拷贝与 deinit,修好取址
  零泄漏。
- 同族排查:urls/text/html/image/color 均深拷贝无问题;连带修复
  VXMimeData_move 漏转移 m_urls(目标丢 urls/源悬空)。
- 自验证:构建 0 错误、回归/验收全绿;ASan+LeakSanitizer 探针
  19/19(修复前 SEGV 复现、64 条扩容、removeFormat、copy/move、
  零泄漏)。
- 建议回归断言:setData→hasFormat→formats 往返(现有套件缺口)、
  扩容后逐条 data、removeFormat 自定义分支、copy/move 所有权。

**串行批次十三:三小项组合**(2026-09-20):
- XWidget_parentWidget 读前 is_widget 校验(批次七方向 b 收口),
  沿父链 4 处直接强转全部改走防护入口(顶层判定/鼠标/右键菜单/
  触摸派发传播循环/nativeParentWidget),绕过 XObject_setParent
  直挂场景收口。
- XLineControl_paste Selection 空回退收紧:systemOnly 标志
  (Selection 模式且后端 supportsSelection 时跳过共享层回退,
  PRIMARY 空则不动作,对标 Qt 中键);Clipboard 回退链与无后端
  嵌入式行为零回归。
- touch→mouse 仿真:默认开(对标 Qt 6),未被接受的 TouchBegin
  合成 PRESS/MOVE/RELEASE(按钮状态对齐 Qt 合成语义),复用
  dispatchPointerEvent 管线;被接受则只走触摸+隐式抓取;
  框架级开关 XWidget_setTouchMouseSynthesisEnabled;
  顺带清理上批遗留 [GRABDBG] 调试输出。
- 自验证:构建 0 错误、回归/验收全绿;探针 28/28。
- 未尽:SYNTHESIZE_MOUSE 属性位三态接线(需 XBitArray 三态)、
  handleTouchEvent 注释更新(文件不在批内)、XMouseEvent 无
  synthesized 来源标志(既有偏差)。

**串行批次十四:strokePath 几何描边 + 虚线节距对齐**(2026-09-20,
Graphics 域 P1-B3 + P2-B10):
- 几何描边器 painterPathStrokeWide:设备笔宽>1 且 Image 可逆变
  换接管(1px 默认笔/Picture/奇异变换走原管线逐像素零回归);
  每段法线 ±半宽对接四边形,拐角沿角平分线 Sutherland-Hodgman
  裁开消除内侧重叠(半透明不双重混色),外侧楔形 join 补片:
  Bevel 三角/Miter 延长交点(miter 超限 2 回退 Bevel,真实截断)/
  Round 6 段弧;Cap:Flat/Square/Round 8 段,虚线实段两端也加帽
  (对标 Qt DotLine+RoundCap=圆点)。
- 各描边片逆映射回用户坐标逐片走 Winding 扫描填充,AA/opacity/
  composition/裁剪/GPU 局部提交全部复用既有通道;描边器内虚线
  节距×笔宽沿轮廓连续推进。
- 虚线节距单位改笔宽倍数(对标 Qt):轴向与通用管线 unitScale=
  max(penWidth,1),宽度 1 数值不变;CustomDashLine 空 pattern
  回退实线(Qt setDashPattern 空列表忽略语义);生产代码无
  笔宽>1 虚线调用方,视觉变化面为零。
- 自验证:构建 0 错误、回归/验收/Gpu 三套件全绿;/tmp 自测
  (Bevel/Miter/Round 拐角像素、圆端帽、节距 4 倍量测、1px
  逐像素等价哨兵、opacity 混合、AA 宽笔曲线白芯灰边)全 PASS。
- 未尽:180° 折返拐角不生成补片(TODO)、无 setMiterLimit API、
  Round 为 6/8 段近似、描边器仅 Image 设备、用户坐标节距未乘
  世界变换缩放(注释注明)。

**串行批次十五:屏幕接入与 DPI 回填**(2026-09-20,平台域 P1-7/8):
- 枚举:RandR 1.5 XRRGetMonitors 逐监视器一屏(屏名取监视器
  原子),扩展不可用回落 XScreenOfDisplay;上限 8;注册经新 WSI
  入口 handleScreenAdded→XGuiApplication_screenAdded(复用
  XScreen 既有注册表),含 (0,0) 的监视器为主屏。
- DPI:physical=pixels/(mm/25.4)(虚拟屏 mm=0 保留不伪造);
  logical=Xft.dpi 资源>解析失败回退 96(对标 QXcbScreen);
  devicePixelRatio 恒 1.0(X11 无缩放管道,注释对标)。
- 事件泵实接:RRScreenChangeNotifyMask 订阅,泵内拦截→
  XRRUpdateConfiguration→重枚举差分回填(值不变不发信号);
  实机 xrandr 切 1920x1440 再切回,geometry 变更均被捕获。
- 连接时序修正:屏幕接入改首次事件泵时惰性接入(应用单例
  必有效,Qt 屏幕接入同样在构造完成后生效)。
- 自验证:构建 0 错误、回归/验收全绿;探针 screens 数量/几何/
  physical 95.94/logical 96 与 xrandr 逐字段一致。
- 未尽:热插拔增删与主屏重选(TODO)、Xft.dpi 运行期变更不触发
  刷新、物理尺寸热刷新直调平台未走 WSI、窗口创建前 screens()
  为空(事件循环启动后可用的既定语义)。

**串行批次十六:剪贴板 mime 多格式协商**(2026-09-20,Input 域
P0-3 收尾):
- 契约尾部追加 formats/mimeData(借用语义免拷贝)/setMimeData
  三个可选回调,未注册零回归;XClipboard setMimeData 经
  clear+逐格式写入,mimeData() 非自有时一次性合并外部 formats
  (m_externalMerged,clear/revoke 复位)。
- X11:多格式镜像条目(MIME 名↔目标原子↔字节流,容量 8),
  text/plain 与既有 m_text 通道双向互通;TARGETS 应答=镜像实际
  持有集合;SelectionRequest 按目标原子匹配回数据(format=8
  原样,PNG 不再编码);读方向 TARGETS 枚举直通(协议目标跳过),
  text/plain 复用 UTF8_STRING→XA_STRING 回退。
- 真机探针:写方向 plain+html+PNG 特征字节→外部 TARGETS 逐
  原子读到字节一致;读方向外部 text/html→mimeData 合并后
  text_subtype("html") 取回;批次三纯文本双向复测通过。
- 修复两个真机才暴露的读缺陷:TARGETS 原子表须按 Xlib long
  数组整拷贝(截短拷贝高位拼垃圾原子致 BadAtom);
  XGetWindowProperty 的 nitems 是元素数不是字节数。
- 已知限制(注释注明):XMimeData 以 XString 存字节,非 UTF-8
  二进制载荷(如 PNG 魔数 0x89)进 mime 前会被转码,对标 Qt 需
  QByteArray 的同类限制;image/png 读取方向、INCR、MULTIPLE、
  SAVE_TARGETS 未做;外部内容后续变化不自动刷新已合并镜像。

**串行批次十七:四小项收尾组合**(2026-09-20):
- touch→mouse 属性接线:XGuiApplication_setAttribute/testAttribute
  新增(属性 12 显式设置时转发框架开关,XBitArray 三态限制用
  静态 bool 记录显式设置,注释注明取舍);handleTouchEvent 注释
  更新为已接。
- XPicture 录制流持久化 fillRule:DrawPath 尾随 4 字节(同
  DrawTiledPixmap 尾随 extra 编码惯例,旧流字节偏移不受影响);
  新旧格式按记录长度精确区分,旧流回放默认 OddEven 零回归,
  fillRule>1 校验拒绝。
- MOTIF 装饰提示位落地(批次六 TODO 消化):就地定义 MWM 5 字段,
  无提示位→DECOR_ALL|FUNC_ALL 存量零回归,Frameless→decorations=0,
  显式模式按位组装(Title/SystemMenu/Min/Max/Close/固定尺寸抑制
  RESIZE),setWindowFlags 与原生 create 两处写入;真实 DDE 会话
  探针 10/10。
- drawPoint AA 约定文档化(零长线硬边,圆点用 drawEllipse_2)。
- 自验证:构建 0 错误、回归/验收/Gpu 全绿;Picture 探针 4/4、
  属性接线 7/7。
- 未尽:XCoreApplication 基类直调 setAttribute 不触发转发(收敛
  GUI 属性包装待后);MOTIF functions 未覆盖 MWM_FUNC_RESIZE
  精确语义、未复刻 Qt Tool/Popup 默认收敛规则。

**串行批次十八:XScreen 热插拔差分增删**(2026-09-20,批次十五
TODO 收口):
- xpwn_screensEnumerate 改与平台注册表差分:按 RandR 监视器名
  匹配(对标 Qt output name 标识),命中仅差分回填(内部变化才发
  信号),未命中 handleScreenAdded,枚举后对未覆盖屏幕逐个
  handleScreenRemoved 注销(含 monitorCount==0 全拔出);
  修复自反性 bug(本轮新登记未标 matched 被同轮误删致抖动,
  探针首跑暴露即修)。
- 主屏重选 xpwn_screensReselectPrimary:含 (0,0) 者优先否则
  取首块,变化才发 primaryScreenChanged;驻留窗口迁移:移除屏
  上窗口 setScreen(新主屏)+几何钳位+原生窗口同步移动
  (对标 Qt setScreen 迁移)。
- 自验证:构建 0 错误、回归/验收/Gpu 全绿;真机 RandR 拓扑
  (setmonitor 改名+分辨率切换驱动 Notify)走通增删/差分/幂等/
  晋升/钳位迁移,显示环境已还原。
- 未尽:多监视器真实热插拔受虚拟驱动限制未端到端(代码路径+
  单屏差分等价验证);同名监视器按首名匹配;主屏原点挪移未实测。

**串行批次十九:剪贴板 image/png 读取方向**(2026-09-20,
遗留清单收尾):
- 复用库内自研编解码 XImageCodec_decode(Png)——支持 8/16 位
  灰度/RGB/RGBA、调色板+tRNS、Adam7 隔行,零自研零新文件。
- XClipboard_image 重写:自有 application/x-qt-image 优先;
  其次后端 formats 含 image/png 时按 Qt QXcbClipboardMime
  按需读语义直取后端字节(绕开 XString 合并镜像的 UTF-8 转换,
  保二进制透明)→解码;pixmap 自动受益;解码失败 isNull 零副作用。
- 自验证:构建 0 错误、回归/验收全绿;真机探针——外部 serve
  合法 8x6 RGBA PNG(独立 CRC 校验)解码宽高/抽样像素含 alpha
  精确一致;垃圾字节 isNull 不崩。
- 未尽:XMimeData 合并镜像对二进制 mime 仍经 XString 暂存
  (XMimeData_data("image/png") 字节不可靠,需 XByteArray 通道,
  批次二十候选);image/bmp/jpeg 等原子接线(codec 能力已备);
  无 TARGETS 老应用、INCR 大图传输未验。

**串行批次二十:mime 二进制安全 + 图像原子扩展**(2026-09-20,
批次十九登记项收口):
- XMimeData 自定义条目载荷 XString*→XByteArray*(对标 QMimeData
  的 QByteArray):新增 setData_bytes/data_bytes 二进制透明通道,
  copy/move/removeFormat/deinit 全链同步;批次十一 mime_customAt
  取址模式保留;XClipboard 外部合并镜像改直存字节,出栈不再过
  UTF-8 转换;旧 XString 接口签名未动,文本格式零回归。Drive/Posix
  侧核对结论:镜像本就 memcpy+format=8 原样存储,字节流安全已满足。
- 图像原子扩展:image/png→image/bmp→image/jpeg 识别序(对标
  Qt png 置首同序),逐项 XImageCodec_canDecode 门闸(裁剪配置
  如实跳过),解码失败回落下一原子;批次十九"绕开镜像"路径被
  更优的二进制透明镜像读取取代。
- 自验证:构建 0 错误、回归/验收全绿;ASan+LeakSanitizer 探针
  33/33(0x89 魔数+全值域 320 字节逐字节一致、png/bmp/jpeg 并存
  优先级、真机跨进程 bmp 解码、copy/move/removeFormat 零泄漏)。
- 未尽:XMimeData_formats 列出 x-qt-image 与 Qt formats() 剔除
  内部类型的对齐属 XMimeData 模块决策(注释注明取舍);
  x-color 平台映射无约定维持跳过;后端 mimeData 借用语义在
  未来 INCR 改造时需复核读方向合并。

**串行批次二十三:Selection INCR 大数据传输**(2026-09-20,
剪贴板协议深水区,对标 ICCCM 2.5 + Qt QXcbClipboardTransaction):
- 写方向 serve INCR:数据超阈值(=min(最大请求长度字节/4,
  262144),下限 1024;XExtendedMaxRequestSize 以 4 字节字计先折
  字节)改走 INCR——PropertyChangeMask 监听对端→type=INCR/format=32
  协议头→notify 先于首批分片(严格 ICCCM 顺序)→会话表
  (requestor/property/数据快照/偏移,容量 8,深拷贝防镜像中途
  覆盖)→PropertyNotify Delete 驱动逐片,末片消费后再收 Delete
  写零长度属性终结并恢复对端掩码;SelectionClear/窗口销毁/
  30s 闲置扫描三路清理。
- 读方向消费 INCR:专用请求窗口 g_xpwnClipReqWin(1x1 不映射,
  对标 QXcbClipboard::m_requestor,修复无业务窗口无法跨进程读
  的缺陷);notify 后按属性实际 type=INCR 进入增量收集
  (协议头总长预留+指数扩容+256MB 防御;终结判定用"NewValue+空读"
  而非 PropertyDelete——自身 delete=True 读取也产生 Delete 不可
  作信号);5s 超时兜底;循环内非目标事件交回框架分派(并行 serve
  会话持续推进)。
- 非致命 Xlib 错误处理器:原默认处理器 exit(),INCR 对端中途退出
  的 BadWindow 属可预期异步错误,改记录继续(对标 Qt"错误是事件")。
- 修复前后:独立 ICCCM 所有者 serve 600KB → 框架此前返回 NULL,
  现逐字节取回;框架 setText 600KB → 规范 INCR serve(3 片,
  请求者实测 incr=1);小文本双向 incr=0 零回归。
- MULTIPLE(80-120+60-80 行)/SAVE_TARGETS(40-60 行)仅评估:
  主流请求者极少使用,建议 P2 缓做(报告存档)。
- 自验证:构建 0 错误、回归/验收/Gpu 全绿;真机探针 8/8
  (60 万字节中文混排双向、CLIPBOARD+PRIMARY、小文本零回归;
  与 fcitx5/Qt 看门进程并发消费同会话正常)。

**串行批次二十四:Input/Platform 域 P2 四连**(2026-09-20):
- XMimeData_formats 剔除 application/x-qt* 内部类型(对标
  QMimeData::formats;hasFormat 走存储分支本就命中内部类型,
  注释说明);XClipboard 推送链改 XMimeData_hasImage 内部查询,
  不再依赖 formats 列出内部类型(必要越界 XClipboard.c,报备)。
- 基类属性分发收敛(批次十七报备项):XCoreApplication 新增
  AttributeHook 单钩子(setAttribute 写位后回调),
  XGuiApplication 覆盖处理属性 12;包装层保留无实例兜底;
  直调基类与包装层行为等价,转发逻辑不再两处重复。
- WSI 键/鼠标负载补齐(扫描 P2-12):XKeyEvent 补
  nativeScanCode/timestamp、XMouseEvent 补 globalPosition/
  timestamp(init 归零向后兼容);新增 handleKeyEvent_ex/
  handleMouseEvent_ex/handleTouchEvent_ex 完整负载入口
  (旧签名委托保 win32 泵零回归);posix 泵传 xkey.keycode/
  xkey.time/根坐标/xbutton.time;touch→mouse 合成经
  touchTimestamp 同步通道透传。
- XCursor X11 映射(扫描 P2-13):平台后端钩子表
  (queryPos/warpPos/applyWindowCursor/clearForWindow,对标
  QPlatformCursor);24 形状→cursorfont 字形映射表(Blank 用
  1x1 空像素图;Forbidden/Busy/手型/拖拽系列为字体近似,
  Qt 官方为位图自绘,映射表注明);XCreateFontCursor+XDefineCursor
  应用、XUndefineCursor 清除、XWarpPointer 定位;XWidget
  setCursor/unsetCursor 接平台应用(惰性建窗补应用)。
- 自验证:构建 0 错误、回归/验收全绿;探针 25/25(过滤/钩子/
  scanCode+timestamp 读回/光标应用与 Warp 实时读回)。
- 未尽:Bitmap/Custom 位图光标 X11 通道未接(回落左箭头);
  XWindow_setCursor 窗口级直调仍仅存储(控件路径已覆盖);
  touch timestamp 以同步通道承载(XTouchEvent 负载不可改折衷)。

**串行批次二十五:平台域收尾组合**(2026-09-20,P2 可行队列
最后一批准):
- WSI 四状态入口:handleWindowStateChanged(经 XWindow_report
  前缀新入口,只持久化+发信号不回写平台防注入回环)/
  handleThemeChanged(落 XStyleHints_setColorScheme→
  colorSchemeChanged,对标 Qt 6.5 深浅色通道)/handleLocaleChange
  (BCP 47 字符串,Auto 布局方向按语言主子标签重解析 ar/he/fa/
  ur/ps/syr/ckb→RTL)/handleApplicationStateChanged(转发既有
  setApplicationState)。
- XPlatformWindow 轻量补齐 setParent(存值)/screenForGeometry
  (中心点命中,未命中回落主屏)/isExposed(绑定转发,缺省 true);
  契约无等价物已 grep 确认,自包含实现。
- XWindow_setCursor 接平台(批次二十四遗留):已创建且挂接时经
  XCursor 后端 apply/clear,未造第二套钩子,存储语义不变。
- SAVE_TARGETS 轻量落地(批次二十三评估的低垂果实):serve 方向
  XA_ATOM/32 回镜像支持的目标集合(目标集构建抽共享
  xpw_clipBuildTargetAtoms,TARGETS 逐字节同序);MULTIPLE 维持
  P2 缓做(注释锚定)。
- 自验证:构建 0 错误、回归/验收/Gpu 全绿;探针 19+10+8 全 PASS
  (状态注入/信号幂等/ar-SA→RTL、apply/clear 带 winId、管理器
  角色读 SAVE_TARGETS 原子数组)。
- 未尽:theme 未联动调色板深浅翻转(独立课题);SAVE_TARGETS
  轻量应答(数据全量在内存未做延迟序列化)。

**P2 可行队列清零声明(2026-09-20)**:两份扫描报告的全部
P0/P1/P2 可行项均已落地;剩余项全部属结构改造/独立工程
(见遗留清单:XDockWidget 浮动、真实 UI 对话框、富文本引擎、
delegate 编辑闭环、XGraphicsEffect 生态、XMainWindow 停靠
几何、XPlainTextEdit 换行架构、多点触控列表 Task 2.20、
MULTIPLE 协议、位图/自定义光标、theme×调色板联动),
需各自立项排期,不适配心跳批处理粒度。

**串行批次二十六:多行文本软换行落地**(2026-09-20,用户实测
"多行文本超出文本边框"缺陷修复,消遗留清单 XPlainTextEdit
换行架构边界项):
- 布局缓存:XTextControlVisualRow{逻辑行,起始字节,字节长,
  像素宽} 全量单调数组+脏标记惰性重建;NoWrap 1:1 退化。
- 双向映射:字节位置↔(可视行,列)全链(光标移动/鼠标 hitTest/
  选区绘制/draw/movePosition,Up/Down 以像素 X 为列目标对标
  cursor x 保持);软断点歧义引入 m_cursorAtRowStart 边沿标记
  (对标 Qt 光标边沿跟踪);IME preedit splice 进布局参与折行。
- 断行规则(简化 UAX#14):空白/CJK/词字符/标点分类+行禁首尾
  禁则表;WordWrap=CJK 逐字+CJK↔西文边界+词边界,溢出先记断点
  再判溢出(贪心最满行,软断像素宽结转);WrapAnywhere 逐字断。
- 滚动:垂直滚动范围按可视行数,WidgetWidth 折行宽=控件宽−2px
  水平滚动条归零,NoWrap 由水平滚动承载。
- NoWrap 裁剪兜底:逐可视行 save/restore+IntersectClip 到
  "文本区∩行带"——任何模式文本绝不越出边框。
- 壳适配:lineCount 可视行口径、blockCount 保持逻辑块口径
  (对标 Qt)、resizeEvent 同步折行宽、wordWrapMode 默认
  WordWrap(对标 QPlainTextEdit)。
- 自验证:构建 0 错误、回归/验收全绿、demo autotest PASS;
  /tmp 独立 harness 24/24(长中文行折 7 行无越界、混排+跨软断点
  选区、NoWrap 硬裁、滚动到底、preedit 折行);截图 /tmp/wrap_*.png。
- **连带根修 XTextDocument_setPlainText 越界(批次二十六探针
  暴露,基线/修改版双复现归属确认)**:写入分支
  `blockIdx < m_blockCount`(clear 后恒 1)使 ensureCapacity 成
  死代码,行尾却直接抬 m_blockCount 到逻辑行数——超容量块从未
  分配未清零,析构/clear 读垃圾 fragment 指针。修复:增长容量+
  新增槽位逐块置零+每行真正写块(空行也建空块)。ASan 探针
  40 行往返/二次覆写全过。
- 未尽:WrapAtWordBoundaryOrAnywhere 细分策略未实现(与
  WordWrap 同路径);布局重建为全量 O(文档长);组合行每次布局
  一次分配。

**串行批次二十一:P2 小项四连**(2026-09-20):
- XPainter miterLimit API:setMiterLimit/miterLimit(默认 2.0,
  <1 与 NaN 钳位 1,对标 QPen::setMiterLimit),入状态快照随
  save/restore,setPen 复位默认;描边器读取状态值(修批次十四
  注释口径:该比值实为 miter 长度/笔宽,与 Qt 基准一致)。
- 描边器 180° 折返补片:平分线退化分支补半圆扇区(圆心=顶点,
  扫向用 RoundJoin 同套转向公式,圆帽落折返外侧),三种 Join
  一律圆弧;像素级验证外侧有墨内侧干净。
- XMouseEvent synthesized 标志:XEvent.h 新增 m_synthesized 位
  +isSynthesized/setSynthesized,touch→mouse 合成置 1,
  XMouseEvent_init 默认 0,vtable copy 逐字段同步;全库核对无
  裸 memcpy 半拷贝路径。
- Xft.dpi 运行期刷新:关键发现——XGetDefault 首调后缓存资源库,
  xrdb 重载永远不可见;改为每次直读根窗口 RESOURCE_MANAGER +
  XrmGetStringDatabase(对标 Qt xcb 每次读属性),缺失回落
  XGetDefault→96;差分回填全部屏幕(重复值不发信号),
  RRScreenChangeNotify 后顺带重读;公开
  XPlatformNativeWindow_refreshScreenLogicalDpi()。
- 自验证:构建 0 错误、回归/验收/Gpu 全绿;探针(miterLimit
  钳位/快照/像素墨量、折返三 Join 半圆、synthesized 同步、
  RESOURCE_MANAGER 改 123.5 刷新差分恰发 1 次)全 PASS。
- 约束偏差报备:XMouseEvent 定义在 XCode/XEvent(非独立头)、
  refreshScreenLogicalDpi 声明落 XPlatformNativeWindow.h,
  均为最小必要。

**串行批次二十二:x-qt-image 写方向编码**(2026-09-20,剪贴板
图像读写全闭环):
- 复用库内 XImageCodec_encode(Png)(canEncode 门闸),零自研;
  setImage/setPixmap/含 x-qt-image 的 setMimeData 三路汇入
  clipboard_pushImagePngToBackend:编码后 setMimeData("image/png")
  深拷贝推平台镜像,内部类型不以原名登记(平台对外 TARGETS
  与 Qt 一致);mime 已显式携带 image/png 时跳过防重复登记;
  编码失败仅平台侧无图像原子,进程内语义不变。
- CLIPBOARD/PRIMARY 双模式;先清后写镜像整体替换语义保持;
  外部认领后 owns/信号复位照旧。
- 自验证:构建 0 错误、回归/验收全绿;真机探针——4x3 ARGB
  渐变(含半透明)image 后 TARGETS 恰含 image/png,读回 137 字节
  PNG 解码 12/12 像素含 alpha 一致;同进程回读等值;桌面剪贴板
  管理器真实请求了该原子(外部可见性旁证)。
- 未尽:XMimeData_formats 列出 x-qt-image 与 Qt formats() 剔除
  内部类型的对齐属 XMimeData 模块决策(注释注明取舍);
  x-color 平台映射无约定维持跳过;后端 mimeData 借用语义在
  未来 INCR 改造时需复核读方向合并。

**串行批次轮收尾小结(批次三~十八,2026-09-19~20)**:
- 扫描报告 P0 全部清零(线宽变换缩放/Winding/截断/输入法接线/
  TOUCH-TABLET/剪贴板 PRIMARY+mime 多格式),P1 消化大部分
  (双线性、浮点重载、中键粘贴、EWMH flags、屏幕 DPI、
  filterEvent、text_subtype html、removeFormat、线条 AA、
  strokePath 描边器、虚线笔宽倍数、setClipPath 精确裁剪、
  热插拔差分、外部变更通知),P2 零散(图片 PNG 读取方向、INCR/
  MULTIPLE/SAVE_TARGETS、miterLimit API、synthesized 来源标志、
  多点触控列表、基类属性分发)转入下方遗留清单按需排期。
- 顺带根修三枚真 bug:XDate_dayOfWeek 偏一天、
  XMimeData 自定义条目取址损坏(+move 漏 urls)、
  XWidget 裸父悬挂指针;XScreen 登记自反抖动即时修复。
- 每批均:构建 0 错误+主回归零 FAIL+验收 68/68(+GPU 套件按需)
  +真机/ASan 探针自证;全部未提交 Git。

**串行批次十二:setClipPath 精确路径裁剪**(2026-09-20,Graphics
P1-B4,消 Task 2.20 既录偏差):
- 状态:m_clipPath 深拷贝(含 fillRule)+ 设置时变换快照
  (对标 Qt 裁剪路径冻结设备空间);合成复用 setClipRect 全语义
  (IntersectClip 求交/NoClip 清除/Picture 录制与查询同步);
  save/restore 仿 XRegion 所有权交接。
- 掩码:复用 AA 填充 4x4 覆盖机制按快照变换光栅化为 8 位掩码,
  (serial,目标图像) 惰性重建;putPixel 掩码门控(未设路径仅一次
  bool 判定零开销),AA 填充做覆盖率×掩码乘法;矩形路径退化为
  clipRect 快路径逐位一致;span/blit/整段填充快路径在路径裁剪
  激活时退逐像素。
- GPU:路径裁剪返回 false 走既有局部软件提交降级;Picture 指令集
  无操作码,录制退化包围盒近似(注释注明);clipPath() 返回副本。
- 自验证:构建 0 错误、回归/验收全绿、XGuiGpu_Test 通过;
  /tmp 19/19(圆形裁剪圆外零写入、交集、往返、save/restore、
  矩形逐位一致、20000 次压测 RSS 零增长)。
- 已登记偏差:路径∧路径相交按包围盒近似(单一路径不可表示);
  t211 断言同步为新行为(原断言硬编码旧偏差,注释说明)。

**扫描报告一:Graphics(XPainter) 域对齐差距**(要点,完整证据
见扫描原文,已核对变换族/38 合成模式/opacity/save-restore/
clip 语义均已对齐无需动):
- P0:①画笔宽度不随变换缩放(全部按 cosmetic 处理,scale(2,2)
  下线宽仍 1px,XPainter.c:1936);②fillPath/drawPath 无
  Winding 填充(硬编码 OddEven,XPainter.c:9563,XPainterPath
  无 fillRule 成员);③折线/多边形 128 点、drawTextRect 64 行
  静默截断(XPAINTER_POLY_MAX_POINTS)。
- P1:SmoothPixmapTransform 空操作(恒最近邻);线条无 AA
  (Antialiasing 仅作用于填充);strokePath 无几何描边(无
  join/cap 几何);setClipPath 仅包围盒;drawImage/drawPixmap
  浮点与 9 参重载缺失(内部参数已浮点化,成本低)。
- P2:旋转文本 AA 退化、3 个 hint 存储无消费、虚线节距不乘
  笔宽、图案画刷部分路径按基色近似、QFontMetrics 类等。
**扫描报告二:Input/Platform 域对齐差距**(要点;QInputMethod/
QPlatformInputContext API 面与信号集、剪贴板信号语义、
XPlatformNativeInterface 全套均已对齐无需动):
- P0:①输入法查询回调从未接线(XWidget_inputMethodQuery 存在
  但无调用点,cursorRectangle 等恒零);②TOUCH/TABLET 事件
  类型/负载齐但 WSI 无 handle 入口、平台无合成(整链路断);
  ③剪贴板后端契约仅纯文本(setImage/setMimeData 不经后端,
  图像/HTML 永远到不了 OS 剪贴板)。
- P1:外部剪贴板变化不通知(SelectionClear 只清平台镜像,
  dataChanged 不发);XPlatformInputContext_filterEvent 死代码;
  窗口 flags 运行时不同步(无 setWindowFlags 契约);屏幕/DPI
  域未接入(XScreen 无 RandR 回填,devicePixelRatio 硬编码 1.0);
  text_subtype 只报 plain。
- P2:InputMethodEvent 无 Attribute 列表、removeFormat 缺失、
  WSI 键鼠事件缺 timestamp/scanCode、XCursor 形状未映射 X11
  cursor font 等。

### 14.122 实机输入页复现与静态场景缓存修复（2026-09-19 午后二）

**用户实测反馈**:输入演示页交互后部分控件"不显示/花屏"。

**复现与定位**:demo autotest 实机键入 + 全页截图巡检 + PNG 像素级
扫描;逐层探针(对象状态/几何/可见性均正确→绘制层)确认:输入页
m_inputStatus 标签文本更新后,其区域在新旧内容**叠印**与**空白**
之间漂移——根因是 demo 静态场景缓存(XGUI_DEMO_STATIC_SCENE_CACHE_ON):
标签属缓存成员,setText 后未标脏,合成帧把缓存旧画与动态新画叠加。

**修复**:两处运行时写入点(textChanged 槽与滑块联动槽)在
XLabel_setText_2 后追加 m_staticSceneDirty + demo_repaint;
验证:叠印乱码消除,autotest 10 项 PASS(含实机键入 abcXYde/
中文+西文混排/选区高亮渲染)。

**遗留(下一轮主项)**:标签最终帧仍偶发不显示——静态场景缓存与
动态子控件的归属边界(哪些子控件入缓存、哪些走实时重绘)需要
按 Qt 的 backing store 脏区跟踪模型重理;探针证据链已存
(对象态→绘制层→合成层的完整排查路径)。

**验证**:主回归 exit=0 全绿、验收 64/64、autotest 10/10;
探针全部移除,构建零错误。

**14.122 补充(同日,xdotool 实机复现)**:用户反馈的"微调框没了/
画面缺很多"已在真窗口复现(resize 522x445 → 点击输入演示页签)。
证据链:demo_layout_content 几何转储证明**布局数据完全正确**
(微调框 (13,67,448,26) 等齐全),但画面残缺(微调框只剩箭头、
滑块只剩凹槽、进度条整体消失,且跨页内容渗漏)——判定为
**库级 paint 分发脏区语义问题**:根控件的静态场景背景块按脏区
memcpy 到后备存储后,仅"自身请求了更新"的子控件重绘,同脏区内
被背景抹掉的相邻控件不重绘,残缺持续到其下次自我更新。
**修复方向(下一轮主项)**:XWidget 绘制分发改为 Qt 语义——父级
PAINT 携带脏区 R 时,所有与 R 相交的后代都必须以 R∩自身 为裁剪
完整重绘(见 Src/XGui/Widget/XWidget.c 绘制树分发);demo 的
static tile 拷贝保留(仅作背景)。当前以临时方案缓解:交互后
demo_repaint 全窗标脏(已使 autotest 10/10 稳定)。

### 14.123 新旧 XLineEdit 绘制架构对比（用户指认回归,修复地图）

**用户实测**:微调框边框与内容不显示、输入框光标不对——迁移前
(HEAD)一切正常。

**HEAD 基线(工作正常)**:VXLineEdit_paintEvent 自绘文本/选区/
光标——直接用 WIDGET 字体(XPainter_setFont(painter,&m_font))+
自算 baseline/lineH,无中间层;光标焦点内常显。

**现行架构(回归温床)**:paintEvent 先 xlineedit_syncControlFont
(把 widget 字体深拷进控制器)再调 XLineControl_draw 一次画
正文/选区/光标——文本与光标全部来自控制器内部布局状态
(m_layoutText/m_layoutAscent/m_lineHeight,经 xlc_redoTextLayout)。
**多出的需精确同步的状态**:①syncControlFont 与 redoTextLayout
的时序(字体更新后布局必须重排);②光标显隐 = 控制器
blinkStatus/focus 状态与壳 focusIn/Out 的联动;③cursorToX 的
viewOffset 钳位口径。任一漂移即"光标不对/文本不可见"。

**下一轮修复步骤(按序)**:①对 XAbstractSpinBox 内嵌 XLineEdit
(占满控件场景)验证 syncControlFont→redoTextLayout 链路
(focusIn/字体下发后是否重排);②XLineControl_draw 的光标分支与
HEAD 常显语义逐行对齐(blinkStatus 置位时机);③layoutAscent 与
HEAD baseline 公式对拍;④XWidget 绘制分发脏区语义根修(14.122)。

**其余控制器同类对比结论(14.123 补充)**:XPlainTextEdit 绘制前
已有 xpe_syncControlFont(957 行)且 XTextControl_draw 用
self->m_font 只读浅拷贝(本轮已修 deinit UAF/setFont 深拷贝)——
无 XLineEdit 同型回归;XLabel 绘制为自绘路径(XWidget_font 深拷
贝逐次取用),渲染不依赖控制器字体——正常。**唯一未修的回归类
仍集中在 XLineControl_draw 链**(光标/blink/布局重排时机,见
①~③),按 §14.123 步骤执行即可。

**下轮建议**:同上①~④ + textcontrol-checklist 58 断言脚手架 +
回归文件泄漏族分批补删除。

### 14.121 掩码引擎根修 + 实机窗口验证轮（2026-09-19 午后）

**掩码引擎根修(C 组 20→0,验收 64/64 全绿)**:①internalInsert
掩码分支 replace 长度误传 0(纯插入)致每次击键净增一空槽,改为
按槽位序号换算被替换字节区的等槽替换(对标 Qt
m_text.replace(m_cursor, ms.size(), ms);UTF-8 槽位字节宽可变,
slotStart+msChars 经 charsByteLen 换算);②掩码整串拒绝(ms 空)
对齐 Qt 提前 return,不再置 textDirty/动光标(消除脏 text 系
信号与光标漂移);③探针实证 C1"---" 为清单笔误
("9999-99-99" 仅 2 个分隔符,stripString 输出本正确);
④C3 第二键/C6 尾空格/C12 撤销分组均为清单误推,已按 Qt 实测
语义校正测试(nextMaskBlank 跨分隔符 separate() 建组,一次
undo 回滚最后一组=Qt 真实行为)。

**实机窗口验证(X11 真窗口)**:demo autotest 追加文本控件键盘
注入(键入/光标移动/居中插入/程序化选区),9 项交互全 PASS:
"abcXYde" C=5 实机键入正确、中文+西文混排、选区高亮渲染、
微调/滑块/进度三联动;全页面截图巡检(QSS 背景/占位符/标题/
状态栏正常,右上角棋盘格为 demo 故意的脏区验证装饰)。

**验证**:主回归 exit=0 全绿;XLineControl 验收 64/64 PASS;
构建零错误。

**下轮建议**:textcontrol-checklist 58 断言脚手架;XPlainTextEdit
实机键入场景入 demo autotest;回归文件泄漏族分批补删除。

### 14.119 壳迁移缺陷批次修复 + 回归 OOM 根修（2026-09-19 上午）

**背景**:14.118 后壳迁移(XLineEdit→XLineControl/XPlainTextEdit、
XLabel→XTextControl)已落地但未提交;首次完整跑通回归暴露两颗
系统级地雷:①`XLineEdit_clear`→`XLineControl_clear`→
`xlc_removeSelectedText` 自尾向首入栈循环在 i 回退到选区起点后
`prevBoundary` 零进度原地打转,每圈压一条撤销命令,历史数组指数
扩容至 30G(实测 LD_PRELOAD 分配探针:单点 13 次 realloc 达
12G,RSS 37MB/s 线性暴涨,开机自愈 agent 跑 ctest 即打满
内存+swap 致桌面卡死/OOM);②`XTextControl` 六处把 self->m_font
浅拷贝到栈后 deinit,释放了共享的 m_family/m_styleName 堆串,
下次度量踩悬垂指针段错误(此前套件从未跑到,一修 30G 即现形)。

**控制器修复**:①removeSelectedText 循环补零进度断行
(同文件 prevCharsByteLen 已有同型防御);②六处浅拷贝读点去
deinit(只读不拥有,注释立约),setFont/font() getter 改
XCopy 深拷贝(对齐 XWidget_font Phase 3.2 裁定),setFont 改
度量/家族真变化才发 updateRequest(防壳同步→paint 回路);
③XLineControl 方向键四处改读解析后 layoutDirection(此前
Auto 缺省被当 RTL,Left/Right 反相);④del() 由 SkipWords
字节距离循环改为单字符删除(对齐 Qt nextCursorPosition
默认 SkipCharacters;internalDelete 每调用移除一个完整
UTF-8 序列);⑤撤销命令 m_uc[4]→m_uc[5](与 m_maskChar/
m_passwordCharacter 同一"4 字节满额+NUL"契约,根除
maskCharSet 越界写告警);⑥XTextControl Delete/Backspace/
覆盖删除三处字节-码点混用改码点粒度(壳级 xpe_eraseCodepoint
补偿并存,互补不双删)。

**壳修复**:XPlainTextEdit 补 xpe_syncControlFont(对齐
XLineEdit 模式)挂命中/光标矩形/鼠标/键盘/绘制五入口——此前
字体只在 create 时同步一次,widget 字体变更后控制器仍用旧度量
(cursorForPosition 行高失配);两处 setFont 调用点按新深拷贝
契约补 deinit。

**测试修复**(xgui_regression_test.c):XTextEdit undo/redo 四连
断言按 Qt 语义重写(程序化 setText 清空撤销栈,插入原语产生
快照——旧断言固化迁移前快照栈行为);moveCursor 魔数 6
(PreviousBlock)→11(End,对齐 Qt QTextCursor);pe 反查用例
固定 XFont8x16 字体(行高 16px 可预期,与默认轮廓字体解耦)。

**验证**:回归完整跑通 exit=0 全绿(含 12 控件套件与 phase32
契约族,峰值 RSS 73MB——此前同流程 30G+OOM);控制器三文件
零新增告警;残留测试期资源(XImage 434 init/3 delete 等)
为有界小额滞留,后续按 editor-delta-audit §5 增量消化。

**下轮建议**:回归文件泄漏族分批补删除(XImage 431 处为最大
族,建议按套件分组脚手架化);XTextEdit_canUndo 仍读哨兵栈
(行为正确,可选直连控制器口径);壳迁移三控件逐项像素比对
验收(参照 linecontrol/textcontrol checklist 断言)。

### 14.118 私有控制器落地轮（2026-09-19 夜间并发,五路子代理）

**§16 一期(共享文本层)集成完成**:XTextUtf8(码点边界,与两套原
实现逐点比对零分歧)/XTextClipboard(剪贴板往返,并集语义)/
XTextMenu(标准编辑菜单,ops 回调表)三模块入 XGuiConfig.h
(XTEXTUTF8/XTEXTCLIPBOARD/XTEXTMENU_ON)与构建;XLineEdit/
XPlainTextEdit 迁移完成(12 处边界调用/剪贴板/菜单构建器切换,
被吸收静态函数删除);XLabel 接入 XTextUtf8(label_utf8len 薄适配
+8 处无界扫描补行尾界);三文件 BOM 清除。已知语义收敛点:PlainTextEdit
空粘贴由早退改为空操作插入(压撤销快照);XLineEdit.m_clipboardText
成员保留恒 NULL(回退缓冲由服务层承载)。

**§16 二期(控制器对象化)双控制器落地**:
- XLineControl 5155 行(对标 QWidgetLineControl 全量 131 方法:
  文本模型/撤销分组/选区族/光标族/回显状态机/校验与输入掩码/
  IME/键盘/绘制数据/12 信号;三条状态机推演:密码回显三态、
  撤销分组边界、掩码解析逐分支);
- XTextControl 5001 行(对标 QWidgetTextControl 平铺行承载:
  方法面映射全表、16 信号真发射、撤销差量命令栈、拖选/双击/三击
  状态机、IME preedit 生命周期、链接命中注册表、draw 选区高亮);
- 两控制器 gcc -Wall -Wextra 零告警,编入库体(构建+回归真绿),
  尚未接入壳(下一波按增量审计迁移);
- 三份验收文档落盘 docs/xgui-audit/2026-09-19/:
  linecontrol-checklist.md(64 断言+状态机清单)、
  textcontrol-checklist.md(58 断言+14 易错点)、
  editor-delta-audit.md(三控件迁移增量图:整体搬移型/模型升级型/
  对齐迁移型 + 壳保留项 + 风险点)。

**下轮建议(按 editor-delta-audit.md 增量图执行壳迁移)**:
XLineEdit 整体搬移(20 项迁入+调色板注入+密码回显宿主判定)、
XPlainTextEdit 模型升级(16 项迁入,增量大于存量)、XLabel 对齐
迁移(9 项);迁移后逐控件像素比对验收(参照各 checklist 断言)。

#### 14.102 续（saveState/restoreState 往返失败——待查项）





- XHeaderView_saveState/restoreState 往返在最小复现中 restoreState
  返回 false(校验拒绝),序列化/解析字段序列已核对对称。
  需后续在 restoreState 校验链中逐步打断点定位(疑似
  XHEADERVIEW_STATE_VERSION 或 stateWriteInt/ReadInt 的位宽不匹配)。
- 影响:XHeaderView 状态保存/恢复暂不可用,段管理 API 本身正常。

#### 14.90 续二（ASan 定位 sortItems 类型混淆修复）

- ASan 精确定位 sortItems 写回阶段 heap-buffer-overflow：snapshot[i]
  (char*) 被误传给期望 const XString* 的 setData——char* 被当 XString*
  解引用 XContainer_memory 越界。修复：改调 setData_2(UTF-8 兼容重载)。
- ASan 下另确认 XHeaderView saveState/restoreState 往返已修复(补
  sortOrder 写入+orientation 写入宽度 WriteDigit→WriteInt+ReadInt
  跳前导空格)，连续 3 轮无崩溃无 FAIL。
- 3 轮稳定性验证：回归全绿零失败；全裁剪构建通过。

