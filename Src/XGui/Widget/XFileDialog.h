/******************************************************************************
 * @file       XFileDialog.h
 * @brief      XFileDialog 文件对话框控件（对标 Qt 6.8 QFileDialog : QDialog）。
 * @details    继承 XDialog，提供文件选择对话框的公共 API 面：
 *             - 静态便捷函数 getOpenFileName/getOpenFileNames/
 *               getSaveFileName/getExistingDirectory（每个均带 _2 UTF-8
 *               重载；无 GUI 对话框环境时按 @note 约定返回默认值）；
 *             - 实例属性：fileMode/acceptMode/nameFilter/nameFilters/
 *               directory/selectedFiles/selectedFile/defaultSuffix/
 *               option 位集/labelText/viewMode/selectFile/selectNameFilter；
 *             - 信号：fileSelected/filesSelected/currentChanged/
 *               directoryEntered/filterSelected（参数带 Qt 语义）。
 *             枚举 FileMode/AcceptMode/Option 数值与 Qt 6.8.3
 *             qtbase/src/widgets/dialogs/qfiledialog.h 完全一致。
 * @note       模块总开关 XDIALOG_ON（XWIDGET_ON && XDIALOG_ON 有效）。
 * @note       无 GUI 对话框环境：静态函数创建临时实例、应用存储 setter，
 *             但不执行模态对话框循环，一律返回默认值（字符串类返回空串、
 *             列表类返回空列表），*selectedFilterIndex 保持 0；信号可由
 *             应用手动触发供测试。渲染与原生文件浏览为后续扩展。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XFILEDIALOG_H
#define XFILEDIALOG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#include "XStringList.h"

typedef struct XByteArray XByteArray; /* 前向声明。 */
#if XDIALOG_ON
#include "XDialog.h"
#endif /* XDIALOG_ON */

#if XWIDGET_ON && XDIALOG_ON

/** @brief 文件对话框文件模式（对标 QFileDialog::FileMode，数值一致）。 */
typedef enum XFileDialogFileMode
{
    XFileDialog_AnyFile = 0,       /**< 任意文件（含不存在文件）。 */
    XFileDialog_ExistingFile = 1,  /**< 已存在的单个文件。 */
    XFileDialog_Directory = 2,     /**< 目录。 */
    XFileDialog_ExistingFiles = 3  /**< 已存在的多个文件。 */
} XFileDialogFileMode;

/** @brief 文件对话框接受模式（对标 QFileDialog::AcceptMode，数值一致）。 */
typedef enum XFileDialogAcceptMode
{
    XFileDialog_AcceptOpen = 0,    /**< 打开模式。 */
    XFileDialog_AcceptSave = 1     /**< 保存模式。 */
} XFileDialogAcceptMode;

/** @brief 文件对话框标签角色（对标 QFileDialog::DialogLabel，数值一致）。 */
typedef enum XFileDialogDialogLabel
{
    XFileDialogDialogLabel_LookIn = 0,   /**< 目录浏览标签。 */
    XFileDialogDialogLabel_FileName = 1, /**< 文件名标签。 */
    XFileDialogDialogLabel_FileType = 2, /**< 文件类型标签。 */
    XFileDialogDialogLabel_Accept = 3,   /**< 接受按钮标签。 */
    XFileDialogDialogLabel_Reject = 4    /**< 拒绝按钮标签。 */
} XFileDialogDialogLabel;

/** @brief 文件对话框选项位（对标 QFileDialog::Option，数值一致）。 */
typedef enum XFileDialogOption
{
    XFileDialog_ShowDirsOnly                = 0x00000001, /**< 仅显示目录。 */
    XFileDialog_DontResolveSymlinks         = 0x00000002, /**< 不解析符号链接。 */
    XFileDialog_DontConfirmOverwrite        = 0x00000004, /**< 覆盖前不确认。 */
    XFileDialog_DontUseNativeDialog         = 0x00000008, /**< 不使用原生对话框。 */
    XFileDialog_ReadOnly                    = 0x00000010, /**< 只读。 */
    XFileDialog_HideNameFilterDetails       = 0x00000020, /**< 隐藏过滤器细节。 */
    XFileDialog_DontUseCustomDirectoryIcons = 0x00000040  /**< 不使用自定义目录图标。 */
} XFileDialogOption;
typedef uint32_t XFileDialogOptions;

/** @brief 文件对话框视图模式（对标 QFileDialog::ViewMode，数值一致）。 */
typedef enum XFileDialogViewMode
{
    XFileDialogViewMode_Detail = 0, /**< 详细信息视图。 */
    XFileDialogViewMode_List = 1    /**< 列表视图。 */
} XFileDialogViewMode;

XCLASS_DEFINE_BEGING(XFileDialog)
XCLASS_DEFINE_EXTEND_END(XFileDialog, XDialog)

/**
 * @brief      XFileDialog 文件对话框对象；m_base 必须是第一个成员。
 * @details    所有字符串字段为拥有型 XString*，列表字段为拥有型
 *             XStringList*；销毁随 XFileDialog_deinit_base 一并释放。
 */
typedef struct XFileDialog
{
    XDialog m_base;                /**< 基类成员；必须是第一个。 */
    XFileDialogFileMode m_fileMode;    /**< 文件模式；默认 ExistingFile。 */
    XFileDialogAcceptMode m_acceptMode;/**< 接受模式；默认 AcceptOpen。 */
    XFileDialogViewMode m_viewMode;    /**< 视图模式；默认 Detail。 */
    XFileDialogOptions m_options;      /**< 选项位集；默认 0。 */
    XString* m_directory;          /**< 当前目录（拥有；对标 directory）。 */
    XStringList* m_nameFilters;    /**< 名称过滤器列表（拥有）。 */
    XString* m_selectedNameFilter; /**< 当前选中过滤器（拥有）。 */
    XStringList* m_selectedFiles;  /**< 已选文件列表（拥有）。 */
    XString* m_defaultSuffix;      /**< 默认后缀（拥有）。 */
    XString* m_labelTexts[5];      /**< 各 DialogLabel 的标签文本（拥有）。 */
    XString* m_directoryUrl;       /**< 目录 URL（拥有；对标 directoryUrl）。 */
    XString* m_filter;             /**< 简单过滤器（拥有；对标 filter）。 */
    XStringList* m_mimeTypeFilters;/**< MIME 过滤器列表（拥有）。 */
    XString* m_selectedMimeTypeFilter; /**< 当前选中 MIME 过滤器（拥有）。 */
    XStringList* m_history;        /**< 历史目录列表（拥有）。 */
    XStringList* m_sidebarUrls;    /**< 侧栏 URL 列表（拥有）。 */
    XStringList* m_supportedSchemes; /**< 支持协议列表（拥有）。 */
    void* m_iconProvider;          /**< 图标提供者（借用；类型不映射，恒 NULL）。 */
} XFileDialog;

/**
 * @brief      XFileDialog 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XFileDialog_class_init(void);

/**
 * @brief      初始化 XFileDialog（对标 QFileDialog 构造）。
 * @param      self 目标对象指针；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XFileDialog_init(XFileDialog* self, XWidget* parent, XWidgetFlags flags);
#define XFileDialog_create(parent, flags) XFileDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建 XFileDialog。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新对象指针；失败返回 NULL。
 */
XFileDialog* XFileDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XFileDialog_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XFileDialog_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 实例属性（对标 QFileDialog） ==================== */

/**
 * @brief      设置文件模式（对标 QFileDialog::setFileMode）。
 * @param      self 目标对话框。
 * @param      mode 文件模式（XFileDialogFileMode）。
 * @return     无返回值。
 */
void XFileDialog_setFileMode(XFileDialog* self, XFileDialogFileMode mode);
/**
 * @brief      获取文件模式（对标 QFileDialog::fileMode）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前文件模式；无效时返回 XFileDialog_ExistingFile。
 */
XFileDialogFileMode XFileDialog_fileMode(const XFileDialog* self);
/**
 * @brief      设置接受模式（对标 QFileDialog::setAcceptMode）。
 * @param      self 目标对话框。
 * @param      mode 接受模式（XFileDialogAcceptMode）。
 * @return     无返回值。
 */
void XFileDialog_setAcceptMode(XFileDialog* self, XFileDialogAcceptMode mode);
/**
 * @brief      获取接受模式（对标 QFileDialog::acceptMode）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前接受模式；无效时返回 XFileDialog_AcceptOpen。
 */
XFileDialogAcceptMode XFileDialog_acceptMode(const XFileDialog* self);
/**
 * @brief      设置视图模式（对标 QFileDialog::setViewMode）。
 * @param      self 目标对话框。
 * @param      mode 视图模式（XFileDialogViewMode）。
 * @return     无返回值。
 */
void XFileDialog_setViewMode(XFileDialog* self, XFileDialogViewMode mode);
/**
 * @brief      获取视图模式（对标 QFileDialog::viewMode）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前视图模式；无效时返回 XFileDialogViewMode_Detail。
 */
XFileDialogViewMode XFileDialog_viewMode(const XFileDialog* self);
/**
 * @brief      设置单一名称过滤器（对标 QFileDialog::setNameFilter）。
 * @param      self 目标对话框。
 * @param      filter 过滤器文本（如 "Images (*.png *.jpg)"）；可为 NULL 清空。
 * @return     无返回值。
 */
void XFileDialog_setNameFilter(XFileDialog* self, const XString* filter);
/**
 * @brief      设置名称过滤器列表（对标 QFileDialog::setNameFilters）。
 * @param      self 目标对话框。
 * @param      filters 过滤器列表（深拷贝）；可为 NULL 清空。
 * @return     无返回值。
 */
void XFileDialog_setNameFilters(XFileDialog* self, const XStringList* filters);
/**
 * @brief      获取名称过滤器列表副本（对标 QFileDialog::nameFilters）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XStringList 深拷贝，调用方拥有，须
 *             XStringList_delete_base；无效时返回空列表。
 */
XStringList* XFileDialog_nameFilters(const XFileDialog* self);
/**
 * @brief      选中指定名称过滤器（对标 QFileDialog::selectNameFilter）。
 * @param      self 目标对话框。
 * @param      filter 要选中的过滤器文本；可为 NULL 清空。
 * @return     无返回值。
 */
void XFileDialog_selectNameFilter(XFileDialog* self, const XString* filter);
/**
 * @brief      获取当前选中的名称过滤器副本（对标 QFileDialog::selectedNameFilter）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无效或无选中时返回空串。
 */
XString* XFileDialog_selectedNameFilter(const XFileDialog* self);
/**
 * @brief      设置当前目录（对标 QFileDialog::setDirectory）。
 * @param      self 目标对话框。
 * @param      directory 目录路径；可为 NULL 清空。
 * @return     无返回值。
 */
void XFileDialog_setDirectory(XFileDialog* self, const XString* directory);
/**
 * @brief      获取当前目录副本（对标 QFileDialog::directory）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无效时返回空串。
 */
XString* XFileDialog_directory(const XFileDialog* self);
/**
 * @brief      把一个文件加入选中列表（对标 QFileDialog::selectFile）。
 * @param      self 目标对话框。
 * @param      filename 文件名；可为 NULL 忽略。
 * @return     无返回值。
 */
void XFileDialog_selectFile(XFileDialog* self, const XString* filename);
/**
 * @brief      获取已选文件列表副本（对标 QFileDialog::selectedFiles）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XStringList 深拷贝，调用方拥有，须
 *             XStringList_delete_base；无效时返回空列表。
 */
XStringList* XFileDialog_selectedFiles(const XFileDialog* self);
/**
 * @brief      获取第一个已选文件副本（对标 QFileDialog::selectedFiles 首项）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无选中或无效时返回空串。
 */
XString* XFileDialog_selectedFile(const XFileDialog* self);
/** @brief 选中文件 URL 列表（对标 selectedUrls；从 selectedFiles 派生）。 */
XStringList* XFileDialog_selectedUrls(const XFileDialog* self);
/** @brief 设置目录 URL（对标 setDirectoryUrl）。 */
void XFileDialog_setDirectoryUrl(XFileDialog* self, const XString* directory);
/** @brief 目录 URL（对标 directoryUrl）。 */
XString* XFileDialog_directoryUrl(const XFileDialog* self);
/** @brief 选中 URL（对标 selectUrl；记录为选中文件）。 */
void XFileDialog_selectUrl(XFileDialog* self, const XString* url);
/** @brief 设置简单过滤器（对标 setFilter）。 */
void XFileDialog_setFilter(XFileDialog* self, const XString* filter);
/** @brief 简单过滤器（对标 filter）。 */
XString* XFileDialog_filter(const XFileDialog* self);
/** @brief 设置 MIME 过滤器列表（对标 setMimeTypeFilters）。 */
void XFileDialog_setMimeTypeFilters(XFileDialog* self,
                                    const XStringList* filters);
/** @brief MIME 过滤器列表（对标 mimeTypeFilters）。 */
XStringList* XFileDialog_mimeTypeFilters(const XFileDialog* self);
/** @brief 选择 MIME 过滤器（对标 selectMimeTypeFilter）。 */
void XFileDialog_selectMimeTypeFilter(XFileDialog* self,
                                      const XString* filter);
/** @brief 当前选中 MIME 过滤器（对标 selectedMimeTypeFilter）。 */
XString* XFileDialog_selectedMimeTypeFilter(const XFileDialog* self);
/** @brief 设置历史目录列表（对标 setHistory）。 */
void XFileDialog_setHistory(XFileDialog* self, const XStringList* history);
/** @brief 历史目录列表（对标 history）。 */
XStringList* XFileDialog_history(const XFileDialog* self);
/** @brief 设置侧栏 URL 列表（对标 setSidebarUrls）。 */
void XFileDialog_setSidebarUrls(XFileDialog* self,
                                const XStringList* urls);
/** @brief 侧栏 URL 列表（对标 sidebarUrls）。 */
XStringList* XFileDialog_sidebarUrls(const XFileDialog* self);
/** @brief 设置支持协议列表（对标 setSupportedSchemes）。 */
void XFileDialog_setSupportedSchemes(XFileDialog* self,
                                     const XStringList* schemes);
/** @brief 支持协议列表（对标 supportedSchemes）。 */
XStringList* XFileDialog_supportedSchemes(const XFileDialog* self);
/** @brief 保存对话框状态（对标 saveState；当前输出空字节数组）。
 * @note 状态序列化未实现（已知偏差 Task 2.20）。 */
void XFileDialog_saveState(const XFileDialog* self, XByteArray* out);
/** @brief 恢复对话框状态（对标 restoreState；当前恒 false）。 */
bool XFileDialog_restoreState(XFileDialog* self, const XByteArray* state);
/** @brief 设置图标提供者（对标 setIconProvider；类型不映射，仅记录）。
 * @note XGui 无 QFileIconProvider 对应物。 */
void XFileDialog_setIconProvider(XFileDialog* self, void* provider);
/** @brief 图标提供者（对标 iconProvider；恒 NULL）。 */
void* XFileDialog_iconProvider(const XFileDialog* self);
/** @brief 设置条目代理（对标 setItemDelegate；类型不映射，仅记录）。 */
void XFileDialog_setItemDelegate(XFileDialog* self, void* delegate);
/** @brief 条目代理（对标 itemDelegate；恒 NULL）。 */
void* XFileDialog_itemDelegate(const XFileDialog* self);
/** @brief 设置代理模型（对标 setProxyModel；类型不映射，仅记录）。 */
void XFileDialog_setProxyModel(XFileDialog* self, void* model);
/** @brief 代理模型（对标 proxyModel；恒 NULL）。 */
void* XFileDialog_proxyModel(const XFileDialog* self);
/**
 * @brief      设置默认后缀（对标 QFileDialog::setDefaultSuffix）。
 * @param      self 目标对话框。
 * @param      suffix 默认后缀（不含点号）；可为 NULL 清空。
 * @return     无返回值。
 */
void XFileDialog_setDefaultSuffix(XFileDialog* self, const XString* suffix);
/**
 * @brief      获取默认后缀副本（对标 QFileDialog::defaultSuffix）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无效时返回空串。
 */
XString* XFileDialog_defaultSuffix(const XFileDialog* self);
/**
 * @brief      设置单个选项位（对标 QFileDialog::setOption）。
 * @param      self 目标对话框。
 * @param      option 选项位（XFileDialogOption）。
 * @param      on true=置位，false=清位。
 * @return     无返回值。
 */
void XFileDialog_setOption(XFileDialog* self, XFileDialogOption option, bool on);
/**
 * @brief      测试选项位（对标 QFileDialog::testOption）。
 * @param      self 目标对话框；可为 NULL。
 * @param      option 选项位（XFileDialogOption）。
 * @return     置位返回 true；无效返回 false。
 */
bool XFileDialog_testOption(const XFileDialog* self, XFileDialogOption option);
/**
 * @brief      设置整个选项位集（对标 QFileDialog::setOptions）。
 * @param      self 目标对话框。
 * @param      options 选项位组合。
 * @return     无返回值。
 */
void XFileDialog_setOptions(XFileDialog* self, XFileDialogOptions options);
/**
 * @brief      获取选项位集（对标 QFileDialog::options）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前选项位组合；无效返回 0。
 */
XFileDialogOptions XFileDialog_options(const XFileDialog* self);
/**
 * @brief      设置指定标签角色的文本（对标 QFileDialog::setLabelText）。
 * @param      self 目标对话框。
 * @param      label 标签角色（XFileDialogDialogLabel）。
 * @param      text 标签文本；可为 NULL 清空。
 * @return     无返回值。
 */
void XFileDialog_setLabelText(XFileDialog* self, XFileDialogDialogLabel label,
                              const XString* text);
/**
 * @brief      获取指定标签角色的文本副本（对标 QFileDialog::labelText）。
 * @param      self 目标对话框；可为 NULL。
 * @param      label 标签角色（XFileDialogDialogLabel）。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             未设置时返回空串。
 */
XString* XFileDialog_labelText(const XFileDialog* self, XFileDialogDialogLabel label);

/* ==================== 静态便捷函数（对标 QFileDialog） ==================== */

/**
 * @brief      获取单个打开文件名（对标 QFileDialog::getOpenFileName）。
 * @note       无 GUI 对话框环境：返回空串，selectedFilterIndex 置 0。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      caption 对话框标题；可为 NULL。
 * @param      dir 起始目录；可为 NULL。
 * @param      filter 名称过滤器；可为 NULL。
 * @param      selectedFilterIndex 输出：选中的过滤器下标（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XFileDialog_getOpenFileName(XWidget* parent, const XString* caption,
                                     const XString* dir, const XString* filter,
                                     int* selectedFilterIndex);
/**
 * @brief      获取单个打开文件名（UTF-8 重载）。
 * @note       无 GUI 对话框环境：返回空串，selectedFilterIndex 置 0。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      caption 对话框标题（UTF-8）；可为 NULL。
 * @param      dir 起始目录（UTF-8）；可为 NULL。
 * @param      filter 名称过滤器（UTF-8）；可为 NULL。
 * @param      selectedFilterIndex 输出：选中的过滤器下标（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XFileDialog_getOpenFileName_2(XWidget* parent, const char* caption,
                                       const char* dir, const char* filter,
                                       int* selectedFilterIndex);
/**
 * @brief      获取多个打开文件名（对标 QFileDialog::getOpenFileNames）。
 * @note       无 GUI 对话框环境：返回空列表，selectedFilterIndex 置 0。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      caption 对话框标题；可为 NULL。
 * @param      dir 起始目录；可为 NULL。
 * @param      filter 名称过滤器；可为 NULL。
 * @param      selectedFilterIndex 输出：选中的过滤器下标（可为 NULL）。
 * @return     新建的 XStringList，调用方拥有，须 XStringList_delete_base。
 */
XStringList* XFileDialog_getOpenFileNames(XWidget* parent, const XString* caption,
                                          const XString* dir, const XString* filter,
                                          int* selectedFilterIndex);
/**
 * @brief      获取多个打开文件名（UTF-8 重载）。
 * @note       无 GUI 对话框环境：返回空列表，selectedFilterIndex 置 0。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      caption 对话框标题（UTF-8）；可为 NULL。
 * @param      dir 起始目录（UTF-8）；可为 NULL。
 * @param      filter 名称过滤器（UTF-8）；可为 NULL。
 * @param      selectedFilterIndex 输出：选中的过滤器下标（可为 NULL）。
 * @return     新建的 XStringList，调用方拥有，须 XStringList_delete_base。
 */
XStringList* XFileDialog_getOpenFileNames_2(XWidget* parent, const char* caption,
                                            const char* dir, const char* filter,
                                            int* selectedFilterIndex);
/**
 * @brief      获取保存文件名（对标 QFileDialog::getSaveFileName）。
 * @note       无 GUI 对话框环境：返回空串，selectedFilterIndex 置 0。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      caption 对话框标题；可为 NULL。
 * @param      dir 起始目录；可为 NULL。
 * @param      filter 名称过滤器；可为 NULL。
 * @param      selectedFilterIndex 输出：选中的过滤器下标（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XFileDialog_getSaveFileName(XWidget* parent, const XString* caption,
                                     const XString* dir, const XString* filter,
                                     int* selectedFilterIndex);
/**
 * @brief      获取保存文件名（UTF-8 重载）。
 * @note       无 GUI 对话框环境：返回空串，selectedFilterIndex 置 0。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      caption 对话框标题（UTF-8）；可为 NULL。
 * @param      dir 起始目录（UTF-8）；可为 NULL。
 * @param      filter 名称过滤器（UTF-8）；可为 NULL。
 * @param      selectedFilterIndex 输出：选中的过滤器下标（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XFileDialog_getSaveFileName_2(XWidget* parent, const char* caption,
                                       const char* dir, const char* filter,
                                       int* selectedFilterIndex);
/**
 * @brief      获取已存在目录（对标 QFileDialog::getExistingDirectory）。
 * @note       无 GUI 对话框环境：返回空串。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      caption 对话框标题；可为 NULL。
 * @param      dir 起始目录；可为 NULL。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XFileDialog_getExistingDirectory(XWidget* parent, const XString* caption,
                                          const XString* dir);
/**
 * @brief      获取已存在目录（UTF-8 重载）。
 * @note       无 GUI 对话框环境：返回空串。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      caption 对话框标题（UTF-8）；可为 NULL。
 * @param      dir 起始目录（UTF-8）；可为 NULL。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XFileDialog_getExistingDirectory_2(XWidget* parent, const char* caption,
                                            const char* dir);

/* ==================== 信号（对标 QFileDialog） ==================== */

/**
 * @brief      单个文件选中信号（对标 QFileDialog::fileSelected）。
 * @param      self 目标对话框。
 * @param      file 选中的文件路径（XString*，载荷深拷贝）。
 * @return     信号标识。
 */
void* XFileDialog_fileSelected_signal(XFileDialog* self, const XString* file);
/**
 * @brief      多个文件选中信号（对标 QFileDialog::filesSelected）。
 * @param      self 目标对话框。
 * @param      files 选中的文件列表（XStringList*，载荷深拷贝）。
 * @return     信号标识。
 */
void* XFileDialog_filesSelected_signal(XFileDialog* self, const XStringList* files);
/**
 * @brief      当前路径变化信号（对标 QFileDialog::currentChanged）。
 * @param      self 目标对话框。
 * @param      path 当前路径（XString*，载荷深拷贝）。
 * @return     信号标识。
 */
void* XFileDialog_currentChanged_signal(XFileDialog* self, const XString* path);
/**
 * @brief      目录进入信号（对标 QFileDialog::directoryEntered）。
 * @param      self 目标对话框。
 * @param      directory 进入的目录（XString*，载荷深拷贝）。
 * @return     信号标识。
 */
void* XFileDialog_directoryEntered_signal(XFileDialog* self, const XString* directory);
/**
 * @brief      过滤器选中信号（对标 QFileDialog::filterSelected）。
 * @param      self 目标对话框。
 * @param      filter 选中的过滤器（XString*，载荷深拷贝）。
 * @return     信号标识。
 */
void* XFileDialog_filterSelected_signal(XFileDialog* self, const XString* filter);

#endif /* XWIDGET_ON && XDIALOG_ON */

#endif /* XFILEDIALOG_H */
