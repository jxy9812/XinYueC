/**
 * @file       XFontComboBox.c
 * @brief      字体选择下拉框控件实现（对标 Qt 6.8 QFontComboBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XFontComboBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGuiConfig.h"
#if XPLATFORMFONTDATABASE_ON
#include "XPlatformFontDatabase.h"
#endif
#include <string.h>

#if XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON

static void xfcb_populate(XFontComboBox* self)
{
#if XPLATFORMFONTDATABASE_ON
    XPlatformFontDatabase* db;
    XVector* families = NULL;
    int64_t i;
    int64_t n;
    if (!self) return;
    db = XPlatformFontDatabase_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!db) return;
    if (XPlatformFontDatabase_isValid(db))
        families = XPlatformFontDatabase_families(db);
    if (families) {
        n = XVector_size_base((const XContainer*)families);
        for (i = 0; i < n; ++i) {
            char** name = (char**)XVector_at_base(families, i);
            if (name && *name)
                XComboBox_addItem(self, *name);
        }
    }
    XPlatformFontDatabase_destroy(db);
#endif /* XPLATFORMFONTDATABASE_ON */
    /* 回退：数据库未启用或无字体族时补默认条目确保控件非空。 */
    if (XComboBox_count(self) > 0)
        XComboBox_setCurrentIndex(self, 0);
    if (XComboBox_count(self) == 0) {
        XComboBox_addItem(self, "XFontOutlineCommon");
        XComboBox_addItem(self, "Sans Serif");
        XComboBox_addItem(self, "Serif");
        XComboBox_addItem(self, "Monospace");
        XComboBox_setCurrentIndex(self, 0);
    }
}

XVtable* XFontComboBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFontComboBox)
    XVTABLE_INHERIT_XCLASS(XComboBox);
    return XVTABLE_DEFAULT;
}

void XFontComboBox_init(XFontComboBox* self, XWidget* parent,
                        XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XComboBox_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XFontComboBox);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_filters = (int)XFontComboBoxFilter_AllFonts;
    xfcb_populate(self);
}

XFontComboBox* XFontComboBox_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XFontComboBox* self =
        (XFontComboBox*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XFontComboBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XFontComboBox_setFontFilters(XFontComboBox* self, int filters)
{
    if (!self || self->m_filters == filters) return;
    self->m_filters = filters;
    /* 其余过滤项依赖字体元数据（可缩放/等宽），待字体数据库能力
     * 补齐后重算条目；当前仅存储。 */
}

int XFontComboBox_fontFilters(const XFontComboBox* self)
{
    return self ? self->m_filters : 0;
}

const char* XFontComboBox_currentFamily(const XFontComboBox* self)
{
    if (!self) return "";
    return XComboBox_currentText(self);
}

void XFontComboBox_setCurrentFamily(XFontComboBox* self, const char* family)
{
    int i;
    int n;
    if (!self || !family) return;
    n = XComboBox_count(self);
    for (i = 0; i < n; ++i) {
        if (strcmp(XComboBox_itemText(self, i), family) == 0) {
            XComboBox_setCurrentIndex(self, i);
            return;
        }
    }
}


void* XFontComboBox_currentFontChanged_signal(XFontComboBox* self)
{
    (void)self;
    return (void*)(size_t)XFontComboBox_currentFontChanged_signal;
}

void XFontComboBox_setCurrentFont(XFontComboBox* self, const char* family)
{ XFontComboBox_setCurrentFamily(self, family); }
void XFontComboBox_setWritingSystem(XFontComboBox* self, int system) { (void)self; (void)system; }
#endif /* XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */