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
#include <stdlib.h>            /* getenv：家目录回退/导航窗格 */
#include <string.h>            /* strrchr：父目录推导 */
#include <time.h>              /* clock_gettime：激活手势去伪时间戳 */
#include "XCoreApplication.h"  /* qApp 等价物：有应用实例才允许模态循环 */
#include "XGuiApplication.h"   /* 主屏查询（弹窗居中） */
#include "XScreen.h"           /* 屏幕几何 */
#include "XObject.h"           /* 动态属性：delegate/proxy 借用登记 */
#include "XVariant.h"          /* Ptr 变体承载不透明指针 */
#include "XLabel.h"            /* 目录/文件名/类型标签 */
#include "XLineEdit.h"         /* 文件名编辑 */
#include "XComboBox.h"         /* 目录路径与过滤器下拉 */
#include "XListView.h"         /* 导航窗格列表 */
#include "XTreeWidget.h"       /* 文件列表（Win10 详情多列：名称/大小/类型） */
#include "XAbstractItemModel.h"/* 导航窗格数据模型 */
#include "XPushButton.h"       /* 确定/取消/后退/上级 */
#include "XBoxLayout.h"        /* 对话框布局 */
#include "XPalette.h"          /* Win10 观感按控件级调色（面板白底/输入白底） */
#include "XDir.h"              /* 目录列举（XFILE_ON && XDIR_ON 时生效） */
#include "XFileInfo.h"         /* 条目大小/类型（W10b-3 详情列数据） */

#if XWIDGET_ON && XDIALOG_ON

#include "XFileDialog.h"
#include "XWidget_Protected.h"

#if XFILE_ON && XDIR_ON
/* W10 表头段点击排序（实例级虚表接管承载；定义在真实弹窗组装区）：
 * XTreeWidget 表头为自绘带、无 sectionClicked 信号源，且框架指针
 * 事件经 XWidget_sendEvent 直投控件事件槽（绕过 notify/事件过滤器
 * 链）——故以克隆树实例虚表、替换 EXWidget_MousePressEvent 槽的
 * 钩子承载段点击拦截。 */
static void xff_treePressHook(XWidget* self, XEvent* event);
static int xff_treeSectionAt(const XTreeWidget* view, int x);
static void xff_headerClicked(XFileDialog* dlg, int section);
static bool xff_sortViewInstall(XFileDialog* dlg, XTreeWidget* view);
static void xff_sortViewUninstall(void);
#endif

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
    const char* utf8;
    if (!self) return;
    xfiledialog_freeString(&self->m_selectedNameFilter);
    if (self->m_nameFilters) {
        XStringList_clear_base((XContainer*)self->m_nameFilters);
    } else {
        self->m_nameFilters =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    }
    /* 对标 Qt 6.8 qfiledialog.cpp setNameFilter = setNameFilters(
     * qt_make_filter_list(filter))：入串按 ";;" 拆分为多条过滤器，
     * 串内无 ";;" 而含 '\n' 时按 '\n' 拆分（qt_make_filter_list 同款
     * 回退），空段跳过；首条成为当前选中（Qt useNameFilter(0) 语义）。
     * 此前整串原样登记为单条，"A;;B" 在过滤器下拉原样显示一条
     * （复扫-5 #31附3），文件通配解析也误混两段的括号模式。 */
    utf8 = filter ? XString_toUtf8(filter) : NULL;
    if (utf8 && utf8[0] && self->m_nameFilters) {
        const char* sep = strstr(utf8, ";;") ? ";;" : "\n";
        size_t sepLen = (sep[1] == '\0') ? 1u : 2u;
        const char* p = utf8;
        for (;;) {
            const char* hit = strstr(p, sep);
            size_t len = hit ? (size_t)(hit - p) : strlen(p);
            if (len > 0) {
                char* part =
                    (char*)XMemory_malloc(len + 1u,
                                          XCLASS_DEFAULT_MEMORY_TYPE);
                if (part) {
                    XString* item;
                    XMemcpy(part, p, len);
                    part[len] = '\0';
                    item = XString_create_utf8(part);
                    XFree_System(part);
                    if (item) {
                        XStringList_push_back_move_base(self->m_nameFilters,
                                                        item);
                        XString_delete_base((XClass*)item);
                    }
                }
            }
            if (!hit) break;
            p = hit + sepLen;
        }
    }
    /* 首条过滤器即当前选中（Qt useNameFilter(0)；无有效段保持空）。 */
    if (self->m_nameFilters &&
        XStringList_size_base((const XContainer*)self->m_nameFilters) > 0) {
        const XString* first = (const XString*)(const void*)
            XStringList_at_base((const XVector*)self->m_nameFilters, 0);
        if (first)
            self->m_selectedNameFilter = xfiledialog_dupString(first);
    }
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
 * 静态便捷函数：构造 XDialog + 后退/上级按钮 + 目录下拉 + 右端搜索框 +
 * 左侧导航窗格（桌面/文档/下载/图片/音乐/此电脑，单击跳转）+
 * XTreeWidget 文件列表树（名称/大小/类型 三列详情 + 表头；双击进目录、
 * 名称过滤、搜索前缀过滤）+ 文件名编辑 + 确定/取消，经 XDialog_exec
 * 阻塞式模态循环（应用模态、Escape→reject）；无 GUI 环境（无
 * XCoreApplication 实例，如无头测试）保持桩约定：返回默认值、
 * *selectedFilterIndex=0。目录列举依赖 XFILE_ON && XDIR_ON（XDir
 * 模块）；目录不可枚举时回退用户家目录（getenv("HOME")）。 */

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
#define XFF_NAME_NAV      "qt_file_dialog_sidebar"
#define XFF_NAME_UPBTN    "qt_file_dialog_up_button"
#define XFF_NAME_BACKBTN  "qt_file_dialog_back_button"
#define XFF_NAME_SEARCH   "qt_file_dialog_search_box"

/* Win10 视觉保真口径（对照 Windows 10 文件对话框实测观感）：
 * - 标题栏行高 28：与 XDialog 标题栏带（XDLG_TB_HEIGHT，#F9F9F9 底 +
 *   右端 [×]）严格相等——布局顶边距按此让位，地址行不再叠上标题栏
 *   遮挡标题字（活体实证：顶边距 12 时「上级」钮顶边 y=o.y+12 压过
 *   标题字形带）。
 * - 面板/输入白底 #FFFFFF：Win10 对话框主体与地址栏/搜索框/文件名框
 *   均为白底（主题默认面板 #EFEFEF、行编辑 Base #FFFFE0 偏黄，均按
 *   控件级 palette 覆写，不外溢全局主题）。
 * - 底部两行左对齐口径：「文件名(N):」「文件类型(T):」标签等宽（84px
 *   ≥ 两串字形宽），编辑框/下拉左缘对齐成列（Win10 底部两行口径）。 */
#define XFF_TB_CONTENT_TOP   28 /**< 有标题时内容让位（=标题栏行高） */
#define XFF_LABEL_COL_WIDTH  84 /**< 底部标签列等宽（两行左对齐） */
#define XFF_WIN10_WHITE      0xFFFFFFFFu /**< Win10 白底 */

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

/** @brief 按控件级 palette 覆写单角色颜色（不外溢全局主题）。
 *  @details Win10 观感落地手段：面板 Window=白、输入类 Base=白。
 *  XWidget_palette 返回副本、XWidget_setPalette 只影响该控件（子控件
 *  各自回落应用主题），故仅覆写传入控件自身。 */
static void xff_setRoleColor(XWidget* w, XPaletteColorRole role,
                             uint32_t rgba)
{
    XPalette pal;
    if (!w) return;
    pal = XWidget_palette(w);
    XPalette_setColor(&pal, XPaletteColorGroup_Active, role,
                      XColor_create_rgba(rgba));
    XWidget_setPalette(w, &pal);
}

/* ---------- 表头段点击排序的实例级虚表接管（W10 接线） ----------
 * XTreeWidget 表头为控件内自绘带（非 XHeaderView 实例，无
 * sectionClicked 信号源）：其 mousePress 只认分隔线 ±3px 拖宽热区，
 * 段内点击静默吞没（XTreeWidget.c 表头带分支）。框架指针事件经
 * XWidget_sendEvent 直投控件事件槽（XWidget.c XWidget_sendEvent→
 * XWidget_event_base，绕过 notify/事件过滤器链），事件过滤器对
 * 鼠标不可达——故以「实例级虚表克隆 + 按压槽替换」承载：对象不
 * 拥有虚表、派发恒经实例 m_vtable（XClass.h 注记 +
 * XWIDGET_VT_DISPATCH 的 XClassGetVirtualFunc 路径），克隆该树实例
 * 虚表并把 EXWidget_MousePressEvent 槽换成对话框侧钩子，即只对此
 * 树实例生效；钩子内段点击走排序翻转，其余原样转调原始槽（行带
 * 按压/展开/勾选/分隔线拖宽手势零回退）。段几何与命中按库侧自绘
 * 表头同一算法在对话框侧镜像（xtw_drawHeader/xtw_columnSpan/
 * xtw_headerHandleAt 均库内 static，契约头不扩）。 */

#define XFF_TREE_HEADER_H   20 /**< 表头带高（=XTreeWidget XTW_HEADER_H）。 */
#define XFF_TREE_HANDLE_HIT  3 /**< 分隔线拖宽热区半径（=XTW_HANDLE_HIT）。 */

#if XFILE_ON && XDIR_ON

/** @brief 已接管会话的对话框（钩子内 xff_headerClicked 的目标；
 *  静态便捷函数路径同一时刻至多一个模态对话框）。 */
static XFileDialog* xff_sortDialog = NULL;

/** @brief 已接管按压槽的树实例（借用；随对话框析构）。 */
static XTreeWidget* xff_sortView = NULL;

/** @brief 实例克隆虚表（本侧拥有；XVTABLEAt 写槽用）。 */
static XVtable* xff_sortViewVtable = NULL;

/** @brief 树实例原始按压槽（VXTreeWidget_mousePressEvent；钩子转调）。 */
static void (*xff_treePressOrig)(XWidget*, XEvent*) = NULL;

/** @brief 按压槽钩子：表头带内、非分隔线热区的左键按压 → 段点击
 *  排序（吞事件，对标 Qt 表头点击不移焦——树按压路径的 setFocus
 *  不再执行）；其余按压原样转调原始槽（零回退）。 */
static void xff_treePressHook(XWidget* self, XEvent* event)
{
    if (event && XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XMouseEvent* me = (XMouseEvent*)event;
        XTreeWidget* view = (XTreeWidget*)self;
        if (XMouseEvent_button(me) == XMouseButton_LeftButton &&
            !XTreeView_isHeaderHidden(&view->m_base)) {
            XPoint pos = XMouseEvent_position(me);
            if (pos.y >= 0 && pos.y < XFF_TREE_HEADER_H) {
                int section = xff_treeSectionAt(view, pos.x);
                if (section >= 0) {
                    xff_headerClicked(xff_sortDialog, section);
                    XEvent_accept(event);
                    return;
                }
            }
        }
    }
    if (xff_treePressOrig) xff_treePressOrig(self, event);
}

/** @brief 接管树实例按压槽：克隆实例虚表 → 记录原始槽 → 换入钩子 →
 *  实例指回克隆表。失败返回 false（排序不可用，弹窗其余功能不受
 *  影响）。对象不拥有虚表（XClass.h 注记），克隆表归本侧
 *  （xff_sortViewUninstall 回收）。 */
static bool xff_sortViewInstall(XFileDialog* dlg, XTreeWidget* view)
{
    XVtable* src;
    XVtable* clone;
    size_t bytes;
    void* orig;
    if (!dlg || !view) return false;
    if (xff_sortViewVtable) return true; /* 已接管（串行会话理论不达）。 */
    src = XClassGetVtable(view);
    if (!src || !src->data || src->capacity == 0) return false;
    clone = (XVtable*)XMemory_malloc(sizeof(XVtable),
                                     XCLASS_DEFAULT_MEMORY_TYPE);
    if (!clone) return false;
    *clone = *src; /* 拷贝 size/capacity/name 标志，data 换堆拷贝。 */
    bytes = sizeof(void*) * (size_t)src->capacity;
    clone->data = (void**)XMemory_malloc(bytes, XCLASS_DEFAULT_MEMORY_TYPE);
    if (!clone->data) {
        XFree_System(clone);
        return false;
    }
    XMemcpy(clone->data, src->data, bytes);
    clone->isStack = 0; /* data 为本侧堆拷贝（语义对齐堆模式虚表）。 */
    orig = XVtable_at(clone, (size_t)EXWidget_MousePressEvent);
    if (!orig) {
        XFree_System(clone->data);
        XFree_System(clone);
        return false;
    }
    xff_treePressOrig = (void (*)(XWidget*, XEvent*))orig;
    XVtable_At(clone, (size_t)EXWidget_MousePressEvent) =
        (void*)xff_treePressHook;
    XClassGetVtable(view) = clone;
    xff_sortViewVtable = clone;
    xff_sortView = view;
    xff_sortDialog = dlg;
    return true;
}

/** @brief 回收接管：还原实例虚表指向 + 释放克隆表与钩子态
 *  （须在树实例析构前调用——xff_teardown 删对话框前）。 */
static void xff_sortViewUninstall(void)
{
    if (xff_sortView && xff_sortViewVtable)
        XClassGetVtable(xff_sortView) = XTreeWidget_class_init();
    if (xff_sortViewVtable) {
        if (xff_sortViewVtable->data) XFree_System(xff_sortViewVtable->data);
        XFree_System(xff_sortViewVtable);
    }
    xff_sortView = NULL;
    xff_sortViewVtable = NULL;
    xff_treePressOrig = NULL;
    xff_sortDialog = NULL;
}

#endif /* XFILE_ON && XDIR_ON */

/** @brief 弹窗主屏居中（对标 Qt 静态便捷函数把对话框定位于屏幕中央）。 */
static void xff_centerOnScreen(XWidget* w)
{
    /* 子控件形态对话框居中于父控件（几何为父系坐标；屏幕坐标会落
     * 到页面坐标系外被裁剪）。无父时回退屏幕居中。 */
    XWidget* parent = w ? XWidget_parentWidget(w) : NULL;
    if (parent) {
        int pw = XWidget_width(parent);
        int ph = XWidget_height(parent);
        int dw = XWidget_width(w);
        int dh = XWidget_height(w);
        XWidget_move(w, pw > dw ? (pw - dw) / 2 : 0,
                        ph > dh ? (ph - dh) / 2 : 0);
        return;
    }
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
}

#if XFILE_ON && XDIR_ON

/* ---------- 目录浏览基础设施 ---------- */

/* ---------- 浏览会话状态（W10b-1 返回栈 / W10b-4 搜索前缀） ----------
 * 静态便捷函数路径同一时刻至多一个模态对话框（xff_runDialog 组装→
 * exec→回收严格串行），故承载为文件级静态：构建时与回收时清空
 * （xff_browsingStateReset），避免跨对话框残留。返回栈容器进程期
 * 常驻（可达静态存储，非泄漏口径），内容随清空释放。 */

/** @brief 后退历史栈（压入曾访问目录；对标 QFileDialog backStack）。 */
static XStringList* xff_backStack = NULL;

/** @brief 搜索前缀（W10b-4；空串=不过滤，ASCII 大小写不敏感）。 */
static char xff_searchPrefix[64] = "";

/** @brief 当前排序列（W10 表头段点击排序；0=名称 1=大小 2=类型，
 *  -1=未排序缺省序）。静态便捷函数同一时刻至多一个模态对话框，与
 *  后退栈/搜索前缀同一会话承载生命周期（xff_browsingStateReset 于
 *  构建前/回收后清防跨对话框残留；会话内换目录/切过滤器保持排序态，
 *  对标 QFileDialog 排序跨导航持久）。 */
static int xff_sortColumn = -1;

/** @brief 当前排序方向（0=升序，1=降序；对标 Qt AscendingOrder/
 *  DescendingOrder）。 */
static int xff_sortOrder = 0;

/** @brief 返回栈容器引用（惰性创建；失败返回 NULL，调用方降级）。 */
static XStringList* xff_backStackRef(void)
{
    if (!xff_backStack)
        xff_backStack =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    return xff_backStack;
}

/* ---------- 激活手势去伪（单击不导航/双击不重复） ----------
 * 框架条目视图的按压路径会把"单击按下"也翻译成 activated
 * （XAbstractItemView 按压路径先发 clicked 后发 activated；树双击
 * 处理又会在 itemDoubleClicked 之后补发一次 itemActivated，与基类
 * activated 汇入同槽）。文件对话框语义（对标 QFileDialog）要求：
 * 单击只选中/回填，双击/Return 才激活。这里以两次同栈发射的落点
 * 时间差配对去伪：
 *  - activated 紧跟在单击回填（xff_viewClicked，同一次按压栈内先后
 *    发射）→ 按压伪激活，忽略；
 *  - activated 紧跟在已处理的一次激活（xff_viewRowActivated）之后
 *    → 双击的补发重复，忽略；
 *  - 键盘 Return 的 activated 无按压配对、距上次激活远超窗口 →
 *    真激活，放行。
 * 窗口取 50ms：同栈两次发射间隔为微秒级，远小于窗口；而双击两次
 * 按压间隔（人类 ~200ms 起）远大于窗口，真双击不受误伤。 */
#define XFF_ACTIVATE_PAIR_MS 50 /**< 同栈配对窗口（毫秒）。 */

/** @brief 单调毫秒时钟（去伪配对用；不可得时回退 0，仅退化为不过滤）。 */
static int64_t xff_nowMs(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

/** @brief 最近一次单击回填落点（按压伪激活配对锚点；-1=尚无）。 */
static int64_t xff_lastClickMs = -1;

/** @brief 最近一次已处理激活落点（双击补发去重锚点；-1=尚无）。 */
static int64_t xff_lastActivateMs = -1;

/** @brief 清空浏览会话状态（对话框构建前/回收后调用防跨实例残留）。 */
static void xff_browsingStateReset(void)
{
    if (xff_backStack)
        XStringList_clear_base((XContainer*)xff_backStack);
    xff_searchPrefix[0] = '\0';
    xff_sortColumn = -1;
    xff_sortOrder = 0;
    xff_lastClickMs = -1;
    xff_lastActivateMs = -1;
}
/** @brief 后退可用（栈非空）。 */
static bool xff_backCanGo(void)
{
    return xff_backStack &&
        XStringList_size_base((const XContainer*)xff_backStack) > 0;
}

/** @brief 同步后退按钮启用态：栈空禁用灰化（对标 Qt 回退钮）。 */
static void xff_updateBackButton(XFileDialog* dlg)
{
    XPushButton* back;
    if (!dlg) return;
    back = (XPushButton*)xff_childByName(&dlg->m_base, XFF_NAME_BACKBTN);
    if (back)
        XWidget_setEnabled((XWidget*)back, xff_backCanGo());
}

/** @brief ASCII 大小写折叠（本地化无关，搜索匹配用）。 */
static char xff_asciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/** @brief ASCII 大小写不敏感名称比较（W10 名称列排序键）。
 *  逐字节折叠后比较；非 ASCII（UTF-8 多字节序列）字节原样参与——
 *  UTF-8 字节序即码点序，与 XStrcmp/XString_compare 既有承载一致。
 *  全库无本地化整序器（XDir_LocaleAware 在 XDir 比较器中未消费、
 *  XTreeWidget_sortItems 键为字节敏感 XStrcmp），名称列「大小写
 *  不敏感本地化比较」以此近似承载（搜索匹配 xff_asciiLower 同款
 *  折叠口径）。 */
static int xff_nameCaseCmp(const char* a, const char* b)
{
    const unsigned char* p = (const unsigned char*)(a ? a : "");
    const unsigned char* q = (const unsigned char*)(b ? b : "");
    while (*p && *q) {
        unsigned char ca = (unsigned char)xff_asciiLower((char)*p);
        unsigned char cb = (unsigned char)xff_asciiLower((char)*q);
        if (ca != cb) return ca < cb ? -1 : 1;
        ++p;
        ++q;
    }
    if (*p) return 1;
    if (*q) return -1;
    return 0;
}

/** @brief 文本键显示序映射（W10 表头排序：名称列键=条目名称、类型列
 *  键=类型列文本）：order[k]=源下标（相对 offset 基准），稳定插入序
 *  （键=xff_nameCaseCmp ASCII 大小写折叠比较，相等不换位保持源序——
 *  对标 Qt 稳定排序等键保位）。返回堆数组（调用方 XFree_System 回收）；
 *  list 为空/分配失败返回 NULL（调用方按恒等序降级）。 */
static int64_t* xff_textOrderMap(const XStringList* list, int64_t offset,
                                 int64_t n, bool descending)
{
    int64_t* order;
    int64_t i;
    if (!list || n <= 0) return NULL;
    order = (int64_t*)XMemory_malloc(sizeof(int64_t) * (size_t)n,
                                     XCLASS_DEFAULT_MEMORY_TYPE);
    if (!order) return NULL;
    for (i = 0; i < n; ++i) order[i] = i;
    for (i = 1; i < n; ++i) {
        int64_t key = order[i];
        int64_t j = i - 1;
        const XString* ks = (const XString*)(const void*)
            XStringList_at_base(list, offset + key);
        const char* keyText = ks ? XString_toUtf8(ks) : NULL;
        while (j >= 0) {
            const XString* js = (const XString*)(const void*)
                XStringList_at_base(list, offset + order[j]);
            const char* jText = js ? XString_toUtf8(js) : NULL;
            int cmp = xff_nameCaseCmp(jText, keyText);
            bool shift = descending ? (cmp < 0) : (cmp > 0);
            if (!shift) break;
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
    return order;
}

/** @brief 大小列当前排序态下的显示序映射（order[k]=源下标；键=原始
 *  字节值数值比较、稳定等值保序）。返回堆数组（调用方 XFree_System
 *  回收）；sizes 为空/分配失败返回 NULL（调用方按恒等序降级）。
 *  @note 库侧 XDir_entryList_2 枚举不 stat（XDir.c info.size 恒 0）、
 *  XDir_Size 比较器恒等值退化 readdir 序——原始字节值由 xff_listDir
 *  在装载三列文本时同源采集（XFileInfo_size），数值序在此承载。 */
static int64_t* xff_sizeOrderMap(const int64_t* sizes, int64_t n,
                                 bool descending)
{
    int64_t* order;
    int64_t i;
    if (!sizes || n <= 0) return NULL;
    order = (int64_t*)XMemory_malloc(sizeof(int64_t) * (size_t)n,
                                     XCLASS_DEFAULT_MEMORY_TYPE);
    if (!order) return NULL;
    for (i = 0; i < n; ++i) order[i] = i;
    for (i = 1; i < n; ++i) {
        int64_t key = order[i];
        int64_t j = i - 1;
        while (j >= 0) {
            int64_t a = sizes[order[j]];
            int64_t b = sizes[key];
            bool shift = descending ? (a < b) : (a > b);
            if (!shift) break;
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }
    return order;
}


/** @brief 搜索过滤判定：名称是否以当前搜索前缀开头（大小写不敏感；
 *  空前缀恒通过=全列显示）。 */
static bool xff_searchAccept(const char* name)
{
    size_t i;
    if (!xff_searchPrefix[0]) return true;
    if (!name) return false;
    for (i = 0; xff_searchPrefix[i]; ++i) {
        if (xff_asciiLower(name[i]) !=
            xff_asciiLower(xff_searchPrefix[i]))
            return false;
    }
    return true;
}


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

/** @brief 探测目录路径可用（存在性；栈上探测不拥有对象，与
 *  getSaveFileName 内 XDir_init_2+exists_1 探测同款模式）。 */
static bool xff_dirUsable(const XString* dirStr)
{
    XDir probe;
    bool ok;
    if (!dirStr) return false;
    XDir_init_2(&probe, dirStr);
    ok = XDir_exists_1(&probe);
    XDir_deinit_base((XClass*)&probe);
    return ok;
}

/** @brief 用户家目录回退路径（getenv("HOME")，XDir 探测存在才返回；
 *  无可用回退返回 NULL。返回值调用方拥有）。 */
static XString* xff_homeDir(void)
{
    const char* home = getenv("HOME");
    XString* hs;
    if (!home || !home[0]) return NULL;
    hs = XString_create_utf8(home);
    if (!hs) return NULL;
    if (!xff_dirUsable(hs)) {
        XString_delete_base((XClass*)hs);
        return NULL;
    }
    return hs;
}

/** @brief 文件大小人性化文本（B/KB/MB/GB，对标 Win10 大小列）。 */
static void xff_formatSize(char* buf, size_t cap, int64_t bytes)
{
    if (!buf || cap == 0) return;
    if (bytes < 1024)
        snprintf(buf, cap, "%lld B", (long long)bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, cap, "%.1f KB", (double)bytes / 1024.0);
    else if (bytes < 1024LL * 1024 * 1024)
        snprintf(buf, cap, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else
        snprintf(buf, cap, "%.1f GB",
                 (double)bytes / (1024.0 * 1024.0 * 1024.0));
}

/** @brief 条目类型列文本：目录「文件夹」；有扩展名「<大写SUF> 文件」；
 *  无扩展名「文件」（对标 Win10 类型列）。返回堆串（调用方拥有）。 */
static XString* xff_typeTextOf(XFileInfo* info)
{
    char buf[128];
    char upper[96];
    XString* suffix;
    const char* s;
    size_t i;
    if (!info) return XString_create_utf8("文件");
    if (XFileInfo_isDir(info)) return XString_create_utf8("文件夹");
    suffix = XFileInfo_completeSuffix(info);
    s = suffix ? XString_toUtf8(suffix) : NULL;
    if (!s || !s[0]) {
        if (suffix) XString_delete_base((XClass*)suffix);
        return XString_create_utf8("文件");
    }
    for (i = 0; s[i] && i + 1 < sizeof(upper); ++i)
        upper[i] = (s[i] >= 'a' && s[i] <= 'z')
            ? (char)(s[i] - 'a' + 'A') : s[i];
    upper[i] = '\0';
    snprintf(buf, sizeof(buf), "%s 文件", upper);
    XString_delete_base((XClass*)suffix);
    return XString_create_utf8(buf);
}

/** @brief 从 XFileInfo 提取一行三列文本追加进平行表（names/sizes/
 *  types 同步追加保持行序一致；对应表为 NULL 则跳过该列）。 */
static void xff_appendInfoRow(XStringList* names, XStringList* sizes,
                              XStringList* types, XFileInfo* info)
{
    char sbuf[32];
    XString* nm;
    XString* tp;
    if (!names || !info) return;
    nm = XFileInfo_fileName(info);
    if (nm) {
        XStringList_push_back_utf8(names, XString_toUtf8(nm));
        XString_delete_base((XClass*)nm);
    }
    if (sizes) {
        if (XFileInfo_isDir(info))
            XStringList_push_back_utf8(sizes, "文件夹");
        else {
            xff_formatSize(sbuf, sizeof(sbuf), XFileInfo_size(info));
            XStringList_push_back_utf8(sizes, sbuf);
        }
    }
    if (types) {
        tp = xff_typeTextOf(info);
        if (tp) {
            XStringList_push_back_utf8(types, XString_toUtf8(tp));
            XString_delete_base((XClass*)tp);
        }
    }
}

/** @brief 列出目录内容（名称过滤只作用于文件，目录恒列出，对标
 *  QFileDialog 过滤语义）。行序约定：目录表在前、文件表在后。
 *  sizes/types 为平行输出表（与 dirs+files 拼接序一致：前 dirs 段
 *  后 files 段；目录行大小列「文件夹」；可为 NULL 不取）——名称与
 *  大小/类型同源于一次 entryInfoList 枚举，行映射不脱节。
 *  outOrderDirs/outOrderFiles 输出当前表头排序态的显示序映射
 *  （order[k]=各自表内源下标；NULL=恒等序，调用方 XFree_System 回收）：
 *  名称列=名称 ASCII 折叠稳定序（xff_textOrderMap）；大小列=原始字节
 *  值数值稳定序（xff_sizeOrderMap，原始值在此与三列文本同源采集——
 *  库侧 XDir_entryList_2 枚举不 stat、XDir_Size 比较器恒等值退化
 *  readdir 序，见 xff_sizeOrderMap 注记）；类型列=类型列文本折叠
 *  稳定序（xff_textOrderMap，键取 types 平行表文件段——库侧 XDir_Type
 *  比较器实测乱序，活体探针核实）。目录组恒名称序（目录行大小/
 *  类型键恒等，名称序即确定序，对标 Qt 稳定序等键保位）。 */
static void xff_listDir(const XString* dirStr, const XStringList* patterns,
                        bool dirsOnly, XStringList** outDirs,
                        XStringList** outFiles, XStringList** outSizes,
                        XStringList** outTypes, int64_t** outOrderDirs,
                        int64_t** outOrderFiles)
{
    XDir* dir;
    XFileInfoList* di = NULL;
    XFileInfoList* fi = NULL;
    int64_t* rawSizes = NULL; /* 大小列数值序键（nd+nf 平行；可 NULL）。 */
    int64_t rawN = 0;
    size_t i, n;
    if (outOrderDirs) *outOrderDirs = NULL;
    if (outOrderFiles) *outOrderFiles = NULL;
    *outDirs = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    *outFiles = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (outSizes)
        *outSizes = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (outTypes)
        *outTypes = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!*outDirs || !*outFiles) return;
    dir = dirStr ? XDir_create_2(dirStr) : NULL;
    if (dir && !XDir_exists_1(dir)) {
        XDir_delete_base((XClass*)dir);
        dir = NULL;
    }
    if (!dir) {
        /* 目录枚举失败（XDir_create_2/exists 失败，含 dirStr 为空）
         * 不再静默空列表：回退用户家目录重试（夜间实证：CWD 无子
         * 目录且无匹配文件时列表只剩 ".." 行，家目录回退是解药；
         * 家目录也不可用则维持空列表原语义）。 */
        XString* home = xff_homeDir();
        if (home) {
            dir = XDir_create_2(home);
            XString_delete_base((XClass*)home);
            if (dir && !XDir_exists_1(dir)) {
                XDir_delete_base((XClass*)dir);
                dir = NULL;
            }
        }
        if (!dir) return;
    }
    /* 名称与大小/类型同源：条目信息表一次枚举同时产出三列。
     * 源枚举恒按 XDir_Name|XDir_DirsFirst 基序（三列统一）：表头排序
     * 的方向与键序全部由显示序映射承载（见函数头注记）——库侧
     * XDir_Size 键缺 stat（恒 0）、XDir_Type 比较器实测乱序（活体
     * 探针核实），除名称外的源旗标不可依赖；基序取名称序还使等键
     * 条目按名称保位，与 Qt 稳定序等键保位一致。目录表恒按名称
     * （目录行大小/类型键恒「文件夹」，名称序即确定序）；目录恒在
     * 文件前由目录/文件两表分段装载结构性保证，XDir_DirsFirst 分组
     * 比较器双保险。 */
    di = XDir_entryInfoList_2(
        dir, NULL, (XDirFilters)(XDir_Dirs | XDir_AllDirs |
                                 XDir_NoDotAndDotDot),
        (XDirSortFlags)(XDir_Name | XDir_DirsFirst));
    if (!dirsOnly) {
        if (!patterns ||
            XStringList_size_base((const XContainer*)patterns) == 0)
            fi = XDir_entryInfoList_2(
                dir, NULL, (XDirFilters)(XDir_Files | XDir_NoDotAndDotDot),
                (XDirSortFlags)(XDir_Name | XDir_DirsFirst));
        else
            fi = XDir_entryInfoList_2(
                dir, patterns,
                (XDirFilters)(XDir_Files | XDir_NoDotAndDotDot),
                (XDirSortFlags)(XDir_Name | XDir_DirsFirst));
    }
    /* 大小列数值键同源采集：容量=两表行数和，装载循环内逐行填充
     * （XFileInfo_size 与大小列文本同一来源，行映射不脱节）。 */
    n = (di ? XVector_size_base(di) : 0) + (fi ? XVector_size_base(fi) : 0);
    if (xff_sortColumn == 1 && n > 0) {
        rawSizes = (int64_t*)XMemory_malloc(sizeof(int64_t) * (size_t)n,
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    }
    if (di) {
        n = XVector_size_base(di);
        for (i = 0; i < n; ++i) {
            XFileInfo* info = (XFileInfo*)XVector_at_base(di, (int64_t)i);
            xff_appendInfoRow(*outDirs, outSizes ? *outSizes : NULL,
                              outTypes ? *outTypes : NULL, info);
            if (rawSizes) rawSizes[rawN++] = XFileInfo_size(info);
        }
        XVector_delete_base(di);
    }
    if (fi) {
        n = XVector_size_base(fi);
        for (i = 0; i < n; ++i) {
            XFileInfo* info = (XFileInfo*)XVector_at_base(fi, (int64_t)i);
            xff_appendInfoRow(*outFiles, outSizes ? *outSizes : NULL,
                              outTypes ? *outTypes : NULL, info);
            if (rawSizes) rawSizes[rawN++] = XFileInfo_size(info);
        }
        XVector_delete_base(fi);
    }
    /* 显示序映射按当前排序态产出（NULL=恒等序）；nd/nf 取两表行数。 */
    {
        bool desc = (xff_sortOrder == 1);
        int64_t ndAll = XStringList_size_base((const XContainer*)*outDirs);
        int64_t nfAll = XStringList_size_base((const XContainer*)*outFiles);
        if (xff_sortColumn == 0) {
            /* 名称列：键=名称（ASCII 折叠）。 */
            if (outOrderDirs)
                *outOrderDirs = xff_textOrderMap(*outDirs, 0, ndAll, desc);
            if (outOrderFiles)
                *outOrderFiles = xff_textOrderMap(*outFiles, 0, nfAll, desc);
        } else if (xff_sortColumn == 1) {
            /* 大小列：键=原始字节值（数值）。 */
            if (outOrderFiles)
                *outOrderFiles = xff_sizeOrderMap(
                    rawSizes ? rawSizes + ndAll : NULL, nfAll, desc);
        } else if (xff_sortColumn == 2 && outTypes && *outTypes) {
            /* 类型列：键=类型列文本（「TXT 文件」等，即大写扩展名）；
             * types 平行表含目录段（前 ndAll 行），文件段自 ndAll 起。 */
            if (outOrderFiles)
                *outOrderFiles = xff_textOrderMap(*outTypes, ndAll, nfAll,
                                                  desc);
        }
    }
    if (rawSizes) XFree_System(rawSizes);
    XDir_delete_base((XClass*)dir);
}

/** @brief 拼接目录与名称为路径（'/' 分隔；对标 QDir::filePath 简化）。
 *  目录以 '/' 结尾（根目录 "/" 或导航窗格「此电脑」目标）时不再追加
 *  分隔符，防产 "//sub" 双斜杠路径（目录模式确认/双击进子目录共用）。 */
static XString* xff_joinPath(const XString* dir, const char* name)
{
    char buf[1024];
    const char* d = (dir && XString_toUtf8(dir)) ? XString_toUtf8(dir) : ".";
    size_t n = strlen(d);
    if (n > 0 && d[n - 1] == '/')
        snprintf(buf, sizeof(buf), "%s%s", d, name ? name : "");
    else
        snprintf(buf, sizeof(buf), "%s/%s", d, name ? name : "");
    return XString_create_utf8(buf);
}

/** @brief 推导父目录（对标 QFileDialog 双击 ".." 回上级）。
 *  @details 相对路径（demo 便捷路径传起始目录 "."）旧实现 strrchr
 *  落空回落 "."（自映射）：「上级」钮/双击 ".." 行在 CWD 相对目录下
 *  原地踏步（:121 活体 g2 组复现，g1 组绝对路径下则正常）。按
 *  xff_listDir 相对枚举同口径，把无斜杠目录解析为进程 CWD 下的条目：
 *  parent(".")=parent(CWD)、parent("foo")=CWD。 */
static XString* xff_parentOf(const XString* dir)
{
    const char* s;
    const char* slash;
    if (!dir || !(s = XString_toUtf8(dir)) || !s[0])
        return XString_create_utf8("/");
    if (!strchr(s, '/')) {
        XString* cwd = XDir_currentPath();
        XString* parent;
        if (cwd) {
            parent = xff_parentOf(cwd); /* CWD 为绝对路径，递归一层止。 */
            XString_delete_base((XClass*)cwd);
            return parent;
        }
        return XString_create_utf8("/"); /* CWD 不可得兜底：根。 */
    }
    slash = strrchr(s, '/');
    if (!slash) return XString_create_utf8(".");
    if (slash == s) return XString_create_utf8("/");
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%.*s", (int)(slash - s), s);
        return XString_create_utf8(buf);
    }
}

/** @brief 导航窗格条目（对标 Qt QFileDialog 侧栏 QUrlModel：用户可读
 *  名 + 实际路径两平行表，行序一一对应）。「此电脑」恒为根 "/"；
 *  家目录下 Desktop/Documents/Downloads/Pictures/Music 经 XDir 探测
 *  存在才显示。条目行序确定（环境不变则重建同序），故点击槽按需
 *  重建后按下标解析路径，无需跨槽持有状态。 */
static void xff_navEntries(XStringList** outNames, XStringList** outPaths)
{
    static const char* const kNames[] = {
        "桌面", "文档", "下载", "图片", "音乐"
    };
    static const char* const kSubs[] = {
        "Desktop", "Documents", "Downloads", "Pictures", "Music"
    };
    const char* home;
    int i;
    *outNames = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    *outPaths = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!*outNames || !*outPaths) {
        if (*outNames) XStringList_delete_base((XClass*)*outNames);
        if (*outPaths) XStringList_delete_base((XClass*)*outPaths);
        *outNames = NULL;
        *outPaths = NULL;
        return;
    }
    /* 「此电脑」= 根目录（恒显示；对标 Win10 侧栏此电脑入口）。 */
    XStringList_push_back_utf8(*outNames, "此电脑");
    XStringList_push_back_utf8(*outPaths, "/");
    home = getenv("HOME");
    if (!home || !home[0]) return;
    for (i = 0; i < (int)(sizeof(kNames) / sizeof(kNames[0])); ++i) {
        char buf[1024];
        XString* cand;
        snprintf(buf, sizeof(buf), "%s/%s", home, kSubs[i]);
        cand = XString_create_utf8(buf);
        if (!cand) continue;
        if (xff_dirUsable(cand)) {
            XStringList_push_back_utf8(*outNames, kNames[i]);
            XStringList_push_back_utf8(*outPaths, buf);
        }
        XString_delete_base((XClass*)cand);
    }
}

/* ---------- 槽与视图刷新 ---------- */

static void xff_acceptSlot(XObject* receiver, XVarList* args);
static void xff_rejectSlot(XObject* receiver, XVarList* args);

/** @brief 文件列表追加一行三列（条目所有权转移给树；装载失败自删）。
 *  列序：0 名称 / 1 大小 / 2 类型（Win10 详情视图）。 */
static void xff_addTreeRow(XTreeWidget* view, const char* name,
                           const char* size, const char* type)
{
    XTreeWidgetItem* item;
    if (!view || !name) return;
    item = XTreeWidgetItem_create_2(name, NULL);
    if (!item) return;
    XTreeWidgetItem_setTextAt_2(item, 1, size ? size : "");
    XTreeWidgetItem_setTextAt_2(item, 2, type ? type : "");
    if (!XTreeWidget_addTopLevelItem(view, item))
        XTreeWidgetItem_delete(item);
}

/** @brief 通过搜索过滤的条目个数（与 xff_nthAccepted 同一次序口径）。 */
static int64_t xff_countAccepted(const XStringList* names)
{
    int64_t i, n, seen = 0;
    if (!names) return 0;
    n = XStringList_size_base((const XContainer*)names);
    for (i = 0; i < n; ++i) {
        const XString* item = (const XString*)(const void*)
            XStringList_at_base(names, i);
        if (item && xff_searchAccept(XString_toUtf8(item))) ++seen;
    }
    return seen;
}

/** @brief 第 nth 个通过搜索过滤的条目副本（无则 NULL）。双击槽与
 *  xff_refresh 装载共用「已接受条目序」映射，搜索过滤下行号不脱节。
 *  order 为显示序映射（xff_listDir 产出；NULL=按源序走，
 *  大小/类型列序由 XDir 源旗标承载、源序即显示序）——激活命中与
 *  排序后的显示行严格一致。 */
static XString* xff_nthAccepted(const XStringList* names, int64_t nth,
                                const int64_t* order, int64_t orderN)
{
    int64_t i, n, seen = 0;
    if (!names || nth < 0) return NULL;
    n = XStringList_size_base((const XContainer*)names);
    for (i = 0; i < n; ++i) {
        int64_t src = (order && i < orderN) ? order[i] : i;
        XString* item = (XString*)(void*)XStringList_at_base(names, src);
        if (!item) continue;
        if (!xff_searchAccept(XString_toUtf8(item))) continue;
        if (seen == nth) return XString_create_copy(item);
        ++seen;
    }
    return NULL;
}

/** @brief 按当前目录/过滤器/搜索前缀/表头排序态重填文件列表树（W10b-3
 *  多列详情视图：行 0 固定 ".. (上级目录)"——修复裸 ".." 歧义，随后
 *  目录行、文件行，各带 大小/类型 列；双击命中按同一确定性次序解析，
 *  行号不依赖文案；搜索前缀过滤只作用于目录/文件行（行 0 恒显示））。
 *  排序（W10 表头段点击）：装载序由 xff_listDir 输出的显示序映射承载
 *  （名称列=ASCII 大小写折叠稳定序、大小列=原始字节值数值稳定序、
 *  类型列=XDir_Type 源旗标序恒等）——装载遍历按映射取源下标，大小/
 *  类型平行表同下标随动，行映射不脱节。目录恒在文件前（两表分段
 *  装载）。 */
static void xff_refresh(XFileDialog* dlg)
{
    XTreeWidget* view;
    XStringList* patterns;
    XStringList* dirs = NULL;
    XStringList* files = NULL;
    XStringList* sizes = NULL;
    XStringList* types = NULL;
    bool dirsOnly;
    int64_t i, nd, nf;
    int64_t* orderD = NULL;
    int64_t* orderF = NULL;
    if (!dlg) return;
    view = (XTreeWidget*)xff_childByName(&dlg->m_base, XFF_NAME_VIEW);
    if (!view) return;
    patterns = dlg->m_selectedNameFilter
        ? xff_filterPatterns(dlg->m_selectedNameFilter) : NULL;
    dirsOnly = (dlg->m_options & (XFileDialogOptions)XFileDialog_ShowDirsOnly)
               ? true : false;
    xff_listDir(dlg->m_directory, patterns, dirsOnly, &dirs, &files,
                &sizes, &types, &orderD, &orderF);
    XStringList_delete_base((XClass*)patterns);
    if (!dirs || !files || !sizes || !types) {
        if (dirs) XStringList_delete_base((XClass*)dirs);
        if (files) XStringList_delete_base((XClass*)files);
        if (sizes) XStringList_delete_base((XClass*)sizes);
        if (types) XStringList_delete_base((XClass*)types);
        if (orderD) XFree_System(orderD);
        if (orderF) XFree_System(orderF);
        return;
    }
    nd = XStringList_size_base((const XContainer*)dirs);
    nf = XStringList_size_base((const XContainer*)files);
    XTreeWidget_clear(view);
    /* 行 0：上级目录（不参与搜索过滤，恒显示；大小列留空）。 */
    xff_addTreeRow(view, ".. (上级目录)", "", "文件夹");
    for (i = 0; i < nd; ++i) {
        int64_t src = orderD ? orderD[i] : i;
        XString* item = (XString*)(void*)XStringList_at_base(dirs, src);
        XString* sz = (XString*)(void*)XStringList_at_base(sizes, src);
        XString* tp = (XString*)(void*)XStringList_at_base(types, src);
        if (!item || !xff_searchAccept(XString_toUtf8(item))) continue;
        xff_addTreeRow(view, XString_toUtf8(item),
                       sz ? XString_toUtf8(sz) : "",
                       tp ? XString_toUtf8(tp) : "");
    }
    for (i = 0; i < nf; ++i) {
        int64_t src = orderF ? orderF[i] : i;
        XString* item = (XString*)(void*)XStringList_at_base(files, src);
        XString* sz =
            (XString*)(void*)XStringList_at_base(sizes, nd + src);
        XString* tp =
            (XString*)(void*)XStringList_at_base(types, nd + src);
        if (!item || !xff_searchAccept(XString_toUtf8(item))) continue;
        xff_addTreeRow(view, XString_toUtf8(item),
                       sz ? XString_toUtf8(sz) : "",
                       tp ? XString_toUtf8(tp) : "");
    }
    if (orderD) XFree_System(orderD);
    if (orderF) XFree_System(orderF);
    XStringList_delete_base((XClass*)dirs);
    XStringList_delete_base((XClass*)files);
    XStringList_delete_base((XClass*)sizes);
    XStringList_delete_base((XClass*)types);
}

/** @brief 目录显示名（basename；根目录等无 basename 时回退全路径）。
 *  目录模式底部「文件夹:」只读框的回显文本（Win10 选文件夹口径：
 *  底部显示所选/当前文件夹名）。buf 恒以 '\0' 起步，无目录时为空串。 */
static void xff_dirDisplayName(const XString* dir, char* buf, size_t cap)
{
    const char* s;
    const char* base;
    if (!buf || cap == 0) return;
    buf[0] = '\0';
    if (!dir || !(s = XString_toUtf8(dir)) || !s[0]) return;
    base = strrchr(s, '/');
    base = base ? base + 1 : s;
    if (!base[0]) base = s; /* 根目录 "/" 无 basename：回退全路径。 */
    snprintf(buf, cap, "%s", base);
}

/** @brief 目录模式底部「文件夹:」只读框同步为当前目录显示名。
 *  换目录（双击进入/上级/后退/地址下拉/导航窗格）后回填，消除
 *  「点选残留名」跨目录误选；文件模式不动（文件名框语义不变）。 */
static void xff_syncDirBox(XFileDialog* dlg)
{
    XLineEdit* edit;
    char buf[256];
    if (!dlg || dlg->m_fileMode != XFileDialog_Directory) return;
    edit = (XLineEdit*)xff_childByName(&dlg->m_base, XFF_NAME_NAMEEDIT);
    if (!edit) return;
    xff_dirDisplayName(dlg->m_directory, buf, sizeof(buf));
    if (buf[0]) XLineEdit_setText(edit, buf);
}

/** @brief 相对路径 → 绝对路径（对标 QDir::absolutePath：相对段相对
 *  进程 CWD 解释；不可得/空串返回 NULL，调用方按原路径降级）。
 *  返回值调用方拥有。 */
static XString* xff_absPath(const XString* p)
{
    XDir d;
    XString* abs;
    if (!p) return NULL;
    XDir_init_2(&d, p);
    abs = XDir_absolutePath(&d);
    XDir_deinit_base((XClass*)&d);
    if (abs && (!XString_toUtf8(abs) || !XString_toUtf8(abs)[0])) {
        XString_delete_base((XClass*)abs);
        return NULL;
    }
    return abs;
}

/** @brief 切换当前目录：更新目录下拉历史、发射 directoryEntered、刷新
 *  列表（对标 QFileDialog 进入目录路径）。pushHist=true 时把切换前
 *  目录压入后退栈（W10b-1，对标 QFileDialog backStack：换目录才压，
 *  首载/同目录重入不产生死项）。 */
static void xff_cdEx(XFileDialog* dlg, const XString* path, bool pushHist)
{
    XComboBox* combo;
    XString* effective = NULL;
    XString* prev = NULL;
    XString* prevAbs = NULL;
    XString* abs = NULL;
    if (!dlg || !path) return;
    /* 目标目录不可枚举（已删除/不可读）时回退家目录，保持 m_directory、
     * 地址下拉与列表实际内容一致；无处可回退则维持原目录不动。 */
    if (!xff_dirUsable(path)) {
        effective = xff_homeDir();
        if (!effective) return;
        path = effective;
    }
    /* 目录状态统一绝对路径（demo 便捷路径传 "." 等相对目录：地址栏/
     * 底部回显/确认结算/上级推导均按全路径语义，getExistingDirectory
     * 回传绝对路径对标 Qt）。绝对化失败（理论不达：目录已验证存在）
     * 按原串降级。 */
    abs = xff_absPath(path);
    if (abs) path = abs;
    prev = dlg->m_directory ? xfiledialog_dupString(dlg->m_directory) : NULL;
    prevAbs = xff_absPath(prev);
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
    /* 成功换目录（绝对化口径下与原目录不同串）才压栈；后退导航不压
     * （防回退乒乓）。 */
    if (pushHist && prev &&
        XString_compare(prevAbs ? prevAbs : prev,
                        (const XString*)path) != 0) {
        XStringList* st = xff_backStackRef();
        if (st)
            XStringList_push_back_utf8(st, XString_toUtf8(prev));
    }
    if (prev) XString_delete_base((XClass*)prev);
    if (prevAbs) XString_delete_base((XClass*)prevAbs);
    XFileDialog_directoryEntered_signal(dlg, path);
    /* 目录模式底部「文件夹:」只读框回填新当前目录名（Win10 口径：
     * 换目录后底部随动，点选残留名不跨目录携带）。 */
    xff_syncDirBox(dlg);
    xff_refresh(dlg);
    xff_updateBackButton(dlg);
    xfiledialog_freeString(&effective);
    xfiledialog_freeString(&abs);
}

/** @brief 切换当前目录（默认压入后退历史栈）。 */
static void xff_cd(XFileDialog* dlg, const XString* path)
{
    xff_cdEx(dlg, path, true);
}

/** @brief 「上级」按钮槽：跳转当前目录的父目录（对标 Win10 地址栏
 *  上级导航；xff_parentOf 对根目录自映射 "/"，重复点击无害）。 */
static void xff_upClicked(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XString* up;
    (void)args;
    if (!dlg) return;
    up = xff_parentOf(dlg->m_directory);
    if (!up) return;
    xff_cd(dlg, up);
    XString_delete_base((XClass*)up);
}

/** @brief 「←」后退槽：弹栈回上一目录（W10b-1，对标 Qt QFileDialog
 *  回退按钮；栈顶与当前目录同串的异常残留先丢弃；回退导航不压栈）。
 *  栈空时按钮已被禁用灰化，本槽不触达。 */
static void xff_backClicked(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XStringList* st;
    int64_t n;
    const XString* top;
    (void)args;
    if (!dlg) return;
    st = xff_backStackRef();
    if (!st) return;
    n = XStringList_size_base((const XContainer*)st);
    while (n > 0) {
        top = (const XString*)(const void*)
            XStringList_at_base(st, n - 1);
        if (dlg->m_directory && top &&
            XString_compare(top, dlg->m_directory) == 0) {
            XStringList_pop_back_base(st);
            --n;
            continue;
        }
        break;
    }
    if (n <= 0) {
        xff_updateBackButton(dlg);
        return;
    }
    top = (const XString*)(const void*)XStringList_at_base(st, n - 1);
    {
        XString* target = top ? XString_create_copy(top) : NULL;
        XStringList_pop_back_base(st);
        if (target) {
            xff_cdEx(dlg, target, false);
            XString_delete_base((XClass*)target);
        }
    }
    xff_updateBackButton(dlg);
}

/** @brief 导航窗格单击槽：按下标解析实际路径并跳转（条目行序由
 *  xff_navEntries 确定性生成，与组装时一致；行越界守卫防条目对应
 *  目录在对话框存续期间消失后误跳）。 */
static void xff_navClicked(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XStringList* names = NULL;
    XStringList* paths = NULL;
    int row;
    if (!dlg || !args) return;
    XVarList_args_2(args, int, r, int, c);
    (void)c;
    row = r;
    if (row < 0) return;
    xff_navEntries(&names, &paths);
    if (!names || !paths) {
        if (names) XStringList_delete_base((XClass*)names);
        if (paths) XStringList_delete_base((XClass*)paths);
        return;
    }
    if (row < (int)XStringList_size_base((const XContainer*)paths)) {
        XString* item =
            (XString*)(void*)XStringList_at_base((const XVector*)paths, row);
        if (item) xff_cd(dlg, item);
    }
    XStringList_delete_base((XClass*)names);
    XStringList_delete_base((XClass*)paths);
}

/** @brief 文件列表行激活公共体：行 0 回上级；目录行进入；文件行置入
 *  文件名编辑并确认。行号按「行 0 + 已接受（搜索过滤）目录 + 已接受
 *  文件」次序映射回条目，映射序=xff_listDir 输出的当前排序态显示序
 *  （与 xff_refresh 装载同一来源）——搜索过滤与表头排序下激活仍命中
 *  可见行。双击与键盘激活（树 Return 经基类 activated (row,col) 信号，
 *  对标 Qt QAbstractItemView 键盘激活路径）共用。 */
static void xff_viewRowActivated(XFileDialog* dlg, int row)
{
    XTreeWidget* view;
    XStringList* patterns;
    XStringList* dirs = NULL;
    XStringList* files = NULL;
    XStringList* sizes = NULL; /* 类型列映射键载体（不读内容，随取随删）。 */
    XStringList* types = NULL;
    bool dirsOnly;
    int64_t ndAcc;
    int64_t* orderD = NULL;
    int64_t* orderF = NULL;
    if (!dlg || row < 0) return;
    /* 激活手势去伪：紧随单击回填的按压伪激活、紧随已处理激活的
     * 双击补发（见 XFF_ACTIVATE_PAIR_MS 处注记）一律忽略，真双击/
     * Return 激活放行。 */
    {
        int64_t now = xff_nowMs();
        if (xff_lastClickMs >= 0 &&
            now - xff_lastClickMs < XFF_ACTIVATE_PAIR_MS)
            return;
        if (xff_lastActivateMs >= 0 &&
            now - xff_lastActivateMs < XFF_ACTIVATE_PAIR_MS)
            return;
        xff_lastActivateMs = now;
    }
    view = (XTreeWidget*)xff_childByName(&dlg->m_base, XFF_NAME_VIEW);
    if (!view) return;
    patterns = dlg->m_selectedNameFilter
        ? xff_filterPatterns(dlg->m_selectedNameFilter) : NULL;
    dirsOnly = (dlg->m_options & (XFileDialogOptions)XFileDialog_ShowDirsOnly)
               ? true : false;
    /* sizes/types 一并请求：类型列显示序映射键取自类型平行表。 */
    xff_listDir(dlg->m_directory, patterns, dirsOnly, &dirs, &files,
                &sizes, &types, &orderD, &orderF);
    XStringList_delete_base((XClass*)patterns);
    if (!dirs || !files) {
        if (dirs) XStringList_delete_base((XClass*)dirs);
        if (files) XStringList_delete_base((XClass*)files);
        if (sizes) XStringList_delete_base((XClass*)sizes);
        if (types) XStringList_delete_base((XClass*)types);
        if (orderD) XFree_System(orderD);
        if (orderF) XFree_System(orderF);
        return;
    }
    if (row == 0) {
        XString* up = xff_parentOf(dlg->m_directory);
        if (up) {
            xff_cd(dlg, up);
            XString_delete_base((XClass*)up);
        }
    } else {
        /* 激活命中按显示序解析（映射与装载同源产出）——排序态下
         * 行号不脱节；映射为 NULL 的列源序即显示序。 */
        int64_t ndAll = XStringList_size_base((const XContainer*)dirs);
        int64_t nfAll = XStringList_size_base((const XContainer*)files);
        ndAcc = xff_countAccepted(dirs);
        if (row <= ndAcc) {
            XString* item = xff_nthAccepted(dirs, row - 1, orderD, ndAll);
            if (item) {
                XString* sub = xff_joinPath(dlg->m_directory,
                                            XString_toUtf8(item));
                if (sub) {
                    xff_cd(dlg, sub);
                    XString_delete_base((XClass*)sub);
                }
                XString_delete_base((XClass*)item);
            }
        } else {
            XString* item = xff_nthAccepted(files, row - 1 - ndAcc,
                                            orderF, nfAll);
            if (item) {
                XLineEdit* nameEdit = (XLineEdit*)xff_childByName(
                    &dlg->m_base, XFF_NAME_NAMEEDIT);
                if (nameEdit)
                    XLineEdit_setText(nameEdit, XString_toUtf8(item));
                /* 文件双击即确认（对标 QFileDialog 双击文件 accept）。 */
                xff_acceptSlot((XObject*)dlg, NULL);
                XString_delete_base((XClass*)item);
            }
        }
        if (orderD) XFree_System(orderD);
        if (orderF) XFree_System(orderF);
    }
    XStringList_delete_base((XClass*)dirs);
    XStringList_delete_base((XClass*)files);
    if (sizes) XStringList_delete_base((XClass*)sizes);
    if (types) XStringList_delete_base((XClass*)types);
}

/** @brief 基类单击时间戳锚点：基类按压路径在同一栈内先发 clicked
 *  后发 activated（微秒级先后），此槽只落点时间供激活去伪配对，
 *  不做任何动作（选中/回填仍由树 itemClicked 槽承担）。 */
static void xff_viewBaseClicked(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    (void)args;
    if (!dlg) return;
    xff_lastClickMs = xff_nowMs();
}

/** @brief 文件列表双击槽（XTreeWidget itemDoubleClicked 载荷=行号）。 */
static void xff_viewDoubleClicked(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    if (!dlg || !args) return;
    XVarList_args_1(args, int, row);
    /* 双击事件本身即真激活手势：解除按压配对窗，防个别平台把第二击
     * 仍按普通按压发射（itemClicked 刷新配对锚点）误伤真双击。 */
    xff_lastClickMs = -1;
    if (row >= 0) xff_viewRowActivated(dlg, row);
}

/** @brief 文件列表键盘激活槽（XAbstractItemView activated 载荷=
 *  行,列；树 Return 经基类路径发射，此前无监听致键盘中激活静默——
 *  :121 活体 i4 组复现。对标 Qt QFileDialog 的 Return 进目录/选文件）。 */
static void xff_viewActivated(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    if (!dlg || !args) return;
    XVarList_args_2(args, int, r, int, c);
    (void)c;
    if (r >= 0) xff_viewRowActivated(dlg, r);
}

/** @brief 文件列表单击槽：命中文件/目录行时把该条目全路径置入底部
 *  编辑/只读框（目录模式 Win10 FOS_PICKFOLDERS 口径回显所选文件夹
 *  全路径、文件模式回显所选文件全路径，accept 侧按绝对路径直接
 *  结算；行 0 为 ".. (上级目录)" 行不回填）。名称读树行条目列 0
 *  文本——搜索过滤下与显示行严格一致。落点时间供 xff_viewRowActivated
 *  做按压伪激活配对（同一次按压栈内基类 clicked 之后紧跟 activated）。
 * @note  对标 QFileDialog 单击只选中不激活：导航/确认仅由双击与
 *  Return 触发（按压伪激活在激活槽内按时间窗过滤）。 */
static void xff_viewClicked(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    XTreeWidget* view;
    XTreeWidgetItem* item;
    const char* name;
    if (!dlg || !args) return;
    xff_lastClickMs = xff_nowMs(); /* 按压伪激活配对锚点（任意行）。 */
    XVarList_args_1(args, int, row); /* itemClicked 载荷首参恒行号。 */
    if (row <= 0) return; /* 行 0 为 ".. (上级目录)" 行，非文件。 */
    view = (XTreeWidget*)xff_childByName(&dlg->m_base, XFF_NAME_VIEW);
    if (!view) return;
    item = XTreeWidget_topLevelItem(view, row);
    name = item ? XTreeWidgetItem_textAt_2(item, 0) : NULL;
    if (name && name[0] && dlg->m_directory) {
        XLineEdit* nameEdit = (XLineEdit*)xff_childByName(
            &dlg->m_base, XFF_NAME_NAMEEDIT);
        if (nameEdit) {
            XString* full = xff_joinPath(dlg->m_directory, name);
            if (full) {
                XLineEdit_setText(nameEdit, XString_toUtf8(full));
                XString_delete_base((XClass*)full);
            }
        }
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

/* ---------- 表头段几何镜像 + 排序接线（W10 表头段点击排序） ---------- */

/** @brief 列带几何：显式列宽（XTreeView_columnWidth >0）优先、其余列
 *  均摊剩余宽——与 XTreeWidget 自绘表头/命中同一算法（xtw_columnSpan
 *  为库内 static，镜像承载；常量口径见 XFF_TREE_HEADER_H 注记）。 */
static void xff_treeColumnSpan(const XTreeWidget* view, int column,
                               int width, int* outX, int* outW)
{
    int c;
    int fixedSum = 0;
    int autoCount = 0;
    int autoShare = 0;
    int x = 0;
    if (outX) *outX = 0;
    if (outW) *outW = 0;
    if (!view || column < 0 || width <= 0) return;
    for (c = 0; c < XTreeWidget_columnCount(view); ++c) {
        int w = XTreeView_columnWidth(&view->m_base, c);
        if (w > 0) fixedSum += w;
        else ++autoCount;
    }
    autoShare = (autoCount > 0 && width > fixedSum)
                    ? (width - fixedSum) / autoCount
                    : 0;
    for (c = 0; c <= column && x < width; ++c) {
        int w = XTreeView_columnWidth(&view->m_base, c);
        if (w <= 0) w = autoShare;
        if (c == column) {
            if (outX) *outX = x;
            if (outW) *outW = w;
            return;
        }
        x += w;
    }
}

/** @brief 表头带内段命中反查：命中段号；分隔线 ±3px 拖宽热区返回 -1
 *  （让位 XTreeWidget 自带拖宽手势——段点击与分隔线拖拽语义互斥，
 *  与库侧 qheaderview 边界同款）。零宽列跳过（与绘制同口径）。 */
static int xff_treeSectionAt(const XTreeWidget* view, int x)
{
    int c;
    int cols;
    int viewW;
    if (!view || x < 0) return -1;
    viewW = XWidget_width((XWidget*)view);
    cols = XTreeWidget_columnCount(view);
    for (c = 0; c < cols; ++c) {
        int colX = 0;
        int colW = 0;
        int sep;
        xff_treeColumnSpan(view, c, viewW, &colX, &colW);
        if (colW <= 0) continue;
        if (x < colX || x >= colX + colW) continue;
        sep = colX + colW - 1;
        if (x >= sep - XFF_TREE_HANDLE_HIT && x <= sep + XFF_TREE_HANDLE_HIT)
            return -1;
        return c;
    }
    return -1;
}

/** @brief 排序指示器落表头标签：当前排序列标签尾随方向符（"^"=升序、
 *  "v"=降序），其余列还原基础标签。内置字体 cmap 仅覆盖 ASCII 与 CJK
 *  整形区，U+25B2/U+25BC 三角形无字形（XTreeWidget.c xtw_elideText 以
 *  "..." 代 U+2026 同一字体覆盖约束），故以 ASCII 符号承载方向；
 *  setHeaderLabels 内部 XWidget_update 驱动表头即时重绘。 */
static void xff_applySortIndicator(XTreeWidget* view)
{
    static const char* const kBase[3] = { "名称", "大小", "类型" };
    const char* labels[3];
    char bufs[3][32];
    int c;
    if (!view) return;
    for (c = 0; c < 3; ++c) {
        if (c == xff_sortColumn)
            snprintf(bufs[c], sizeof(bufs[c]), "%s %s", kBase[c],
                     xff_sortOrder == 1 ? "v" : "^");
        else
            snprintf(bufs[c], sizeof(bufs[c]), "%s", kBase[c]);
        labels[c] = bufs[c];
    }
    XTreeWidget_setHeaderLabels(view, labels, 3);
}

/** @brief 表头段点击槽（经 VXFileDialog_eventFilter 拦截承载）：同段
 *  升降交替、新段先升序（对标 Qt QHeaderView::flipSortIndicator——
 *  指示段相同则翻向、不同则重置升序）；随后更新指示器并按新排序态
 *  重列（xff_listDir 源旗标 + 名称列显示序映射，目录恒在文件前）。
 *  越界段（表头三列外）忽略。 */
static void xff_headerClicked(XFileDialog* dlg, int section)
{
    XTreeWidget* view;
    if (!dlg || section < 0 || section > 2) return;
    if (xff_sortColumn == section)
        xff_sortOrder = (xff_sortOrder == 1) ? 0 : 1;
    else {
        xff_sortColumn = section;
        xff_sortOrder = 0;
    }
    view = (XTreeWidget*)xff_childByName(&dlg->m_base, XFF_NAME_VIEW);
    xff_applySortIndicator(view);
    xff_refresh(dlg);
}

/** @brief 搜索框文本变化槽（W10b-4）：记录搜索前缀并重列——刷新装载
 *  与双击映射共用 xff_searchAccept 判定，过滤后行号保持一致。 */
static void xff_searchChanged(XObject* receiver, XVarList* args)
{
    XFileDialog* dlg = (XFileDialog*)receiver;
    if (!dlg || !args) return;
    XVarList_args_1(args, const char*, text);
    if (text && text[0]) {
        size_t i;
        for (i = 0; text[i] && i + 1 < sizeof(xff_searchPrefix); ++i)
            xff_searchPrefix[i] = text[i];
        xff_searchPrefix[i] = '\0';
    } else {
        xff_searchPrefix[0] = '\0'; /* 清空恢复全列。 */
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
    /* 目录模式（对标 Win10 FOS_PICKFOLDERS / Qt getExistingDirectory）：
     * 底部「文件夹:」只读框点选了子目录名时确认取该子目录（Qt
     * selectedFiles 首项口径）；显示为当前目录名（未点选）时取当前
     * 目录。子目录已不存在则回落当前目录；Esc/取消回传空语义不变。 */
    if (dlg->m_fileMode == XFileDialog_Directory) {
        XLineEdit* nameEdit = (XLineEdit*)xff_childByName(
            &dlg->m_base, XFF_NAME_NAMEEDIT);
        const char* name = nameEdit ? XLineEdit_text(nameEdit) : NULL;
        char cur[256];
        XString* chosen = NULL;
        xff_dirDisplayName(dlg->m_directory, cur, sizeof(cur));
        if (dlg->m_selectedFiles)
            XStringList_clear_base((XContainer*)dlg->m_selectedFiles);
        /* 底部框两类合法载荷（xff_viewClicked 回显口径）：当前目录
         * 显示名（未点选，= cur）或条目全路径（绝对路径，单击回填）。
         * 绝对路径直接结算（可存在才采纳）；相对名按当前目录拼接
         * （手输/历史行为兼容）。 */
        if (name && name[0] && strcmp(name, cur) != 0 && dlg->m_directory) {
            XString* sel = (name[0] == '/')
                ? XString_create_utf8(name)
                : xff_joinPath(dlg->m_directory, name);
            if (sel) {
                if (xff_dirUsable(sel))
                    chosen = sel;
                else
                    XString_delete_base((XClass*)sel);
            }
        }
        if (!chosen && dlg->m_directory)
            chosen = xfiledialog_dupString(dlg->m_directory);
        if (chosen) {
            /* 对标 QFileDialog::accept：fileSelected 随确认发射（目录
             * 模式载荷为选定目录）。 */
            XFileDialog_selectFile(dlg, chosen);
            XFileDialog_fileSelected_signal(dlg, chosen);
            XString_delete_base((XClass*)chosen);
        }
        XDialog_accept(&dlg->m_base);
        return;
    }
    nameEdit = (XLineEdit*)xff_childByName(&dlg->m_base, XFF_NAME_NAMEEDIT);
    name = nameEdit ? XLineEdit_text(nameEdit) : NULL;
    if (!name || !name[0]) return; /* 无文件名：忽略确认（对标 OK 无效态）。 */
    {
        XString* path;
        /* 底部框绝对路径（xff_viewClicked 单击回显文件全路径/手输绝
         * 对路径）直接结算；相对名按当前目录拼接。 */
        if (name[0] == '/') {
            path = XString_create_utf8(name);
        } else if (dlg->m_acceptMode == XFileDialog_AcceptSave &&
                   dlg->m_defaultSuffix &&
                   !strchr(name, '.')) {
            /* 保存模式默认后缀：名称无 '.' 时补 defaultSuffix（对标
             * QFileDialog defaultSuffix 语义；仅相对名补，绝对路径
             * 不改写）。 */
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
    XBoxLayout* navRow;   /**< 地址行（后退 + 上级按钮 + 标签 + 目录下拉 + 搜索框）。 */
    XBoxLayout* bodyRow;  /**< 主体行（导航窗格 + 文件列表树）。 */
    XBoxLayout* filterRow;/**< 过滤器行（标签 + 下拉）。 */
    XBoxLayout* nameRow;  /**< 文件名行（标签 + 编辑）。 */
    XBoxLayout* bar;      /**< 按钮行。 */
    XAbstractItemModel* navModel; /**< 导航窗格模型（视图不拥有，须自删）。
                                       文件列表树（XTreeWidget）用内建
                                       桥模型，无外部 model 需回收。 */
} XFFLayouts;

/** @brief 组装文件对话框并填充起始目录列表（对标 Qt 非原生
 *  QFileDialog 的 ui 组装；子控件经 objectName 标识）。Win10 风格：
 *  地址行（←后退 + 上级 + 目录下拉 + 搜索框）→ 主体行（左导航窗格 +
 *  文件列表树）→ 文件名行 → 过滤器行 → 按钮行。
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
    XTreeWidget* view;
    if (!ls) return NULL;
    ls->root = ls->navRow = ls->bodyRow = NULL;
    ls->filterRow = ls->nameRow = ls->bar = NULL;
    ls->navModel = NULL;
    /* 跨对话框浏览状态（后退栈/搜索前缀）先清空再装配。 */
    xff_browsingStateReset();
    /* 子控件形态：见 XInputDialog 同款注记（单原生窗口模型下窗口
     * 形态首帧 flush 不可靠）。 */
    dlg = XFileDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, parent, 0);
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
    /* 起始目录不可枚举（不存在/不可读，含 currentPath 失败为空）时
     * 回退家目录：地址下拉、m_directory 与列表实际内容保持一致
     * （与 xff_listDir 的枚举回退同一策略；家目录不可用则维持原状）。 */
    if (!dlg->m_directory || !xff_dirUsable(dlg->m_directory)) {
        XString* home = xff_homeDir();
        if (home) {
            XFileDialog_setDirectory(dlg, home);
            XString_delete_base((XClass*)home);
        }
    }
    /* 起始目录同样绝对化（demo 便捷路径 "."：首屏地址栏/底部回显/
     * 单击回填/确认结算全路径语义，与 xff_cdEx 同一口径）。 */
    {
        XString* abs0 = xff_absPath(dlg->m_directory);
        if (abs0) {
            XFileDialog_setDirectory(dlg, abs0);
            XString_delete_base((XClass*)abs0);
        }
    }
    ls->root = XBoxLayout_create(XBoxLayoutDirection_TopToBottom,
                                 (XWidget*)dlg);
    if (!ls->root) {
        XFileDialog_delete_base((XClass*)dlg);
        return NULL;
    }
    /* Win10 观感：面板白底 + 输入白底（控件级 palette 覆写，见
     * xff_setRoleColor；标题栏带仍由 XDialog 按其自有口径绘制）。 */
    xff_setRoleColor((XWidget*)dlg, XPaletteColorRole_Window,
                     XFF_WIN10_WHITE);
    xff_setRoleColor((XWidget*)dlg, XPaletteColorRole_Base,
                     XFF_WIN10_WHITE);
    /* 顶部边距按标题栏行高让位（XFF_TB_CONTENT_TOP=28，与 XDialog
     * 标题栏带同键）：子控件形态 + 已设标题时 XDialog 会在面板顶部
     * 绘制标题栏行，地址行若仍按 12 起排会叠上标题带、遮住标题字
     * （活体实证见条目 R1）；无标题保持原 12 边距布局。 */
    XLayout_setContentsMargins((XLayout*)ls->root, 12,
                               (caption && XString_toUtf8(caption) &&
                                XString_toUtf8(caption)[0])
                                   ? XFF_TB_CONTENT_TOP : 12,
                               12, 12);
    XLayout_setSpacing((XLayout*)ls->root, 8);
    /* 行 1：←后退 + 上级 + 目录下拉 + 搜索框（Win10 地址行：后退/上级
     * 导航钮 + 历史地址栏 + 右端搜索，对标 QFileDialog lookInCombo +
     * backButton/upButton 组合 + Win10 搜索框）。Win10 地址行无「目录:」
     * 文字标签，故不再插入（且标签无显式最小宽时在行布局中零宽不可
     * 见）；地址下拉白底（Win10 地址栏白底口径）。 */
    {
        XPushButton* back = XPushButton_create((XWidget*)dlg, 0);
        XPushButton* up = XPushButton_create((XWidget*)dlg, 0);
        XComboBox* combo;
        XLineEdit* search = XLineEdit_create((XWidget*)dlg, 0);
        combo = XComboBox_create((XWidget*)dlg, 0);
        if (combo) {
            xff_setName((XObject*)combo, XFF_NAME_DIRCOMBO);
            XWidget_setMinimumSize((XWidget*)combo, 280, 26);
            xff_setRoleColor((XWidget*)combo, XPaletteColorRole_Base,
                             XFF_WIN10_WHITE);
            /* XComboBox 基线焦点策略 NoFocus（类内未设置）——地址栏不可
             * Tab 达。对标 Qt QComboBox 默认 WheelFocus（Tab 可达）：
             * 置 StrongFocus 纳入对话框 Tab 链（Win10 地址栏可达口径）。 */
            XWidget_setFocusPolicy((XWidget*)combo,
                                   XWidgetFocusPolicy_StrongFocus);
        }
        if (up) {
            XAbstractButton_setText_2((XAbstractButton*)up, "上级");
            XWidget_setMinimumSize((XWidget*)up, 64, 26);
            xff_setName((XObject*)up, XFF_NAME_UPBTN);
            XWidget_setVisible((XWidget*)up, true);
            /* 导航钮退出 autoDefault 候选（对标 Qt QFileDialog 的
             * QToolButton 性质）：XDialog 默认按钮遍历按先序收首个
             * m_autoDefault!=Off 的可见按钮，Win10 改造加入的上级/
             * 后退钮排在确定之前——不退出则对话框初始焦点落到上级
             * 钮、Return 派发点上级（后退可用时点后退=回退导航），
             * 文件名框内 Return 无法确认文件（:121 活体 g3/h4 复现）。 */
            up->m_autoDefault = XPushButtonAutoDefault_Off;
        }
        if (back) {
            XAbstractButton_setText_2((XAbstractButton*)back, "←");
            XWidget_setMinimumSize((XWidget*)back, 36, 26);
            xff_setName((XObject*)back, XFF_NAME_BACKBTN);
            XWidget_setVisible((XWidget*)back, true);
            /* 首载后退栈为空：初始即禁用灰化（xff_cdEx 换目录后同步）。 */
            XWidget_setEnabled((XWidget*)back, false);
            /* 同上级钮：后退可用时不得劫持默认按钮遍历/Return 派发。 */
            back->m_autoDefault = XPushButtonAutoDefault_Off;
        }
        if (search) {
            xff_setName((XObject*)search, XFF_NAME_SEARCH);
            XWidget_setMinimumSize((XWidget*)search, 130, 24);
            XWidget_setFixedWidth((XWidget*)search, 150);
            XLineEdit_setPlaceholderText(search, "搜索");
            /* Win10 搜索框白底（主题 Base #FFFFE0 偏黄，控件级覆写）。 */
            xff_setRoleColor((XWidget*)search, XPaletteColorRole_Base,
                             XFF_WIN10_WHITE);
        }
        ls->navRow = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        if (ls->navRow) {
            if (back) XBoxLayout_addWidget(ls->navRow, (XWidget*)back);
            if (up) XBoxLayout_addWidget(ls->navRow, (XWidget*)up);
            if (combo)
                XBoxLayout_addWidgetEx(ls->navRow, (XWidget*)combo, 1, 0);
            if (search)
                XBoxLayout_addWidget(ls->navRow, (XWidget*)search);
            XBoxLayout_addLayout(ls->root, (XLayout*)ls->navRow);
        }
    }
    /* 行 2：主体行（左导航窗格 + 文件列表树；Win10 详情视图三列
     * 名称/大小/类型 + 表头，对标 detailMode。目录行大小列「文件夹」。
     * 导航窗格固定宽 ~120px，条目为用户可读名，数据侧路径由
     * xff_navEntries 映射，单击即跳转——CWD 无子目录/无匹配文件
     * 时用户仍可经此去桌面/文档/下载等常驻目录）。 */
    view = XTreeWidget_create((XWidget*)dlg, 0);
    if (view) {
        static const char* const kHeaders[3] = { "名称", "大小", "类型" };
        xff_setName((XObject*)view, XFF_NAME_VIEW);
        XWidget_setMinimumSize((XWidget*)view, 360, 220);
        /* 多列详情 + 表头（页5 树双列先例同款：setColumnCount +
         * setHeaderLabels；内建桥模型随条目同步，无外部 model）。 */
        XTreeWidget_setColumnCount(view, 3);
        XTreeWidget_setHeaderLabels(view, kHeaders, 3);
    }
    {
        XListView* nav = XListView_create((XWidget*)dlg, 0);
        if (nav) {
            XAbstractItemModel* navModel = XAbstractItemModel_create();
            xff_setName((XObject*)nav, XFF_NAME_NAV);
            XWidget_setMinimumSize((XWidget*)nav, 120, 220);
            XWidget_setFixedWidth((XWidget*)nav, 120);
            if (navModel) {
                XStringList* navNames = NULL;
                XStringList* navPaths = NULL;
                /* 对标 Qt：视图不拥有 model，析构归调用方。 */
                XAbstractItemView_setModel((XAbstractItemView*)nav, navModel);
                ls->navModel = navModel;
                xff_navEntries(&navNames, &navPaths);
                if (navNames && navPaths) {
                    int64_t i, n = XStringList_size_base(
                        (const XContainer*)navNames);
                    XAbstractItemModel_setDimension(navModel, (int)n, 1);
                    for (i = 0; i < n; ++i) {
                        XString* nm = (XString*)(void*)
                            XStringList_at_base((const XVector*)navNames, i);
                        if (nm)
                            XAbstractItemModel_setData_2(
                                navModel, (int)i, 0, XString_toUtf8(nm));
                    }
                }
                if (navNames) XStringList_delete_base((XClass*)navNames);
                if (navPaths) XStringList_delete_base((XClass*)navPaths);
            }
            XWidget_setVisible((XWidget*)nav, true);
        }
        ls->bodyRow = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        if (ls->bodyRow) {
            if (nav) XBoxLayout_addWidget(ls->bodyRow, (XWidget*)nav);
            if (view)
                XBoxLayout_addWidgetEx(ls->bodyRow, (XWidget*)view, 1, 0);
            /* 主体行伸展因子 1：垂直多余空间全部归文件列表（Win10 口
             * 径：列表吸高、底部两行保持紧凑；否则空间均摊、文件名框
             * 被撑到 ~46px 高）。 */
            XBoxLayout_addLayoutEx(ls->root, (XLayout*)ls->bodyRow, 1);
        }
    }
    /* 行 3：文件名编辑。文件模式「文件名(N):」可编辑（Win10 助记样式，
     * 与下行「文件类型(T):」等宽左对齐）；目录模式转「文件夹:」只读
     * 回显当前选中目录（对标 Win10 FOS_PICKFOLDERS 底部所选文件夹名；
     * 点选列表条目由 xff_viewClicked 回填名，换目录由 xff_syncDirBox
     * 随动），不再整行隐藏——Win10 选文件夹时底部恒有所选文件夹名。 */
    {
        XLabel* lb = XLabel_create((XWidget*)dlg, 0);
        XLineEdit* edit;
        bool dirMode = (fileMode == XFileDialog_Directory) ? true : false;
        XLabel_setText_2(lb, dirMode ? "文件夹:" : "文件名(N):");
        /* 标签显式最小宽（与下行「文件类型(T):」等宽，Win10 底部两行
         * 左对齐口径）：无显式最小宽时行布局按零宽摆放、标签不可见。 */
        XWidget_setMinimumSize((XWidget*)lb, XFF_LABEL_COL_WIDTH, 20);
        edit = XLineEdit_create((XWidget*)dlg, 0);
        if (edit) {
            xff_setName((XObject*)edit, XFF_NAME_NAMEEDIT);
            XWidget_setMinimumSize((XWidget*)edit, 240, 24);
            /* Win10 文件名框白底（主题 Base 偏黄，控件级覆写）。 */
            xff_setRoleColor((XWidget*)edit, XPaletteColorRole_Base,
                             XFF_WIN10_WHITE);
            if (dirMode) {
                char dbuf[256];
                XLineEdit_setReadOnly(edit, true);
                xff_dirDisplayName(dlg->m_directory, dbuf, sizeof(dbuf));
                if (dbuf[0]) XLineEdit_setText(edit, dbuf);
            } else if (prefillName) {
                XLineEdit_setText(edit, prefillName);
            }
        }
        ls->nameRow = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        if (ls->nameRow) {
            if (lb) XBoxLayout_addWidget(ls->nameRow, (XWidget*)lb);
            if (edit) XBoxLayout_addWidgetEx(ls->nameRow, (XWidget*)edit, 1, 0);
            XBoxLayout_addLayout(ls->root, (XLayout*)ls->nameRow);
        }
    }
    /* 行 4：名称过滤器下拉（解析自 filter 串，对标 FileType 组合；
     * Win10 助记样式「文件类型(T):」）。 */
    if (filter) {
        XLabel* lb = XLabel_create((XWidget*)dlg, 0);
        XComboBox* combo;
        XLabel_setText_2(lb, "文件类型(T):");
        /* 与上行「文件名(N):」标签等宽（Win10 底部两行左对齐口径）；
         * 无显式最小宽时行布局按零宽摆放、标签不可见。 */
        XWidget_setMinimumSize((XWidget*)lb, XFF_LABEL_COL_WIDTH, 20);
        combo = XComboBox_create((XWidget*)dlg, 0);
        if (combo) {
            xff_setName((XObject*)combo, XFF_NAME_FILTERCOMBO);
            XWidget_setMinimumSize((XWidget*)combo, 300, 26);
            /* Win10 文件类型下拉白底（控件级覆写，同地址栏）。 */
            xff_setRoleColor((XWidget*)combo, XPaletteColorRole_Base,
                             XFF_WIN10_WHITE);
            /* 同地址栏：NoFocus 基线改 StrongFocus，Tab 链可达。 */
            XWidget_setFocusPolicy((XWidget*)combo,
                                   XWidgetFocusPolicy_StrongFocus);
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
                /* 目录模式确认钮「选择文件夹」（对标 Win10
                 * FOS_PICKFOLDERS 与 Qt getExistingDirectory 的 Accept
                 * 标签口径）；应用 setLabelText(Accept) 显式设置优先。 */
                const char* okText = "确定";
                XString* acceptLabel =
                    dlg->m_labelTexts[XFileDialogDialogLabel_Accept];
                if (acceptLabel && XString_toUtf8(acceptLabel) &&
                    XString_toUtf8(acceptLabel)[0])
                    okText = XString_toUtf8(acceptLabel);
                else if (fileMode == XFileDialog_Directory)
                    okText = "选择文件夹";
                XAbstractButton_setText_2((XAbstractButton*)ok, okText);
                XWidget_setMinimumSize((XWidget*)ok, 80, 28);
                xff_setName((XObject*)ok, XFF_NAME_OK);
                /* 确定钮 = 显式默认按钮（对标 QFileDialog 的 Accept 钮
                 * setDefault(true)）：对话框 Return/初始焦点分派不再依
                 * 赖「先序首个 autoDefault 按钮」的插入序偶然性。 */
                XPushButton_setDefault(ok, true);
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
            /* 对标 Qt 模态对话框内 Tab 焦点链不越出对话框的窗口级语
               义（详见 XDialogButtonBox.c xdb_relayout 同款注记）：
               显式 Tab 环链把确定/取消围成子树内闭环（夜间台账
               #23/#24 同根防范）。 */
            if (ok && cancel) {
                XWidget_setTabOrder((XWidget*)ok, (XWidget*)cancel);
                XWidget_setTabOrder((XWidget*)cancel, (XWidget*)ok);
            }
            XBoxLayout_addLayout(ls->root, (XLayout*)ls->bar);
        }
    }
    /* 全链显式 Tab 环（对标 Qt QFileDialog：各交互控件均可 Tab 达，
     * 顺序=Win10 视觉序 后退→上级→地址栏→搜索→导航窗格→文件列表→
     * 文件名框→文件类型→确定→取消→回后退）。组合框基线 NoFocus 已
     * 上调 StrongFocus，setTabOrder 要求双端非 NoFocus（XWidget.c
     * :4384 门禁）故置于策略设置之后。单端为 NULL（裁剪/创建失败）
     * 时 setTabOrder 自身返回，链退化文档序。 */
    {
        XWidget* back = xff_childByName(&dlg->m_base, XFF_NAME_BACKBTN);
        XWidget* up = xff_childByName(&dlg->m_base, XFF_NAME_UPBTN);
        XWidget* dirCombo = xff_childByName(&dlg->m_base, XFF_NAME_DIRCOMBO);
        XWidget* search = xff_childByName(&dlg->m_base, XFF_NAME_SEARCH);
        XWidget* nav = xff_childByName(&dlg->m_base, XFF_NAME_NAV);
        XWidget* fileList = xff_childByName(&dlg->m_base, XFF_NAME_VIEW);
        XWidget* nameEdit = xff_childByName(&dlg->m_base, XFF_NAME_NAMEEDIT);
        XWidget* filterCombo =
            xff_childByName(&dlg->m_base, XFF_NAME_FILTERCOMBO);
        XWidget* ok = xff_childByName(&dlg->m_base, XFF_NAME_OK);
        XWidget* cancel = xff_childByName(&dlg->m_base, XFF_NAME_CANCEL);
        XWidget_setTabOrder(back, up);
        XWidget_setTabOrder(up, dirCombo);
        XWidget_setTabOrder(dirCombo, search);
        XWidget_setTabOrder(search, nav);
        XWidget_setTabOrder(nav, fileList);
        XWidget_setTabOrder(fileList, nameEdit);
        XWidget_setTabOrder(nameEdit, filterCombo);
        XWidget_setTabOrder(filterCombo, ok);
        XWidget_setTabOrder(cancel, back);
    }
    /* 信号挂接（view/combo/导航窗格/上级/后退/搜索槽；接收者均为
     * 对话框，对标 Qt 信号连接）。文件树用自身 itemClicked/
     * itemDoubleClicked（载荷首参恒行号；XTreeView 不发射
     * XAbstractItemView clicked 族）。表头段点击排序：XTreeWidget
     * 自绘表头无 sectionClicked 信号源且指针事件绕过过滤器链，以
     * 实例级虚表接管承载（段点击在 xff_treePressHook 内翻转排序，
     * 分隔线热区放行自带拖宽手势）。 */
    if (view) {
        xff_sortViewInstall(dlg, view);
        XObject_connect_1((XObject*)view,
                          (size_t)XTreeWidget_itemDoubleClicked_signal(NULL, 0),
                          (XObject*)dlg, xff_viewDoubleClicked,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)view,
                          (size_t)XTreeWidget_itemClicked_signal(NULL, 0),
                          (XObject*)dlg, xff_viewClicked,
                          XConnectionType_Direct);
        /* 基类按压配对锚点：只记时间不动作（见 xff_viewBaseClicked）。 */
        XObject_connect_1((XObject*)view,
                          (size_t)XAbstractItemView_clicked_signal,
                          (XObject*)dlg, xff_viewBaseClicked,
                          XConnectionType_Direct);
        /* 键盘激活（Return/Enter）：基类 keyPress 在当前行上发射
         * activated(row,col) 并 accept（XAbstractItemView.c:1854-1873）
         * ——事件被消费、不再沿父链到对话框默认按钮路径，故必须在此
         * 接线才能键盘进目录/选文件（对标 Qt activated→QFileDialog）。 */
        XObject_connect_1((XObject*)view,
                          (size_t)XAbstractItemView_activated_signal(NULL, 0, 0),
                          (XObject*)dlg, xff_viewActivated,
                          XConnectionType_Direct);
    }
    {
        XListView* nav = (XListView*)xff_childByName(&dlg->m_base,
                                                     XFF_NAME_NAV);
        if (nav) {
            XObject_connect_1((XObject*)nav,
                              (size_t)XAbstractItemView_clicked_signal,
                              (XObject*)dlg, xff_navClicked,
                              XConnectionType_Direct);
            /* 导航窗格 Return 激活：同 clicked 槽（载荷同为 行,列，
             * xff_navClicked 已按 args_2 解析），键达侧栏跳转对标 Qt。 */
            XObject_connect_1((XObject*)nav,
                              (size_t)XAbstractItemView_activated_signal(NULL, 0, 0),
                              (XObject*)dlg, xff_navClicked,
                              XConnectionType_Direct);
        }
    }
    {
        XPushButton* up = (XPushButton*)xff_childByName(&dlg->m_base,
                                                        XFF_NAME_UPBTN);
        if (up)
            XObject_connect_1((XObject*)up,
                              (size_t)XAbstractButton_clicked_signal,
                              (XObject*)dlg, xff_upClicked,
                              XConnectionType_Direct);
    }
    {
        XPushButton* back = (XPushButton*)xff_childByName(&dlg->m_base,
                                                          XFF_NAME_BACKBTN);
        if (back)
            XObject_connect_1((XObject*)back,
                              (size_t)XAbstractButton_clicked_signal,
                              (XObject*)dlg, xff_backClicked,
                              XConnectionType_Direct);
    }
    {
        XLineEdit* search = (XLineEdit*)xff_childByName(&dlg->m_base,
                                                        XFF_NAME_SEARCH);
        if (search)
            XObject_connect_1((XObject*)search,
                              (size_t)XLineEdit_textChanged_signal,
                              (XObject*)dlg, xff_searchChanged,
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

/** @brief 收尾：先回收树实例虚表接管（须在树析构前还原指向），
 *  再删布局（不随控件析构）与对话框；浏览会话状态（后退栈/搜索
 *  前缀/排序态）随收尾清空，防跨对话框残留。 */
static void xff_teardown(XFileDialog* dlg, XFFLayouts* ls)
{
#if XFILE_ON && XDIR_ON
    xff_sortViewUninstall();
#endif
    if (ls->root) XLayout_delete_base((XLayout*)ls->root);
    if (ls->navRow) XLayout_delete_base((XLayout*)ls->navRow);
    if (ls->bodyRow) XLayout_delete_base((XLayout*)ls->bodyRow);
    if (ls->filterRow) XLayout_delete_base((XLayout*)ls->filterRow);
    if (ls->nameRow) XLayout_delete_base((XLayout*)ls->nameRow);
    if (ls->bar) XLayout_delete_base((XLayout*)ls->bar);
    if (dlg) XFileDialog_delete_base((XClass*)dlg);
    if (ls->navModel) XAbstractItemModel_delete_base((XClass*)ls->navModel);
    xff_browsingStateReset();
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
    XLineEdit* nameEdit;
    if (!dlg) return false;
    /* 宽度容纳地址行（后退36+上级64+标签+目录下拉280+搜索150）与
     * 主体行（导航窗格 120 + 文件树 min 360）+ 页边距；680 与 Win10
     * 文件对话框默认幅面同量级。 */
    XWidget_resize((XWidget*)dlg, 680, 460);
    xff_centerOnScreen((XWidget*)dlg);
    /* 初始焦点对标 QFileDialogPrivate::initialFocus：文件名模式聚焦
     * 文件名编辑框（打开即键入，Qt 新建/打开口径），目录模式维持
     * exec 内 grabInitialFocus 的默认钮（「选择文件夹」）。exec 里的
     * grabInitialFocus 见焦点已在对话框子树内则不再抢占
     * （XDialog.c dialog_containsFocus 门禁）。 */
    if (dlg->m_fileMode != XFileDialog_Directory) {
        nameEdit = (XLineEdit*)xff_childByName(&dlg->m_base,
                                               XFF_NAME_NAMEEDIT);
        if (nameEdit)
            XWidget_setFocusReason((XWidget*)nameEdit, XFocusReason_Other);
    }
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
        /* 对标 Qt getExistingDirectory（Win10 FOS_PICKFOLDERS）：目录
         * 模式 + ShowDirsOnly（列表仅文件夹）+ 底部「文件夹:」只读框
         * + 「选择文件夹」确认钮；确认结算所选子目录或当前目录到
         * selectedFiles（xff_acceptSlot），Esc/取消回传空语义不变。 */
        XFileDialog* dlg = NULL;
        XFFLayouts ls;
        bool accepted = xff_runDialog(parent, caption, dir, NULL,
                                      (XFileDialogOptions)XFileDialog_ShowDirsOnly,
                                      XFileDialog_Directory,
                                      XFileDialog_AcceptOpen, NULL,
                                      &dlg, &ls);
        XString* result;
        if (accepted && dlg) {
            /* Qt 口径：selectedFiles 首项优先，空回落当前目录。 */
            result = xff_firstSelected(dlg);
            if (result && XString_toUtf8(result) && XString_toUtf8(result)[0]) {
                /* 已选中：直接采用。 */
            } else {
                /* 无选中/空串/分配失败统一重造回落值。 */
                if (result) XString_delete_base((XClass*)result);
                result = dlg->m_directory
                    ? xfiledialog_dupString(dlg->m_directory)
                    : XString_create();
            }
        } else
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
