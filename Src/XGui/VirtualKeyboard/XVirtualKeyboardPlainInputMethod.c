/**
 * @file       XVirtualKeyboardPlainInputMethod.c
 * @brief      XVirtualKeyboardPlainInputMethod 拉丁直通输入法实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardPlainInputMethod.h"
#include "XVirtualKeyboardInputEngine.h"
#include "XMemory.h"

/* 前向声明。 */
static void XVkPlain_deinit(XVirtualKeyboardPlainInputMethod* self);
void XVirtualKeyboardPlainInputMethod_init(
        XVirtualKeyboardPlainInputMethod* self);

/* ==================== 虚槽实现 ==================== */

/** @brief inputModes：{Latin}（locale 无关）。 */
static int XVkPlain_inputModes(XVirtualKeyboardAbstractInputMethod* self,
                               const char* locale, int* outModes,
                               int maxCount)
{
    (void)self; (void)locale;
    if (outModes && maxCount > 0)
        outModes[0] = (int)XVirtualKeyboardInputEngineInputMode_Latin;
    return 1;
}

/** @brief setInputMode：仅接受 Latin。 */
static bool XVkPlain_setInputMode(XVirtualKeyboardAbstractInputMethod* self,
                                  const char* locale, int inputMode)
{
    (void)self; (void)locale;
    return inputMode == (int)XVirtualKeyboardInputEngineInputMode_Latin;
}

/** @brief setTextCase：接受并应用（直通无组串，仅记录语义）。 */
static bool XVkPlain_setTextCase(XVirtualKeyboardAbstractInputMethod* self,
                                 int textCase)
{
    (void)self;
    return textCase == (int)XVirtualKeyboardInputEngineTextCase_Lower ||
           textCase == (int)XVirtualKeyboardInputEngineTextCase_Upper;
}

/** @brief keyEvent：恒 false（拉丁直通，面板既有写入链承载）。 */
static bool XVkPlain_keyEvent(XVirtualKeyboardAbstractInputMethod* self,
                              int key, const char* text, uint32_t modifiers)
{
    (void)self; (void)key; (void)text; (void)modifiers;
    return false;
}

/* ==================== 生命周期 ==================== */

XVtable* XVirtualKeyboardPlainInputMethod_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardPlainInputMethod)
    XVTABLE_INHERIT_XCLASS(XVirtualKeyboardAbstractInputMethod);
    /* 本类无新增槽位（容量=基类槽位全长）：四个实现是对基类已声明
       槽位的重载，必须逐槽 OVERLOAD 定位；ADD_FUNC_LIST 是追加语义，
       追加只会越界（静态表容量检查 exit）或落在无效偏移上。 */
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_InputModes,
        XVkPlain_inputModes);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SetInputMode,
        XVkPlain_setInputMode);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SetTextCase,
        XVkPlain_setTextCase);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_KeyEvent,
        XVkPlain_keyEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVkPlain_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：直通基类（无自有资源）。 */
static void XVkPlain_deinit(XVirtualKeyboardPlainInputMethod* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XVirtualKeyboardAbstractInputMethod,
                         (XVirtualKeyboardAbstractInputMethod*)self);
}

void XVirtualKeyboardPlainInputMethod_init(
        XVirtualKeyboardPlainInputMethod* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XVirtualKeyboardAbstractInputMethod_init(&self->m_base);
    XClassSetVtable(self, XVirtualKeyboardPlainInputMethod);
}

XVirtualKeyboardPlainInputMethod*
XVirtualKeyboardPlainInputMethod_create_ex(XMemoryType memory)
{
    XVirtualKeyboardPlainInputMethod* self =
        (XVirtualKeyboardPlainInputMethod*)XMemory_malloc(sizeof(*self),
                                                          memory);
    if (!self) return NULL;
    XVirtualKeyboardPlainInputMethod_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardPlainInputMethod_factory(void)
{
    return &XVirtualKeyboardPlainInputMethod_create()->m_base;
}

#endif /* XVIRTUALKEYBOARD_ON */
