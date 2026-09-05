/**
 * @file       XMovie.h
 * @brief      XMovie 动画图像控制类（对标 Qt 6.8 QMovie）。
 * @details    XMovie 使用 XImageReader 读取图像帧，并通过 XObject 信号
 *             报告状态和帧变化。GIF 在启用 XIMAGECODEC_GIF_ANIM_ON 时提供
 *             多帧、延迟与循环次数；其它格式保持单帧或已知多帧语义。
 */
#ifndef XMOVIE_H
#define XMOVIE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XClass.h"
#include "XObject.h"
#include "XImage.h"
#include "XImageReader.h"
#include "XPixmap.h"
#include "XColor.h"
#include "XString.h"
#include "XStringList.h"
#include "XTypes.h"

typedef struct XIODevice XIODevice;

/** @brief XMovie 继承 XObject，不新增虚函数槽位。 */
XCLASS_DEFINE_BEGING(XMovie)
XCLASS_DEFINE_EXTEND_END(XMovie, XObject)

/** @brief QMovie 的运行状态。 */
typedef enum XMovieState
{
    XMovieState_NotRunning = 0, /**< 没有运行中的播放。 */
    XMovieState_Paused = 1,     /**< 播放已暂停。 */
    XMovieState_Running = 2     /**< 正在处理帧。 */
} XMovieState;

/** @brief QMovie 的帧缓存策略。单帧后端两种策略效果相同。 */
typedef enum XMovieCacheMode
{
    XMovieCacheMode_CacheNone = 0, /**< 不缓存额外帧。 */
    XMovieCacheMode_CacheAll = 1   /**< 缓存全部已读取帧。 */
} XMovieCacheMode;

/** @brief XMovie 对象；m_class 必须是第一个成员且禁止手工修改。 */
typedef struct XMoviePrivate XMoviePrivate;
typedef struct XMovie
{
    XObject       m_class; /**< 第一个成员，由 XObject 管理，禁止手工修改。 */
    XMoviePrivate* m_data; /**< 私有状态，由 XMovie 拥有，仅供实现访问。 */
} XMovie;

/**
 * @brief 初始化 XMovie 虚函数表，返回共享表指针。
 * @return XMovie 类虚函数表指针。
 */
XVtable* XMovie_class_init(void);

/**
 * @brief 初始化空 XMovie。
 * @param self 待初始化的对象指针；生命周期结束时必须调用 deinit_base。
 */
void XMovie_init(XMovie* self);
/**
 * @brief 使用设备和 XString 格式初始化 XMovie。
 * @param self 待初始化的对象指针。
 * @param device 输入设备指针，由调用方持有。
 * @param format XString 格式名；可为 NULL 以自动检测。
 */
void XMovie_init_device(XMovie* self, XIODevice* device, const XString* format);
/**
 * @brief 使用 UTF-8 设备格式初始化 XMovie 的兼容重载。
 * @param self 待初始化的对象指针。
 * @param device 输入设备指针，由调用方持有。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 */
void XMovie_init_device_2(XMovie* self, XIODevice* device, const char* format);
/**
 * @brief 使用 XString 文件名和格式初始化 XMovie。
 * @param self 待初始化的对象指针。
 * @param fileName XString 文件名。
 * @param format XString 格式名；可为 NULL 以自动检测。
 */
void XMovie_init_file(XMovie* self, const XString* fileName, const XString* format);
/**
 * @brief 使用 UTF-8 文件名和格式初始化 XMovie 的兼容重载。
 * @param self 待初始化的对象指针。
 * @param fileName UTF-8 编码的文件名。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 */
void XMovie_init_file_2(XMovie* self, const char* fileName, const char* format);
/**
 * @brief 创建空堆对象。
 * @param memory 对象使用的内存类型。
 * @return 新对象指针；失败时返回 NULL，调用方使用 XMovie_delete_base 释放。
 */
XMovie* XMovie_create_ex(XMemoryType memory);
/**
 * @brief 使用设备创建堆对象。
 * @param memory 对象使用的内存类型。
 * @param device 输入设备指针，由调用方持有。
 * @param format XString 格式名；可为 NULL。
 * @return 新对象指针；失败时返回 NULL。
 */
XMovie* XMovie_create_device_ex(XMemoryType memory, XIODevice* device, const XString* format);
/**
 * @brief 使用 UTF-8 设备格式创建堆对象的兼容重载。
 * @param memory 对象使用的内存类型。
 * @param device 输入设备指针，由调用方持有。
 * @param format UTF-8 编码格式名；可为 NULL。
 * @return 新对象指针；失败时返回 NULL。
 */
XMovie* XMovie_create_device_ex_2(XMemoryType memory, XIODevice* device, const char* format);
/**
 * @brief 使用文件名创建堆对象。
 * @param memory 对象使用的内存类型。
 * @param fileName XString 文件名。
 * @param format XString 格式名；可为 NULL。
 * @return 新对象指针；失败时返回 NULL。
 */
XMovie* XMovie_create_file_ex(XMemoryType memory, const XString* fileName, const XString* format);
/**
 * @brief 使用 UTF-8 文件名创建堆对象的兼容重载。
 * @param memory 对象使用的内存类型。
 * @param fileName UTF-8 编码的文件名。
 * @param format UTF-8 编码格式名；可为 NULL。
 * @return 新对象指针；失败时返回 NULL。
 */
XMovie* XMovie_create_file_ex_2(XMemoryType memory, const char* fileName, const char* format);

/**
 * @brief 释放对象资源但保留对象存储，之后可再次 init。
 * @param self 待释放的对象指针。
 */
/**
 * @brief 通过 XClass 虚析构入口释放对象资源。
 * @param self 待释放的对象指针。
 */
/** @brief 通过 XClass 虚表释放动画资源。 @param self 待释放的动画对象指针。 */
#define XMovie_deinit_base(self) XClass_deinit_base((XClass*)(self))
/**
 * @brief 删除堆对象；栈对象只能调用 deinit_base。
 * @param self 待删除的堆对象指针。
 */
/** @brief 删除堆上的动画对象。 @param self 待删除的动画对象指针。 */
#define XMovie_delete_base(self) XClass_delete_base((XClass*)(self))
/** @brief 深拷贝 XMovie；目标未初始化时会先初始化，源对象为空时不执行。 */
/** @brief 移动 XMovie；目标未初始化时会先初始化，源对象移动后为空。 */
/** @brief 通过虚表执行深拷贝。 */

/**
 * @brief 返回当前支持的图像格式列表。
 * @return 新建的 XStringList；调用方负责释放。
 */
XStringList* XMovie_supportedFormats(void);

/**
 * @brief 设置输入设备；XMovie 不取得设备所有权。
 * @param self 目标对象指针。
 * @param device 输入设备指针，可为 NULL。
 */
void XMovie_setDevice(XMovie* self, XIODevice* device);
/**
 * @brief 返回当前输入设备借用指针。
 * @param self 对象指针。
 * @return 当前设备指针；未设置时返回 NULL。
 */
XIODevice* XMovie_device(const XMovie* self);
/**
 * @brief 设置 XString 文件名；对象复制字符串内容。
 * @param self 目标对象指针。
 * @param fileName 文件名；可为 NULL 清除文件名。
 */
void XMovie_setFileName(XMovie* self, const XString* fileName);
/**
 * @brief 设置 UTF-8 文件名的兼容重载。
 * @param self 目标对象指针。
 * @param fileName UTF-8 编码的文件名；可为 NULL 清除文件名。
 */
void XMovie_setFileName_2(XMovie* self, const char* fileName);
/**
 * @brief 返回文件名副本。
 * @param self 对象指针。
 * @return 新建的 XString；调用方负责释放。
 */
XString* XMovie_fileName(const XMovie* self);
/**
 * @brief 返回内部文件名只读引用。
 * @param self 对象指针。
 * @return 内部 XString 引用；不得释放或修改。
 */
const XString* XMovie_fileName_const(const XMovie* self);
/**
 * @brief 返回 UTF-8 文件名兼容指针。
 * @param self 对象指针。
 * @return UTF-8 指针，由内部转换缓存持有，不得释放。
 */
const char* XMovie_fileName_2(const XMovie* self);
/**
 * @brief 设置 XString 图像格式；空值恢复自动检测。
 * @param self 目标对象指针。
 * @param format 图像格式名；可为 NULL 自动检测。
 */
void XMovie_setFormat(XMovie* self, const XString* format);
/**
 * @brief 设置 UTF-8 图像格式的兼容重载。
 * @param self 目标对象指针。
 * @param format UTF-8 编码的格式名；可为 NULL 自动检测。
 */
void XMovie_setFormat_2(XMovie* self, const char* format);
/**
 * @brief 返回图像格式副本。
 * @param self 对象指针。
 * @return 新建的 XString；调用方负责释放。
 */
XString* XMovie_format(const XMovie* self);
/**
 * @brief 返回内部图像格式只读引用。
 * @param self 对象指针。
 * @return 内部 XString 引用；不得释放或修改。
 */
const XString* XMovie_format_const(const XMovie* self);
/**
 * @brief 返回 UTF-8 图像格式兼容指针。
 * @param self 对象指针。
 * @return UTF-8 指针，由内部转换缓存持有，不得释放。
 */
const char* XMovie_format_2(const XMovie* self);

/**
 * @brief 设置背景颜色；颜色值按值复制。
 * @param self 目标对象指针。
 * @param color 背景颜色指针；可为 NULL 使用默认颜色。
 */
void XMovie_setBackgroundColor(XMovie* self, const XColor* color);
/**
 * @brief 返回背景颜色值。
 * @param self 对象指针。
 * @return 背景颜色值；无效对象返回无效颜色。
 */
XColor XMovie_backgroundColor(const XMovie* self);
/**
 * @brief 返回当前播放状态。
 * @param self 对象指针。
 * @return 当前 XMovieState 状态。
 */
XMovieState XMovie_state(const XMovie* self);
/**
 * @brief 返回当前帧矩形。
 * @param self 对象指针。
 * @param out 输出矩形指针。
 */
void XMovie_frameRect(const XMovie* self, XRect* out);
/**
 * @brief 将当前帧复制到调用方提供的 XImage。
 * @param self 对象指针。
 * @param out 输出图像指针，旧内容会先释放。
 */
void XMovie_currentImage(const XMovie* self, XImage* out);
/**
 * @brief 将当前帧复制到调用方提供的 XPixmap。
 * @param self 对象指针。
 * @param out 输出像素图指针，旧内容会先释放。
 */
void XMovie_currentPixmap(const XMovie* self, XPixmap* out);
/**
 * @brief 判断读取器当前是否具有有效图像源。
 * @param self 对象指针。
 * @return 有效返回 true，否则返回 false。
 */
bool XMovie_isValid(const XMovie* self);
/**
 * @brief 返回最近一次读取错误。
 * @param self 对象指针。
 * @return XImageReaderError 错误枚举。
 */
XImageReaderError XMovie_lastError(const XMovie* self);
/**
 * @brief 返回最近一次错误文本副本。
 * @param self 对象指针。
 * @return 新建的 XString；调用方负责释放。
 */
XString* XMovie_lastErrorString(const XMovie* self);
/**
 * @brief 返回最近一次错误文本内部只读引用。
 * @param self 对象指针。
 * @return 内部 XString 引用；不得释放或修改。
 */
const XString* XMovie_lastErrorString_const(const XMovie* self);
/**
 * @brief 返回最近一次错误文本的 UTF-8 兼容指针。
 * @param self 对象指针。
 * @return UTF-8 指针，由内部转换缓存持有，不得释放。
 */
const char* XMovie_lastErrorString_2(const XMovie* self);

/**
 * @brief 跳转到指定帧；当前单帧后端仅接受帧号 0。
 * @param self 目标对象指针。
 * @param frameNumber 目标帧编号。
 * @return 跳转成功返回 true，否则返回 false。
 */
bool XMovie_jumpToFrame(XMovie* self, int frameNumber);
/**
 * @brief 返回循环次数；不支持动画时为 0。
 * @param self 对象指针。
 * @return 循环次数。
 */
int XMovie_loopCount(const XMovie* self);
/**
 * @brief 返回帧数；不可读或无源时为 0。
 * @param self 对象指针。
 * @return 帧数。
 */
int XMovie_frameCount(const XMovie* self);
/**
 * @brief 返回下一帧延迟毫秒数；单帧后端为 0。
 * @param self 对象指针。
 * @return 延迟毫秒数。
 */
int XMovie_nextFrameDelay(const XMovie* self);
/**
 * @brief 返回当前帧编号；未加载帧时为 -1。
 * @param self 对象指针。
 * @return 当前帧编号。
 */
int XMovie_currentFrameNumber(const XMovie* self);
/**
 * @brief 返回播放速度百分比。
 * @param self 对象指针。
 * @return 播放速度百分比。
 */
int XMovie_speed(const XMovie* self);
/**
 * @brief 设置播放速度百分比；单帧后端保留该属性但不伪造计时。
 * @param self 目标对象指针。
 * @param percentSpeed 播放速度百分比，通常为正数。
 */
void XMovie_setSpeed(XMovie* self, int percentSpeed);
/**
 * @brief 返回缩放尺寸；未设置时宽高为 -1。
 * @param self 对象指针。
 * @param out 输出尺寸指针。
 */
void XMovie_scaledSize(const XMovie* self, XSize* out);
/**
 * @brief 设置读取器缩放尺寸；对象复制尺寸值。Qt 允许宽或高为 -1/0，
 *        由读取器按原图比例补齐另一维；设置属性本身不清空当前帧。
 * @param self 目标对象指针。
 * @param size 缩放尺寸指针；必须非 NULL，当前轻量读取器没有单独的清除接口。
 */
void XMovie_setScaledSize(XMovie* self, const XSize* size);
/**
 * @brief 返回缓存策略。
 * @param self 对象指针。
 * @return 当前 XMovieCacheMode 值。
 */
XMovieCacheMode XMovie_cacheMode(const XMovie* self);
/**
 * @brief 设置缓存策略；单帧后端不缓存额外帧。
 * @param self 目标对象指针。
 * @param mode 缓存策略枚举值。
 */
void XMovie_setCacheMode(XMovie* self, XMovieCacheMode mode);

/**
 * @brief 读取下一帧并开始播放；暂停状态只恢复播放，运行状态重复调用无操作。
 * @param self 目标对象指针。
 */
void XMovie_start(XMovie* self);
/**
 * @brief 尝试跳转下一帧；遇到动画循环时按 loopCount 回到第 0 帧。
 * @param self 目标对象指针。
 * @return 成功跳转返回 true，否则返回 false。
 */
bool XMovie_jumpToNextFrame(XMovie* self);
/**
 * @brief 设置暂停状态；暂停 NotRunning 无操作，恢复时进入 Running。
 * @param self 目标对象指针。
 * @param paused 是否暂停。
 */
void XMovie_setPaused(XMovie* self, bool paused);
/**
 * @brief 停止播放并清理运行状态，但保留当前帧。
 * @param self 目标对象指针。
 */
void XMovie_stop(XMovie* self);

/**
 * @brief 发出 started 信号。
 * @param self 目标对象指针。
 * @return 信号分发结果指针。
 */
void* XMovie_started_signal(XMovie* self);
/**
 * @brief 发出 resized 信号。
 * @param self 目标对象指针。
 * @param size 新尺寸指针。
 * @return 信号分发结果指针。
 */
void* XMovie_resized_signal(XMovie* self, const XSize* size);
/**
 * @brief 发出 updated 信号。
 * @param self 目标对象指针。
 * @param rect 更新区域指针。
 * @return 信号分发结果指针。
 */
void* XMovie_updated_signal(XMovie* self, const XRect* rect);
/**
 * @brief 发出状态变化信号。
 * @param self 目标对象指针。
 * @param state 新播放状态。
 * @return 信号分发结果指针。
 */
void* XMovie_stateChanged_signal(XMovie* self, XMovieState state);
/**
 * @brief 发出读取错误信号。
 * @param self 目标对象指针。
 * @param error 错误枚举值。
 * @return 信号分发结果指针。
 */
void* XMovie_error_signal(XMovie* self, XImageReaderError error);
/**
 * @brief 发出播放完成信号。
 * @param self 目标对象指针。
 * @return 信号分发结果指针。
 */
void* XMovie_finished_signal(XMovie* self);
/**
 * @brief 发出帧变化信号。
 * @param self 目标对象指针。
 * @param frameNumber 当前帧编号。
 * @return 信号分发结果指针。
 */
void* XMovie_frameChanged_signal(XMovie* self, int frameNumber);

#ifdef __cplusplus
}
#endif

#undef XMovie_create
#define XMovie_create() XMovie_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XMOVIE_H */
