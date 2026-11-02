/**
 * @file       XVfdCommandEnum.c
 * @brief      离散命令枚举通用基类实现。
 * @details    本类只含「命令码表 → 命令槽写入」的通用骨架；品牌知识（命令码
 *             取值、地址、缩放、故障表）全在品牌子类。
 */
#include "XVfdCommandEnum.h"
#if XVFD_ON
#if XVFD_COMMANDENUM_ON

#include "XVfdCommandEnum_Protected.h"
#include "XVfdDevice_Protected.h"
#include <string.h>

/* ==================== 命令码表 ==================== */

void XVfdCommandEnum_setCommands(XVfdCommandEnum* dev, const XVfdCommandEnumItem* items,
                                 int count)
{
    if (!dev) return;
    dev->m_items = items;
    dev->m_itemCount = (items && count > 0) ? count : 0;
}

bool XVfdCommandEnum_findCode(const XVfdCommandEnum* dev, uint16_t op, bool reverse,
                               int16_t* outCode)
{
    int i;
    if (!dev || !outCode || !dev->m_items || dev->m_itemCount <= 0) return false;
    for (i = 0; i < dev->m_itemCount; ++i) {
        const XVfdCommandEnumItem* it = &dev->m_items[i];
        if (!it) continue;
        if ((uint16_t)it->m_op != op) continue;
        if ((it->m_reverse ? true : false) != reverse) continue;
        *outCode = it->m_code;
        return true;
    }
    return false;
}

const XVfdCommandEnumItem* XVfdCommandEnum_items_const(const XVfdCommandEnum* dev, int* count)
{
    if (count) *count = dev ? dev->m_itemCount : 0;
    return dev ? dev->m_items : NULL;
}

/* ==================== 写入模板 ==================== */

bool XVfdCommandEnum_write(XVfdCommandEnum* dev, XVfdOperation op, bool reverse,
                           XVfdPointRole role)
{
    int16_t code = 0;
    if (!dev) return false;
    if (!XVfdCommandEnum_findCode(dev, (uint16_t)op, reverse, &code)) {
        XVfdDevice_setLastError(&dev->m_base, XVfdError_NotSupported);
        XVfdDevice_emitError(&dev->m_base, "该品牌命令码表未登记此动作");
        return false;
    }
    return XVfdDevice_actionWrite(&dev->m_base, op, role, (int32_t)code);
}

/* ==================== 虚函数表 / 生命周期 ==================== */

XVtable* XVfdCommandEnum_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdCommandEnum)
    XVTABLE_INHERIT_XCLASS(XVfdDevice);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdCommandEnum");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdCommandEnum);
    return XVTABLE_DEFAULT;
}

void XVfdCommandEnum_init(XVfdCommandEnum* dev)
{
    if (!dev) return;
    memset(dev, 0, sizeof(XVfdCommandEnum));
    XVfdDevice_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdCommandEnum_class_init();
}

#endif /* XVFD_COMMANDENUM_ON */
#endif /* XVFD_ON */
