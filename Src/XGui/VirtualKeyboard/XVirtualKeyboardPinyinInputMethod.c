/**
 * @file       XVirtualKeyboardPinyinInputMethod.c
 * @brief      XVirtualKeyboardPinyinInputMethod 拼音输入法实现（内嵌
 *             XPinyinEngine 状态机；Qt pinyin 插件形态对齐）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON && XKEYBOARD_IME_ON

#include "XVirtualKeyboardPinyinInputMethod.h"
#include "XVirtualKeyboardInputEngine.h"
#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardSelectionListModel.h"
#include "XEvent.h"
#include "XString.h"
#include "XMemory.h"

/* 前向声明。 */
static void XVkPinyin_deinit(XVirtualKeyboardPinyinInputMethod* self);
void XVirtualKeyboardPinyinInputMethod_init(
        XVirtualKeyboardPinyinInputMethod* self);

/** @brief 内部取插件对象（经基类指针回溯）。 */
static XVirtualKeyboardPinyinInputMethod* xvkpy_self(
        XVirtualKeyboardAbstractInputMethod* base)
{
    return (XVirtualKeyboardPinyinInputMethod*)base;
}

/** @brief 发射本插件 selectionListChanged(WordCandidateList) 信号。 */
static void xvkpy_emitListChanged(XVirtualKeyboardAbstractInputMethod* base)
{
    XObject* object = (XObject*)base;
    XVarList* args;
    int listType =
        (int)XVirtualKeyboardSelectionListModelType_WordCandidateList;
    if (!base || !object->m_signalSlot) return;
    args = XVarList_Create(XVar(int, listType));
    if (args) {
        XObject_emitSignal(object, (size_t)
                           XVirtualKeyboardAbstractInputMethod_selectionListChanged_signal(
                               NULL, 0),
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

/** @brief 组串/候选变化后的统一同步：preedit 镜像 + 候选列表信号。 */
static void xvkpy_sync(XVirtualKeyboardAbstractInputMethod* base)
{
    XVirtualKeyboardPinyinInputMethod* self = xvkpy_self(base);
    XVirtualKeyboardInputContext* ctx =
        XVirtualKeyboardAbstractInputMethod_inputContext(base);
    if (ctx) {
        XVirtualKeyboardInputContext_setPreeditText_2(
            ctx, XPinyinEngine_composingText(&self->m_ime));
    }
    xvkpy_emitListChanged(base);
}

/* ==================== 纯虚四件 ==================== */

/** @brief inputModes：状态机可用返回 {Pinyin, Latin}。 */
static int XVkPinyin_inputModes(XVirtualKeyboardAbstractInputMethod* base,
                                const char* locale, int* outModes,
                                int maxCount)
{
    (void)locale;
    if (outModes && maxCount > 0) {
        outModes[0] = (int)XVirtualKeyboardInputEngineInputMode_Pinyin;
        if (maxCount > 1)
            outModes[1] = (int)XVirtualKeyboardInputEngineInputMode_Latin;
    }
    return 2;
}

/** @brief setInputMode：Pinyin=中文态、Latin=EN 直写（setChinese 清组串）。 */
static bool XVkPinyin_setInputMode(XVirtualKeyboardAbstractInputMethod* base,
                                   const char* locale, int inputMode)
{
    XVirtualKeyboardPinyinInputMethod* self = xvkpy_self(base);
    (void)locale;
    if (inputMode == (int)XVirtualKeyboardInputEngineInputMode_Pinyin) {
        XPinyinEngine_resetComposition(&self->m_ime); /* 先复位（Qt 口径）。 */
        XPinyinEngine_setChinese(&self->m_ime, true);
        xvkpy_sync(base);
        return true;
    }
    if (inputMode == (int)XVirtualKeyboardInputEngineInputMode_Latin) {
        XPinyinEngine_resetComposition(&self->m_ime);
        XPinyinEngine_setChinese(&self->m_ime, false);
        xvkpy_sync(base);
        return true;
    }
    return false;
}

/** @brief setTextCase：接受但忽略（Qt pinyininputmethod.cpp:375-379 口径）。 */
static bool XVkPinyin_setTextCase(XVirtualKeyboardAbstractInputMethod* base,
                                  int textCase)
{
    (void)base;
    return textCase == (int)XVirtualKeyboardInputEngineTextCase_Lower ||
           textCase == (int)XVirtualKeyboardInputEngineTextCase_Upper;
}

/**
 * @brief keyEvent：按键→XPinyinEngine feed 映射。
 * @return 已消费返回 true；EN 态/未识别键返回 false（面板原语义放行）。
 */
static bool XVkPinyin_keyEvent(XVirtualKeyboardAbstractInputMethod* base,
                               int key, const char* text, uint32_t modifiers)
{
    XVirtualKeyboardPinyinInputMethod* self = xvkpy_self(base);
    XVirtualKeyboardInputContext* ctx =
        XVirtualKeyboardAbstractInputMethod_inputContext(base);
    char ch;
    (void)modifiers;
    if (!self) return false;
    if (!XPinyinEngine_isChinese(&self->m_ime)) return false; /* EN 直通。 */
    /* 键文本优先（面板统一以 UTF-8 文本喂入；单字节可打印才可组串）。 */
    ch = (text && text[0] && !text[1]) ? text[0] : (char)key;
    if (ch >= 'a' && ch <= 'z') {
        if (XPinyinEngine_feedLetter(&self->m_ime, ch) ==
            XPinyinEngineFeed_Ignored)
            return false;
        xvkpy_sync(base);
        return true;
    }
    if (key == XKey_Backspace || ch == '\b') {
        if (XPinyinEngine_feedBackspace(&self->m_ime) ==
            XPinyinEngineFeed_Ignored)
            return false; /* 空组串：透传编辑框退格。 */
        xvkpy_sync(base);
        return true;
    }
    if (key == XKey_Return || ch == '\r' || ch == '\n') {
        if (XPinyinEngine_feedCommitRaw(&self->m_ime) !=
            XPinyinEngineFeed_Committed)
            return false; /* IDLE：放行原换行语义。 */
        XVirtualKeyboardInputContext_commit_2(
            ctx, XPinyinEngine_commitString(&self->m_ime));
        xvkpy_sync(base);
        return true;
    }
    if (ch == ' ') {
        if (XPinyinEngine_feedCommitFirst(&self->m_ime) !=
            XPinyinEngineFeed_Committed)
            return false; /* IDLE：放行为真空格。 */
        XVirtualKeyboardInputContext_commit_2(
            ctx, XPinyinEngine_commitString(&self->m_ime));
        xvkpy_sync(base);
        return true;
    }
    if (ch >= '1' && ch <= '9') {
        /* 数字选候选：面板分页换算后经 selectItem→候选钩子路径；
           此处仅处理「组串中越界/无候选」吞掉语义。 */
        XPinyinEngineFeed feed = XPinyinEngine_feedDigit(&self->m_ime,
                                                       ch - '0');
        if (feed == XPinyinEngineFeed_Committed) {
            XVirtualKeyboardInputContext_commit_2(
                ctx, XPinyinEngine_commitString(&self->m_ime));
            xvkpy_sync(base);
            return true;
        }
        return feed == XPinyinEngineFeed_Consumed;
    }
    return false;
}

/* ==================== 候选钩子（SelectionListModel 数据源） ==================== */

static int XVkPinyin_selectionLists(XVirtualKeyboardAbstractInputMethod* base,
                                    int* outTypes, int maxCount)
{
    (void)base;
    if (outTypes && maxCount > 0)
        outTypes[0] =
            (int)XVirtualKeyboardSelectionListModelType_WordCandidateList;
    return 1;
}

static int XVkPinyin_selectionListItemCount(
        XVirtualKeyboardAbstractInputMethod* base, int type)
{
    XVirtualKeyboardPinyinInputMethod* self = xvkpy_self(base);
    if (type != (int)XVirtualKeyboardSelectionListModelType_WordCandidateList)
        return 0;
    return XPinyinEngine_candidateCount(&self->m_ime);
}

/** @brief 候选数据：Display=文本（新建 XVariant*）、补全长 0、词典
 *         Default、不可移除（本轮无用户词典面）。 */
static XVariant* XVkPinyin_selectionListData(
        XVirtualKeyboardAbstractInputMethod* base, int type, int index,
        int role)
{
    XVirtualKeyboardPinyinInputMethod* self = xvkpy_self(base);
    if (type != (int)XVirtualKeyboardSelectionListModelType_WordCandidateList)
        return NULL;
    switch (role) {
    case (int)XVirtualKeyboardSelectionListModelRole_Display: {
        const char* candidate =
            XPinyinEngine_candidateAt(&self->m_ime, index);
        if (!candidate) return NULL;
        return XString_toVariant_utf8(candidate);
    }
    case (int)XVirtualKeyboardSelectionListModelRole_WordCompletionLength: {
        int32_t zero = 0;
        return XVariant_create(&zero, sizeof(zero), XVariantType_Int32);
    }
    case (int)XVirtualKeyboardSelectionListModelRole_Dictionary: {
        int32_t dict =
            (int32_t)XVirtualKeyboardSelectionListModelDictionaryType_Default;
        return XVariant_create(&dict, sizeof(dict), XVariantType_Int32);
    }
    case (int)XVirtualKeyboardSelectionListModelRole_CanRemoveSuggestion: {
        bool removable = false;
        return XVariant_create(&removable, sizeof(removable),
                               XVariantType_Bool);
    }
    default:
        return NULL;
    }
}

/** @brief 候选选中（点选/翻页选择路径）：feedCandidate→commit。 */
static void XVkPinyin_selectionListItemSelected(
        XVirtualKeyboardAbstractInputMethod* base, int type, int index)
{
    XVirtualKeyboardPinyinInputMethod* self = xvkpy_self(base);
    XVirtualKeyboardInputContext* ctx =
        XVirtualKeyboardAbstractInputMethod_inputContext(base);
    if (type != (int)XVirtualKeyboardSelectionListModelType_WordCandidateList)
        return;
    if (XPinyinEngine_feedCandidate(&self->m_ime, index) !=
        XPinyinEngineFeed_Committed)
        return;
    XVirtualKeyboardInputContext_commit_2(
        ctx, XPinyinEngine_commitString(&self->m_ime));
    xvkpy_sync(base);
}

/** @brief 移除候选：无用户词典面→无操作返回 false（Qt 行为等价）。 */
static bool XVkPinyin_selectionListRemoveItem(
        XVirtualKeyboardAbstractInputMethod* base, int type, int index)
{
    (void)base; (void)type; (void)index;
    return false;
}

/** @brief reset：复位组串（closePopup 弃草稿链；不翻转中文态）。 */
static void XVkPinyin_reset(XVirtualKeyboardAbstractInputMethod* base)
{
    XVirtualKeyboardPinyinInputMethod* self = xvkpy_self(base);
    XPinyinEngine_resetComposition(&self->m_ime);
    xvkpy_sync(base);
}

/** @brief update：外部变化同步（编辑框变化后镜像组串）。 */
static void XVkPinyin_update(XVirtualKeyboardAbstractInputMethod* base)
{
    xvkpy_sync(base);
}

/* ==================== 生命周期 ==================== */

XVtable* XVirtualKeyboardPinyinInputMethod_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardPinyinInputMethod)
    XVTABLE_INHERIT_XCLASS(XVirtualKeyboardAbstractInputMethod);
    /* 本类无新增槽位（容量=基类槽位全长）：十一个实现是对基类已声明
       槽位的重载，必须逐槽 OVERLOAD 定位；ADD_FUNC_LIST 是追加语义，
       追加只会越界（静态表容量检查 exit）或落在无效偏移上。 */
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_InputModes,
        XVkPinyin_inputModes);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SetInputMode,
        XVkPinyin_setInputMode);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SetTextCase,
        XVkPinyin_setTextCase);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_KeyEvent,
        XVkPinyin_keyEvent);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SelectionLists,
        XVkPinyin_selectionLists);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SelectionListItemCount,
        XVkPinyin_selectionListItemCount);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SelectionListData,
        XVkPinyin_selectionListData);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SelectionListItemSelected,
        XVkPinyin_selectionListItemSelected);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_SelectionListRemoveItem,
        XVkPinyin_selectionListRemoveItem);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_Reset, XVkPinyin_reset);
    XVTABLE_OVERLOAD_DEFAULT(
        EXVirtualKeyboardAbstractInputMethod_Update, XVkPinyin_update);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVkPinyin_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：状态机无资源（纯数据）直通基类。 */
static void XVkPinyin_deinit(XVirtualKeyboardPinyinInputMethod* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XVirtualKeyboardAbstractInputMethod,
                         (XVirtualKeyboardAbstractInputMethod*)self);
}

void XVirtualKeyboardPinyinInputMethod_init(
        XVirtualKeyboardPinyinInputMethod* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XVirtualKeyboardAbstractInputMethod_init(&self->m_base);
    XClassSetVtable(self, XVirtualKeyboardPinyinInputMethod);
    XPinyinEngine_init(&self->m_ime); /* 状态机默认中文态/页容量 9。 */
}

XVirtualKeyboardPinyinInputMethod*
XVirtualKeyboardPinyinInputMethod_create_ex(XMemoryType memory)
{
    XVirtualKeyboardPinyinInputMethod* self =
        (XVirtualKeyboardPinyinInputMethod*)XMemory_malloc(sizeof(*self),
                                                           memory);
    if (!self) return NULL;
    XVirtualKeyboardPinyinInputMethod_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardPinyinInputMethod_factory(void)
{
    return &XVirtualKeyboardPinyinInputMethod_create()->m_base;
}

#endif /* XVIRTUALKEYBOARD_ON */
