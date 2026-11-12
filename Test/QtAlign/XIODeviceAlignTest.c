/**
 * @file       XIODeviceAlignTest.c
 * @brief      XIODevice 与 Qt 6.8 QIODevice 对齐契约测试。
 * @details    覆盖：OpenMode 位编码、open/close 复位、read/peek/skip 的错误契约、
 *             readAll 的顺序设备分支、readLine 的 '\0' 终止与 maxlen<2 拒绝、
 *             seek/pos/size/atEnd、事务（含顺序设备）、ungetChar、写契约、
 *             信号（aboutToClose 发射时机）、errorString、canReadLine、
 *             Text 模式剥 '\r'、waitFor* 基类语义、父子所有权。
 *
 *             **参考口径**全部来自本机 Qt 6.8.0 实测（本轮探针）与 qtbase
 *             v6.8.0 `src/corelib/io/qiodevice.cpp` 原文：
 *             - 关闭态 `read(buf,0)` / `read(buf,4)` / `peek` / `skip` 均为 **-1**
 *               （CHECK_READABLE 先于 CHECK_MAXLEN）；
 *             - 打开态 `read(buf,0) == 0`、`write(x,0) == 0`、`write(x,-1) == -1`；
 *             - `readLine(buf,3)` 读到 2 字节且 `data[2]=='\0'`；`readLine(buf,1)`
 *               与 `readLine(buf,0)` 返回 -1（CHECK_LINEMAXLEN：maxSize<2）；
 *             - `readLine` 在 EOF：随机访问设备 **-1**、顺序设备 **0**
 *               （与 readLineData 的 `isSequential ? lastReadReturn : -1` 一致，
 *               文档写的 "0" 与实测不符）；
 *             - 顺序设备 `bytesAvailable()` 恒 0 时 `readAll()` 仍能读全（循环
 *               readData 直到 <=0）；
 *             - 事务：随机访问设备 rollback 后同一段数据可重读，commit 后位置前移；
 *               顺序设备同样可重读；重复 startTransaction 不改原点；
 *             - `ungetChar` 后 `bytesAvailable()+1`、`pos-1`，下一次读拿到该字符；
 *               事务进行中调用被忽略。**本机 Qt 6.8.0 在多字节 peek/read 上会跳过
 *               一个字节（QRingBuffer::reserveFront 与读游标的实现缺陷）；X 侧按
 *               Qt 文档契约实现「放回的字符就是下一个字节」，不放行数据丢失。**
 *             - 基类 `waitForReadyRead/waitForBytesWritten` 立即返回 false；
 *             - 默认 `errorString()` 为 "Unknown error"。
 *
 * @note       被测设备是本文件内的最小内存设备（QBuffer 对位）：只重载
 *             ReadData/WriteData/Seek/Size/IsSequential，**不重载 Pos**——这正是
 *             Qt 文档要求的子类契约（"The current position is maintained internally
 *             by QIODevice, so reimplementing this function is not necessary"）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"           /* 只查不建缓冲 API（P0-57 断言用） */
#include "XByteArray.h"
#include "XContainer.h"
#include "XMemory.h"
#include "XObject.h"
#include "XString.h"
#include "XVtable.h"
#include "XNetwork_config.h"
#include <string.h>

#if XNETWORK_ON && XNETWORK_ABSTRACT_SOCKET_ON
#include "XTcpSocket.h"
#define XIO_ALIGN_HAS_SOCKET 1
#else
#define XIO_ALIGN_HAS_SOCKET 0
#endif

/* ==================== 最小内存设备（QBuffer 对位） ==================== */

/** @brief 测试设备结构：支撑数据 + 设备游标。 */
typedef struct XAlignMemDevice
{
    XIODevice m_base;       /**< 基类，必须是第一个成员。 */
    XByteArray* m_buf;      /**< 支撑数据（拥有）。 */
    int64_t m_cursor;       /**< 设备游标（readData/writeData/seek 维护）。 */
    bool m_sequential;      /**< 为 true 时模拟顺序设备（不可 seek、size 不可知）。 */
    int m_readDataCalls;    /**< readData 调用次数（P0-08 回填分支的旁证）。 */
} XAlignMemDevice;

/** @brief 设备析构计数：用于父子所有权用例（父析构必须带走子对象）。 */
static int s_memDeleted = 0;

static void XAlignMem_deinit(XAlignMemDevice* dev);
static bool XAlignMem_isSequential(const XIODevice* dev);
static int64_t XAlignMem_size(const XIODevice* dev);
static bool XAlignMem_seek(XIODevice* dev, int64_t pos);
static int64_t XAlignMem_readData(XIODevice* dev, char* data, int64_t maxlen);
static int64_t XAlignMem_writeData(XIODevice* dev, const char* data, int64_t len);

XCLASS_DEFINE_BEGING(XAlignMemDevice)
XCLASS_DEFINE_EXTEND_END(XAlignMemDevice, XIODevice)

static XVtable* XAlignMemDevice_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAlignMemDevice)
    XVTABLE_INHERIT_XCLASS(XIODevice);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XAlignMem_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, XAlignMem_isSequential);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Size, XAlignMem_size);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Seek, XAlignMem_seek);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, XAlignMem_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, XAlignMem_writeData);
    return XVTABLE_DEFAULT;
}

/** @brief 创建测试设备；init 为初始内容（可为 NULL）。 */
static XAlignMemDevice* XAlignMem_create(const char* init, size_t len, bool sequential)
{
    XAlignMemDevice* dev = (XAlignMemDevice*)XMemory_malloc(sizeof(XAlignMemDevice),
                                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!dev) return NULL;
    XIODevice_init(&dev->m_base);
    XClassSetVtable(dev, XAlignMemDevice);
    Set_Class_Memory(dev, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(dev, true);
    dev->m_buf = XByteArray_create();
    dev->m_cursor = 0;
    dev->m_sequential = sequential;
    dev->m_readDataCalls = 0;
    if (!dev->m_buf) {
        XClassDelete((XClass*)dev);
        return NULL;
    }
    if (init && len > 0) {
        if (!XByteArray_resize_base(dev->m_buf, len)) {
            XClassDelete((XClass*)dev);
            return NULL;
        }
        memcpy(XContainerDataAddr(dev->m_buf), init, len);
    }
    return dev;
}

static void XAlignMem_deinit(XAlignMemDevice* dev)
{
    if (!dev) return;
    if (dev->m_buf) {
        XClassDelete(dev->m_buf);
        dev->m_buf = NULL;
    }
    ++s_memDeleted;
    XClass_Deinit_Parent(XIODevice, dev);
}

static bool XAlignMem_isSequential(const XIODevice* io)
{
    const XAlignMemDevice* dev = (const XAlignMemDevice*)io;
    return dev ? dev->m_sequential : false;
}

static int64_t XAlignMem_size(const XIODevice* io)
{
    const XAlignMemDevice* dev = (const XAlignMemDevice*)io;
    /* 顺序设备的长度不可知（Qt：顺序设备 size() 走 bytesAvailable）。 */
    if (!dev || dev->m_sequential || !dev->m_buf) return 0;
    return (int64_t)XByteArray_size_base(dev->m_buf);
}

static bool XAlignMem_seek(XIODevice* io, int64_t pos)
{
    XAlignMemDevice* dev = (XAlignMemDevice*)io;
    if (!dev) return false;
    if (dev->m_sequential) return false;             /* 顺序设备不可 seek */
    /* 对齐 QBuffer::seek：先移动设备自身位置，再调基类实现登记（**不能**调
     * XIODevice_seek_base——那是虚表派发，会回到本函数造成无限递归）。 */
    if (!XIODevice_seek(io, pos)) return false;
    if (dev->m_buf && pos > (int64_t)XByteArray_size_base(dev->m_buf))
        return false;                                /* QBuffer 实测：越界 seek 返回 false */
    dev->m_cursor = pos;
    return true;
}

static int64_t XAlignMem_readData(XIODevice* io, char* data, int64_t maxlen)
{
    XAlignMemDevice* dev = (XAlignMemDevice*)io;
    const char* base;
    int64_t size, n;
    if (!dev || !data || maxlen <= 0 || !dev->m_buf) return 0;
    ++dev->m_readDataCalls;
    size = (int64_t)XByteArray_size_base(dev->m_buf);
    if (dev->m_cursor >= size) return 0;
    n = size - dev->m_cursor;
    if (n > maxlen) n = maxlen;
    /* XContainerDataAddr 是没有外层括号的三元表达式：必须先落成局部指针再加偏移，
     * 否则 `(char*)XContainerDataAddr(x) + off` 的偏移会落到 NULL 分支而被丢弃。 */
    base = (const char*)XContainerDataAddr(dev->m_buf);
    memcpy(data, base + dev->m_cursor, (size_t)n);
    dev->m_cursor += n;
    return n;
}

static int64_t XAlignMem_writeData(XIODevice* io, const char* data, int64_t len)
{
    XAlignMemDevice* dev = (XAlignMemDevice*)io;
    char* base;
    size_t old;
    if (!dev || !data || len <= 0 || !dev->m_buf) return 0;
    old = XByteArray_size_base(dev->m_buf);
    if ((size_t)dev->m_cursor + (size_t)len > old) {
        if (!XByteArray_resize_base(dev->m_buf, (size_t)dev->m_cursor + (size_t)len))
            return -1;
    }
    base = (char*)XContainerDataAddr(dev->m_buf);   /* 同上：先落指针再加偏移 */
    memcpy(base + dev->m_cursor, data, (size_t)len);
    dev->m_cursor += len;
    return len;
}

/* ==================== 信号计数槽 ==================== */

static int s_aboutToClose = 0;
static int s_readyRead = 0;
static int s_bytesWritten = 0;
static int s_readChannelFinished = 0;

static void xio_onAboutToClose(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++s_aboutToClose;
}
static void xio_onReadyRead(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++s_readyRead;
}
static void xio_onBytesWritten(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++s_bytesWritten;
}
static void xio_onReadChannelFinished(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++s_readChannelFinished;
}

/* ==================== 假套接字（引擎缓冲 + 读环两层） ==================== */

/**
 * @brief 模拟 QAbstractSocket 的两层缓冲：平台「引擎」待读字节 + XIODevice 读环。
 * @details 只用于锁定 XAbstractSocket 重载 bytesAvailable 的**公式**与 atEnd
 *          语义（gaps #27/#28）。真实实现见
 *          `XAbstractSocket.c` 的 `VXAbstractSocket_bytesAvailable`
 *          （`XIODevice_bytesAvailable(self) + socketEngineBytesAvailable(self)`）
 *          与 `socketDrainEngineReadBuffer`（引擎 → 读环）。
 *
 *          本类刻意**不重载 AtEnd**——与 Qt 的 QAbstractSocket 一致：atEnd
 *          继承基类实现，判据里的 bytesAvailable() 走虚函数（含引擎字节）。
 */
typedef struct XAlignFakeSocket
{
    XIODevice m_base;       /**< 基类，必须是第一个成员。 */
    XByteArray* m_engine;   /**< 引擎缓冲（拥有）：尚未搬进读环的字节。 */
    int64_t m_enginePos;    /**< 引擎缓冲已交付偏移。 */
} XAlignFakeSocket;

static void XAlignFakeSocket_deinit(XAlignFakeSocket* s);
static bool XAlignFakeSocket_isSequential(const XIODevice* io);
static int64_t XAlignFakeSocket_size(const XIODevice* io);
static int64_t XAlignFakeSocket_bytesAvailable(const XIODevice* io);
static int64_t XAlignFakeSocket_readData(XIODevice* io, char* data, int64_t maxlen);

XCLASS_DEFINE_BEGING(XAlignFakeSocket)
XCLASS_DEFINE_EXTEND_END(XAlignFakeSocket, XIODevice)

static XVtable* XAlignFakeSocket_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAlignFakeSocket)
    XVTABLE_INHERIT_XCLASS(XIODevice);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XAlignFakeSocket_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, XAlignFakeSocket_isSequential);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Size, XAlignFakeSocket_size);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, XAlignFakeSocket_bytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, XAlignFakeSocket_readData);
    return XVTABLE_DEFAULT;
}

static XAlignFakeSocket* XAlignFakeSocket_create(const char* engine, size_t len)
{
    XAlignFakeSocket* s = (XAlignFakeSocket*)XMemory_malloc(sizeof(XAlignFakeSocket),
                                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!s) return NULL;
    XIODevice_init(&s->m_base);
    XClassSetVtable(s, XAlignFakeSocket);
    Set_Class_Memory(s, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(s, true);
    s->m_engine = XByteArray_create();
    s->m_enginePos = 0;
    if (!s->m_engine) {
        XClassDelete((XClass*)s);
        return NULL;
    }
    if (engine && len > 0) {
        if (!XByteArray_resize_base(s->m_engine, len)) {
            XClassDelete((XClass*)s);
            return NULL;
        }
        memcpy(XContainerDataAddr(s->m_engine), engine, len);
    }
    return s;
}

static void XAlignFakeSocket_deinit(XAlignFakeSocket* s)
{
    if (!s) return;
    if (s->m_engine) {
        XClassDelete(s->m_engine);
        s->m_engine = NULL;
    }
    XClass_Deinit_Parent(XIODevice, s);
}

static bool XAlignFakeSocket_isSequential(const XIODevice* io)
{
    (void)io;
    return true;    /* 套接字是顺序设备（size() 不可知）。 */
}

static int64_t XAlignFakeSocket_size(const XIODevice* io)
{
    (void)io;
    return 0;
}

/** @brief 引擎里尚未交付的字节数（真实实现取平台 ReadFinishedBytes）。 */
static int64_t xalignFakeEnginePending(const XAlignFakeSocket* s)
{
    int64_t total;
    if (!s || !s->m_engine) return 0;
    total = (int64_t)XByteArray_size_base(s->m_engine);
    return total > s->m_enginePos ? total - s->m_enginePos : 0;
}

static int64_t XAlignFakeSocket_bytesAvailable(const XIODevice* io)
{
    const XAlignFakeSocket* s = (const XAlignFakeSocket*)io;
    /* 与 XAbstractSocket 同一条公式：基类（读环）+ 引擎待读。
     * 基类部分必须调**非虚**的 XIODevice_bytesAvailable（调 _base 会经
     * 虚表回到本函数 → 无限递归，正是历史不敢重载的原因）。 */
    return XIODevice_bytesAvailable(io) + xalignFakeEnginePending(s);
}

static int64_t XAlignFakeSocket_readData(XIODevice* io, char* data, int64_t maxlen)
{
    XAlignFakeSocket* s = (XAlignFakeSocket*)io;
    const char* base;
    int64_t pending, n;
    if (!s || !data || maxlen <= 0 || !s->m_engine) return 0;
    pending = xalignFakeEnginePending(s);
    if (pending <= 0) return 0;
    n = pending < maxlen ? pending : maxlen;
    base = (const char*)XContainerDataAddr(s->m_engine);
    memcpy(data, base + s->m_enginePos, (size_t)n);
    s->m_enginePos += n;
    return n;
}

/* ==================== 用例 ==================== */

/** @brief OpenMode 位编码必须与 Qt 6.8 逐值一致（qiodevicebase.h:16-27）。 */
XQ_TEST(XIODevice, openModeEncoding)
{
    XQ_EQ_I64(XIODevice_NotOpen, 0x0000);
    XQ_EQ_I64(XIODevice_ReadOnly, 0x0001);
    XQ_EQ_I64(XIODevice_WriteOnly, 0x0002);
    XQ_EQ_I64(XIODevice_ReadWrite, 0x0003);
    XQ_EQ_I64(XIODevice_Append, 0x0004);
    XQ_EQ_I64(XIODevice_Truncate, 0x0008);
    /* Qt 6.8 实测：Text=0x10 Unbuffered=0x20 NewOnly=0x40 ExistingOnly=0x80 */
    XQ_EQ_I64(XIODevice_Text, 0x0010);
    XQ_EQ_I64(XIODevice_Unbuffered, 0x0020);
    XQ_EQ_I64(XIODevice_NewOnly, 0x0040);
    XQ_EQ_I64(XIODevice_ExistingOnly, 0x0080);
    /* 历史坑回归锁：旧编码里 Text 恰好等于 Qt 的 ExistingOnly、NewOnly 等于 Qt 的
     * Unbuffered；这里断言四个 Qt 位互不相同，且本框架扩展位 Create 不与任何
     * Qt 位重合。 */
    XQ_CHECK(XIODevice_Text != XIODevice_ExistingOnly);
    XQ_CHECK(XIODevice_Unbuffered != XIODevice_NewOnly);
    XQ_CHECK((XIODevice_Create & 0x00FF) == 0);
    XQ_CHECK((XIODevice_Create & XIODevice_Text) == 0);
    XQ_CHECK((XIODevice_Create & XIODevice_Unbuffered) == 0);
    XQ_CHECK(XIODevice_Existing == XIODevice_ExistingOnly);
    XQ_EQ_I64(XIODevice_ReadWrite, XIODevice_ReadOnly | XIODevice_WriteOnly);
}

/** @brief 关闭态/打开态的通道数与 openMode 契约（对齐 Qt open/close）。 */
XQ_TEST(XIODevice, openCloseStateReset)
{
    XAlignMemDevice* dev = XAlignMem_create("0123456789", 10, false);
    XQ_REQUIRE(dev != NULL);
    /* 关闭态：未打开、通道数 0、pos/size 0 */
    XQ_CHECK(!XIODevice_isOpen((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_openMode((XIODevice*)dev), XIODevice_NotOpen);
    XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)dev), 0);
    XQ_EQ_I64(XIODevice_writeChannelCount((XIODevice*)dev), 0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 0);

    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_CHECK(XIODevice_isOpen((XIODevice*)dev));
    XQ_CHECK(XIODevice_isReadable((XIODevice*)dev));
    XQ_CHECK(!XIODevice_isWritable((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)dev), 1);
    XQ_EQ_I64(XIODevice_writeChannelCount((XIODevice*)dev), 0);

    /* 读一部分后 close：pos 归零、读通道丢弃（Qt close 置 pos=0 / 读通道 0） */
    {
        char b[4];
        XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 4), 4);
        XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 4);
        XIODevice_close_base((XIODevice*)dev);
        XQ_CHECK(!XIODevice_isOpen((XIODevice*)dev));
        XQ_EQ_I64(XIODevice_openMode((XIODevice*)dev), XIODevice_NotOpen);
        XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)dev), 0);
        XQ_EQ_I64(XIODevice_openMode((XIODevice*)dev) & XIODevice_ReadOnly, 0);
    }

    /* 重新 open 必须从头开始读（Qt open 清读缓冲 + pos=0） */
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_WriteOnly | XIODevice_Text));
    XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)dev), 0);
    XQ_EQ_I64(XIODevice_writeChannelCount((XIODevice*)dev), 1);
    XQ_CHECK(XIODevice_isTextModeEnabled((XIODevice*)dev));
    XQ_CHECK((XIODevice_openMode((XIODevice*)dev) & XIODevice_Text) != 0);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly | XIODevice_Append));
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 10);   /* Append ⇒ pos = size() */

    XClassDelete((XClass*)dev);
}

/** @brief 读/窥探/跳过的错误契约（not-open 与不可读一律 -1）。 */
XQ_TEST(XIODevice, readErrorContract)
{
    char b[8];
    XAlignMemDevice* dev = XAlignMem_create("abcdef", 6, false);
    XQ_REQUIRE(dev != NULL);

    /* 关闭态：read(buf,0) 也是 -1（CHECK_READABLE 先于 CHECK_MAXLEN） */
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 0), -1);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 4), -1);
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)dev, b, 4), -1);
    XQ_EQ_I64(XIODevice_skip((XIODevice*)dev, 2), -1);
    XQ_CHECK(XIODevice_atEnd_base((XIODevice*)dev));

    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 0), 0);   /* 打开态空读为 0 */
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, -1), -1); /* 负长度是参数错误 */
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)dev, b, -1), -1);

    /* peek 不移动 pos，且 read 仍读到同样的数据 */
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)dev, b, 3), 3);
    XQ_CHECK(memcmp(b, "abc", 3) == 0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 0);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 3), 3);
    XQ_CHECK(memcmp(b, "abc", 3) == 0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 3);

    XClassDelete((XClass*)dev);
}

/** @brief 顺序设备：bytesAvailable 恒 0 时 readAll 仍要读全。 */
XQ_TEST(XIODevice, readAllSequential)
{
    XAlignMemDevice* dev = XAlignMem_create("hello world", 11, true);
    XByteArray* all;
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)dev), 0);

    all = XIODevice_readAll_3((XIODevice*)dev);
    XQ_CHECK_PTR(all);
    if (all) {
        XQ_EQ_SIZE(XByteArray_size_base(all), 11);
        XQ_CHECK(memcmp(XContainerDataAddr(all), "hello world", 11) == 0);
        XClassDelete((XClass*)all);
    }
    XQ_CHECK(XIODevice_atEnd_base((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 0);  /* 顺序设备无位置 */

    XClassDelete((XClass*)dev);
}

/** @brief readLine：'\0' 终止、maxSize<2 拒绝、无换行的残行与 EOF。 */
XQ_TEST(XIODevice, readLineContract)
{
    char lb[8];
    XAlignMemDevice* dev = XAlignMem_create("ab\ncdef", 7, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));

    memset(lb, 0x7f, sizeof lb);
    /* 最多读 maxlen-1 = 2 字节，并写结尾 '\0' */
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, lb, 3), 2);
    XQ_EQ_I64(lb[0], 'a');
    XQ_EQ_I64(lb[1], 'b');
    XQ_EQ_I64(lb[2], 0);

    /* maxSize < 2 一律 -1（Qt CHECK_LINEMAXLEN） */
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, lb, 1), -1);
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, lb, 0), -1);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 2);  /* 失败的调用不消费数据 */

    /* 剩下的 '\n' 独占一行 */
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, lb, 5), 1);
    XQ_EQ_I64(lb[0], '\n');
    XQ_EQ_I64(lb[1], 0);

    /* 无换行的残行：读到 EOF 也算一行 */
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, lb, 5), 4);
    XQ_CHECK(memcmp(lb, "cdef", 4) == 0);
    XQ_EQ_I64(lb[4], 0);

    /* EOF：随机访问设备返回 **-1**（本机 Qt 6.8.0 实测：readLineData 对非顺序
     * 设备在 EOF 返回 -1，readLine 原样透出——文档写的 "0" 与实现不符），
     * 但 data[0] 仍被写成 '\0'。 */
    memset(lb, 0x7f, sizeof lb);
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, lb, 5), -1);
    XQ_EQ_I64(lb[0], 0);

    XClassDelete((XClass*)dev);
}

/** @brief readLine 在顺序设备上 EOF 返回 0（Qt 实测：readLineData 返回 read 的结果 0）。 */
XQ_TEST(XIODevice, readLineSequentialEof)
{
    char lb[8];
    XAlignMemDevice* dev = XAlignMem_create("ab", 2, true);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, lb, 8), 2);
    XQ_CHECK(memcmp(lb, "ab", 2) == 0);
    XQ_EQ_I64(lb[2], 0);
    memset(lb, 0x7f, sizeof lb);
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, lb, 8), 0);   /* 顺序设备 EOF -> 0 */
    XQ_EQ_I64(lb[0], 0);
    XClassDelete((XClass*)dev);
}

/** @brief XByteArray 版 readLine（含 readLine_4 的长度上限）。 */
XQ_TEST(XIODevice, readLineByteArray)
{
    XByteArray* line;
    XAlignMemDevice* dev = XAlignMem_create("line1\nline2\n", 12, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));

    line = XIODevice_readLine_3((XIODevice*)dev);
    XQ_CHECK_PTR(line);
    if (line) {
        XQ_EQ_SIZE(XByteArray_size_base(line), 6);
        XQ_CHECK(memcmp(XContainerDataAddr(line), "line1\n", 6) == 0);
        XClassDelete((XClass*)line);
    }
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 6);

    /* maxlen=4 ⇒ 最多 3 个数据字节 */
    line = XIODevice_readLine_4((XIODevice*)dev, 4);
    XQ_CHECK_PTR(line);
    if (line) {
        XQ_EQ_SIZE(XByteArray_size_base(line), 3);
        XQ_CHECK(memcmp(XContainerDataAddr(line), "lin", 3) == 0);
        XClassDelete((XClass*)line);
    }
    /* maxlen=1 非法 ⇒ 空数组 */
    line = XIODevice_readLine_4((XIODevice*)dev, 1);
    XQ_CHECK_PTR(line);
    if (line) {
        XQ_EQ_SIZE(XByteArray_size_base(line), 0);
        XClassDelete((XClass*)line);
    }
    /* maxlen=0 ⇒ 不限长，读到行尾 */
    line = XIODevice_readLine_4((XIODevice*)dev, 0);
    XQ_CHECK_PTR(line);
    if (line) {
        XQ_EQ_SIZE(XByteArray_size_base(line), 3);   /* "e2\n" */
        XQ_CHECK(memcmp(XContainerDataAddr(line), "e2\n", 3) == 0);
        XClassDelete((XClass*)line);
    }

    XClassDelete((XClass*)dev);
}

/** @brief skip 先吃缓冲、再 seek 快路径；seek/pos/size/atEnd 边界。 */
XQ_TEST(XIODevice, skipSeekPosSize)
{
    char b[8];
    XAlignMemDevice* dev = XAlignMem_create("0123456789", 10, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));

    XQ_EQ_I64(XIODevice_size_base((XIODevice*)dev), 10);
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)dev), 10);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 0);
    XQ_CHECK(!XIODevice_atEnd_base((XIODevice*)dev));

    /* peek 会预读进内部缓冲；skip 必须先消费它，不能把同一段数据再读出来 */
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)dev, b, 4), 4);
    XQ_CHECK(memcmp(b, "0123", 4) == 0);
    XQ_EQ_I64(XIODevice_skip((XIODevice*)dev, 2), 2);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 2);
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)dev, b, 2), 2);
    XQ_CHECK(memcmp(b, "23", 2) == 0);

    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 2);
    XQ_CHECK(memcmp(b, "23", 2) == 0);

    /* skip 超过剩余长度：截到设备末尾（pos 已到 4，只剩 6 字节） */
    XQ_EQ_I64(XIODevice_skip((XIODevice*)dev, 100), 6);
    XQ_CHECK(XIODevice_atEnd_base((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 10);

    /* seek 边界：负值与越界都拒绝（QBuffer 实测同口径） */
    XQ_CHECK(!XIODevice_seek_base((XIODevice*)dev, 100));
    XQ_CHECK(!XIODevice_seek_base((XIODevice*)dev, -1));
    XQ_CHECK(XIODevice_reset_base((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 0);
    XQ_CHECK(XIODevice_seek_base((XIODevice*)dev, 5));
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 5);
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)dev), 5);

    XClassDelete((XClass*)dev);
}

/** @brief 事务：随机访问设备 rollback 重读、commit 前移、重复 start 不改原点。 */
XQ_TEST(XIODevice, transactionRandomAccess)
{
    char b[8];
    XAlignMemDevice* dev = XAlignMem_create("abcdefgh", 8, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));

    XQ_CHECK(!XIODevice_isTransactionStarted((XIODevice*)dev));
    XIODevice_commitTransaction((XIODevice*)dev);     /* 无事务：静默忽略 */
    XIODevice_rollbackTransaction((XIODevice*)dev);
    XQ_CHECK(!XIODevice_isTransactionStarted((XIODevice*)dev));

    XIODevice_startTransaction((XIODevice*)dev);
    XQ_CHECK(XIODevice_isTransactionStarted((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 3), 3);
    XQ_CHECK(memcmp(b, "abc", 3) == 0);

    /* 二次 start 不得覆盖原点（Qt：只告警不改状态） */
    XIODevice_startTransaction((XIODevice*)dev);
    XIODevice_rollbackTransaction((XIODevice*)dev);
    XQ_CHECK(!XIODevice_isTransactionStarted((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 3), 3);
    XQ_CHECK(memcmp(b, "abc", 3) == 0);   /* 回滚后同一段数据重读 */

    /* 事务中禁止 ungetChar / 切通道（位置停在 3：读了 "abc" 又重读了一遍） */
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 3);
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)dev), 5);
    XIODevice_startTransaction((XIODevice*)dev);
    XIODevice_ungetChar((XIODevice*)dev, 'Z');
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)dev), 5);   /* 事务中放回被忽略 */
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 3);
    XIODevice_setCurrentReadChannel((XIODevice*)dev, 7);
    XQ_EQ_I64(XIODevice_currentReadChannel((XIODevice*)dev), 0);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 1), 1);
    XQ_CHECK(b[0] == 'd');
    XIODevice_commitTransaction((XIODevice*)dev);

    /* commit 保留已读位置 */
    XQ_CHECK(!XIODevice_isTransactionStarted((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 1), 1);
    XQ_CHECK(b[0] == 'e');

    XClassDelete((XClass*)dev);
}

/** @brief 事务：顺序设备回滚必须能把同一段数据再读一遍。 */
XQ_TEST(XIODevice, transactionSequential)
{
    char b[8];
    XAlignMemDevice* dev = XAlignMem_create("abcdef", 6, true);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));

    XIODevice_startTransaction((XIODevice*)dev);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 3), 3);
    XQ_CHECK(memcmp(b, "abc", 3) == 0);
    XIODevice_rollbackTransaction((XIODevice*)dev);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 3), 3);
    XQ_CHECK(memcmp(b, "abc", 3) == 0);

    /* 事务中读 2 字节后提交：这 2 字节视为已消费，后续只剩 "f"。
     * （本机 Qt 6.8.0 实测同口径：commit 后读 2 字节只返回 "f" 这 1 个字节。） */
    XIODevice_startTransaction((XIODevice*)dev);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 2);
    XQ_CHECK(memcmp(b, "de", 2) == 0);
    XIODevice_commitTransaction((XIODevice*)dev);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 1);
    XQ_CHECK(b[0] == 'f');

    XClassDelete((XClass*)dev);
}

/** @brief ungetChar：对 bytesAvailable/peek/read 可见，并把 pos 回退一格。
 *  @note 按 Qt 契约，放回的字符**必须是刚读出的那个**（否则行为未定义）；
 *        本用例只放回刚读出的字节。 */
XQ_TEST(XIODevice, ungetCharContract)
{
    char b[4];
    XAlignMemDevice* dev = XAlignMem_create("abc", 3, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 2);
    XQ_CHECK(memcmp(b, "ab", 2) == 0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 2);

    XIODevice_ungetChar((XIODevice*)dev, 'b');                      /* 放回刚读出的 'b' */
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)dev), 2);   /* pos=1 ⇒ 剩 'b','c' */
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 1);              /* pos 回退一格 */
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)dev, b, 2), 2);
    XQ_CHECK(b[0] == 'b');                                          /* peek 也看得到放回的字符 */
    XQ_CHECK(b[1] == 'c');
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 1);              /* peek 不移动 pos */
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 2);
    XQ_CHECK(b[0] == 'b');
    XQ_CHECK(b[1] == 'c');
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 3);

    /* 连续 unget 是 LIFO（后放回的先读出） */
    XQ_CHECK(XIODevice_reset_base((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 3), 3);
    XQ_CHECK(memcmp(b, "abc", 3) == 0);
    XIODevice_ungetChar((XIODevice*)dev, 'c');
    XIODevice_ungetChar((XIODevice*)dev, 'b');
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 2);
    XQ_CHECK(b[0] == 'b');
    XQ_CHECK(b[1] == 'c');

    XClassDelete((XClass*)dev);
}

/** @brief 写契约：0 长度合法、负长度/只读拒绝、只写设备读接口一律 -1。 */
XQ_TEST(XIODevice, writeContract)
{
    char b[4];
    XAlignMemDevice* dev = XAlignMem_create(NULL, 0, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)dev, "x", 1), -1);   /* 只读设备不可写 */
    XIODevice_close_base((XIODevice*)dev);

    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_WriteOnly));
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)dev, "x", 0), 0);     /* 合法空写 */
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)dev, "x", -1), -1);   /* 负长度是参数错误 */
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 1), -1);       /* 只写设备不可读 */
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)dev, b, 1), -1);
    XQ_EQ_I64(XIODevice_skip((XIODevice*)dev, 1), -1);
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)dev, "abc", 3), 3);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 3);
    XQ_EQ_I64(XIODevice_write_3((XIODevice*)dev, "de"), 2);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 5);

    /* 写缓冲模型：本框架 write 直接下发，bytesToWrite 恒 0 */
    XQ_EQ_I64(XIODevice_bytesToWrite_base((XIODevice*)dev), 0);
    XQ_CHECK(XIODevice_flush((XIODevice*)dev));   /* 空写缓冲 flush 恒成功 */

    XClassDelete((XClass*)dev);
}

/** @brief 信号：aboutToClose 在 close 时发一次；基类读写不发 readyRead/bytesWritten。 */
XQ_TEST(XIODevice, signalContract)
{
    XObject* receiver;
    XAlignMemDevice* dev = XAlignMem_create("abcdef", 6, false);
    XQ_REQUIRE(dev != NULL);
    receiver = XObject_create();
    XQ_REQUIRE(receiver != NULL);

    s_aboutToClose = 0; s_readyRead = 0; s_bytesWritten = 0;
    XQ_CHECK_PTR(XObject_connect((XObject*)dev, XSignal(XIODevice_aboutToClose_signal),
                                   receiver, xio_onAboutToClose, XConnectionType_Direct));
    XQ_CHECK_PTR(XObject_connect((XObject*)dev, XSignal(XIODevice_readyRead_signal),
                                   receiver, xio_onReadyRead, XConnectionType_Direct));
    XQ_CHECK_PTR(XObject_connect((XObject*)dev, XSignal(XIODevice_bytesWritten_signal),
                                   receiver, xio_onBytesWritten, XConnectionType_Direct));

    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadWrite));
    {
        char b[4];
        XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 4), 4);
        XQ_EQ_I64(XIODevice_write_1((XIODevice*)dev, "zz", 2), 2);
    }
    /* 基类收发都不发信号：Qt 里 readyRead 由「新数据到达」触发、bytesWritten
     * 由真正完成一次设备写入的子类发射。 */
    XQ_EQ_I64(s_readyRead, 0);
    XQ_EQ_I64(s_bytesWritten, 0);
    XQ_EQ_I64(s_aboutToClose, 0);
    XIODevice_close_base((XIODevice*)dev);
    XQ_EQ_I64(s_aboutToClose, 1);
    XIODevice_close_base((XIODevice*)dev);   /* 已关闭再 close：不重复发 */
    XQ_EQ_I64(s_aboutToClose, 1);

    XClassDelete((XClass*)receiver);
    XClassDelete((XClass*)dev);
}

/** @brief errorString：默认文案、setErrorString 生效、open 清空。 */
XQ_TEST(XIODevice, errorStringContract)
{
    XString* err;
    XAlignMemDevice* dev = XAlignMem_create("abc", 3, false);
    XQ_REQUIRE(dev != NULL);

    err = XIODevice_errorString((XIODevice*)dev);
    XQ_CHECK_PTR(err);
    if (err) {
        XQ_EQ_STR(XString_toUtf8(err), "Unknown error");
        XClassDelete((XClass*)err);
    }

    XIODevice_setErrorString((XIODevice*)dev, "No such file or directory");
    err = XIODevice_errorString((XIODevice*)dev);
    XQ_CHECK_PTR(err);
    if (err) {
        XQ_EQ_STR(XString_toUtf8(err), "No such file or directory");
        XClassDelete((XClass*)err);
    }

    /* Qt：open() 会清空 errorString（qiodevice.cpp:767） */
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    err = XIODevice_errorString((XIODevice*)dev);
    XQ_CHECK_PTR(err);
    if (err) {
        XQ_EQ_STR(XString_toUtf8(err), "Unknown error");
        XClassDelete((XClass*)err);
    }

    XClassDelete((XClass*)dev);
}

/** @brief 父对象析构必须带走设备（QIODevice 的 parent 语义）。 */
XQ_TEST(XIODevice, parentOwnership)
{
    XObject* parent = XObject_create();
    XAlignMemDevice* dev;
    XQ_REQUIRE(parent != NULL);
    dev = XAlignMem_create("abc", 3, false);
    XQ_REQUIRE(dev != NULL);

    s_memDeleted = 0;
    XObject_setParent((XObject*)dev, parent);
    XQ_CHECK(XObject_parent((XObject*)dev) == parent);
    XClassDelete((XClass*)parent);      /* 子对象随父析构（不手动删 dev） */
    XQ_EQ_I64(s_memDeleted, 1);
}

/** @brief 基类 waitFor* 立即返回 false（Qt 基类什么都不做）。 */
XQ_TEST(XIODevice, waitForBaseReturnsFalse)
{
    XAlignMemDevice* dev = XAlignMem_create("abc", 3, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadWrite));
    XQ_CHECK(!XIODevice_waitForReadyRead_base((XIODevice*)dev, 0));
    XQ_CHECK(!XIODevice_waitForReadyRead_base((XIODevice*)dev, -1));
    XQ_CHECK(!XIODevice_waitForBytesWritten_base((XIODevice*)dev, 0));
    XClassDelete((XClass*)dev);
}

/** @brief canReadLine：缓冲里有 '\n' 才为真（peek/readLine 预读后立即生效）。 */
XQ_TEST(XIODevice, canReadLine)
{
    char b[8];
    XAlignMemDevice* dev = XAlignMem_create("ab\ncd", 5, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_CHECK(!XIODevice_canReadLine_base((XIODevice*)dev));   /* 缓冲还没预读 */
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)dev, b, 5), 5);     /* 预读整段 */
    XQ_CHECK(XIODevice_canReadLine_base((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 3), 3);     /* 吃掉换行 */
    XQ_CHECK(!XIODevice_canReadLine_base((XIODevice*)dev));

    XClassDelete((XClass*)dev);
}

/** @brief Text 模式读路径剥 '\r'（Qt 在读路径做，两平台一致）。 */
XQ_TEST(XIODevice, textModeStripsCarriageReturn)
{
    char b[8];
    XAlignMemDevice* dev = XAlignMem_create("a\r\nb", 4, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly | XIODevice_Text));
    XQ_CHECK(XIODevice_isTextModeEnabled((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, b, 8), 2);
    XQ_EQ_I64(b[0], 'a');
    XQ_EQ_I64(b[1], '\n');
    XQ_EQ_I64(b[2], 0);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 1), 1);
    XQ_EQ_I64(b[0], 'b');

    /* 关掉 Text 位后 '\r' 照常返回 */
    XIODevice_setTextModeEnabled((XIODevice*)dev, false);
    XQ_CHECK(!XIODevice_isTextModeEnabled((XIODevice*)dev));
    XQ_CHECK((XIODevice_openMode((XIODevice*)dev) & XIODevice_Text) == 0);

    XClassDelete((XClass*)dev);
}

/* ==================== 位宽 / Text 模式 / 通道信号 / 引擎缓冲（gaps #27~#34） ==================== */

/** @brief openMode 是 16 位：框架扩展位 Create(0x0100) 不得被截断（gaps #29）。 */
XQ_TEST(XIODevice, openModeKeepsExtensionBits)
{
    XAlignMemDevice* dev = XAlignMem_create("abc", 3, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadWrite | XIODevice_Create));
    XQ_EQ_I64(XIODevice_openMode((XIODevice*)dev) & XIODevice_Create, XIODevice_Create);
    XQ_EQ_I64(XIODevice_openMode((XIODevice*)dev), XIODevice_ReadWrite | XIODevice_Create);
    XQ_CHECK(XIODevice_isOpen((XIODevice*)dev));
    XClassDelete((XClass*)dev);
}

/** @brief 未打开时 setTextModeEnabled 只告警、不改状态（qiodevice.cpp:529-540）。 */
XQ_TEST(XIODevice, setTextModeRequiresOpen)
{
    XAlignMemDevice* dev = XAlignMem_create("abc", 3, false);
    XQ_REQUIRE(dev != NULL);
    /* 关闭态：本机 Qt 6.8 实测 setTextModeEnabled(true) 后 isTextModeEnabled()
     * 仍为 false（Qt 打印 "The device is not open"）。 */
    XIODevice_setTextModeEnabled((XIODevice*)dev, true);
    XQ_CHECK(!XIODevice_isTextModeEnabled((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_openMode((XIODevice*)dev) & XIODevice_Text, 0);

    /* 打开（不带 Text）⇒ 仍是 false */
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_CHECK(!XIODevice_isTextModeEnabled((XIODevice*)dev));

    /* 打开态设置生效，并同步到 openMode 的 Text 位 */
    XIODevice_setTextModeEnabled((XIODevice*)dev, true);
    XQ_CHECK(XIODevice_isTextModeEnabled((XIODevice*)dev));
    XQ_CHECK((XIODevice_openMode((XIODevice*)dev) & XIODevice_Text) != 0);
    XIODevice_setTextModeEnabled((XIODevice*)dev, false);
    XQ_CHECK(!XIODevice_isTextModeEnabled((XIODevice*)dev));

    /* close 清掉 Text 位；重开不继承 */
    XIODevice_setTextModeEnabled((XIODevice*)dev, true);
    XIODevice_close_base((XIODevice*)dev);
    XQ_CHECK(!XIODevice_isTextModeEnabled((XIODevice*)dev));
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_CHECK(!XIODevice_isTextModeEnabled((XIODevice*)dev));

    XClassDelete((XClass*)dev);
}

/** @brief Text 模式写侧：Windows 展开 '\n'→"\r\n"，POSIX 原样（qiodevice.cpp:1652-1699）。 */
XQ_TEST(XIODevice, textModeWriteExpansion)
{
    XAlignMemDevice* dev = XAlignMem_create(NULL, 0, false);
    const char* base;
    size_t n;
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_WriteOnly | XIODevice_Text));
    /* 返回值是**源字节数**（'\n' 记 1，不按展开后的 2 计），Qt 同款。 */
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)dev, "a\nb", 3), 3);
    n = XByteArray_size_base(dev->m_buf);
    base = (const char*)XContainerDataAddr(dev->m_buf);
#if defined(_WIN32)
    XQ_EQ_SIZE(n, 4);
    XQ_CHECK(memcmp(base, "a\r\nb", 4) == 0);
#else
    XQ_EQ_SIZE(n, 3);
    XQ_CHECK(memcmp(base, "a\nb", 3) == 0);
#endif
    XClassDelete((XClass*)dev);
}

/** @brief readChannelFinished：读通道随 close 结束（gaps #32 的基类发射点）。 */
XQ_TEST(XIODevice, readChannelFinishedOnClose)
{
    XObject* receiver;
    XAlignMemDevice* dev = XAlignMem_create("abc", 3, false);
    XQ_REQUIRE(dev != NULL);
    receiver = XObject_create();
    XQ_REQUIRE(receiver != NULL);

    s_readChannelFinished = 0;
    XQ_CHECK_PTR(XObject_connect((XObject*)dev, XSignal(XIODevice_readChannelFinished_signal),
                                   receiver, xio_onReadChannelFinished, XConnectionType_Direct));
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)dev), 1);
    XIODevice_close_base((XIODevice*)dev);
    XQ_EQ_I64(s_readChannelFinished, 1);
    XIODevice_close_base((XIODevice*)dev);          /* 已关闭：不重复发 */
    XQ_EQ_I64(s_readChannelFinished, 1);

    /* 只写设备打开时没有读通道 ⇒ close 不发 */
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_WriteOnly));
    XIODevice_close_base((XIODevice*)dev);
    XQ_EQ_I64(s_readChannelFinished, 1);

    XClassDelete((XClass*)receiver);
    XClassDelete((XClass*)dev);
}

/** @brief 非虚 XIODevice_bytesAvailable 与虚函数默认实现同值（子类叠加的基石）。 */
XQ_TEST(XIODevice, bytesAvailableHelperMatchesBase)
{
    char b[8];
    XAlignMemDevice* dev;
    XAlignMemDevice* seq;

    /* 随机访问：max(size()-pos, 0) */
    dev = XAlignMem_create("abcdef", 6, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_bytesAvailable((XIODevice*)dev), 6);
    XQ_EQ_I64(XIODevice_bytesAvailable((XIODevice*)dev),
              XIODevice_bytesAvailable_base((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 2);
    XQ_EQ_I64(XIODevice_bytesAvailable((XIODevice*)dev), 4);
    XQ_EQ_I64(XIODevice_bytesAvailable((XIODevice*)dev),
              XIODevice_bytesAvailable_base((XIODevice*)dev));
    XClassDelete((XClass*)dev);

    /* 顺序设备：读环里的字节（peek 预读后）
     * Qt 6.8.0 实测（定制顺序 QIODevice，6 字节 "abcdef"）：peek(4) 后
     * bytesAvailable()==**6**——QIODevicePrivate::read 的 buffered 分支
     * 按 buffer.chunkSize() 把整段读进内部缓冲，peek 只回放前 4 字节不
     * 消费（探针输出 peek=4 avail_after_peek=6 read=4 avail_after_read=2）。
     * 修复前本断言写 4，与 Qt 实测相反。 */
    seq = XAlignMem_create("abcdef", 6, true);
    XQ_REQUIRE(seq != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)seq, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_bytesAvailable((XIODevice*)seq), 0);
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)seq, b, 4), 4);
    XQ_EQ_I64(XIODevice_bytesAvailable((XIODevice*)seq), 6);
    XQ_EQ_I64(XIODevice_bytesAvailable((XIODevice*)seq),
              XIODevice_bytesAvailable_base((XIODevice*)seq));
    /* read(4) 从环里消费前 4 字节 ⇒ 剩 2（Qt 探针同口径）。 */
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)seq, b, 4), 4);
    XQ_CHECK(memcmp(b, "abcd", 4) == 0);
    XQ_EQ_I64(XIODevice_bytesAvailable((XIODevice*)seq), 2);
    XClassDelete((XClass*)seq);
}

/** @brief 套接字式两层缓冲：引擎待读字节计入 bytesAvailable ⇒ 不再假 EOF（gaps #27/#28）。 */
XQ_TEST(XIODevice, socketBytesAvailableIncludesEngine)
{
    char b[8];
    XAlignFakeSocket* s = XAlignFakeSocket_create("hello", 5);
    XQ_REQUIRE(s != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)s, XIODevice_ReadOnly));

    /* 引擎有 5 字节、读环空：bytesAvailable 必须看见它们（Qt:
     * QIODevice::bytesAvailable() + socketEngine->bytesAvailable()，
     * qabstractsocket.cpp:1755-1767），atEnd 因而是 false。 */
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)s), 5);
    XQ_CHECK(!XIODevice_atEnd_base((XIODevice*)s));

    XQ_EQ_I64(XIODevice_read_1((XIODevice*)s, b, 2), 2);
    XQ_CHECK(memcmp(b, "he", 2) == 0);
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)s), 3);
    XQ_CHECK(!XIODevice_atEnd_base((XIODevice*)s));

    XQ_EQ_I64(XIODevice_read_1((XIODevice*)s, b, 3), 3);
    XQ_CHECK(memcmp(b, "llo", 3) == 0);
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)s), 0);
    XQ_CHECK(XIODevice_atEnd_base((XIODevice*)s));

    XClassDelete((XClass*)s);
}

#if XIO_ALIGN_HAS_SOCKET
/** @brief 真实 XAbstractSocket 的虚表结构：重载 BytesAvailable、不重载 AtEnd。 */
XQ_TEST(XIODevice, socketVtableStructure)
{
    XIODevice* plain = XIODevice_create();
    XTcpSocket* sock;
    void* plainBytesAvailable;
    void* sockBytesAvailable;
    void* plainAtEnd;
    void* sockAtEnd;
    XQ_REQUIRE(plain != NULL);
    sock = XTcpSocket_create();
    if (!sock) {
        /* 先释放已建对象再报失败：否则泄漏门会把「构造失败」记成用例泄漏。 */
        XClassDelete((XClass*)plain);
        xq_fail_simple("XTcpSocket_create() != NULL", __FILE__, __LINE__);
        return;
    }

    plainBytesAvailable = XClassGetVirtualFunc(plain, EXIODevice_BytesAvailable, void*);
    sockBytesAvailable = XClassGetVirtualFunc((XIODevice*)sock, EXIODevice_BytesAvailable, void*);
    plainAtEnd = XClassGetVirtualFunc(plain, EXIODevice_AtEnd, void*);
    sockAtEnd = XClassGetVirtualFunc((XIODevice*)sock, EXIODevice_AtEnd, void*);

    /* gaps #28：套接字必须重载 BytesAvailable（叠加引擎待读字节）。 */
    XQ_CHECK(sockBytesAvailable != plainBytesAvailable);
    /* gaps #27：不重载 AtEnd——Qt 的 QAbstractSocket 没有 atEnd 重载，基类
     * atEnd 的判据里调虚函数 bytesAvailable() 因而能看到引擎字节。 */
    XQ_CHECK(sockAtEnd == plainAtEnd);

    /* 未连接 = 未打开 ⇒ atEnd true（Qt: openMode==NotOpen），可读 0。 */
    XQ_CHECK(XIODevice_atEnd_base((XIODevice*)sock));
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)sock), 0);

    XClassDelete((XClass*)sock);
    XClassDelete((XClass*)plain);
}
#endif /* XIO_ALIGN_HAS_SOCKET */

/* ==================== 直读型设备（readLineData 重载对位） ==================== */

/**
 * @brief 顺序设备，重载 ReadLineData 且**绕过基类 read()** 直读设备流。
 * @details 对位 XSerialPort / XFileDevice 的真实形态：readLineData 自己扫设备，
 *          数据不会自动进 XIODevice 的内部读环——这正是 P0-09 要强制基类实现
 *          的原因（事务里读到的行不进缓冲，rollback 无法重放）。
 */
typedef struct XAlignDirectLineDevice
{
    XIODevice m_base;        /**< 基类，必须是第一个成员。 */
    const char* m_payload;   /**< 支撑数据（借用，静态字符串）。 */
    int64_t m_len;           /**< 支撑数据长度。 */
    int64_t m_cursor;        /**< 设备游标（子类自维护）。 */
    int m_readLineDataCalls; /**< 重载 readLineData 被调用次数（强制基类的旁证）。 */
} XAlignDirectLineDevice;

static void XAlignDirect_deinit(XAlignDirectLineDevice* dev);
static bool XAlignDirect_isSequential(const XIODevice* io);
static int64_t XAlignDirect_readData(XIODevice* io, char* data, int64_t maxlen);
static int64_t XAlignDirect_readLineData(XIODevice* io, char* data, int64_t maxlen);

XCLASS_DEFINE_BEGING(XAlignDirectLineDevice)
XCLASS_DEFINE_EXTEND_END(XAlignDirectLineDevice, XIODevice)

static XVtable* XAlignDirectLineDevice_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAlignDirectLineDevice)
    XVTABLE_INHERIT_XCLASS(XIODevice);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XAlignDirect_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, XAlignDirect_isSequential);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, XAlignDirect_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadLineData, XAlignDirect_readLineData);
    return XVTABLE_DEFAULT;
}

static XAlignDirectLineDevice* XAlignDirect_create(const char* payload)
{
    XAlignDirectLineDevice* dev = (XAlignDirectLineDevice*)XMemory_malloc(
        sizeof(XAlignDirectLineDevice), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!dev) return NULL;
    XIODevice_init(&dev->m_base);
    XClassSetVtable(dev, XAlignDirectLineDevice);
    Set_Class_Memory(dev, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(dev, true);
    dev->m_payload = payload;
    dev->m_len = payload ? (int64_t)strlen(payload) : 0;
    dev->m_cursor = 0;
    dev->m_readLineDataCalls = 0;
    return dev;
}

static void XAlignDirect_deinit(XAlignDirectLineDevice* dev)
{
    if (!dev) return;
    XClass_Deinit_Parent(XIODevice, dev);
}

static bool XAlignDirect_isSequential(const XIODevice* io)
{
    (void)io;
    return true;
}

static int64_t XAlignDirect_readData(XIODevice* io, char* data, int64_t maxlen)
{
    XAlignDirectLineDevice* dev = (XAlignDirectLineDevice*)io;
    int64_t n;
    if (!dev || !data || maxlen <= 0) return 0;
    n = dev->m_len - dev->m_cursor;
    if (n <= 0) return 0;
    if (n > maxlen) n = maxlen;
    memcpy(data, dev->m_payload + dev->m_cursor, (size_t)n);
    dev->m_cursor += n;
    return n;
}

/** @brief 重载的 readLineData：不过 XIODevice_read_1，直接扫设备流。 */
static int64_t XAlignDirect_readLineData(XIODevice* io, char* data, int64_t maxlen)
{
    XAlignDirectLineDevice* dev = (XAlignDirectLineDevice*)io;
    int64_t readSoFar = 0;
    if (!dev || !data || maxlen <= 0) return 0;
    ++dev->m_readLineDataCalls;
    while (readSoFar < maxlen && dev->m_cursor < dev->m_len) {
        char c = dev->m_payload[dev->m_cursor++];
        data[readSoFar++] = c;
        if (c == '\n')
            break;
    }
    return readSoFar;
}

/* ==================== 用例（P0-07/08/09/55/56/57/E14） ==================== */

/**
 * @brief P0-08：带缓冲设备的小读必须先回填内部缓冲再回放。
 * @details Qt 6.8.0 qiodevice.cpp:1074-1108 —— 只有「无缓冲」或「请求不小于一个
 *          chunk」才直读调用方缓冲；否则把数据预读进 QIODevice 的内部缓冲。
 *          修复前顺序缓冲设备 `read(n)`（n < chunkSize）后
 *          `bytesAvailable()/canReadLine()` 看不到预读内容，且逐字节读
 *          （getChar）每字节一次 readData。
 */
XQ_TEST(XIODevice, smallReadBackfillsInternalBuffer)
{
    char b[16];
    char line[16];
    XAlignMemDevice* dev = XAlignMem_create("ab\ncdef", 7, true);
    XAlignMemDevice* byteWise;
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));

    /* 小读（2 < chunkSize）：走回填分支，预读内容对查询可见。 */
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 2);
    XQ_CHECK(memcmp(b, "ab", 2) == 0);
    /* 设备共 7 字节、已交付 2 ⇒ 内部环里还压着 5 字节 "\ncdef"。 */
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)dev), 5);
    XQ_CHECK(XIODevice_canReadLine_base((XIODevice*)dev));   /* '\n' 在环里 */
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, line, (int64_t)sizeof(line)), 1);
    XQ_CHECK(memcmp(line, "\n", 1) == 0);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 4), 4);
    XQ_CHECK(memcmp(b, "cdef", 4) == 0);
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)dev), 0);
    XQ_CHECK(XIODevice_atEnd_base((XIODevice*)dev));
    XClassDelete((XClass*)dev);

    /* 逐字节读（getChar → read_1(…,1)）：整段设备数据只允许一次 readData
     * 回填 + 一次「EOF 探针」（环空后的第 7 次 getChar 会按 chunk 再问一次
     * 设备，得到 0 才判 EOF——Qt 同款口径）。修复前是 7 次（每字节一次）。 */
    byteWise = XAlignMem_create("abcdef", 6, true);
    XQ_REQUIRE(byteWise != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)byteWise, XIODevice_ReadOnly));
    while (XIODevice_getChar((XIODevice*)byteWise, b))
        ;
    XQ_EQ_I64(byteWise->m_readDataCalls, 2);
    XClassDelete((XClass*)byteWise);
}

/**
 * @brief P0-09：事务中的 readLine 强制基类实现（数据进内部缓冲，回滚可重放）。
 * @details Qt 原文 qiodevice.cpp:1391-1395 —— "Force base implementation for
 *          transaction on sequential device as it stores the data in internal
 *          buffer automatically"。修复前 X 无论是否事务都走虚派发：直读型子类
 *          （XSerialPort / XFileDevice）的行不进内部缓冲，rollback 后同一段
 *          数据读出来的是**下一行**而不是原行。
 */
XQ_TEST(XIODevice, transactionForcesBaseReadLineData)
{
    char line[16];
    XAlignDirectLineDevice* dev = XAlignDirect_create("aa\nbb\n");
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));

    XIODevice_startTransaction((XIODevice*)dev);
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, line, (int64_t)sizeof(line)), 3);
    XQ_CHECK(memcmp(line, "aa\n", 3) == 0);
    XQ_EQ_I64(dev->m_readLineDataCalls, 0);    /* 重载被强制绕开 */

    XIODevice_rollbackTransaction((XIODevice*)dev);
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, line, (int64_t)sizeof(line)), 3);
    XQ_CHECK(memcmp(line, "aa\n", 3) == 0);    /* 回滚后重放同一行 */
    XQ_EQ_I64(dev->m_readLineDataCalls, 0);

    /* 数据确实进了内部缓冲：重放后剩余行照常可读（此时已无事务）。 */
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, line, (int64_t)sizeof(line)), 3);
    XQ_CHECK(memcmp(line, "bb\n", 3) == 0);
    XQ_EQ_I64(dev->m_readLineDataCalls, 0);

    /* 对照：EOF 且无事务 ⇒ 走子类重载（Qt 只有事务才强制基类）。 */
    XQ_EQ_I64(XIODevice_readLine_1((XIODevice*)dev, line, (int64_t)sizeof(line)), 0);
    XQ_EQ_I64(dev->m_readLineDataCalls, 1);
    XClassDelete((XClass*)dev);
}

/**
 * @brief P0-57：const 查询（bytesAvailable/atEnd/bytesToWrite）不得分配缓冲。
 * @details Qt 只读不分配；修复前 `bufferedBytes`/`atEnd`/`bytesToWrite` 走
 *          getOrCreate 路径，一次查询就给空设备分配 4KB chunk（嵌入式
 *          2.6KB 堆下 malloc 直接失败）。
 */
XQ_TEST(XIODevice, constQueriesDoNotAllocateBuffers)
{
    XAlignMemDevice* dev = XAlignMem_create("abcdef", 6, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_ReadOnly));
    XQ_REQUIRE(dev->m_base.m_d != NULL);

    XQ_CHECK(XIODevicePrivate_readBufferConst(dev->m_base.m_d, 0) == NULL);
    XQ_CHECK(XIODevicePrivate_writeBufferConst(dev->m_base.m_d, 0) == NULL);

    (void)XIODevice_bytesAvailable_base((XIODevice*)dev);
    (void)XIODevice_atEnd_base((XIODevice*)dev);
    (void)XIODevice_bytesToWrite_base((XIODevice*)dev);
    (void)XIODevice_canReadLine_base((XIODevice*)dev);

    XQ_CHECK(XIODevicePrivate_readBufferConst(dev->m_base.m_d, 0) == NULL);
    XQ_CHECK(XIODevicePrivate_writeBufferConst(dev->m_base.m_d, 0) == NULL);
    /* 真正读数据时才允许建环（getOrCreate 路径保留给读写路径）。 */
    {
        char b[4];
        XQ_EQ_I64(XIODevice_read_1((XIODevice*)dev, b, 2), 2);
        XQ_CHECK(XIODevicePrivate_readBufferConst(dev->m_base.m_d, 0) != NULL);
    }
    XClassDelete((XClass*)dev);
}

/**
 * @brief P0-55/56：空数组写返回 0；read(maxSize<=0) 返回空数组对象。
 * @details Qt：`write(QByteArray())` = `write(constData(), 0)` ⇒ 走
 *          CHECK_WRITABLE 后返回 0（未打开仍 -1）；`read(qint64)` 的
 *          CHECK_READABLE / CHECK_MAXLEN 失败都返回**空 QByteArray 对象**。
 *          修复前：空 XByteArray 的 data 指针为 NULL ⇒ write 返回 -1；
 *          read(maxlen<=0) 返回 NULL（调用方按 Qt 习惯直接迭代会空指针）。
 */
XQ_TEST(XIODevice, emptyByteArrayContracts)
{
    XByteArray* empty = XByteArray_create();
    XByteArray* got;
    XAlignMemDevice* writer = XAlignMem_create(NULL, 0, false);
    XAlignMemDevice* reader = XAlignMem_create("abc", 3, false);
    XAlignMemDevice* closed;
    XQ_REQUIRE(empty != NULL && writer != NULL && reader != NULL);

    XQ_EQ_SIZE(XByteArray_size_base(empty), 0);

    /* 已打开的可写设备：空数组写 = 合法空写（CHECK_WRITABLE 通过后按
     * maxSize==0 返回 0，Qt 同）。 */
    XQ_CHECK(XIODevice_open_base((XIODevice*)writer, XIODevice_WriteOnly));
    XQ_EQ_I64(XIODevice_write_2((XIODevice*)writer, empty), 0);
    XClassDelete((XClass*)writer);

    /* 只读设备：CHECK_WRITABLE 先于长度判断 ⇒ 空数组写也 -1（Qt 同）。 */
    XQ_CHECK(XIODevice_open_base((XIODevice*)reader, XIODevice_ReadOnly));
    XQ_EQ_I64(XIODevice_write_2((XIODevice*)reader, empty), -1);

    /* 未打开设备：同样 -1。 */
    closed = XAlignMem_create(NULL, 0, false);
    XQ_REQUIRE(closed != NULL);
    XQ_EQ_I64(XIODevice_write_2((XIODevice*)closed, empty), -1);
    XClassDelete((XClass*)closed);

    /* read(maxSize <= 0)：返回空数组对象，不返回 NULL。 */
    got = XIODevice_read_3((XIODevice*)reader, 0);
    XQ_CHECK(got != NULL);
    if (got) XQ_EQ_SIZE(XByteArray_size_base(got), 0);
    XClassDelete((XClass*)got);
    got = XIODevice_read_3((XIODevice*)reader, -1);
    XQ_CHECK(got != NULL);
    if (got) XQ_EQ_SIZE(XByteArray_size_base(got), 0);
    XClassDelete((XClass*)got);

    XClassDelete((XClass*)empty);
    XClassDelete((XClass*)reader);
}

/**
 * @brief P0-E14：`open(NotOpen)` 返回 true 且状态保持 NotOpen（Qt 同口径）。
 * @details Qt 的 `QIODevice::open(OpenMode)` 没有 NotOpen 早退：它把
 *          `openMode = NotOpen` 照写并返回 true，`isOpen()` 随之为 false。
 *          修复前 X 返回 false，调用方会误判为「打开失败」。
 */
XQ_TEST(XIODevice, openNotOpenAccepted)
{
    XAlignMemDevice* dev = XAlignMem_create("abc", 3, false);
    XQ_REQUIRE(dev != NULL);
    XQ_CHECK(XIODevice_open_base((XIODevice*)dev, XIODevice_NotOpen));
    XQ_CHECK(!XIODevice_isOpen((XIODevice*)dev));
    XQ_EQ_I64(XIODevice_openMode((XIODevice*)dev), XIODevice_NotOpen);
    XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)dev), 0);
    XQ_EQ_I64(XIODevice_writeChannelCount((XIODevice*)dev), 0);
    /* 状态不得被部分改写（回退早退保留：NotOpen 时不碰 pos/通道数/缓冲）。 */
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)dev), 0);
    XClassDelete((XClass*)dev);
}
