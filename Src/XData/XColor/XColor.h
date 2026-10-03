/******************************************************************************
 * @file       XColor.h
 * @brief      XColor 颜色类（对标 Qt 6.8 QColor）
 * @author     XinYueC 团队
 * @details    对齐 Qt 6.8.3 QColor（qcolor.h/qcolor.cpp）：按规格位存储颜色
 *             分量（RGB/HSV/CMYK/HSL 五视图），分量统一用 16 位无符号整数
 *             表示；支持 148 项 CSS/SVG 命名颜色与 #RGB 族十六进制解析。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他
 *             平台 API。
 ******************************************************************************/
#ifndef XCOLOR_H
#define XCOLOR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XString.h"
#include "XStringList.h"

/* ========== 颜色规格枚举（对标 Qt 6.8.3 QColor::Spec，qcolor.h:35） ========== */

/**
 * @brief      颜色规格枚举
 * @note       表示颜色以哪个色彩空间指定；枚举值序与 QColor::Spec 同构。
 */
typedef enum XColor_Spec
{
    XColor_Invalid = 0,     /**< 无效颜色 */
    XColor_Rgb = 1,         /**< RGB 色彩空间 */
    XColor_Hsv = 2,         /**< HSV 色彩空间 */
    XColor_Cmyk = 3,        /**< CMYK 色彩空间 */
    XColor_Hsl = 4,         /**< HSL 色彩空间 */
    XColor_ExtendedRgb = 5  /**< 扩展 RGB 色彩空间（声明边界：本库不实现，
                                 u16×5 布局无法承载 F16 宽色域，仅保留枚举
                                 边界对齐 QColor::Spec；任何 API 都不会产出
                                 该规格的实例，请求它一律落无效色） */
} XColor_Spec;

/* ========== 颜色名称格式枚举（对标 Qt 6.8.3 QColor::NameFormat，qcolor.h:36） ========== */

/**
 * @brief      颜色名称格式枚举
 * @note       指定 toHexString() 输出的格式。
 */
typedef enum XColor_NameFormat
{
    XColor_HexRgb = 0,  /**< #RRGGBB 格式（6 位十六进制） */
    XColor_HexArgb = 1  /**< #AARRGGBB 格式（8 位十六进制） */
} XColor_NameFormat;

/* ========== XColor 结构体 ========== */

/**
 * @brief      XColor 颜色结构体（对标 Qt 6.8.3 QColor 的 CT 联合，qcolor.h:215-257）
 * @note       按规格位存储：每个分量通道以 0~65535 的 16 位无符号整数表示，
 *             各规格把 m_comp1..4 解释为自己的空间分量（对标 QColor::CT
 *             的 argb/ahsv/acmyk/ahsl 视图）；0~255 的整数经 ×0x101 复制式
 *             扩展入存储，0.0~1.0 的浮点经 qRound(f×65535) 入存储：
 *             - Rgb：m_comp1=R、m_comp2=G、m_comp3=B、m_comp4=0（填充位）；
 *             - Hsv：m_comp1=色相（单位百分之一度，0~36000；灰色等无定义
 *               色相存 65535 哨兵，读出为 -1）、m_comp2=饱和度、
 *               m_comp3=明度、m_comp4=0（填充位）；
 *             - Cmyk：m_comp1=青、m_comp2=品红、m_comp3=黄、m_comp4=黑；
 *             - Hsl：m_comp1=色相（同 Hsv 编码与哨兵）、m_comp2=饱和度、
 *               m_comp3=亮度、m_comp4=0（填充位）；
 *             - Invalid：m_alpha=65535、m_comp1..4=0（对标 invalidate()，
 *               qcolor.cpp:2930-2938）。
 *             m_alpha 恒 0~65535，各规格通用。
 */
typedef struct XColor
{
    XColor_Spec m_spec;     /**< 颜色规格（色彩空间类型，禁止调用者直接改写） */
    uint16_t m_alpha;       /**< Alpha 通道值（0~65535，0=透明，65535=不透明） */
    uint16_t m_comp1;       /**< 分量1：R / 色相(百分之一度或 65535 哨兵) / C / 色相 */
    uint16_t m_comp2;       /**< 分量2：G / 饱和度 / M / 饱和度 */
    uint16_t m_comp3;       /**< 分量3：B / 明度 / Y / 亮度 */
    uint16_t m_comp4;       /**< 分量4：0(填充) / 0(填充) / K / 0(填充) */
} XColor;

/* ========== 创建与初始化函数 ========== */

/**
 * @brief      创建一个无效的 XColor 对象
 * @return     无效的 XColor 对象（m_spec = XColor_Invalid、m_alpha = 65535、
 *             分量全 0）
 * @note       对齐 `QColor()` 默认构造 + `QColor::invalidate()`
 *             （qcolor.cpp:2930-2938）。
 */
XColor XColor_create(void);

/**
 * @brief      使用整数 RGB 值创建 XColor 对象
 * @param r    红色分量（0~255）
 * @param g    绿色分量（0~255）
 * @param b    蓝色分量（0~255）
 * @param a    Alpha 通道（0~255）
 * @return     如果 RGB 值全部有效则返回对应的 XColor 对象，否则输出警告并
 *             返回无效颜色
 * @note       对齐 `QColor(int,int,int,int)` / `QColor::fromRgb(int,int,int,int)`
 *             （qcolor.cpp:2397-2412）：任一参数越 0~255 即整组拒绝。
 */
XColor XColor_create_rgb(int r, int g, int b, int a);

/**
 * @brief      使用浮点 RGB 值创建 XColor 对象
 * @param r    红色分量（0.0~1.0）
 * @param g    绿色分量（0.0~1.0）
 * @param b    蓝色分量（0.0~1.0）
 * @param a    Alpha 通道（0.0~1.0）
 * @return     alpha 越界时输出警告并返回无效颜色；r/g/b 越界时输出警告并
 *             钳位到 [0,1] 后按 Rgb 色返回（偏差声明：Qt 转 ExtendedRgb，
 *             本库按声明边界不支持该规格）
 * @note       对齐 `QColor::fromRgbF(float,float,float,float)`
 *             （qcolor.cpp:2425-2453）：分量直接按 qRound(f×65535) 落 16 位
 *             存储，不经 8 位中转。
 */
XColor XColor_create_rgbF(float r, float g, float b, float a);

/**
 * @brief      使用整数值的 HSV 值创建 XColor 对象
 * @param h    色相：-1 表示无定义色相（灰色），其余合法域为 [0,360)
 * @param s    饱和度（0~255）
 * @param v    明度（0~255）
 * @param a    Alpha 通道（0~255）
 * @return     参数合法返回 Hsv 规格的 XColor 对象（直接按规格位存储，不转
 *             RGB），否则输出警告并返回无效颜色
 * @note       对齐 `QColor::fromHsv(int,int,int,int)`（qcolor.cpp:2497-2515）：
 *             h=-1 存 65535 哨兵、读出 -1；h≥360 拒绝（与 setHsv 的回卷
 *             口径不同，两族不得混同）。
 */
XColor XColor_create_hsv(int h, int s, int v, int a);

/**
 * @brief      使用浮点 HSV 值创建 XColor 对象
 * @param h    色相：-1.0 表示无定义色相（灰色），其余合法域为 [0.0,1.0]
 * @param s    饱和度（0.0~1.0）
 * @param v    明度（0.0~1.0）
 * @param a    Alpha 通道（0.0~1.0）
 * @return     参数合法返回 Hsv 规格的 XColor 对象，否则输出警告并返回无效
 *             颜色
 * @note       对齐 `QColor::fromHsvF(float,float,float,float)`
 *             （qcolor.cpp:2528-2546）：色相按 qRound(h×36000) 落存储，
 *             h=1.0 得 36000（toRgb 按 0 处理、hsvHue 读出 360，忠实保留
 *             Qt 原样怪癖）。
 */
XColor XColor_create_hsvF(float h, float s, float v, float a);

/**
 * @brief      使用整数 CMYK 值创建 XColor 对象
 * @param c    青色（0~255）
 * @param m    品红（0~255）
 * @param y    黄色（0~255）
 * @param k    黑色（0~255）
 * @param a    Alpha 通道（0~255）
 * @return     参数合法返回 Cmyk 规格的 XColor 对象（直接按规格位存储，不转
 *             RGB），否则输出警告并返回无效颜色
 * @note       对齐 `QColor::fromCmyk(int,int,int,int,int)`
 *             （qcolor.cpp:2739-2758）。
 */
XColor XColor_create_cmyk(int c, int m, int y, int k, int a);

/**
 * @brief      使用浮点 CMYK 值创建 XColor 对象
 * @param c    青色（0.0~1.0）
 * @param m    品红（0.0~1.0）
 * @param y    黄色（0.0~1.0）
 * @param k    黑色（0.0~1.0）
 * @param a    Alpha 通道（0.0~1.0）
 * @return     参数合法返回 Cmyk 规格的 XColor 对象，否则输出警告并返回无效
 *             颜色
 * @note       对齐 `QColor::fromCmykF(float,float,float,float,float)`
 *             （qcolor.cpp:2771-2790）。
 */
XColor XColor_create_cmykF(float c, float m, float y, float k, float a);

/**
 * @brief      使用整数 HSL 值创建 XColor 对象
 * @param h    色相：-1 表示无定义色相（灰色），其余合法域为 [0,360)
 * @param s    饱和度（0~255）
 * @param l    亮度（0~255）
 * @param a    Alpha 通道（0~255）
 * @return     参数合法返回 Hsl 规格的 XColor 对象（直接按规格位存储，不转
 *             RGB），否则输出警告并返回无效颜色
 * @note       对齐 `QColor::fromHsl(int,int,int,int)`（qcolor.cpp:2560-2578）：
 *             h≥360 拒绝（与 setHsl 的回卷口径不同，两族不得混同）。
 */
XColor XColor_create_hsl(int h, int s, int l, int a);

/**
 * @brief      使用浮点 HSL 值创建 XColor 对象
 * @param h    色相：-1.0 表示无定义色相（灰色），其余合法域为 [0.0,1.0]
 * @param s    饱和度（0.0~1.0）
 * @param l    亮度（0.0~1.0）
 * @param a    Alpha 通道（0.0~1.0）
 * @return     参数合法返回 Hsl 规格的 XColor 对象，否则输出警告并返回无效
 *             颜色
 * @note       对齐 `QColor::fromHslF(float,float,float,float)`
 *             （qcolor.cpp:2592-2612）：色相 qRound(h×36000) 后 ==36000 回卷
 *             为 0（fromHsvF 无此回卷——忠实保留 Qt 的不对称）。
 */
XColor XColor_create_hslF(float h, float s, float l, float a);

/**
 * @brief      使用 32 位 RGB 值创建 XColor 对象
 * @param rgb  0x00RRGGBB 格式值；高 8 位（alpha）忽略
 * @return     对应的 XColor 对象，alpha 恒为 255（不透明）
 * @note       对齐 `QColor(QRgb)`（qcolor.cpp:728-736）/`QColor::fromRgb(QRgb)`
 *             （qcolor.cpp:2367-2370）：入参 alpha 位忽略、alpha 置全不透明。
 */
XColor XColor_create_rgba(uint32_t rgb);

/**
 * @brief      使用 32 位 ARGB 值创建 XColor 对象（含 alpha）
 * @param argb 0xAARRGGBB 格式值
 * @return     对应的 XColor 对象
 * @note       对齐 `QColor::fromRgba(QRgb)`（qcolor.cpp:2383-2386）。
 */
XColor XColor_create_argb(uint32_t argb);

/**
 * @brief      XColor 初始化函数
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @note       初始化为无效颜色（同 XColor_create）。
 */
void XColor_init(XColor* self);

/**
 * @brief      XColor 初始化函数（RGB）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param r    红色分量（0~255）
 * @param g    绿色分量（0~255）
 * @param b    蓝色分量（0~255）
 * @param a    Alpha 通道（0~255）
 * @note       等效 *self = XColor_create_rgb(r, g, b, a)。
 */
void XColor_init_rgb(XColor* self, int r, int g, int b, int a);

/* ========== 查询方法 ========== */

/**
 * @brief      判断颜色是否有效
 * @param self 目标 XColor 对象指针；NULL 按"调用者没有提供对象"处理，返回
 *             false
 * @return     有效返回 true，无效返回 false
 * @note       对齐 `QColor::isValid()`（cspec != Invalid）。
 */
bool XColor_isValid(const XColor* self);

/**
 * @brief      获取颜色规格（色彩空间类型）
 * @param self 目标 XColor 对象指针；NULL 返回 XColor_Invalid
 * @return     颜色规格枚举值
 * @note       对齐 `QColor::spec()`。
 */
XColor_Spec XColor_spec(const XColor* self);

/**
 * @brief      将颜色转换为十六进制字符串（"#RRGGBB" 或 "#AARRGGBB"）
 * @param self   目标 XColor 对象指针；NULL 返回空字符串（C 护栏）
 * @param format 名称格式（HexRgb 或 HexArgb）
 * @return       颜色名称字符串（新建 XString，由调用者释放）
 * @note       对齐 `QColor::name(QColor::NameFormat)`（qcolor.cpp:832-842）。
 *             无效颜色不特判：按无效存储（alpha=65535、分量全 0）换算，
 *             输出 "#000000" / "#ff000000"（忠实保留 Qt 口径）。
 */
XString XColor_toHexString(const XColor* self, XColor_NameFormat format);

/**
 * @brief      从字符串名称解析颜色
 * @param name 颜色名称字符串；NULL 或空串返回无效颜色
 * @return     解析成功返回对应的 XColor 对象，否则返回无效颜色
 * @note       对齐 `QColor::fromString`（qcolor.cpp:978-993）：'#' 前缀走
 *             十六进制变体（#RGB / #RRGGBB / #AARRGGBB / #RRRGGGBBB /
 *             #RRRRGGGGBBBB，任一非十六进制字符即整体无效；'#' 前缀解析
 *             失败不回退名称查找）；非 '#' 前缀走命名色查找（查找前剔除
 *             全部空格与制表符、ASCII 小写化、长度上限 255）。
 */
XColor XColor_fromString(const XString* name);

/**
 * @brief      用命名颜色设置颜色（XString 主版本）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param name 颜色名称字符串；解析失败时对象被置为无效颜色
 * @note       对齐 `QColor::setNamedColor`（qcolor.cpp:871-874，Qt 6.6 起
 *             标废弃、推荐 fromString）。Qt 废弃状态如实声明；XinYueC 保留
 *             本入口与 fromString 组成孪生 API 对齐。算法即
 *             *self = XColor_fromString(name)。
 */
void XColor_setNamedColor(XColor* self, const XString* name);

/**
 * @brief      用命名颜色设置颜色（UTF-8 兼容重载）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param name 颜色名称字符串，按 UTF-8 解码；NULL 按无效名称处理（对象置
 *             为无效颜色）
 * @note       对齐 `QColor::setNamedColor` 重载族；内部建临时 XString 转发
 *             XColor_setNamedColor，不维护第二份字符串状态。
 */
void XColor_setNamedColor_2(XColor* self, const char* name);

/**
 * @brief      判断字符串是否是可解析的颜色名称（XString 主版本）
 * @param name 颜色名称字符串；NULL 返回 false
 * @return     可被 XColor_fromString 解析出有效颜色时返回 true
 * @note       对齐 `QColor::isValidColorName`（qcolor.cpp:948-951），实现
 *             即 XColor_fromString(name).isValid()。
 */
bool XColor_isValidColorName(const XString* name);

/**
 * @brief      判断字符串是否是可解析的颜色名称（UTF-8 兼容重载）
 * @param name 颜色名称字符串，按 UTF-8 解码；NULL 返回 false
 * @return     可被 XColor_fromString 解析出有效颜色时返回 true
 * @note       对齐 `QColor::isValidColorName`；内部建临时 XString 转发
 *             XColor_isValidColorName。
 */
bool XColor_isValidColorName_2(const char* name);

/* ========== RGB 分量访问 ========== */

/**
 * @brief      获取 Alpha 通道值（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     Alpha 通道值（qt_div_257 四舍五入除法）
 * @note       对齐 `QColor::alpha()`（qcolor.cpp:1464-1469；ExtendedRgb
 *             分支按声明边界不实现）。
 */
int XColor_alpha(const XColor* self);

/**
 * @brief      设置 Alpha 通道值
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param a    Alpha 通道值（0~255）
 * @note       对齐 `QColor::setAlpha`（qcolor.cpp:1479-1488）：越界输出
 *             警告并钳位到 [0,255] 后照常写入（"警告+钳位"档）。
 */
void XColor_setAlpha(XColor* self, int a);

/**
 * @brief      获取浮点 Alpha 通道值（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点 Alpha 通道值（m_alpha / 65535.0f 直读）
 * @note       对齐 `QColor::alphaF()`（qcolor.cpp:1495-1499）。
 */
float XColor_alphaF(const XColor* self);

/**
 * @brief      设置浮点 Alpha 通道值
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param a    浮点 Alpha 通道值（0.0~1.0）
 * @note       对齐 `QColor::setAlphaF`（qcolor.cpp:1509-1518）：越界输出
 *             警告并钳位到 [0,1] 后照常写入；存储值为 qRound(a×65535)。
 */
void XColor_setAlphaF(XColor* self, float a);

/**
 * @brief      获取红色分量（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     红色分量值；非 Rgb 规格（Invalid 除外）先转 RGB 再读，无效
 *             颜色直读存储分量（得 0）
 * @note       对齐 `QColor::red()`（qcolor.cpp:1526-1531）。
 */
int XColor_red(const XColor* self);

/**
 * @brief      获取绿色分量（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     绿色分量值；非 Rgb 规格（Invalid 除外）先转 RGB 再读，无效
 *             颜色直读存储分量（得 0）
 * @note       对齐 `QColor::green()`（qcolor.cpp:1553-1558）。
 */
int XColor_green(const XColor* self);

/**
 * @brief      获取蓝色分量（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     蓝色分量值；非 Rgb 规格（Invalid 除外）先转 RGB 再读，无效
 *             颜色直读存储分量（得 0）
 * @note       对齐 `QColor::blue()`（qcolor.cpp:1581-1586）。
 */
int XColor_blue(const XColor* self);

/**
 * @brief      设置红色分量
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param r    红色分量值（0~255）
 * @note       对齐 `QColor::setRed`（qcolor.cpp:1539-1546）：越界输出警告
 *             并钳位到 [0,255] 后照常写入。非 Rgb 规格（含无效色）时等效
 *             setRgb(r, green(), blue(), alpha())，规格随之改判为 Rgb；
 *             对无效色 green()/blue() 读 0、alpha() 读 255，即结果为
 *             (r,0,0,255) 的 Rgb 色（忠实保留 Qt 怪癖）。
 */
void XColor_setRed(XColor* self, int r);

/**
 * @brief      设置绿色分量
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param g    绿色分量值（0~255）
 * @note       对齐 `QColor::setGreen`（qcolor.cpp:1566-1573）：越界输出警告
 *             并钳位到 [0,255] 后照常写入；非 Rgb 规格（含无效色）时等效
 *             setRgb(red(), g, blue(), alpha()) 并改判为 Rgb（无效色读出
 *             (0,0,255)）。
 */
void XColor_setGreen(XColor* self, int g);

/**
 * @brief      设置蓝色分量
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param b    蓝色分量值（0~255）
 * @note       对齐 `QColor::setBlue`（qcolor.cpp:1595-1602）：越界输出警告
 *             并钳位到 [0,255] 后照常写入；非 Rgb 规格（含无效色）时等效
 *             setRgb(red(), green(), b, alpha()) 并改判为 Rgb（无效色读出
 *             (0,0,255)）。
 */
void XColor_setBlue(XColor* self, int b);

/**
 * @brief      获取浮点红色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点红色分量值（存储值 / 65535.0f 直读，非 Rgb 规格先转 RGB）
 * @note       对齐 `QColor::redF()`（qcolor.cpp:1609-1617）。
 */
float XColor_redF(const XColor* self);

/**
 * @brief      获取浮点绿色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点绿色分量值（存储值 / 65535.0f 直读，非 Rgb 规格先转 RGB）
 * @note       对齐 `QColor::greenF()`（qcolor.cpp:1641-1649）。
 */
float XColor_greenF(const XColor* self);

/**
 * @brief      获取浮点蓝色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点蓝色分量值（存储值 / 65535.0f 直读，非 Rgb 规格先转 RGB）
 * @note       对齐 `QColor::blueF()`（qcolor.cpp:1673-1681）。
 */
float XColor_blueF(const XColor* self);

/**
 * @brief      设置浮点红色分量
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param r    浮点红色分量值（0.0~1.0）
 * @note       对齐 `QColor::setRedF`（qcolor.cpp:1626-1634）：Rgb 规格且域
 *             内时单通道直写（qRound(r×65535)）；否则走 setRgbF 路径整组
 *             重写（越界输出警告并钳位保持 Rgb）。
 */
void XColor_setRedF(XColor* self, float r);

/**
 * @brief      设置浮点绿色分量
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param g    浮点绿色分量值（0.0~1.0）
 * @note       对齐 `QColor::setGreenF`（qcolor.cpp:1658-1666）：Rgb 规格且
 *             域内时单通道直写；否则走 setRgbF 路径整组重写。
 */
void XColor_setGreenF(XColor* self, float g);

/**
 * @brief      设置浮点蓝色分量
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param b    浮点蓝色分量值（0.0~1.0）
 * @note       对齐 `QColor::setBlueF`（qcolor.cpp:1688-1696）：Rgb 规格且
 *             域内时单通道直写；否则走 setRgbF 路径整组重写。
 */
void XColor_setBlueF(XColor* self, float b);

/**
 * @brief      获取所有 RGB 分量（整数）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param r    输出红色分量指针（可为 NULL，跳过）
 * @param g    输出绿色分量指针（可为 NULL，跳过）
 * @param b    输出蓝色分量指针（可为 NULL，跳过）
 * @param a    输出 Alpha 通道指针（可为 NULL，跳过）
 * @note       对齐 `QColor::getRgb`（qcolor.cpp:1285-1301）：非 Rgb 规格
 *             （Invalid 除外）先转 RGB 再读；无效颜色照读存储得
 *             (0,0,0,255)。
 */
void XColor_getRgb(const XColor* self, int* r, int* g, int* b, int* a);

/**
 * @brief      设置所有 RGB 分量（整数）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param r    红色分量（0~255）
 * @param g    绿色分量（0~255）
 * @param b    蓝色分量（0~255）
 * @param a    Alpha 通道（0~255）
 * @note       对齐 `QColor::setRgb(int,int,int,int)`（qcolor.cpp:1348-1362）：
 *             任一参数越界输出警告并将整色置为无效。
 */
void XColor_setRgb(XColor* self, int r, int g, int b, int a);

/**
 * @brief      获取所有 RGB 分量（浮点）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param r    输出浮点红色分量指针（可为 NULL，跳过）
 * @param g    输出浮点绿色分量指针（可为 NULL，跳过）
 * @param b    输出浮点蓝色分量指针（可为 NULL，跳过）
 * @param a    输出浮点 Alpha 通道指针（可为 NULL，跳过）
 * @note       对齐 `QColor::getRgbF`（qcolor.cpp:1250-1273）：逐通道按存储
 *             值 / 65535.0f 直读，16 位精度不经 8 位中转；非 Rgb 规格
 *             （Invalid 除外）先转 RGB。
 */
void XColor_getRgbF(const XColor* self, float* r, float* g, float* b, float* a);

/**
 * @brief      设置所有 RGB 分量（浮点）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param r    浮点红色分量（0.0~1.0）
 * @param g    浮点绿色分量（0.0~1.0）
 * @param b    浮点蓝色分量（0.0~1.0）
 * @param a    浮点 Alpha 通道（0.0~1.0）
 * @note       对齐 `QColor::setRgbF`（qcolor.cpp:1315-1339）：alpha 越界
 *             输出警告并将整色置为无效；r/g/b 越界输出警告并钳位到 [0,1]
 *             后按 Rgb 规格写入（偏差声明：Qt 转 ExtendedRgb，本库按声明
 *             边界不支持该规格）。
 */
void XColor_setRgbF(XColor* self, float r, float g, float b, float a);

/* ========== HSV 分量访问 ========== */

/**
 * @brief      获取 HSV 色相
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     色相（0~359）；无定义色相（灰色）返回 -1
 * @note       对齐 `QColor::hue()`（qcolor.cpp:1706-1709），即 hsvHue()。
 */
int XColor_hue(const XColor* self);

/**
 * @brief      获取 HSV 饱和度（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     HSV 饱和度值；非 Hsv 规格（Invalid 除外）先转 HSV 再读
 * @note       对齐 `QColor::saturation()`（qcolor.cpp:1732-1735），即
 *             hsvSaturation()。
 */
int XColor_saturation(const XColor* self);

/**
 * @brief      获取 HSV 色相
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     色相（0~359）；无定义色相（灰色）返回 -1（存储 65535 哨兵）；
 *             非 Hsv 规格（Invalid 除外）先转 HSV 再读；hsvHue 读出为
 *             存储值整除 100（36000 读出 360，忠实保留 Qt 怪癖）
 * @note       对齐 `QColor::hsvHue()`（qcolor.cpp:1716-1721）。
 */
int XColor_hsvHue(const XColor* self);

/**
 * @brief      获取 HSV 饱和度（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     HSV 饱和度值；非 Hsv 规格（Invalid 除外）先转 HSV 再读
 * @note       对齐 `QColor::hsvSaturation()`（qcolor.cpp:1742-1747）。
 */
int XColor_hsvSaturation(const XColor* self);

/**
 * @brief      获取 HSV 明度（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     明度值；非 Hsv 规格（Invalid 除外）先转 HSV 再读
 * @note       对齐 `QColor::value()`（qcolor.cpp:1754-1759）。
 */
int XColor_value(const XColor* self);

/**
 * @brief      获取浮点 HSV 色相（0.0~1.0 轮分数）
 * @param self 目标 XColor 对象指针；NULL 返回 -1.0f
 * @return     色相轮分数（存储值 / 36000.0f）；无定义色相返回 -1.0f
 * @note       对齐 `QColor::hueF()` / `QColor::hsvHueF()`（qcolor.cpp:1768-1784）。
 *             返回 0.0~1.0 的轮分数（实现口径），不是 0~360 整数度——
 *             qcolor.h:119-123 的 "0.0 <= hueF < 360.0" 注释系 Qt 陈旧文档
 *             错，以实现为准。
 */
float XColor_hueF(const XColor* self);

/**
 * @brief      获取浮点 HSV 饱和度（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点饱和度值（存储值 / 65535.0f 直读）
 * @note       对齐 `QColor::saturationF()`（qcolor.cpp:1794-1797），即
 *             hsvSaturationF()。
 */
float XColor_saturationF(const XColor* self);

/**
 * @brief      获取浮点 HSV 色相（0.0~1.0 轮分数）
 * @param self 目标 XColor 对象指针；NULL 返回 -1.0f
 * @return     色相轮分数；无定义色相（灰色）返回 -1.0f；非 Hsv 规格
 *             （Invalid 除外）先转 HSV 再读
 * @note       对齐 `QColor::hsvHueF()`（qcolor.cpp:1779-1784）。
 */
float XColor_hsvHueF(const XColor* self);

/**
 * @brief      获取浮点 HSV 饱和度（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点饱和度值；非 Hsv 规格（Invalid 除外）先转 HSV 再读
 * @note       对齐 `QColor::hsvSaturationF()`（qcolor.cpp:1804-1809）。
 */
float XColor_hsvSaturationF(const XColor* self);

/**
 * @brief      获取浮点 HSV 明度（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点明度值；非 Hsv 规格（Invalid 除外）先转 HSV 再读
 * @note       对齐 `QColor::valueF()`（qcolor.cpp:1816-1821）。
 */
float XColor_valueF(const XColor* self);

/**
 * @brief      获取所有 HSV 分量（整数）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param h    输出色相指针（可为 NULL，跳过）；灰色输出 -1
 * @param s    输出饱和度指针（可为 NULL，跳过）
 * @param v    输出明度指针（可为 NULL，跳过）
 * @param a    输出 Alpha 通道指针（可为 NULL，跳过）
 * @note       对齐 `QColor::getHsv`（qcolor.cpp:1043-1059）：非 Hsv 规格
 *             （Invalid 除外）先转 HSV 再读；无效颜色照读存储。
 */
void XColor_getHsv(const XColor* self, int* h, int* s, int* v, int* a);

/**
 * @brief      设置所有 HSV 分量（整数）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param h    色相：-1 表示无定义色相（灰色，存 65535 哨兵）；其余 ≥-1，
 *             ≥360 时按 (h%360)×100 回卷（工厂族 create_hsv 则拒绝 h≥360，
 *             两族口径不同）
 * @param s    饱和度（0~255）
 * @param v    明度（0~255）
 * @param a    Alpha 通道（0~255）
 * @note       对齐 `QColor::setHsv`（qcolor.cpp:1097-1111）：h < -1 或
 *             s/v/a 越界输出警告并将整色置为无效。
 */
void XColor_setHsv(XColor* self, int h, int s, int v, int a);

/**
 * @brief      获取所有 HSV 分量（浮点）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param h    输出浮点色相指针（可为 NULL，跳过）；灰色输出 -1.0f
 * @param s    输出浮点饱和度指针（可为 NULL，跳过）
 * @param v    输出浮点明度指针（可为 NULL，跳过）
 * @param a    输出浮点 Alpha 指针（可为 NULL，跳过）
 * @note       对齐 `QColor::getHsvF`（qcolor.cpp:1015-1031）：逐通道按存储
 *             值 / 65535.0f 直读；色相 / 36000.0f。
 */
void XColor_getHsvF(const XColor* self, float* h, float* s, float* v, float* a);

/**
 * @brief      设置所有 HSV 分量（浮点）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param h    浮点色相：-1.0 表示无定义色相（灰色，存 65535 哨兵）；其余
 *             合法域为 [0.0,1.0]（h=1.0 存 36000，读出怪癖见 XColor_hsvHue）
 * @param s    浮点饱和度（0.0~1.0）
 * @param v    浮点明度（0.0~1.0）
 * @param a    浮点 Alpha（0.0~1.0）
 * @note       对齐 `QColor::setHsvF`（qcolor.cpp:1069-1086）：h ∉[0,1] 且
 *             ≠-1.0，或 s/v/a 越界时输出警告并将整色置为无效。
 */
void XColor_setHsvF(XColor* self, float h, float s, float v, float a);

/* ========== CMYK 分量访问 ========== */

/**
 * @brief      获取青色分量（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     青色分量值；非 Cmyk 规格（Invalid 除外）先转 CMYK 再读
 * @note       对齐 `QColor::cyan()`（qcolor.cpp:1912-1917）。
 */
int XColor_cyan(const XColor* self);

/**
 * @brief      获取品红分量（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     品红分量值；非 Cmyk 规格（Invalid 除外）先转 CMYK 再读
 * @note       对齐 `QColor::magenta()`（qcolor.cpp:1924-1929）。
 */
int XColor_magenta(const XColor* self);

/**
 * @brief      获取黄色分量（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     黄色分量值；非 Cmyk 规格（Invalid 除外）先转 CMYK 再读
 * @note       对齐 `QColor::yellow()`（qcolor.cpp:1936-1941）。
 */
int XColor_yellow(const XColor* self);

/**
 * @brief      获取黑色分量（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     黑色分量值；非 Cmyk 规格（Invalid 除外）先转 CMYK 再读
 * @note       对齐 `QColor::black()`（qcolor.cpp:1949-1954）。
 */
int XColor_black(const XColor* self);

/**
 * @brief      获取浮点青色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点青色分量值（存储值 / 65535.0f 直读）
 * @note       对齐 `QColor::cyanF()`（qcolor.cpp:1961-1966）。
 */
float XColor_cyanF(const XColor* self);

/**
 * @brief      获取浮点品红分量（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点品红分量值（存储值 / 65535.0f 直读）
 * @note       对齐 `QColor::magentaF()`（qcolor.cpp:1973-1978）。
 */
float XColor_magentaF(const XColor* self);

/**
 * @brief      获取浮点黄色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点黄色分量值（存储值 / 65535.0f 直读）
 * @note       对齐 `QColor::yellowF()`（qcolor.cpp:1985-1990）。
 */
float XColor_yellowF(const XColor* self);

/**
 * @brief      获取浮点黑色分量（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点黑色分量值（存储值 / 65535.0f 直读）
 * @note       对齐 `QColor::blackF()`（qcolor.cpp:1997-2002）。
 */
float XColor_blackF(const XColor* self);

/**
 * @brief      获取所有 CMYK 分量（整数）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param c    输出青色指针（可为 NULL，跳过）
 * @param m    输出品红指针（可为 NULL，跳过）
 * @param y    输出黄色指针（可为 NULL，跳过）
 * @param k    输出黑色指针（可为 NULL，跳过）
 * @param a    输出 Alpha 指针（可为 NULL，跳过）
 * @note       对齐 `QColor::getCmyk`（qcolor.cpp:2624-2641）：非 Cmyk 规格
 *             （Invalid 除外）先转 CMYK 再读；无效颜色照读存储。
 */
void XColor_getCmyk(const XColor* self, int* c, int* m, int* y, int* k, int* a);

/**
 * @brief      设置所有 CMYK 分量（整数）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param c    青色（0~255）
 * @param m    品红（0~255）
 * @param y    黄色（0~255）
 * @param k    黑色（0~255）
 * @param a    Alpha 通道（0~255）
 * @note       对齐 `QColor::setCmyk`（qcolor.cpp:2680-2698）：任一参数越界
 *             输出警告并将整色置为无效。
 */
void XColor_setCmyk(XColor* self, int c, int m, int y, int k, int a);

/**
 * @brief      获取所有 CMYK 分量（浮点）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param c    输出浮点青色指针（可为 NULL，跳过）
 * @param m    输出浮点品红指针（可为 NULL，跳过）
 * @param y    输出浮点黄色指针（可为 NULL，跳过）
 * @param k    输出浮点黑色指针（可为 NULL，跳过）
 * @param a    输出浮点 Alpha 指针（可为 NULL，跳过）
 * @note       对齐 `QColor::getCmykF`（qcolor.cpp:2653-2670）：逐通道按
 *             存储值 / 65535.0f 直读。
 */
void XColor_getCmykF(const XColor* self, float* c, float* m, float* y, float* k, float* a);

/**
 * @brief      设置所有 CMYK 分量（浮点）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param c    浮点青色（0.0~1.0）
 * @param m    浮点品红（0.0~1.0）
 * @param y    浮点黄色（0.0~1.0）
 * @param k    浮点黑色（0.0~1.0）
 * @param a    浮点 Alpha（0.0~1.0）
 * @note       对齐 `QColor::setCmykF`（qcolor.cpp:2710-2728）：任一参数越界
 *             输出警告并将整色置为无效。
 */
void XColor_setCmykF(XColor* self, float c, float m, float y, float k, float a);

/* ========== HSL 分量访问 ========== */

/**
 * @brief      获取 HSL 色相
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     色相（0~359）；无定义色相（灰色）返回 -1；非 Hsl 规格先转
 *             HSL 再读
 * @note       对齐 `QColor::hslHue()`（qcolor.cpp:1830-1835）。
 */
int XColor_hslHue(const XColor* self);

/**
 * @brief      获取 HSL 饱和度（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     HSL 饱和度值；非 Hsl 规格（Invalid 除外）先转 HSL 再读
 * @note       对齐 `QColor::hslSaturation()`（qcolor.cpp:1844-1849）。
 */
int XColor_hslSaturation(const XColor* self);

/**
 * @brief      获取 HSL 亮度（0~255）
 * @param self 目标 XColor 对象指针；NULL 返回 0
 * @return     亮度值；非 Hsl 规格（Invalid 除外）先转 HSL 再读
 * @note       对齐 `QColor::lightness()`（qcolor.cpp:1858-1863）。
 */
int XColor_lightness(const XColor* self);

/**
 * @brief      获取浮点 HSL 色相（0.0~1.0 轮分数）
 * @param self 目标 XColor 对象指针；NULL 返回 -1.0f
 * @return     色相轮分数（存储值 / 36000.0f）；无定义色相（灰色）返回
 *             -1.0f；非 Hsl 规格（Invalid 除外）先转 HSL 再读
 * @note       对齐 `QColor::hslHueF()`（qcolor.cpp:1872-1877），口径同
 *             XColor_hueF。
 */
float XColor_hslHueF(const XColor* self);

/**
 * @brief      获取浮点 HSL 饱和度（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点饱和度值；非 Hsl 规格（Invalid 除外）先转 HSL 再读
 * @note       对齐 `QColor::hslSaturationF()`（qcolor.cpp:1886-1891）。
 */
float XColor_hslSaturationF(const XColor* self);

/**
 * @brief      获取浮点 HSL 亮度（0.0~1.0）
 * @param self 目标 XColor 对象指针；NULL 返回 0.0f
 * @return     浮点亮度值；非 Hsl 规格（Invalid 除外）先转 HSL 再读
 * @note       对齐 `QColor::lightnessF()`（qcolor.cpp:1900-1905）。
 */
float XColor_lightnessF(const XColor* self);

/**
 * @brief      获取所有 HSL 分量（整数）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param h    输出色相指针（可为 NULL，跳过）；灰色输出 -1
 * @param s    输出饱和度指针（可为 NULL，跳过）
 * @param l    输出亮度指针（可为 NULL，跳过）
 * @param a    输出 Alpha 指针（可为 NULL，跳过）
 * @note       对齐 `QColor::getHsl`（qcolor.cpp:1155-1171）：非 Hsl 规格
 *             （Invalid 除外）先转 HSL 再读；无效颜色照读存储。
 */
void XColor_getHsl(const XColor* self, int* h, int* s, int* l, int* a);

/**
 * @brief      设置所有 HSL 分量（整数）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param h    色相：-1 表示无定义色相（灰色，存 65535 哨兵）；其余 ≥-1，
 *             ≥360 时按 (h%360)×100 回卷（工厂族 create_hsl 则拒绝 h≥360，
 *             两族口径不同）
 * @param s    饱和度（0~255）
 * @param l    亮度（0~255）
 * @param a    Alpha 通道（0~255）
 * @note       对齐 `QColor::setHsl`（qcolor.cpp:1213-1227）：h < -1 或
 *             s/l/a 越界输出警告并将整色置为无效。
 */
void XColor_setHsl(XColor* self, int h, int s, int l, int a);

/**
 * @brief      获取所有 HSL 分量（浮点）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param h    输出浮点色相指针（可为 NULL，跳过）；灰色输出 -1.0f
 * @param s    输出浮点饱和度指针（可为 NULL，跳过）
 * @param l    输出浮点亮度指针（可为 NULL，跳过）
 * @param a    输出浮点 Alpha 指针（可为 NULL，跳过）
 * @note       对齐 `QColor::getHslF`（qcolor.cpp:1125-1141）：逐通道按存储
 *             值 / 65535.0f 直读；色相 / 36000.0f。
 */
void XColor_getHslF(const XColor* self, float* h, float* s, float* l, float* a);

/**
 * @brief      设置所有 HSL 分量（浮点）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param h    浮点色相：-1.0 表示无定义色相（灰色，存 65535 哨兵）；其余
 *             合法域为 [0.0,1.0]（h=1.0 的 36000 回卷为 0，见
 *             XColor_create_hslF）
 * @param s    浮点饱和度（0.0~1.0）
 * @param l    浮点亮度（0.0~1.0）
 * @param a    浮点 Alpha（0.0~1.0）
 * @note       对齐 `QColor::setHslF`（qcolor.cpp:1183-1200）：h ∉[0,1] 且
 *             ≠-1.0，或 s/l/a 越界时输出警告并将整色置为无效。
 */
void XColor_setHslF(XColor* self, float h, float s, float l, float a);

/* ========== 转换与工具函数 ========== */

/**
 * @brief      转换为 RGB 规格
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param out  输出颜色对象指针；无效颜色或已是 Rgb 规格时原样拷贝
 * @note       对齐 `QColor::toRgb()`（qcolor.cpp:2033-2174）：从 Hsv/Hsl/
 *             Cmyk 存储规格位做浮点精确换算（含灰色 hue 哨兵、色相 36000
 *             按 0、Hsl 亮度 0 与 u16==1 修正、CMYK 1-(c(1-k)+k) 公式），
 *             alpha 原样携带。函数体内不回环调用任何对外 API。
 */
void XColor_toRgb(const XColor* self, XColor* out);

/**
 * @brief      转换为 HSV 规格
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param out  输出颜色对象指针；无效颜色或已是 Hsv 规格时原样拷贝
 * @note       对齐 `QColor::toHsv()`（qcolor.cpp:2186-2230）：非 Rgb 规格
 *             先转 RGB；RGB→HSV 浮点逐度换算（delta==0 时色相存 65535
 *             哨兵、饱和度 0），色相按 qRound(度×100) 存百分之一度；alpha
 *             原样携带。函数体内不回环调用任何对外 API。
 */
void XColor_toHsv(const XColor* self, XColor* out);

/**
 * @brief      转换为 CMYK 规格
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param out  输出颜色对象指针；无效颜色或已是 Cmyk 规格时原样拷贝
 * @note       对齐 `QColor::toCmyk()`（qcolor.cpp:2293-2332）：非 Rgb 规格
 *             先转 RGB；RGB 全零时 c=m=y=0、k=65535，否则 k=min(c,m,y)、
 *             c'=(c-k)/(1-k)；alpha 原样携带。函数体内不回环调用任何对外
 *             API。
 */
void XColor_toCmyk(const XColor* self, XColor* out);

/**
 * @brief      转换为 HSL 规格
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param out  输出颜色对象指针；无效颜色或已是 Hsl 规格时原样拷贝
 * @note       对齐 `QColor::toHsl()`（qcolor.cpp:2237-2286）：非 Rgb 规格
 *             先转 RGB；亮度 l=(max+min)/2，delta==0 时色相存 65535 哨兵、
 *             饱和度 0，饱和度按 l<0.5 分母切换；alpha 原样携带。函数体内
 *             不回环调用任何对外 API。
 */
void XColor_toHsl(const XColor* self, XColor* out);

/**
 * @brief      转换为指定规格
 * @param self      目标 XColor 对象指针；NULL 返回无效颜色
 * @param colorSpec 目标规格（XColor_Spec）
 * @return          同规格原样拷贝；请求 Rgb/Hsv/Cmyk/Hsl 返回对应转换结
 *                  果；请求 Invalid 或 ExtendedRgb（声明边界，不实现）返
 *                  回无效颜色
 * @note       对齐 `QColor::convertTo(Spec)`（qcolor.cpp:2334-2353）。返回
 *             值不得忽略（Qt 侧为 [[nodiscard]]）。ExtendedRgb 分支按声明
 *             边界落无效色（偏差声明，见头文件枚举注）。
 */
XColor XColor_convertTo(const XColor* self, XColor_Spec colorSpec);

/**
 * @brief      返回更亮（或更暗）的颜色副本
 * @param self   目标 XColor 对象指针；NULL 返回无效颜色
 * @param factor 亮度系数：>100 变亮（150 即亮 50%），<100 变暗（此时等效
 *               darker(10000/factor)），<=0 原样拷贝返回
 * @return       转换到 HSV 调明度后转回原规格的颜色副本（不修改本对象）
 * @note       对齐 `QColor::lighter(int)`（qcolor.cpp:2810-2835；Qt 默认
 *               factor=150，C 无默认参数、调用方显式传 150）：明度溢出时
 *               按溢出量补偿饱和度（s -= v-65535，下限 0）。
 */
XColor XColor_lighter(const XColor* self, int factor);

/**
 * @brief      返回更暗（或更亮）的颜色副本
 * @param self   目标 XColor 对象指针；NULL 返回无效颜色
 * @param factor 暗度系数：>100 变暗（300 即亮度三分之一），<100 变亮（此
 *               时等效 lighter(10000/factor)），<=0 原样拷贝返回
 * @return       转换到 HSV 调明度后转回原规格的颜色副本（不修改本对象）
 * @note       对齐 `QColor::darker(int)`（qcolor.cpp:2855-2867；Qt 默认
 *               factor=200，C 无默认参数、调用方显式传 200）。
 */
XColor XColor_darker(const XColor* self, int factor);

/**
 * @brief      获取 32 位 ARGB 值（含 alpha）
 * @param self 目标 XColor 对象指针；NULL 返回 0（C 护栏）
 * @return     0xAARRGGBB 格式值；非 Rgb 规格（Invalid 除外）先转 RGB 再读。
 *             无效颜色不特判：按无效存储（alpha=65535、分量全 0）换算得
 *             0xFF000000（Qt 文档称该值"未指定"，以实现为准）
 * @note       对齐 `QColor::rgba()`（qcolor.cpp:1374-1379）。
 */
uint32_t XColor_rgba(const XColor* self);

/**
 * @brief      获取 32 位 RGB 值（alpha 置全不透明）
 * @param self 目标 XColor 对象指针；NULL 返回 0（C 护栏）
 * @return     0xFFRRGGBB 格式值（高位字节为 0xFF，对标 qRgb 四元组 (255,r,
 *             g,b)）；非 Rgb 规格（Invalid 除外）先转 RGB 再读；无效颜色
 *             按无效存储换算得 0xFF000000
 * @note       对齐 `QColor::rgb()`（qcolor.cpp:1437-1442）。
 */
uint32_t XColor_rgb(const XColor* self);

/**
 * @brief      设置 32 位 ARGB 值（含 alpha）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param argb 0xAARRGGBB 格式值，各通道 ×0x101 复制式直存，规格改判 Rgb
 * @note       对齐 `QColor::setRgba(QRgb)`（qcolor.cpp:1386-1394）。
 */
void XColor_setRgba(XColor* self, uint32_t argb);

/**
 * @brief      设置 32 位 RGB 值（alpha 置全不透明）
 * @param self 目标 XColor 对象指针；传入 NULL 时函数不执行任何操作
 * @param rgb  0x00RRGGBB 格式值；高 8 位忽略
 * @note       对齐 `QColor::setRgb(QRgb)`（qcolor.cpp:1449-1457）。C 无重
 *             载，_uint32 后缀即 Qt 的 setRgb(int,int,int,int) /
 *             setRgb(QRgb) 双重载的 C 拼写（对齐风格文档数字后缀重载惯例）。
 */
void XColor_setRgb_uint32(XColor* self, uint32_t rgb);

/**
 * @brief      比较两个颜色是否相等
 * @param a    颜色 A；NULL 返回 false（C 护栏）
 * @param b    颜色 B；NULL 返回 false（C 护栏）
 * @return     相等返回 true，否则返回 false
 * @note       对齐 `QColor::operator==`（qcolor.cpp:2885-2903）：双无效色
 *             相等；规格不同不等；alpha 相等；Hsv/Hsl 规格按色相 %36000
 *             比较（36000≡0、哨兵 65535≡29535 双方一致仍可等），其余规格
 *             直接比较分量；Cmyk 再比黑色通道。ExtendedRgb 与 Rgb 互容分
 *             支按声明边界不实现。`operator!=` 即取反，C 侧调用方自行 `!`。
 */
bool XColor_equals(const XColor* a, const XColor* b);

/**
 * @brief      获取所有 SVG/CSS 命名颜色名称列表
 * @return     XStringList 指针（新建对象，含 148 项命名颜色，由调用者
 *             XStringList_delete_base 释放）；分配失败返回 NULL
 * @note       对齐 `QColor::colorNames()`（qcolor.cpp:1000-1003）；命名表
 *             与 Qt rgbTbl 全量对齐（148 项，含 transparent）。
 */
XStringList* XColor_colorNames(void);

/**
 * @brief      按名称获取 SVG/CSS 命名颜色
 * @param name 颜色名称（ASCII 小写化比较，剔除全部空格与制表符，长度上限
 *             255）；NULL 返回无效颜色
 * @return     命中返回对应颜色（transparent 为 rgb(0,0,0,0)），未命中返回
 *             无效颜色
 * @note       对齐 QColor 构造函数内部的 `get_named_rgb` 路径
 *             （qcolor.cpp:314-329，Qt 非公开 API，XinYueC 公开为独立函数）。
 */
XColor XColor_fromName(const XString* name);

/* ========== 预定义常用颜色常量 ========== */

/* 对标 QColorConstants / Qt::GlobalColor（qcolor.h:288-310）。映射事实：
 * XColor_Gray=128,128,128 同 Qt::darkGray 与 SVG 色名 "gray"（并非
 * Qt::gray=160,160,164）；XColor_LightGray=192,192,192 同 Qt::lightGray；
 * XColor_DarkGray=64,64,64 在 Qt 无常量对应。宏集保留不动。 */
#define XColor_White       XColor_create_rgb(255, 255, 255, 255)
#define XColor_Black       XColor_create_rgb(0, 0, 0, 255)
#define XColor_Red         XColor_create_rgb(255, 0, 0, 255)
#define XColor_Green       XColor_create_rgb(0, 255, 0, 255)
#define XColor_Blue        XColor_create_rgb(0, 0, 255, 255)
#define XColor_Cyan        XColor_create_rgb(0, 255, 255, 255)
#define XColor_Magenta     XColor_create_rgb(255, 0, 255, 255)
#define XColor_Yellow      XColor_create_rgb(255, 255, 0, 255)
#define XColor_Gray        XColor_create_rgb(128, 128, 128, 255)
#define XColor_DarkGray    XColor_create_rgb(64, 64, 64, 255)
#define XColor_LightGray   XColor_create_rgb(192, 192, 192, 255)
#define XColor_Transparent XColor_create_rgb(0, 0, 0, 0)

#ifdef __cplusplus
}
#endif
#endif /* XCOLOR_H */
