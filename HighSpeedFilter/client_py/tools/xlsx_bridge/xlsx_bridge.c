/* =============================================================================
 * xlsx_bridge —— XExcel（XCore/XData）的**扁平 C ABI** 封装，给 Python ctypes 用。
 *
 * [为什么存在] Python 端导出 Excel 不引第三方库（openpyxl/matplotlib 在板端
 * 不可用），改走本仓库现成的 C 端 XExcel 框架：本文件把 XDocument/
 * XWorksheet 的面向对象 API 折成 6 个纯 C 函数，编译成 xlsx_bridge.dll，
 * Python 用 ctypes 加载（见 hsfsrv_client/xlsx_writer.py）。
 *
 * [所有权] xlsx_create 返回的句柄归调用方；用完必须 xlsx_delete，
 * 否则泄漏（与 C 端 XDocument_create/XDocument_delete 的约定一致）。
 *
 * [编译] 目标定义在主构建 HighSpeedFilter/CMakeLists.txt（xlsx_bridge 目标，
 * 链接 XinYueCoreS + Library 外部库，与 XDataTest 同口径）；Windows 下双击
 * 同目录 build.bat（缺省用 build/w3 主构建树）。产出自动拷贝到
 * hsfsrv_client/bin/xlsx_bridge.dll。
 *
 * [行列基约] 透传 XExcel 原生口径：**row/col 从 1 起**（与 C 端测试一致）；
 * 0 基换算由 Python 封装层（hsfsrv_client/xlsx_writer.py）负责。
 * ============================================================================ */
#include <stdint.h>
#include <stdlib.h>

/* [必须最先包含] XDocument.h 全体内容包在 `#if XEXCEL_ON` 里，而该宏由
 * CXinYueConfig.h 定义（经 XMemory.h 反向包含）；XDocument.h 自己的
 * include 链在宏判断**之后**才建立 —— 直接先包含它会把整个头裁成空。 */
#include "CXinYueConfig.h"
#include "XMemory.h"

#include "XDocument.h"
#include "XWorksheet.h"
#include "XAbstractSheet.h"

/* 新建工作簿（默认带一张 Sheet1），返回句柄；失败返回 NULL。 */
#ifdef _WIN32
__declspec(dllexport)
#endif
void* xlsx_create(void)
{
    return (void*)XDocument_create();
}

/* 释放句柄。传 NULL 安全。 */
#ifdef _WIN32
__declspec(dllexport)
#endif
void xlsx_delete(void* doc)
{
    if (doc) {
        XDocument_delete((XDocument*)doc);
    }
}

/* 新增一张工作表并选中它（后续写入都落在这张表上）。
 * 返回 1=成功 0=失败。 */
#ifdef _WIN32
__declspec(dllexport)
#endif
int xlsx_add_sheet(void* doc, const char* name_utf8)
{
    if (!doc || !name_utf8) {
        return 0;
    }
    if (!XDocument_addSheet_utf8((XDocument*)doc, name_utf8,
                                 XAbstractSheet_ST_WorkSheet)) {
        return 0;
    }
    return XDocument_selectSheet_utf8((XDocument*)doc, name_utf8) ? 1 : 0;
}

/* 写字符串（UTF-8）。row/col 从 0 起。返回 1=成功。 */
#ifdef _WIN32
__declspec(dllexport)
#endif
int xlsx_write_str(void* doc, int row, int col, const char* text_utf8)
{
    XWorksheet* ws;
    if (!doc || !text_utf8) {
        return 0;
    }
    ws = XDocument_currentWorksheet((XDocument*)doc);
    if (!ws) {
        return 0;
    }
    return XWorksheet_writeString_utf8(ws, row, col, text_utf8, NULL) ? 1 : 0;
}

/* 写数字。返回 1=成功。 */
#ifdef _WIN32
__declspec(dllexport)
#endif
int xlsx_write_num(void* doc, int row, int col, double value)
{
    XWorksheet* ws;
    if (!doc) {
        return 0;
    }
    ws = XDocument_currentWorksheet((XDocument*)doc);
    if (!ws) {
        return 0;
    }
    return XWorksheet_writeNumeric(ws, row, col, value, NULL) ? 1 : 0;
}

/* 设定某列宽度（col 从 0 起；width 用 Excel 字符宽口径，如 12.0）。 */
#ifdef _WIN32
__declspec(dllexport)
#endif
int xlsx_set_col_width(void* doc, int col, double width)
{
    XWorksheet* ws;
    if (!doc) {
        return 0;
    }
    ws = XDocument_currentWorksheet((XDocument*)doc);
    if (!ws) {
        return 0;
    }
    return XWorksheet_setColumnWidth(ws, col, col, width) ? 1 : 0;
}

/* 保存为 .xlsx 文件（路径 UTF-8）。返回 1=成功。 */
#ifdef _WIN32
__declspec(dllexport)
#endif
int xlsx_save(void* doc, const char* path_utf8)
{
    if (!doc || !path_utf8) {
        return 0;
    }
    return XDocument_saveAs_utf8((XDocument*)doc, path_utf8) ? 1 : 0;
}

/* 当前框架堆存量（字节）——泄漏测试用。
 *
 * [口径] XMemory_statistics_2 直读系统分配器实时计数（不受统计使能门控）。
 * 框架对象全部走 XMalloc/XFree，故「一组 create→write→save→delete 前后
 * 读数不涨」即无泄漏；涨了就是哪一路 delete/释放没走全。
 */
#ifdef _WIN32
__declspec(dllexport)
#endif
int64_t xlsx_mem_bytes(void)
{
    return (int64_t)XMemory_statistics_2(XMEMORY_TYPE_SYSTEM).systemBytes;
}
