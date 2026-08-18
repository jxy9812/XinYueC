/**
 * @file       XDevice.h
 * @brief      统一设备抽象公开门面（XDevice + XFd）。
 * @details    XDevice 是设备的多态基类（继承 XClass），同时是对外统一门面 API。
 *             具体设备（文件、目录、控制台、共享内存、串口、定时器、套接字、
 *             GPIO/ADC/PWM/CAN/I2C/SPI/USB 等）通过继承 XDevice、注册类别名即可
 *             接入；所有设备以 XFd 句柄对外暴露。只依赖 XinYueC 抽象层，禁止
 *             直接调用 Win32、POSIX 等平台 API。
 */
#ifndef XDEVICE_H
#define XDEVICE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XTypes.h"
#include "XClass.h"
/* 前向声明：设备打开选项中的路径使用 XString，避免在公共头引入完整 XString 头。 */
struct XString;
typedef struct XString XString;
struct XVarList;
typedef struct XVarList XVarList;

/* ============================================================================
 * 虚函数表定义
 * ============================================================================ */
/** @brief XDevice 类虚函数表；继承 XClass 后追加 Open 以后的新槽位。 */
XCLASS_DEFINE_BEGING(XDevice)
XCLASS_DEFINE_ENUM(XDevice, Open) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XDevice, Close),
XCLASS_DEFINE_ENUM(XDevice, Read),
XCLASS_DEFINE_ENUM(XDevice, Write),
XCLASS_DEFINE_ENUM(XDevice, Seek),
XCLASS_DEFINE_ENUM(XDevice, Flush),
XCLASS_DEFINE_ENUM(XDevice, Resize),
XCLASS_DEFINE_ENUM(XDevice, SetProperty),
XCLASS_DEFINE_ENUM(XDevice, GetProperty),
XCLASS_DEFINE_ENUM(XDevice, QueryProperty),
XCLASS_DEFINE_ENUM(XDevice, Control),
XCLASS_DEFINE_END(XDevice)

/* ============================================================================
 * 设备类型 / 能力 / 属性 / 命令 / 状态
 * ============================================================================ */
/**
 * @brief 设备类型枚举。
 * @details XDeviceType_Class 表示外部注册设备，只允许通过类别名字符串打开。
 */
typedef enum XDeviceType
{
    XDeviceType_Free    = 0, /**< 空闲槽位，不是有效设备。 */
    XDeviceType_File,        /**< 普通文件。 */
    XDeviceType_Socket,      /**< 网络套接字（TCP/UDP）。 */
    XDeviceType_Serial,      /**< 串口设备。 */
    XDeviceType_Timer,       /**< 定时器。 */
    XDeviceType_Dir,         /**< 目录迭代器。 */
    XDeviceType_Console,     /**< 控制台。 */
    XDeviceType_Mapping,     /**< 共享内存映射段。 */
    XDeviceType_Class,       /**< 外部注册设备，按类别名打开。 */
    XDeviceType_Count        /**< 类型数量边界，不是有效设备。 */
} XDeviceType;

/**
 * @brief 设备能力位集合。
 * @details 每个位代表一个通用 I/O 能力；具体设备可组合多个能力位。
 */
typedef enum XDeviceCap
{
    XDeviceCap_None   = 0x00, /**< 无通用 I/O 能力，只支持属性和命令。 */
    XDeviceCap_Read   = 0x01, /**< 支持 XDevice_read 同步读取。 */
    XDeviceCap_Write  = 0x02, /**< 支持 XDevice_write 同步写入。 */
    XDeviceCap_Seek   = 0x04, /**< 支持 XDevice_seek 随机定位。 */
    XDeviceCap_Flush  = 0x08, /**< 支持 XDevice_flush 刷新缓冲。 */
    XDeviceCap_Resize = 0x10, /**< 支持 XDevice_resize 调整大小。 */
    XDeviceCap_Async  = 0x20  /**< 支持异步 I/O。 */
} XDeviceCap;

/**
 * @brief 设备打开标志位集合。
 * @details 用于控制打开行为和后续 I/O 模式；具体设备可组合多个标志位。
 */
typedef enum XDeviceOpenFlag
{
    XDeviceOpenFlag_None        = 0x00, /**< 无特殊打开标志。 */
    XDeviceOpenFlag_NonBlocking = 0x01, /**< 以非阻塞模式打开；设备不支持时打开失败。 */
    XDeviceOpenFlag_Exclusive   = 0x02  /**< 独占打开；已被其它对象占用的设备将打开失败。 */
} XDeviceOpenFlag;

/**
 * @brief 设备属性编号枚举。
 * @details 设备专有属性从 XDeviceProperty_Count 起分配。
 */
typedef enum XDeviceProperty
{
    XDeviceProperty_None        = 0,            /**< 空属性。 */
    XDeviceProperty_OpenMode    = 0x0001,       /**< 打开模式，值为 int（对齐 XIODeviceBaseMode）。 */
    XDeviceProperty_IoMode      = 0x0002,       /**< I/O 模式，值为 XDeviceIoMode。 */
    XDeviceProperty_State       = 0x0003,       /**< 设备状态，值为 XDeviceState。 */
    XDeviceProperty_NonBlocking = 0x0004,       /**< 是否非阻塞，值为 bool。 */
    XDeviceProperty_LastError   = 0x0005,       /**< 最近一次错误码，值为 int。 */
    XDeviceProperty_Size         = 0x0006,       /**< 设备当前大小（字节），值为 int64_t。 */
    XDeviceProperty_NativeHandle = 0x0100,      /**< 原生平台句柄，值为借用 void*。 */
    XDeviceProperty_DeviceBase  = 0x00010000,   /**< 设备专有属性保留编号。 */
    XDeviceProperty_Count                         /**< 父类属性数量；子类从此值继续编号。 */
} XDeviceProperty;

/**
 * @brief 设备命令编号枚举。
 * @details 设备专有命令从父类命令枚举的 Count 值起继续分配。
 */
typedef enum XDeviceCommand
{
    XDeviceCommand_None       = 0,            /**< 空命令；in/out 均为 NULL。 */
    XDeviceCommand_Cancel     = 0x0001,       /**< 取消在途异步 I/O；in/out 均为 NULL。 */
    XDeviceCommand_Poll       = 0x0002,       /**< 查询设备就绪状态；in 为 NULL，out 为 XVarList(bool)。 */
    XDeviceCommand_Count                      /**< 父类命令数量；子类从此值继续编号。 */
} XDeviceCommand;


/**
 * @brief 设备定位基准枚举。
 */
typedef enum XDeviceSeekWhence
{
    XDeviceSeekWhence_Begin   = 0, /**< 从起始位置开始定位。 */
    XDeviceSeekWhence_Current = 1, /**< 从当前位置开始定位。 */
    XDeviceSeekWhence_End     = 2  /**< 从末尾开始定位。 */
} XDeviceSeekWhence;

/**
 * @brief 设备 I/O 模式枚举。
 */
typedef enum XDeviceIoMode
{
    XDeviceIoMode_Sync  = 0, /**< 同步 I/O。 */
    XDeviceIoMode_Async = 1  /**< 异步 I/O。 */
} XDeviceIoMode;

/**
 * @brief 设备生命周期状态枚举。
 */
typedef enum XDeviceState
{
    XDeviceState_Free     = 0, /**< 槽位空闲。 */
    XDeviceState_Opening  = 1, /**< 正在打开。 */
    XDeviceState_Inactive = 2, /**< 已分配但未激活。 */
    XDeviceState_Active   = 3, /**< 已激活，通用 I/O 可用。 */
    XDeviceState_Closing  = 4  /**< 正在关闭，拒绝新一轮 I/O。 */
} XDeviceState;

/**
 * @brief 设备错误码枚举。
 */
typedef enum XDeviceError
{
    XDeviceError_None             = 0,  /**< 无错误。 */
    XDeviceError_InvalidArgument  = -1, /**< 参数非法。 */
    XDeviceError_InvalidDescriptor= -2, /**< 句柄非法或槽位已释放。 */
    XDeviceError_OutOfMemory      = -3, /**< 内存不足。 */
    XDeviceError_NotSupported     = -4, /**< 不支持该操作/属性/命令。 */
    XDeviceError_NotFound         = -5, /**< 找不到指定类别或设备。 */
    XDeviceError_AlreadyOpen      = -6, /**< 设备已经打开。 */
    XDeviceError_NotOpen          = -7, /**< 设备未打开。 */
    XDeviceError_Timeout          = -8, /**< 操作超时。 */
    XDeviceError_IoFail           = -9, /**< 底层 I/O 失败。 */
    XDeviceError_Closed           = -10,/**< 设备已关闭或正在关闭。 */
    XDeviceError_Busy             = -11 /**< 设备忙。 */
} XDeviceError;

/* ============================================================================
 * 结构体与基类
 * ============================================================================ */
/**
 * @brief 打开设备时的通用选项。
 */
typedef struct XDeviceOpenOptions
{
    int m_openMode;                          /**< 打开模式位组合，见 XFileInfo 的 XDeviceFile_* 打开模式。0 表示设备默认模式。 */
    uint32_t m_flags;                        /**< XDeviceOpenFlag 位组合。 */
    int64_t m_timeoutMs;                     /**< 打开操作超时毫秒；0 表示设备默认或无限等待。 */
    const XString* m_target;                 /**< 设备目标名称；文件为路径、网络 Connect 为主机名，其它设备按各自契约解释；仅在 XDevice_open 调用期间借用。 */
} XDeviceOpenOptions;

/**
 * @brief 设备基类；所有具体设备类必须把它作为第一个成员。
 * @details 虚函数调度由 XDevice 内部完成（_base 入口不对外暴露），
 *          外部统一使用 XDevice_open/openClass 创建 XFd 句柄。
 */
typedef struct XDevice
{
    XClass m_class;                            /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDeviceType m_type;                        /**< 设备类型，取自 XDeviceType；内置设备由对应设备初始化时设置，外部注册设备为 XDeviceType_Class。 */
    XDeviceOpenOptions* m_defaultOpenOptions;  /**< 默认打开选项；借用指针，可为 NULL；子类可扩展 XDeviceOpenOptions 结构，并在此存储扩展结构体第一个成员 m_base 的地址。 */
    uint32_t m_capabilities;                   /**< XDeviceCap 位集合，表示设备通用 I/O 能力。 */
    bool m_registered;                         /**< 该设备类对象是否已注册到 XDevice 注册表。 */
    uint32_t m_refCount;                       /**< 设备类对象引用计数；由 XDevice_ref/XDevice_unref 管理，open 成功 +1、close -1。注册设备为静态单例，计数归零不释放；未注册的堆对象计数归零时自动释放。 */
} XDevice;
/* ============================================================================
 * 设备打开上下文
 * ============================================================================ */
/**
 * @brief 设备打开上下文基类。
 * @details 每个 XDevice_open/openClass 成功调用由 Open 虚函数分配一份打开上下文；
 *          子类必须把本结构体作为第一个成员，按需扩展设备专有状态。fd 的 handle
 *          字段保存本结构体指针，生命周期由对应设备类管理，Close 虚函数负责释放。
 */
typedef struct XDeviceContext
{
    XDevice* m_device;                  /**< 所属设备类对象（借用，由 openClass 填充）。 */
    uint16_t m_state      : 3;          /**< 设备生命周期状态（XDeviceState）。 */
    uint16_t m_ioMode     : 1;          /**< I/O 模式（XDeviceIoMode）。 */
    uint16_t m_pendingOps : 8;          /**< 在途异步操作数。 */
    uint16_t m_reserved   : 4;          /**< 预留位，必须为 0。 */
    int16_t  m_lastError;               /**< 最近一次错误码（XDeviceError）。 */
    XFd      m_fd;                      /**< 持有本上下文的统一描述符；打开阶段可由子类预先绑定。 */
} XDeviceContext;


/* ============================================================================
 * 虚函数指针类型
 * ============================================================================ */
typedef XDeviceContext* (*XDeviceOpenFn)(XDevice* self, const XDeviceOpenOptions* opts, int* err);
typedef void  (*XDeviceCloseFn)(XDevice* self, XDeviceContext* ctx);
typedef int64_t (*XDeviceReadFn)(XDevice* self, XDeviceContext* ctx, void* buffer, int64_t size);
typedef int64_t (*XDeviceWriteFn)(XDevice* self, XDeviceContext* ctx, const void* data, int64_t size);
typedef int64_t (*XDeviceSeekFn)(XDevice* self, XDeviceContext* ctx, int64_t offset, int whence);
typedef bool  (*XDeviceFlushFn)(XDevice* self, XDeviceContext* ctx);
typedef bool  (*XDeviceResizeFn)(XDevice* self, XDeviceContext* ctx, int64_t size);
typedef bool  (*XDeviceSetPropertyFn)(XDevice* self, XDeviceContext* ctx, uint32_t property, const XVariant* value);
typedef bool  (*XDeviceGetPropertyFn)(XDevice* self, XDeviceContext* ctx, uint32_t property, XVariant* value);
typedef bool  (*XDeviceQueryPropertyFn)(XDevice* self, XDeviceContext* ctx, uint32_t property, XVariant* value);
typedef bool  (*XDeviceControlFn)(XDevice* self, XDeviceContext* ctx, uint32_t command, const XVarList* in, XVarList* out);

/**
 * @brief 初始化 XDevice 类的共享虚函数表。
 * @return 指向共享虚函数表的指针；失败返回 NULL。
 * @note   具体设备类别在自己的 class_init 中调用 XVTABLE_INHERIT_XCLASS(XDevice)
 *         继承本表，再追加/重载自己的函数指针。
 */
XVtable* XDevice_class_init(void);

/**
 * @brief 初始化一个已分配的 XDevice 设备对象。
 * @param self 待初始化的设备对象；不能为 NULL，需保证至少 sizeof(XDevice) 字节。
 * @note   初始化后 m_capabilities 为 0；具体设备类通常在 init 中设置能力位并切换 vtable。
 */
void XDevice_init(XDevice* self);

/**
 * @brief 在堆上创建设备基类对象。
 * @return 新创建设备对象；失败返回 NULL。
 * @note   返回的是新对象，调用方不再使用时调用 XDevice_delete_base 释放。
 */
XDevice* XDevice_create(void);

/**
 * @brief 增加设备类对象的引用计数。
 * @param self 设备类对象；借用指针，可为 NULL（空指针直接返回）。
 * @note   一般不需要外部直接调用；XDevice_open/openClass 成功打开后内部会自动 +1。
 */
void XDevice_ref(XDevice* self);

/**
 * @brief 减少设备类对象的引用计数。
 * @param self 设备类对象；借用指针，可为 NULL（空指针直接返回）。
 * @note   每次与 XDevice_ref 成对调用；计数归零：若对象为未注册的堆对象则自动释放，
 *         否则仅回到 0 不清除（注册单例由注册表持有生命周期）。
 */
void XDevice_unref(XDevice* self);

/** @brief 复用 XClass 的 deinit 基类虚函数实现。 */
#define XDevice_deinit_base XClass_deinit_base
/** @brief 复用 XClass 的 delete 基类虚函数实现。 */
#define XDevice_delete_base XClass_delete_base
/** @brief 复用 XClass 的 copy 基类虚函数实现。 */
#define XDevice_copy_base   XClass_copy_base
/** @brief 复用 XClass 的 move 基类虚函数实现。 */
#define XDevice_move_base   XClass_move_base

/* ============================================================================
 * 统一公共门面 API
 * ============================================================================ */
/**
 * @brief 按设备类型打开设备。
 * @details 内部通过类型名查找已注册的 XDevice 设备类，再调用其 Open 虚函数。
 * @param type 设备类型；不能是 XDeviceType_Free 或 XDeviceType_Count。
 * @param opts 打开选项；借用指针，可为 NULL；NULL 表示使用默认选项。
 * @param err  错误码输出；借用指针，可为 NULL。
 * @return 打开的 XFd 句柄；成功返回大于等于 0 的句柄，失败返回 XFD_INVALID 并写入 err。
 * @note   返回句柄由调用方持有，使用完调用 XDevice_close 释放。
 */
XFd XDevice_open(XDeviceType type, const XDeviceOpenOptions* opts, int* err);

/**
 * @brief 按设备类别名打开设备。
 * @details 与 XDevice_open 行为一致，但通过字符串类别名查找设备类，可打开任意已注册的新设备。
 * @param className 设备类别名（例如 "file"、"socket"、"gpio"）；借用指针，不能为 NULL 或空串。
 * @param opts      打开选项；借用指针，可为 NULL；NULL 表示使用默认选项。
 * @param err       错误码输出；借用指针，可为 NULL。
 * @return 打开的 XFd 句柄；成功返回大于等于 0 的句柄，失败返回 XFD_INVALID 并写入 err。
 * @note   返回句柄由调用方持有，使用完调用 XDevice_close 释放。
 */
XFd XDevice_openClass(const char* className, const XDeviceOpenOptions* opts, int* err);

/**
 * @brief 关闭设备并回收描述符槽位。
 * @param fd 待关闭设备的 XFd 句柄；XFD_INVALID 时函数直接返回。
 * @note   关闭后 fd 立即失效，重复 close 由 XFileDescriptor 去重保护。
 */
void XDevice_close(XFd fd);

/**
 * @brief 从设备读取数据。
 * @param fd     设备句柄；必须由 XDevice_open/openClass 返回。
 * @param buffer 输出缓冲区；借用指针，不能为 NULL，至少 size 字节。
 * @param size   请求读取的字节数；必须大于等于 0。
 * @return 已读取字节数；EOF 返回 0，失败返回 -1 并可通过 XDevice_lastError 查看原因。
 */
int64_t XDevice_read(XFd fd, void* buffer, int64_t size);

/**
 * @brief 向设备写入数据。
 * @param fd   设备句柄；必须由 XDevice_open/openClass 返回。
 * @param data 待写入数据；借用指针，不能为 NULL，至少 size 字节。
 * @param size 请求写入的字节数；必须大于等于 0。
 * @return 已写入字节数；失败返回 -1 并可通过 XDevice_lastError 查看原因。
 */
int64_t XDevice_write(XFd fd, const void* data, int64_t size);

/**
 * @brief 定位设备读写位置。
 * @param fd     设备句柄；必须由 XDevice_open/openClass 返回。
 * @param offset 新位置偏移量；单位字节，负值合法性取决于 whence。
 * @param whence 定位基准，取自 XDeviceSeekWhence 枚举。
 * @return 后的绝对位置；不支持定位或失败返回 -1。
 */
int64_t XDevice_seek(XFd fd, int64_t offset, XDeviceSeekWhence whence);

/**
 * @brief 刷新设备缓冲。
 * @param fd 设备句柄；必须由 XDevice_open/openClass 返回。
 * @return 刷新成功返回 true，失败返回 false。
 */
bool XDevice_flush(XFd fd);

/**
 * @brief 调整设备大小。
 * @param fd   设备句柄；必须由 XDevice_open/openClass 返回。
 * @param size 目标字节数；必须大于等于 0。
 * @return 调整成功返回 true，失败返回 false。
 */
bool XDevice_resize(XFd fd, int64_t size);

/**
 * @brief 设置设备属性。
 * @param fd       设备句柄；必须由 XDevice_open/openClass 返回。
 * @param property 属性编号。
 * @param value    属性新值；借用指针，不能为 NULL。
 * @return 设置成功返回 true，失败返回 false 且属性保持不变。
 */
bool XDevice_setProperty(XFd fd, XDeviceProperty property, const XVariant* value);

/**
 * @brief 读取设备属性。
 * @param fd       设备句柄；必须由 XDevice_open/openClass 返回。
 * @param property 属性编号。
 * @param value    接收属性值的输出参数；借用指针，不能为 NULL。
 * @return 读取成功返回 true，失败返回 false。
 */
bool XDevice_getProperty(XFd fd, XDeviceProperty property, XVariant* value);

/**
 * @brief 查询已打开设备属性并返回当前值。
 * @param fd       设备句柄；必须由 XDevice_open/openClass 返回。
 * @param property 属性编号。
 * @param value    属性值输出参数；借用指针，不能为 NULL，成功时填充当前值。
 * @return 查询成功返回 true，失败返回 false。
 */
bool XDevice_queryProperty(XFd fd, XDeviceProperty property, XVariant* value);

/**
 * @brief 执行设备专用命令。
 * @param fd      设备句柄；必须由 XDevice_open/openClass 返回。
 * @param command 命令码；通用命令见 XDeviceCommand，子类命令从父类 Count 值继续编号。
 * @param in      命令输入参数列表；借用指针，可为 NULL。参数顺序和实际类型由 command
 *                所属枚举值注释定义，调用期间不得释放或修改。
 * @param out     命令输出参数列表；借用指针，可为 NULL。调用方按命令注释预先创建
 *                对应类型和顺序的槽位，设备仅在调用期间覆写槽位中的值。
 * @return 命令执行成功返回 true，失败返回 false。
 * @note XDevice_control 不取得 in/out 的所有权，也不保存其地址；调用方负责在返回后
 *       使用 XVarList_delete 释放由 XVarList_Create 创建的列表。
 */
bool XDevice_control(XFd fd, uint32_t command, const XVarList* in, XVarList* out);

/**
 * @brief 读取设备最近一次错误码。
 * @param fd 设备句柄；必须由 XDevice_open/openClass 返回；XFD_INVALID 时返回 XDeviceError_InvalidDescriptor。
 * @return 设备最近一次错误码，取自 XDeviceError 枚举。
 */
int XDevice_lastError(XFd fd);

/**
 * @brief 获取 fd 对应的 XDevice 设备类对象。
 * @param fd 设备句柄；必须由 XDevice_open/openClass 返回。
 * @return 设备类对象（借用指针，由描述符槽位持有，调用方不得释放；无效句柄返回 NULL）。
 */
XDevice* XDevice_class(XFd fd);

/**
 * @brief 获取 fd 对应的设备打开上下文句柄。
 * @param fd 设备句柄；必须由 XDevice_open/openClass 返回。
 * @return Open 虚函数返回的 XDeviceContext 打开上下文基类指针（借用指针，由描述符持有，调用方不得释放；无效句柄返回 NULL）。
 * @note Close 虚函数执行期间仍可取得上下文，供子类取消平台异步操作；Close 返回后 fd 立即失效。
 */
XDeviceContext* XDevice_handle(XFd fd);

/* ============================================================================
 * 设备类别注册 / 查找
 * ============================================================================ */
/**
 * @brief 注册一个 XDevice 设备类别。
 * @param device 已初始化的 XDevice 设备对象；借用指针，不能为 NULL，类名必须已配置。
 * @return 注册成功返回 true；参数非法、类名为空或重复时返回 false。
 * @note   静态注册表容量固定，不动态加载。
 */
bool XDevice_register(XDevice* device);

/**
 * @brief 按类别名查找已注册的 XDevice 设备对象。
 * @param className 设备类别名；借用指针，不能为 NULL 或空串。
 * @return 找到返回设备对象（借用指针，由注册表持有），找不到返回 NULL。
 */
XDevice* XDevice_find(const char* className);

/**
 * @brief 把内置设备类型映射到设备类别名。
 * @param type 内置设备类型。
 * @return 对应类别名的借用指针；type 非法时返回 NULL。
 */
const char* XDevice_typeClassName(XDeviceType type);

#ifdef __cplusplus
}
#endif
#endif /* XDEVICE_H */
