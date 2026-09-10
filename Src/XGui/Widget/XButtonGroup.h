/**
 * @file       XButtonGroup.h
 * @brief      XButtonGroup 按钮逻辑分组（对标 Qt 6.8 QButtonGroup 全部
 *             公共 API）。
 * @details    功能范围：
 *             - 成员管理：addButton（可指定 id，默认 -1 自动分配）、
 *               removeButton、buttons（按加入顺序）、button(id)、
 *               setId/id、checkedButton/checkedId；
 *             - 互斥：exclusive（默认 true）——组内某按钮选中时自动
 *               取消其它按钮的选中；
 *             - 信号：buttonClicked/buttonPressed/buttonReleased/
 *               buttonToggled（按钮维度）与 idClicked/idPressed/
 *               idReleased/idToggled（id 维度）。
 *             XButtonGroup 为逻辑分组对象（继承 XObject，非控件），
 *             与 Qt 相同不负责成员按钮的销毁。
 * @note       模块总开关 XBUTTONGROUP_ON 定义于 XGuiConfig.h；=0 时
 *             裁剪全部公共 API。依赖 XOBJECT_ON、XABSTRACTBUTTON_ON。
 * @author     XinYueC 团队
 */
#ifndef XBUTTONGROUP_H
#define XBUTTONGROUP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XObject.h"
#if XABSTRACTBUTTON_ON
#include "XAbstractButton.h"
#endif

#if XABSTRACTBUTTON_ON && XBUTTONGROUP_ON

XCLASS_DEFINE_BEGING(XButtonGroup)
XCLASS_DEFINE_EXTEND_END(XButtonGroup, XObject)

/**
 * @brief      XButtonGroup 分组对象；m_base 必须是第一个成员。
 */
typedef struct XButtonGroup
{
    XObject m_base;        /**< 基类成员；必须是第一个。 */
    XVector* m_buttons;    /**< 成员按钮借用指针数组（XAbstractButton*）。 */
    XVector* m_ids;        /**< 与成员顺序一致的 id 数组（int）。 */
    XVector* m_bridges;    /**< 与成员顺序一致的内信号桥（XBGroupBridge*）。 */
    XAbstractButton* m_checkedButton;
                           /**< 当前选中按钮（借用；对标 d->checkedButton）。 */
    bool m_exclusive;      /**< 互斥（默认 true）。 */
} XButtonGroup;

/* ==================== 生命周期 ==================== */

XVtable* XButtonGroup_class_init(void);
void XButtonGroup_init(XButtonGroup* self, XObject* parent);
#define XButtonGroup_create(parent) XButtonGroup_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
XButtonGroup* XButtonGroup_create_ex(XMemoryType memory, XObject* parent);
#define XButtonGroup_deinit_base(self) XObject_deinit_base((XObject*)(self))
#define XButtonGroup_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 成员与属性（对标 QButtonGroup public API） ==================== */

/** @brief 查询互斥标志（默认 true）。 */
/**
 * @brief      获取互斥模式。
 */
bool XButtonGroup_exclusive(const XButtonGroup* self);
/** @brief 设置互斥标志。 */
/**
 * @brief      设置互斥模式。
 */
void XButtonGroup_setExclusive(XButtonGroup* self, bool exclusive);
/** @brief 加入按钮；id < 0 时自动分配（对标 addButton）。 */
/**
 * @brief      添加按钮到组。
 */
void XButtonGroup_addButton(XButtonGroup* self, XAbstractButton* button,
                            int id);
/** @brief 移除按钮（对标 removeButton）。 */
/**
 * @brief      从组移除按钮。
 */
void XButtonGroup_removeButton(XButtonGroup* self, XAbstractButton* button);
/** @brief 按加入顺序返回成员按钮数组（XAbstractButton* 元素；
 *         无成员返回 NULL 或空数组，对标 buttons()）。 */
const XVector* XButtonGroup_buttons(const XButtonGroup* self);
/** @brief 按 id 查询成员按钮；未找到返回 NULL。 */
/**
 * @brief      获取标准按钮。
 */
XAbstractButton* XButtonGroup_button(const XButtonGroup* self, int id);
/** @brief 查询当前选中按钮；无选中返回 NULL。 */
/**
 * @brief      获取选中按钮。
 */
XAbstractButton* XButtonGroup_checkedButton(const XButtonGroup* self);
/** @brief 查询当前选中按钮 id；无选中返回 -1。 */
/**
 * @brief      获取选中 ID。
 */
int XButtonGroup_checkedId(const XButtonGroup* self);
/** @brief 设置成员按钮的 id（对标 setId；非成员忽略）。 */
/**
 * @brief      设置按钮 ID。
 */
void XButtonGroup_setId(XButtonGroup* self, XAbstractButton* button, int id);
/** @brief 查询成员按钮的 id；非成员返回 -1。 */
/**
 * @brief      获取按钮 ID。
 */
int XButtonGroup_id(const XButtonGroup* self, XAbstractButton* button);

/* ==================== 信号（对标 QButtonGroup signals） ==================== */

/**
 * @brief      按钮点击信号（真发射）。
 */
void* XButtonGroup_buttonClicked_signal(XButtonGroup* self,
                                        XAbstractButton* button);
/**
 * @brief      按钮按下信号（真发射）。
 */
void* XButtonGroup_buttonPressed_signal(XButtonGroup* self,
                                        XAbstractButton* button);
/**
 * @brief      按钮释放信号（真发射）。
 */
void* XButtonGroup_buttonReleased_signal(XButtonGroup* self,
                                         XAbstractButton* button);
/**
 * @brief      按钮切换信号（真发射）。
 */
void* XButtonGroup_buttonToggled_signal(XButtonGroup* self,
                                        XAbstractButton* button, bool checked);
void* XButtonGroup_idClicked_signal(XButtonGroup* self, int id);
void* XButtonGroup_idPressed_signal(XButtonGroup* self, int id);
void* XButtonGroup_idReleased_signal(XButtonGroup* self, int id);
void* XButtonGroup_idToggled_signal(XButtonGroup* self, int id, bool checked);

#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTBUTTON_ON && XBUTTONGROUP_ON */

#ifdef __cplusplus
}
#endif
#endif /* XBUTTONGROUP_H */
