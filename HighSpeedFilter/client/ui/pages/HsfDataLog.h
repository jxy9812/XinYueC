/**
 * @file       HsfDataLog.h
 * @brief      数据-日志叶子页：服务端运行日志分页（0x0119）→ 表格。
 * @details    [2026-11-09 真实现替换占位] 数据源 = 服务端 logs 表（关键运维
 *             事件定点入库：conn/ctrl/alarm/auto/auth 五类），经
 *             HSF_CMD_LOG_PAGE 请求-应答拉取；工具条（时间范围/类别下拉/
 *             关键字/分页）+ 表格（时间/类别/事件）；导出走文件对话框
 *             （格式=过滤器档位）按当前筛选条件全量拉取（CSV/XLSX）。
 *             [形态约束] HsfUi.c 的 kHsfPageAdapt[10] 固定调
 *             HsfDataTablePage_adapt（HsfUi.c 本模块不可改），故本页结构
 *             前四成员与 HsfDataTablePage **同型同序**（见 HsfDataLog.c
 *             结构注释）——改成员顺序前必须连 HsfDataTable.c 一起核对。
 */
#ifndef HSFDATALOG_H
#define HSFDATALOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建日志叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（本页无返回钮，参数保留兼容页表签名）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfDataLog_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFDATALOG_H */
