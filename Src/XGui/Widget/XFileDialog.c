/******************************************************************************
 * @file       XFileDialog.c
 * @brief      文件对话框控件实现（对标 Qt 6.8 QFileDialog 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。静态便捷函数创建临时实例、
 *             应用存储 setter（caption→窗口标题、dir→directory、filter→
 *             nameFilters、options），但无 GUI 对话框环境不执行模态循环，
 *             统一返回默认值（空串/空列表）；信号由应用按 Qt 载荷语义
 *             手动触发（测试用 XObject_emitSignal）。枚举数值与 Qt 6.8.3
 *             qfiledialog.h 核对一致。
 * @note       本文件不依赖任何平台 API；原生文件浏览为后续扩展。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"

#if XWIDGET_ON && XDIALOG_ON

#include "XFileDialog.h"
#include "XWidget_Protected.h"

/* ==================== 内部辅助 ==================== */

/** @brief 释放并清空拥有型 XString 字段。 */
static void xfiledialog_freeString(XString** slot)
{
    if (slot && *slot) {
        XString_delete_base((XClass*)*slot);
        *slot = NULL;
    }
}

/** @brief 深拷贝 XString；NULL 视为空串。失败返回 NULL（按空处理）。 */
static XString* xfiledialog_dupString(const XString* src)
{
    if (!src) return XString_create();
    return XString_create_copy(src);
}

/** @brief 字符串信号参数释放回调：释放列表内拷贝的 XString。 */
static void xfiledialog_stringSignal_del(XVarList* list)
{
    XVarList_args_1(list, XString*, text);
    if (text)
        XString_delete_base((XClass*)text);
}

/** @brief 列表信号参数释放回调：释放列表内拷贝的 XStringList。 */
static void xfiledialog_listSignal_del(XVarList* list)
{
    XVarList_args_1(list, XStringList*, files);
    if (files)
        XStringList_delete_base((XClass*)files);
}

/** @brief 发射携带 XString* 深拷贝的信号；无接收者时释放参数列表。 */
static void xfiledialog_emitString(XFileDialog* self, size_t signal,
                                   const XString* text)
{
    XString* copy;
    XVarList* args;
    /* XSignal() 以 NULL 单参调用信号函数：self 为空时不得读取载荷。 */
    if (!self) return;
    copy = xfiledialog_dupString(text);
    if (!copy) return;
    args = XVarList_Create(XVar(XString*, copy));
    if (!args) {
        XString_delete_base((XClass*)copy);
        return;
    }
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args,
                           xfiledialog_stringSignal_del, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_setArgsDel(args, xfiledialog_stringSignal_del);
        XVarList_delete(args);
    }
}

/** @brief 发射携带 XStringList* 深拷贝的信号；无接收者时释放参数列表。 */
static void xfiledialog_emitList(XFileDialog* self, size_t signal,
                                 const XStringList* files)
{
    XStringList* copy;
    XVarList* args;
    /* XSignal() 以 NULL 单参调用信号函数：self 为空时不得读取载荷。 */
    if (!self) return;
    if (!files) copy = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    else copy = XStringList_create_copy(files);
    if (!copy) return;
    args = XVarList_Create(XVar(XStringList*, copy));
    if (!args) {
        XStringList_delete_base((XClass*)copy);
        return;
    }
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args,
                           xfiledialog_listSignal_del, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_setArgsDel(args, xfiledialog_listSignal_del);
        XVarList_delete(args);
    }
}

/* ==================== 类与实例生命周期 ==================== */

/** @brief 释放对话框自有拥有字段，再委托父类。 */
static void VXFileDialog_deinit(XFileDialog* self)
{
    int i;
    if (!self) return;
    xfiledialog_freeString(&self->m_directory);
    xfiledialog_freeString(&self->m_selectedNameFilter);
    xfiledialog_freeString(&self->m_defaultSuffix);
    for (i = 0; i < 5; ++i)
        xfiledialog_freeString(&self->m_labelTexts[i]);
    if (self->m_nameFilters) {
        XStringList_delete_base((XClass*)self->m_nameFilters);
        self->m_nameFilters = NULL;
    }
    if (self->m_selectedFiles) {
        XStringList_delete_base((XClass*)self->m_selectedFiles);
        self->m_selectedFiles = NULL;
    }
    xfiledialog_freeString(&self->m_directoryUrl);
    xfiledialog_freeString(&self->m_filter);
    xfiledialog_freeString(&self->m_selectedMimeTypeFilter);
    if (self->m_mimeTypeFilters) {
        XStringList_delete_base((XClass*)self->m_mimeTypeFilters);
        self->m_mimeTypeFilters = NULL;
    }
    if (self->m_history) {
        XStringList_delete_base((XClass*)self->m_history);
        self->m_history = NULL;
    }
    if (self->m_sidebarUrls) {
        XStringList_delete_base((XClass*)self->m_sidebarUrls);
        self->m_sidebarUrls = NULL;
    }
    if (self->m_supportedSchemes) {
        XStringList_delete_base((XClass*)self->m_supportedSchemes);
        self->m_supportedSchemes = NULL;
    }
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

XVtable* XFileDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFileDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFileDialog_deinit);
    return XVTABLE_DEFAULT;
}

void XFileDialog_init(XFileDialog* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XFileDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_fileMode = XFileDialog_ExistingFile;
    self->m_acceptMode = XFileDialog_AcceptOpen;
    self->m_viewMode = XFileDialogViewMode_Detail;
    self->m_options = 0;
    self->m_directory = NULL;
    self->m_nameFilters = NULL;
    self->m_selectedNameFilter = NULL;
    self->m_selectedFiles = NULL;
    self->m_defaultSuffix = NULL;
    {
        int i;
        for (i = 0; i < 5; ++i) self->m_labelTexts[i] = NULL;
    }
}

XFileDialog* XFileDialog_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XFileDialog* self = (XFileDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XFileDialog_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

void XFileDialog_setFileMode(XFileDialog* self, XFileDialogFileMode mode)
{ if (self) self->m_fileMode = mode; }

XFileDialogFileMode XFileDialog_fileMode(const XFileDialog* self)
{ return self ? self->m_fileMode : XFileDialog_ExistingFile; }

void XFileDialog_setAcceptMode(XFileDialog* self, XFileDialogAcceptMode mode)
{ if (self) self->m_acceptMode = mode; }

XFileDialogAcceptMode XFileDialog_acceptMode(const XFileDialog* self)
{ return self ? self->m_acceptMode : XFileDialog_AcceptOpen; }

void XFileDialog_setViewMode(XFileDialog* self, XFileDialogViewMode mode)
{ if (self) self->m_viewMode = mode; }

XFileDialogViewMode XFileDialog_viewMode(const XFileDialog* self)
{ return self ? self->m_viewMode : XFileDialogViewMode_Detail; }

void XFileDialog_setNameFilter(XFileDialog* self, const XString* filter)
{
    if (!self) return;
    xfiledialog_freeString(&self->m_selectedNameFilter);
    self->m_selectedNameFilter = xfiledialog_dupString(filter);
    /* 单一过滤器即一个元素的过滤器列表（Qt setNameFilter 语义）。 */
    if (self->m_nameFilters) {
        XStringList_clear_base((XContainer*)self->m_nameFilters);
    } else {
        self->m_nameFilters =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    }
    if (self->m_nameFilters && filter)
        XStringList_push_back_base(self->m_nameFilters,
                                   (void*)self->m_selectedNameFilter);
}

void XFileDialog_setNameFilters(XFileDialog* self, const XStringList* filters)
{
    int64_t i, n;
    if (!self) return;
    if (self->m_nameFilters) {
        XStringList_clear_base((XContainer*)self->m_nameFilters);
    } else {
        self->m_nameFilters =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    }
    if (!self->m_nameFilters || !filters) return;
    n = XStringList_size_base((const XContainer*)filters);
    for (i = 0; i < n; ++i) {
        XString* item = (XString*)XStringList_at_base(filters, i);
        XString* copy = item ? XString_create_copy(item) : XString_create();
        if (copy) {
            XStringList_push_back_move_base(self->m_nameFilters, copy);
            XString_delete_base((XClass*)copy);
            copy = NULL;
        }
    }
    /* 切换列表后未显式 selectNameFilter 时，保持原选中；列表为空则清空。 */
    if (n == 0)
        xfiledialog_freeString(&self->m_selectedNameFilter);
}

XStringList* XFileDialog_nameFilters(const XFileDialog* self)
{
    XStringList* out;
    int64_t i, n;
    if (!self) return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    /* 未设置过列表时返回空列表。 */
    if (!self->m_nameFilters) return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    out = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!out) return NULL;
    n = XStringList_size_base((const XContainer*)self->m_nameFilters);
    for (i = 0; i < n; ++i) {
        XString* item = (XString*)XStringList_at_base(self->m_nameFilters, i);
        if (!item) continue;
        {
            XString* copy = XString_create_copy(item);
            if (copy) {
                XStringList_push_back_move_base(out, copy);
                XString_delete_base((XClass*)copy);
                copy = NULL;
            }
        }
    }
    return out;
}

void XFileDialog_selectNameFilter(XFileDialog* self, const XString* filter)
{
    if (!self) return;
    xfiledialog_freeString(&self->m_selectedNameFilter);
    self->m_selectedNameFilter = xfiledialog_dupString(filter);
}

XString* XFileDialog_selectedNameFilter(const XFileDialog* self)
{
    return self ? xfiledialog_dupString(self->m_selectedNameFilter)
                : XString_create();
}

void XFileDialog_setDirectory(XFileDialog* self, const XString* directory)
{
    if (!self) return;
    xfiledialog_freeString(&self->m_directory);
    self->m_directory = xfiledialog_dupString(directory);
}

XString* XFileDialog_directory(const XFileDialog* self)
{
    return self ? xfiledialog_dupString(self->m_directory) : XString_create();
}

void XFileDialog_selectFile(XFileDialog* self, const XString* filename)
{
    XString* copy;
    if (!self || !filename) return;
    if (!self->m_selectedFiles)
        self->m_selectedFiles =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self->m_selectedFiles) return;
    copy = XString_create_copy(filename);
    if (copy) {
        XStringList_push_back_move_base(self->m_selectedFiles, copy);
        XString_delete_base((XClass*)copy);
        copy = NULL;
    }
}

XStringList* XFileDialog_selectedFiles(const XFileDialog* self)
{
    XStringList* out;
    int64_t i, n;
    if (!self) return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    out = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!out) return NULL;
    if (!self->m_selectedFiles) return out;
    n = XStringList_size_base((const XContainer*)self->m_selectedFiles);
    for (i = 0; i < n; ++i) {
        XString* item =
            (XString*)XStringList_at_base(self->m_selectedFiles, i);
        if (!item) continue;
        {
            XString* copy = XString_create_copy(item);
            if (copy) {
                XStringList_push_back_move_base(out, copy);
                XString_delete_base((XClass*)copy);
                copy = NULL;
            }
        }
    }
    return out;
}

XString* XFileDialog_selectedFile(const XFileDialog* self)
{
    XString* item;
    if (!self || !self->m_selectedFiles) return XString_create();
    item = (XString*)XStringList_at_base(self->m_selectedFiles, 0);
    return xfiledialog_dupString(item);
}

void XFileDialog_setDefaultSuffix(XFileDialog* self, const XString* suffix)
{
    if (!self) return;
    xfiledialog_freeString(&self->m_defaultSuffix);
    self->m_defaultSuffix = xfiledialog_dupString(suffix);
}

XString* XFileDialog_defaultSuffix(const XFileDialog* self)
{
    return self ? xfiledialog_dupString(self->m_defaultSuffix)
                : XString_create();
}

void XFileDialog_setOption(XFileDialog* self, XFileDialogOption option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= (XFileDialogOptions)option;
    else self->m_options &= (XFileDialogOptions)~option;
}

bool XFileDialog_testOption(const XFileDialog* self, XFileDialogOption option)
{
    return self ? (self->m_options & (XFileDialogOptions)option) != 0 : false;
}

void XFileDialog_setOptions(XFileDialog* self, XFileDialogOptions options)
{ if (self) self->m_options = options; }

XFileDialogOptions XFileDialog_options(const XFileDialog* self)
{ return self ? self->m_options : 0; }

void XFileDialog_setLabelText(XFileDialog* self, XFileDialogDialogLabel label,
                              const XString* text)
{
    if (!self || label < XFileDialogDialogLabel_LookIn ||
        label > XFileDialogDialogLabel_Reject)
        return;
    xfiledialog_freeString(&self->m_labelTexts[(int)label]);
    self->m_labelTexts[(int)label] = xfiledialog_dupString(text);
}

XString* XFileDialog_labelText(const XFileDialog* self,
                               XFileDialogDialogLabel label)
{
    if (!self || label < XFileDialogDialogLabel_LookIn ||
        label > XFileDialogDialogLabel_Reject)
        return XString_create();
    return xfiledialog_dupString(self->m_labelTexts[(int)label]);
}

/* ==================== 静态便捷函数 ==================== */

/** @brief 创建临时实例并按静态参数应用存储 setter（无模态执行）。 */
static XFileDialog* xfiledialog_tempSetup(XWidget* parent,
                                          const XString* caption,
                                          const XString* dir,
                                          const XString* filter,
                                          XFileDialogOptions options)
{
    XFileDialog* dlg = XFileDialog_create(parent, 0);
    if (!dlg) return NULL;
    if (caption)
        XWidget_setWindowTitle((XWidget*)dlg, caption);
    XFileDialog_setDirectory(dlg, dir);
    XFileDialog_setNameFilter(dlg, filter);
    XFileDialog_setOptions(dlg, options);
    return dlg;
}

XString* XFileDialog_getOpenFileName(XWidget* parent, const XString* caption,
                                     const XString* dir, const XString* filter,
                                     int* selectedFilterIndex)
{
    XFileDialog* dlg;
    XString* result;
    if (selectedFilterIndex) *selectedFilterIndex = 0;
    dlg = xfiledialog_tempSetup(parent, caption, dir, filter, 0);
    if (!dlg) return XString_create();
    result = XString_create();
    XFileDialog_delete_base(dlg);
    return result;
}

XString* XFileDialog_getOpenFileName_2(XWidget* parent, const char* caption,
                                       const char* dir, const char* filter,
                                       int* selectedFilterIndex)
{
    XString* c = caption ? XString_create_utf8(caption) : NULL;
    XString* d = dir ? XString_create_utf8(dir) : NULL;
    XString* f = filter ? XString_create_utf8(filter) : NULL;
    XString* result = XFileDialog_getOpenFileName(parent, c, d, f,
                                                  selectedFilterIndex);
    xfiledialog_freeString(&c);
    xfiledialog_freeString(&d);
    xfiledialog_freeString(&f);
    return result;
}

XStringList* XFileDialog_getOpenFileNames(XWidget* parent,
                                          const XString* caption,
                                          const XString* dir,
                                          const XString* filter,
                                          int* selectedFilterIndex)
{
    XFileDialog* dlg;
    XStringList* result;
    if (selectedFilterIndex) *selectedFilterIndex = 0;
    dlg = xfiledialog_tempSetup(parent, caption, dir, filter, 0);
    if (!dlg) return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    result = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XFileDialog_delete_base(dlg);
    return result;
}

XStringList* XFileDialog_getOpenFileNames_2(XWidget* parent,
                                            const char* caption,
                                            const char* dir,
                                            const char* filter,
                                            int* selectedFilterIndex)
{
    XString* c = caption ? XString_create_utf8(caption) : NULL;
    XString* d = dir ? XString_create_utf8(dir) : NULL;
    XString* f = filter ? XString_create_utf8(filter) : NULL;
    XStringList* result = XFileDialog_getOpenFileNames(parent, c, d, f,
                                                       selectedFilterIndex);
    xfiledialog_freeString(&c);
    xfiledialog_freeString(&d);
    xfiledialog_freeString(&f);
    return result;
}

XString* XFileDialog_getSaveFileName(XWidget* parent, const XString* caption,
                                     const XString* dir, const XString* filter,
                                     int* selectedFilterIndex)
{
    XFileDialog* dlg;
    XString* result;
    if (selectedFilterIndex) *selectedFilterIndex = 0;
    dlg = xfiledialog_tempSetup(parent, caption, dir, filter, 0);
    if (!dlg) return XString_create();
    if (dlg) XFileDialog_setAcceptMode(dlg, XFileDialog_AcceptSave);
    result = XString_create();
    XFileDialog_delete_base(dlg);
    return result;
}

XString* XFileDialog_getSaveFileName_2(XWidget* parent, const char* caption,
                                       const char* dir, const char* filter,
                                       int* selectedFilterIndex)
{
    XString* c = caption ? XString_create_utf8(caption) : NULL;
    XString* d = dir ? XString_create_utf8(dir) : NULL;
    XString* f = filter ? XString_create_utf8(filter) : NULL;
    XString* result = XFileDialog_getSaveFileName(parent, c, d, f,
                                                  selectedFilterIndex);
    xfiledialog_freeString(&c);
    xfiledialog_freeString(&d);
    xfiledialog_freeString(&f);
    return result;
}

XString* XFileDialog_getExistingDirectory(XWidget* parent,
                                          const XString* caption,
                                          const XString* dir)
{
    XFileDialog* dlg;
    XString* result;
    dlg = xfiledialog_tempSetup(parent, caption, dir, NULL,
                                (XFileDialogOptions)XFileDialog_ShowDirsOnly);
    if (!dlg) return XString_create();
    result = XString_create();
    XFileDialog_delete_base(dlg);
    return result;
}

XString* XFileDialog_getExistingDirectory_2(XWidget* parent,
                                            const char* caption,
                                            const char* dir)
{
    XString* c = caption ? XString_create_utf8(caption) : NULL;
    XString* d = dir ? XString_create_utf8(dir) : NULL;
    XString* result = XFileDialog_getExistingDirectory(parent, c, d);
    xfiledialog_freeString(&c);
    xfiledialog_freeString(&d);
    return result;
}

/* ==================== 信号 ==================== */

void* XFileDialog_fileSelected_signal(XFileDialog* self, const XString* file)
{
    xfiledialog_emitString(self, (size_t)XFileDialog_fileSelected_signal,
                           file);
    return (void*)(size_t)XFileDialog_fileSelected_signal;
}

void* XFileDialog_filesSelected_signal(XFileDialog* self,
                                       const XStringList* files)
{
    xfiledialog_emitList(self, (size_t)XFileDialog_filesSelected_signal,
                         files);
    return (void*)(size_t)XFileDialog_filesSelected_signal;
}

void* XFileDialog_currentChanged_signal(XFileDialog* self, const XString* path)
{
    xfiledialog_emitString(self, (size_t)XFileDialog_currentChanged_signal,
                           path);
    return (void*)(size_t)XFileDialog_currentChanged_signal;
}

void* XFileDialog_directoryEntered_signal(XFileDialog* self,
                                          const XString* directory)
{
    xfiledialog_emitString(self, (size_t)XFileDialog_directoryEntered_signal,
                           directory);
    return (void*)(size_t)XFileDialog_directoryEntered_signal;
}

void* XFileDialog_filterSelected_signal(XFileDialog* self,
                                        const XString* filter)
{
    xfiledialog_emitString(self, (size_t)XFileDialog_filterSelected_signal,
                           filter);
    return (void*)(size_t)XFileDialog_filterSelected_signal;
}


/* ==================== Task 2.21 回检补齐：URL/过滤/历史/状态 API ========== */

XStringList* XFileDialog_selectedUrls(const XFileDialog* self)
{
    return XFileDialog_selectedFiles(self);
}

void XFileDialog_setDirectoryUrl(XFileDialog* self, const XString* directory)
{
    if (!self) return;
    xfiledialog_freeString(&self->m_directoryUrl); self->m_directoryUrl = xfiledialog_dupString(directory);
}

XString* XFileDialog_directoryUrl(const XFileDialog* self)
{
    return self ? xfiledialog_dupString(self->m_directoryUrl) : XString_create();
}

void XFileDialog_selectUrl(XFileDialog* self, const XString* url)
{
    if (!self || !url) return;
    if (!self->m_selectedFiles)
        self->m_selectedFiles =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_selectedFiles)
        XStringList_push_back_base(self->m_selectedFiles, (XString*)url);
}

void XFileDialog_setFilter(XFileDialog* self, const XString* filter)
{
    if (!self) return;
    xfiledialog_freeString(&self->m_filter); self->m_filter = xfiledialog_dupString(filter);
}

XString* XFileDialog_filter(const XFileDialog* self)
{
    return self ? xfiledialog_dupString(self->m_filter) : XString_create();
}

void XFileDialog_setMimeTypeFilters(XFileDialog* self,
                                    const XStringList* filters)
{
    if (!self) return;
    if (self->m_mimeTypeFilters)
        XStringList_delete_base((XClass*)self->m_mimeTypeFilters);
    self->m_mimeTypeFilters = filters
        ? XStringList_create_copy(filters) : NULL;
}

XStringList* XFileDialog_mimeTypeFilters(const XFileDialog* self)
{
    if (!self || !self->m_mimeTypeFilters)
        return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    return XStringList_create_copy(self->m_mimeTypeFilters);
}

void XFileDialog_selectMimeTypeFilter(XFileDialog* self,
                                      const XString* filter)
{
    if (!self) return;
    xfiledialog_freeString(&self->m_selectedMimeTypeFilter); self->m_selectedMimeTypeFilter = xfiledialog_dupString(filter);
}

XString* XFileDialog_selectedMimeTypeFilter(const XFileDialog* self)
{
    return self
        ? xfiledialog_dupString(self->m_selectedMimeTypeFilter)
        : XString_create();
}

void XFileDialog_setHistory(XFileDialog* self, const XStringList* history)
{
    if (!self) return;
    if (self->m_history)
        XStringList_delete_base((XClass*)self->m_history);
    self->m_history = history
        ? XStringList_create_copy(history) : NULL;
}

XStringList* XFileDialog_history(const XFileDialog* self)
{
    if (!self || !self->m_history)
        return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    return XStringList_create_copy(self->m_history);
}

void XFileDialog_setSidebarUrls(XFileDialog* self, const XStringList* urls)
{
    if (!self) return;
    if (self->m_sidebarUrls)
        XStringList_delete_base((XClass*)self->m_sidebarUrls);
    self->m_sidebarUrls = urls
        ? XStringList_create_copy(urls) : NULL;
}

XStringList* XFileDialog_sidebarUrls(const XFileDialog* self)
{
    if (!self || !self->m_sidebarUrls)
        return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    return XStringList_create_copy(self->m_sidebarUrls);
}

void XFileDialog_setSupportedSchemes(XFileDialog* self,
                                     const XStringList* schemes)
{
    if (!self) return;
    if (self->m_supportedSchemes)
        XStringList_delete_base((XClass*)self->m_supportedSchemes);
    self->m_supportedSchemes = schemes
        ? XStringList_create_copy(schemes) : NULL;
}

XStringList* XFileDialog_supportedSchemes(const XFileDialog* self)
{
    if (!self || !self->m_supportedSchemes)
        return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    return XStringList_create_copy(self->m_supportedSchemes);
}

void XFileDialog_saveState(const XFileDialog* self, XByteArray* out)
{
    (void)self;
    if (out) XByteArray_init(out, true);
}

bool XFileDialog_restoreState(XFileDialog* self, const XByteArray* state)
{
    (void)self; (void)state;
    return false;
}

void XFileDialog_setIconProvider(XFileDialog* self, void* provider)
{
    if (self) self->m_iconProvider = provider;
}

void* XFileDialog_iconProvider(const XFileDialog* self)
{
    return self ? self->m_iconProvider : NULL;
}

void XFileDialog_setItemDelegate(XFileDialog* self, void* delegate)
{
    (void)self; (void)delegate;
}

void* XFileDialog_itemDelegate(const XFileDialog* self)
{
    (void)self;
    return NULL;
}

void XFileDialog_setProxyModel(XFileDialog* self, void* model)
{
    (void)self; (void)model;
}

void* XFileDialog_proxyModel(const XFileDialog* self)
{
    (void)self;
    return NULL;
}

#endif /* XWIDGET_ON && XDIALOG_ON */
