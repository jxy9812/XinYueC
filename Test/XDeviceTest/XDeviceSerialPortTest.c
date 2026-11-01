#include "XDeviceSerialPortTest.h"
#include "XDevice.h"
#include "XDeviceSerialPort.h"
#include "XFileDescriptor.h"
#include "XSerialPort.h"
#include "XSerialPortInfo.h"
#include "XString.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XThread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

static bool checkDevicePropertyInt(XFd fd, uint32_t property, int expected)
{
    XVariant value;
    bool ok;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    ok = XDevice_getProperty(fd, (XDeviceProperty)property, &value) &&
         XVariant_toInt(&value) == expected;
    XClassDeinit((XClass*)&value);
    return ok;
}

#if defined(_WIN32) && XDEVICE_SERIALPORT_ON

/* 平台布防探针：验证打开的串口走的是 IOCP 常驻读通道（回归前提）。
 * 仅为断言布防成功，不参与数据接收。 */
static void xdeviceSerialProbeReadable(void* ctx)
{
    (void)ctx;
}

/* Windows 自动化分支（2026-11-01「主站点击断开即崩」回归锁）。
 *
 * 崩溃机理：串口以 FILE_FLAG_OVERLAPPED 打开并关联框架 IOCP 后，open 即
 * 布防常驻异步读；完成回调无条件续挂下一次读，而关闭回收循环以
 * 「m_readPending==false」为完成条件 ⇒ 回收空转耗尽后强清标志，带着
 * **仍在途的读** CloseHandle ⇒ OS 取消读并把完成包投进完成端口 ⇒ 上下文
 * 已被释放 ⇒ 下一轮事件环出队该包即在已释放内存上解引用（GUI 表现为
 * Modbus 主站点「断开」下一帧即崩，processOneCompletion 读访问冲突）。
 *
 * 本测试以 GUI 崩溃的最小驱动序列复现：
 *   open（rxArm 即布防常驻读）→ 写一帧 → 读在途时 close
 *   → 事件环再泵若干轮（悬垂完成包在此出队显形）→ 循环重开放大显形。
 * 修复后每轮回收恰好消费一个完成包，循环干净通过。
 *
 * 环境适配：XSERIAL_TEST_PORT 覆盖目标口；否则枚举取第一个非占用口；
 * 无可用串口的环境（CI 裸机）→ SKIP 返回 true，不假红。 */
static bool xdeviceSerialPortWin32CloseRaceTest(void)
{
    const char* overridePort = getenv("XSERIAL_TEST_PORT");
    char portName[32];
    static const char probeFrame[] = "XDSP-CLOSE-RACE-01";
    int cycle;
    bool ok = true;

    /* 全局事件环就位（调度器懒创建）：IOCP 模式打开的前提。 */
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    if (overridePort && overridePort[0]) {
        strncpy(portName, overridePort, sizeof(portName) - 1);
        portName[sizeof(portName) - 1] = '\0';
    } else {
        XSerialPortInfoList* list = XSerialPortInfo_availablePorts();
        size_t count = list ? XVector_size_base((const XContainer*)list) : 0;
        size_t i;
        portName[0] = '\0';
        for (i = 0; i < count && portName[0] == '\0'; ++i) {
            XSerialPortInfo** slot = (XSerialPortInfo**)XVector_at_base(list, (int64_t)i);
            XSerialPortInfo* info = slot ? *slot : NULL;
            const XString* name;
            if (!info || XSerialPortInfo_isBusy(info)) continue;
            name = XSerialPortInfo_portName(info);
            if (!name) continue;
            strncpy(portName, XString_toUtf8(name), sizeof(portName) - 1);
            portName[sizeof(portName) - 1] = '\0';
        }
        for (i = 0; i < count; ++i) {
            XSerialPortInfo** slot = (XSerialPortInfo**)XVector_at_base(list, (int64_t)i);
            if (slot && *slot) XClassDelete(*slot);
        }
        if (list) XClassDelete(list);
        if (portName[0] == '\0') {
            puts("XDeviceSerialPort test: SKIP (win32 close-race: 无可用串口)");
            return true;
        }
    }

    for (cycle = 0; cycle < 6 && ok; ++cycle) {
        XSerialPort* serial = XSerialPort_create();
        int64_t t0;
        int64_t closeMs;
        int64_t written;
        int pump;
        if (!serial) { ok = false; break; }
        XSerialPort_setPortName(serial, portName);
        XSerialPort_setBaudRate(serial, 9600, XSerialPort_AllDirections);
        if (!XSerialPort_open_base(&serial->base, XIODevice_ReadWrite)) {
            XPrintf("  close-race 第 %d 轮：open %s 失败（被占用？）\n",
                    cycle + 1, portName);
            XClassDelete((XClass*)serial);
            ok = false;
            break;
        }
        /* 回归前提：确认走的是 IOCP 事件驱动通道（常驻读已布防）。
         * 同步兜底形态下本回归无从显形，明确报告而非静默假绿。 */
        if (!XDeviceSerialPort_platformArmReadable(serial->base.m_fd,
                                                   xdeviceSerialProbeReadable,
                                                   serial)) {
            XPrintf("  close-race 第 %d 轮：串口未走 IOCP 布防"
                    "（同步兜底？XSERIAL_WIN32_SYNC？）\n", cycle + 1);
            XSerialPort_close_base(&serial->base);
            XClassDelete((XClass*)serial);
            ok = false;
            break;
        }
        /* 制造在途写 + 常驻读双挂后立即断开（GUI「断开」时刻的形态）。 */
        written = XSerialPort_write_base(&serial->base, probeFrame,
                                         (int64_t)sizeof(probeFrame) - 1);
        if (written != (int64_t)sizeof(probeFrame) - 1) {
            XPrintf("  close-race 第 %d 轮：write 返回 %d\n",
                    cycle + 1, (int)written);
            XSerialPort_close_base(&serial->base);
            XClassDelete((XClass*)serial);
            ok = false;
            break;
        }
        XThread_msleep(5);
        t0 = XDateTime_currentMSecsSinceEpoch();
        XSerialPort_close_base(&serial->base);
        closeMs = XDateTime_currentMSecsSinceEpoch() - t0;
        /* 确定性判据：缺陷形态下关闭回收空转 XSPW32_CANCEL_RECLAIM_TRIES
         * (50) 次（完成回调续挂读 ⇒ m_readPending 永不为 false），实测
         * ≥130ms；修复后回收恰好消费一个完成包即收口，毫秒级。40ms 门
         * 两侧余量均 >25%。 */
        if (closeMs > 40) {
            XPrintf("  close-race 第 %d 轮：close 耗时 %lld ms > 40ms"
                    "（关闭回收空转/悬垂完成包）\n", cycle + 1, (long long)closeMs);
            XClassDelete((XClass*)serial);
            ok = false;
            break;
        }
        /* 悬垂完成包的显形窗口：close 后事件环必须再泵若干轮
         * （GUI 是下一帧崩，控制台等价物是这里的出队循环；
         *  在守护页/池化分配器下此处直接 AV）。 */
        for (pump = 0; pump < 64; ++pump) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if ((pump & 7) == 7) XThread_msleep(2);
        }
        XClassDelete((XClass*)serial);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XPrintf("  close-race 第 %d/%d 轮：close 耗时 %lld ms\n",
                cycle + 1, 6, (long long)closeMs);
    }
    return ok;
}
#endif /* _WIN32 && XDEVICE_SERIALPORT_ON */

bool XDeviceSerialPortTest_runAll(void)
{
#if !XDEVICE_SERIALPORT_ON
    puts("XDeviceSerialPort test: SKIP");
    return true;
#elif defined(_WIN32)
    bool ok = xdeviceSerialPortWin32CloseRaceTest();
    puts(ok ? "XDeviceSerialPort test: PASS" : "XDeviceSerialPort test: FAIL");
    return ok;
#elif !defined(__linux__) && !defined(__unix__) && !defined(__APPLE__)
    puts("XDeviceSerialPort test: SKIP");
    return true;
#else
    int master = -1;
    int slaveFd = -1;
    const char* slaveName;
    XString* path = NULL;
    XDeviceSerialPortOpenOptions options;
    XSerialPort serial;
    XFd fd = XFD_INVALID;
    XVarList* clearArgs = NULL;
    XVariant value;
    XSerialPort_Direction directions = XSerialPort_AllDirections;
    char buffer[32];
    const char incoming[] = "device-in";
    const char outgoing[] = "device-out";
    const char highIncoming[] = "high-in";
    const char highOutgoing[] = "high-out";
    int error = XDeviceError_None;
    bool ok = false;

    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) goto cleanup;
    slaveName = ptsname(master);
    if (!slaveName) goto cleanup;
    path = XString_create_utf8(slaveName);
    if (!path) goto cleanup;

    XDeviceSerialPortOpenOptions_init(&options);
    options.m_base.m_target = path;
    options.m_owner = NULL;
    fd = XDevice_open(XDeviceType_Serial, &options.m_base, &error);
    if (fd == XFD_INVALID || error != XDeviceError_None || XFd_type(fd) != XFD_TYPE_CLASS)
        goto cleanup;
    if (!XDevice_handle(fd) || XDevice_handle(fd)->m_fd != fd) goto cleanup;
    if (!checkDevicePropertyInt(fd, XDeviceSerialPortProperty_BaudRate, XSerialPort_Baud9600) ||
        !checkDevicePropertyInt(fd, XDeviceSerialPortProperty_DataBits, XSerialPort_Data8) ||
        !checkDevicePropertyInt(fd, XDeviceSerialPortProperty_Parity, XSerialPort_NoParity) ||
        !checkDevicePropertyInt(fd, XDeviceSerialPortProperty_StopBits, XSerialPort_OneStop) ||
        !checkDevicePropertyInt(fd, XDeviceSerialPortProperty_FlowControl, XSerialPort_NoFlowControl))
        goto cleanup;

    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    XVariant_setValue_int(&value, XSerialPort_Baud115200);
    if (!XDevice_setProperty(fd, (XDeviceProperty)XDeviceSerialPortProperty_BaudRate, &value) ||
        !XDevice_getProperty(fd, (XDeviceProperty)XDeviceSerialPortProperty_BaudRate, &value) ||
        XVariant_toInt(&value) != XSerialPort_Baud115200) {
        XClassDeinit((XClass*)&value);
        goto cleanup;
    }
    XClassDeinit((XClass*)&value);

    if (write(master, incoming, sizeof(incoming) - 1) != (ssize_t)(sizeof(incoming) - 1) ||
        XDevice_read(fd, buffer, sizeof(incoming) - 1) != (int64_t)(sizeof(incoming) - 1) ||
        memcmp(buffer, incoming, sizeof(incoming) - 1) != 0 ||
        XDevice_write(fd, outgoing, sizeof(outgoing) - 1) != (int64_t)(sizeof(outgoing) - 1) ||
        read(master, buffer, sizeof(outgoing) - 1) != (ssize_t)(sizeof(outgoing) - 1) ||
        memcmp(buffer, outgoing, sizeof(outgoing) - 1) != 0)
        goto cleanup;

    clearArgs = XVarList_Create(XVar(XSerialPort_Direction, directions));
    if (!clearArgs || !XDevice_control(fd, XDeviceSerialPortCommand_Clear, clearArgs, NULL))
        goto cleanup;
    XVarList_delete(clearArgs);
    clearArgs = NULL;
    XDevice_close(fd);
    fd = XFD_INVALID;

    XSerialPort_init(&serial);
    XSerialPort_setPortName(&serial, slaveName);
    if (!XSerialPort_setBaudRate(&serial, XSerialPort_Baud9600, XSerialPort_AllDirections) ||
        !XSerialPort_open_base(&serial.base, XIODevice_ReadWrite))
        goto cleanup_serial;
    if (XSerialPort_handle(&serial) == (XHandle)-1) goto cleanup_serial;
    if (write(master, highIncoming, sizeof(highIncoming) - 1) != (ssize_t)(sizeof(highIncoming) - 1) ||
        XSerialPort_read_base(&serial.base, buffer, sizeof(highIncoming) - 1) != (int64_t)(sizeof(highIncoming) - 1) ||
        memcmp(buffer, highIncoming, sizeof(highIncoming) - 1) != 0 ||
        XSerialPort_write_base(&serial.base, highOutgoing, sizeof(highOutgoing) - 1) != (int64_t)(sizeof(highOutgoing) - 1) ||
        read(master, buffer, sizeof(highOutgoing) - 1) != (ssize_t)(sizeof(highOutgoing) - 1) ||
        memcmp(buffer, highOutgoing, sizeof(highOutgoing) - 1) != 0)
        goto cleanup_serial;
    (void)XSerialPort_setDataTerminalReady(&serial, true);
    (void)XSerialPort_setRequestToSend(&serial, true);
    (void)XSerialPort_setBreakEnabled(&serial, true);
    (void)XSerialPort_setBreakEnabled(&serial, false);
    if (!XSerialPort_clear(&serial, XSerialPort_AllDirections) || !XSerialPort_flush(&serial))
        goto cleanup_serial;
    ok = true;

cleanup_serial:
    if (XSerialPort_isOpen(&serial.base)) XSerialPort_close_base(&serial.base);
    XClassDeinit((XClass*)&serial);
cleanup:
    if (clearArgs) XVarList_delete(clearArgs);
    if (fd != XFD_INVALID) XDevice_close(fd);
    if (slaveFd >= 0) close(slaveFd);
    if (path) XClassDelete((XClass*)path);
    if (master >= 0) close(master);
    puts(ok ? "XDeviceSerialPort test: PASS" : "XDeviceSerialPort test: FAIL");
    return ok;
#endif
}
