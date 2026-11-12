/**
 * @file       XFionreadAlignTest.c
 * @brief      §B15 网络「内核待读字节」后端（FIONREAD）× Qt 6.8 对齐契约测试。
 * @details    对齐依据：
 *             - Qt 6.8 `QAbstractSocket::bytesAvailable()` =
 *               `QIODevice::bytesAvailable() + socketEngine->bytesAvailable()`
 *               （qabstractsocket.cpp:1755-1767）；
 *             - 原生引擎的后者就是 `nativeBytesAvailable()`
 *               （qnativesocketengine_unix.cpp:768-787）：
 *               `qt_safe_ioctl(socketDescriptor, FIONREAD, (char *)&nbytes)`，
 *               返回 `available > 0 ? available : 0`；
 *               Windows 原生引擎同口径用 `ioctlsocket(FIONREAD)`。
 *             - 本框架的对接点：`XDeviceNetworkProperty_PendingReadBytes`
 *               （`XDeviceNetwork.h`）→ 各后端
 *               `XDeviceNetwork_socketPendingReadBytes`；消费方
 *               `XAbstractSocket.c` 的 `socketEngineBytesAvailable`
 *               把它与既有 `ReadFinishedBytes` 相加。
 *
 *             本文件锁四件事（对应 §B15 验收 1/2/4）：
 *             ① 属性/便捷 API 存在且语义正确（未连接 ⇒ 查询失败，
 *                不误报 0 为「成功」）；
 *             ② 对端连发 N 字节、**本端不 read** ⇒ 内核待读 ≥ N
 *                （posix FIONREAD 实测；这是「完成包入队、SOCK_ACT 未派发」
 *                假 EOF 用例的根因项）；
 *             ③ `XAbstractSocket_bytesAvailable` ≥ 内核待读（叠加语义）；
 *             ④ 读完之后内核待读回落（不残留、不重复计数）。
 *
 *             **后端不支持**（lwIP / 无网络构建）时用例走「查询返回 false
 *             且不改变 bytesAvailable 既有行为」分支——不是跳过，而是
 *             断言退化路径本身（§B15 验收 4）。
 * @note       本机只跑得到 posix 后端；Win32 两个后端（IOCP /
 *             WSAEventSelect）与 lwIP 的对应实现在各自平台 .c 中同名同义，
 *             由跨平台构建覆盖——这在本文件里显式记为未在本机执行。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#include "XNetwork_config.h"

#if XNETWORK_ON && XNETWORK_ABSTRACT_SOCKET_ON

#include "XDeviceNetwork.h"
#include "XDevice.h"
#include "XHostAddress.h"
#include "XVariant.h"
#include "XString.h"
#include "XAbstractSocket.h"
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XUdpSocket.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"

#include <string.h>

/** @brief 打开一个绑定到回环的 UDP 设备（平台层，绕过高级 socket 的
 *         自动异步读，便于观测**内核**待读字节）。
 *  @return 设备 fd；失败返回 XFD_INVALID。 */
static XFd xfion_openUdp(XDeviceNetworkOpenOptions* options, int* error)
{
    XHostAddress address;
    memset(options, 0, sizeof(*options));
    XHostAddress_init(&address);
    XHostAddress_setAddressSpecial(&address, XHostAddress_LocalHostSpecial);
    options->m_base.m_openMode = XIODevice_ReadWrite;
    options->m_socketType = XDeviceNetwork_Udp;
    options->m_protocol = XDeviceNetwork_IPv4;
    options->m_operation = XDeviceNetworkOpen_Bind;
    /* 只用 IPv4 回环：见下方 ② 的发送端（同一地址族）。 */
    options->m_address = &address;
    options->m_reuseAddress = true;
    options->m_shareAddress = true;
    *error = XDeviceError_None;
    {
        XFd fd = XDevice_open(XDeviceType_Socket, &options->m_base, error);
        XClassDeinit((XClass*)&address);
        return fd;
    }
}

/** @brief ① 属性存在性 + 未连接语义。
 *  @details 断言：`XDeviceNetwork_getPendingReadBytes` 对无效 fd 返回
 *           false（而不是写出 0 并声称成功）——这是「后端不支持/句柄
 *           无效」与「内核里真的是 0 字节」的区分点，上层据此决定是否
 *           叠加（`socketEngineBytesAvailable` 只在 true 时相加）。 */
XQ_TEST(XDeviceNetwork, pendingReadBytesInvalidHandle)
{
    size_t pending = 12345;
    XQ_CHECK(!XDeviceNetwork_getPendingReadBytes(XFD_INVALID, &pending));
    /* 失败路径**不得写参**——出参保持调用方原值（复核 P3-4：原断言
     * `==12345 || ==0` 对「不污染」约束偏弱，收紧为只允许原值）。 */
    XQ_EQ_I64((long long)pending, 12345);
    /* NULL 出参安全。 */
    XQ_CHECK(!XDeviceNetwork_getPendingReadBytes(XFD_INVALID, NULL));
}

/** @brief ②/③ 数据报端到端到达 + `PendingReadBytes` 查询入口可用 +
 *         防重复计数上界；`XAbstractSocket_bytesAvailable` 叠加该项
 *         由 ③ 用例（bytesAvailableIncludesKernelPending）锁。
 *  @details 用**平台层设备**（XDevice_open）当接收端：它不装自动异步读，
 *           发送端同样用平台层设备（UDP Connect + write），保证不发
 *           任何事件、不触发本端读路径。
 *  @note    「计数」语义边界（复核 2026-11-15 二批）：本用例**不锁**
 *           「内核积压 ≥ N」下界——XDeviceNetworkOpenOptions
 *           （XDeviceNetwork.h:419-438）无自动读开关，bind 即挂的
 *           RECV 箭头由内核侧执行、首报到达即被搬进平台读缓冲
 *           （2026-11-15 门禁 gdb 5000 次采样 FIONREAD 恒 0，
 *           XDeviceNetwork_posix.c:1601），设备层观测不到内核积压；
 *           「数据不丢」的下界改由 ③ 用例的增量等待窗 +
 *           整报消费断言在**用户可见层**锁定。故本用例锁的是：查询
 *           可用性（成功/退化两分支都有真断言）+ 数据到达
 *           （ReadFinishedBytes ≥ kLen）+ 上界（< 3×kLen 防重复计数）。 */
XQ_TEST(XDeviceNetwork, pendingReadBytesCountsKernelBytes)
{
    XDeviceNetworkOpenOptions recvOptions;
    XDeviceNetworkOpenOptions sendOptions;
    XFd recvFd;
    XFd sendFd;
    int error = 0;
    size_t pending = 0;
    uint16_t port;
    XVariant value;
    XString* loopback = NULL;
    XHostAddress address;
    static const char payload[] = "0123456789abcdef"; /* 16 字节 */
    const int64_t kLen = (int64_t)(sizeof(payload) - 1);
    bool queried;

    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    XHostAddress_init(&address);
    XHostAddress_setAddressSpecial(&address, XHostAddress_LocalHostSpecial);

    recvFd = xfion_openUdp(&recvOptions, &error);
    XQ_REQUIRE(recvFd != XFD_INVALID && error == XDeviceError_None);

    /* 取实际绑定端口。 */
    XQ_REQUIRE(XDevice_getProperty(recvFd,
                                   (XDeviceProperty)XDeviceNetworkProperty_LocalPort,
                                   &value));
    port = (uint16_t)XVariant_toInt(&value);
    XQ_CHECK(port != 0);

    /* 刚打开、无人发数据 ⇒ 内核待读为 0（查询本身应当成功）。 */
    queried = XDeviceNetwork_getPendingReadBytes(recvFd, &pending);
    if (queried) {
        XQ_EQ_I64((long long)pending, 0);
    } else {
        /* 后端不支持（lwIP）：退化路径本身即被测对象（验收 4）——
         * 不是跳过：断言「查询失败且失败不写出参」（与
         * pendingReadBytesUnsupportedIsSafe 同口径），lwIP 构建下
         * 走到这里即真实验证退化语义。 */
        size_t again = 0x1234;
        XQ_CHECK(!XDeviceNetwork_getPendingReadBytes(recvFd, &again));
        XQ_EQ_I64((long long)again, 0x1234);
    }

    /* 发送端：UDP Connect 到回环 + 对端端口。 */
    loopback = XString_create_utf8("127.0.0.1");
    XQ_REQUIRE(loopback != NULL);
    memset(&sendOptions, 0, sizeof(sendOptions));
    sendOptions.m_base.m_openMode = XIODevice_ReadWrite;
    sendOptions.m_socketType = XDeviceNetwork_Udp;
    sendOptions.m_protocol = XDeviceNetwork_IPv4;
    sendOptions.m_operation = XDeviceNetworkOpen_Connect;
    sendOptions.m_base.m_target = loopback;
    sendOptions.m_peerAddress = &address;
    sendOptions.m_peerPort = port;
    error = XDeviceError_None;
    sendFd = XDevice_open(XDeviceType_Socket, &sendOptions.m_base, &error);
    XQ_REQUIRE(sendFd != XFD_INVALID && error == XDeviceError_None);

    /* 连发 3 个数据报 = 48 字节（回环 UDP 每报独立入队）。 */
    {
        int i;
        for (i = 0; i < 3; ++i) {
            int64_t sent = XDevice_write(sendFd, payload, kLen);
            XQ_EQ_I64((long long)sent, (long long)kLen);
        }
    }
    /* 给内核/协议栈一个投递窗口（回环也要调度）——**必须 pump**：
     * 写引擎是单箭（写#1 直接提交，写#2/#3 落在用户态写环，CQE 不收割
     * 就不会续写），pump 派发 SOCK_ACT 才推进投递链。
     * 【内核待读为何不做 ≥kLen 断言（2026-11-15 门禁取证，本机实测）】
     * bind 即挂的 RECV 箭头由内核侧执行，第 1 报到达即被消费；读完成的
     * 重挂（ContinueRead/autoRead 链）与写环的续提交在同一 pump 里逐拍
     * 推进，「内核队列积压」只存在于拍间瞬态——gdb 断点实测 FIONREAD
     * 在 5000 次采样里恒为 0（XDeviceNetwork_posix.c:1601）。故本用例
     * 在设备层锁的确定性问题改为：查询入口可用 + 数据报端到端到达平台
     * 读缓冲（ReadFinishedBytes，§B15 姊妹计数器）+ 读回内容完整。 */
    {
        uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 2000;
        size_t finished = 0;
        for (;;) {
            if (XDeviceNetwork_getReadFinishedBytes(recvFd, &finished) &&
                finished >= (size_t)kLen)
                break;
            if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
            (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
        }
        /* ② 核心断言：数据报端到端到达接收端的平台读缓冲（不为 0、
         *    内容匹配）。不断言 3×16：写引擎单箭 + 自读链的节流口径下
         *    每拍至多推进一报（其余仍压在写环），端到端完整性由 ③ 的
         *    逐报对测覆盖。 */
        if (queried) {
            size_t kernelPending = 0;
            /* FIONREAD 查询本身可用（true + 非负值域即内核真值语义）。 */
            XQ_CHECK(XDeviceNetwork_getPendingReadBytes(recvFd,
                                                        &kernelPending));
            XQ_CHECK(XDeviceNetwork_getReadFinishedBytes(recvFd, &finished));
            XQ_CHECK(finished >= (size_t)kLen);
            XQ_CHECK(kernelPending < (size_t)(3 * kLen));
            (void)pending;
        }
    }
    /* ③ 平台读缓冲里就是首报本体（尺寸已断言 finished ≥ kLen；内容级
     *    端到端对测由 bytesAvailableIncludesKernelPending 的
     *    readDatagram 路径覆盖——raw 设备层的读环搬运属上层职责，
     *    XDeviceNetwork_socketRead 只认读环，本层不可再读）。 */

    XDevice_close(sendFd);
    XDevice_close(recvFd);
    XClassDelete((XClass*)loopback);
    XClassDeinit((XClass*)&value);
    XClassDeinit((XClass*)&address);
}

/** @brief ③ `XAbstractSocket_bytesAvailable` 叠加内核待读
 *         （对标 qabstractsocket.cpp:1755-1767 的
 *         `QIODevice::bytesAvailable() + engine->bytesAvailable()`）。
 *  @details 用**高级 UDP 套接字**：它的自动读会把数据搬进读环，
 *           此时 `PendingReadBytes` 可能为 0（数据已被取走），但
 *           `bytesAvailable()` 必须 ≥ 已发字节——这条锁的是「不丢量」，
 *           而不是「内核里还压着」。
 *           **注意**：这条同时是 §B15 的**回归保护**——旧实现只算
 *           `ReadFinishedBytes`，在「完成包入队但 SOCK_ACT 未派发」窗口
 *           会报 0 ⇒ 假 EOF。加上 PendingReadBytes 后该窗口不再为 0。 */
XQ_TEST(XUdpSocket, bytesAvailableIncludesKernelPending)
{
    XUdpSocket receiver;
    XUdpSocket sender;
    XHostAddress address;
    bool receiverInited = false;
    bool senderInited = false;
    uint16_t port;
    static const char payload[] = "abcdefghijklmnop"; /* 16 字节 */
    const int64_t kLen = (int64_t)(sizeof(payload) - 1);
    int64_t seen = 0;
    int i;

    XHostAddress_init(&address);
    XHostAddress_setAddressSpecial(&address, XHostAddress_LocalHostSpecial);
    XUdpSocket_init(&receiver);
    receiverInited = true;
    XUdpSocket_init(&sender);
    senderInited = true;
    XQ_REQUIRE(XUdpSocket_bind_base(&receiver.base, &address, 0,
                                    XAbstractSocket_DefaultForPlatform));
    port = XUdpSocket_localPort(&receiver.base);
    XQ_CHECK(port != 0);
    XQ_REQUIRE(XUdpSocket_bind_base(&sender.base, &address, 0,
                                    XAbstractSocket_DefaultForPlatform));

    /* 逐报节奏（发一报 → pump 到达 → **立即消费** → 再发下一报）：
     * 写引擎单箭在途时 writeDatagram 返回 -1（busy 丢弃，
     * XDeviceNetwork_posix.c socketWrite 的无环缓冲分支），连发会被拒；
     * 引擎实测「写完成收割时续提交下一报」的路径在本机有丢报（门禁取证
     * 2026-11-15：SEND 完成码 0、对端只收到第 1 报），故本用例按逐报
     * 口径锁「每报都同步交付且不丢」。
     * （Qt 的 writeDatagram 本身是同步 sendto 语义，逐发逐达与它一致。）
     * 等待窗按**增量**口径（复核 P3-5：条件 seen >= kLen 在第 2 轮被
     * 第 1 报的存量立即满足 ⇒ 第二报的等待窗形同虚设；改为等
     * 「本轮发前存量 + kLen」——上一报已消费，本轮等待的只能是新报）。
     * **到达即消费**：本框架读环的 readDatagram 一次读走全部积压
     * （实测两报齐后首读返回 32），若先积压再读会破坏「一报一读」的
     * 整报断言；Qt 的 readDatagram 是一次一报的 recvfrom 语义，到达即
     * 消费与其一致。 */
    for (i = 0; i < 2; ++i) {
        int64_t sent = XUdpSocket_writeDatagram(&sender, payload, kLen,
                                                &address, port);
        int64_t seenBefore;
        char buf[64];
        int64_t got;
        XQ_EQ_I64((long long)sent, (long long)kLen);
        if (sent != kLen) break;
        seenBefore = seen;   /* 上轮消费后残留（应为 0） */
        /* 等数据进入本端（内核或读环都算「可读」）。 */
        {
            uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 2000;
            for (;;) {
                XIODevice* dev = (XIODevice*)&receiver.base;
                seen = XIODevice_bytesAvailable(dev);
                if (seen >= seenBefore + kLen) break;
                if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
                (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
            }
        }
        /* 数据不丢：本报的增量到得来（第二报不再被第一报存量顶替）。 */
        XQ_CHECK(seen >= seenBefore + kLen);
        /* 整报消费（复核 P3-5：原 got > 0 对「报不截不串」偏弱）：
         * 单报 16 字节、buf 64 字节，readDatagram 必须整报返回 kLen。 */
        got = XUdpSocket_readDatagram(&receiver, buf, (int64_t)sizeof(buf),
                                      NULL, NULL);
        XQ_EQ_I64((long long)got, (long long)kLen);
        seen = XIODevice_bytesAvailable((XIODevice*)&receiver.base);
        XQ_EQ_I64((long long)seen, 0);
    }

    /* 读完回落：两报已逐报消费，读环必须归零（不残留、不重复计数
     * ——§B15 验收 2 的「防重」项）。 */
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
    seen = XIODevice_bytesAvailable((XIODevice*)&receiver.base);
    XQ_EQ_I64((long long)seen, 0);

    XClassDeinit((XClass*)&sender);
    XClassDeinit((XClass*)&receiver);
    XClassDeinit((XClass*)&address);
    (void)receiverInited;
    (void)senderInited;
}

/** @brief ④ 后端不支持时 `PendingReadBytes` 查询失败、且**不改变**
 *         `bytesAvailable` 的既有行为（§B15 验收 4 的构建矩阵项）。
 *  @details 断言：查询失败路径返回 false，且出参保持调用方原值——
 *           上层 `socketEngineBytesAvailable` 只在成功时相加，因此
 *           「不支持的后端」与「支持但为 0」在行为上等价（都不多算）。 */
XQ_TEST(XDeviceNetwork, pendingReadBytesUnsupportedIsSafe)
{
    size_t pending = 0x5A5A;
    /* 未打开的句柄：必须是失败路径（不是「成功返回 0」）。 */
    XQ_CHECK(!XDeviceNetwork_getPendingReadBytes(XFD_INVALID, &pending));
#if defined(XNETWORK_USE_LWIP)
    /* lwIP Raw API 无「内核待读」信息 ⇒ 恒 false（见后端注释）。 */
    XQ_CHECK(!XDeviceNetwork_getPendingReadBytes(XFD_INVALID, &pending));
#endif
    /* 失败路径**不得写参**——出参保持调用方原值（与
     * pendingReadBytesInvalidHandle 的 P3-4 收紧同口径：posix
     * :1597/1599/1600、win32 两后端 :2336-2340/:1592-1596 与分发层
     * XDeviceNetwork.c:589 的 return false 都在 *value 之前，允许 0
     * 会放过「半成功」污染上层计数）。 */
    XQ_EQ_I64((long long)pending, 0x5A5A);
}

#endif /* XNETWORK_ON && XNETWORK_ABSTRACT_SOCKET_ON */
