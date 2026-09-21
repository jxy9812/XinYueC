/******************************************************************************
 * @file       XClipboard.h
 * @brief      XClipboard 剪贴板类（对标 Qt 6.8 QClipboard，实现全部公开 API）。
 * @details    XClipboard 继承 XObject，提供进程内剪贴板：三种模式
 *             （Clipboard/Selection/FindBuffer，对标 QClipboard::Mode）、
 *             文本/HTML/图像/像素图/MIME 数据读写、所有权标志与 4 个通知
 *             信号。本模块不依赖任何平台 API：所有数据程序化存储于对象
 *             内部，不连接系统剪贴板；XGuiApplication_clipboard 返回进程
 *             内单例后，未来平台后端可在 setMimeData 时把数据同步给系统
 *             剪贴板。Selection/FindBuffer 两种平台相关模式的能力取决于
 *             平台后端：X11 后端接入 PRIMARY 选择区后 supportsSelection()
 *             返回 true（对标 QXcbClipboard），FindBuffer 的
 *             supportsFindBuffer() 恒为 false，与 Qt 在不支持该选择缓冲
 *             的平台上行为一致。
 * @note       模块开关 XCLIPBOARD_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XClipboard 公共 API。依赖子开关 XMIMEDATA_ON，关闭时
 *             MIME 数据接口退化为仅文本模式（空实现）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCLIPBOARD_H
#define XCLIPBOARD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XString.h"
#if XMIMEDATA_ON
#include "XMimeData.h"
#endif

#if XCLIPBOARD_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XClipboardPrivate XClipboardPrivate;/** @brief 声明 XClipboard 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XClipboard)
XCLASS_DEFINE_EXTEND_END(XClipboard, XObject)



/**
 * @brief      剪贴板模式（对标 Qt 6.8 QClipboard::Mode）。
 * @details    Selection 为 X11 主选择缓冲、FindBuffer 为 X11 查找缓冲；
 *             本实现保留枚举以对齐 Qt，但两种模式仅做存储、不能与系统
 *             交互。
 */
typedef enum XClipboardMode
{
    XClipboardMode_Clipboard = 0, /**< 标准剪贴板（X11 CLIPBOARD 选择区）。 */
    XClipboardMode_Selection,     /**< 选择缓冲（X11 平台接 PRIMARY 选择区）。 */
    XClipboardMode_FindBuffer,    /**< 查找缓冲（supportsFindBuffer 恒 false）。 */
    XClipboardMode_LastMode = XClipboardMode_FindBuffer /**< 最后一个模式。 */
} XClipboardMode;

/**
 * @brief      XClipboard 剪贴板对象；m_class 必须为第一个成员。
 * @details    每个模式的数据保存在 m_data 私有块中，调用方不得直接访问。
 */
typedef struct XClipboard
{
    XObject              m_class; /**< 第一个成员，由 XObject 管理。 */
    XClipboardPrivate*   m_data;  /**< 私有数据块，由 XClipboard 拥有。 */
} XClipboard;

/**
 * @brief      初始化 XClipboard 类虚函数表并返回共享表指针。
 * @return     XClipboard 类的共享 XVtable 指针。
 */
XVtable* XClipboard_class_init(void);

/**
 * @brief      初始化空 XClipboard（三种模式均无数据）。
 * @param      self 待初始化对象；必须与 XClipboard_deinit_base 成对调用。
 */
void XClipboard_init(XClipboard* self);

/**
 * @brief      使用默认内存类型在堆上创建空 XClipboard。
 * @return     新对象指针；失败返回 NULL，调用方用 XClipboard_delete_base 释放。
 */
#define XClipboard_create() XClipboard_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建空 XClipboard。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XClipboard* XClipboard_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放 XClipboard 资源（栈/外部存储对象使用）。 */
#define XClipboard_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XClipboard 对象。 */
#define XClipboard_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 模式能力与所有权（对标 QClipboard） ==================== */

/**
 * @brief      查询是否支持选择缓冲（对标 QClipboard::supportsSelection）。
 * @return     平台后端声明支持时为 true（X11 后端接入 PRIMARY 选择区，
 *             对标 QXcbClipboard::supportsMode(Selection)）；未注入后端或
 *             后端不支持时为 false。
 */
bool XClipboard_supportsSelection(const XClipboard* self);

/**
 * @brief      查询是否支持查找缓冲（对标 QClipboard::supportsFindBuffer）。
 * @return     恒为 false。
 */
bool XClipboard_supportsFindBuffer(const XClipboard* self);

/** @brief 剪贴板内容是否由本进程写入（对标 ownsClipboard）。 */
bool XClipboard_ownsClipboard(const XClipboard* self);
/** @brief 选择缓冲内容是否由本进程写入（对标 ownsSelection）。 */
bool XClipboard_ownsSelection(const XClipboard* self);
/** @brief 查找缓冲内容是否由本进程写入（对标 ownsFindBuffer）。 */
bool XClipboard_ownsFindBuffer(const XClipboard* self);

/* ==================== 内容清空（对标 QClipboard::clear） ==================== */

/**
 * @brief      清空指定模式的剪贴板内容（对标 QClipboard::clear）。
 * @details    清除文本与 MIME 数据、解开所有权，并发射 changed(mode)；
 *             若剪贴板内容本就为空，Qt 仍会通知，这里保持一致。
 * @param      self 目标对象；可为 NULL。
 * @param      mode 目标模式。
 */
void XClipboard_clear(XClipboard* self, XClipboardMode mode);

/* ==================== 文本（对标 QClipboard::text / setText） ==================== */

/**
 * @brief      读取指定模式下的纯文本（对标 QClipboard::text(Mode)）。
 * @return     新建 XString 堆拷贝（UTF-8），该模式无文本时返回 NULL；
 *             空文本返回非 NULL 的空 XString（以 toUtf8_length==0 区分）。
 *             调用方用 XString_delete_base 释放。
 */
XString* XClipboard_text(XClipboard* self, XClipboardMode mode);

/**
 * @brief      读取文本，同时输出子类型（对标 QClipboard::text(QString&, Mode)）。
 * @param      self    目标对象；可为 NULL。
 * @param      subtype in/out 参数（对标 Qt）：输入为 NULL 或空串时按 Qt
 *                    formats() 顺序依次尝试 "text/*"（plain -> html，无
 *                    plain 而 mime 含 text/html 时返回 html 内容并输出
 *                    "html"），输出为命中的子类型堆拷贝，无命中置 NULL；
 *                    输入非空（如 "html"）时只尝试 "text/<请求子类型>"，
 *                    命中与否都原样保留 *subtype（原地复用，调用方继续
 *                    持有所有权，对标 Qt 的 QString& 复用）。
 *                    本参数可为 NULL（等价空请求）。
 * @param      mode    目标模式。
 * @return     命中子类型的堆拷贝文本；均无时返回 NULL；调用方释放。
 */
XString* XClipboard_text_subtype(XClipboard* self, XString** subtype,
                              XClipboardMode mode);

/**
 * @brief      设置指定模式的纯文本（对标 QClipboard::setText）。
 * @details    深拷贝文本、标记本进程所有权并发射 changed(mode)；
 *             MIME 开启时按 Qt 语义创建新的 XMimeData，登记 text/plain 后
 *             通过 setMimeData() 转移所有权，因此 mimeData() 与 text() 保持
 *             同一份文本；MIME 裁剪关闭时退化为独立的 m_text 存储。
 * @param      self 目标对象；可为 NULL。
 * @param      text UTF-8 文本；可为 NULL（等价空串）。
 * @param      mode 目标模式。
 */
/* ==================== 平台后端挂载点（对标 QPlatformClipboard） ==================== */

/** @brief 平台剪贴板后端接口（C 函数指针形态；由平台集成层填充）。
 *  @details 对标 Qt：QClipboard 为抽象 API,各平台提供 QPlatformClipboard
 *           子类(X11 选择区/Win32 Clipboard)。未注入后端时 XClipboard
 *           使用进程内存储（现状,仅进程内复制粘贴可用）。 */
typedef struct XClipboardBackend
{
    void* ud;          /**< 平台层用户数据（传回各回调首参）。 */
    bool (*text)(void* ud, int mode, char** outText);      /**< 读平台剪贴板文本（调用方 XFree_System 释放）。 */
    bool (*setText)(void* ud, int mode, const char* text); /**< 写平台剪贴板文本。 */
    bool (*clear)(void* ud, int mode);                     /**< 清平台剪贴板。 */
    bool supportsSelection;                                /**< 平台是否支持 Selection 选择区（对标 QPlatformClipboard::supportsSelection；X11 接 PRIMARY 时置 true）。 */
    /* 可选反向通知：平台检测到本进程认领的选择区被其他应用夺走
     * （X11 SelectionClear）时调用，未注册时保持纯进程内语义
     * （对标 QXcbClipboard::handleSelectionClearRequest 直接调
     * QPlatformClipboard 上层，C 分层下经本指针解耦）。 */
    void (*selectionRevoked)(void* ud, int mode);          /**< 选择区所有权被夺通知（mode 为 XClipboardMode）。 */
    /* ---- mime 多格式平台协商（对标 QXcbClipboard 把 QMimeData 各
     * 格式映射为 X11 TARGETS 原子并在 SelectionRequest 按目标原子
     * 回数）。三个回调均为可选：追加在结构体尾部保持既有位置初始化
     * 兼容（C 语法对未列出的尾部成员补零），未注册（NULL）时全部
     * 走既有纯文本路径，行为零回归。格式名统一使用 MIME 写法
     * （"text/plain"、"text/html"、"image/png"……），长度上限
     * XCLIPBOARD_FORMAT_NAME_MAX（含结束符）。 */
    int (*formats)(void* ud, int mode, char outFormats[][64], int max); /**< 列出该模式当前可提供的 mime 格式名，返回实际个数（0=无）。 */
    bool (*mimeData)(void* ud, int mode, const char* format,
                     const unsigned char** data, int* len); /**< 读指定格式字节（借用语义：*data 指向平台内部镜像/接收缓冲，免拷贝，仅在下次后端调用前有效；对标 Qt 平台 mimeData 直接借用 QMimeData）。 */
    bool (*setMimeData)(void* ud, int mode, const char* format,
                        const unsigned char* data, int len); /**< 写指定格式字节进平台剪贴板（平台内部深拷贝；对标 QXcbClipboard::setMimeData 逐格式登记）。 */
    /* ---- INCR 增量读超时参数化（可选回调）：X11 大数据 INCR 读方向的
     * 整体超时由应用按需调整（慢生产者/大载荷/慢速远程连接场景）。
     * 追加在结构体尾部保持既有位置初始化兼容；无 INCR 语义的平台
     * （进程内/Win32）留 NULL 为 no-op。 */
    void (*setIncrTimeoutMs)(void* ud, int ms); /**< 设置 INCR 增量读整体超时（毫秒；平台内部对 ms<=0 恢复默认值）。 */
} XClipboardBackend;

/** @brief INCR 增量读整体超时默认值（毫秒；读方向对端死亡兜底口径，
 *  与 posix 后端 ICCCM 2.5 读路径一致）。 */
#define XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS 5000

/** @brief 后端格式名缓冲上限（含结束符；与 formats 回调的 outFormats
 *  第二维一致，供上层在栈上分配格式名表）。 */
#define XCLIPBOARD_FORMAT_NAME_MAX 64
/** @brief 上层一次枚举格式的合理容量（对标 QMimeData::formats() 列表规模）。 */
#define XCLIPBOARD_MAX_FORMATS 16

/** @brief 安装平台后端（NULL 恢复进程内存储语义）。 */
void XClipboard_installBackend(const XClipboardBackend* backend);

/**
 * @brief      设置 INCR 增量读整体超时（毫秒）。
 * @details    仅影响读方向（本进程作为请求方从外部所有者增量收集大数据
 *             时的整体兜底超时，ICCCM 2.5）；服务方向不受影响（由闲置
 *             回收治理）。Qt 无公开对应（QXcbClipboard 内部常量），此为
 *             框架自有运维参数：慢生产者/大载荷场景可调大，紧凑环境可
 *             调小。值在前端进程内记录，经后端契约 setIncrTimeoutMs
 *             可选回调下发（未装后端时先设后装亦生效）；无 INCR 语义的
 *             平台为 no-op。ms<=0 恢复默认
 *             XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS。
 * @param      ms 超时毫秒数；<=0 恢复默认值。
 */
void XClipboard_setIncrTimeoutMs(int ms);

/**
 * @brief      读取当前生效的 INCR 增量读整体超时（毫秒）。
 * @return     当前值（未设置过则返回 XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS）。
 */
int XClipboard_incrTimeoutMs(void);

/**
 * @brief      平台反向通知入口：指定模式的选择区所有权被其他应用夺走
 *             （对标 QXcbClipboard::handleSelectionClearRequest：清空
 *             ownerData 并经 setMimeData(NULL) 发射变化信号）。
 * @details    由平台经后端契约的 selectionRevoked 回调进入；实现复位该
 *             模式的 owns 状态与数据，并按 Qt 顺序发射模式专用信号
 *             （dataChanged/selectionChanged）再发射 changed(mode)。
 * @param      ud   后端用户数据（可忽略）。
 * @param      mode XClipboardMode 取值。
 */
void XClipboard_backendSelectionRevoked(void* ud, int mode);

void XClipboard_setText(XClipboard* self, const XString* text, XClipboardMode mode);

/* ==================== MIME 数据（对标 QClipboard::mimeData / setMimeData） ==================== */

/**
 * @brief      读取指定模式下的 MIME 数据（对标 QClipboard::mimeData）。
 * @return     内部借用指针，由 XClipboard 拥有，调用方不得释放；
 *             该模式无数据时返回 NULL。
 */
const XMimeData* XClipboard_mimeData(const XClipboard* self, XClipboardMode mode);

/**
 * @brief      设置指定模式的 MIME 数据（对标 QClipboard::setMimeData）。
 * @details    函数接管 data 所有权（此后由 XClipboard 统一释放），标记
 *             本进程所有权，发射 changed(mode) 与 dataChanged()；
 *             同时清空同模式旧文本。
 * @param      self 目标对象；可为 NULL。
 * @param      data XMimeData 对象（堆分配）；可为 NULL（等价 clear）。
 * @param      mode 目标模式。
 */
void XClipboard_setMimeData(XClipboard* self, XMimeData* data, XClipboardMode mode);

/* ==================== 图像 / 像素图（对标 QClipboard::setImage / image 等） ==================== */

/**
 * @brief      读取指定模式下的图像（对标 QClipboard::image）。
 * @return     新建 XImage（与内部共享引用计数像素），该模式无图像时返回
 *             NULL；调用方用 XImage_delete_base 释放。
 */
XImage* XClipboard_image(const XClipboard* self, XClipboardMode mode);

/**
 * @brief      设置指定模式的图像（对标 QClipboard::setImage）。
 * @details    等价于构造含图像的 XMimeData 后调用 setMimeData；进程内以
 *             application/x-qt-image 存储（回读走自有 mime 优先），同时
 *             平台后端接入时把图像 PNG 编码为 image/png 原子推送平台
 *             镜像（对标 Qt setImage 后 xcb 端 TARGETS 含 image/png），
 *             编码失败仅影响平台侧原子，进程内语义不变。
 * @param      self  目标对象；可为 NULL。
 * @param      image 源图像；可为 NULL。
 * @param      mode  目标模式。
 */
void XClipboard_setImage(XClipboard* self, const XImage* image, XClipboardMode mode);

/**
 * @brief      读取指定模式下的像素图（对标 QClipboard::pixmap）。
 * @return     新建 XPixmap（像素数据由图像转换），无图像时返回 NULL；
 *             调用方用 XPixmap_delete_base 释放。
 */
XPixmap* XClipboard_pixmap(const XClipboard* self, XClipboardMode mode);

/**
 * @brief      设置指定模式的像素图（对标 QClipboard::setPixmap）。
 * @details    内部转换为 XImage 后按 setMimeData 语义保存（与 setImage
 *             相同：进程内 x-qt-image 存储 + 平台侧 image/png 派生镜像）。
 * @param      self   目标对象；可为 NULL。
 * @param      pixmap 源像素图；可为 NULL。
 * @param      mode   目标模式。
 */
void XClipboard_setPixmap(XClipboard* self, const XPixmap* pixmap, XClipboardMode mode);

/* ==================== 信号（对标 QClipboard 全部信号） ==================== */

/**
 * @brief      剪贴板内容变化信号（对标 QClipboard::changed）。
 * @param      self 目标对象。
 * @param      mode 发生变化的模式。
 * @return     信号句柄；槽用 XObject_connect_2 连接。
 */
void* XClipboard_changed_signal(XClipboard* self, XClipboardMode mode);

/** @brief 选择缓冲变化信号（对标 QClipboard::selectionChanged）。 */
void* XClipboard_selectionChanged_signal(XClipboard* self);

/** @brief 查找缓冲变化信号（对标 QClipboard::findBufferChanged）。 */
void* XClipboard_findBufferChanged_signal(XClipboard* self);

/** @brief 数据内容变化信号（对标 QClipboard::dataChanged）。 */
void* XClipboard_dataChanged_signal(XClipboard* self);

#endif /* XCLIPBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XCLIPBOARD_H */
