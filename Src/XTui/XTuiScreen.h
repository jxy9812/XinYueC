/**
 * @file       XTuiScreen.h
 * @brief      XTui 屏幕单元格缓冲类公开 API。
 * @details    只继承 XClass，不依赖 XObject/事件系统。屏幕是一个离屏二维
 *             单元格数组，保存文本、前景色、背景色和属性；由 XTui 会话负责
 *             把差异绘制到终端。所有单元格按列(x)、行(y)寻址，原点在左上角。
 */

#ifndef XTUI_SCREEN_H
#define XTUI_SCREEN_H

#include "XTuiConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTUI_ON && XTUI_SCREEN_ON

#include "XTuiTypes.h"
#include "XClass.h"
#include "XMemory.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/** @brief 屏幕单元格容量是否随屏幕尺寸动态分配；固定容量模式下使用编译期数组。 */
#ifndef XTUI_SCREEN_DYNAMIC_BUFFER
#define XTUI_SCREEN_DYNAMIC_BUFFER 1
#endif

#if XTUI_SCREEN_DYNAMIC_BUFFER
/* 动态缓冲区：单元格数组由 XTuiScreen 在堆上分配。 */
#else
/* 固定缓冲区：单元格数组在结构中静态分配，请确认容量满足产品需要。 */
#endif

/** @brief 屏幕单元格；保存一段 UTF-8 文本和终端渲染属性。 */
typedef struct XTuiCell
{
    char     m_utf8[XTUI_CELL_UTF8_MAX + 1]; /**< UTF-8 文本，以 '\0' 结尾。 */
    uint8_t  m_fg;   /**< 前景色调色板索引（0-15）；XTUI_COLOR_DEFAULT_INDEX 表示默认色。 */
    uint8_t  m_bg;   /**< 背景色调色板索引（0-15）；XTUI_COLOR_DEFAULT_INDEX 表示默认色。 */
    uint8_t  m_attrs;/**< XTuiAttribute 位组合。 */
} XTuiCell;

/**
 * @brief XTuiScreen 类虚函数表枚举（仅继承 XClass，不新增虚函数）。
 */
XCLASS_DEFINE_BEGING(XTuiScreen)
XCLASS_DEFINE_EXTEND_END(XTuiScreen, XClass)

/**
 * @brief 屏幕缓冲对象。
 * @details 第一成员为 XClass，禁止手工修改。m_cells 是动态数组，仅由
 *          XTuiScreen 内部管理；调用者不得直接持有并释放。
 */
typedef struct XTuiScreen
{
    XClass  m_class;   /**< 基类，第一个成员，由 XClass 管理，禁止手工修改。 */
    int     m_width;   /**< 当前宽度（列数）。 */
    int     m_height;  /**< 当前高度（行数）。 */
#if XTUI_SCREEN_DYNAMIC_BUFFER
    XTuiCell* m_cells; /**< 单元格数组（m_width * m_height），仅由本对象管理。 */
#else
    XTuiCell m_cells[XTUI_SCREEN_MAX_COLUMNS * XTUI_SCREEN_MAX_ROWS]; /**< 固定单元格数组。 */
#endif
    XPoint m_cursor; /**< 当前光标位置。 */
} XTuiScreen;

/** @brief 初始化 XTuiScreen 类虚函数表。 */
XVtable* XTuiScreen_class_init(void);

/** @brief 在栈上初始化屏幕对象。 */
void XTuiScreen_init(XTuiScreen* screen);

/**
 * @brief 在堆上创建默认（1x1）屏幕对象。
 * @return 新对象，失败返回 NULL；使用 XTuiScreen_delete_base 释放。
 */
/**
 * @brief 在堆上创建指定尺寸的屏幕对象。
 * @param width 列数，必须大于 0。
 * @param height 行数，必须大于 0。
 * @return 新对象，失败返回 NULL；使用 XTuiScreen_delete_base 释放。
 */
XTuiScreen* XTuiScreen_create_ex(XMemoryType memory, int width, int height);

#define XTuiScreen_delete_base XClass_delete_base /**< 释放堆对象。 */

/**
 * @brief 调整屏幕尺寸；新区域清空，原有内容保留。
 * @param screen 目标屏幕；NULL 不执行。
 * @param width 新列数，必须大于 0。
 * @param height 新行数，必须大于 0。
 * @return true 表示成功；容量非法或内存不足返回 false，此时屏幕保持不变。
 */
bool XTuiScreen_resize(XTuiScreen* screen, int width, int height);

/** @brief 获取屏幕宽度（列数）。 */
int XTuiScreen_width(const XTuiScreen* screen);

/** @brief 获取屏幕高度（行数）。 */
int XTuiScreen_height(const XTuiScreen* screen);

/**
 * @brief 清空整个屏幕为空格，并重置前景/背景/属性。
 * @param screen 目标屏幕；NULL 不执行。
 */
void XTuiScreen_clear(XTuiScreen* screen);

/**
 * @brief 用指定单元格内容填充整个屏幕。
 * @param screen 目标屏幕。
 * @param cell 填充用的单元格；NULL 不执行。
 */
void XTuiScreen_fill(XTuiScreen* screen, const XTuiCell* cell);

/**
 * @brief 读取指定位置的单元格内容。
 * @param screen 目标屏幕。
 * @param x 列坐标。
 * @param y 行坐标。
 * @return 单元格指针；越界或参数非法返回 NULL。返回的是借用指针，调用者不得释放。
 */
const XTuiCell* XTuiScreen_cell(const XTuiScreen* screen, int x, int y);

/**
 * @brief 在指定位置写入一个 UTF-8 字符。
 * @param screen 目标屏幕。
 * @param x 列坐标。
 * @param y 行坐标。
 * @param utf8 UTF-8 字节序列，长度不超过 XTUI_CELL_UTF8_MAX。
 * @return true 表示写入成功；参数非法或越界返回 false。
 */
bool XTuiScreen_setUtf8(XTuiScreen* screen, int x, int y, const char* utf8);

/**
 * @brief 在指定位置写入一个 UTF-8 字符并设置渲染属性。
 * @param screen 目标屏幕。
 * @param x 列坐标。
 * @param y 行坐标。
 * @param utf8 UTF-8 字节序列。
 * @param fg 前景色 XColor；无效颜色（XTUI_COLOR_DEFAULT）表示终端默认色。
 * @param bg 背景色 XColor；无效颜色表示终端默认色。
 * @param attrs XTuiAttribute 位组合。
 * @return true 表示写入成功；参数非法或越界返回 false。
 */
bool XTuiScreen_setCell(XTuiScreen* screen, int x, int y,
                        const char* utf8, XColor fg, XColor bg, int attrs);

/**
 * @brief 在指定位置写入一段 UTF-8 文本，超过右边界时截断。
 * @param screen 目标屏幕。
 * @param x 起始列。
 * @param y 行坐标。
 * @param text UTF-8 文本；NULL 不执行。
 * @param length 限制最多写入的 UTF-8 字节数；小于 0 表示不限制。
 * @return 实际写入的单元格数。
 */
int XTuiScreen_writeText(XTuiScreen* screen, int x, int y, const char* text, int length);

/** @brief 设置光标位置。 */
void XTuiScreen_setCursor(XTuiScreen* screen, int x, int y);

/** @brief 获取光标位置。 */
XPoint XTuiScreen_cursor(const XTuiScreen* screen);

/**
 * @brief 把另一个屏幕的内容整屏拷贝到本屏幕。
 * @param dest 目标屏幕。
 * @param src 源屏幕。
 * @return true 表示成功。
 */
bool XTuiScreen_copyFrom(XTuiScreen* dest, const XTuiScreen* src);

/**
 * @brief 判断两个屏幕的单元格内容是否逐格相同。
 * @param a 屏幕 a。
 * @param b 屏幕 b。
 * @return true 表示可见内容完全一致。
 */
bool XTuiScreen_equal(const XTuiScreen* a, const XTuiScreen* b);

/**
 * @brief 创建空的默认单元格（空格、默认色）。
 * @param cell 输出单元格；NULL 不执行。
 */
void XTuiCell_makeDefault(XTuiCell* cell);

#endif /* XTUI_ON && XTUI_SCREEN_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XTuiScreen_create
#define XTuiScreen_create() \
	XTuiScreen_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, 1)

#endif /* XTUI_SCREEN_H */
