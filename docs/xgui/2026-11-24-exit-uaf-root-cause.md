# 退出段 UAF 根因（HsfClient / HsfVfdProbe，2026-11-24）

## 结论（两条独立根因，均已修）

### 根因 A：动态属性 value 被「按返回值释放」二次释放（主因）

- 位置：`HighSpeedFilter/client/ui/HsfUi.c` 的 `hsfUi_tagFontBase`
  （字号基准登记，`hsfFontBase` 动态属性）。
- 错误写法：
  ```c
  value = XVariant_create_int(basePx);
  if (!XObject_setProperty((XObject*)w, &name, value))
      XClassDelete((XClass*)value);      /* ← 错：setProperty 恒返回 false */
  ```
- `XObject_setProperty` 的返回值是 Qt 语义「是否命中**静态**属性」；本仓库
  静态属性层未实现 ⇒ **动态写入恒返回 false**，与「是否接管 value」无关。
  且 setProperty 在**所有**路径（含参数非法/分配失败）都会接管并释放
  value。于是 value 刚存进 `m_dynamicPropertyValues` 就被调用方释放 ⇒
  对象析构时对同一指针二次 `XClassDelete` ⇒
  `XClass_deinit_base` 读到 `vtable=0xFEEEFEEE`（堆释放填充）⇒ AV。
- 现象：HsfClient `--no-login` 退出 3/3 段错误；崩溃栈恒为
  `XObject_deinit → 动态属性 values 循环 → XClass_delete_base`。
- 修法：`(void)XObject_setProperty(...)`，调用方永不释放 value。
- 同族检查：`XStateMachine_writeProperty` / `XMenu` / `XToolButton` /
  `XFileDialog` / `XWidget` 均已是「(void) 调用 + 预失败才释放」的正确写法；
  全仓只有 HsfUi.c 这一处漏改。

### 根因 B：自定义 Deinit 重载漏调父类链（同族三处）

自定义 `EXClass_Deinit` 重载**必须**在末尾链到直接父类的 deinit，否则
`VXObject_deinit` 整段被跳过：不摘父链 `m_children`、不回收子对象、不销毁
信号、不 deref threadData ⇒ 对象被 `memory->free` 后仍留在父的 children
列表里 ⇒ 父析构时对已释放内存二次析构（内存复用后 vtable 已换成别的类）。
本轮实测三处：

| 位置 | 类 | 修法 |
|---|---|---|
| `HighSpeedFilter/client/ui/pages/HsfHomePage.c` `VHsfHomeDialWidget_deinit` | HsfHomeDialWidget（倒计时表盘） | 末尾 `XClass_Deinit_Parent(XWidget, obj)` |
| `HighSpeedFilter/server/HsfServerStore.c` `VXHsfServerStore_deinit` | HsfServerStore | 末尾 `XClass_Deinit_Parent(XObject, obj)` |
| `Src/XCore/XProtocol/XHttp/XServerChan.c` `VXServerChan_deinit` | XServerChan | 末尾 `XClass_Deinit_Parent(XObject, self)` |

正确样板：`VHsfFmGauge_deinit` / `VHsfVfdGaugeWidget_deinit` /
`VHsfLoginWindow_deinit` / `VHsfMultiSelectPopup_deinit`。
（`VXAnimationDriver_deinit` 用 `XVtableGetFunc(XObject_class_init(),
EXClass_Deinit, ...)` 直调，等价合法，不是缺陷。）

## 定位手法（可复用）

1. **cdb 抓第一现场**：`sxe av; g; kb` —— 崩溃指令
   `mov rax,[rax] ds:feeefeee`feeefeee` 即 `object->m_vtable` 是释放填充，
   直接判定「对象内存已释放再被析构」。
2. **活对象表 + 释放探针**（临时诊断，已回退）：
   - `XObject_init` 登记活对象、`VXObject_deinit` 摘除 + 死表；
   - 系统槽 `xmemory_system_free` 前置探针：指针仍命中活表 ⇒
     「free 但没有 deinit」并打印 dbghelp 符号化栈；
   - `XClass_deinit_base` 入口守卫：死表命中且 vtable 未变 ⇒ 二次析构；
   - 父析构 child 循环校验 `child 在活表 && child->m_parent == parent`；
   - 动态属性 values 循环校验 value 的 vtable 是否释放填充。
   注意：`XClass_delete_base` 直调 `memory->free`，探针必须挂在**槽函数**
   （`xmemory_system_free`）上，挂在 `XMemory_free` 封装上抓不到。
3. **地址复用会制造假阳性**：同一地址先前的 XString/XFile 释放记录会被新
   对象复用；「死表命中」必须叠加「当前 vtable 仍是当时 vtable（或释放
   填充）」才判二次析构，否则会拦下合法的新对象析构（内存泄漏）。

## 验收

- HsfClient `--no-login` 2s 自动退出：修复前 3/3 exit=139；修复后 8/8 exit=0。
- HsfVfdProbe：修复前 exit=139（收尾段错误，此前挂账）；修复后
  **166 [OK] / 1 [FAIL]**，FAIL 是既有「点设备状态表表头：无排序指示」，
  收尾不再崩（exit=1 仅来自该断言）。
- `XGuiRegression_Test`：exit=0（`XGui regression tests passed`）。
