/******************************************************************************
 * @file       XCssValue.h
 * @brief      CSS 值类型化解析扩展：渐变/调色板角色/url/背景重复与定位。
 * @details    对标 Qt 6.8.3 qcssparser.cpp 的 parseBrushValue（qlinear/
 *             qradial/qconical 渐变）与 parseColorValue（palette 角色）
 *             的值子语法，补齐 XStyleSheetStyle.h 值解析族（颜色/长度）
 *             之外的复合值类型。纯函数无状态：输入 UTF-8 值文本，输出
 *             类型化结构，失败时输出参数保持原值不写（同 XCssParseColor
 *             口径）。供样式表消费线按属性拆解 background-image/
 *             background-repeat/background-position/color 等声明值。
 * @note       本模块随 XSTYLE_ON 裁剪（渐变停靠点颜色经 XCssParseColor
 *             解析，与样式表值解析族同门控）；XCssParsePaletteRole 另需
 *             XPALETTE_ON（角色名表对照 XPaletteColorRole 枚举，裁剪时
 *             恒返回 false）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCSSVALUE_H
#define XCSSVALUE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"

#if XSTYLE_ON

/** @brief 渐变类型（对标 Qt 6.8 QGradient 三形态在 QSS 的三个函数名）。 */
typedef enum XCssGradientType
{
    XCssGradient_Linear = 0,   /**< 线性渐变（qlineargradient）。 */
    XCssGradient_Radial,       /**< 径向渐变（qradialgradient）。 */
    XCssGradient_Conical       /**< 锥形渐变（qconicalgradient）。 */
} XCssGradientType;

/** @brief 渐变颜色停靠点：position 0.0~1.0，ARGB（0xAARRGGBB）。 */
typedef struct XCssGradientStop
{
    double m_position;   /**< 停靠位置 0.0~1.0（越界整串拒绝）。 */
    uint32_t m_color;    /**< 停靠颜色（ARGB；经 XCssParseColor 解析）。 */
} XCssGradientStop;

/**
 * @brief      类型化渐变（对标 Qt 6.8.3 qcssparser_p.h Gradient 的纯值
 *             子集，按嵌入式消费端裁剪：spread 与 coordinatemode 仅校验
 *             不落盘、径向焦点 fx/fy 校验后丢弃——本结构无对应字段）。
 * @details    仅与 m_type 对应的坐标组有效，其余字段置 0；m_stops 按
 *             声明顺序存放，m_stopCount ≥ 1（≤ 容量 16，超出整串拒绝）。
 */
typedef struct XCssGradient
{
    XCssGradientType m_type;              /**< 渐变类型。 */
    double m_x1, m_y1, m_x2, m_y2;        /**< 线性起点/终点（仅 Linear 有效）。 */
    double m_cx, m_cy, m_radius, m_angle; /**< 径向圆心/半径与锥形圆心/起始角（度，仅 Radial/Conical 有效）。 */
    XCssGradientStop m_stops[16];         /**< 停靠点数组（容量 16）。 */
    int m_stopCount;                      /**< 实际停靠点数。 */
} XCssGradient;

/**
 * @brief 解析 QSS 渐变函数（qlineargradient/qradialgradient/qconicalgradient
 *        全语法）。
 *
 *        语法（大小写不敏感，空白容忍，逗号切分参数段）：
 *        - qlineargradient(x1:n, y1:n, x2:n, y2:n, ...)：四个坐标必填；
 *        - qradialgradient(cx:n, cy:n, radius:n, ...)：三项必填；fx/fy
 *          可选——语法校验后丢弃（结构无焦点字段，Qt 有而本库裁剪）；
 *        - qconicalgradient(cx:n, cy:n, angle:n, ...)：三项必填（angle
 *          为度数，原样存储不归一化）；
 *        - 三者通用可选参数：spread ∈ {pad, reflect, repeat}、
 *          coordinatemode ∈ {logical, stretchtodevice, objectbounding,
 *          object}——仅校验取值合法（对齐 Qt parseBrushValue 的关键字
 *          表），本结构不落盘（QGradient 默认 PadSpread/LogicalMode）；
 *        - 停靠点段：stop:pos color，pos 为 0.0~1.0 数值（可写 50% 形
 *          式，读作 /100；越界整串拒绝），color 经 XCssParseColor 解析
 *          （#hex/rgba()/hsla()/hsv()/具名色），颜色解析失败整体返回
 *          false；至少 1 个 stop，多于 16 个整串拒绝；
 *        - 坐标 n 为十进制数（可带符号；可带 % 读作 /100），其他单位后
 *          缀视为尾部垃圾拒绝；参数段 name:value 的 name 未知或重复段
 *          缺 ':' 拒绝；同名重复段后写覆盖（对齐 Qt QHash::insert 语义）；
 *          必填坐标缺项整体拒绝（比 Qt 宽松默认更严，产出确定性结果）。
 *
 * @param value 渐变函数文本（UTF-8；可含首尾空白）。
 * @param out 输出渐变（失败时不写）。
 * @return 解析成功返回 true。
 */
bool XCssParseGradient(const char* value, XCssGradient* out);

/**
 * @brief 解析调色板角色色（"palette(角色名)" → XPaletteColorRole 枚举值）。
 *
 *        角色名表对照 XPalette.h 的 XPaletteColorRole 枚举全量（NColorRoles
 *         为内部计数无名字）；多词角色采用 Qt qcssparser values 表的连
 *        字符式全小写拼写，大小写不敏感：window-text、button、light、
 *        midlight、dark、mid、text、bright-text、button-text、base、
 *        window、shadow、highlight、highlighted-text、link、link-visited、
 *        alternate-base、no-role、tooltip-base、tooltip-text、
 *        placeholder-text、accent。
 *
 * @param value 角色色文本（如 "palette(base)"；大小写不敏感）。
 * @param roleOut 输出角色枚举值（int 形式；失败时不写）。
 * @return 解析成功返回 true；非 palette() 形态、角色名未登记或
 *         XPALETTE_ON=0 时返回 false。
 */
bool XCssParsePaletteRole(const char* value, int* roleOut);

/**
 * @brief 解析 url(path) 资源路径（去引号去空白）。
 *
 *        剥 "url(...)" 外壳（大小写不敏感）、去壳内首尾空白、剥一层
 *        配对引号（单/双引号，剥后再次去首尾空白）；路径内部空白原样
 *        保留（文件名可含空格）。缓冲不足按整串拒绝不截断；空路径拒绝。
 *
 * @param value url() 文本。
 * @param out 输出路径缓冲（NUL 结尾；失败时不写）。
 * @param cap 输出缓冲容量（须 ≥ 路径字节数 + 1）。
 * @return 解析成功返回 true。
 */
bool XCssParseUrlPath(const char* value, char* out, size_t cap);

/** @brief 背景重复方式（对标 qcssparser Repeat 枚举）。 */
typedef enum XCssRepeatMode
{
    XCssRepeat_Repeat = 0,   /**< repeat：双向平铺。 */
    XCssRepeat_RepeatX,      /**< repeat-x：仅横向平铺。 */
    XCssRepeat_RepeatY,      /**< repeat-y：仅纵向平铺。 */
    XCssRepeat_NoRepeat      /**< no-repeat：不平铺。 */
} XCssRepeatMode;

/**
 * @brief 解析背景重复关键字（repeat/repeat-x/repeat-y/no-repeat）。
 *
 *        大小写不敏感，容忍首尾空白；其他取值拒绝。
 *
 * @param value 重复关键字文本。
 * @param out 输出重复方式（失败时不写）。
 * @return 解析成功返回 true。
 */
bool XCssParseBackgroundRepeat(const char* value, XCssRepeatMode* out);

/**
 * @brief 解析背景定位（关键字与数值/百分比混排，1~2 个空白分隔分量）。
 *
 *        分量口径（对标 CSS background-position，关键字大小写不敏感）：
 *        - x 轴关键字：left=0%、right=100%；y 轴关键字：top=0%、
 *          bottom=100%；center=50%（轴中立）；关键字结果按百分比语义
 *          输出（对应 xIsPercent/yIsPercent=true）；
 *        - 数值分量：经 XCssParseLengthEx 解析（px/pt/em/ex 数值原样
 *          输出且百分位标志 false；% 百分比去 % 号输出且标志 true）。
 *          超集登记：Qt parseAlignment 只收 KnownIdentifier 关键字，
 *          数值分量为本库超集（Qt QSS 中数值定位需经其他值路径）；
 *        - 单分量：关键字自归其轴，数值/center 归 x 轴；缺省轴取
 *          center（50%，百分比语义）；
 *        - 双分量：按轴归属装配——x/y 轴关键字各归其轴（"top left" 与
 *          "left top" 等价，对标 CSS3 关键字无序），数值/center 按 x→y
 *          顺序落第一个未定轴（"top 30px" ⇒ y=0%、x=30px）；轴冲突
 *          （如 "left right"）与三分量以上拒绝；
 *        - 失败时四个输出均不写。
 *
 * @param value 定位文本（如 "center top"、"10px 50%"）。
 * @param outX 输出 x 分量数值（百分比已去 % 号；失败时不写）。
 * @param outY 输出 y 分量数值（同上）。
 * @param xIsPercent 输出 x 是否百分比语义（失败时不写）。
 * @param yIsPercent 输出 y 是否百分比语义（失败时不写）。
 * @return 解析成功返回 true。
 */
bool XCssParseBackgroundPosition(const char* value, double* outX, double* outY, bool* xIsPercent, bool* yIsPercent);

#endif /* XSTYLE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XCSSVALUE_H */
