/******************************************************************************
 * @file       XSurfaceFormat.h
 * @brief      XSurfaceFormat 表面格式值类型（对标 Qt 6.8 QSurfaceFormat）。
 * @details    描述窗口/离屏表面的像素缓冲配置：红绿蓝透明度位深、深度/模板
 *             缓冲大小、多重采样数、交换行为、OpenGL 上下文配置（可渲染类型、
 *             profile、主次版本）、立体缓冲、格式选项、交换间隔与色彩空间。
 *             本类是轻量值类型（不继承 XObject、不持有外部资源），与
 *             XColorSpace/XRect 等一致；为 QWindow::setFormat/format/
 *             requestedFormat 提供数据载体，未来由 XGuiApplication 平台
 *             后端解析为具体原生表面格式。
 * @note       模块总开关 XSURFACEFORMAT_ON 定义于 XGuiConfig.h；置 0 时
 *             裁剪整个 XSurfaceFormat 公共 API，XWindow 的格式接口退化为
 *             空实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSURFACEFORMAT_H
#define XSURFACEFORMAT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XColorSpace.h"
#if XSURFACEFORMAT_ON

/** @brief 表面格式选项位（对标 Qt 6.8 QSurfaceFormat::FormatOption）。 */
typedef enum XSurfaceFormatOption
{
    XSurfaceFormat_StereoBuffers       = 0x0001, /**< 立体缓冲。 */
    XSurfaceFormat_DebugContext        = 0x0002, /**< OpenGL 调试上下文。 */
    XSurfaceFormat_DeprecatedFunctions = 0x0004, /**< 允许已废弃 OpenGL 函数。 */
    XSurfaceFormat_ResetNotification   = 0x0008, /**< OpenGL 上下文重置通知。 */
    XSurfaceFormat_ProtectedContent    = 0x0010  /**< 受保护内容（DRM）。 */
} XSurfaceFormatOption;

/** @brief 格式选项位组合（对标 QSurfaceFormat::FormatOptions）。 */
typedef uint32_t XSurfaceFormatOptions;

/** @brief 交换行为（对标 QSurfaceFormat::SwapBehavior）。 */
typedef enum XSurfaceFormatSwapBehavior
{
    XSurfaceFormat_DefaultSwapBehavior = 0, /**< 平台默认。 */
    XSurfaceFormat_SingleBuffer        = 1, /**< 单缓冲。 */
    XSurfaceFormat_DoubleBuffer        = 2, /**< 双缓冲。 */
    XSurfaceFormat_TripleBuffer        = 3  /**< 三缓冲。 */
} XSurfaceFormatSwapBehavior;

/** @brief 可渲染类型（对标 QSurfaceFormat::RenderableType）。 */
typedef enum XSurfaceFormatRenderableType
{
    XSurfaceFormat_DefaultRenderableType = 0x0, /**< 平台默认。 */
    XSurfaceFormat_OpenGL                 = 0x1, /**< OpenGL。 */
    XSurfaceFormat_OpenGLES               = 0x2, /**< OpenGL ES。 */
    XSurfaceFormat_OpenVG                 = 0x4  /**< OpenVG。 */
} XSurfaceFormatRenderableType;

/** @brief OpenGL 上下文 profile（对标 QSurfaceFormat::OpenGLContextProfile）。 */
typedef enum XSurfaceFormatProfile
{
    XSurfaceFormat_NoProfile           = 0, /**< 无 profile。 */
    XSurfaceFormat_CoreProfile         = 1, /**< 核心 profile。 */
    XSurfaceFormat_CompatibilityProfile = 2 /**< 兼容 profile。 */
} XSurfaceFormatProfile;

/**
 * @brief      XSurfaceFormat 表面格式值类型。
 * @details    默认值与 Qt 6.8 QSurfaceFormatPrivate 完全一致：颜色/深度/模板
 *             缓冲大小均为 -1（未指定）、采样数 -1、交换行为 Default、可渲染
 *             类型 Default、profile NoProfile、OpenGL 主版本 2 次版本 0、
 *             交换间隔 1（垂直同步）、无立体缓冲、无格式选项、色彩空间为空。
 *             所有字段均为值语义，可直接赋值拷贝。Qt 的 operator== 只比较
 *             其实现中列出的格式字段，不比较 renderableType 和 colorSpace；
 *             XSurfaceFormat_equals() 保持这一行为，m_stereo 仅为 C 接口兼容
 *             缓存，也不参与相等判断。
 */
typedef struct XSurfaceFormat
{
    XSurfaceFormatOptions   m_options;          /**< 格式选项位组合。 */
    int                     m_redBufferSize;    /**< 红色缓冲位深；-1 未指定。 */
    int                     m_greenBufferSize;  /**< 绿色缓冲位深；-1 未指定。 */
    int                     m_blueBufferSize;   /**< 蓝色缓冲位深；-1 未指定。 */
    int                     m_alphaBufferSize;  /**< 透明缓冲位深；-1 未指定。 */
    int                     m_depthBufferSize;  /**< 深度缓冲大小；-1 未指定。 */
    int                     m_stencilBufferSize;/**< 模板缓冲大小；-1 未指定。 */
    int                     m_samples;          /**< 多重采样数；-1 未指定。 */
    XSurfaceFormatSwapBehavior   m_swapBehavior;/**< 交换行为；默认平台默认。 */
    XSurfaceFormatRenderableType m_renderableType; /**< 可渲染类型。 */
    XSurfaceFormatProfile        m_profile;     /**< OpenGL 上下文 profile。 */
    int                     m_majorVersion;     /**< OpenGL 主版本；默认 2。 */
    int                     m_minorVersion;     /**< OpenGL 次版本；默认 0。 */
    int                     m_swapInterval;     /**< 交换间隔；默认 1（垂直同步）。 */
    bool                    m_stereo;           /**< 是否启用立体缓冲。 */
    XColorSpace             m_colorSpace;       /**< 表面色彩空间。 */
} XSurfaceFormat;

/**
 * @brief      创建默认表面格式（对标 QSurfaceFormat() 默认构造）。
 * @return     初始化后的默认表面格式值。
 */
XSurfaceFormat XSurfaceFormat_create(void);

/**
 * @brief      以指定格式选项创建表面格式（对标 QSurfaceFormat(FormatOptions)）。
 * @param      options 格式选项位组合。
 * @return     初始化后的表面格式值。
 */
XSurfaceFormat XSurfaceFormat_create_ex(XSurfaceFormatOptions options);

/**
 * @brief      拷贝表面格式值。
 * @param      self 源格式；可为 NULL 按默认格式处理。
 * @return     self 的值副本。
 */
XSurfaceFormat XSurfaceFormat_copy(const XSurfaceFormat* self);

/**
 * @brief      设置深度缓冲大小（对标 QSurfaceFormat::setDepthBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @param      size 深度缓冲大小（位）；-1 表示未指定。
 */
void XSurfaceFormat_setDepthBufferSize(XSurfaceFormat* self, int size);

/**
 * @brief      返回深度缓冲大小（对标 QSurfaceFormat::depthBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @return     深度缓冲大小；未设置或入参非法返回 -1。
 */
int XSurfaceFormat_depthBufferSize(const XSurfaceFormat* self);

/**
 * @brief      设置模板缓冲大小（对标 QSurfaceFormat::setStencilBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @param      size 模板缓冲大小（位）；-1 表示未指定。
 */
void XSurfaceFormat_setStencilBufferSize(XSurfaceFormat* self, int size);

/**
 * @brief      返回模板缓冲大小（对标 QSurfaceFormat::stencilBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @return     模板缓冲大小；未设置或入参非法返回 -1。
 */
int XSurfaceFormat_stencilBufferSize(const XSurfaceFormat* self);

/**
 * @brief      设置红色缓冲大小（对标 QSurfaceFormat::setRedBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @param      size 红色缓冲位深；-1 表示未指定。
 */
void XSurfaceFormat_setRedBufferSize(XSurfaceFormat* self, int size);

/**
 * @brief      返回红色缓冲大小（对标 QSurfaceFormat::redBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @return     红色缓冲位深；未设置或入参非法返回 -1。
 */
int XSurfaceFormat_redBufferSize(const XSurfaceFormat* self);

/**
 * @brief      设置绿色缓冲大小（对标 QSurfaceFormat::setGreenBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @param      size 绿色缓冲位深；-1 表示未指定。
 */
void XSurfaceFormat_setGreenBufferSize(XSurfaceFormat* self, int size);

/**
 * @brief      返回绿色缓冲大小（对标 QSurfaceFormat::greenBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @return     绿色缓冲位深；未设置或入参非法返回 -1。
 */
int XSurfaceFormat_greenBufferSize(const XSurfaceFormat* self);

/**
 * @brief      设置蓝色缓冲大小（对标 QSurfaceFormat::setBlueBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @param      size 蓝色缓冲位深；-1 表示未指定。
 */
void XSurfaceFormat_setBlueBufferSize(XSurfaceFormat* self, int size);

/**
 * @brief      返回蓝色缓冲大小（对标 QSurfaceFormat::blueBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @return     蓝色缓冲位深；未设置或入参非法返回 -1。
 */
int XSurfaceFormat_blueBufferSize(const XSurfaceFormat* self);

/**
 * @brief      设置 Alpha 缓冲大小（对标 QSurfaceFormat::setAlphaBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @param      size Alpha 缓冲位深；-1 表示未指定。
 */
void XSurfaceFormat_setAlphaBufferSize(XSurfaceFormat* self, int size);

/**
 * @brief      返回 Alpha 缓冲大小（对标 QSurfaceFormat::alphaBufferSize）。
 * @param      self 目标格式；可为 NULL。
 * @return     Alpha 缓冲位深；未设置或入参非法返回 -1。
 */
int XSurfaceFormat_alphaBufferSize(const XSurfaceFormat* self);

/**
 * @brief      设置多重采样数（对标 QSurfaceFormat::setSamples）。
 * @param      self 目标格式；可为 NULL。
 * @param      numSamples 每像素采样数；-1 表示未指定。
 */
void XSurfaceFormat_setSamples(XSurfaceFormat* self, int numSamples);

/**
 * @brief      返回多重采样数（对标 QSurfaceFormat::samples）。
 * @param      self 目标格式；可为 NULL。
 * @return     采样数；未设置或入参非法返回 -1。
 */
int XSurfaceFormat_samples(const XSurfaceFormat* self);

/**
 * @brief      设置交换行为（对标 QSurfaceFormat::setSwapBehavior）。
 * @param      self 目标格式；可为 NULL。
 * @param      behavior 交换行为。
 */
void XSurfaceFormat_setSwapBehavior(XSurfaceFormat* self,
                                    XSurfaceFormatSwapBehavior behavior);

/**
 * @brief      返回交换行为（对标 QSurfaceFormat::swapBehavior）。
 * @param      self 目标格式；可为 NULL。
 * @return     交换行为；入参非法返回 DefaultSwapBehavior。
 */
XSurfaceFormatSwapBehavior XSurfaceFormat_swapBehavior(const XSurfaceFormat* self);

/**
 * @brief      判断格式是否包含 Alpha 缓冲（对标 QSurfaceFormat::hasAlpha）。
 * @param      self 目标格式；可为 NULL。
 * @return     alphaBufferSize > 0 时返回 true；默认值 -1 和零位请求返回 false。
 */
bool XSurfaceFormat_hasAlpha(const XSurfaceFormat* self);

/**
 * @brief      设置 OpenGL 上下文 profile（对标 QSurfaceFormat::setProfile）。
 * @param      self 目标格式；可为 NULL。
 * @param      profile 上下文 profile。
 */
void XSurfaceFormat_setProfile(XSurfaceFormat* self, XSurfaceFormatProfile profile);

/**
 * @brief      返回 OpenGL 上下文 profile（对标 QSurfaceFormat::profile）。
 * @param      self 目标格式；可为 NULL。
 * @return     上下文 profile；入参非法返回 NoProfile。
 */
XSurfaceFormatProfile XSurfaceFormat_profile(const XSurfaceFormat* self);

/**
 * @brief      设置可渲染类型（对标 QSurfaceFormat::setRenderableType）。
 * @param      self 目标格式；可为 NULL。
 * @param      type 可渲染类型。
 */
void XSurfaceFormat_setRenderableType(XSurfaceFormat* self,
                                      XSurfaceFormatRenderableType type);

/**
 * @brief      返回可渲染类型（对标 QSurfaceFormat::renderableType）。
 * @param      self 目标格式；可为 NULL。
 * @return     可渲染类型；入参非法返回 DefaultRenderableType。
 */
XSurfaceFormatRenderableType XSurfaceFormat_renderableType(const XSurfaceFormat* self);

/**
 * @brief      设置 OpenGL 主版本（对标 QSurfaceFormat::setMajorVersion）。
 * @param      self 目标格式；可为 NULL。
 * @param      majorVersion 主版本号。
 */
void XSurfaceFormat_setMajorVersion(XSurfaceFormat* self, int majorVersion);

/**
 * @brief      返回 OpenGL 主版本（对标 QSurfaceFormat::majorVersion）。
 * @param      self 目标格式；可为 NULL。
 * @return     主版本号；入参非法返回 0。
 */
int XSurfaceFormat_majorVersion(const XSurfaceFormat* self);

/**
 * @brief      设置 OpenGL 次版本（对标 QSurfaceFormat::setMinorVersion）。
 * @param      self 目标格式；可为 NULL。
 * @param      minorVersion 次版本号。
 */
void XSurfaceFormat_setMinorVersion(XSurfaceFormat* self, int minorVersion);

/**
 * @brief      返回 OpenGL 次版本（对标 QSurfaceFormat::minorVersion）。
 * @param      self 目标格式；可为 NULL。
 * @return     次版本号；入参非法返回 0。
 */
int XSurfaceFormat_minorVersion(const XSurfaceFormat* self);

/**
 * @brief      同时设置 OpenGL 主次版本（对标 QSurfaceFormat::setVersion）。
 * @param      self 目标格式；可为 NULL。
 * @param      major 主版本号。
 * @param      minor 次版本号。
 */
void XSurfaceFormat_setVersion(XSurfaceFormat* self, int major, int minor);

/**
 * @brief      返回 OpenGL 主次版本（对标 QSurfaceFormat::version）。
 * @param      self 目标格式；可为 NULL。
 * @param      major 输出主版本号；可为 NULL。
 * @param      minor 输出次版本号；可为 NULL。
 */
void XSurfaceFormat_version(const XSurfaceFormat* self,
                            int* major, int* minor);

/**
 * @brief      设置是否启用立体缓冲（对标 QSurfaceFormat::setStereo）。
 * @param      self 目标格式；可为 NULL。
 * @param      enable true 启用立体缓冲。
 */
void XSurfaceFormat_setStereo(XSurfaceFormat* self, bool enable);

/**
 * @brief      返回是否启用立体缓冲（对标 QSurfaceFormat::stereo）。
 * @param      self 目标格式；可为 NULL。
 * @return     true 表示启用立体缓冲。
 */
bool XSurfaceFormat_stereo(const XSurfaceFormat* self);

/**
 * @brief      设置格式选项位组合（对标 QSurfaceFormat::setOptions）。
 * @param      self 目标格式；可为 NULL。
 * @param      options 格式选项位组合。
 */
void XSurfaceFormat_setOptions(XSurfaceFormat* self, XSurfaceFormatOptions options);

/**
 * @brief      设置单个格式选项（对标 QSurfaceFormat::setOption）。
 * @param      self 目标格式；可为 NULL。
 * @param      option 格式选项。
 * @param      on true 置位，false 清除。
 */
void XSurfaceFormat_setOption(XSurfaceFormat* self,
                              XSurfaceFormatOption option, bool on);

/**
 * @brief      测试格式选项是否置位（对标 QSurfaceFormat::testOption）。
 * @param      self 目标格式；可为 NULL。
 * @param      option 格式选项。
 * @return     true 表示该选项已置位。
 */
bool XSurfaceFormat_testOption(const XSurfaceFormat* self,
                               XSurfaceFormatOption option);

/**
 * @brief      返回格式选项位组合（对标 QSurfaceFormat::options）。
 * @param      self 目标格式；可为 NULL。
 * @return     格式选项位组合。
 */
XSurfaceFormatOptions XSurfaceFormat_options(const XSurfaceFormat* self);

/**
 * @brief      设置交换间隔（对标 QSurfaceFormat::setSwapInterval）。
 * @details    默认 1（垂直同步）；0 表示尽可能不垂直同步，负数表示平台默认。
 * @param      self 目标格式；可为 NULL。
 * @param      interval 交换间隔。
 */
void XSurfaceFormat_setSwapInterval(XSurfaceFormat* self, int interval);

/**
 * @brief      返回交换间隔（对标 QSurfaceFormat::swapInterval）。
 * @param      self 目标格式；可为 NULL。
 * @return     交换间隔；入参非法返回 1。
 */
int XSurfaceFormat_swapInterval(const XSurfaceFormat* self);

/**
 * @brief      返回表面色彩空间（对标 QSurfaceFormat::colorSpace）。
 * @param      self 目标格式；可为 NULL。
 * @return     色彩空间值副本；入参非法返回无效色彩空间。
 */
XColorSpace XSurfaceFormat_colorSpace(const XSurfaceFormat* self);

/**
 * @brief      设置表面色彩空间（对标 QSurfaceFormat::setColorSpace）。
 * @param      self 目标格式；可为 NULL。
 * @param      colorSpace 色彩空间值。
 */
void XSurfaceFormat_setColorSpace(XSurfaceFormat* self, XColorSpace colorSpace);

/**
 * @brief      设置进程级默认表面格式（对标 QSurfaceFormat::setDefaultFormat）。
 * @details    该格式供窗口/上下文初始化路径读取；普通
 *             XSurfaceFormat_create() 始终使用 Qt 标准出厂默认值，不受此
 *             全局格式影响。入参为 NULL 时恢复 Qt 标准的出厂默认值。仅保存
 *             值，不解析原生格式。
 * @param      format 默认格式；可为 NULL 恢复出厂默认。
 */
void XSurfaceFormat_setDefaultFormat(const XSurfaceFormat* format);

/**
 * @brief      返回进程级默认表面格式（对标 QSurfaceFormat::defaultFormat）。
 * @details    初始值为 Qt 标准出厂默认；调用 setDefaultFormat 后返回已设置值。
 * @return     默认表面格式值副本。
 */
XSurfaceFormat XSurfaceFormat_defaultFormat(void);

/**
 * @brief      判断两个表面格式是否相等。
 * @details    按 Qt 6.8 QSurfaceFormat::operator== 的字段集合比较；该集合
 *             不包含可渲染类型、色彩空间和 C 兼容缓存字段 m_stereo。
 * @param      lhs 左侧格式；两个参数同时为空时返回 true。
 * @param      rhs 右侧格式；仅一个参数为空时返回 false。
 * @return     若 Qt 相等比较中的全部字段一致则返回 true。
 */
bool XSurfaceFormat_equals(const XSurfaceFormat* lhs, const XSurfaceFormat* rhs);

#endif /* XSURFACEFORMAT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSURFACEFORMAT_H */
