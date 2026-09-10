/**
 * @file       XButtonGroup.c
 * @brief      按钮分组控件实现（对标 Qt 6.8 QButtonGroup 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XButtonGroup.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include <string.h>

#if XABSTRACTBUTTON_ON && XBUTTONGROUP_ON

/* ==================== 内部工具 ==================== */

/** @brief 发射带按钮参数的信号（无连接时释放参数，防泄漏）。 */
static void xbgroup_emitButton(XButtonGroup* self, size_t signal,
                               XAbstractButton* button)
{
    XVarList* args = XVarList_Create(XVar(XAbstractButton*, button));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射带 id 参数的信号。 */
static void xbgroup_emitId(XButtonGroup* self, size_t signal, int id)
{
    XVarList* args = XVarList_Create(XVar(int, id));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射带 (按钮, 选中) 参数的信号。 */
static void xbgroup_emitButtonToggled(XButtonGroup* self, size_t signal,
                                      XAbstractButton* button, bool checked)
{
    XVarList* args = XVarList_Create(XVar(XAbstractButton*, button),
                                     XVar(bool, checked));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射带 (id, 选中) 参数的信号。 */
static void xbgroup_emitIdToggled(XButtonGroup* self, size_t signal,
                                  int id, bool checked)
{
    XVarList* args = XVarList_Create(XVar(int, id), XVar(bool, checked));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 成员索引；非成员返回 -1。 */
static int xbgroup_indexOf(const XButtonGroup* self,
                           const XAbstractButton* button)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_buttons || !button) return -1;
    n = XVector_size_base((const XContainer*)self->m_buttons);
    for (i = 0; i < n; ++i) {
        XAbstractButton** item =
            (XAbstractButton**)XVector_at_base(self->m_buttons, i);
        if (item && *item == button) return (int)i;
    }
    return -1;
}

/** @brief 互斥：选中某按钮时取消其它成员选中（exclusive 语义）。 */
static void xbgroup_applyExclusive(XButtonGroup* self,
                                   XAbstractButton* checked)
{
    int64_t i;
    int64_t n;
    if (!self->m_exclusive || !self->m_buttons) return;
    n = XVector_size_base((const XContainer*)self->m_buttons);
    for (i = 0; i < n; ++i) {
        XAbstractButton** item =
            (XAbstractButton**)XVector_at_base(self->m_buttons, i);
        if (item && *item && *item != checked &&
            XAbstractButton_isChecked(*item))
            XAbstractButton_setChecked(*item, false);
    }
}

/* ==================== 成员按钮桥接（C 语言的 per-button 闭包等价物） ==================== */

XCLASS_DEFINE_BEGING(XBGroupBridge)
XCLASS_DEFINE_EXTEND_END(XBGroupBridge, XObject)

typedef struct XBGroupBridge
{
    XObject m_base;                /**< 基类成员；必须是第一个。 */
    XButtonGroup* m_group;         /**< 所属组（借用）。 */
    XAbstractButton* m_button;     /**< 桥接的成员按钮（借用）。 */
    bool m_buttonDead;             /**< 按钮已销毁（跳过断连）。 */
} XBGroupBridge;

static XVtable* XBGroupBridge_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBGroupBridge)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

static void xbgroup_bridgeClickedSlot(XObject* receiver, XVarList* args);
static void xbgroup_bridgePressedSlot(XObject* receiver, XVarList* args);
static void xbgroup_bridgeReleasedSlot(XObject* receiver, XVarList* args);
static void xbgroup_bridgeToggledSlot(XObject* receiver, XVarList* args);

static XBGroupBridge* xbgroup_bridgeCreate(XButtonGroup* group,
                                           XAbstractButton* button)
{
    XBGroupBridge* bridge =
        (XBGroupBridge*)XMemory_malloc(sizeof(*bridge),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!bridge) return NULL;
    memset(bridge, 0, sizeof(*bridge));
    XObject_init(&bridge->m_base);
    XClassSetVtable(bridge, XBGroupBridge);
    Set_Class_Memory(bridge, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(bridge, true);
    bridge->m_group = group;
    bridge->m_button = button;
    return bridge;
}

static void xbgroup_bridgeDestroy(XBGroupBridge* bridge)
{
    if (!bridge) return;
    if (bridge->m_buttonDead) {
        /* 按钮已销毁：其连接表随之消亡，直接删除桥。 */
        XClass_delete_base((XClass*)bridge);
        return;
    }
    /* 桥销毁前必须断开按钮侧连接，避免按钮信号访问已释放的桥。 */
    XObject_disconnect_1((XObject*)bridge->m_button,
                         XSignal(XAbstractButton_clicked_signal),
                         (XObject*)bridge, xbgroup_bridgeClickedSlot);
    XObject_disconnect_1((XObject*)bridge->m_button,
                         XSignal(XAbstractButton_pressed_signal),
                         (XObject*)bridge, xbgroup_bridgePressedSlot);
    XObject_disconnect_1((XObject*)bridge->m_button,
                         XSignal(XAbstractButton_released_signal),
                         (XObject*)bridge, xbgroup_bridgeReleasedSlot);
    XObject_disconnect_1((XObject*)bridge->m_button,
                         XSignal(XAbstractButton_toggled_signal),
                         (XObject*)bridge, xbgroup_bridgeToggledSlot);
    XClass_delete_base((XClass*)bridge);
}

/** @brief toggled 转发：维护 checkedButton/checkedId、互斥、发射信号。 */
static void xbgroup_bridgeToggledSlot(XObject* receiver, XVarList* args)
{
    XBGroupBridge* bridge = (XBGroupBridge*)receiver;
    XButtonGroup* self;
    XAbstractButton* button;
    int id;
    if (!bridge || !bridge->m_group || !bridge->m_button || !args) return;
    self = bridge->m_group;
    button = bridge->m_button;
    XVarList_args_1(args, bool, checked);
    id = XButtonGroup_id(self, button);
    if (checked) {
        xbgroup_applyExclusive(self, button);
        self->m_checkedButton = button;
    } else if (self->m_checkedButton == button) {
        self->m_checkedButton = NULL;
    }
    xbgroup_emitButtonToggled(self,
        (size_t)XButtonGroup_buttonToggled_signal, button, checked);
    xbgroup_emitIdToggled(self, (size_t)XButtonGroup_idToggled_signal,
                          id, checked);
}

/** @brief clicked 转发。 */
static void xbgroup_bridgeClickedSlot(XObject* receiver, XVarList* args)
{
    XBGroupBridge* bridge = (XBGroupBridge*)receiver;
    XButtonGroup* self;
    XAbstractButton* button;
    if (!bridge || !bridge->m_group || !bridge->m_button || !args) return;
    self = bridge->m_group;
    button = bridge->m_button;
    xbgroup_emitButton(self, (size_t)XButtonGroup_buttonClicked_signal,
                       button);
    xbgroup_emitId(self, (size_t)XButtonGroup_idClicked_signal,
                   XButtonGroup_id(self, button));
}

/** @brief pressed 转发。 */
static void xbgroup_bridgePressedSlot(XObject* receiver, XVarList* args)
{
    XBGroupBridge* bridge = (XBGroupBridge*)receiver;
    XButtonGroup* self;
    XAbstractButton* button;
    if (!bridge || !bridge->m_group || !bridge->m_button || !args) return;
    self = bridge->m_group;
    button = bridge->m_button;
    xbgroup_emitButton(self, (size_t)XButtonGroup_buttonPressed_signal,
                       button);
    xbgroup_emitId(self, (size_t)XButtonGroup_idPressed_signal,
                   XButtonGroup_id(self, button));
}

/** @brief released 转发。 */
static void xbgroup_bridgeReleasedSlot(XObject* receiver, XVarList* args)
{
    XBGroupBridge* bridge = (XBGroupBridge*)receiver;
    XButtonGroup* self;
    XAbstractButton* button;
    if (!bridge || !bridge->m_group || !bridge->m_button || !args) return;
    self = bridge->m_group;
    button = bridge->m_button;
    xbgroup_emitButton(self, (size_t)XButtonGroup_buttonReleased_signal,
                       button);
    xbgroup_emitId(self, (size_t)XButtonGroup_idReleased_signal,
                   XButtonGroup_id(self, button));
}

/** @brief 按钮 destroyed 转发：按钮先于组销毁时自动移除成员并删桥
 *         （按钮连接表已随对象消亡，跳过断连直接删桥）。 */
static void xbgroup_bridgeDestroyedSlot(XObject* receiver, XVarList* args)
{
    XBGroupBridge* bridge = (XBGroupBridge*)receiver;
    XButtonGroup* self;
    XAbstractButton* button;
    int64_t i;
    int64_t n;
    int index = -1;
    if (!bridge || !bridge->m_group) return;
    self = bridge->m_group;
    bridge->m_buttonDead = true;
    button = bridge->m_button;
    if (!self->m_bridges) return;
    n = XVector_size_base((const XContainer*)self->m_bridges);
    for (i = 0; i < n; ++i) {
        XBGroupBridge** bp =
            (XBGroupBridge**)XVector_at_base(self->m_bridges, i);
        if (bp && *bp == bridge) {
            index = (int)i;
            break;
        }
    }
    if (index >= 0) {
        int id = -1;
        if (self->m_ids) {
            int* pid = (int*)XVector_at_base(self->m_ids, index);
            if (pid) id = *pid;
        }
        if (self->m_buttons)
            XVector_remove_base(self->m_buttons, index, 1);
        if (self->m_ids)
            XVector_remove_base(self->m_ids, index, 1);
        XVector_remove_base(self->m_bridges, index, 1);
        if (self->m_checkedButton == button)
            self->m_checkedButton = NULL;
        XClass_delete_base((XClass*)bridge);
    }
}

/** @brief 为成员按钮创建桥并连接四类信号。 */
static XBGroupBridge* xbgroup_connectButton(XButtonGroup* self,
                                            XAbstractButton* button)
{
    XBGroupBridge* bridge = xbgroup_bridgeCreate(self, button);
    if (!bridge) return NULL;
    XObject_connect_1((XObject*)button,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)bridge, xbgroup_bridgeClickedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)button,
                      XSignal(XAbstractButton_pressed_signal),
                      (XObject*)bridge, xbgroup_bridgePressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)button,
                      XSignal(XAbstractButton_released_signal),
                      (XObject*)bridge, xbgroup_bridgeReleasedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)button,
                      XSignal(XAbstractButton_toggled_signal),
                      (XObject*)bridge, xbgroup_bridgeToggledSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)button,
                      XSignal(XObject_destroyed_signal),
                      (XObject*)bridge, xbgroup_bridgeDestroyedSlot,
                      XConnectionType_Direct);
    return bridge;
}

/* ==================== 生命周期与虚表 ==================== */

/** @brief 析构：释放成员/ID 数组（按钮本身不归组所有）。 */
static void VX_buttonGroup_deinit(XButtonGroup* self)
{
    if (!self) return;
    if (self->m_buttons) {
        XVector_delete_base(self->m_buttons);
        self->m_buttons = NULL;
    }
    if (self->m_ids) {
        XVector_delete_base(self->m_ids);
        self->m_ids = NULL;
    }
    if (self->m_bridges) {
        int64_t i;
        int64_t n = XVector_size_base((const XContainer*)self->m_bridges);
        for (i = 0; i < n; ++i) {
            XBGroupBridge** bp =
                (XBGroupBridge**)XVector_at_base(self->m_bridges, i);
            if (bp && *bp)
                xbgroup_bridgeDestroy(*bp);
        }
        XVector_delete_base(self->m_bridges);
        self->m_bridges = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XButtonGroup_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XButtonGroup)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_buttonGroup_deinit);
    return XVTABLE_DEFAULT;
}

void XButtonGroup_init(XButtonGroup* self, XObject* parent)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    if (parent)
        XObject_setParent((XObject*)self, parent);
    XClassSetVtable(self, XButtonGroup);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_exclusive = true;
    self->m_checkedButton = NULL;
    self->m_buttons = XVector_Create(XAbstractButton*);
    self->m_ids = XVector_Create(int);
    self->m_bridges = XVector_Create(XBGroupBridge*);
}

XButtonGroup* XButtonGroup_create_ex(XMemoryType memory, XObject* parent)
{
    XButtonGroup* self = (XButtonGroup*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XButtonGroup_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 成员与属性 ==================== */

bool XButtonGroup_exclusive(const XButtonGroup* self)
{
    return self ? self->m_exclusive : false;
}

void XButtonGroup_setExclusive(XButtonGroup* self, bool exclusive)
{
    if (!self) return;
    self->m_exclusive = exclusive;
}

void XButtonGroup_addButton(XButtonGroup* self, XAbstractButton* button,
                            int id)
{
    int nextId;
    if (!self || !button ||
        xbgroup_indexOf(self, button) >= 0) return;
    if (!self->m_buttons || !self->m_ids) return;
    /* 对标 Qt：id < 0 时按负序自动分配（-1、-2、…）。 */
    if (id < 0) {
        /* 对标 Qt：自动 id 从 -2 起按负序递减（mapping 最小值决定）。 */
        nextId = -2;
        for (;;) {
            if (XButtonGroup_button(self, nextId) == NULL) break;
            --nextId;
        }
        id = nextId;
    }
    XVector_push_back_1_base(self->m_buttons, &button);
    XVector_push_back_1_base(self->m_ids, &id);
    {
        XBGroupBridge* bridge = xbgroup_connectButton(self, button);
        XVector_push_back_1_base(self->m_bridges, &bridge);
    }
    if (XAbstractButton_isChecked(button)) {
        xbgroup_applyExclusive(self, button);
        self->m_checkedButton = button;
    }
}

void XButtonGroup_removeButton(XButtonGroup* self, XAbstractButton* button)
{
    int index;
    int id;
    if (!self || !self->m_buttons || !self->m_ids) return;
    index = xbgroup_indexOf(self, button);
    if (index < 0) return;
    id = *(int*)XVector_at_base(self->m_ids, index);
    {
        XBGroupBridge** bp =
            (XBGroupBridge**)XVector_at_base(self->m_bridges, index);
        if (bp && *bp)
            xbgroup_bridgeDestroy(*bp);
    }
    XVector_remove_base(self->m_bridges, index, 1);
    XVector_remove_base(self->m_buttons, index, 1);
    XVector_remove_base(self->m_ids, index, 1);
    if (self->m_checkedButton == button)
        self->m_checkedButton = NULL;
}

const XVector* XButtonGroup_buttons(const XButtonGroup* self)
{
    return self ? self->m_buttons : NULL;
}

XAbstractButton* XButtonGroup_button(const XButtonGroup* self, int id)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_buttons || !self->m_ids) return NULL;
    n = XVector_size_base((const XContainer*)self->m_ids);
    for (i = 0; i < n; ++i) {
        int* pid = (int*)XVector_at_base(self->m_ids, i);
        XAbstractButton** item =
            (XAbstractButton**)XVector_at_base(self->m_buttons, i);
        if (pid && *pid == id && item) return *item;
    }
    return NULL;
}

XAbstractButton* XButtonGroup_checkedButton(const XButtonGroup* self)
{
    return self ? self->m_checkedButton : NULL;
}

int XButtonGroup_checkedId(const XButtonGroup* self)
{
    if (!self || !self->m_checkedButton) return -1;
    return XButtonGroup_id(self, self->m_checkedButton);
}

void XButtonGroup_setId(XButtonGroup* self, XAbstractButton* button, int id)
{
    int index;
    if (!self || !self->m_ids) return;
    index = xbgroup_indexOf(self, button);
    if (index < 0) return;
    *(int*)XVector_at_base(self->m_ids, index) = id;
}

int XButtonGroup_id(const XButtonGroup* self, XAbstractButton* button)
{
    int index;
    if (!self || !self->m_ids) return -1;
    index = xbgroup_indexOf(self, button);
    if (index < 0) return -1;
    return *(int*)XVector_at_base(self->m_ids, index);
}

/* ==================== 信号 ==================== */

void* XButtonGroup_buttonClicked_signal(XButtonGroup* self,
                                        XAbstractButton* button)
{
    (void)self;
    (void)button;
    return (void*)(size_t)XButtonGroup_buttonClicked_signal;
}

void* XButtonGroup_buttonPressed_signal(XButtonGroup* self,
                                        XAbstractButton* button)
{
    (void)self;
    (void)button;
    return (void*)(size_t)XButtonGroup_buttonPressed_signal;
}

void* XButtonGroup_buttonReleased_signal(XButtonGroup* self,
                                         XAbstractButton* button)
{
    (void)self;
    (void)button;
    return (void*)(size_t)XButtonGroup_buttonReleased_signal;
}

void* XButtonGroup_buttonToggled_signal(XButtonGroup* self,
                                        XAbstractButton* button, bool checked)
{
    (void)self;
    (void)button;
    (void)checked;
    return (void*)(size_t)XButtonGroup_buttonToggled_signal;
}

void* XButtonGroup_idClicked_signal(XButtonGroup* self, int id)
{
    (void)self;
    (void)id;
    return (void*)(size_t)XButtonGroup_idClicked_signal;
}

void* XButtonGroup_idPressed_signal(XButtonGroup* self, int id)
{
    (void)self;
    (void)id;
    return (void*)(size_t)XButtonGroup_idPressed_signal;
}

void* XButtonGroup_idReleased_signal(XButtonGroup* self, int id)
{
    (void)self;
    (void)id;
    return (void*)(size_t)XButtonGroup_idReleased_signal;
}

void* XButtonGroup_idToggled_signal(XButtonGroup* self, int id, bool checked)
{
    (void)self;
    (void)id;
    (void)checked;
    return (void*)(size_t)XButtonGroup_idToggled_signal;
}

#endif /* XABSTRACTBUTTON_ON && XBUTTONGROUP_ON */
