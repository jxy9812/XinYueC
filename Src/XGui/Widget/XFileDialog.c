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
/* 真实弹窗依赖（对标 Qt QFileDialog 静态便捷函数的对话框组装路径）： */
#include <stdio.h>             /* snprintf：路径拼接 */
#include <string.h>            /* strrchr：父目录推导 */
#include "XCoreApplication.h"  /* qApp 等价物：有应用实例才允许模态循环 */
#include "XGuiApplication.h"   /* 主屏查询（弹窗居中） */
#include "XScreen.h"           /* 屏幕几何 */
#include "XObject.h"           /* 动态属性：delegate/proxy 借用登记 */
#include "XVariant.h"          /* Ptr 变体承载不透明指针 */
#include "XLabel.h"            /* 目录/文件名/类型标签 */
#include "XLineEdit.h"         /* 文件名编辑 */
#include "XComboBox.h"         /* 目录路径与过滤器下拉 */
#include "XListView.h"         /* 文件列表 */
#include "XAbstractItemModel.h"/* 文件列表数据模型 */
#include "XPushButton.h"       /* 确定/取消 */
#include "XBoxLayout.h"        /* 对话框布局 */
#include "XDir.h"              /* 目录列举（XFILE_ON && XDIR_ON 时生效） */

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

/* ==================== 静态便捷函数（真实弹窗） ====================
 * 对标 Qt QFileDialog::getOpenFileName/getSaveFileName/getExistingDirectory
 * 静态便捷函数：构造 XDialog + 目录下拉 + XListView 文件列表（双击进
 * 目录、名称过滤）+ 文件名编辑 + 确定/取消，经 XDialog_exec 阻塞式
 * 模态循环（应用模态、Escape→reject）；无 GUI 环境（无 XCoreApplication
 * 实例，如无头测试）保持桩约定：返回默认值、*selectedFilterIndex=0。
 * 目录列举依赖 XFILE_ON && XDIR_ON（XDir 模块）。 */

/** @brief GUI 环境探测：存在 XCoreApplication 实例才执行真实模态循环。 */
static bool xff_guiReady(void)
{
    return XCoreApplication_instance() != NULL;
}

/** @brief 以 UTF-8 设置对象 objectName（对标 QObject::setObjectName）。 */
static void xff_setName(XObject* obj, const char* name)
{
    XString tmp;
    if (!obj) return;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    XObject_setObjectName(obj, &tmp);
    XClass_deinit_base((XClass*)&tmp);
}

/* 子控件 objectName 常量（对标 Qt 对话框私有子对象命名；槽内经
 * findChild 取回）。 */
#define XFF_NAME_DIRCOMBO "qt_file_dialog_dir_combo"
#define XFF_NAME_VIEW     "qt_file_dialog_view"
#define XFF_NAME_NAMEEDIT "qt_file_dialog_name_edit"
#define XFF_NAME_FILTERCOMBO "qt_file_dialog_filter_combo"
#define XFF_NAME_OK       "qt_file_dialog_ok"
#define XFF_NAME_CANCEL   "qt_file_dialog_cancel"

/** @brief 按 objectName 查找对话框直接子控件（对标 QObject::findChild）。 */
static XWidget* xff_childByName(XDialog* dlg, const char* name)
{
    XString tmp;
    XWidget* w;
    if (!dlg) return NULL;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    w = (XWidget*)XObject_findChild((XObject*)dlg, &tmp,
                                    XFindDirectChildrenOnly);
    XClass_deinit_base((XClass*)&tmp);
    return w;
}

/** @brief 弹窗主屏居中（对标 Qt 静态便捷函数把对话框定位于屏幕中央）。 */
static void xff_centerOnScreen(XWidget* w)
{
    XScreen* screen;
    XRect g;
    if (!w) return;
    screen = XGuiApplication_primaryScreen();
    if (!screen) return;
    g = XScreen_geometry(screen);
    if (g.width <= 0 || g.height <= 0) return;
    XWidget_move(w, g.x + (g.width - XWidget_width(w)) / 2,
                    g.y + (g.height - XWidget_height(w)) / 2);
}

#if XFILE_ON && XDIR_ON

/* ---------- 目录浏览基础设施 ---------- */

/** @brief 解析 Qt 风格过滤器串中的通配模式（对标 nameFilters 的括号
 *  截取）："Images (*.png *.jpg)" → ["*.png","*.jpg"]。
 * @note  XDir_nameFiltersFromString 不剥说明文字，这里自行解析；无括号
 *        时整串按模式表处理（空表=不过滤，对标 Qt 空过滤器语义）。 */
static XStringList* xff_filterPatterns(const XString* filter)
{
    XStringList* out = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    const char* s;
    const char* lparen;
    const char* rparen;
    char* dup;
    char* token;
    if (!out || !filter) return out;
    s = XString_toUtf8(filter);
    if (!s) return out;
    lparen = strchr(s, '(');
    rparen = lparen ? strchr(lparen, ')') : NULL;
    if (lparen && rparen && rparen > lparen) {
        dup = XMemory_strdup(lparen + 1);
        if (dup && rparen > lparen + 1)
            dup[rparen - lparen - 1] = '\0';
    } else {
        dup = XMemory_strdup(s);
    }
    if (!dup) return out;
    token = strtok(dup, " ;,");
    while (token) {
        if (strchr(token, '*') || strchr(token, '?'))
            XStringList_push_back_utf8(out, token);
        token = strtok(NULL, " ;,");
    }
    XFree_System(dup);
    return out;
}

/** @brief 列出目录内容（名称过滤只作用于文件，目录恒列出，对标
 *  QFileDialog 过滤语义）。行序约定：目录表在前、文件表在后。 */
static void xff_listDir(const XString* dirStr, const XStringList* patterns,
                        bool dirsOnly, XStringList** outDirs,
                        XStringList** outFiles)
{
    XDir* dir;
    *outDirs = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    *outFiles = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!*outDirs || !*outFiles || !dirStr) return;
    dir = XDir_create_2(dirStr);
    if (!dir) return;
    if (!XDir_exists_1(dir)) {
        XDir_delete_base((XClass*)dir);
        return;
    }
    {
        XStringList* d = XDir_entryList_2(
            dir, NULL, (XDirFilters)(XDir_Dirs | XDir_AllDirs |
                                     XDir_NoDotAndDotDot),
            (XDirSortFlags)(XDir_Name | XDir_DirsFirst));
        XStringList* f = NULL;
        int64_t i, n;
        if (dirsOnly || !patterns ||
            XStringList_size_base((const XContainer*)patterns) == 0) {
            f = XDir_entryList_2(
                dir, NULL, (XDirFilters)(XDir_Files | XDir_NoDotAndDotDot),
                (XDirSortFlags)XDir_Name);
        } else {
            f = XDir_entryList_2(
                dir, patterns,
                (XDirFilters)(XDir_Files | XDir_NoDotAndDotDot),
                (XDirSortFlags)XDir_Name);
        }
        if (d) {
            n = XStringList_size_base((const XContainer*)d);
            for (i = 0; i < n; ++i) {
                XString* item =
                    (XString*)(void*)XStringList_at_base(
                        (const XVector*)d, i);
                if (item)
                    XStringList_push_back_utf8(*outDirs,
                                               XString_toUtf8(item));
            }
            XStringList_delete_base((XClass*)d);
        }
        if (f) {
            n = XStringList_size_base((const XContainer*)f);
            for (i = 0; i < n; ++i) {
                XString* item =
                    (XString*)(void*)XStringList_at_base(
                        (const XVector*)f, i);
                if (item)
                    XStringList_push_back_utf8(*outFiles,
                                               XString_toUtf8(item));
            }
            XStringList_delete_base((XClass*)f);
        }
    }
    XDir_delete_base((XClass*)dir);
}

/** @brief 拼接目录与名称为路径（'/' 分隔；对标 QDir::filePath 简化）。 */
static XString* xff_joinPath(const XString* dir, const char* name)
{
    char buf[1024];
    const char* d = (dir && XString_toUtf8(dir)) ? XString_toUtf8(dir) : ".";
    snprintf(buf, sizeof(buf), "%s/%s", d, name ? name : "");
    return XString_create_utf8(buf);
}

/** @brief 推导父目录（对标 QFileDialog 双击 ".." 回上级）。 */
static XString* xff_parentOf(const XString* dir)
{
    const char* s;
    const char* slash;
    if (!dir || !(s = XString_toUtf8(dir)) || !s[0])
        return XString_create_utf8("/");
    slash = strrchr(s, '/');
    if (!slash) return XString_create_utf8(".");
    if (slash == s) return XString_create_utf8("/");
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%.*s", (int)(slash - s), s);
        return XString_create_utf8(buf);
    }
}

/* ---------- 槽与视图刷新 ---------- */

static void xff_acceptSlot(XObject* receiver, XVarList* args);
static void xff_rejectSlot(XObject* receiver, XVarList* args);

/** @brief 按当前目录/过滤器重填文件列表模型（行 0 固定为 ".."，随后
 *  目录行、文件行；双击命中按同一确定性次序解析）。 */
static void xff_refresh(XFileDialog* dlg)
{
    XListView* view;
    XAbstractItemModel* model;
    XStringList* patterns;
    XStringList* dirs = NULL;
    XStringList* files = NULL;
    bool dirsOnly;
    int64_t i, nd, nf;
    if (!dlg) return;
    view = (XListView*)xff_childByName(&dlg->m_base, XFF_NAME_VIEW);
    if (!view) return;
    model = XAbstractItemView_model((XAbstractItemView*)view);
    if (!model) return;
    patterns = dlg->m_selectedNameFilter
        ? xff_filterPatterns(dlg->m_selectedNameFilter) : NULL;
    dirsOnly = (dlg->m_options & (XFileDialogOptions)XFileDialog_ShowDirsOnly)
               ? true : false;
    xff_listDir(dlg->m_directory, patterns, dirsOnly, &dirs, &files);
    XStringList_delete_base((XClass*)patterns);
    if (!dirs || !files) {
        if (dirs) XStringList_delete_base((XClass*)dirs);
        if (files) XStringList_delete_base((XClass*)files);
        return;
    }
    nd = XStringList_size_base((const XContainer*)dirs);
    nf = XStringList_size_base((const XContainer*)files);
    XAbstractItemModel_setDimension(model, (int)(1 + nd + nf), 1);
    XAbstractItemModel_setData_2(model, 0, 0, "..");
    for (i = 0; i < nd; ++i) {
        XString* item =
            (XString*)(void*)XStringList_at_base((const XVector*)dirs, i);
        if (item)
            XAbstractItemModel_setData_2(model, (int)(1 + i), 0,
                                         XString_toUtf8(item));
    }
    for (i = 0; i < nf; ++i) {
        XString* item =
            (XString*)(void*)XStringList_at_base((const XVector*)files, i);
        if (item)
            XAbstractItemModel_setData_2(model, (int)(1 + nd + i), 0,
                                         XString_toUtf8(item));
    }
    XStringList_delete_base((XClass*)dirs);
    XStringList_delete_base((XClass*)files);
}

/** @brief 切换当前目录：更新目录下拉历史、发射 directoryEntered、刷新
 *  列表（对标 QFileDialog 进入目录路径）。 */
static void xff_cd(XFileDialog* dlg, const XString* path)
{
    XComboBox* combo;
    if (!dlg || !path) return;
    XFileDialog_setDirectory(dlg, path);
    combo = (XComboBox*)xff_childByName(&dlg->m_base, XFF_NAME_DIRCOMBO);
    if (combo) {
        int idx = XComboBox_findText_2(combo, XString_toUtf8(path));
        if (idx < 0) {
            XComboBox_insertItem_2(combo, XComboBox_count(combo),
                                   XString_toUtf8(path));
            idx = XComboBox_count(combo) - 1;
        }
        XComboBox_setCurrentIndex(combo, idx);
    }
    XFileDialog_directoryEntered_signal(dlg, path);
    xff_refresh(dlg);
}

/** @brief 文件列表双击槽：行 0 回上级；目录行进入；文件行置入文件名
 *  编辑并确认（对标 QFileDialog 双击语义）。 */
static void xff_viewDoubleClicked(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XListView* view;
    XAbstractItemModel* model;
    XStringList* patterns;
    XStringList* dirs = NULL;
    XStringList* files = NULL;
    bool dirsOnly;
    int64_t nd;
    if (!dlg || !args) return;
    XVarList_args_2(args, int, row, int, col);
    (void)col;
    if (row < 0) return;
    view = (XListView*)xff_childByName(&dlg->m_base, XFF_NAME_VIEW);
    if (!view) return;
    patterns = dlg->m_selectedNameFilter
        ? xff_filterPatterns(dlg->m_selectedNameFilter) : NULL;
    dirsOnly = (dlg->m_options & (XFileDialogOptions)XFileDialog_ShowDirsOnly)
               ? true : false;
    xff_listDir(dlg->m_directory, patterns, dirsOnly, &dirs, &files);
    XStringList_delete_base((XClass*)patterns);
    if (!dirs || !files) {
        if (dirs) XStringList_delete_base((XClass*)dirs);
        if (files) XStringList_delete_base((XClass*)files);
        return;
    }
    nd = XStringList_size_base((const XContainer*)dirs);
    if (row == 0) {
        XString* up = xff_parentOf(dlg->m_directory);
        if (up) {
            xff_cd(dlg, up);
            XString_delete_base((XClass*)up);
        }
    } else if ((int64_t)row <= nd) {
        XString* item = (XString*)(void*)XStringList_at_base(
            (const XVector*)dirs, row - 1);
        if (item) {
            XString* sub = xff_joinPath(dlg->m_directory,
                                        XString_toUtf8(item));
            if (sub) {
                xff_cd(dlg, sub);
                XString_delete_base((XClass*)sub);
            }
        }
    } else {
        int64_t fi = (int64_t)row - 1 - nd;
        XString* item = (XString*)(void*)XStringList_at_base(
            (const XVector*)files, fi);
        if (item) {
            XLineEdit* nameEdit = (XLineEdit*)xff_childByName(
                &dlg->m_base, XFF_NAME_NAMEEDIT);
            if (nameEdit)
                XLineEdit_setText(nameEdit, XString_toUtf8(item));
            /* 文件双击即确认（对标 QFileDialog 双击文件 accept）。 */
            xff_acceptSlot(receiver, NULL);
        }
    }
    XStringList_delete_base((XClass*)dirs);
    XStringList_delete_base((XClass*)files);
}

/** @brief 文件列表单击槽：命中文件行时把名称置入文件名编辑（对标
 *  QFileDialog 单击选中回填文件名）。 */
static void xff_viewClicked(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XListView* view;
    XAbstractItemModel* model;
    const XString* cell;
    if (!dlg || !args) return;
    XVarList_args_2(args, int, row, int, col);
    (void)col;
    if (row <= 0) return; /* 行 0 为 ".."，非文件。 */
    view = (XListView*)xff_childByName(&dlg->m_base, XFF_NAME_VIEW);
    if (!view) return;
    model = XAbstractItemView_model((XAbstractItemView*)view);
    if (!model) return;
    cell = XAbstractItemModel_data(model, row, 0);
    if (cell) {
        XLineEdit* nameEdit = (XLineEdit*)xff_childByName(
            &dlg->m_base, XFF_NAME_NAMEEDIT);
        if (nameEdit)
            XLineEdit_setText(nameEdit, XString_toUtf8(cell));
    }
}

/** @brief 目录下拉激活槽：选中历史目录即进入（对标 QFileDialog 下拉
 *  目录历史；activated 仅用户选择触发，程序填充不回环）。 */
static void xff_dirActivated(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XComboBox* combo;
    if (!dlg || !args) return;
    XVarList_args_1(args, int, index);
    combo = (XComboBox*)xff_childByName(&dlg->m_base, XFF_NAME_DIRCOMBO);
    if (!combo || index < 0) return;
    {
        const char* path = XComboBox_itemText_2(combo, index);
        if (path && path[0]) {
            XString* p = XString_create_utf8(path);
            if (p) {
                xff_cd(dlg, p);
                XString_delete_base((XClass*)p);
            }
        }
    }
}

/** @brief 过滤器下拉激活槽：切换选中过滤器并重列文件（对标
 *  QFileDialog::filterSelected + 视图刷新）。 */
static void xff_filterActivated(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XComboBox* combo;
    if (!dlg || !args) return;
    XVarList_args_1(args, int, index);
    combo = (XComboBox*)xff_childByName(&dlg->m_base, XFF_NAME_FILTERCOMBO);
    if (!combo || index < 0) return;
    {
        const char* text = XComboBox_itemText_2(combo, index);
        if (text) {
            XString* f = XString_create_utf8(text);
            if (f) {
                XFileDialog_selectNameFilter(dlg, f);
                XFileDialog_filterSelected_signal(dlg, f);
                XString_delete_base((XClass*)f);
            }
        }
    }
    xff_refresh(dlg);
}

/** @brief 确定槽：结算文件名（默认后缀补全）→ 选中列表 → 发射
 *  fileSelected → accept（对标 QFileDialog::accept 结算路径）。 */
static void xff_acceptSlot(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XLineEdit* nameEdit;
    const char* name;
    (void)args;
    if (!dlg) return;
    /* 目录模式：确认即取当前目录（对标 getExistingDirectory 语义）。 */
    if (dlg->m_fileMode == XFileDialog_Directory) {
        if (dlg->m_selectedFiles)
            XStringList_clear_base((XContainer*)dlg->m_selectedFiles);
        XDialog_accept(&dlg->m_base);
        return;
    }
    nameEdit = (XLineEdit*)xff_childByName(&dlg->m_base, XFF_NAME_NAMEEDIT);
    name = nameEdit ? XLineEdit_text(nameEdit) : NULL;
    if (!name || !name[0]) return; /* 无文件名：忽略确认（对标 OK 无效态）。 */
    {
        XString* path;
        /* 保存模式默认后缀：名称无 '.' 时补 defaultSuffix（对标
         * QFileDialog defaultSuffix 语义）。 */
        if (dlg->m_acceptMode == XFileDialog_AcceptSave &&
            dlg->m_defaultSuffix &&
            !strchr(name, '.')) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s.%s", name,
                     XString_toUtf8(dlg->m_defaultSuffix));
            path = xff_joinPath(dlg->m_directory, buf);
        } else {
            path = xff_joinPath(dlg->m_directory, name);
        }
        if (path) {
            if (dlg->m_selectedFiles)
                XStringList_clear_base((XContainer*)dlg->m_selectedFiles);
            XFileDialog_selectFile(dlg, path);
            XFileDialog_fileSelected_signal(dlg, path);
            XString_delete_base((XClass*)path);
        }
    }
    XDialog_accept(&dlg->m_base);
}

static void xff_rejectSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) XDialog_reject((XDialog*)receiver);
}

/* ---------- 对话框组装与执行 ---------- */

/** @brief 布局句柄集：顶层布局 + 子行布局均由调用方删除（addLayout
 *  子布局不归父布局所有，对标 QLayout 所有权语义）。 */
typedef struct XFFLayouts
{
    XBoxLayout* root;     /**< 顶层垂直布局。 */
    XBoxLayout* dirRow;   /**< 目录行（标签 + 下拉）。 */
    XBoxLayout* filterRow;/**< 过滤器行（标签 + 下拉）。 */
    XBoxLayout* nameRow;  /**< 文件名行（标签 + 编辑）。 */
    XBoxLayout* bar;      /**< 按钮行。 */
    XAbstractItemModel* model; /**< 文件列表模型（视图不拥有，须自删）。 */
} XFFLayouts;

/** @brief 组装文件对话框并填充起始目录列表（对标 Qt 非原生
 *  QFileDialog 的 ui 组装；子控件经 objectName 标识）。
 * @param prefillName 保存模式下预填文件名（可为 NULL）。
 * @return 对话框指针（调用方以 xff_teardown 回收）；失败 NULL。 */
static XFileDialog* xff_buildDialog(XWidget* parent, const XString* caption,
                                    const XString* dir, const XString* filter,
                                    XFileDialogOptions options,
                                    XFileDialogFileMode fileMode,
                                    XFileDialogAcceptMode acceptMode,
                                    const char* prefillName,
                                    XFFLayouts* ls)
{
    XFileDialog* dlg;
    XListView* view;
    XAbstractItemModel* model;
    if (!ls) return NULL;
    ls->root = ls->dirRow = ls->filterRow = ls->nameRow = ls->bar = NULL;
    /* 对标 Qt：静态便捷函数创建顶层对话框（Dialog 窗口标志）。 */
    dlg = XFileDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, parent,
                                (XWidgetFlags)XWindowType_Dialog);
    if (!dlg) return NULL;
    if (caption)
        XWidget_setWindowTitle((XWidget*)dlg, caption);
    XFileDialog_setFileMode(dlg, fileMode);
    XFileDialog_setAcceptMode(dlg, acceptMode);
    XFileDialog_setOptions(dlg, options);
    XFileDialog_setNameFilter(dlg, filter);
    if (dir)
        XFileDialog_setDirectory(dlg, dir);
    else {
        /* 起始目录缺省为当前目录（对标 QFileDialog 空目录语义）。 */
        XString* cwd = XDir_currentPath();
        if (cwd) {
            XFileDialog_setDirectory(dlg, cwd);
            XString_delete_base((XClass*)cwd);
        }
    }
    ls->model = NULL;
    ls->root = XBoxLayout_create(XBoxLayoutDirection_TopToBottom,
                                 (XWidget*)dlg);
    if (!ls->root) {
        XFileDialog_delete_base((XClass*)dlg);
        return NULL;
    }
    XLayout_setContentsMargins((XLayout*)ls->root, 12, 12, 12, 12);
    XLayout_setSpacing((XLayout*)ls->root, 8);
    /* 行 1：目录下拉（历史目录导航入口，对标 LookIn 组合）。 */
    {
        XLabel* lb = XLabel_create((XWidget*)dlg, 0);
        XComboBox* combo;
        XLabel_setText_2(lb, "目录:");
        combo = XComboBox_create((XWidget*)dlg, 0);
        if (combo) {
            xff_setName((XObject*)combo, XFF_NAME_DIRCOMBO);
            XWidget_setMinimumSize((XWidget*)combo, 300, 26);
        }
        ls->dirRow = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        if (ls->dirRow) {
            if (lb) XBoxLayout_addWidget(ls->dirRow, (XWidget*)lb);
            if (combo) XBoxLayout_addWidgetEx(ls->dirRow, (XWidget*)combo, 1, 0);
            XBoxLayout_addLayout(ls->root, (XLayout*)ls->dirRow);
        }
    }
    /* 行 2：文件列表（模型 + 视图；目录/文件平铺单列，对标 listMode）。 */
    view = XListView_create((XWidget*)dlg, 0);
    if (view) {
        xff_setName((XObject*)view, XFF_NAME_VIEW);
        XWidget_setMinimumSize((XWidget*)view, 360, 220);
        model = XAbstractItemModel_create();
        if (model) {
            /* 对标 Qt：QAbstractItemView 不拥有 model，析构归调用方。 */
            XAbstractItemView_setModel((XAbstractItemView*)view, model);
            ls->model = model;
        }
        XBoxLayout_addWidget(ls->root, (XWidget*)view);
    }
    /* 行 3：文件名编辑（目录模式隐藏，对标 getExistingDirectory）。 */
    {
        XLabel* lb = XLabel_create((XWidget*)dlg, 0);
        XLineEdit* edit;
        XLabel_setText_2(lb, "文件名:");
        edit = XLineEdit_create((XWidget*)dlg, 0);
        if (edit) {
            xff_setName((XObject*)edit, XFF_NAME_NAMEEDIT);
            XWidget_setMinimumSize((XWidget*)edit, 240, 24);
            if (prefillName)
                XLineEdit_setText(edit, prefillName);
        }
        ls->nameRow = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        if (ls->nameRow) {
            if (lb) XBoxLayout_addWidget(ls->nameRow, (XWidget*)lb);
            if (edit) XBoxLayout_addWidgetEx(ls->nameRow, (XWidget*)edit, 1, 0);
            XBoxLayout_addLayout(ls->root, (XLayout*)ls->nameRow);
            if (fileMode == XFileDialog_Directory) {
                if (lb) XWidget_setVisible((XWidget*)lb, false);
                if (edit) XWidget_setVisible((XWidget*)edit, false);
            }
        }
    }
    /* 行 4：名称过滤器下拉（解析自 filter 串，对标 FileType 组合）。 */
    if (filter) {
        XLabel* lb = XLabel_create((XWidget*)dlg, 0);
        XComboBox* combo;
        XLabel_setText_2(lb, "文件类型:");
        combo = XComboBox_create((XWidget*)dlg, 0);
        if (combo) {
            xff_setName((XObject*)combo, XFF_NAME_FILTERCOMBO);
            XWidget_setMinimumSize((XWidget*)combo, 300, 26);
            if (dlg->m_nameFilters) {
                int64_t i, n =
                    XStringList_size_base(
                        (const XContainer*)dlg->m_nameFilters);
                for (i = 0; i < n; ++i) {
                    XString* f = (XString*)(void*)XStringList_at_base(
                        (const XVector*)dlg->m_nameFilters, i);
                    if (f)
                        XComboBox_insertItem_2(combo, (int)i,
                                               XString_toUtf8(f));
                }
                XComboBox_setCurrentIndex(combo, 0);
            }
        }
        ls->filterRow =
            XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        if (ls->filterRow) {
            if (lb) XBoxLayout_addWidget(ls->filterRow, (XWidget*)lb);
            if (combo) XBoxLayout_addWidgetEx(ls->filterRow, (XWidget*)combo, 1, 0);
            XBoxLayout_addLayout(ls->root, (XLayout*)ls->filterRow);
        }
    }
    /* 行 5：确定/取消（文本与 XDialogButtonBox 标准一致）。 */
    {
        XPushButton* ok = XPushButton_create((XWidget*)dlg, 0);
        XPushButton* cancel = XPushButton_create((XWidget*)dlg, 0);
        ls->bar = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        if (ls->bar) {
            XBoxLayout_addStretch(ls->bar, 1);
            if (ok) {
                XAbstractButton_setText_2((XAbstractButton*)ok, "确定");
                XWidget_setMinimumSize((XWidget*)ok, 80, 28);
                xff_setName((XObject*)ok, XFF_NAME_OK);
                XBoxLayout_addWidget(ls->bar, (XWidget*)ok);
                XObject_connect_1((XObject*)ok,
                                  (size_t)XAbstractButton_clicked_signal,
                                  (XObject*)dlg, xff_acceptSlot,
                                  XConnectionType_Direct);
            }
            if (cancel) {
                XAbstractButton_setText_2((XAbstractButton*)cancel, "取消");
                XWidget_setMinimumSize((XWidget*)cancel, 80, 28);
                xff_setName((XObject*)cancel, XFF_NAME_CANCEL);
                XBoxLayout_addWidget(ls->bar, (XWidget*)cancel);
                XObject_connect_1((XObject*)cancel,
                                  (size_t)XAbstractButton_clicked_signal,
                                  (XObject*)dlg, xff_rejectSlot,
                                  XConnectionType_Direct);
            }
            XBoxLayout_addLayout(ls->root, (XLayout*)ls->bar);
        }
    }
    /* 信号挂接（view/combo 槽；接收者均为对话框，对标 Qt 信号连接）。 */
    if (view) {
        XObject_connect_1((XObject*)view,
                          (size_t)XAbstractItemView_doubleClicked_signal,
                          (XObject*)dlg, xff_viewDoubleClicked,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)view,
                          (size_t)XAbstractItemView_clicked_signal,
                          (XObject*)dlg, xff_viewClicked,
                          XConnectionType_Direct);
    }
    {
        XComboBox* combo = (XComboBox*)xff_childByName(&dlg->m_base,
                                                       XFF_NAME_DIRCOMBO);
        if (combo)
            XObject_connect_1((XObject*)combo,
                              (size_t)XComboBox_activated_signal,
                              (XObject*)dlg, xff_dirActivated,
                              XConnectionType_Direct);
    }
    {
        XComboBox* combo = (XComboBox*)xff_childByName(&dlg->m_base,
                                                       XFF_NAME_FILTERCOMBO);
        if (combo)
            XObject_connect_1((XObject*)combo,
                              (size_t)XComboBox_activated_signal,
                              (XObject*)dlg, xff_filterActivated,
                              XConnectionType_Direct);
    }
    /* 首次填充：目录下拉当前项 + 文件列表。 */
    {
        XComboBox* combo = (XComboBox*)xff_childByName(&dlg->m_base,
                                                       XFF_NAME_DIRCOMBO);
        if (combo && dlg->m_directory &&
            XComboBox_findText_2(combo, XString_toUtf8(dlg->m_directory)) < 0)
            XComboBox_insertItem_2(combo, 0,
                                   XString_toUtf8(dlg->m_directory));
        if (combo)
            XComboBox_setCurrentIndex(combo, 0);
    }
    xff_refresh(dlg);
    return dlg;
}

/** @brief 收尾：先删布局（不随控件析构）再删对话框。 */
static void xff_teardown(XFileDialog* dlg, XFFLayouts* ls)
{
    if (ls->root) XLayout_delete_base((XLayout*)ls->root);
    if (ls->dirRow) XLayout_delete_base((XLayout*)ls->dirRow);
    if (ls->filterRow) XLayout_delete_base((XLayout*)ls->filterRow);
    if (ls->nameRow) XLayout_delete_base((XLayout*)ls->nameRow);
    if (ls->bar) XLayout_delete_base((XLayout*)ls->bar);
    if (dlg) XFileDialog_delete_base((XClass*)dlg);
    if (ls->model) XAbstractItemModel_delete_base((XClass*)ls->model);
}

/** @brief 当前选中过滤器在其列表中的下标（未命中返回 0）。 */
static int xff_selectedFilterIndex(const XFileDialog* dlg)
{
    int64_t i, n;
    if (!dlg || !dlg->m_selectedNameFilter || !dlg->m_nameFilters) return 0;
    n = XStringList_size_base((const XContainer*)dlg->m_nameFilters);
    for (i = 0; i < n; ++i) {
        const XString* f = (const XString*)(const void*)
            XStringList_at_base((const XVector*)dlg->m_nameFilters, i);
        if (f && XString_compare(f, dlg->m_selectedNameFilter) == 0)
            return (int)i;
    }
    return 0;
}

/** @brief 模态执行并回收：返回是否接受。 */
static bool xff_exec(XFileDialog* dlg)
{
    if (!dlg) return false;
    XWidget_resize((XWidget*)dlg, 480, 400);
    xff_centerOnScreen((XWidget*)dlg);
    return XDialog_exec(&dlg->m_base) == 1;
}

/** @brief 接受后取首个选中文件路径副本（无选中返回空串）。 */
static XString* xff_firstSelected(const XFileDialog* dlg)
{
    XString* item;
    if (!dlg || !dlg->m_selectedFiles) return XString_create();
    if (XStringList_size_base((const XContainer*)dlg->m_selectedFiles) == 0)
        return XString_create();
    item = (XString*)(void*)XStringList_at_base(
        (const XVector*)dlg->m_selectedFiles, 0);
    return item ? XString_create_copy(item) : XString_create();
}

/** @brief 真实弹窗路径公共主体：组装 → exec → 回收。 */
static bool xff_runDialog(XWidget* parent, const XString* caption,
                          const XString* dir, const XString* filter,
                          XFileDialogOptions options,
                          XFileDialogFileMode fileMode,
                          XFileDialogAcceptMode acceptMode,
                          const char* prefillName, XFileDialog** outDlg,
                          XFFLayouts* outLs)
{
    XFileDialog* dlg;
    if (!outDlg || !outLs) return false;
    *outDlg = NULL;
    dlg = xff_buildDialog(parent, caption, dir, filter, options, fileMode,
                          acceptMode, prefillName, outLs);
    if (!dlg) return false;
    *outDlg = dlg;
    return xff_exec(dlg);
}

#endif /* XFILE_ON && XDIR_ON */

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
    if (selectedFilterIndex) *selectedFilterIndex = 0;
    if (xff_guiReady()) {
#if XFILE_ON && XDIR_ON
        XFileDialog* dlg = NULL;
        XFFLayouts ls;
        bool accepted = xff_runDialog(parent, caption, dir, filter, 0,
                                      XFileDialog_ExistingFile,
                                      XFileDialog_AcceptOpen, NULL,
                                      &dlg, &ls);
        XString* result;
        if (selectedFilterIndex)
            *selectedFilterIndex = xff_selectedFilterIndex(dlg);
        result = accepted ? xff_firstSelected(dlg) : XString_create();
        xff_teardown(dlg, &ls);
        return result;
#else
        return XString_create(); /* 无 XDir 模块：无法浏览目录。 */
#endif
    }
    /* 无 GUI 环境（无头测试）：返回空串，*selectedFilterIndex=0（桩约定）。 */
    {
        XFileDialog* dlg;
        XString* result;
        dlg = xfiledialog_tempSetup(parent, caption, dir, filter, 0);
        if (!dlg) return XString_create();
        result = XString_create();
        XFileDialog_delete_base(dlg);
        return result;
    }
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
    if (selectedFilterIndex) *selectedFilterIndex = 0;
    /* 多选文件列表不在本批真实弹窗范围（单选 getOpenFileName 已实化）；
     * 无 GUI 环境桩约定：返回空列表、*selectedFilterIndex=0。 */
    {
        XFileDialog* dlg =
            xfiledialog_tempSetup(parent, caption, dir, filter, 0);
        XStringList* result =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (dlg) XFileDialog_delete_base(dlg);
        return result;
    }
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
    if (selectedFilterIndex) *selectedFilterIndex = 0;
    if (xff_guiReady()) {
#if XFILE_ON && XDIR_ON
        /* 对标 Qt：保存模式下 dir 可含文件名（路径不存在则按
         * "父目录 + 预填名"解释，预填文件名编辑框）。 */
        XFileDialog* dlg = NULL;
        XFFLayouts ls;
        XString* startDir = NULL;
        XString* prefill = NULL;
        char buf[1024];
        const char* dutf;
        const char* slash;
        bool accepted;
        XString* result;
        bool dirIsPath = false;
        if (dir && (dutf = XString_toUtf8(dir)) && dutf[0]) {
            XDir probe;
            XDir_init_2(&probe, dir);
            dirIsPath = XDir_exists_1(&probe);
            XDir_deinit_base((XClass*)&probe);
            if (!dirIsPath) {
                /* 非现存目录：按“父目录 + 预填文件名”拆分（对标 Qt
                 * getSaveFileName 传入完整文件路径的行为）。 */
                slash = strrchr(dutf, '/');
                if (slash && slash != dutf) {
                    snprintf(buf, sizeof(buf), "%.*s",
                             (int)(slash - dutf), dutf);
                    startDir = XString_create_utf8(buf);
                    prefill = XString_create_utf8(slash + 1);
                }
            }
        }
        if (!startDir && dir) {
            startDir = xfiledialog_dupString(dir);
        }
        accepted = xff_runDialog(parent, caption, startDir, filter, 0,
                                 XFileDialog_AnyFile,
                                 XFileDialog_AcceptSave,
                                 prefill ? XString_toUtf8(prefill) : NULL,
                                 &dlg, &ls);
        if (selectedFilterIndex)
            *selectedFilterIndex = xff_selectedFilterIndex(dlg);
        result = accepted ? xff_firstSelected(dlg) : XString_create();
        if (startDir) XString_delete_base((XClass*)startDir);
        if (prefill) XString_delete_base((XClass*)prefill);
        xff_teardown(dlg, &ls);
        return result;
#else
        return XString_create(); /* 无 XDir 模块：无法浏览目录。 */
#endif
    }
    /* 无 GUI 环境（无头测试）：返回空串，*selectedFilterIndex=0（桩约定）。 */
    {
        XFileDialog* dlg;
        XString* result;
        dlg = xfiledialog_tempSetup(parent, caption, dir, filter, 0);
        if (!dlg) return XString_create();
        if (dlg) XFileDialog_setAcceptMode(dlg, XFileDialog_AcceptSave);
        result = XString_create();
        XFileDialog_delete_base(dlg);
        return result;
    }
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
    if (xff_guiReady()) {
#if XFILE_ON && XDIR_ON
        /* 对标 Qt：目录模式 + ShowDirsOnly（文件行隐藏），确认取当前
         * 目录（双击进入子目录 / 双击 ".." 回上级后确认）。 */
        XFileDialog* dlg = NULL;
        XFFLayouts ls;
        bool accepted = xff_runDialog(parent, caption, dir, NULL,
                                      (XFileDialogOptions)XFileDialog_ShowDirsOnly,
                                      XFileDialog_Directory,
                                      XFileDialog_AcceptOpen, NULL,
                                      &dlg, &ls);
        XString* result;
        if (accepted && dlg && dlg->m_directory)
            result = xfiledialog_dupString(dlg->m_directory);
        else
            result = XString_create();
        xff_teardown(dlg, &ls);
        return result;
#else
        return XString_create(); /* 无 XDir 模块：无法浏览目录。 */
#endif
    }
    /* 无 GUI 环境（无头测试）：返回空串（桩约定）。 */
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

/* ---- 不透明借用登记（delegate/proxy）：结构体无专用槽位（XFileDialog.h
 * 归属主线头文件批次），经对象动态属性（XObject_setProperty，XVariant
 * Ptr 变体）承载，随对象析构自动释放；对标 Qt setItemDelegate/
 * setProxyModel 的"记录"语义（类型不映射，仅保存指针供回读）。 ---- */

/** @brief 写入一条不透明借用登记；指针为 NULL 时撤销登记。 */
static void xfiledialog_setOpaqueRecord(XFileDialog* self,
                                        const char* keyUtf8, void* value)
{
    XString key;
    if (!self || !keyUtf8) return;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    if (value) {
        XVariant* v = XVariant_create_ptr(value);
        if (v) {
            /* setProperty 成功后所有权转移给对象；失败则自回滚防泄漏。 */
            if (!XObject_setProperty((XObject*)self, &key, v))
                XVariant_delete_base(v);
        }
    } else {
        XObject_removeProperty((XObject*)self, &key);
    }
    XString_deinit_base(&key);
}

/** @brief 读回一条不透明借用登记；未登记返回 NULL。 */
static void* xfiledialog_opaqueRecord(const XFileDialog* self,
                                      const char* keyUtf8)
{
    XString key;
    XVariant* v;
    void* out = NULL;
    if (!self || !keyUtf8) return NULL;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    v = XObject_property((const XObject*)self, &key);
    if (v) out = XVariant_toPtr(v);
    XString_deinit_base(&key);
    return out;
}

void XFileDialog_setItemDelegate(XFileDialog* self, void* delegate)
{
    xfiledialog_setOpaqueRecord(self, "xgui.itemDelegate", delegate);
}

void* XFileDialog_itemDelegate(const XFileDialog* self)
{
    return xfiledialog_opaqueRecord(self, "xgui.itemDelegate");
}

void XFileDialog_setProxyModel(XFileDialog* self, void* model)
{
    xfiledialog_setOpaqueRecord(self, "xgui.proxyModel", model);
}

void* XFileDialog_proxyModel(const XFileDialog* self)
{
    return xfiledialog_opaqueRecord(self, "xgui.proxyModel");
}

#endif /* XWIDGET_ON && XDIALOG_ON */
