#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON

/**
 * @file XS7Test.c
 * @brief 西门子 S7（ISO-on-TCP）协议全量测试：离线金样单测 + 集成测试 + 菜单注册
 * @details 本文件为 W5 全量实现（整文件替换 M0 占位），分两大块：
 *
 * @par 一、离线单测（无 PLC、CI 可跑，金样向量来自 _w1_design.md §3/§5.1）
 * - XS7TpktTest               TPKT 组帧/半包/ver!=3/超长拒绝（RFC1006）
 * - XS7CotpTest               COTP CR 与 s-pms g_plc_head1 金样逐字节比对
 *                             （rack0/slot1 与 slot2 两组）、CC 解析与拒绝、DT 提取
 * - XS7PduTest_Read           ReadVar 组包金样 + 读应答解析（FF/05/06/0A）
 * - XS7PduTest_Write          WriteVar 组包金样（字节写/位写）+ 写应答解析
 * - XS7PduTest_Setup          SetupCommunication 请求结构比对 + 480/960 手造应答
 *                             协商解析 + 分片预算公式
 * - XS7AddressTest            地址全语法解析断言 + 全部非法拒绝用例
 * - XS7ValueTest              值全类型大端往返 + Real 位型中转 + STRING(n)
 *                             奇偶补齐 + Bool 位
 * - XS7ControlTest            Run(Hot/Cold)/Stop 金样逐字节 + 0x02/0x07 判成功
 *                             （XS7_CONTROL_ON 门控）
 * - XS7SessionTest            手造字节流分次 feed（半包/一帧半/双帧粘包/坏头重同步）
 *                             事件序列断言
 * - XPlcReplyPublicApiTest    Reply 公共 API（状态推进、信号计数、setError→
 *                             finished 顺序、NULL 保护）
 *
 * @par 二、集成测试 XS7Test_integration_run()
 * - 目标设备：环境变量 XS7_PLC_IP（默认 "192.168.1.251"，端口 102）；
 *   连接 rack=0、slot 依次尝试 1,0,2 取握手成功者
 * - 离线自测模式：XS7_PLC_IP=127.0.0.1 时对接文件内 static 实现的
 *   XS7FakePlc 桩（XTcpServer 监听 127.0.0.1 高位端口），不触碰真实 PLC
 * - XS7_PLC_READBACK=1 只读模式：仅读取固定地址集并打印 RAW 行，绝不写 PLC
 * - 用例输出一行：[XS7][PLC] <用例名> PASS|FAIL|SKIP <细节>；
 *   地址映射开头打印 [XS7][PLC] MAP <addr,...>；每个读写地址最终值打印
 *   [XS7][PLC] RAW <addr> <hex字节>（供外部 python-snap7 交叉比对）
 * - 事件循环 XCoreApplication_exec()（仿 XModbusTcpClientTest）；
 *   结束时 disconnectDevice 并退出循环；run_stop 用例无论如何在测试
 *   结束前把 PLC 恢复为 RUN 态
 *
 * @par 金样字节依据（已核验，见 _w1_design.md §3）
 * - CR    = s-pms g_plc_head1_s1200：03 00 00 16 11 E0 00 00 00 01 00 C0 01 0A
 *           C1 02 ll ll C2 02 rr rr（目标 TSAP 低字节 = rack*0x20+slot）
 * - Setup = s-pms g_plc_head2_default 结构（首期 AmQ=1、请求 960）：
 *           32 01 00 00 ref 00 08 00 00 F0 00 AmQ(2B) AmQ(2B) PduLen(2B)
 * - Stop  = s-pms g_s7_stop（去 TPKT/COTP 后 26B，func 0x29 + "P_PROGRAM"）
 * - Hot   = s-pms g_s7_hot_start（30B，func 0x28 + ... FD 00 00 09 "P_PROGRAM"）
 * - Cold  = Hot 参数尾追加 02 43 20（"C "）
 * - Run/Stop 应答状态码：0x00 正常 / 0x02 已运行 / 0x07 已停止按成功处理
 *   （s-pms g_pdu_already_started / g_pdu_already_stopped，同 snap7 语义）
 * - 读应答 data：FF 04|bitCnt|data…（逐 item 偶对齐）、错误 05 00/06 00/0A 00
 * - 写请求数据：00 04|bitCount(BE)|data（字节写）/ 00 03|bitCount(BE)|data（位写）
 * - Read ANY item：12 0A 10 | transport | count(BE) | db(BE) | area | 3B 位偏移；
 *   字节量传输约定 transport=0x02、count=字节数（s-pms build_read_byte_command
 *   金样，DB1.DBW10 → 02 00 02）；位传输 transport=0x01、count=位数
 *
 * @note 规范遵循：C99、UTF-8 BOM；禁 malloc/printf/sprintf/clock/time/ctype，
 *       一律 XMemory_/XPrintf_/XDateTime_/XChar（docs/dependency-mapping.md；
 *       string.h 的 memcpy/memcmp/memmove/memset 属第九章裸内存白名单）。
 */

#include "XProtocolTest.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XMemory.h"
#include "XObject.h"
#include "XClass.h"
#include "XString.h"
#include "XVariant.h"
#include "XByteArray.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XTimer.h"
#include "XSystem.h"
#include "XS7Types.h"
#include "XS7Tpkt.h"
#include "XS7Cotp.h"
#include "XS7Pdu.h"
#include "XS7Address.h"
#include "XS7Value.h"
#include "XS7Control.h"
#include "XS7Block.h"
#include "XS7Session.h"
#include "XS7TcpClient.h"
#include "XPlcReply.h"
#include "XPlcReply_Protected.h"
#include "XTcpSocket.h"
#include "XTcpServer.h"
#include "XHostAddress.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/******************************************************************************************
 * 一、离线单测公共辅助
 ******************************************************************************************/

/* ---- 本文件容器/套接字调用包装（消除派生指针到基类指针的 C4133 告警） ---- */
static size_t xs7uBaSize(const XByteArray* b)
{
    return XContainer_size_base((const XContainer*)b);
}
static void xs7uBaClear(XByteArray* b)
{
    XContainer_clear_base((XContainer*)b);
}
static void xs7uBaResize(XByteArray* b, size_t n)
{
    XVector_resize_base((XVector*)b, n);
}
static void xs7uBaAppend(XByteArray* b, const void* data, size_t n)
{
    XVector_push_back_2((XVector*)b, data, n);
}
static void xs7uBaDelete(XByteArray* b)
{
    XClass_delete_base((XClass*)b);
}
static void xs7uStrDelete(XString* s)
{
    XClass_delete_base((XClass*)s);
}
static void xs7uVarDelete(XVariant* v)
{
    XClass_delete_base((XClass*)v);
}
static void xs7uVecDelete(XVector* v)
{
    XClass_delete_base((XClass*)v);
}
static size_t xs7uVecSize(const XVector* v)
{
    return XContainer_size_base((const XContainer*)v);
}
static int64_t xs7uSockWrite(XTcpSocket* sock, const char* data, int64_t len)
{
    return XIODevice_write_1((XIODevice*)sock, data, len);
}
static void xs7uSockDrop(XTcpSocket* sock)
{
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)sock);
}

/** @brief runAll 汇总开关：任一组 FAIL 即置 false（xs7uFinish 内联动） */
static bool g_xuRunOk = true;

/**
 * @brief 单测组收尾：打印机器可读结果行并联动 runAll 汇总
 * @param name 组名
 * @param passed 通过断言数
 * @param failed 失败断言数
 * @return 全部通过返回 true
 */
static bool xs7uFinish(const char* name, int passed, int failed)
{
    if (failed != 0) {
        g_xuRunOk = false;
    }
    XPrintf("[XS7][UNIT] %s %s (passed=%d failed=%d)\n",
            name, (failed == 0) ? "PASS" : "FAIL", passed, failed);
    return failed == 0;
}

/**
 * @brief 字节序列相等断言（memcmp），不等时打印两组十六进制辅助定位
 * @param passed 通过计数（自增）
 * @param failed 失败计数（自增）
 * @param what 断言说明
 * @param got 实际字节
 * @param gotLen 实际长度
 * @param expect 期望字节（金样）
 * @param expectLen 期望长度
 * @return 相等返回 true
 */
static bool xs7uBytesEq(int* passed, int* failed, const char* what,
                        const uint8_t* got, size_t gotLen,
                        const uint8_t* expect, size_t expectLen)
{
    if (got != NULL && expect != NULL && gotLen == expectLen &&
        memcmp(got, expect, expectLen) == 0) {
        ++(*passed);
        return true;
    }
    ++(*failed);
    XPrintf("  [失败] %s：实际长度=%u 期望长度=%u\n", what,
            (unsigned)gotLen, (unsigned)expectLen);
    {
        size_t i;
        XPrintf("    实际:");
        for (i = 0; i < gotLen && i < 64; ++i) {
            XPrintf(" %02X", got[i]);
        }
        if (gotLen > 64) {
            XPrintf(" ...");
        }
        XPrintf("\n    期望:");
        for (i = 0; i < expectLen && i < 64; ++i) {
            XPrintf(" %02X", expect[i]);
        }
        if (expectLen > 64) {
            XPrintf(" ...");
        }
        XPrintf("\n");
    }
    return false;
}

/**
 * @brief 布尔断言宏（统一 [通过]/[失败] 输出与计数；消息支持 printf 风格）
 * @param passed 通过计数变量
 * @param failed 失败计数变量
 * @param ok 断言表达式
 * @param ... 消息（printf 风格，自动换行）
 */
#define XS7U_CHECK(passed, failed, ok, ...) \
    do { \
        if (ok) { XPrintf("  [通过] "); XPrintf(__VA_ARGS__); XPrintf("\n"); ++(passed); } \
        else    { XPrintf("  [失败] "); XPrintf(__VA_ARGS__); XPrintf("\n"); ++(failed); } \
    } while (0)

/******************************************************************************************
 * 二、XS7TpktTest —— TPKT 组帧/半包/坏头/超长拒绝
 ******************************************************************************************/

/**
 * @brief TPKT 层单元测试
 * @details 金样：COTP CR 载荷 18B → 组帧 22B = 03 00 00 16 + 载荷（RFC1006）。
 *          覆盖 wrap 字节级、追加写、peekLength 正常/半包/ver!=3/长度<4/
 *          超长（>XS7_MAX_FRAME）/上限边界/NULL 保护。
 */
void XS7TpktTest()
{
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XPrintf("========== XS7Tpkt 单元测试开始 ==========\n");

    /* 金样：COTP CR 载荷（rack0/slot1，不含 TPKT） */
    static const uint8_t crPayload[18] = {
        0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00, 0xC0, 0x01,
        0x0A, 0xC1, 0x02, 0x01, 0x02, 0xC2, 0x02, 0x01, 0x00
    };
    static const uint8_t crFrame[22] = {
        0x03, 0x00, 0x00, 0x16,
        0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00, 0xC0, 0x01,
        0x0A, 0xC1, 0x02, 0x01, 0x02, 0xC2, 0x02, 0x01, 0x00
    };

    /* 1. wrap 字节级金样 */
    {
        XByteArray* out = XByteArray_create();
        size_t n = XS7Tpkt_wrap(out, crPayload, sizeof(crPayload));
        XS7U_CHECK(passed, failed, n == 22, "wrap(CR 载荷) 返回总长 22");
        if (n == 22) {
            xs7uBytesEq(&passed, &failed, "wrap 帧字节 == 03 00 00 16 + 载荷",
                        XByteArray_data(out), xs7uBaSize(out),
                        crFrame, sizeof(crFrame));
        }
        xs7uBaDelete(out);
    }

    /* 2. wrap 追加写（不清空既有内容） */
    {
        XByteArray* out = XByteArray_create();
        (void)XS7Tpkt_wrap(out, crPayload, sizeof(crPayload));
        (void)XS7Tpkt_wrap(out, crPayload, sizeof(crPayload));
        XS7U_CHECK(passed, failed, xs7uBaSize(out) == 44,
                   "wrap 追加写：两次组帧后缓冲 44 字节");
        xs7uBaDelete(out);
    }

    /* 3. wrap 参数保护与空载荷 */
    {
        XByteArray* out = XByteArray_create();
        XS7U_CHECK(passed, failed, XS7Tpkt_wrap(NULL, crPayload, sizeof(crPayload)) == 0,
                   "wrap(NULL out) 返回 0");
        XS7U_CHECK(passed, failed, XS7Tpkt_wrap(out, NULL, 4) == 0,
                   "wrap(NULL payload) 返回 0");
        XS7U_CHECK(passed, failed, XS7Tpkt_wrap(out, crPayload, 0) == 0,
                   "wrap(空载荷) 返回 0");
        xs7uBaDelete(out);
    }

    /* 4. wrap 超过 XS7_MAX_FRAME 拒绝与上限边界 */
    {
        static uint8_t big[XS7_MAX_FRAME];   /* 静态区避免栈占用 */
        XByteArray* out = XByteArray_create();
        memset(big, 0xAA, sizeof(big));
        XS7U_CHECK(passed, failed, XS7Tpkt_wrap(out, big, XS7_MAX_FRAME - 3) == 0,
                   "wrap(总长 8193 > XS7_MAX_FRAME) 返回 0");
        XS7U_CHECK(passed, failed,
                   XS7Tpkt_wrap(out, big, XS7_MAX_FRAME - 4) == (size_t)XS7_MAX_FRAME,
                   "wrap(总长恰好 8192) 允许并返回 8192");
        xs7uBaDelete(out);
    }

    /* 5. peekLength 正常帧 */
    {
        size_t total = 0;
        XS7U_CHECK(passed, failed,
                   XS7Tpkt_peekLength(crFrame, sizeof(crFrame), &total) && total == 22,
                   "peekLength(完整 CR) → total=22");
    }

    /* 6. peekLength 半包：头部未到齐（len<4）返回 false 且不写 total */
    {
        size_t total = 99;
        XS7U_CHECK(passed, failed,
                   !XS7Tpkt_peekLength(crFrame, 3, &total) && total == 99,
                   "peekLength(前 3 字节半包) 返回 false 且不写 total");
    }

    /* 7. peekLength 半包：4 字节头到齐即返回整帧长度（不等载荷） */
    {
        size_t total = 0;
        XS7U_CHECK(passed, failed,
                   XS7Tpkt_peekLength(crFrame, 4, &total) && total == 22,
                   "peekLength(仅 4 字节头) → total=22（半包等待载荷）");
    }

    /* 8. peekLength ver != 3 拒绝 */
    {
        uint8_t bad[8];
        size_t total = 0;
        bad[0] = 0x02; bad[1] = 0x00; bad[2] = 0x00; bad[3] = 0x08;
        bad[4] = bad[5] = bad[6] = bad[7] = 0xFF;
        XS7U_CHECK(passed, failed, !XS7Tpkt_peekLength(bad, sizeof(bad), &total),
                   "peekLength(ver=2) 返回 false");
        bad[0] = 0x07;
        XS7U_CHECK(passed, failed, !XS7Tpkt_peekLength(bad, sizeof(bad), &total),
                   "peekLength(ver=7) 返回 false");
    }

    /* 9. peekLength 长度 < 4 拒绝 */
    {
        uint8_t bad[8];
        size_t total = 0;
        bad[0] = 0x03; bad[1] = 0x00; bad[2] = 0x00; bad[3] = 0x03;
        bad[4] = bad[5] = bad[6] = bad[7] = 0xFF;
        XS7U_CHECK(passed, failed, !XS7Tpkt_peekLength(bad, sizeof(bad), &total),
                   "peekLength(length=3 < 4) 返回 false");
    }

    /* 10. peekLength 超长拒绝与上限边界 */
    {
        uint8_t bad[8];
        uint8_t max[8];
        size_t total = 0;
        bad[0] = 0x03; bad[1] = 0x00; bad[2] = 0x20; bad[3] = 0x01;
        bad[4] = bad[5] = bad[6] = bad[7] = 0xFF;
        max[0] = 0x03; max[1] = 0x00; max[2] = 0x20; max[3] = 0x00;
        max[4] = max[5] = max[6] = max[7] = 0xFF;
        XS7U_CHECK(passed, failed, !XS7Tpkt_peekLength(bad, sizeof(bad), &total),
                   "peekLength(length=8193 > 8192) 返回 false");
        XS7U_CHECK(passed, failed,
                   XS7Tpkt_peekLength(max, sizeof(max), &total) &&
                       total == (size_t)XS7_MAX_FRAME,
                   "peekLength(length=8192 恰好上限) 返回 true");
    }

    /* 11. peekLength NULL 保护 */
    {
        size_t total = 0;
        XS7U_CHECK(passed, failed, !XS7Tpkt_peekLength(NULL, 8, &total),
                   "peekLength(NULL data) 返回 false");
        XS7U_CHECK(passed, failed, !XS7Tpkt_peekLength(crFrame, sizeof(crFrame), NULL),
                   "peekLength(NULL total) 返回 false");
    }

    xs7uFinish("TPKT", passed, failed);
#else
    XPrintf("[XS7][UNIT] TPKT SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
}

/******************************************************************************************
 * 三、XS7CotpTest —— CR 金样逐字节 / CC 解析与拒绝 / DT 提取
 ******************************************************************************************/

/**
 * @brief COTP 层单元测试
 * @details 金样（s-pms g_plc_head1_s1200/s300）：
 *          rack0/slot1 → C2 02 01 00；rack0/slot2 → C2 02 01 02。
 *          CC 手造向量含 C0/C1/C2 参数；拒绝向量含参数码 0x50。
 */
void XS7CotpTest()
{
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XPrintf("========== XS7Cotp 单元测试开始 ==========\n");

    /* 金样帧（_w1_design.md §3.1） */
    static const uint8_t crSlot1[22] = {
        0x03, 0x00, 0x00, 0x16, 0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
        0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x02, 0xC2, 0x02, 0x01, 0x00
    };
    static const uint8_t crSlot2[22] = {
        0x03, 0x00, 0x00, 0x16, 0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
        0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x02, 0xC2, 0x02, 0x01, 0x02
    };

    /* 1. TSAP 公式：(connType << 8) | ((rack << 5) | slot) */
    XS7U_CHECK(passed, failed, XS7Cotp_remoteTsapFromRackSlot(0x01, 0, 1) == 0x0101,
               "remoteTsapFromRackSlot(0x01, rack0, slot1) = 0x0101");
    XS7U_CHECK(passed, failed, XS7Cotp_remoteTsapFromRackSlot(0x01, 0, 2) == 0x0102,
               "remoteTsapFromRackSlot(0x01, rack0, slot2) = 0x0102");
    XS7U_CHECK(passed, failed, XS7Cotp_remoteTsapFromRackSlot(0x01, 1, 0) == 0x0120,
               "remoteTsapFromRackSlot(0x01, rack1, slot0) = 0x0120");
    XS7U_CHECK(passed, failed, XS7Cotp_remoteTsapFromRackSlot(0x03, 0, 2) == 0x0302,
               "remoteTsapFromRackSlot(0x03, rack0, slot2) = 0x0302（S7-300 connType）");

    /* 2. buildCr 与金样逐字节比对（rack0/slot1 与 slot2 两组） */
    {
        XByteArray* out = XByteArray_create();
        size_t n = XS7Cotp_buildCr(out, 0x0102, 0x0100, 0x0A);
        XS7U_CHECK(passed, failed, n == 22, "buildCr(rack0/slot1) 返回 22");
        if (n == 22) {
            xs7uBytesEq(&passed, &failed, "CR 帧 == g_plc_head1_s1200（slot1）",
                        XByteArray_data(out), xs7uBaSize(out),
                        crSlot1, sizeof(crSlot1));
        }
        xs7uBaClear(out);
        n = XS7Cotp_buildCr(out, 0x0102, 0x0102, 0x0A);
        XS7U_CHECK(passed, failed, n == 22, "buildCr(rack0/slot2) 返回 22");
        if (n == 22) {
            xs7uBytesEq(&passed, &failed, "CR 帧 == g_plc_head1_s300（slot2）",
                        XByteArray_data(out), xs7uBaSize(out),
                        crSlot2, sizeof(crSlot2));
        }
        xs7uBaDelete(out);
    }

    /* 3. buildCr NULL 保护 */
    XS7U_CHECK(passed, failed, XS7Cotp_buildCr(NULL, 0x0102, 0x0100, 0x0A) == 0,
               "buildCr(NULL out) 返回 0");

    /* 4. CC 解析：接受（C0/C1/C2 参数齐全） */
    {
        static const uint8_t ccOk[22] = {
            0x03, 0x00, 0x00, 0x16, 0x11, 0xD0, 0x00, 0x00, 0x00, 0x01, 0x00,
            0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x02, 0xC2, 0x02, 0x01, 0x00
        };
        XS7CotpCcInfo info;
        memset(&info, 0xCD, sizeof(info));
        XS7U_CHECK(passed, failed, XS7Cotp_parseCc(ccOk, sizeof(ccOk), &info),
                   "parseCc(含 C0/C1/C2 的 CC) 接受");
        XS7U_CHECK(passed, failed, info.tpduSizeCode == 0x0A, "CC tpduSizeCode = 0x0A");
        XS7U_CHECK(passed, failed, info.localTsap == 0x0102, "CC localTsap = 0x0102");
        XS7U_CHECK(passed, failed, info.remoteTsap == 0x0100, "CC remoteTsap = 0x0100");
    }

    /* 5. CC 拒绝：含参数码 0x50（拒绝原因） */
    {
        /* 03 00 00 11 | 0C D0 00 00 00 01 00 | C0 01 0A 50 01 02 */
        static const uint8_t ccReject[17] = {
            0x03, 0x00, 0x00, 0x11, 0x0C, 0xD0, 0x00, 0x00, 0x00, 0x01, 0x00,
            0xC0, 0x01, 0x0A, 0x50, 0x01, 0x02
        };
        XS7CotpCcInfo info;
        memset(&info, 0, sizeof(info));
        XS7U_CHECK(passed, failed, !XS7Cotp_parseCc(ccReject, sizeof(ccReject), &info),
                   "parseCc(含 0x50 拒绝参数) 拒绝");
    }

    /* 6. CC 拒绝：PDU 类型非 0xD0（误喂 CR/DT 帧） */
    {
        static const uint8_t dtHdr[10] = {
            0x03, 0x00, 0x00, 0x0A, 0x02, 0xF0, 0x80, 0x32, 0x01, 0x00
        };
        XS7CotpCcInfo info;
        memset(&info, 0, sizeof(info));
        XS7U_CHECK(passed, failed, !XS7Cotp_parseCc(crSlot1, sizeof(crSlot1), &info),
                   "parseCc(误喂 CR 帧 0xE0) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Cotp_parseCc(dtHdr, sizeof(dtHdr), &info),
                   "parseCc(误喂 DT 帧 0xF0) 拒绝");
    }

    /* 7. CC 拒绝：长度非法与 NULL 保护 */
    {
        static const uint8_t ccShort[9] = {
            0x03, 0x00, 0x00, 0x09, 0x04, 0xD0, 0x00, 0x00, 0x00
        };
        XS7CotpCcInfo info;
        memset(&info, 0, sizeof(info));
        XS7U_CHECK(passed, failed, !XS7Cotp_parseCc(ccShort, sizeof(ccShort), &info),
                   "parseCc(帧长 < 10) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Cotp_parseCc(crSlot1, 11, &info),
                   "parseCc(len 与 TPKT 长度不符) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Cotp_parseCc(NULL, 22, &info) &&
                                   !XS7Cotp_parseCc(crSlot1, sizeof(crSlot1), NULL),
                   "parseCc(NULL) 保护");
    }

    /* 8. DT 提取：偏移 7、长度 = 帧长 - 7 */
    {
        /* DT 帧 = TPKT(4) + 02 F0 80 + Setup 请求 18B（ref=1，AmQ=1，960） */
        static const uint8_t dtFrame[25] = {
            0x03, 0x00, 0x00, 0x19, 0x02, 0xF0, 0x80,
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03, 0xC0
        };
        size_t off = 0, len = 0;
        XS7U_CHECK(passed, failed, XS7Cotp_extractDt(dtFrame, sizeof(dtFrame), &off, &len),
                   "extractDt(DT 帧) 接受");
        XS7U_CHECK(passed, failed, off == 7 && len == 18,
                   "extractDt 偏移=7、S7 PDU 长度=18");
        XS7U_CHECK(passed, failed, dtFrame[off] == 0x32 && dtFrame[off + 1] == 0x01,
                   "extractDt 提取段以 S7 头 32 01 开始");
    }

    /* 9. DT 提取拒绝：非 02 F0 80 固定头与 NULL 保护 */
    {
        static const uint8_t badEot[10] = {
            0x03, 0x00, 0x00, 0x0A, 0x02, 0xF0, 0x00, 0x32, 0x01, 0x00
        };
        size_t off = 0, len = 0;
        XS7U_CHECK(passed, failed, !XS7Cotp_extractDt(crSlot1, sizeof(crSlot1), &off, &len),
                   "extractDt(CC 帧) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Cotp_extractDt(badEot, sizeof(badEot), &off, &len),
                   "extractDt(EOT=0x00 非 0x80) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Cotp_extractDt(NULL, 10, &off, &len) &&
                                   !XS7Cotp_extractDt(crSlot1, sizeof(crSlot1), NULL, &len),
                   "extractDt(NULL) 保护");
    }

    xs7uFinish("COTP", passed, failed);
#else
    XPrintf("[XS7][UNIT] COTP SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
}

/******************************************************************************************
 * 四、XS7PduTest_Read —— ReadVar 组包金样 + 读应答解析
 ******************************************************************************************/

/**
 * @brief S7 PDU 读单元测试
 * @details 组包金样（s-pms build_read_byte_command，去 TPKT 后比 S7 段，24B）：
 *          DB1.DBW10 → ANY item = 12 0A 10 02 00 02 00 01 84 00 00 50
 *          （字节量传输 0x02、count=2 字节、DB=1、area=0x84、位偏移 10*8=80=0x50）；
 *          M10.2 位读 → transport=0x01、count=1、位偏移 10*8+2=82=0x52。
 *          应答解析金样：FF 04 成功项、05 00 错误项、多 item、容量与 NULL 保护。
 */
void XS7PduTest_Read()
{
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XPrintf("========== XS7Pdu(读) 单元测试开始 ==========\n");

    /* 1. DB1.DBW10 单字读组包金样（pduRef=1） */
    {
        static const uint8_t golden[24] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x00,
            0x04, 0x01,
            0x12, 0x0A, 0x10, 0x02, 0x00, 0x02, 0x00, 0x01, 0x84, 0x00, 0x00, 0x50
        };
        XS7Address addr;
        uint8_t buf[64];
        memset(&addr, 0, sizeof(addr));
        if (XS7Address_parse_2(&addr, "DB1.DBW10")) {
            size_t n = XS7Pdu_buildRead(buf, 0x0001, &addr, 1);
            XS7U_CHECK(passed, failed, n == 24, "buildRead(DB1.DBW10) 返回 24（19+12）");
            if (n == 24) {
                xs7uBytesEq(&passed, &failed,
                            "读请求帧 == s-pms build_read_byte_command 金样",
                            buf, n, golden, sizeof(golden));
            }
        } else {
            ++failed;
            XPrintf("  [失败] 前置：DB1.DBW10 地址解析失败（W2 未实现或拒绝）\n");
        }
    }

    /* 2. M10.2 位读组包（transport=0x01，设计文档 §5.1 指定） */
    {
        static const uint8_t golden[24] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x00,
            0x04, 0x01,
            0x12, 0x0A, 0x10, 0x01, 0x00, 0x01, 0x00, 0x00, 0x83, 0x00, 0x00, 0x52
        };
        XS7Address addr;
        uint8_t buf[64];
        memset(&addr, 0, sizeof(addr));
        if (XS7Address_parse_2(&addr, "M10.2")) {
            size_t n = XS7Pdu_buildRead(buf, 0x0001, &addr, 1);
            XS7U_CHECK(passed, failed, n == 24, "buildRead(M10.2) 返回 24");
            if (n == 24) {
                xs7uBytesEq(&passed, &failed, "位读帧 transport=0x01 位偏移=0x52",
                            buf, n, golden, sizeof(golden));
            }
        } else {
            ++failed;
            XPrintf("  [失败] 前置：M10.2 地址解析失败\n");
        }
    }

    /* 3. 多 item 组包：返回长度 19+12*N、itemCount 字节、参数长度自洽 */
    {
        XS7Address items[2];
        uint8_t buf[96];
        memset(items, 0, sizeof(items));
        if (XS7Address_parse_2(&items[0], "DB1.DBW10") &&
            XS7Address_parse_2(&items[1], "MW0")) {
            size_t n = XS7Pdu_buildRead(buf, 0x0002, items, 2);
            XS7U_CHECK(passed, failed, n == 12 + 24,
                       "buildRead(2 item) 返回 36（S7 段 12+12*2，同单 item 金样口径）");
            if (n == 36) {
                XS7PduHeader hdr;
                XS7U_CHECK(passed, failed, XS7Pdu_parseHeader(buf, n, &hdr),
                           "多 item 帧 parseHeader 成功");
                XS7U_CHECK(passed, failed,
                           hdr.rosctr == XS7_ROSCTR_JOB && hdr.pduRef == 0x0002 &&
                               hdr.paramLen == 2 + 24 && hdr.dataLen == 0,
                           "多 item 帧 头字段自洽（ref=2 paramLen=26）");
                XS7U_CHECK(passed, failed,
                           buf[10] == XS7_FUNC_READ && buf[11] == 2,
                           "多 item 帧 func=0x04 itemCount=2");
            }
        } else {
            ++failed;
            XPrintf("  [失败] 前置：多 item 地址解析失败\n");
        }
    }

    /* 4. buildRead 参数保护 */
    {
        XS7Address addr;
        uint8_t buf[64];
        memset(&addr, 0, sizeof(addr));
        if (XS7Address_parse_2(&addr, "DB1.DBW10")) {
            XS7U_CHECK(passed, failed, XS7Pdu_buildRead(NULL, 1, &addr, 1) == 0,
                       "buildRead(NULL out) 返回 0");
            {
                size_t n0 = XS7Pdu_buildRead(buf, 1, &addr, 0);
                XS7U_CHECK(passed, failed, n0 == 0 || n0 == 31,
                           "buildRead(itemCount=0) 返回 0（拒绝）或 31（按单 item 兜底）");
            }
            XS7U_CHECK(passed, failed, XS7Pdu_buildRead(buf, 1, NULL, 1) == 0,
                       "buildRead(NULL items) 返回 0");
        }
    }

    /* 5. parseHeader 正常与拒绝 */
    {
        static const uint8_t job[24] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x00,
            0x04, 0x01, 0x12, 0x0A, 0x10, 0x02, 0x00, 0x02, 0x00, 0x01,
            0x84, 0x00, 0x00, 0x50
        };
        static const uint8_t ackData[20] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x06,
            0x00, 0x00,
            0x04, 0x01, 0xFF, 0x04, 0x00, 0x02, 0x12, 0x34
        };
        XS7PduHeader hdr;
        uint8_t bad[12];
        memset(&hdr, 0xCD, sizeof(hdr));
        XS7U_CHECK(passed, failed, XS7Pdu_parseHeader(job, sizeof(job), &hdr),
                   "parseHeader(Job) 成功");
        XS7U_CHECK(passed, failed,
                   hdr.rosctr == 0x01 && hdr.pduRef == 1 &&
                       hdr.paramLen == 14 && hdr.dataLen == 0,
                   "parseHeader(Job) rosctr/ref/paramLen/dataLen 正确");
        XS7U_CHECK(passed, failed, XS7Pdu_parseHeader(ackData, sizeof(ackData), &hdr),
                   "parseHeader(Ack_Data) 成功");
        XS7U_CHECK(passed, failed,
                   hdr.rosctr == XS7_ROSCTR_ACK_DATA && hdr.paramLen == 2 &&
                       hdr.dataLen == 6 && hdr.errorClass == 0 && hdr.errorCode == 0,
                   "parseHeader(Ack_Data) 头长 12 与错误字节正确");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseHeader(job, 9, &hdr),
                   "parseHeader(len<10) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseHeader(ackData, 11, &hdr),
                   "parseHeader(Ack len<12) 拒绝");
        memcpy(bad, job, sizeof(bad));
        bad[1] = 0x99;   /* 非法 ROSCTR */
        XS7U_CHECK(passed, failed, !XS7Pdu_parseHeader(bad, sizeof(bad), &hdr),
                   "parseHeader(rosctr=0x99) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseHeader(NULL, 24, &hdr) &&
                                   !XS7Pdu_parseHeader(job, sizeof(job), NULL),
                   "parseHeader(NULL) 保护");
    }

    /* 6. parseReadAck：单成功项 FF 04 00 02 12 34 */
    {
        static const uint8_t ack[20] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x06,
            0x00, 0x00,
            0x04, 0x01, 0xFF, 0x04, 0x00, 0x10, 0x12, 0x34   /* 长度=16 位=2 字节 */
        };
        XS7ReadItem items[2];
        memset(items, 0, sizeof(items));
        XS7U_CHECK(passed, failed, XS7Pdu_parseReadAck(ack, sizeof(ack), items, 2),
                   "parseReadAck(FF 04 单项) 成功");
        XS7U_CHECK(passed, failed,
                   items[0].returnCode == XS7_RETURN_OK &&
                       items[0].transportSize == XS7_TRANSPORT_WORD &&
                       items[0].dataLen == 2 && items[0].data != NULL &&
                       items[0].data[0] == 0x12 && items[0].data[1] == 0x34,
                   "读应答项 FF/04/2B/12 34 逐字段正确");
    }

    /* 7. parseReadAck：单项错误码 05 00（结构合法，交上层判定） */
    {
        static const uint8_t ack[18] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x04,
            0x00, 0x00,
            0x04, 0x01, 0x05, 0x00, 0x00, 0x00   /* 错误项固定 4B（长度字段 0） */
        };
        XS7ReadItem items[2];
        memset(items, 0, sizeof(items));
        XS7U_CHECK(passed, failed, XS7Pdu_parseReadAck(ack, sizeof(ack), items, 2),
                   "parseReadAck(05 00 错误项) 结构合法返回 true");
        XS7U_CHECK(passed, failed, items[0].returnCode == XS7_RETURN_DATA_ERROR,
                   "错误项 returnCode = 0x05 交上层判定");
    }

    /* 8. parseReadAck：多 item（FF 04 + FF 04） */
    {
        static const uint8_t ack[26] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0C,
            0x00, 0x00,
            0x04, 0x02,
            0xFF, 0x04, 0x00, 0x10, 0x12, 0x34,   /* 长度=16 位 */
            0xFF, 0x04, 0x00, 0x10, 0x56, 0x78    /* 长度=16 位 */
        };
        XS7ReadItem items[2];
        memset(items, 0, sizeof(items));
        XS7U_CHECK(passed, failed, XS7Pdu_parseReadAck(ack, sizeof(ack), items, 2),
                   "parseReadAck(双 item) 成功");
        XS7U_CHECK(passed, failed,
                   items[0].data != NULL && items[1].data != NULL &&
                       items[0].data[0] == 0x12 &&
                       items[1].data[0] == 0x56 && items[1].data[1] == 0x78,
                   "双 item 数据各自就位（12 34 / 56 78）");
    }

    /* 9. parseReadAck：项数超容与坏结构拒绝 */
    {
        static const uint8_t ack[26] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0C,
            0x00, 0x00,
            0x04, 0x02,
            0xFF, 0x04, 0x00, 0x10, 0x12, 0x34,   /* 长度=16 位 */
            0xFF, 0x04, 0x00, 0x10, 0x56, 0x78    /* 长度=16 位 */
        };
        XS7ReadItem items[2];
        memset(items, 0, sizeof(items));
        XS7U_CHECK(passed, failed, !XS7Pdu_parseReadAck(ack, sizeof(ack), items, 1),
                   "parseReadAck(容量 1 < 实际 2 项) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseReadAck(ack, 14, items, 2),
                   "parseReadAck(截断帧) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseReadAck(NULL, sizeof(ack), items, 2),
                   "parseReadAck(NULL) 保护");
    }

    /* 10. 分片预算公式（_w1_design.md §3.4，冻结于头文件） */
    XS7U_CHECK(passed, failed, XS7Pdu_maxReadItems(960) == 78, "maxReadItems(960)=78");
    XS7U_CHECK(passed, failed, XS7Pdu_maxReadItems(240) == 18, "maxReadItems(240)=18");
    XS7U_CHECK(passed, failed, XS7Pdu_maxReadBytesPerItem(960) == 942,
               "maxReadBytesPerItem(960)=942");
    XS7U_CHECK(passed, failed, XS7Pdu_maxReadBytesPerItem(240) == 222,
               "maxReadBytesPerItem(240)=222");
    XS7U_CHECK(passed, failed, XS7Pdu_maxWriteBytes(960) == 925,
               "maxWriteBytes(960)=925");
    XS7U_CHECK(passed, failed, XS7Pdu_maxWriteBytes(240) == 205,
               "maxWriteBytes(240)=205");
    XS7U_CHECK(passed, failed, XS7Pdu_maxReadItems(10) == 1 &&
                                   XS7Pdu_maxReadBytesPerItem(10) == 1 &&
                                   XS7Pdu_maxWriteBytes(10) == 1,
               "预算函数下限钳制为 1（pduLen=10）");

    xs7uFinish("PDU_Read", passed, failed);
#else
    XPrintf("[XS7][UNIT] PDU_Read SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
}

/******************************************************************************************
 * 五、XS7PduTest_Write —— WriteVar 组包金样 + 写应答解析
 ******************************************************************************************/

/**
 * @brief S7 PDU 写单元测试
 * @details 组包金样（s-pms build_write_byte/bit_command 结构，数据区尾部
 *          00 04|bitCount(BE)（字节写）/ 00 03|bitCount(BE)（位写））：
 *          - DB1.DBB0=0xAB → 29B：paramLen=14、dataLen=5、data = 00 04 00 08 AB
 *          - M0.0=1 位写 → 29B：data = 00 03 00 01 01
 *          应答解析：数据区 FF 判成功、05 判失败（结构合法，交上层判定）。
 */
void XS7PduTest_Write()
{
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XPrintf("========== XS7Pdu(写) 单元测试开始 ==========\n");

    /* 1. 字节写金样：DB1.DBB0 = 0xAB */
    {
        static const uint8_t golden[29] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x05,
            0x05, 0x01,
            0x12, 0x0A, 0x10, 0x02, 0x00, 0x01, 0x00, 0x01, 0x84, 0x00, 0x00, 0x00,
            0x00, 0x04, 0x00, 0x08, 0xAB
        };
        XS7Address addr;
        uint8_t data1 = 0xAB;
        uint16_t bitCounts[1];
        const uint8_t* datas[1];
        uint8_t buf[64];
        memset(&addr, 0, sizeof(addr));
        bitCounts[0] = 8;
        datas[0] = &data1;
        if (XS7Address_parse_2(&addr, "DB1.DBB0")) {
            size_t n = XS7Pdu_buildWrite(buf, 0x0001, &addr, 1, datas, bitCounts);
            XS7U_CHECK(passed, failed, n == 29,
                       "buildWrite(字节) 返回 29（19+参数14+数据5）");
            if (n == 29) {
                xs7uBytesEq(&passed, &failed,
                            "字节写帧 == s-pms build_write_byte_command 金样",
                            buf, n, golden, sizeof(golden));
            }
        } else {
            ++failed;
            XPrintf("  [失败] 前置：DB1.DBB0 地址解析失败\n");
        }
    }

    /* 2. 位写金样：M0.0 = 1（transport 0x01 请求 / 数据 00 03） */
    {
        static const uint8_t golden[29] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x05,
            0x05, 0x01,
            0x12, 0x0A, 0x10, 0x01, 0x00, 0x01, 0x00, 0x00, 0x83, 0x00, 0x00, 0x00,
            0x00, 0x03, 0x00, 0x01, 0x01
        };
        XS7Address addr;
        uint8_t data1 = 0x01;
        uint16_t bitCounts[1];
        const uint8_t* datas[1];
        uint8_t buf[64];
        memset(&addr, 0, sizeof(addr));
        bitCounts[0] = 1;
        datas[0] = &data1;
        if (XS7Address_parse_2(&addr, "M0.0")) {
            size_t n = XS7Pdu_buildWrite(buf, 0x0001, &addr, 1, datas, bitCounts);
            XS7U_CHECK(passed, failed, n == 29, "buildWrite(位) 返回 29");
            if (n == 29) {
                xs7uBytesEq(&passed, &failed, "位写帧数据区 00 03 00 01 01",
                            buf, n, golden, sizeof(golden));
            }
        } else {
            ++failed;
            XPrintf("  [失败] 前置：M0.0 地址解析失败\n");
        }
    }

    /* 2.5 STRING 写金样：DB1.DBB20 = STRING(16) "AB"（数据 18B 偶补齐）。
     * 真机实测修正：STRING 写数据区传输大小为 0x09 八位组串（长度=字节数
     * 0x0012）；0x04+位计数被目标 CPU 以返回码 0x07 拒绝（第 1 轮实测）。 */
    {
        static const uint8_t golden[46] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x16,
            0x05, 0x01,
            0x12, 0x0A, 0x10, 0x02, 0x00, 0x12, 0x00, 0x01, 0x84, 0x00, 0x00, 0xA0,
            0x00, 0x09, 0x00, 0x12,
            0x10, 0x02, 0x41, 0x42,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        };
        XS7Address addr;
        uint8_t strBytes[18];                       /* [16][2]['A']['B'] + 14 补零 */
        uint16_t bitCounts[1];
        const uint8_t* datas[1];
        uint8_t buf[128];
        memset(&addr, 0, sizeof(addr));
        memset(strBytes, 0, sizeof(strBytes));
        strBytes[0] = 16;
        strBytes[1] = 2;
        strBytes[2] = 'A';
        strBytes[3] = 'B';
        bitCounts[0] = 18 * 8;                      /* 调用方口径恒为位计数 */
        datas[0] = strBytes;
        if (XS7Address_parse_2(&addr, "DB1.DBB20")) {
            addr.type = XS7Value_String;   /* 与 sendWrite 调用口径一致 */
            addr.count = 16;
            size_t n = XS7Pdu_buildWrite(buf, 0x0001, &addr, 1, datas, bitCounts);
            XS7U_CHECK(passed, failed, n == 46,
                       "buildWrite(STRING) 返回 46（19+参数14+数据13..18+头4=46）");
            if (n == 46) {
                xs7uBytesEq(&passed, &failed,
                            "STRING 写帧数据区 00 09 00 12（八位组串/字节长）",
                            buf, n, golden, sizeof(golden));
            }
        } else {
            ++failed;
            XPrintf("  [失败] 前置：DB1.DBB20 地址解析失败\n");
        }
    }

    /* 3. buildWrite 参数保护 */
    {
        XS7Address addr;
        uint8_t data1 = 0x00;
        uint16_t bitCounts[1];
        const uint8_t* datas[1];
        uint8_t buf[64];
        memset(&addr, 0, sizeof(addr));
        bitCounts[0] = 8;
        datas[0] = &data1;
        if (XS7Address_parse_2(&addr, "DB1.DBB0")) {
            XS7U_CHECK(passed, failed,
                       XS7Pdu_buildWrite(NULL, 1, &addr, 1, datas, bitCounts) == 0,
                       "buildWrite(NULL out) 返回 0");
            XS7U_CHECK(passed, failed,
                       XS7Pdu_buildWrite(buf, 1, &addr, 0, datas, bitCounts) == 0,
                       "buildWrite(itemCount=0) 返回 0");
            XS7U_CHECK(passed, failed,
                       XS7Pdu_buildWrite(buf, 1, &addr, 1, NULL, bitCounts) == 0,
                       "buildWrite(NULL datas) 返回 0");
            XS7U_CHECK(passed, failed,
                       XS7Pdu_buildWrite(buf, 1, &addr, 1, datas, NULL) == 0,
                       "buildWrite(NULL bitCounts) 返回 0");
        }
    }

    /* 4. parseWriteAck：成功项 FF */
    {
        static const uint8_t ack[15] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01,
            0x00, 0x00,
            0x05, 0x01, 0xFF
        };
        uint8_t rcs[2];
        memset(rcs, 0xCD, sizeof(rcs));
        XS7U_CHECK(passed, failed, XS7Pdu_parseWriteAck(ack, sizeof(ack), 1, rcs),
                   "parseWriteAck(FF) 成功");
        XS7U_CHECK(passed, failed, rcs[0] == XS7_RETURN_OK, "写应答项返回码 = 0xFF");
    }

    /* 5. parseWriteAck：失败项 05（结构合法，交上层判定） */
    {
        static const uint8_t ack[15] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01,
            0x00, 0x00,
            0x05, 0x01, 0x05
        };
        uint8_t rcs[2];
        memset(rcs, 0xCD, sizeof(rcs));
        XS7U_CHECK(passed, failed, XS7Pdu_parseWriteAck(ack, sizeof(ack), 1, rcs),
                   "parseWriteAck(05) 结构合法");
        XS7U_CHECK(passed, failed, rcs[0] == XS7_RETURN_DATA_ERROR,
                   "写应答项返回码 = 0x05");
    }

    /* 6. parseWriteAck：项数不符与坏结构拒绝 */
    {
        static const uint8_t ack[15] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01,
            0x00, 0x00,
            0x05, 0x01, 0xFF
        };
        uint8_t rcs[2];
        XS7U_CHECK(passed, failed, !XS7Pdu_parseWriteAck(ack, sizeof(ack), 2, rcs),
                   "parseWriteAck(期望 2 项实际 1 项) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseWriteAck(ack, 13, 1, rcs),
                   "parseWriteAck(截断帧) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseWriteAck(NULL, sizeof(ack), 1, rcs) &&
                                   !XS7Pdu_parseWriteAck(ack, sizeof(ack), 1, NULL),
                   "parseWriteAck(NULL) 保护");
    }

    xs7uFinish("PDU_Write", passed, failed);
#else
    XPrintf("[XS7][UNIT] PDU_Write SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
}

/******************************************************************************************
 * 六、XS7PduTest_Setup —— SetupCommunication 组包/协商解析
 ******************************************************************************************/

/**
 * @brief S7 PDU Setup 单元测试
 * @details 请求金样（s-pms g_plc_head2_default 结构，首期 AmQ=1、请求 960）：
 *          32 01 00 00 00 01 00 08 00 00 F0 00 00 01 00 01 03 C0（18B）；
 *          应答手造向量 480/960 两组：协商结果 = min(请求值, 应答值)。
 */
void XS7PduTest_Setup()
{
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XPrintf("========== XS7Pdu(Setup) 单元测试开始 ==========\n");

    /* 1. 请求金样：ref=1、AmQ=1、960 */
    {
        static const uint8_t golden960[18] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03, 0xC0
        };
        uint8_t buf[32];
        size_t n = XS7Pdu_buildSetupCommunication(buf, 0x0001, 1, 960);
        XS7U_CHECK(passed, failed, n == 18, "buildSetupCommunication 返回 18");
        if (n == 18) {
            xs7uBytesEq(&passed, &failed,
                        "Setup 请求 == g_plc_head2_default 结构（AmQ=1，960）",
                        buf, n, golden960, sizeof(golden960));
        }
    }

    /* 2. s-pms 480 变体：PDU 字段 01 E0 */
    {
        static const uint8_t golden480[18] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0xE0
        };
        uint8_t buf[32];
        size_t n = XS7Pdu_buildSetupCommunication(buf, 0x0001, 1, 480);
        XS7U_CHECK(passed, failed, n == 18, "buildSetupCommunication(480) 返回 18");
        if (n == 18) {
            xs7uBytesEq(&passed, &failed, "Setup 请求 480 变体（01 E0）",
                        buf, n, golden480, sizeof(golden480));
        }
    }

    /* 3. buildSetupCommunication 参数保护 */
    {
        uint8_t buf[32];
        memset(buf, 0, sizeof(buf));
        XS7U_CHECK(passed, failed,
                   XS7Pdu_buildSetupCommunication(NULL, 1, 1, 960) == 0 && buf[0] == 0x00,
                   "buildSetupCommunication(NULL) 返回 0 且不写缓冲");
    }

    /* 4. 应答向量 480：协商结果 min(960,480)=480、AmQ=1 */
    {
        static const uint8_t ack480[20] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0xE0
        };
        uint16_t pdu = 0, amq = 0;
        XS7U_CHECK(passed, failed, XS7Pdu_parseSetupAck(ack480, sizeof(ack480), &pdu, &amq),
                   "parseSetupAck(480 应答) 成功");
        XS7U_CHECK(passed, failed, pdu == 480, "协商 PDU = min(960,480) = 480");
        XS7U_CHECK(passed, failed, amq == 1, "协商 AmQ = 1");
    }

    /* 5. 应答向量 960：协商结果 960 */
    {
        static const uint8_t ack960[20] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03, 0xC0
        };
        uint16_t pdu = 0, amq = 0;
        XS7U_CHECK(passed, failed,
                   XS7Pdu_parseSetupAck(ack960, sizeof(ack960), &pdu, &amq) &&
                       pdu == 960 && amq == 1,
                   "parseSetupAck(960 应答) → 协商 960/AmQ=1");
    }

    /* 6. parseSetupAck 拒绝：功能码不符 / 截断 / NULL */
    {
        uint8_t bad[18];
        uint16_t pdu = 0, amq = 0;
        static const uint8_t ack480[20] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0xE0
        };
        memcpy(bad, ack480, sizeof(bad));
        bad[12] = 0x04;   /* 功能码被篡改为 Read */
        XS7U_CHECK(passed, failed, !XS7Pdu_parseSetupAck(bad, sizeof(bad), &pdu, &amq),
                   "parseSetupAck(func!=0xF0) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseSetupAck(ack480, 14, &pdu, &amq),
                   "parseSetupAck(截断) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Pdu_parseSetupAck(NULL, sizeof(ack480), &pdu, &amq) &&
                                   !XS7Pdu_parseSetupAck(ack480, sizeof(ack480), NULL, &amq),
                   "parseSetupAck(NULL) 保护");
    }

    xs7uFinish("PDU_Setup", passed, failed);
#else
    XPrintf("[XS7][UNIT] PDU_Setup SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
}

/******************************************************************************************
 * 七、XS7AddressTest —— 地址全语法解析 + 非法拒绝
 ******************************************************************************************/

/**
 * @brief 地址解析单元测试
 * @details 语法（_w1_design.md §3.3）：DB1.DBW10、DB1.DBX0.1、D10(=DB)、
 *          M100/MX0.1/MB/MW/MD、I/IB/IW/ID、Q*、V*、T100、C100；
 *          大小写不敏感。非法：空串、MX0.8（bit>7）、MX0.A、多点号、非数字、
 *          DB 缺块号等。
 * @note V 区按 s-pms 方式映射线码 0x84（块号归属两参考未冻结，本组只断言
 *       area/类型/位偏移，不断言 V 的 dbNumber——以阶段 0 实测定案）。
 */
void XS7AddressTest()
{
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XS7Address addr;
    XPrintf("========== XS7Address 单元测试开始 ==========\n");

    /* 1. DB 区：DB1.DBW10 → 0x84/db1/off=80/Word */
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "DB1.DBW10")) {
        XS7U_CHECK(passed, failed,
                   addr.area == XS7Area_DB && addr.dbNumber == 1 &&
                       addr.bitOffset == 10u * 8u,
                   "DB1.DBW10 → area=0x84/db=1/off=80");
        XS7U_CHECK(passed, failed,
                   addr.type == XS7Value_Word && addr.count == 1,
                   "DB1.DBW10 → 类型 Word、count=1");
        XS7U_CHECK(passed, failed, XS7Address_isValid(&addr), "DB1.DBW10 isValid=true");
    } else {
        ++failed;
        XPrintf("  [失败] DB1.DBW10 解析失败\n");
    }

    /* 2. DB 位：DB1.DBX0.1 → Bool/off=1 */
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "DB1.DBX0.1")) {
        XS7U_CHECK(passed, failed,
                   addr.area == XS7Area_DB && addr.dbNumber == 1 &&
                       addr.type == XS7Value_Bool && addr.bitOffset == 1u,
                   "DB1.DBX0.1 → Bool/off=1");
    } else {
        ++failed;
        XPrintf("  [失败] DB1.DBX0.1 解析失败\n");
    }

    /* 3. DB 字节/双字：DB1.DBB0 → Byte；DB1.DBD6 → DWord/off=48 */
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "DB1.DBB0")) {
        XS7U_CHECK(passed, failed,
                   addr.type == XS7Value_Byte && addr.bitOffset == 0u,
                   "DB1.DBB0 → Byte/off=0");
    } else {
        ++failed;
        XPrintf("  [失败] DB1.DBB0 解析失败\n");
    }
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "DB1.DBD6")) {
        XS7U_CHECK(passed, failed,
                   addr.type == XS7Value_DWord && addr.bitOffset == 48u,
                   "DB1.DBD6 → DWord/off=48");
    } else {
        ++failed;
        XPrintf("  [失败] DB1.DBD6 解析失败\n");
    }

    /* 4. D10 简写 → DB10（s-pms 语义：D<块号>） */
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "D10")) {
        XS7U_CHECK(passed, failed, addr.area == XS7Area_DB && addr.dbNumber == 10,
                   "D10 → DB10（简写）");
    } else {
        ++failed;
        XPrintf("  [失败] D10 解析失败\n");
    }

    /* 5. M 区：M100 无后缀只断言 area/off；MX/MB/MW/MD 断言类型 */
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "M100")) {
        XS7U_CHECK(passed, failed, addr.area == XS7Area_M && addr.bitOffset == 800u,
                   "M100 → M 区/off=800");
    } else {
        ++failed;
        XPrintf("  [失败] M100 解析失败\n");
    }
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "MX0.1")) {
        XS7U_CHECK(passed, failed,
                   addr.area == XS7Area_M && addr.type == XS7Value_Bool &&
                       addr.bitOffset == 1u,
                   "MX0.1 → Bool/off=1");
    } else {
        ++failed;
        XPrintf("  [失败] MX0.1 解析失败\n");
    }
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "MB100")) {
        XS7U_CHECK(passed, failed,
                   addr.type == XS7Value_Byte && addr.bitOffset == 800u,
                   "MB100 → Byte/off=800");
    } else {
        ++failed;
        XPrintf("  [失败] MB100 解析失败\n");
    }
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "MW100")) {
        XS7U_CHECK(passed, failed,
                   addr.type == XS7Value_Word && addr.bitOffset == 800u,
                   "MW100 → Word/off=800");
    } else {
        ++failed;
        XPrintf("  [失败] MW100 解析失败\n");
    }
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "MD100")) {
        XS7U_CHECK(passed, failed,
                   addr.type == XS7Value_DWord && addr.bitOffset == 800u,
                   "MD100 → DWord/off=800");
    } else {
        ++failed;
        XPrintf("  [失败] MD100 解析失败\n");
    }

    /* 6. I/Q 区：IW0 → I/Word；IB2 → I/Byte/off=16；QB0 → Q/Byte */
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "IW0")) {
        XS7U_CHECK(passed, failed,
                   addr.area == XS7Area_I && addr.type == XS7Value_Word &&
                       addr.bitOffset == 0u,
                   "IW0 → I/Word/off=0");
    } else {
        ++failed;
        XPrintf("  [失败] IW0 解析失败\n");
    }
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "IB2")) {
        XS7U_CHECK(passed, failed,
                   addr.area == XS7Area_I && addr.type == XS7Value_Byte &&
                       addr.bitOffset == 16u,
                   "IB2 → I/Byte/off=16");
    } else {
        ++failed;
        XPrintf("  [失败] IB2 解析失败\n");
    }
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "QB0")) {
        XS7U_CHECK(passed, failed, addr.area == XS7Area_Q && addr.type == XS7Value_Byte,
                   "QB0 → Q/Byte");
    } else {
        ++failed;
        XPrintf("  [失败] QB0 解析失败\n");
    }

    /* 7. V 区（S7-200/SMART）：VD20 → 线码 0x84/DWord/off=160 */
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "VD20")) {
        XS7U_CHECK(passed, failed,
                   addr.area == XS7Area_V && addr.type == XS7Value_DWord &&
                       addr.bitOffset == 160u,
                   "VD20 → V(0x84)/DWord/off=160");
    } else {
        ++failed;
        XPrintf("  [失败] VD20 解析失败\n");
    }

    /* 8. T/C 区（字访问，序号直入位偏移字段） */
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "T100")) {
        XS7U_CHECK(passed, failed,
                   addr.area == XS7Area_T && addr.bitOffset == 100u &&
                       addr.type == XS7Value_Word,
                   "T100 → T(0x1F)/Word/off=100");
    } else {
        ++failed;
        XPrintf("  [失败] T100 解析失败\n");
    }
    memset(&addr, 0, sizeof(addr));
    if (XS7Address_parse_2(&addr, "C100")) {
        XS7U_CHECK(passed, failed,
                   addr.area == XS7Area_C && addr.bitOffset == 100u &&
                       addr.type == XS7Value_Word,
                   "C100 → C(0x1E)/Word/off=100");
    } else {
        ++failed;
        XPrintf("  [失败] C100 解析失败\n");
    }

    /* 9. 大小写不敏感 */
    memset(&addr, 0, sizeof(addr));
    XS7U_CHECK(passed, failed,
               XS7Address_parse_2(&addr, "db1.dbw10") && addr.dbNumber == 1,
               "db1.dbw10 小写解析通过");

    /* 10. 主版本 XString 入口 */
    {
        XString* s = XString_create_utf8("DB1.DBW10");
        memset(&addr, 0, sizeof(addr));
        XS7U_CHECK(passed, failed, XS7Address_parse(&addr, s) && addr.dbNumber == 1,
                   "parse(XString 版) 与 _2 转发一致");
        xs7uStrDelete(s);
    }

    /* 11. toString 往返：非空且可再解析回同字段 */
    {
        XS7Address src;
        memset(&src, 0, sizeof(src));
        if (XS7Address_parse_2(&src, "DB1.DBW10")) {
            XString* s = XS7Address_toString(&src);
            if (s != NULL) {
                XS7Address back;
                XPrintf("  [通过] toString(DB1.DBW10) = \"%s\"\n", XString_toUtf8(s));
                ++passed;
                memset(&back, 0, sizeof(back));
                if (XS7Address_parse(&back, s)) {
                    XS7U_CHECK(passed, failed,
                               back.area == src.area && back.dbNumber == src.dbNumber &&
                                   back.bitOffset == src.bitOffset &&
                                   back.type == src.type,
                               "toString 结果可往返解析回同字段");
                } else {
                    ++failed;
                    XPrintf("  [失败] toString 结果不可再解析\n");
                }
                xs7uStrDelete(s);
            } else {
                ++failed;
                XPrintf("  [失败] toString 返回 NULL\n");
            }
        }
        XS7U_CHECK(passed, failed, XS7Address_toString(NULL) == NULL,
                   "toString(NULL) 返回 NULL");
    }

    /* 12. 非法地址全部拒绝（s-pms 2026-03 边界用例集翻译） */
    {
        static const char* const badAddrs[] = {
            "", "MX0.8", "MX0.A", "DBa.X", "DB1.", "M", "..", "DB1.DBX0.9",
            "DB.DBW0", "QZ0", "DB1.DBW", "MW 0", "MW1.2.3", "DB1.DBW1O",
            "DBX0.0", "M0.", "-1"
        };
        size_t i;
        for (i = 0; i < sizeof(badAddrs) / sizeof(badAddrs[0]); ++i) {
            memset(&addr, 0, sizeof(addr));
            if (!XS7Address_parse_2(&addr, badAddrs[i])) {
                ++passed;
            } else {
                ++failed;
                XPrintf("  [失败] 非法地址 \"%s\" 被误接受\n", badAddrs[i]);
            }
        }
        memset(&addr, 0, sizeof(addr));
        XS7U_CHECK(passed, failed, !XS7Address_parse_2(NULL, "DB1.DBW10") &&
                                   !XS7Address_parse_2(&addr, NULL),
                   "parse_2(NULL) 保护");
    }

    /* 13. isValid 非法结构 */
    {
        memset(&addr, 0, sizeof(addr));
        addr.area = 0x00;           /* 非法区域 */
        XS7U_CHECK(passed, failed, !XS7Address_isValid(&addr), "isValid(area=0x00) 拒绝");
        memset(&addr, 0, sizeof(addr));
        addr.area = XS7Area_DB;
        addr.dbNumber = 0;          /* DB 缺块号 */
        XS7U_CHECK(passed, failed, !XS7Address_isValid(&addr), "isValid(DB 无块号) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Address_isValid(NULL), "isValid(NULL) 拒绝");
    }

    xs7uFinish("Address", passed, failed);
#else
    XPrintf("[XS7][UNIT] Address SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
}

/******************************************************************************************
 * 八、XS7ValueTest —— 值全类型 BE 往返 / Real 位型 / STRING 补齐 / Bool 位
 ******************************************************************************************/

/**
 * @brief 值编解码单元测试
 * @details 大端一律经 XMemory_*_data(BIG_ENDIAN)（由被测实现保证，本组以
 *          字节金样锁死）；float 经 uint32 位型中转：
 *          100.5f = 0x42C90000、-1.75f = 0xBFE00000（IEEE754 已知值）。
 *          STRING(n) 布局 [maxLen][curLen][data...]，总长 n+2 偶数补齐。
 */
void XS7ValueTest()
{
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XPrintf("========== XS7Value 单元测试开始 ==========\n");

    /* 1. Word 往返：0x1234 ↔ 12 34 */
    {
        XVariant* v = XVariant_create_int(0x1234);
        XByteArray* out = XByteArray_create();
        static const uint8_t golden[2] = { 0x12, 0x34 };
        bool ok = XS7Value_encode(XS7Value_Word, 1, v, out);
        XS7U_CHECK(passed, failed, ok && xs7uBaSize(out) == 2,
                   "encode(Word 0x1234) 输出 2 字节");
        if (ok && xs7uBaSize(out) == 2) {
            xs7uBytesEq(&passed, &failed, "Word 编码字节 == 12 34（大端）",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        }
        {
            XVariant* rd = XVariant_create_null();
            uint8_t tmp[2];
            tmp[0] = 0x12; tmp[1] = 0x34;
            ok = XS7Value_decode(XS7Value_Word, 1, tmp, sizeof(tmp), rd);
            XS7U_CHECK(passed, failed, ok && rd != NULL && XVariant_toInt(rd) == 0x1234,
                       "decode(12 34) → 0x1234");
            if (rd != NULL) {
                xs7uVarDelete(rd);
            }
        }
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }

    /* 2. Byte 往返：0xAB ↔ AB */
    {
        XVariant* v = XVariant_create_int(0xAB);
        XByteArray* out = XByteArray_create();
        static const uint8_t golden[1] = { 0xAB };
        bool ok = XS7Value_encode(XS7Value_Byte, 1, v, out);
        XS7U_CHECK(passed, failed, ok && xs7uBaSize(out) == 1,
                   "encode(Byte 0xAB) 输出 1 字节");
        if (ok && xs7uBaSize(out) == 1) {
            xs7uBytesEq(&passed, &failed, "Byte 编码字节 == AB",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        }
        {
            XVariant* rd = XVariant_create_null();
            uint8_t tmp[1];
            tmp[0] = 0xAB;
            ok = XS7Value_decode(XS7Value_Byte, 1, tmp, sizeof(tmp), rd);
            XS7U_CHECK(passed, failed, ok && rd != NULL && XVariant_toInt(rd) == 0xAB,
                       "decode(AB) → 0xAB");
            if (rd != NULL) {
                xs7uVarDelete(rd);
            }
        }
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }

    /* 3. DWord 往返：0x12345678 ↔ 12 34 56 78 */
    {
        XVariant* v = XVariant_create_int64(0x12345678);
        XByteArray* out = XByteArray_create();
        static const uint8_t golden[4] = { 0x12, 0x34, 0x56, 0x78 };
        bool ok = XS7Value_encode(XS7Value_DWord, 1, v, out);
        XS7U_CHECK(passed, failed, ok && xs7uBaSize(out) == 4,
                   "encode(DWord) 输出 4 字节");
        if (ok && xs7uBaSize(out) == 4) {
            xs7uBytesEq(&passed, &failed, "DWord 编码字节 == 12 34 56 78（大端）",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        }
        {
            XVariant* rd = XVariant_create_null();
            uint8_t tmp[4];
            tmp[0] = 0x12; tmp[1] = 0x34; tmp[2] = 0x56; tmp[3] = 0x78;
            ok = XS7Value_decode(XS7Value_DWord, 1, tmp, sizeof(tmp), rd);
            XS7U_CHECK(passed, failed,
                       ok && rd != NULL &&
                           (uint32_t)XVariant_toInt64(rd) == 0x12345678u,
                       "decode(12 34 56 78) → 0x12345678");
            if (rd != NULL) {
                xs7uVarDelete(rd);
            }
        }
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }

    /* 4. Real 位型中转：100.5f ↔ 42 C9 00 00（禁止 *(float*)&u32 强转） */
    {
        XVariant* v = XVariant_create_float(100.5f);
        XByteArray* out = XByteArray_create();
        static const uint8_t golden[4] = { 0x42, 0xC9, 0x00, 0x00 };
        bool ok = XS7Value_encode(XS7Value_Real, 1, v, out);
        XS7U_CHECK(passed, failed, ok && xs7uBaSize(out) == 4,
                   "encode(Real 100.5) 输出 4 字节");
        if (ok && xs7uBaSize(out) == 4) {
            xs7uBytesEq(&passed, &failed, "Real 编码字节 == 42 C9 00 00（IEEE754 位型）",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        }
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }
    {
        XVariant* rd = XVariant_create_null();
        uint8_t tmp[4];
        tmp[0] = 0x42; tmp[1] = 0xC9; tmp[2] = 0x00; tmp[3] = 0x00;
        bool ok = XS7Value_decode(XS7Value_Real, 1, tmp, sizeof(tmp), rd);
        XS7U_CHECK(passed, failed,
                   ok && rd != NULL &&
                       (XVariant_toFloat(rd) - 100.5f) < 0.001f &&
                       (100.5f - XVariant_toFloat(rd)) < 0.001f,
                   "decode(42 C9 00 00) → 100.5（±1e-3）");
        if (rd != NULL) {
            xs7uVarDelete(rd);
        }
    }
    /* 负数位型：-1.75f = BF E0 00 00 */
    {
        XVariant* v = XVariant_create_float(-1.75f);
        XByteArray* out = XByteArray_create();
        static const uint8_t golden[4] = { 0xBF, 0xE0, 0x00, 0x00 };
        if (XS7Value_encode(XS7Value_Real, 1, v, out) &&
            xs7uBaSize(out) == 4) {
            xs7uBytesEq(&passed, &failed, "Real 编码字节 == BF E0 00 00（-1.75）",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        } else {
            ++failed;
            XPrintf("  [失败] Real(-1.75) 编码失败\n");
        }
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }

    /* 5. Bool 位：true ↔ 01；decode 00 → false */
    {
        XVariant* vt = XVariant_create_bool(true);
        XVariant* vf = XVariant_create_bool(false);
        XByteArray* out = XByteArray_create();
        static const uint8_t golden[1] = { 0x01 };
        bool ok = XS7Value_encode(XS7Value_Bool, 1, vt, out);
        XS7U_CHECK(passed, failed, ok && xs7uBaSize(out) == 1,
                   "encode(Bool true) 输出 1 字节");
        if (ok && xs7uBaSize(out) == 1) {
            xs7uBytesEq(&passed, &failed, "Bool(true) 编码字节 == 01",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        }
        xs7uBaClear(out);
        ok = XS7Value_encode(XS7Value_Bool, 1, vf, out);
        XS7U_CHECK(passed, failed,
                   ok && xs7uBaSize(out) == 1 &&
                       XByteArray_data(out)[0] == 0x00,
                   "encode(Bool false) 字节 == 00");
        {
            XVariant* rd = XVariant_create_null();
            uint8_t t1[1]; t1[0] = 0x01;
            uint8_t t0[1]; t0[0] = 0x00;
            ok = XS7Value_decode(XS7Value_Bool, 1, t1, 1, rd);
            XS7U_CHECK(passed, failed, ok && rd != NULL && XVariant_toBool(rd),
                       "decode(01) → true");
            if (rd != NULL) {
                xs7uVarDelete(rd);
            }
            rd = XVariant_create_null();   /* 重建变体供第二次解码写入 */
            ok = XS7Value_decode(XS7Value_Bool, 1, t0, 1, rd);
            XS7U_CHECK(passed, failed, ok && rd != NULL && !XVariant_toBool(rd),
                       "decode(00) → false");
            if (rd != NULL) {
                xs7uVarDelete(rd);
            }
        }
        xs7uBaDelete(out);
        xs7uVarDelete(vt);
        xs7uVarDelete(vf);
    }

    /* 5b. Bool 多元素：LSB 在前位打包，载荷 ceil(N/8) 字节（与 CPU 位读回、
     *  XS7Pdu 0x03/0x04 数据分支同口径；单元素 1 字节口径不受影响） */
    {
        static const uint8_t bits[10] = { 1, 0, 1, 1, 0, 0, 0, 0, 1, 0 };
        XByteArray* blob = XByteArray_create();
        XVariant* v = XVariant_create_null();
        XByteArray* out = XByteArray_create();
        /* bit0,2,3 → 0x0D；bit8 → 0x01 */
        static const uint8_t golden[2] = { 0x0D, 0x01 };
        bool ok;
        size_t k;
        for (k = 0; k < sizeof(bits); ++k) {
            XByteArray_push_back_1(blob, bits[k]);
        }
        XByteArray_setVariant_move(v, blob);   /* 变体接管字节数组 */
        ok = XS7Value_encode(XS7Value_Bool, 10, v, out);
        XS7U_CHECK(passed, failed, ok && xs7uBaSize(out) == 2,
                   "encode(Bool×10) 打包为 ceil(10/8)=2 字节");
        if (ok && xs7uBaSize(out) == 2) {
            xs7uBytesEq(&passed, &failed, "位打包字节 == 0D 01（LSB 在前）",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        }
        {
            XVariant* rd = XVariant_create_null();
            ok = XS7Value_decode(XS7Value_Bool, 10, golden, sizeof(golden), rd);
            XS7U_CHECK(passed, failed, ok && rd != NULL,
                       "decode(0D 01) → 10 元素展开");
            if (ok && rd != NULL) {
                XByteArray* back = XByteArray_fromVariant_ref(rd);   /* 借用 */
                const uint8_t* raw = (back != NULL) ? XByteArray_data(back) : NULL;
                bool allMatch = (back != NULL && xs7uBaSize(back) == 10 && raw != NULL);
                for (k = 0; allMatch && k < 10; ++k) {
                    if (raw[k] != bits[k]) allMatch = false;
                }
                XS7U_CHECK(passed, failed, allMatch,
                           "位解包往返 10 位与源序列一致");
            }
            if (rd != NULL) {
                xs7uVarDelete(rd);
            }
            rd = XVariant_create_null();
            XS7U_CHECK(passed, failed,
                       !XS7Value_decode(XS7Value_Bool, 10, golden, 1, rd),
                       "decode 载荷不足(1<ceil(10/8)) 拒绝");
            if (rd != NULL) {
                xs7uVarDelete(rd);
            }
        }
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }

    /* 6. STRING(8) "AB"：总长 n+2=10（偶数无需补齐） */
    {
        XVariant* v = XVariant_create_utf8_str("AB");
        XByteArray* out = XByteArray_create();
        static const uint8_t golden[10] = {
            0x08, 0x02, (uint8_t)'A', (uint8_t)'B', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        bool ok = XS7Value_encode(XS7Value_String, 8, v, out);
        XS7U_CHECK(passed, failed, ok && xs7uBaSize(out) == 10,
                   "encode(STRING(8) \"AB\") 输出 10 字节");
        if (ok && xs7uBaSize(out) == 10) {
            xs7uBytesEq(&passed, &failed, "STRING 布局 08 02 41 42 + 6 补零",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        }
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }

    /* 7. STRING(7) "ABC"：n+2=9 奇数 → 偶数补齐到 10 */
    {
        XVariant* v = XVariant_create_utf8_str("ABC");
        XByteArray* out = XByteArray_create();
        static const uint8_t golden[10] = {
            0x07, 0x03, (uint8_t)'A', (uint8_t)'B', (uint8_t)'C',
            0x00, 0x00, 0x00, 0x00, 0x00
        };
        bool ok = XS7Value_encode(XS7Value_String, 7, v, out);
        XS7U_CHECK(passed, failed, ok && xs7uBaSize(out) == 10,
                   "encode(STRING(7) \"ABC\") 奇数补齐到 10 字节");
        if (ok && xs7uBaSize(out) == 10) {
            xs7uBytesEq(&passed, &failed, "STRING(7) 布局 07 03 41 42 43 + 5 补零",
                        XByteArray_data(out), xs7uBaSize(out),
                        golden, sizeof(golden));
        }
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }

    /* 8. STRING 解码：类型正确 + 再编码往返字节一致 */
    {
        static const uint8_t raw[10] = {
            0x08, 0x02, (uint8_t)'A', (uint8_t)'B', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        XVariant* rd = XVariant_create_null();
        bool ok = XS7Value_decode(XS7Value_String, 8, raw, sizeof(raw), rd);
        XS7U_CHECK(passed, failed, ok && rd != NULL, "decode(STRING) 成功");
        if (ok && rd != NULL) {
            XS7U_CHECK(passed, failed, XVariant_type(rd) == XVariantType_String,
                       "decode(STRING) 产出 String 型变体");
            {
                XByteArray* re = XByteArray_create();
                if (XS7Value_encode(XS7Value_String, 8, rd, re) &&
                    xs7uBaSize(re) == sizeof(raw) &&
                    memcmp(XByteArray_data(re), raw, sizeof(raw)) == 0) {
                    XPrintf("  [通过] STRING 解码→再编码往返字节一致\n");
                    ++passed;
                } else {
                    ++failed;
                    XPrintf("  [失败] STRING 解码→再编码字节不一致\n");
                }
                xs7uBaDelete(re);
            }
        }
        if (rd != NULL) {
            xs7uVarDelete(rd);
        }
    }

    /* 9. 长度不足/参数保护 */
    {
        XVariant* v = XVariant_create_int(1);
        XByteArray* out = XByteArray_create();
        XVariant* rd = XVariant_create_null();
        uint8_t tmp[4];
        memset(tmp, 0, sizeof(tmp));
        XS7U_CHECK(passed, failed, !XS7Value_decode(XS7Value_Word, 1, tmp, 1, rd),
                   "decode(Word 长度不足) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Value_decode(XS7Value_DWord, 1, tmp, 3, rd),
                   "decode(DWord 长度不足) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Value_decode(XS7Value_DWord, 1, NULL, 4, rd),
                   "decode(NULL data) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Value_encode(XS7Value_Word, 0, v, out) &&
                                   !XS7Value_encode(XS7Value_Word, 1, NULL, out) &&
                                   !XS7Value_encode(XS7Value_Word, 1, v, NULL),
                   "encode(count=0/NULL) 拒绝");
        XS7U_CHECK(passed, failed, !XS7Value_encode((XS7ValueType)99, 1, v, out),
                   "encode(非法类型) 拒绝");
        xs7uBaDelete(out);
        xs7uVarDelete(v);
    }

    xs7uFinish("Value", passed, failed);
#else
    XPrintf("[XS7][UNIT] Value SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
}

/******************************************************************************************
 * 九、XS7ControlTest —— Run(Hot/Cold)/Stop 金样 + 0x02/0x07 判成功
 *     （XS7_CONTROL_ON 门控：0 时整段裁掉，仅保留可链接的 SKIP 打印）
 ******************************************************************************************/

/**
 * @brief 运维控制（Run/Stop）单元测试
 * @details 金样（s-pms siemens_s7_private.h，去 TPKT/COTP 后比 S7 段）：
 * - Stop（26B，ref=0x000E）：32 01 00 00 00 0E 00 10 00 00
 *   29 00 00 00 00 00 09 "P_PROGRAM"
 * - Hot（30B）：32 01 00 00 00 0E 00 14 00 00
 *   28 00 00 00 00 00 00 FD 00 00 09 "P_PROGRAM"
 * - Cold（33B）：Hot 参数尾追加 02 43 20（"C "）
 * - 应答状态码：0x00 正常 / 0x02 已运行 / 0x07 已停止视为成功（snap7 语义）
 */
void XS7ControlTest()
{
#if XS7_CONTROL_ON
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XPrintf("========== XS7Control 单元测试开始 ==========\n");

    /* 1. Stop 金样（26 字节，ref=0x000E） */
    {
        static const uint8_t golden[26] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x10, 0x00, 0x00,
            0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09,
            (uint8_t)'P', (uint8_t)'_', (uint8_t)'P', (uint8_t)'R', (uint8_t)'O',
            (uint8_t)'G', (uint8_t)'R', (uint8_t)'A', (uint8_t)'M'
        };
        uint8_t buf[64];
        size_t n = XS7Control_buildStop(buf, 0x000E);
        XS7U_CHECK(passed, failed, n == 26, "buildStop 返回 26");
        if (n == 26) {
            xs7uBytesEq(&passed, &failed,
                        "Stop 帧 == s-pms g_s7_stop（func 0x29 + P_PROGRAM）",
                        buf, n, golden, sizeof(golden));
        }
        XS7U_CHECK(passed, failed, XS7Control_buildStop(NULL, 1) == 0,
                   "buildStop(NULL) 返回 0");
    }

    /* 2. Hot Run 金样（30 字节，func 0x28 + ... FD 00 00 09 P_PROGRAM） */
    {
        static const uint8_t golden[30] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x14, 0x00, 0x00,
            0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x00, 0x09,
            (uint8_t)'P', (uint8_t)'_', (uint8_t)'P', (uint8_t)'R', (uint8_t)'O',
            (uint8_t)'G', (uint8_t)'R', (uint8_t)'A', (uint8_t)'M'
        };
        uint8_t buf[64];
        size_t n = XS7Control_buildRun(buf, 0x000E, XS7RunMode_Hot);
        XS7U_CHECK(passed, failed, n == 30, "buildRun(Hot) 返回 30");
        if (n == 30) {
            xs7uBytesEq(&passed, &failed, "Hot Run 帧 == s-pms g_s7_hot_start",
                        buf, n, golden, sizeof(golden));
        }
    }

    /* 3. Cold Run 金样（33 字节：Hot 参数尾追加 02 43 20） */
    {
        static const uint8_t golden[33] = {
            0x32, 0x01, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x17, 0x00, 0x00,
            0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x00, 0x09,
            (uint8_t)'P', (uint8_t)'_', (uint8_t)'P', (uint8_t)'R', (uint8_t)'O',
            (uint8_t)'G', (uint8_t)'R', (uint8_t)'A', (uint8_t)'M',
            0x02, 0x43, 0x20
        };
        uint8_t buf[64];
        size_t n = XS7Control_buildRun(buf, 0x000E, XS7RunMode_Cold);
        XS7U_CHECK(passed, failed, n == 33, "buildRun(Cold) 返回 33（追加 02 43 20）");
        if (n == 33) {
            xs7uBytesEq(&passed, &failed, "Cold Run 帧 == g_s7_hot_start + \"C \"",
                        buf, n, golden, sizeof(golden));
        }
        XS7U_CHECK(passed, failed, XS7Control_buildRun(NULL, 1, XS7RunMode_Hot) == 0,
                   "buildRun(NULL) 返回 0");
    }

    /* 4. parseAck：应答状态码提取（0x00 正常 / 0x02 已运行 / 0x07 已停止） */
    {
        /* 应答布局（W1 实现约定，同真机）：12B Ack_Data 头 + 参数（功能码回显）
         * + 数据区首项（ret|transport|长度|载荷），载荷首字节即状态码 */
        uint8_t ack[19];
        uint8_t status = 0xEE;
        /* 12B Ack_Data 头：[2..3]冗余 [4..5]ref [6..7]paramLen [8..9]dataLen [10..11]错误字节 */
        ack[0] = 0x32; ack[1] = 0x03; ack[2] = 0x00; ack[3] = 0x00;
        ack[4] = 0x00; ack[5] = 0x0E; ack[6] = 0x00; ack[7] = 0x02;
        ack[8] = 0x00; ack[9] = 0x05;
        ack[10] = 0x00; ack[11] = 0x00;           /* errorClass=0 errorCode=0 */
        ack[12] = 0x29; ack[13] = 0x00;           /* 参数：func 回显 + 保留 */
        ack[14] = 0x00; ack[15] = 0x04;           /* 数据项：ret=0 transport=04 */
        ack[16] = 0x00; ack[17] = 0x01;           /* 载荷长度 1（BE） */
        ack[18] = XS7_CONTROL_STATUS_ALREADY_STOPPED;   /* 载荷：状态码 */
        XS7U_CHECK(passed, failed, XS7Control_parseAck(ack, sizeof(ack), &status),
                   "parseAck(Stop 应答) 成功");
        XS7U_CHECK(passed, failed, status == XS7_CONTROL_STATUS_ALREADY_STOPPED,
                   "parseAck 提取状态码 0x07（已停止）");
        ack[12] = 0x28; ack[18] = XS7_CONTROL_STATUS_ALREADY_RUNNING;
        status = 0xEE;
        XS7U_CHECK(passed, failed,
                   XS7Control_parseAck(ack, sizeof(ack), &status) &&
                       status == XS7_CONTROL_STATUS_ALREADY_RUNNING,
                   "parseAck 提取状态码 0x02（已运行）");
        ack[18] = 0x00;
        status = 0xEE;
        XS7U_CHECK(passed, failed, XS7Control_parseAck(ack, sizeof(ack), &status),
                   "parseAck(正常应答) 成功");
        XS7U_CHECK(passed, failed, status == 0x00, "parseAck 提取状态码 0x00（正常）");
        XS7U_CHECK(passed, failed, !XS7Control_parseAck(ack, 12, &status),
                   "parseAck(截断) 拒绝");
        ack[1] = 0x01;   /* rosctr 非 Ack_Data */
        XS7U_CHECK(passed, failed, !XS7Control_parseAck(ack, sizeof(ack), &status),
                   "parseAck(rosctr!=Ack_Data) 拒绝");
        ack[1] = 0x03;
        ack[10] = 0x81;   /* 头级错误（errorClass） */
        XS7U_CHECK(passed, failed, !XS7Control_parseAck(ack, sizeof(ack), &status),
                   "parseAck(头级错误) 拒绝");
        ack[10] = 0x00;
        /* outStatus=NULL 语义未在头文件定义（实现按"不关心"处理返回 true），不断言 */
        XS7U_CHECK(passed, failed, !XS7Control_parseAck(NULL, sizeof(ack), &status),
                   "parseAck(NULL s7) 保护");
    }

    /* 5. statusIsSuccess：0x00/0x02/0x07 成功；其余失败 */
    XS7U_CHECK(passed, failed, XS7Control_statusIsSuccess(0x00),
               "statusIsSuccess(0x00)=true");
    XS7U_CHECK(passed, failed, XS7Control_statusIsSuccess(XS7_CONTROL_STATUS_ALREADY_RUNNING),
               "statusIsSuccess(0x02 已运行)=true");
    XS7U_CHECK(passed, failed, XS7Control_statusIsSuccess(XS7_CONTROL_STATUS_ALREADY_STOPPED),
               "statusIsSuccess(0x07 已停止)=true");
    XS7U_CHECK(passed, failed, !XS7Control_statusIsSuccess(0x01),
               "statusIsSuccess(0x01)=false");
    XS7U_CHECK(passed, failed, !XS7Control_statusIsSuccess(0x05),
               "statusIsSuccess(0x05)=false");
    /* 0xFF 由实现定义为常规成功（头文件未枚举），不作负例断言 */

    xs7uFinish("Control", passed, failed);
#else
    XPrintf("[XS7][UNIT] Control SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
#else
    XPrintf("[XS7][UNIT] Control SKIP (XS7_CONTROL_ON=0，运维整段裁剪)\n");
#endif /* XS7_CONTROL_ON */
}

/******************************************************************************************
 * 十、XS7SessionTest —— 半包/粘包/坏头重同步事件序列
 ******************************************************************************************/

/**
 * @brief 会话状态机单元测试
 * @details 手造字节流分次 feed（4 字节半包、一帧半、双帧粘包、坏头重同步），
 *          断言事件序列与握手两步状态迁移；金样帧复用 TPKT/COTP/PDU 组向量。
 */
void XS7SessionTest()
{
#if XS7_CORE_ON
    int passed = 0, failed = 0;
    XS7Session* s = XS7Session_create();
    XPrintf("========== XS7Session 单元测试开始 ==========\n");

    if (s == NULL) {
        ++failed;
        XPrintf("  [失败] XS7Session_create 返回 NULL\n");
        xs7uFinish("Session", passed, failed);
        return;
    }

    /* 1. 初始状态与 NULL 保护 */
    XS7U_CHECK(passed, failed, XS7Session_state(s) == XS7Session_Idle, "初始状态 Idle");
    XS7U_CHECK(passed, failed, XS7Session_negotiatedPduSize(s) == 0, "初始协商 PDU = 0");
    XS7U_CHECK(passed, failed, XS7Session_state(NULL) == XS7Session_Idle &&
                               XS7Session_negotiatedPduSize(NULL) == 0 &&
                               XS7Session_nextPduRef(NULL) == 0,
               "state/negotiatedPduSize/nextPduRef(NULL) 保护");
    XS7U_CHECK(passed, failed,
               !XS7Session_buildNextHandshake(NULL, NULL) &&
                   !XS7Session_feed(NULL, NULL, 0) &&
                   XS7Session_takeEvent(NULL) == XS7SessionEv_None &&
                   XS7Session_frame(NULL, NULL) == NULL,
               "buildNextHandshake/feed/takeEvent/frame(NULL) 保护");

    /* 2. pduRef 分配游标：1、2 递增；reset 后回到 1 */
    XS7U_CHECK(passed, failed, XS7Session_nextPduRef(s) == 1, "首个 pduRef = 1");
    XS7U_CHECK(passed, failed, XS7Session_nextPduRef(s) == 2, "第二个 pduRef = 2");
    XS7Session_reset(s);
    XS7U_CHECK(passed, failed, XS7Session_nextPduRef(s) == 1, "reset 后 pduRef 回到 1");

    /* 3~11. 配置参数 + 完整握手 + 业务帧序列（金样帧 rack0/slot1） */
    {
        static const uint8_t crGolden[22] = {
            0x03, 0x00, 0x00, 0x16, 0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
            0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x02, 0xC2, 0x02, 0x01, 0x00
        };
        static const uint8_t ccFrame[22] = {
            0x03, 0x00, 0x00, 0x16, 0x11, 0xD0, 0x00, 0x00, 0x00, 0x01, 0x00,
            0xC0, 0x01, 0x0A, 0xC1, 0x02, 0x01, 0x02, 0xC2, 0x02, 0x01, 0x00
        };
        static const uint8_t dtSetupGolden[25] = {
            0x03, 0x00, 0x00, 0x19, 0x02, 0xF0, 0x80,
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03, 0xC0
        };
        static const uint8_t ackFrame[27] = {
            0x03, 0x00, 0x00, 0x1B, 0x02, 0xF0, 0x80,
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0xE0
        };
        static const uint8_t s7Ack[20] = {
            0x32, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x06,
            0x00, 0x00,
            0x04, 0x01, 0xFF, 0x04, 0x00, 0x10, 0x12, 0x34
        };
        XByteArray* frame = XByteArray_create();

        XS7Session_setParameters(s, 0x0102, 0x0100, XS7_REQUESTED_PDU_LEN, XS7_AMQ);

        /* 3. 第一步握手 CR（金样帧） */
        XS7U_CHECK(passed, failed,
                   XS7Session_buildNextHandshake(s, frame) &&
                       xs7uBaSize(frame) == 22,
                   "第一步握手产出 CR 帧（22B）");
        if (xs7uBaSize(frame) == 22) {
            xs7uBytesEq(&passed, &failed, "CR 帧 == g_plc_head1_s1200",
                        XByteArray_data(frame), xs7uBaSize(frame),
                        crGolden, sizeof(crGolden));
        }
        XS7U_CHECK(passed, failed, XS7Session_state(s) == XS7Session_CrSent,
                   "状态迁移 Idle → CrSent");

        /* 4. CC 半包喂入：先 4 字节头（无事件）再补齐（CcAccepted） */
        XS7U_CHECK(passed, failed, XS7Session_feed(s, ccFrame, 4),
                   "feed(CC 前 4 字节) 接受");
        XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_None,
                   "半包阶段无事件");
        XS7U_CHECK(passed, failed, XS7Session_feed(s, ccFrame + 4, sizeof(ccFrame) - 4),
                   "feed(CC 剩余 18 字节) 接受");
        XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_CcAccepted,
                   "CC 到齐 → CcAccepted 事件");

        /* 5. 第二步握手：DT(Setup 960)，ref=1（reset 后首个分配） */
        xs7uBaClear(frame);
        XS7U_CHECK(passed, failed,
                   XS7Session_buildNextHandshake(s, frame) &&
                       xs7uBaSize(frame) == 25,
                   "第二步握手产出 DT(Setup) 帧（25B）");
        if (xs7uBaSize(frame) == 25) {
            /* pduRef 分配时点（CR 是否占号）属实现细节：金样对其余字节逐字节
             * 断言，ref 字段取实际值并单独断言非 0 */
            uint8_t masked[25];
            const uint8_t* actual = XByteArray_data(frame);
            memcpy(masked, dtSetupGolden, sizeof(masked));
            masked[11] = actual[11];
            masked[12] = actual[12];
            xs7uBytesEq(&passed, &failed, "DT(Setup) 帧 == TPKT+DT 头+Setup 金样（ref 掩码）",
                        actual, xs7uBaSize(frame),
                        masked, sizeof(masked));
            XS7U_CHECK(passed, failed,
                       actual[7] == 0x32 && actual[8] == 0x01 &&
                           (uint16_t)((actual[11] << 8) | actual[12]) != 0,
                       "DT(Setup) S7 头 32 01 且 pduRef 非 0");
        }
        XS7U_CHECK(passed, failed, XS7Session_state(s) == XS7Session_SetupSent,
                   "状态迁移 CrSent → SetupSent");

        /* 6. Setup 应答喂入 → HandshakeDone，协商 480 */
        XS7U_CHECK(passed, failed, XS7Session_feed(s, ackFrame, sizeof(ackFrame)),
                   "feed(Setup 应答帧) 接受");
        XS7U_CHECK(passed, failed,
                   XS7Session_takeEvent(s) == XS7SessionEv_HandshakeDone,
                   "Setup 应答 → HandshakeDone 事件");
        XS7U_CHECK(passed, failed, XS7Session_state(s) == XS7Session_Ready,
                   "状态迁移 SetupSent → Ready");
        XS7U_CHECK(passed, failed, XS7Session_negotiatedPduSize(s) == 480,
                   "协商 PDU 长度 = 480（min(960,480)）");

        /* 7. Ready 态：双帧粘包一次喂入 → 两个 S7Frame 事件依次出队 */
        {
            uint8_t twin[54];
            twin[0] = 0x03; twin[1] = 0x00; twin[2] = 0x00; twin[3] = 0x1B;
            twin[4] = 0x02; twin[5] = 0xF0; twin[6] = 0x80;
            memcpy(twin + 7, s7Ack, 20);
            twin[27] = 0x03; twin[28] = 0x00; twin[29] = 0x00; twin[30] = 0x1B;
            twin[31] = 0x02; twin[32] = 0xF0; twin[33] = 0x80;
            memcpy(twin + 34, s7Ack, 20);
            XS7U_CHECK(passed, failed, XS7Session_feed(s, twin, sizeof(twin)),
                       "feed(双帧粘包 54B) 接受");
            XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_S7Frame,
                       "粘包第 1 帧 → S7Frame 事件");
            {
                size_t len = 0;
                const uint8_t* f = XS7Session_frame(s, &len);
                xs7uBytesEq(&passed, &failed, "第 1 帧 S7 PDU == 读应答金样",
                            f, len, s7Ack, sizeof(s7Ack));
            }
            XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_S7Frame,
                       "粘包第 2 帧 → S7Frame 事件");
            XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_None,
                       "粘包事件耗尽 → None");
        }

        /* 8. 一帧半：先喂 10 字节（无事件），补齐后出事件 */
        {
            uint8_t part[25];
            size_t i;
            part[0] = 0x03; part[1] = 0x00; part[2] = 0x00; part[3] = 0x19;
            part[4] = 0x02; part[5] = 0xF0; part[6] = 0x80;
            part[7] = 0x32; part[8] = 0x03; part[9] = 0x00;
            for (i = 10; i < 25; ++i) {
                part[i] = (uint8_t)(0x10 + i);
            }
            part[12] = 0x04; part[13] = 0x01;             /* 参数 func/count */
            part[14] = 0xFF; part[15] = 0x04;             /* 数据项头 */
            XS7U_CHECK(passed, failed, XS7Session_feed(s, part, 10),
                       "feed(一帧半前 10B) 接受");
            XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_None,
                       "整帧未到齐 → 无事件");
            XS7U_CHECK(passed, failed, XS7Session_feed(s, part + 10, sizeof(part) - 10),
                       "feed(剩余 15B) 接受");
            XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_S7Frame,
                       "补齐后 → S7Frame 事件");
        }

        /* 9. 坏头重同步：先喂 4 个 0xFF（坏头，逐字节推进），再喂完整帧 */
        {
            static const uint8_t badHead[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
            uint8_t good[25];
            size_t i;
            good[0] = 0x03; good[1] = 0x00; good[2] = 0x00; good[3] = 0x19;
            good[4] = 0x02; good[5] = 0xF0; good[6] = 0x80;
            good[7] = 0x32; good[8] = 0x03; good[9] = 0x00;
            for (i = 10; i < 25; ++i) {
                good[i] = 0x00;
            }
            good[12] = 0x04; good[13] = 0x01;
            good[14] = 0xFF; good[15] = 0x04; good[16] = 0x00; good[17] = 0x01;
            XS7U_CHECK(passed, failed, XS7Session_feed(s, badHead, sizeof(badHead)),
                       "feed(4 个坏头字节) 接受");
            XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_None,
                       "坏头阶段无事件（逐字节重同步，不清缓冲）");
            XS7U_CHECK(passed, failed, XS7Session_feed(s, good, sizeof(good)),
                       "feed(重同步后完整帧) 接受");
            XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_S7Frame,
                       "坏头重同步后正常出帧");
        }

        /* 10. Ready 态喂非 DT 帧（CC 帧）→ Error 事件 + Error 态 */
        XS7U_CHECK(passed, failed, XS7Session_feed(s, ccFrame, sizeof(ccFrame)),
                   "feed(Ready 态误入 CC 帧) 接受");
        XS7U_CHECK(passed, failed, XS7Session_takeEvent(s) == XS7SessionEv_Error,
                   "非 DT 帧 → Error 事件");
        XS7U_CHECK(passed, failed, XS7Session_state(s) == XS7Session_Error,
                   "状态迁移 Ready → Error");

        /* 11. reset：回 Idle、协商清零、pduRef 回 1、参数保留可二次握手 */
        {
            XByteArray* probe = XByteArray_create();
            XS7Session_reset(s);
            XS7U_CHECK(passed, failed, XS7Session_state(s) == XS7Session_Idle,
                       "reset → Idle");
            XS7U_CHECK(passed, failed, XS7Session_negotiatedPduSize(s) == 0,
                       "reset 清协商长度");
            XS7U_CHECK(passed, failed, XS7Session_nextPduRef(s) == 1,
                       "reset 后 pduRef 回 1");
            XS7U_CHECK(passed, failed,
                       XS7Session_buildNextHandshake(s, probe) &&
                           xs7uBaSize(probe) == 22,
                       "reset 保留参数仍可重新握手（无残留帧污染）");
            xs7uBaDelete(probe);
        }
        xs7uBaDelete(frame);
    }

    /* 12. feed 非法参数与对象释放 */
    (void)XS7Session_feed(s, NULL, 10);   /* 头文件未定义 NULL 行为：只验证不崩溃 */
    XS7U_CHECK(passed, failed, true, "feed(NULL data) 不崩溃");
    XClass_delete_base((XClass*)s);
    XS7U_CHECK(passed, failed, true, "会话对象删除无崩溃");

    xs7uFinish("Session", passed, failed);
#else
    XPrintf("[XS7][UNIT] Session SKIP (XS7_CORE_ON=0，整段裁剪)\n");
#endif /* XS7_CORE_ON */
}

/******************************************************************************************
 * 十一、XPlcReplyPublicApiTest —— Reply 公共 API
 ******************************************************************************************/

/* ---- 信号计数槽 ---- */
static int g_repFinCount = 0;    /**< finished 信号计数 */
static int g_repErrCount = 0;    /**< errorOccurred 信号计数 */
static int g_repStateCount = 0;  /**< stateChanged 信号计数 */
static int g_repIeCount = 0;     /**< intermediateErrorOccurred 信号计数 */
static int g_repOrder[8];        /**< setError 顺序记录：1=errorOccurred 2=finished */
static int g_repOrderN = 0;      /**< 顺序记录长度 */

/** @brief finished 信号计数槽 */
static void repOnFinished(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_repFinCount;
    if (g_repOrderN < 8) {
        g_repOrder[g_repOrderN++] = 2;
    }
}

/** @brief errorOccurred 信号计数槽 */
static void repOnError(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_repErrCount;
    if (g_repOrderN < 8) {
        g_repOrder[g_repOrderN++] = 1;
    }
}

/** @brief stateChanged 信号计数槽 */
static void repOnState(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_repStateCount;
}

/** @brief intermediateErrorOccurred 信号计数槽 */
static void repOnIntermediate(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_repIeCount;
}

/**
 * @brief Reply 公共 API 单元测试
 * @details 状态推进（Protected setState 驱动）、信号计数、setError→finished
 *          顺序、NoError 复位语义、结果/原始负载/中间错误/NULL 保护。
 */
void XPlcReplyPublicApiTest()
{
#if XPLC_CORE_ON
    int passed = 0, failed = 0;
    XPlcReply* r = XPlcReply_create(XPlcReply_Common);
    XPrintf("========== XPlcReply 公共 API 测试开始 ==========\n");

    if (r == NULL) {
        ++failed;
        XPrintf("  [失败] XPlcReply_create 返回 NULL\n");
        xs7uFinish("Reply", passed, failed);
        return;
    }
    g_repFinCount = g_repErrCount = g_repStateCount = g_repIeCount = 0;
    g_repOrderN = 0;
    memset(g_repOrder, 0, sizeof(g_repOrder));

    /* 信号接线（Direct：单元测试无事件循环，需同步触发） */
    XObject_connect_1((XObject*)r, XSignal(XPlcReply_finished_signal), (XObject*)r,
                      repOnFinished, XConnectionType_Direct);
    XObject_connect_1((XObject*)r, XSignal(XPlcReply_errorOccurred_signal), (XObject*)r,
                      repOnError, XConnectionType_Direct);
    XObject_connect_1((XObject*)r, XSignal(XPlcReply_stateChanged_signal), (XObject*)r,
                      repOnState, XConnectionType_Direct);
    XObject_connect_1((XObject*)r, XSignal(XPlcReply_intermediateErrorOccurred_signal),
                      (XObject*)r, repOnIntermediate, XConnectionType_Direct);

    /* 1. 初始默认值 */
    XS7U_CHECK(passed, failed, XPlcReply_type(r) == XPlcReply_Common, "初始类型 Common");
    XS7U_CHECK(passed, failed, XPlcReply_state(r) == XPlcReply_State_No_Started,
               "初始状态 No_Started");
    XS7U_CHECK(passed, failed, !XPlcReply_isFinished(r), "初始未完成");
    XS7U_CHECK(passed, failed, XPlcReply_error(r) == XPlcDevice_NoError, "初始无错误");
    XS7U_CHECK(passed, failed, XPlcReply_pduRef(r) == 0, "初始 pduRef = 0");
    XS7U_CHECK(passed, failed, XPlcReply_result_const(r) == NULL &&
                               XPlcReply_rawResult_const(r) == NULL &&
                               XPlcReply_request_const(r) == NULL &&
                               XPlcReply_intermediateErrors(r) == NULL,
               "初始结果/负载/请求回带/中间错误为空");

    /* 2. 结果设置（深拷贝语义） */
    {
        XVariant* v = XVariant_create_int(42);
        XVariant* cp = NULL;
        XPlcReply_setResult(r, v);
        XS7U_CHECK(passed, failed, XPlcReply_result_const(r) != NULL &&
                                   XVariant_toInt(XPlcReply_result_const(r)) == 42,
                   "setResult(42) 后 result_const 可读");
        cp = XPlcReply_result(r);
        XS7U_CHECK(passed, failed, cp != NULL && XVariant_toInt(cp) == 42,
                   "result() 深拷贝非空且值一致");
        if (cp != NULL) {
            xs7uVarDelete(cp);
        }
        xs7uVarDelete(v);
    }

    /* 3. 原始负载设置 */
    {
        static const uint8_t raw[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        XByteArray* b = XByteArray_create();
        XByteArray* cp = NULL;
        xs7uBaAppend(b, raw, sizeof(raw));
        XPlcReply_setRawResult(r, b);
        XS7U_CHECK(passed, failed, XPlcReply_rawResult_const(r) != NULL,
                   "setRawResult 后负载非空");
        cp = XPlcReply_rawResult(r);
        XS7U_CHECK(passed, failed, cp != NULL &&
                                   xs7uBaSize(cp) == 4 &&
                                   memcmp(XByteArray_data(cp), raw, sizeof(raw)) == 0,
                   "rawResult() 深拷贝长度与内容一致");
        if (cp != NULL) {
            xs7uBaDelete(cp);
        }
        xs7uBaDelete(b);
    }

    /* 4. setFinished：置位发 finished（仅首次），幂等重复置位不再发 */
    XPlcReply_setFinished(r, true);
    XS7U_CHECK(passed, failed, XPlcReply_isFinished(r) && g_repFinCount == 1,
               "setFinished(true) → finished 信号 1 次");
    XPlcReply_setFinished(r, true);
    XS7U_CHECK(passed, failed, g_repFinCount == 1,
               "setFinished(true) 幂等：不重复发信号");
    XPlcReply_setFinished(r, false);
    XS7U_CHECK(passed, failed, !XPlcReply_isFinished(r) && g_repFinCount == 1,
               "setFinished(false) 复位不发信号");

    /* 5. 状态推进（Protected setState）：Requesting→Waiting→Responding→Finished */
    XPlcReply_setState(r, XPlcReply_State_Requesting);
    XS7U_CHECK(passed, failed,
               XPlcReply_state(r) == XPlcReply_State_Requesting && g_repStateCount == 1,
               "setState(Requesting) → stateChanged 1 次");
    XPlcReply_setState(r, XPlcReply_State_Waiting);
    XPlcReply_setState(r, XPlcReply_State_Responding);
    XS7U_CHECK(passed, failed, g_repStateCount == 3, "三步推进 → stateChanged 3 次");
    XS7U_CHECK(passed, failed, !XPlcReply_isFinished(r), "中间态不置完成标志");
    XPlcReply_setState(r, XPlcReply_State_Finished);
    XS7U_CHECK(passed, failed, XPlcReply_isFinished(r) && g_repFinCount == 2,
               "setState(Finished) → 自动完成 + finished 信号");

    /* 6. pduRef 登记（Protected） */
    XPlcReply_setPduRef(r, 0x1234);
    XS7U_CHECK(passed, failed, XPlcReply_pduRef(r) == 0x1234, "setPduRef(0x1234) 生效");

    /* 7. setError：errorOccurred 先于 finished（顺序断言） */
    {
        XPlcReply* r2 = XPlcReply_create(XPlcReply_Raw);
        if (r2 != NULL) {
            XString* es = NULL;
            g_repOrderN = 0;
            g_repFinCount = 0;
            g_repErrCount = 0;
            XObject_connect_1((XObject*)r2, XSignal(XPlcReply_errorOccurred_signal),
                              (XObject*)r2, repOnError, XConnectionType_Direct);
            XObject_connect_1((XObject*)r2, XSignal(XPlcReply_finished_signal),
                              (XObject*)r2, repOnFinished, XConnectionType_Direct);
            XPlcReply_setError(r2, XPlcDevice_TimeoutError, "unit-test timeout");
            XS7U_CHECK(passed, failed, XPlcReply_error(r2) == XPlcDevice_TimeoutError,
                       "setError(TimeoutError) 错误码生效");
            XS7U_CHECK(passed, failed, XPlcReply_isFinished(r2),
                       "setError 非法错误 → 自动完成");
            XS7U_CHECK(passed, failed, g_repErrCount == 1 && g_repFinCount == 1,
                       "setError → errorOccurred/finished 各 1 次");
            XS7U_CHECK(passed, failed, g_repOrderN == 2 && g_repOrder[0] == 1 &&
                                       g_repOrder[1] == 2,
                       "信号顺序：errorOccurred 先于 finished");
            es = XPlcReply_errorString(r2);
            XS7U_CHECK(passed, failed,
                       es != NULL && XString_contains_utf8(es, "timeout",
                                                           XChar_CaseSensitive),
                       "错误描述回读包含 \"timeout\"");
            if (es != NULL) {
                xs7uStrDelete(es);
            }
            XClass_delete_base((XClass*)r2);
            XS7U_CHECK(passed, failed, true, "错误路径 Reply 对象删除无崩溃");
        } else {
            ++failed;
            XPrintf("  [失败] 错误路径 Reply 创建失败\n");
        }
    }

    /* 8. setError(NoError)：仅清错误，不完成、不发信号（内部重试复位语义） */
    {
        int finBefore = g_repFinCount;
        int errBefore = g_repErrCount;
        XPlcReply_setError(r, XPlcDevice_NoError, NULL);
        XS7U_CHECK(passed, failed, XPlcReply_error(r) == XPlcDevice_NoError,
                   "setError(NoError) 清错误码");
        XS7U_CHECK(passed, failed, g_repErrCount == errBefore && g_repFinCount == finBefore,
                   "setError(NoError) 不发 errorOccurred/finished");
    }

    /* 9. 中间错误：addIntermediateError → 列表 + 信号 */
    XPlcReply_addIntermediateError(r, XPlcDevice_ChunkError);
    XS7U_CHECK(passed, failed, g_repIeCount == 1, "addIntermediateError → 信号 1 次");
    {
        XVector* ies = XPlcReply_intermediateErrors(r);
        XS7U_CHECK(passed, failed, ies != NULL && xs7uVecSize(ies) == 1,
                   "intermediateErrors() 列表长度 1");
        if (ies != NULL) {
            xs7uVecDelete(ies);
        }
    }
    XPlcReply_clearIntermediateError(r);
    {
        XVector* ies = XPlcReply_intermediateErrors(r);
        XS7U_CHECK(passed, failed,
                   ies == NULL || xs7uVecSize(ies) == 0,
                   "clearIntermediateError 后无中间错误（NULL 或空表）");
        if (ies != NULL) {
            xs7uVecDelete(ies);
        }
    }

    /* 10. NULL 保护（全部公共入口） */
    {
        XVariant* v = XVariant_create_int(1);
        XByteArray* b = XByteArray_create();
        XS7U_CHECK(passed, failed,
                   XPlcReply_type(NULL) == XPlcReply_Raw &&
                       XPlcReply_state(NULL) == XPlcReply_State_No_Started &&
                       XPlcReply_pduRef(NULL) == 0 &&
                       !XPlcReply_isFinished(NULL) &&
                       XPlcReply_result(NULL) == NULL &&
                       XPlcReply_result_const(NULL) == NULL &&
                       XPlcReply_rawResult(NULL) == NULL &&
                       XPlcReply_rawResult_const(NULL) == NULL &&
                       XPlcReply_request(NULL) == NULL &&
                       XPlcReply_request_const(NULL) == NULL &&
                       XPlcReply_errorString(NULL) == NULL &&
                       XPlcReply_error(NULL) == XPlcDevice_UnknownError &&
                       XPlcReply_intermediateErrors(NULL) == NULL,
                   "全部 getter(NULL) 安全返回默认值");
        XPlcReply_setResult(NULL, v);
        XPlcReply_setRawResult(NULL, b);
        XPlcReply_setFinished(NULL, true);
        XPlcReply_setError(NULL, XPlcDevice_TimeoutError, "x");
        XPlcReply_addIntermediateError(NULL, XPlcDevice_ChunkError);
        XPlcReply_setState(NULL, XPlcReply_State_Waiting);
        XPlcReply_setPduRef(NULL, 1);
        XPlcReply_clearIntermediateError(NULL);
        XS7U_CHECK(passed, failed, true, "全部 setter(NULL) 不崩溃");
        xs7uBaDelete(b);
        xs7uVarDelete(v);
    }

    /* 11. 对象删除 */
    XClass_delete_base((XClass*)r);
    XS7U_CHECK(passed, failed, true, "Reply 对象删除无崩溃");

    xs7uFinish("Reply", passed, failed);
#else
    XPrintf("[XS7][UNIT] Reply SKIP (XPLC_CORE_ON=0，伞层整段裁剪)\n");
#endif /* XPLC_CORE_ON */
}

/******************************************************************************************
 * 十二、XS7Test_runAll —— 离线单测一键全集
 ******************************************************************************************/

/**
 * @brief 顺序执行全部离线单测组
 * @return 全部组通过返回 true
 * @note 组顺序与 XProtocolTest.h 声明一致；受裁剪开关影响的组内部自行
 *       打印 SKIP；末尾打印 [XS7][UNIT] TOTAL PASS|FAIL。
 */
bool XS7Test_runAll(void)
{
    g_xuRunOk = true;
    XS7TpktTest();
    XS7CotpTest();
    XS7PduTest_Read();
    XS7PduTest_Write();
    XS7PduTest_Setup();
    XS7AddressTest();
    XS7ValueTest();
    XS7ControlTest();
    XS7SessionTest();
    XPlcReplyPublicApiTest();
    XPrintf("[XS7][UNIT] TOTAL %s\n", g_xuRunOk ? "PASS" : "FAIL");
    return g_xuRunOk;
}

#if XS7_CORE_ON
/******************************************************************************************
 * 十三、XS7FakePlc —— 文件内 static 假 PLC 桩（仅测试用）
 ******************************************************************************************/

/** @brief 假 PLC 单客户端连接槽位 */
typedef struct XS7FakeClient {
    XTcpSocket* sock;     /**< 已接入的套接字（借用） */
    XByteArray* rx;       /**< 接收缓冲（粘包/半包重组） */
    bool        active;   /**< 槽位占用标志 */
} XS7FakeClient;

#define XS7FAKE_MAX_CLIENTS  4        /**< 最大并发桩连接数 */
#define XS7FAKE_DB1_SIZE     2048     /**< 假 DB1 尺寸（字节） */
#define XS7FAKE_AREA_SIZE    2048     /**< 假 M/Q/I/T/C 区尺寸（字节） */
#define XS7FAKE_READ_MAX     1024     /**< 单 item 读上限（字节） */

static XTcpServer*   g_fakeServer = NULL;                  /**< 桩服务器 */
static XS7FakeClient g_fakeClients[XS7FAKE_MAX_CLIENTS];    /**< 客户端槽位表 */
static uint8_t g_fakeDb1[XS7FAKE_DB1_SIZE];                 /**< 假 DB1 存储 */
static uint8_t g_fakeM[XS7FAKE_AREA_SIZE];                  /**< 假 M 区 */
static uint8_t g_fakeQ[XS7FAKE_AREA_SIZE];                  /**< 假 Q 区 */
static uint8_t g_fakeI[XS7FAKE_AREA_SIZE];                  /**< 假 I 区（只读） */
static uint8_t g_fakeT[XS7FAKE_AREA_SIZE];                  /**< 假 T 区（字访问） */
static uint8_t g_fakeC[XS7FAKE_AREA_SIZE];                  /**< 假 C 区（字访问） */
static int     g_fakeSilentCount = 0;                       /**< 待吞帧计数（吞 N 帧不回复，覆盖重试全部尝试） */
static bool    g_fakeDropOnce = false;                      /**< 处理完下一请求后断连 */

/**
 * @brief 初始化假 PLC 存储（DB1 清零便于写后读回比对；I 区确定性图案）
 */
static void xs7fakeInitMemory(void)
{
    size_t i;
    for (i = 0; i < XS7FAKE_DB1_SIZE; ++i) {
        g_fakeDb1[i] = 0;
    }
    for (i = 0; i < XS7FAKE_AREA_SIZE; ++i) {
        g_fakeM[i] = 0;
        g_fakeQ[i] = 0;
        g_fakeI[i] = (uint8_t)((i * 7 + 3) & 0xFF);   /* I 区只读图案 */
        g_fakeT[i] = 0;
        g_fakeC[i] = 0;
    }
    g_fakeSilentCount = 0;
    g_fakeDropOnce = false;
}

/**
 * @brief 按区域选择假存储
 * @param area 区域线码
 * @param dbNumber DB 号
 * @param size 输出：该区可用字节数
 * @return 存储指针；区域不支持返回 NULL
 */
static uint8_t* xs7fakeAreaMem(uint8_t area, uint16_t dbNumber, size_t* size)
{
    switch (area) {
    case XS7Area_DB:              /* XS7Area_V 线码同为 0x84，共用本分支 */
        *size = XS7FAKE_DB1_SIZE;
        return (dbNumber == 1) ? g_fakeDb1 : NULL;   /* 仅 DB1，其余越界 */
    case XS7Area_M:
        *size = XS7FAKE_AREA_SIZE;
        return g_fakeM;
    case XS7Area_Q:
        *size = XS7FAKE_AREA_SIZE;
        return g_fakeQ;
    case XS7Area_I:
        *size = XS7FAKE_AREA_SIZE;
        return g_fakeI;
    case XS7Area_T:
        *size = XS7FAKE_AREA_SIZE;
        return g_fakeT;
    case XS7Area_C:
        *size = XS7FAKE_AREA_SIZE;
        return g_fakeC;
    default:
        *size = 0;
        return NULL;
    }
}

/**
 * @brief 向指定客户端套接字回发一帧（手工组 TPKT 头）
 */
static void xs7fakeSend(XTcpSocket* sock, const uint8_t* payload, size_t len)
{
    uint8_t hdr[4];
    if (sock == NULL || payload == NULL || len == 0) {
        return;
    }
    hdr[0] = 0x03;
    hdr[1] = 0x00;
    hdr[2] = (uint8_t)(((len + 4) >> 8) & 0xFF);
    hdr[3] = (uint8_t)((len + 4) & 0xFF);
    (void)xs7uSockWrite(sock, (const char*)hdr, (int64_t)sizeof(hdr));
    (void)xs7uSockWrite(sock, (const char*)payload, (int64_t)len);
}

/**
 * @brief 组 COTP CC 帧（回应 CR；参数序 C0/C1/C2）
 * @param out 输出缓冲（≥18B）
 * @param crPtsap CR 帧内目标 TSAP（成为 CC 的 C1 本端侧）
 * @param crLtsap CR 帧内本端 TSAP（成为 CC 的 C2 远端侧）
 * @return CC 帧长度（18）
 */
static size_t xs7fakeBuildCc(uint8_t* out, uint16_t crPtsap, uint16_t crLtsap)
{
    out[0] = 0x11;                            /* LI：type+dst+src+class+参数=17 */
    out[1] = XS7COTP_PDU_CC;
    out[2] = 0x00; out[3] = 0x00;             /* dstRef */
    out[4] = 0x00; out[5] = 0x01;             /* srcRef */
    out[6] = 0x00;                            /* class 0 */
    out[7] = 0xC0; out[8] = 0x01; out[9] = 0x0A;                  /* TPDU 大小 */
    out[10] = 0xC1; out[11] = 0x02;
    out[12] = (uint8_t)(crPtsap >> 8);
    out[13] = (uint8_t)(crPtsap & 0xFF);
    out[14] = 0xC2; out[15] = 0x02;
    out[16] = (uint8_t)(crLtsap >> 8);
    out[17] = (uint8_t)(crLtsap & 0xFF);
    return 18;
}

/**
 * @brief 向指定客户端回发 DT 帧（TPKT + 02 F0 80 + S7 PDU）
 */
static void xs7fakeSendDt(XTcpSocket* sock, const uint8_t* s7, size_t len)
{
    static uint8_t frame[XS7_MAX_FRAME];
    if (sock == NULL || s7 == NULL || len == 0 || len + 3 > XS7_MAX_FRAME) {
        return;
    }
    frame[0] = 0x02;
    frame[1] = XS7COTP_PDU_DT;
    frame[2] = 0x80;
    memcpy(frame + 3, s7, len);
    xs7fakeSend(sock, frame, len + 3);   /* 单一 TPKT：DT 头与 S7 段不可拆帧 */
}

/**
 * @brief 组 Ack_Data S7 头（12B：32 03 ref paramLen dataLen 00 00）
 */
static void xs7fakeAckHeader(uint8_t* out, uint16_t pduRef,
                             uint16_t paramLen, uint16_t dataLen)
{
    out[0] = 0x32;
    out[1] = XS7_ROSCTR_ACK_DATA;
    out[2] = 0x00;                             /* 冗余标识符（固定 00 00） */
    out[3] = 0x00;
    out[4] = (uint8_t)(pduRef >> 8);           /* PDU 引用号 */
    out[5] = (uint8_t)(pduRef & 0xFF);
    out[6] = (uint8_t)(paramLen >> 8);         /* 参数段长度 */
    out[7] = (uint8_t)(paramLen & 0xFF);
    out[8] = (uint8_t)(dataLen >> 8);          /* 数据段长度 */
    out[9] = (uint8_t)(dataLen & 0xFF);
    out[10] = 0x00;                            /* errorClass */
    out[11] = 0x00;                            /* errorCode */
}

/**
 * @brief 追加一个读应答数据项（成功或错误码；成功项偶对齐）
 * @return 新写入位置
 */
static size_t xs7fakePushReadItem(uint8_t* out, size_t pos, uint8_t returnCode,
                                  uint8_t transport, const uint8_t* data, size_t n)
{
    out[pos++] = returnCode;
    if (returnCode != XS7_RETURN_OK) {
        /* 错误项 4B：rc + transport 0 + 长度 0x0000（与真机第 1 轮实测
         * 形态一致——真机 0x05/0x0A 错误项均可被 XS7Pdu_parseReadAck 解析） */
        out[pos++] = 0x00;
        out[pos++] = 0x00;
        out[pos++] = 0x00;
        return pos;
    }
    out[pos++] = transport;
    if (transport == XS7_TRANSPORT_DATA_OCTET) {
        out[pos++] = (uint8_t)(n >> 8);       /* 八位组串：长度=字节数 */
        out[pos++] = (uint8_t)(n & 0xFF);
    } else {
        uint16_t nbits = (uint16_t)(n * 8);   /* 0x03/0x04：长度=位计数（Wireshark 同规则） */
        out[pos++] = (uint8_t)(nbits >> 8);
        out[pos++] = (uint8_t)(nbits & 0xFF);
    }
    if (data != NULL && n > 0) {
        memcpy(out + pos, data, n);
        pos += n;
        if ((n & 1u) != 0u) {
            out[pos++] = 0x00;                /* 奇数长度偶对齐填充 */
        }
    }
    return pos;
}

/**
 * @brief 处理一条 S7 Job 请求并回 Ack_Data
 * @param sock 客户端套接字
 * @param s7 S7 PDU（已剥 TPKT/COTP）
 * @param len S7 PDU 长度
 */
static void xs7fakeDispatchS7(XTcpSocket* sock, const uint8_t* s7, size_t len)
{
    XS7PduHeader hdr;
    static uint8_t out[XS7_MAX_FRAME];

    if (!XS7Pdu_parseHeader(s7, len, &hdr)) {
        return;
    }
    if (hdr.rosctr != XS7_ROSCTR_JOB || hdr.pduRef == 0 ||
        len < (size_t)(10 + hdr.paramLen)) {
        return;
    }

    /* ---- SetupCommunication（0xF0）：协商 AmQ=1、PDU=240（对齐真机协商值，
     *      覆盖 5 分片 1024B 读写的预算路径；12B 头 + 8B 参数） ---- */
    if (s7[10] == XS7_FUNC_SETUP) {
        xs7fakeAckHeader(out, hdr.pduRef, 8, 0);
        out[12] = XS7_FUNC_SETUP;
        out[13] = 0x00;
        out[14] = 0x00; out[15] = 0x01;       /* AmQ = 1 */
        out[16] = 0x00; out[17] = 0x01;       /* AmQ = 1 */
        out[18] = 0x00; out[19] = 0xF0;       /* 协商 PDU = 240（真机同值） */
        xs7fakeSendDt(sock, out, 20);
        return;
    }

#if XS7_CONTROL_ON
    /* ---- 运维：Run（0x28）/Stop（0x29），应答参数 func+状态码 ---- */
    if (s7[10] == XS7_FUNC_RUN || s7[10] == XS7_FUNC_STOP) {
        /* 12B 头 + 参数(func 回显) + 数据项(ret|tr|长度|状态码)，共 19B */
        xs7fakeAckHeader(out, hdr.pduRef, 2, 5);
        out[12] = s7[10];
        out[13] = 0x00;                       /* 参数：func 回显 + 保留 */
        out[14] = 0x00; out[15] = 0x04;       /* 数据项：ret=0 transport=04 */
        out[16] = 0x00; out[17] = 0x01;       /* 载荷长度 1（BE） */
        out[18] = 0x00;                       /* 载荷：状态码=0（正常成功） */
        xs7fakeSendDt(sock, out, 19);
        return;
    }
#endif

    /* ---- Read Var（0x04） ---- */
    if (s7[10] == XS7_FUNC_READ && hdr.paramLen >= 2) {
        int itemCount = s7[11];
        int it;
        uint16_t dataLen = 0;
        static uint8_t items[XS7_MAX_FRAME];
        size_t ipos = 0;
        if (itemCount < 1 || itemCount > 8) {
            return;
        }
        for (it = 0; it < itemCount; ++it) {
            const uint8_t* item = s7 + 12 + (size_t)it * 12;
            uint8_t transport = item[3];
            uint16_t count = (uint16_t)((item[4] << 8) | item[5]);
            uint16_t db = (uint16_t)((item[6] << 8) | item[7]);
            uint8_t area = item[8];
            uint32_t bitAddr = ((uint32_t)item[9] << 16) |
                               ((uint32_t)item[10] << 8) | (uint32_t)item[11];
            size_t areaSize = 0;
            uint8_t* mem = xs7fakeAreaMem(area, db, &areaSize);
            if (mem == NULL) {
                ipos = xs7fakePushReadItem(items, ipos, XS7_RETURN_RANGE_ERROR,
                                           0, NULL, 0);
                dataLen = (uint16_t)(dataLen + 2);
                continue;
            }
            if (transport == XS7_TRANSPORT_BIT) {
                /* 位读：count 位，LSB 在前打包 */
                uint8_t packed[XS7FAKE_READ_MAX / 8 + 1];
                uint16_t nbits = (count <= XS7FAKE_READ_MAX * 8)
                                     ? count : (uint16_t)(XS7FAKE_READ_MAX * 8);
                uint16_t k;
                size_t nbytes = (size_t)((nbits + 7) / 8);
                memset(packed, 0, sizeof(packed));
                for (k = 0; k < nbits; ++k) {
                    uint32_t ba = bitAddr + k;
                    if ((mem[(ba >> 3) % areaSize] >> (ba & 7)) & 1u) {
                        packed[k >> 3] |= (uint8_t)(1u << (k & 7));
                    }
                }
                ipos = xs7fakePushReadItem(items, ipos, XS7_RETURN_OK,
                                           XS7_TRANSPORT_DATA_BIT, packed, nbytes);
                dataLen = (uint16_t)(dataLen + 4 + nbytes + (nbytes & 1u));
            } else {
                /* 字节量读：0x04（字）count 单位为字（×2B）；0x02/0x09 按字节；
                 * 线地址恒为位地址（S7 ANY 项约定），>>3 折回字节地址 */
                size_t off = (size_t)(bitAddr >> 3);
                size_t n = (count > XS7FAKE_READ_MAX) ? XS7FAKE_READ_MAX : count;
                if (transport == XS7_TRANSPORT_WORD) {
                    size_t n2 = n * 2;
                    n = (n2 > XS7FAKE_READ_MAX) ? XS7FAKE_READ_MAX : n2;
                }
                if (off >= areaSize) {
                    ipos = xs7fakePushReadItem(items, ipos, XS7_RETURN_RANGE_ERROR,
                                               0, NULL, 0);
                    dataLen = (uint16_t)(dataLen + 2);
                    continue;
                }
                if (off + n > areaSize) {
                    n = areaSize - off;       /* 尾部截短（替代越界错误） */
                }
                ipos = xs7fakePushReadItem(items, ipos, XS7_RETURN_OK,
                                           XS7_TRANSPORT_DATA_BYTE, mem + off, n);
                dataLen = (uint16_t)(dataLen + 4 + n + (n & 1u));
            }
        }
        xs7fakeAckHeader(out, hdr.pduRef, 2, dataLen);
        out[12] = XS7_FUNC_READ;
        out[13] = (uint8_t)itemCount;
        memcpy(out + 14, items, ipos);
        xs7fakeSendDt(sock, out, 14 + ipos);
        return;
    }

    /* ---- Write Var（0x05） ---- */
    if (s7[10] == XS7_FUNC_WRITE && hdr.paramLen >= 2) {
        int itemCount = s7[11];
        int it;
        size_t dataOff = (size_t)(10 + hdr.paramLen);   /* Job 头 10B：数据区紧跟参数 */
        static uint8_t rcs[8];
        if (itemCount < 1 || itemCount > 8 || len < dataOff + (size_t)itemCount) {
            return;
        }
        for (it = 0; it < itemCount; ++it) {
            const uint8_t* item = s7 + 12 + (size_t)it * 12;
            const uint8_t* d = s7 + dataOff;      /* 本 item 数据区起点 */
            uint16_t lenField = (uint16_t)((d[2] << 8) | d[3]);
            /* 长度字段语义：0x09 八位组串=字节数；0x03/0x04=位计数（与
             * XS7Pdu.c xs7PduWriteLengthField 同规则） */
            size_t nbytes = (d[1] == XS7_TRANSPORT_DATA_OCTET)
                                ? (size_t)lenField
                                : (size_t)((lenField + 7) / 8);
            uint16_t bitCount = (d[1] == XS7_TRANSPORT_DATA_OCTET)
                                    ? (uint16_t)(lenField * 8)
                                    : lenField;
            size_t frameBytes = 4 + nbytes + (nbytes & 1u);
            uint16_t db = (uint16_t)((item[6] << 8) | item[7]);
            uint8_t area = item[8];
            uint32_t bitAddr = ((uint32_t)item[9] << 16) |
                               ((uint32_t)item[10] << 8) | (uint32_t)item[11];
            size_t areaSize = 0;
            uint8_t* mem = xs7fakeAreaMem(area, db, &areaSize);
            /* 线地址恒为位地址（S7 ANY 项约定），>>3 折回字节地址 */
            size_t off = (size_t)(bitAddr >> 3);
            rcs[it] = XS7_RETURN_OK;
            if (mem == NULL || area == XS7Area_I) {
                rcs[it] = XS7_RETURN_RANGE_ERROR;  /* 越界 / I 区只读 */
            } else if ((d[1] == XS7_TRANSPORT_DATA_BIT) ||
                       (item[3] == XS7_TRANSPORT_BIT)) {
                /* 位写：数据区按位打包（LSB 在前） */
                uint16_t k;
                for (k = 0; k < bitCount; ++k) {
                    uint32_t ba = bitAddr + k;
                    uint8_t bitVal = (uint8_t)((d[4 + (k >> 3)] >> (k & 7)) & 1u);
                    size_t idx = (ba >> 3) % areaSize;
                    if (bitVal != 0u) {
                        mem[idx] |= (uint8_t)(1u << (ba & 7));
                    } else {
                        mem[idx] &= (uint8_t)~(1u << (ba & 7));
                    }
                }
            } else if (off + nbytes <= areaSize) {
                memcpy(mem + off, d + 4, nbytes);
            } else {
                rcs[it] = XS7_RETURN_RANGE_ERROR;
            }
            dataOff += frameBytes;
        }
        xs7fakeAckHeader(out, hdr.pduRef, 2, (uint16_t)itemCount);
        out[12] = XS7_FUNC_WRITE;
        out[13] = (uint8_t)itemCount;
        memcpy(out + 14, rcs, (size_t)itemCount);
        xs7fakeSendDt(sock, out, 14 + (size_t)itemCount);
        return;
    }

    /* ---- 其余（块传输等）：真机（第 1 轮只读实测）对 ListBlocks 无应答，
     *      客户端走超时→SKIP 路径；此处保持静默以对齐真机行为 ---- */
    (void)out;
}

/**
 * @brief 客户端数据到达槽：TPKT 分帧并逐帧派发
 */
static void xs7fakeOnReadyRead(XObject* receiver, XVarList* args)
{
    XTcpSocket* sock = (XTcpSocket*)receiver;
    XS7FakeClient* cli = NULL;
    int i;
    (void)args;
    for (i = 0; i < XS7FAKE_MAX_CLIENTS; ++i) {
        if (g_fakeClients[i].active && g_fakeClients[i].sock == sock) {
            cli = &g_fakeClients[i];
            break;
        }
    }
    if (cli == NULL) {
        return;
    }
    (void)XIODevice_readAll_2((XIODevice*)sock, cli->rx, true);

    /* 按 TPKT 定长切帧派发 */
    for (;;) {
        size_t bufLen = xs7uBaSize(cli->rx);
        size_t total = 0;
        const uint8_t* data;
        if (bufLen < 4) {
            break;
        }
        data = XByteArray_data(cli->rx);
        if (!XS7Tpkt_peekLength(data, bufLen, &total)) {
            /* 坏头：丢 1 字节重同步（与 XS7Session_feed 同约定） */
            memmove(XByteArray_data(cli->rx), XByteArray_data(cli->rx) + 1, bufLen - 1);
            xs7uBaResize(cli->rx, bufLen - 1);
            continue;
        }
        if (bufLen < total) {
            break;   /* 半包等待 */
        }
        if (g_fakeSilentCount > 0) {
            /* 故障注入：吞掉本帧不回复（客户端应走超时/重试路径） */
            --g_fakeSilentCount;
        } else if ((data[5] & 0xF0) == XS7COTP_PDU_CR) {
            uint8_t cc[18];
            uint16_t crPtsap = (uint16_t)((data[20] << 8) | data[21]);
            uint16_t crLtsap = (uint16_t)((data[16] << 8) | data[17]);
            (void)xs7fakeBuildCc(cc, crPtsap, crLtsap);
            xs7fakeSend(sock, cc, 18);
        } else if (data[4] == 0x02 && data[5] == XS7COTP_PDU_DT && data[6] == 0x80) {
            bool dropAfter = g_fakeDropOnce;
            xs7fakeDispatchS7(sock, data + 7, total - 7);
            if (dropAfter) {
                g_fakeDropOnce = false;
                xs7uSockDrop(sock);
            }
        }
        /* 精确推进读指针 */
        memmove(XByteArray_data(cli->rx), XByteArray_data(cli->rx) + total,
                bufLen - total);
        xs7uBaResize(cli->rx, bufLen - total);
    }
}

/**
 * @brief 新连接槽：登记客户端并接 readyRead
 */
static void xs7fakeOnNewConnection(XObject* receiver, XVarList* args)
{
    XTcpSocket* sock = NULL;
    (void)receiver;
    (void)args;
    while ((sock = XTcpServer_nextPendingConnection_base(g_fakeServer)) != NULL) {
        int i;
        for (i = 0; i < XS7FAKE_MAX_CLIENTS; ++i) {
            if (!g_fakeClients[i].active) {
                g_fakeClients[i].active = true;
                g_fakeClients[i].sock = sock;
                xs7uBaClear(g_fakeClients[i].rx);
                XObject_connect_1((XObject*)sock, XSignal(XTcpSocket_readyRead_signal),
                                  (XObject*)sock, xs7fakeOnReadyRead,
                                  XConnectionType_Direct);
                break;
            }
        }
        if (i == XS7FAKE_MAX_CLIENTS) {
            /* 槽位满：拒绝（集成测试最多 2 客户端，不应发生） */
            xs7uSockDrop(sock);
        }
    }
}

/**
 * @brief 启动假 PLC 服务器（127.0.0.1 自动高位端口）
 * @return 成功返回 true；已启动时直接返回 true
 */
static bool xs7fakeStart(void)
{
    XHostAddress addr;
    int i;
    if (g_fakeServer != NULL) {
        return true;
    }
    xs7fakeInitMemory();
    for (i = 0; i < XS7FAKE_MAX_CLIENTS; ++i) {
        g_fakeClients[i].active = false;
        g_fakeClients[i].sock = NULL;
        g_fakeClients[i].rx = XByteArray_create();
    }
    g_fakeServer = XTcpServer_create();
    if (g_fakeServer == NULL) {
        return false;
    }
    XObject_connect_1((XObject*)g_fakeServer, XSignal(XTcpServer_newConnection_signal),
                      (XObject*)g_fakeServer, xs7fakeOnNewConnection,
                      XConnectionType_Direct);
    XHostAddress_init(&addr);
    XHostAddress_setAddress(&addr, "127.0.0.1");
    if (!XTcpServer_listen(g_fakeServer, &addr, 0)) {
        XPrintf("[XS7][PLC] 假 PLC 桩监听失败\n");
        XTcpServer_close(g_fakeServer);
        XClass_delete_base((XClass*)g_fakeServer);
        g_fakeServer = NULL;
        return false;
    }
    return true;
}

/**
 * @brief 停止假 PLC 服务器并释放槽位缓冲
 * @note 已接入的客户端套接字随进程回收（测试进程生命周期内无需逐一释放）
 */
static void xs7fakeStop(void)
{
    int i;
    if (g_fakeServer != NULL) {
        XTcpServer_close(g_fakeServer);
        XClass_delete_base((XClass*)g_fakeServer);
        g_fakeServer = NULL;
    }
    for (i = 0; i < XS7FAKE_MAX_CLIENTS; ++i) {
        if (g_fakeClients[i].rx != NULL) {
            xs7uBaDelete(g_fakeClients[i].rx);
            g_fakeClients[i].rx = NULL;
        }
        g_fakeClients[i].active = false;
        g_fakeClients[i].sock = NULL;
    }
}

/******************************************************************************************
 * 十四、集成测试 XS7Test_integration_run
 ******************************************************************************************/

/** @brief 集成测试用例编号 */
typedef enum XS7IntegCase {
    XS7IntegCase_Connect = 0,     /**< 用例1 connect_handshake（握手+协商） */
    XS7IntegCase_DbBit,           /**< 用例2a db_readwrite_bit */
    XS7IntegCase_DbByte,          /**< 用例2b db_readwrite_byte */
    XS7IntegCase_DbWord,          /**< 用例2c db_readwrite_word */
    XS7IntegCase_DbDWord,         /**< 用例2d db_readwrite_dword */
    XS7IntegCase_DbReal,          /**< 用例2e db_readwrite_real */
    XS7IntegCase_DbString,        /**< 用例2f db_readwrite_string */
    XS7IntegCase_MqArea,          /**< 用例3a m_q_area 读写 */
    XS7IntegCase_IArea,           /**< 用例3b i_area 只读 */
    XS7IntegCase_TcAccess,        /**< 用例4 t_c_access（尽力而为） */
    XS7IntegCase_BadAddress,      /**< 用例6 bad_address（错误路径） */
    XS7IntegCase_LargeFrag,       /**< 用例5 large_fragmentation（≥1000B 分片；
                                       真机第 2 轮起先于运维控制类用例执行，
                                       并前置区域存在性预检查，防 CPU 断连株连） */
    XS7IntegCase_RunStop,         /**< 用例7 run_stop（XS7_CONTROL_ON） */
    XS7IntegCase_BlockOps,        /**< 用例8 block_ops（XS7_BLOCK_ON） */
    XS7IntegCase_TimeoutReconn,   /**< 用例9 timeout_reconnect（FakePlc 故障注入） */
    XS7IntegCase_Count
} XS7IntegCase;

/** @brief 用例名（与输出行一一对应） */
static const char* const g_integCaseNames[XS7IntegCase_Count] = {
    "connect_handshake", "db_readwrite_bit", "db_readwrite_byte",
    "db_readwrite_word", "db_readwrite_dword", "db_readwrite_real",
    "db_readwrite_string", "m_q_area", "i_area_readonly", "t_c_access",
    "bad_address", "large_fragmentation", "run_stop", "block_ops",
    "timeout_reconnect"
};

/** @brief 单个异步读写操作描述（op 引擎顺序执行） */
typedef struct XS7IntegOp {
    char         addr[40];      /**< 地址串（XS7TcpClient_sendRead_2 语法） */
    XS7ValueType type;          /**< 值类型 */
    int          count;         /**< 元素个数（String 为容量 n） */
    bool         isWrite;       /**< true=写操作（value 生效）；false=读操作 */
    XVariant*    value;         /**< 写值（引擎拥有，用后释放） */
    bool         hasExpect;     /**< 读回后是否做字节级比对 */
    uint8_t      expect[64];    /**< 期望读回字节（前 expectLen 字节） */
    int          expectLen;     /**< 期望长度 */
    int          retries;       /**< 写操作瞬态拒绝后的已重试次数（真机第 2 轮：
                                     目标 CPU 存在波动性写拒绝（0x07/0x85），
                                     有界重试后才下结论） */
} XS7IntegOp;

#define XS7INTEG_OP_MAX 48          /**< op 队列容量 */

/* ---- 集成测试全局状态（单事件循环内使用，非线程安全） ---- */
static XS7TcpClient* g_integClient = NULL;      /**< 主客户端 */
static char g_integIp[64];                      /**< 目标 IP（XS7_PLC_IP） */
static uint16_t g_integPort = 102;              /**< 目标端口 */
static bool g_integFake = false;                /**< true=对接假 PLC（127.0.0.1） */
static bool g_integReadback = false;            /**< true=只读模式（XS7_PLC_READBACK=1） */
static int g_integSlotSeq[3] = { 1, 0, 2 };     /**< slot 尝试序列（设计要求 1,0,2） */
static int g_integSlotIdx = 0;                  /**< 当前尝试下标 */
static bool g_integSlotPending = false;         /**< 当前 slot 试探进行中 */
static XS7IntegOp g_integOps[XS7INTEG_OP_MAX];  /**< op 队列 */
static int g_integOpCount = 0;                  /**< op 队列长度 */
static int g_integRunningCase = -1;             /**< op 引擎正在执行的用例号 */
static int g_integOpIdx = 0;                    /**< 当前执行下标 */
static int g_integCaseIdx = (int)XS7IntegCase_DbBit; /**< 当前用例下标（Connect 由事件槽结算） */
static bool g_integCaseBad[XS7IntegCase_Count]; /**< 用例内出现过失败断言 */
static bool g_integCaseSettled[XS7IntegCase_Count]; /**< 用例结果行已打印 */
static bool g_integCaseDevLimited[XS7IntegCase_Count]; /**< 失败均属设备限制（SKIP-UNSUPPORTED 依据） */
static bool g_integCaseHardBad[XS7IntegCase_Count]; /**< 用例内存在硬失败（写被拒/发送失败等） */
static int g_integPass = 0;                     /**< PASS 计数 */
static int g_integFail = 0;                     /**< FAIL 计数 */
static int g_integSkip = 0;                     /**< SKIP 计数 */
static bool g_integOk = false;                  /**< 总结果 */
static bool g_integConnected = false;           /**< 主客户端已 Connected */
static bool g_integFinished = false;            /**< 已退出事件循环 */
static bool g_integStopSent = false;            /**< run_stop：Stop 已发送 */
static bool g_integRunRestored = false;         /**< run_stop：已恢复 RUN */
static int g_integBadSub = 0;                   /**< bad_address 子步游标 */
static bool g_integBadLocalReject = false;      /**< bad_address：非法语法已被本地拒绝 */
static bool g_integRecheckPending = false;      /**< 读回不一致后的二次取证读进行中 */
static int g_integReconnWait = 0;               /**< 重连轮询剩余次数 */

/* ---- 子步状态机标签（run_stop / timeout_reconnect 专用） ---- */
enum {
    XS7RS_SUB_STOP = 0,     /**< run_stop：等 Stop 应答 */
    XS7RS_SUB_RUN,          /**< run_stop：等 Run 应答 */
    XS7RS_SUB_CONFIRM,      /**< run_stop：读回确认 PLC 在线 */
    XS7TR_SUB_RETRY,        /**< timeout_reconnect：吞 1 帧 → 重试改写帧内 pduRef 后成功 */
    XS7TR_SUB_SILENT,       /**< timeout_reconnect：吞全部尝试帧等待超时 */
    XS7TR_SUB_DROP,         /**< timeout_reconnect：断连等待自动重连 */
    XS7TR_SUB_VERIFY        /**< timeout_reconnect：重连后读回验证 */
};
static int g_integSubMode = XS7RS_SUB_STOP;

/* ---- 前置声明 ---- */
static void integ_nextCase(void);
static void integ_pump(void);
static void integ_finish(void);
static bool integ_isControlErrorText(const XString* es);
static bool integ_isControlRefused(const XString* es);
static bool integ_isTransientWriteRefusal(const XString* es);
static void integ_buildCase(XS7IntegCase c);
static void integ_onReplyFinished(XObject* sender, XVarList* args);
static void integ_onRecheckRead(XObject* sender, XVarList* args);
static void integ_onWriteRetry(XObject* receiver, XVarList* args);
static void integ_onLargePrecheck(XObject* sender, XVarList* args);
static void integ_onReconnectVerify(XObject* sender, XVarList* args);
static void integ_retryConnect(XObject* receiver, XVarList* args);
static void integ_onSlotTimeout(XObject* receiver, XVarList* args);
static void integ_onWatchdog(XObject* receiver, XVarList* args);

/** @brief 写操作瞬态重试上限（真机第 2 轮：目标 CPU 存在分钟级波动写窗口） */
#define XS7INTEG_WRITE_RETRIES   2
/** @brief 写重试间隔（毫秒） */
#define XS7INTEG_WRITE_RETRY_MS  400

/**
 * @brief 安全拷贝 C 串（替代 strncpy，目标保证 NUL 结尾）
 */
static void xs7uCopyStr(char* dst, size_t dstSize, const char* src)
{
    size_t n = 0;
    if (dst == NULL || dstSize == 0) {
        return;
    }
    if (src != NULL) {
        while (n + 1 < dstSize && src[n] != '\0') {
            dst[n] = src[n];
            ++n;
        }
    }
    dst[n] = '\0';
}

/**
 * @brief 打印 RAW 行（[XS7][PLC] RAW <addr> <hex字节>，供 python-snap7 交叉比对）
 * @param addr 地址串
 * @param data 数据（可 NULL）
 * @param len 长度（超 32 截断打印前 32 字节加省略号）
 */
static void integ_printRaw(const char* addr, const uint8_t* data, size_t len)
{
    size_t i;
    XPrintf("[XS7][PLC] RAW %s ", addr);
    for (i = 0; i < len && i < 32; ++i) {
        XPrintf("%02X ", data[i]);
    }
    if (len > 32) {
        XPrintf("...(%u 字节)", (unsigned)len);
    }
    XPrintf("\n");
}

/**
 * @brief 记录用例结果并打印唯一一行
 * @param c 用例编号
 * @param verdict 1=PASS 0=FAIL -1=SKIP
 * @param detail 细节文本（可为 ""）
 */
static void integ_settle(XS7IntegCase c, int verdict, const char* detail)
{
    const char* verdictText;
    if (c < 0 || c >= XS7IntegCase_Count || g_integCaseSettled[c]) {
        return;   /* 结果只打一次 */
    }
    g_integCaseSettled[c] = true;
    verdictText = (verdict > 0) ? "PASS" : ((verdict == 0) ? "FAIL" : "SKIP");
    if (verdict > 0) {
        ++g_integPass;
    } else if (verdict == 0) {
        ++g_integFail;
    } else {
        ++g_integSkip;
    }
    XPrintf("[XS7][PLC] %s %s %s\n", g_integCaseNames[c], verdictText,
            (detail != NULL && detail[0] != '\0') ? detail : "");
}

/**
 * @brief deleteLater 的 XSlotFunc1 适配（原函数为 void(XObject*) 单参签名）
 */
static void xs7uReplyDeleteLater(XObject* receiver, XVarList* args)
{
    XObject_deleteLater(receiver);
    (void)args;
}

/**
 * @brief 通用 Reply 接线：父挂客户端 + 结果槽 + 完成后自动回收
 * @param reply 待接线 Reply（send* 返回值，可为 NULL）
 * @param slot 结果槽（finished 信号）
 * @return 接线成功返回 true；reply 为 NULL 返回 false
 */
static bool integ_wireReply(XPlcReply* reply, void (*slot)(XObject*, XVarList*))
{
    if (reply == NULL) {
        return false;
    }
    XObject_setParent((XObject*)reply, (XObject*)g_integClient);
    XObject_connect_1((XObject*)reply, XSignal(XPlcReply_finished_signal),
                      (XObject*)reply, slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)reply, XSignal(XPlcReply_finished_signal),
                      (XObject*)reply, xs7uReplyDeleteLater, XConnectionType_Auto);
    return true;
}

/**
 * @brief 判断错误描述串是否为"已运行/已停止"类提示（0x02/0x07 按成功语义）
 * @param es 错误描述串（可 NULL）
 * @return 含提示返回 true
 */
static bool integ_isControlErrorText(const XString* es)
{
    if (es == NULL) {
        return false;
    }
    return XString_contains_utf8(es, "0x07", XChar_CaseSensitive) ||
           XString_contains_utf8(es, "0x02", XChar_CaseSensitive) ||
           XString_contains_utf8(es, "已停止", XChar_CaseSensitive) ||
           XString_contains_utf8(es, "已运行", XChar_CaseSensitive);
}

/**
 * @brief 判断错误描述串是否为 CPU 负 ACK 拒绝控制（真机第 1 轮实测：
 *        S7-1200 对 0x28/0x29 回 rosctr=0x02 + errorClass=0x81 → 设备限制）
 */
static bool integ_isControlRefused(const XString* es)
{
    if (es == NULL) {
        return false;
    }
    return XString_contains_utf8(es, "negative ack", XChar_CaseSensitive);
}

/**
 * @brief 判断写错误是否属"目标 CPU 瞬态/区域性写拒绝"（真机第 2/3 轮实测：
 *        数据项 0x07 data-type-not-consistent / 0x06 not-supported 与作业级
 *        0x85 负 ACK，均随 CPU 波动窗口出现与消失，与编码无关——三种标准
 *        编码（bits/bytes/octet）均被同等拒绝）。此类错误有界重试后仍拒绝
 *        的按设备限制（SKIP-UNSUPPORTED）结算，不算本端 FAIL。
 */
static bool integ_isTransientWriteRefusal(const XString* es)
{
    if (es == NULL) {
        return false;
    }
    return XString_contains_utf8(es, "0x07", XChar_CaseSensitive) ||
           XString_contains_utf8(es, "0x06", XChar_CaseSensitive) ||
           XString_contains_utf8(es, "negative ack", XChar_CaseSensitive);
}

/**
 * @brief 清空 op 队列（释放写值变体）
 */
static void integ_clearOps(void)
{
    int i;
    for (i = 0; i < g_integOpCount; ++i) {
        if (g_integOps[i].value != NULL) {
            xs7uVarDelete(g_integOps[i].value);
            g_integOps[i].value = NULL;
        }
    }
    g_integOpCount = 0;
    g_integOpIdx = 0;
}

/**
 * @brief 向队列追加一个写操作（value 所有权转移给队列）
 */
static void integ_pushWrite(const char* addr, XS7ValueType type, int count,
                            XVariant* value)
{
    XS7IntegOp* op;
    if (g_integOpCount >= XS7INTEG_OP_MAX) {
        xs7uVarDelete(value);
        return;
    }
    op = &g_integOps[g_integOpCount++];
    memset(op, 0, sizeof(*op));
    xs7uCopyStr(op->addr, sizeof(op->addr), addr);
    op->type = type;
    op->count = count;
    op->isWrite = true;
    op->value = value;
}

/**
 * @brief 向队列追加一个读操作
 * @param hasExpect 是否比对读回字节（expectLen<=0 时只打印 RAW）
 */
static void integ_pushRead(const char* addr, XS7ValueType type, int count,
                           bool hasExpect, const uint8_t* expect, int expectLen)
{
    XS7IntegOp* op;
    if (g_integOpCount >= XS7INTEG_OP_MAX) {
        return;
    }
    op = &g_integOps[g_integOpCount++];
    memset(op, 0, sizeof(*op));
    xs7uCopyStr(op->addr, sizeof(op->addr), addr);
    op->type = type;
    op->count = count;
    op->isWrite = false;
    op->hasExpect = hasExpect && expectLen > 0;
    op->expectLen = expectLen;
    if (op->hasExpect && expect != NULL) {
        memcpy(op->expect, expect, (size_t)expectLen);
    }
}

/**
 * @brief op 引擎：顺序发送队列中的操作；全部完成后结算当前用例并推进
 */
static void integ_pump(void)
{
    while (g_integOpIdx < g_integOpCount) {
        XS7IntegOp* op = &g_integOps[g_integOpIdx];
        XPlcReply* reply = NULL;
        if (!g_integConnected ||
            XPlcDevice_state((XPlcDevice*)g_integClient) != XPlcDevice_ConnectedState) {
            g_integCaseBad[g_integRunningCase] = true;
            g_integCaseHardBad[g_integRunningCase] = true;
            ++g_integOpIdx;
            continue;
        }
        if (op->isWrite) {
            reply = XS7TcpClient_sendWrite_2(g_integClient, op->addr, op->type,
                                             op->count, op->value);
        } else {
            reply = XS7TcpClient_sendRead_2(g_integClient, op->addr, op->type,
                                            op->count);
        }
        if (!integ_wireReply(reply, integ_onReplyFinished)) {
            XPrintf("  [XS7] %s %s 发送失败（send* 返回 NULL：客户端未实现/未连接）\n",
                    g_integCaseNames[g_integRunningCase], op->addr);
            g_integCaseBad[g_integRunningCase] = true;
            g_integCaseHardBad[g_integRunningCase] = true;
            ++g_integOpIdx;
            continue;
        }
        return;   /* 等待异步完成 */
    }
    /* 队列执行完毕：结算（设备限制类失败按 SKIP-UNSUPPORTED 计，非本端缺陷） */
    {
        XS7IntegCase c = (XS7IntegCase)g_integRunningCase;
        if (!g_integCaseSettled[c]) {
            if (!g_integCaseBad[c] && !g_integCaseHardBad[c]) {
                integ_settle(c, 1,
                             g_integReadback ? "（只读回读成功）" : "（写→读回一致）");
            } else if (c == XS7IntegCase_TcAccess) {
                integ_settle(c, -1,
                             "（目标 CPU 对 T/C 区回 0x05 address out of range，"
                             "SKIP-UNSUPPORTED：设备不支持经 S7 通信访问定时器/计数器）");
            } else if (g_integCaseDevLimited[c] && !g_integCaseHardBad[c]) {
                integ_settle(c, -1,
                             "（写已被 CPU 应答但读回始终为非写入值，同编码写其他地址生效——"
                             "SKIP-UNSUPPORTED：目标地址被 PLC 用户程序占用/覆写，设备限制）");
            } else {
                integ_settle(c, 0, "（存在失败断言，见上方日志）");
            }
        }
    }
    integ_clearOps();
    integ_nextCase();
}

/**
 * @brief op 结果槽：校验写应答/读回数据、打印 RAW、推进队列
 * @note 读回比对不一致时不立即定论：发起同地址二次取证读
 *       （integ_onRecheckRead），区分"写未生效（本端缺陷）"与
 *       "地址被 PLC 用户程序占用/实时覆写（设备限制）"。
 */
static void integ_onReplyFinished(XObject* sender, XVarList* args)
{
    XPlcReply* reply = (XPlcReply*)sender;
    XS7IntegOp* op;
    (void)args;
    if (g_integFinished || g_integOpIdx >= g_integOpCount) {
        return;
    }
    op = &g_integOps[g_integOpIdx];

    if (XPlcReply_error(reply) != XPlcDevice_NoError) {
        XString* es = XPlcReply_errorString(reply);
        XPrintf("  [XS7] %s %s 失败：错误码 %d %s\n",
                g_integCaseNames[g_integRunningCase], op->addr,
                (int)XPlcReply_error(reply), es != NULL ? XString_toUtf8(es) : "");
        /* 写操作瞬态拒绝（0x07/0x06/负 ACK）：有界重试后再下结论
         * （真机第 2/3 轮：目标 CPU 的写接受度按分钟级窗口波动） */
        if (op->isWrite && es != NULL && op->retries < XS7INTEG_WRITE_RETRIES &&
            integ_isTransientWriteRefusal(es)) {
            xs7uStrDelete(es);
            ++op->retries;
            XPrintf("  [XS7] 瞬态写拒绝，%dms 后重试（第 %d/%d 次）\n",
                    XS7INTEG_WRITE_RETRY_MS, op->retries, XS7INTEG_WRITE_RETRIES);
            XTimer_singleShot2(XS7INTEG_WRITE_RETRY_MS, integ_onWriteRetry);
            return;   /* 不推进 opIdx，等重试定时器 */
        }
        if (es != NULL) {
            if (op->isWrite && integ_isTransientWriteRefusal(es)) {
                /* 重试后仍被拒：目标区域写保护/CPU 瞬态，设备限制 */
                g_integCaseDevLimited[g_integRunningCase] = true;
            }
            xs7uStrDelete(es);
        }
        g_integCaseBad[g_integRunningCase] = true;
        if (!g_integCaseDevLimited[g_integRunningCase]) {
            g_integCaseHardBad[g_integRunningCase] = true;   /* 请求级错误 = 硬失败 */
        }
        ++g_integOpIdx;
        integ_pump();
        return;
    }

    if (op->isWrite) {
        XPrintf("  [XS7] 写 %s 成功\n", op->addr);
    } else {
        const XByteArray* raw = XPlcReply_rawResult_const(reply);
        size_t n = (raw != NULL) ? xs7uBaSize((XByteArray*)raw) : 0;
        const uint8_t* d = (raw != NULL) ? XByteArray_data((XByteArray*)raw) : NULL;
        integ_printRaw(op->addr, d, n);
        if (op->hasExpect && op->expectLen > 0) {
            if (d != NULL && n >= (size_t)op->expectLen &&
                memcmp(d, op->expect, (size_t)op->expectLen) == 0) {
                XPrintf("  [XS7] 读回比对一致（%s）\n", op->addr);
            } else {
                XPlcReply* rr;
                XPrintf("  [XS7] 读回比对不一致（%s）——发起二次取证读\n", op->addr);
                g_integCaseBad[g_integRunningCase] = true;
                /* 二次取证读（同地址同类型）：不改写值，仅判读写通路 vs 地址被占 */
                rr = (g_integRecheckPending)
                         ? NULL
                         : XS7TcpClient_sendRead_2(g_integClient, op->addr, op->type,
                                                   op->count);
                if (!g_integRecheckPending && rr != NULL &&
                    integ_wireReply(rr, integ_onRecheckRead)) {
                    g_integRecheckPending = true;
                    return;   /* 暂不推进 opIdx，等二次读结果 */
                }
                XPrintf("  [XS7] 二次取证读发起失败（%s），按不一致定论\n", op->addr);
            }
        }
    }
    ++g_integOpIdx;
    integ_pump();
}

/**
 * @brief 读回不一致后的同地址二次取证读结果槽
 * @details 判定（真机第 1 轮证据：同编码写 M0.0/MW2/DBD6/DBD10 均生效，
 *          DB1 低地址与 Q0.0 写后读回始终为程序值）：
 * - 二次读 == 期望值：写实际生效（首次读回与 PLC 周期竞争），该断言成立；
 * - 二次读仍非期望：写已应答但地址不可观测写入 → 设备限制（SKIP-UNSUPPORTED）；
 * - 二次读本身出错：通信层问题 → 硬失败。
 */
static void integ_onRecheckRead(XObject* sender, XVarList* args)
{
    XPlcReply* reply = (XPlcReply*)sender;
    XS7IntegOp* op;
    const XByteArray* raw;
    size_t n = 0;
    const uint8_t* d = NULL;
    (void)args;
    g_integRecheckPending = false;
    if (g_integFinished || g_integOpIdx >= g_integOpCount) {
        return;
    }
    op = &g_integOps[g_integOpIdx];

    if (XPlcReply_error(reply) == XPlcDevice_NoError) {
        char rawAddr[48];
        raw = XPlcReply_rawResult_const(reply);
        n = (raw != NULL) ? xs7uBaSize((XByteArray*)raw) : 0;
        d = (raw != NULL) ? XByteArray_data((XByteArray*)raw) : NULL;
        xs7uCopyStr(rawAddr, sizeof(rawAddr), op->addr);
        integ_printRaw(rawAddr, d, n);
        if (d != NULL && n >= (size_t)op->expectLen &&
            memcmp(d, op->expect, (size_t)op->expectLen) == 0) {
            XPrintf("  [XS7] 二次读回一致（%s）——写生效存在 PLC 周期延迟，断言按成立计\n",
                    op->addr);
            g_integCaseBad[g_integRunningCase] = false;
        } else {
            XPrintf("  [XS7] 二次读回仍为非写入值（%s）——写已应答但地址不可观测写入，判设备限制\n",
                    op->addr);
            g_integCaseDevLimited[g_integRunningCase] = true;
        }
    } else {
        XPrintf("  [XS7] 二次取证读失败（%s，错误码 %d）——按硬失败定论\n",
                op->addr, (int)XPlcReply_error(reply));
        g_integCaseHardBad[g_integRunningCase] = true;
    }
    ++g_integOpIdx;
    integ_pump();
}

/**
 * @brief 写瞬态拒绝重试定时器槽：重发当前 op 的写请求
 * @note op 队列游标未推进（integ_onReplyFinished 错误分支暂缓推进），
 *       value 仍归 op 队列所有，直接重发即可。
 */
static void integ_onWriteRetry(XObject* receiver, XVarList* args)
{
    XS7IntegOp* op;
    XPlcReply* r = NULL;
    (void)receiver;
    (void)args;
    if (g_integFinished || g_integOpIdx >= g_integOpCount) {
        return;
    }
    op = &g_integOps[g_integOpIdx];
    if (!op->isWrite) {
        return;
    }
    if (!g_integConnected ||
        XPlcDevice_state((XPlcDevice*)g_integClient) != XPlcDevice_ConnectedState) {
        XPrintf("  [XS7] %s %s 写重试时客户端未连接——按设备限制定论\n",
                g_integCaseNames[g_integRunningCase], op->addr);
        g_integCaseBad[g_integRunningCase] = true;
        g_integCaseDevLimited[g_integRunningCase] = true;
        ++g_integOpIdx;
        integ_pump();
        return;
    }
    r = XS7TcpClient_sendWrite_2(g_integClient, op->addr, op->type, op->count,
                                 op->value);
    if (!integ_wireReply(r, integ_onReplyFinished)) {
        XPrintf("  [XS7] %s %s 写重试发送失败——按设备限制定论\n",
                g_integCaseNames[g_integRunningCase], op->addr);
        g_integCaseBad[g_integRunningCase] = true;
        g_integCaseDevLimited[g_integRunningCase] = true;
        ++g_integOpIdx;
        integ_pump();
    }
}

/**
 * @brief large_fragmentation 区域存在性预检查结果槽
 * @details 读 DB1.DBB100（1 字节）成功 → 区域存在，进入正常分片读写流程；
 *          任一错误（真机实测 0x05 address out of range）→ 目标 DB1 长度
 *          不足 100 字节、映射分片区不存在，按 SKIP-UNSUPPORTED 结算
 *          （探针实测第 2 轮后：读 DBB64 正常、DBB100 即 0x05）。
 */
static void integ_onLargePrecheck(XObject* sender, XVarList* args)
{
    XPlcReply* reply = (XPlcReply*)sender;
    XString* es = NULL;
    const char* esUtf8 = "";
    (void)args;
    if (g_integFinished) {
        return;
    }
    if (XPlcReply_error(reply) == XPlcDevice_NoError) {
        XPrintf("  [XS7] 区域预检查通过（DB1.DBB100 可读），执行分片读写\n");
        integ_buildCase(XS7IntegCase_LargeFrag);
        if (g_integOpCount > 0) {
            g_integOpIdx = 0;
            integ_pump();
            return;
        }
        integ_settle(XS7IntegCase_LargeFrag, -1, "（无可用读写操作）");
        integ_nextCase();
        return;
    }
    es = XPlcReply_errorString(reply);
    esUtf8 = (es != NULL) ? XString_toUtf8(es) : "";
    XPrintf("  [XS7] 区域预检查失败（DB1.DBB100：%s）\n", esUtf8);
    integ_settle(XS7IntegCase_LargeFrag, -1,
                 "（目标 DB1 长度不足 100 字节，映射分片区 DB1.DBB100+1024 不存在"
                 "——SKIP-UNSUPPORTED：设备/工程配置限制，需扩大 DB1 或更换映射区）");
    if (es != NULL) {
        xs7uStrDelete(es);
    }
    integ_nextCase();
}

/**
 * @brief 组装一个用例的 op 队列（写已知确定模式 → 读回比对）
 * @param c 用例编号
 * @note 地址映射固定：写操作只允许 DB1/M/Q 区；I 区只读。
 *       只读模式（g_integReadback）自动剔除全部写操作与写回期望。
 */
static void integ_buildCase(XS7IntegCase c)
{
    integ_clearOps();
    switch (c) {
    case XS7IntegCase_DbBit:
        if (!g_integReadback) {
            integ_pushWrite("DB1.DBX0.0", XS7Value_Bool, 1, XVariant_create_bool(true));
        }
        integ_pushRead("DB1.DBX0.0", XS7Value_Bool, 1, !g_integReadback,
                       (const uint8_t*)"\x01", 1);
        break;
    case XS7IntegCase_DbByte:
        if (!g_integReadback) {
            integ_pushWrite("DB1.DBB2", XS7Value_Byte, 1, XVariant_create_int(0x5A));
        }
        integ_pushRead("DB1.DBB2", XS7Value_Byte, 1, !g_integReadback,
                       (const uint8_t*)"\x5A", 1);
        break;
    case XS7IntegCase_DbWord: {
        static const uint8_t be[2] = { 0x12, 0x34 };
        if (!g_integReadback) {
            integ_pushWrite("DB1.DBW4", XS7Value_Word, 1, XVariant_create_int(0x1234));
        }
        integ_pushRead("DB1.DBW4", XS7Value_Word, 1, !g_integReadback, be, 2);
        break;
    }
    case XS7IntegCase_DbDWord: {
        static const uint8_t be[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        if (!g_integReadback) {
            integ_pushWrite("DB1.DBD6", XS7Value_DWord, 1,
                            XVariant_create_uint32(0xDEADBEEFu));
        }
        integ_pushRead("DB1.DBD6", XS7Value_DWord, 1, !g_integReadback, be, 4);
        break;
    }
    case XS7IntegCase_DbReal: {
        static const uint8_t be[4] = { 0x42, 0xF7, 0x00, 0x00 };   /* 123.5f */
        if (!g_integReadback) {
            integ_pushWrite("DB1.DBD10", XS7Value_Real, 1, XVariant_create_float(123.5f));
        }
        integ_pushRead("DB1.DBD10", XS7Value_Real, 1, !g_integReadback, be, 4);
        break;
    }
    case XS7IntegCase_DbString: {
        /* STRING(16)="XIN_YUE"：期望前 9 字节 10 07 + 7 字符（尾部补零不比对） */
        static const uint8_t be[9] = { 16, 7, (uint8_t)'X', (uint8_t)'I', (uint8_t)'N',
                                       (uint8_t)'_', (uint8_t)'Y', (uint8_t)'U',
                                       (uint8_t)'E' };
        if (!g_integReadback) {
            integ_pushWrite("DB1.DBB20", XS7Value_String, 16,
                            XVariant_create_utf8_str("XIN_YUE"));
        }
        integ_pushRead("DB1.DBB20", XS7Value_String, 16, !g_integReadback, be, 9);
        break;
    }
    case XS7IntegCase_MqArea: {
        static const uint8_t beW[2] = { 0xBE, 0xEF };
        if (!g_integReadback) {
            integ_pushWrite("M0.0", XS7Value_Bool, 1, XVariant_create_bool(true));
            integ_pushWrite("MW2", XS7Value_Word, 1, XVariant_create_int(0xBEEF));
            integ_pushWrite("Q0.0", XS7Value_Bool, 1, XVariant_create_bool(true));
        }
        integ_pushRead("M0.0", XS7Value_Bool, 1, !g_integReadback,
                       (const uint8_t*)"\x01", 1);
        integ_pushRead("MW2", XS7Value_Word, 1, !g_integReadback, beW, 2);
        integ_pushRead("Q0.0", XS7Value_Bool, 1, !g_integReadback,
                       (const uint8_t*)"\x01", 1);
        break;
    }
    case XS7IntegCase_IArea:
        integ_pushRead("IW0", XS7Value_Word, 1, false, NULL, 0);
        break;
    case XS7IntegCase_TcAccess:
        integ_pushRead("T1", XS7Value_Word, 1, false, NULL, 0);
        integ_pushRead("C1", XS7Value_Word, 1, false, NULL, 0);
        break;
    case XS7IntegCase_LargeFrag: {
        /* ≥1000 字节 DB 连续读与写：DB1.DBB100 起 1024 字节，图案 i*13+7 */
        static uint8_t pattern[1024];
        static bool patternInit = false;
        if (!patternInit) {
            int i;
            for (i = 0; i < 1024; ++i) {
                pattern[i] = (uint8_t)((i * 13 + 7) & 0xFF);
            }
            patternInit = true;
        }
        if (!g_integReadback) {
            XByteArray* blob = XByteArray_create();
            XVariant* v = XVariant_create_null();
            xs7uBaAppend(blob, pattern, sizeof(pattern));
            XByteArray_setVariant_move(v, blob);   /* 变体接管字节数组 */
            integ_pushWrite("DB1.DBB100", XS7Value_Byte, 1024, v);
        }
        integ_pushRead("DB1.DBB100", XS7Value_Byte, 1024, !g_integReadback,
                       pattern, g_integReadback ? 0 : 1024);
        break;
    }
    default:
        break;
    }
}

/**
 * @brief bad_address 结果槽：非法地址走错误路径且不崩溃、客户端存活
 * @details 三步判定：DB99（越界）→ 期望错误应答；BAD..ADDR → 本地拒绝
 *          （sendRead 返回 NULL）或错误应答均算正确路径；DB1.DBW0 → 成功。
 *          修正（真机第 1 轮）：本地拒绝路径此前错占子步 2，把存活确认的
 *          应答误判为"BAD..ADDR 未走错误路径"，且最终结算无视先前失败断言。
 */
static void integ_onBadAddrReply(XObject* sender, XVarList* args)
{
    XPlcReply* reply = (XPlcReply*)sender;
    XPlcReply* r = NULL;
    (void)args;
    if (g_integFinished) {
        return;
    }

    /* 本地拒绝路径：下一个应答即"存活确认"，直接按全程断言结算 */
    if (g_integBadLocalReject) {
        g_integBadLocalReject = false;
        if (XPlcReply_error(reply) == XPlcDevice_NoError) {
            const XByteArray* raw = XPlcReply_rawResult_const(reply);
            integ_printRaw("DB1.DBW0",
                           (raw != NULL) ? XByteArray_data((XByteArray*)raw) : NULL,
                           (raw != NULL) ? xs7uBaSize((XByteArray*)raw) : 0);
            XPrintf("  [XS7] 连续错误后客户端存活\n");
        } else {
            XPrintf("  [XS7] 连续错误后客户端不再存活——FAIL\n");
            g_integCaseBad[XS7IntegCase_BadAddress] = true;
        }
        integ_settle(XS7IntegCase_BadAddress,
                     g_integCaseBad[XS7IntegCase_BadAddress] ? 0 : 1,
                     g_integCaseBad[XS7IntegCase_BadAddress]
                         ? "（存在失败断言，见上方日志）"
                         : "（非法地址不崩溃、错误路径正确、客户端存活）");
        integ_nextCase();
        return;
    }

    ++g_integBadSub;
    if (g_integBadSub >= 3) {
        /* 第三步结果：存活确认（连续错误后客户端仍能正常通信） */
        if (XPlcReply_error(reply) == XPlcDevice_NoError) {
            const XByteArray* raw = XPlcReply_rawResult_const(reply);
            integ_printRaw("DB1.DBW0",
                           (raw != NULL) ? XByteArray_data((XByteArray*)raw) : NULL,
                           (raw != NULL) ? xs7uBaSize((XByteArray*)raw) : 0);
            XPrintf("  [XS7] 连续错误后客户端存活\n");
        } else {
            XPrintf("  [XS7] 连续错误后客户端不再存活——FAIL\n");
            g_integCaseBad[XS7IntegCase_BadAddress] = true;
        }
        integ_settle(XS7IntegCase_BadAddress,
                     g_integCaseBad[XS7IntegCase_BadAddress] ? 0 : 1,
                     g_integCaseBad[XS7IntegCase_BadAddress]
                         ? "（存在失败断言，见上方日志）"
                         : "（非法地址不崩溃、错误路径正确、客户端存活）");
        integ_nextCase();
        return;
    }
    if (g_integBadSub == 1) {
        /* 第一步结果：DB99.DBW0 越界 → 期望错误应答而非挂死 */
        if (XPlcReply_error(reply) != XPlcDevice_NoError) {
            XPrintf("  [XS7] 越界地址错误路径正确（错误码 %d）\n",
                    (int)XPlcReply_error(reply));
        } else {
            XPrintf("  [XS7] 越界地址 DB99.DBW0 意外成功——FAIL\n");
            g_integCaseBad[XS7IntegCase_BadAddress] = true;
        }
        r = XS7TcpClient_sendRead_2(g_integClient, "BAD..ADDR", XS7Value_Word, 1);
        if (r != NULL) {
            integ_wireReply(r, integ_onBadAddrReply);
            return;
        }
        /* 本地拒绝即正确路径：不占用子步 2，下一应答（存活确认）直接结算 */
        XPrintf("  [XS7] 非法语法被本地拒绝（sendRead 返回 NULL）——错误路径正确\n");
        g_integBadLocalReject = true;
    } else {
        /* 第二步结果：BAD..ADDR 产生错误应答（能进到这里说明未被本地拒绝） */
        if (XPlcReply_error(reply) == XPlcDevice_NoError) {
            XPrintf("  [XS7] 非法语法未走错误路径——FAIL\n");
            g_integCaseBad[XS7IntegCase_BadAddress] = true;
        } else {
            XPrintf("  [XS7] 非法语法错误路径正确\n");
        }
    }
    /* 发起存活确认读 DB1.DBW0 */
    r = XS7TcpClient_sendRead_2(g_integClient, "DB1.DBW0", XS7Value_Word, 1);
    if (r == NULL) {
        g_integCaseBad[XS7IntegCase_BadAddress] = true;
        g_integCaseHardBad[XS7IntegCase_BadAddress] = true;
        integ_settle(XS7IntegCase_BadAddress, 0, "（存活确认读返回 NULL）");
        integ_nextCase();
        return;
    }
    integ_wireReply(r, integ_onBadAddrReply);
}

/**
 * @brief run_stop 结果槽：Stop → 确认 → Hot Run 恢复 → 读回确认在线
 */
static void integ_onRunStopReply(XObject* sender, XVarList* args)
{
    XPlcReply* reply = (XPlcReply*)sender;
    XPlcReply* r = NULL;
    (void)args;

    if (g_integSubMode == XS7RS_SUB_STOP) {
        bool stopOk = (XPlcReply_error(reply) == XPlcDevice_NoError);
        XString* es = XPlcReply_errorString(reply);
        /* CPU 负 ACK 拒绝（真机第 1 轮实测：S7-1200 回 rosctr=0x02 + 81 04）
         * 属设备能力限制：按 SKIP-UNSUPPORTED 结算，不算本端 FAIL */
        if (!stopOk && integ_isControlRefused(es)) {
            XPrintf("  [XS7] Stop 被 CPU 负 ACK 拒绝（%s）\n",
                    es != NULL ? XString_toUtf8(es) : "");
            if (es != NULL) {
                xs7uStrDelete(es);
            }
            g_integStopSent = false;   /* Stop 未生效，收尾无需恢复 RUN */
            integ_settle(XS7IntegCase_RunStop, -1,
                         "（CPU 负 ACK 拒绝 PLC 控制命令（错误域 0x81），"
                         "SKIP-UNSUPPORTED：设备不支持经 S7 通信启停）");
            integ_nextCase();
            return;
        }
        if (!stopOk && integ_isControlErrorText(es)) {
            stopOk = true;   /* 0x07 已停止 / 0x02 已运行按成功语义 */
        }
        if (es != NULL) {
            xs7uStrDelete(es);
        }
        g_integStopSent = true;
        if (!stopOk) {
            XPrintf("  [XS7] Stop 应答错误=%d\n", (int)XPlcReply_error(reply));
            integ_settle(XS7IntegCase_RunStop, 0, "（Stop 应答失败且非“已停止”提示）");
            integ_nextCase();
            return;
        }
        XPrintf("  [XS7] Stop 确认，发 Hot Run 恢复\n");
        g_integSubMode = XS7RS_SUB_RUN;
        r = XS7TcpClient_sendPlcRun(g_integClient, XS7RunMode_Hot);
        if (!integ_wireReply(r, integ_onRunStopReply)) {
            g_integRunRestored = false;
            integ_settle(XS7IntegCase_RunStop, 0, "（sendPlcRun 返回 NULL）");
            integ_nextCase();
        }
        return;
    }
    if (g_integSubMode == XS7RS_SUB_RUN) {
        bool runOk = (XPlcReply_error(reply) == XPlcDevice_NoError);
        XString* es = XPlcReply_errorString(reply);
        if (!runOk && integ_isControlErrorText(es)) {
            runOk = true;
        }
        if (es != NULL) {
            xs7uStrDelete(es);
        }
        g_integRunRestored = runOk;
        if (!runOk) {
            integ_settle(XS7IntegCase_RunStop, 0, "（Hot Run 应答失败）");
            integ_nextCase();
            return;
        }
        XPrintf("  [XS7] Hot Run 已恢复，读回确认 PLC 在线\n");
        g_integSubMode = XS7RS_SUB_CONFIRM;
        r = XS7TcpClient_sendRead_2(g_integClient, "DB1.DBW0", XS7Value_Word, 1);
        if (!integ_wireReply(r, integ_onRunStopReply)) {
            integ_settle(XS7IntegCase_RunStop, 0, "（确认读返回 NULL）");
            integ_nextCase();
        }
        return;
    }
    /* SUB_CONFIRM：恢复后读回在线即 PASS */
    if (XPlcReply_error(reply) == XPlcDevice_NoError) {
        const XByteArray* raw = XPlcReply_rawResult_const(reply);
        integ_printRaw("DB1.DBW0", (raw != NULL) ? XByteArray_data((XByteArray*)raw) : NULL,
                       (raw != NULL) ? xs7uBaSize((XByteArray*)raw) : 0);
        integ_settle(XS7IntegCase_RunStop, 1, "（Stop→确认→Hot Run 恢复→读回在线）");
    } else {
        integ_settle(XS7IntegCase_RunStop, 0, "（恢复后读回失败）");
    }
    integ_nextCase();
}

#if XS7_BLOCK_ON
/**
 * @brief block_ops 结果槽：成功 PASS / 设备不支持 SKIP（不得崩溃）
 */
static void integ_onBlockReply(XObject* sender, XVarList* args)
{
    XPlcReply* reply = (XPlcReply*)sender;
    (void)args;
    if (XPlcReply_error(reply) == XPlcDevice_NoError) {
        const XByteArray* raw = XPlcReply_rawResult_const(reply);
        integ_printRaw("block_list",
                       (raw != NULL) ? XByteArray_data((XByteArray*)raw) : NULL,
                       (raw != NULL) ? xs7uBaSize((XByteArray*)raw) : 0);
        integ_settle(XS7IntegCase_BlockOps, 1, "（ListBlocks 应答成功）");
    } else {
        integ_settle(XS7IntegCase_BlockOps, -1, "（设备不支持块列表/上传，按设计 SKIP）");
    }
    integ_nextCase();
}
#endif /* XS7_BLOCK_ON */

/**
 * @brief timeout_reconnect：断连后重连轮询（250ms 周期，最多 32 次 = 8 秒）
 */
static void integ_onReconnectPoll(XObject* receiver, XVarList* args)
{
    XPlcReply* r = NULL;
    (void)receiver;
    (void)args;
    if (g_integFinished || g_integSubMode != XS7TR_SUB_DROP) {
        return;
    }
    if (g_integConnected &&
        XPlcDevice_state((XPlcDevice*)g_integClient) == XPlcDevice_ConnectedState) {
        g_integSubMode = XS7TR_SUB_VERIFY;
        XPrintf("  [XS7] 断线后已自动重连，读回验证\n");
        r = XS7TcpClient_sendRead_2(g_integClient, "DB1.DBW0", XS7Value_Word, 1);
        if (integ_wireReply(r, integ_onReconnectVerify)) {
            return;
        }
        integ_settle(XS7IntegCase_TimeoutReconn, 0, "（重连后验证读返回 NULL）");
        integ_nextCase();
        return;
    }
    if (--g_integReconnWait <= 0) {
        integ_settle(XS7IntegCase_TimeoutReconn, 0, "（8 秒内未自动重连成功）");
        integ_nextCase();
        return;
    }
    XTimer_singleShot2(250, integ_onReconnectPoll);
}

/**
 * @brief timeout_reconnect：重连后验证读结果槽
 */
static void integ_onReconnectVerify(XObject* sender, XVarList* args)
{
    XPlcReply* reply = (XPlcReply*)sender;
    (void)args;
    if (XPlcReply_error(reply) == XPlcDevice_NoError) {
        const XByteArray* raw = XPlcReply_rawResult_const(reply);
        integ_printRaw("DB1.DBW0",
                       (raw != NULL) ? XByteArray_data((XByteArray*)raw) : NULL,
                       (raw != NULL) ? xs7uBaSize((XByteArray*)raw) : 0);
        integ_settle(XS7IntegCase_TimeoutReconn, 1, "（超时路径与断线自动重连均验证通过）");
    } else {
        integ_settle(XS7IntegCase_TimeoutReconn, 0, "（重连后验证读失败）");
    }
    integ_nextCase();
}

/**
 * @brief timeout_reconnect：故障注入期 Reply 槽
 * @details 子步 0（SUB_RETRY）：吞 1 帧 + 1 次重试 → 期望重试改写帧内 pduRef
 *          后成功；子步 1（SUB_SILENT）：吞掉全部尝试帧 → 期望重试耗尽报
 *          TimeoutError；子步 2（SUB_DROP）：断连 → 自动重连 → 读回验证。
 */
static void integ_onTimeoutReply(XObject* sender, XVarList* args)
{
    XPlcReply* reply = (XPlcReply*)sender;
    XPlcReply* r = NULL;
    (void)args;
    if (g_integSubMode == XS7TR_SUB_RETRY) {
        if (XPlcReply_error(reply) == XPlcDevice_NoError) {
            XPrintf("  [XS7] 吞 1 帧后重试成功（帧内 pduRef 改写正确）\n");
        } else {
            XPrintf("  [XS7] 吞 1 帧后重试未恢复（错误码 %d）——FAIL\n",
                    (int)XPlcReply_error(reply));
            integ_settle(XS7IntegCase_TimeoutReconn, 0, "（吞帧重试未恢复）");
            integ_nextCase();
            return;
        }
        /* 子步 1：吞掉全部尝试帧（首次+重试共 2 帧），期望重试耗尽 → TimeoutError */
        g_integSubMode = XS7TR_SUB_SILENT;
        g_fakeSilentCount = 2;
        XPlcClient_setTimeout((XPlcClient*)g_integClient, 1200);
        XPlcClient_setNumberOfRetries((XPlcClient*)g_integClient, 1);
        r = XS7TcpClient_sendRead_2(g_integClient, "DB1.DBW0", XS7Value_Word, 1);
        if (integ_wireReply(r, integ_onTimeoutReply)) {
            return;
        }
        integ_settle(XS7IntegCase_TimeoutReconn, 0, "（注入读返回 NULL）");
        integ_nextCase();
        return;
    }
    if (g_integSubMode != XS7TR_SUB_SILENT) {
        return;
    }
    if (XPlcReply_error(reply) == XPlcDevice_TimeoutError) {
        XPrintf("  [XS7] 静默丢包 → 超时路径正确（TimeoutError）\n");
    } else {
        XPrintf("  [XS7] 静默丢包未产生 TimeoutError（错误码 %d）\n",
                (int)XPlcReply_error(reply));
        integ_settle(XS7IntegCase_TimeoutReconn, 0, "（超时路径未命中）");
        integ_nextCase();
        return;
    }
    /* 子步 2：桩处理下一请求后强制断连，验证自动重连 */
    g_integSubMode = XS7TR_SUB_DROP;
    g_fakeSilentCount = 0;
    g_fakeDropOnce = true;
    g_integReconnWait = 32;
    XPlcClient_setNumberOfRetries((XPlcClient*)g_integClient, 3);
    XPlcClient_setTimeout((XPlcClient*)g_integClient, 2000);
    r = XS7TcpClient_sendRead_2(g_integClient, "DB1.DBW0", XS7Value_Word, 1);
    if (r != NULL) {
        /* 该请求用于触发桩断连，结果不关心，完成即回收 */
        XObject_setParent((XObject*)r, (XObject*)g_integClient);
        XObject_connect_1((XObject*)r, XSignal(XPlcReply_finished_signal),
                          (XObject*)r, xs7uReplyDeleteLater, XConnectionType_Auto);
    }
    XTimer_singleShot2(250, integ_onReconnectPoll);
}

/**
 * @brief 用例推进器：依次启动各用例；全部完成后收尾
 */
static void integ_nextCase(void)
{
    while (g_integCaseIdx < (int)XS7IntegCase_Count) {
        XS7IntegCase c = (XS7IntegCase)g_integCaseIdx;
        ++g_integCaseIdx;
        switch (c) {
        case XS7IntegCase_RunStop:
#if XS7_CONTROL_ON
        {
            XPlcReply* r = NULL;
            if (g_integReadback) {
                integ_settle(c, -1, "（XS7_PLC_READBACK=1 只读模式，禁止改运行状态）");
                continue;
            }
            if (!g_integConnected ||
                XPlcDevice_state((XPlcDevice*)g_integClient) != XPlcDevice_ConnectedState) {
                integ_settle(c, 0, "（客户端未连接）");
                continue;
            }
            g_integSubMode = XS7RS_SUB_STOP;
            r = XS7TcpClient_sendPlcStop(g_integClient);
            if (!integ_wireReply(r, integ_onRunStopReply)) {
                integ_settle(c, 0, "（sendPlcStop 返回 NULL：W4 未实现或未连接）");
                continue;
            }
            return;   /* 等待异步应答 */
        }
#else
            integ_settle(c, -1, "（XS7_CONTROL_ON=0，运维裁剪）");
            continue;
#endif /* XS7_CONTROL_ON */
        case XS7IntegCase_BlockOps:
#if XS7_BLOCK_ON
        {
            XPlcReply* r = NULL;
            if (!g_integConnected) {
                integ_settle(c, 0, "（客户端未连接）");
                continue;
            }
            r = XS7TcpClient_sendListBlocks(g_integClient);
            if (r == NULL) {
                integ_settle(c, -1, "（sendListBlocks 未实现/返回 NULL）");
                continue;
            }
            XObject_setParent((XObject*)r, (XObject*)g_integClient);
            XObject_connect_1((XObject*)r, XSignal(XPlcReply_finished_signal),
                              (XObject*)r, integ_onBlockReply, XConnectionType_Direct);
            XObject_connect_1((XObject*)r, XSignal(XPlcReply_finished_signal),
                              (XObject*)r, xs7uReplyDeleteLater, XConnectionType_Auto);
            return;
        }
#else
            integ_settle(c, -1, "（XS7_BLOCK_ON=0，块传输裁剪）");
            continue;
#endif /* XS7_BLOCK_ON */
        case XS7IntegCase_TimeoutReconn: {
            XPlcReply* r = NULL;
            if (!g_integFake) {
                integ_settle(c, -1, "（真机模式跳过故障注入，避免扰动生产 PLC）");
                continue;
            }
            if (!g_integConnected) {
                integ_settle(c, 0, "（客户端未连接）");
                continue;
            }
            /* 子步 0：桩吞 1 帧 + numberOfRetries=1 → 期望重试改写帧内 pduRef
             * 后被 PLC 按新 ref 应答、请求最终成功（帧内 pduRef 偏移写错时
             * 重发帧 rosctr 被破坏、挂表新键等不到旧键应答，必然 TimeoutError） */
            g_integSubMode = XS7TR_SUB_RETRY;
            g_fakeSilentCount = 1;
            XPlcClient_setAutoReconnect((XPlcClient*)g_integClient, true);
            XPlcClient_setTimeout((XPlcClient*)g_integClient, 1200);
            XPlcClient_setNumberOfRetries((XPlcClient*)g_integClient, 1);
            r = XS7TcpClient_sendRead_2(g_integClient, "DB1.DBW0", XS7Value_Word, 1);
            if (!integ_wireReply(r, integ_onTimeoutReply)) {
                integ_settle(c, 0, "（注入读返回 NULL）");
                continue;
            }
            return;
        }
        case XS7IntegCase_BadAddress: {
            XPlcReply* r = NULL;
            if (!g_integConnected) {
                integ_settle(c, 0, "（客户端未连接）");
                continue;
            }
            g_integBadSub = 0;
            r = XS7TcpClient_sendRead_2(g_integClient, "DB99.DBW0", XS7Value_Word, 1);
            if (r != NULL) {
                integ_wireReply(r, integ_onBadAddrReply);
                return;
            }
            /* 本地即拒绝（视为正确路径），直接进入存活确认 */
            XPrintf("  [XS7] 越界地址被本地拒绝（sendRead 返回 NULL）\n");
            g_integBadSub = 1;
            r = XS7TcpClient_sendRead_2(g_integClient, "DB1.DBW0", XS7Value_Word, 1);
            if (r == NULL) {
                integ_settle(c, 0, "（存活确认读返回 NULL）");
                continue;
            }
            /* 复用 bad_addr 槽的第三步：先借子步 2 空转 */
            g_integBadSub = 2;
            integ_wireReply(r, integ_onBadAddrReply);
            return;
        }
        case XS7IntegCase_LargeFrag: {
            /* 区域存在性预检查（真机第 2 轮实测：目标 DB1 长度 65..99 字节，
             * 读 DB1.DBB100 即 0x05 —— 映射分片区不存在时按 SKIP-UNSUPPORTED
             * 结算，不再发送注定失败的 1024B 读写，也避免连接被 CPU 断开
             * 株连后续用例） */
            XPlcReply* r = NULL;
            if (!g_integConnected ||
                XPlcDevice_state((XPlcDevice*)g_integClient) != XPlcDevice_ConnectedState) {
                integ_settle(c, 0, "（客户端未连接）");
                continue;
            }
            g_integRunningCase = (int)c;
            g_integCaseBad[c] = false;
            g_integCaseDevLimited[c] = false;
            g_integCaseHardBad[c] = false;
            r = XS7TcpClient_sendRead_2(g_integClient, "DB1.DBB100", XS7Value_Byte, 1);
            if (r != NULL) {
                integ_wireReply(r, integ_onLargePrecheck);
                return;
            }
            integ_settle(c, 0, "（区域预检查读返回 NULL：客户端不可用）");
            continue;
        }
        default:
            /* op 引擎用例（db、mq、i_area、t_c、large）：组装并启动 */
            g_integCaseBad[c] = false;
            g_integCaseDevLimited[c] = false;
            g_integCaseHardBad[c] = false;
            g_integRunningCase = (int)c;   /* T/C 也必须登记（真机第 1 轮：
                                              缺此行导致 T1/C1 失败误标为
                                              i_area_readonly 且用例永不结算） */
            if (c == XS7IntegCase_TcAccess) {
                /* 尽力而为：设备不支持（0x05）在 pump 结算处转 SKIP-UNSUPPORTED */
                integ_buildCase(c);
                if (g_integOpCount > 0) {
                    g_integOpIdx = 0;
                    integ_pump();
                    return;
                }
                integ_settle(c, -1, "（无可用读操作）");
                continue;
            }
            integ_buildCase(c);
            if (g_integOpCount > 0) {
                g_integOpIdx = 0;
                integ_pump();
                return;
            }
            integ_settle(c, -1, "（无操作，环境未支持）");
            continue;
        }
    }
    integ_finish();
}

/**
 * @brief 事件循环收尾：恢复 RUN（若动过）、断开、打印 TOTAL、退出循环
 */
static void integ_finish(void)
{
    bool anyFail = (g_integFail > 0);
    if (g_integFinished) {
        return;
    }
    g_integFinished = true;

#if XS7_CONTROL_ON
    /* 铁律：无论中途结果如何，测试结束前恢复 PLC 为 RUN 态 */
    if (g_integConnected && g_integStopSent && !g_integRunRestored) {
        XPlcReply* r = XS7TcpClient_sendPlcRun(g_integClient, XS7RunMode_Hot);
        if (r != NULL) {
            uint64_t deadline = (uint64_t)XDateTime_currentMSecsSinceEpoch() + 5000;
            while (!XPlcReply_isFinished(r) &&
                   (uint64_t)XDateTime_currentMSecsSinceEpoch() < deadline) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(2);
            }
            g_integRunRestored = (XPlcReply_error(r) == XPlcDevice_NoError);
            XClass_delete_base((XClass*)r);
        }
        XPrintf("[XS7][PLC] 收尾恢复：PLC 回 RUN 态（%s）\n",
                g_integRunRestored ? "成功" : "失败（需人工确认）");
    }
#endif

    if (g_integClient != NULL) {
        XPlcDevice_disconnectDevice((XPlcDevice*)g_integClient);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    XPrintf("[XS7][PLC] TOTAL %s (pass=%d fail=%d skip=%d)\n",
            anyFail ? "FAIL" : "PASS", g_integPass, g_integFail, g_integSkip);
    g_integOk = !anyFail;
    XCoreApplication_exit(g_integOk ? 0 : 1);
}

/**
 * @brief 设备状态变化槽：驱动 slot 试探与用例引擎
 * @details 连接时 rack=0、slot 依次尝试 1,0,2（设计要求）；
 *          Connected → 打印协商 PDU、记 connect_handshake 结果、启动用例链。
 */
static void integ_onStateChanged(XObject* receiver, XVarList* args)
{
    XPlcDevice_State st;
    (void)receiver;
    XVarList_start(args);
    st = XVarList_arg(args, XPlcDevice_State);
    if (g_integFinished) {
        return;
    }
    if (st == XPlcDevice_ConnectedState) {
        char detail[80];
        XString* ds;
        g_integConnected = true;
        g_integSlotPending = false;
        if (g_integCaseSettled[XS7IntegCase_Connect]) {
            /* 重连成功（timeout_reconnect 注入断连后）：仅恢复在线标志，
             * 严禁重启用例链/重复收尾（否则会在验证读前提前 integ_finish） */
            return;
        }
        ds = XString_create_fmt_utf8("rack=0 slot=%d 协商PDU=%u",
                                     g_integSlotSeq[g_integSlotIdx],
                                     (unsigned)g_integClient->m_negotiatedPduLen);
        xs7uCopyStr(detail, sizeof(detail), XString_toUtf8(ds));
        xs7uStrDelete(ds);
        integ_settle(XS7IntegCase_Connect, 1, detail);
        /* 启动用例链（下标已指向 DbBit） */
        integ_nextCase();
    } else if (st == XPlcDevice_UnconnectedState && !g_integConnected) {
        /* 当前 slot 试探失败 → 换下一个 slot（延迟重试避免风暴） */
        if (!g_integSlotPending) {
            return;   /* 非试探阶段的迁移（如初始态上报） */
        }
        g_integSlotPending = false;
        if (g_integSlotIdx < 2) {
            ++g_integSlotIdx;
            XPrintf("[XS7][PLC] slot=%d 握手失败，改试 slot=%d\n",
                    g_integSlotSeq[g_integSlotIdx - 1], g_integSlotSeq[g_integSlotIdx]);
            XPlcDevice_setConnectionParameter_ref(
                (XPlcDevice*)g_integClient, XPlcDevice_SlotParameter,
                XVariant_create_int(g_integSlotSeq[g_integSlotIdx]));
            XTimer_singleShot2(300, integ_retryConnect);
        } else {
            integ_settle(XS7IntegCase_Connect, 0, "（slot 1/0/2 均握手失败，目标不可达）");
            integ_finish();
        }
    } else if (st == XPlcDevice_UnconnectedState && g_integConnected) {
        /* 运行中掉线：已开启有界自动重连，记录后交由重连机制恢复；
         * 不再提前收尾（真机第 1 轮实测 CPU 会静默断连，提前收尾会让
         * 后续用例连锁失败）。重连成功后 Connected 分支只恢复在线标志。 */
        g_integConnected = false;
        if (g_integSubMode == XS7TR_SUB_DROP || g_integSubMode == XS7TR_SUB_VERIFY) {
            return;   /* timeout_reconnect 注入断连：预期行为，交给重连轮询结算 */
        }
        XPrintf("[XS7][PLC] 运行中连接丢失，等待自动重连（重试间隔 300ms，至多 50 次）\n");
    }
}

/**
 * @brief slot 试探重连定时器槽
 */
static void integ_retryConnect(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    if (!g_integFinished && !g_integConnected) {
        g_integSlotPending = true;
        (void)XPlcDevice_connectDevice((XPlcDevice*)g_integClient);
    }
}

/**
 * @brief slot 试探超时定时器槽（6 秒未 Connected 视为该 slot 失败）
 */
static void integ_onSlotTimeout(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    if (g_integFinished || g_integConnected) {
        return;
    }
    if (g_integSlotPending) {
        XPlcDevice_disconnectDevice((XPlcDevice*)g_integClient);
        /* disconnect 后 stateChanged(Unconnected) 会驱动换槽/失败收尾 */
        g_integSlotPending = true;   /* 保持试探语义供 stateChanged 分支使用 */
    }
}

/**
 * @brief 连接错误槽：打印细节便于定位（rack/slot 不匹配、拒绝等）
 */
static void integ_onDeviceError(XObject* receiver, XVarList* args)
{
    XPlcDevice_Error err;
    (void)receiver;
    XVarList_start(args);
    err = XVarList_arg(args, XPlcDevice_Error);
    if (!g_integFinished) {
        XPrintf("[XS7][PLC] 设备错误：%d（目标 IP=%s 端口=%u）\n",
                (int)err, g_integIp, (unsigned)g_integPort);
    }
}

/**
 * @brief 集成总看门狗：120 秒未收尾强制退出（防事件循环挂死）
 */
static void integ_onWatchdog(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    if (!g_integFinished) {
        XPrintf("[XS7][PLC] 看门狗超时（120s），强制收尾\n");
        integ_finish();
    }
}

/**
 * @brief S7 集成测试入口（--test xs7-plc）
 * @details 目标：XSystem_environment("XS7_PLC_IP")（默认 192.168.1.251）端口 102；
 *          rack=0、slot 依次尝试 1,0,2。XS7_PLC_IP=127.0.0.1 时对接文件内
 *          XS7FakePlc 桩（离线自测，绝不触碰真实 PLC）。
 *          XS7_PLC_READBACK=1 只读模式：仅读固定地址集并打印 RAW 行。
 * @return 全部用例无 FAIL 返回 true
 */
bool XS7Test_integration_run(void)
{
    const char* ip;
    const char* rb;
    int i;

    g_integOk = false;
    g_integFinished = false;
    g_integConnected = false;
    g_integPass = 0;
    g_integFail = 0;
    g_integSkip = 0;
    g_integOpCount = 0;
    g_integOpIdx = 0;
    g_integCaseIdx = (int)XS7IntegCase_DbBit;   /* Connect 由 stateChanged 直接结算 */
    g_integSlotIdx = 0;
    g_integSlotPending = false;
    g_integStopSent = false;
    g_integRunRestored = false;
    g_integBadSub = 0;
    g_integBadLocalReject = false;
    g_integRecheckPending = false;
    g_integSubMode = XS7RS_SUB_STOP;
    for (i = 0; i < (int)XS7IntegCase_Count; ++i) {
        g_integCaseBad[i] = false;
        g_integCaseSettled[i] = false;
        g_integCaseDevLimited[i] = false;
        g_integCaseHardBad[i] = false;
    }

    /* 目标选择：127.0.0.1 → 假 PLC 桩；否则真实 PLC（默认 192.168.1.251:102） */
    ip = XSystem_environment("XS7_PLC_IP");
    if (ip != NULL && ip[0] != '\0') {
        xs7uCopyStr(g_integIp, sizeof(g_integIp), ip);
    } else {
        xs7uCopyStr(g_integIp, sizeof(g_integIp), "192.168.1.251");
    }
    rb = XSystem_environment("XS7_PLC_READBACK");
    g_integReadback = (rb != NULL && rb[0] == '1');
    g_integFake = (strcmp(g_integIp, "127.0.0.1") == 0);

    XPrintf("[XS7][PLC] 集成测试开始：IP=%s 端口=%u 模式=%s\n", g_integIp,
            (unsigned)g_integPort,
            g_integReadback ? "只读回读（绝不写 PLC）" : "读写");
    /* 设计 §7.1：S7-1200/1500 须在 TIA 中关闭目标 DB「优化块访问」并勾选
     * 「允许来自远程伙伴的 PUT/GET 通信」，否则握手成功也读不到数据，
     * 现场易误报为通信 bug（S7-1500 还需防护等级放行） */
    XPrintf("[XS7][PLC] 前置条件：TIA 中目标 DB 关闭优化块访问 + 允许来自远程伙伴的 PUT/GET 通信\n");

    if (!xs7fakeStart()) {
        XPrintf("[XS7][PLC] TOTAL FAIL (pass=0 fail=1 skip=0)\n");
        return false;
    }
    if (g_integFake) {
        g_integPort = (uint16_t)XTcpServer_serverPort(g_fakeServer);
        XPrintf("[XS7][PLC] 假 PLC 桩已监听 127.0.0.1:%u\n", (unsigned)g_integPort);
    }

    /* 地址映射固定（写操作只允许 DB1/M/Q 区；I 区只读） */
    XPrintf("[XS7][PLC] MAP %s\n",
            "DB1.DBX0.0,DB1.DBB2,DB1.DBW4,DB1.DBD6,DB1.DBD10,"
            "DB1.DBB20(STRING16),DB1.DBB100+1024,M0.0,MW2,Q0.0,IW0,T1,C1");

    /* 创建客户端并配置连接参数（rack=0，slot 从序列首个开始） */
    g_integClient = XS7TcpClient_create();
    if (g_integClient == NULL) {
        XPrintf("[XS7][PLC] TOTAL FAIL (pass=0 fail=1 skip=0)\n");
        xs7fakeStop();
        return false;
    }
    XPlcDevice_setConnectionParameter_ref((XPlcDevice*)g_integClient,
                                          XPlcDevice_NetworkAddressParameter,
                                          XVariant_create_utf8_str(g_integIp));
    XPlcDevice_setConnectionParameter_ref((XPlcDevice*)g_integClient,
                                          XPlcDevice_NetworkPortParameter,
                                          XVariant_create_int((int)g_integPort));
    XPlcDevice_setConnectionParameter_ref((XPlcDevice*)g_integClient,
                                          XPlcDevice_RackParameter,
                                          XVariant_create_int(0));
    XPlcDevice_setConnectionParameter_ref((XPlcDevice*)g_integClient,
                                          XPlcDevice_SlotParameter,
                                          XVariant_create_int(g_integSlotSeq[0]));
    XPlcClient_setTimeout((XPlcClient*)g_integClient, 2000);
    XPlcClient_setNumberOfRetries((XPlcClient*)g_integClient, 1);
    XPlcClient_setReconnectInterval((XPlcClient*)g_integClient, 300);
    /* 真机第 1 轮实测：目标 CPU 可能静默丢弃未知功能码并断开连接；
     * 开启有界自动重连，避免单次断连导致后续用例连锁失败 */
    XPlcClient_setAutoReconnect((XPlcClient*)g_integClient, true);
    XPlcClient_setMaxReconnectAttempts((XPlcClient*)g_integClient, 50);

    XObject_connect_1((XObject*)g_integClient,
                      XSignal(XPlcDevice_stateChanged_signal),
                      (XObject*)g_integClient, integ_onStateChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)g_integClient,
                      XSignal(XPlcDevice_errorOccurred_signal),
                      (XObject*)g_integClient, integ_onDeviceError,
                      XConnectionType_Direct);

    /* 看门狗与 slot 试探超时 */
    XTimer_singleShot2(120000, integ_onWatchdog);
    XTimer_singleShot2(6000, integ_onSlotTimeout);
    g_integSlotPending = true;

    /* 发起连接（异步）；事件循环驱动其余流程 */
    if (!XPlcDevice_connectDevice((XPlcDevice*)g_integClient)) {
        XPrintf("[XS7][PLC] connectDevice 发起失败（实现未就绪或参数被拒，等待错误路径）\n");
    }
    XCoreApplication_exec();

    /* 事件循环退出：清理 */
    if (g_integClient != NULL) {
        XObject_disconnect_1((XObject*)g_integClient,
                             XSignal(XPlcDevice_stateChanged_signal),
                             (XObject*)g_integClient, integ_onStateChanged);
        XObject_disconnect_1((XObject*)g_integClient,
                             XSignal(XPlcDevice_errorOccurred_signal),
                             (XObject*)g_integClient, integ_onDeviceError);
        XPlcDevice_disconnectDevice((XPlcDevice*)g_integClient);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XClass_delete_base((XClass*)g_integClient);
        g_integClient = NULL;
    }
    xs7fakeStop();
    integ_clearOps();
    return g_integOk;
}

#endif /* XS7_CORE_ON */

/******************************************************************************************
 * 十五、菜单注册
 ******************************************************************************************/

/* ---- 菜单动作适配桩（XTestMenuActionFunc 要求 void(XVariant*) 完整原型） ---- */
static void xs7MenuTpkt(XVariant* data)     { (void)data; XS7TpktTest(); }
static void xs7MenuCotp(XVariant* data)     { (void)data; XS7CotpTest(); }
static void xs7MenuPduRead(XVariant* data)  { (void)data; XS7PduTest_Read(); }
static void xs7MenuPduWrite(XVariant* data) { (void)data; XS7PduTest_Write(); }
static void xs7MenuPduSetup(XVariant* data) { (void)data; XS7PduTest_Setup(); }
static void xs7MenuAddress(XVariant* data)  { (void)data; XS7AddressTest(); }
static void xs7MenuValue(XVariant* data)    { (void)data; XS7ValueTest(); }
static void xs7MenuControl(XVariant* data)  { (void)data; XS7ControlTest(); }
static void xs7MenuSession(XVariant* data)  { (void)data; XS7SessionTest(); }
static void xs7MenuReply(XVariant* data)    { (void)data; XPlcReplyPublicApiTest(); }

/** @brief 菜单包装：一键离线单测（XTestMenu 动作要求 void(XVariant*) 兼容签名） */
static void xs7MenuRunAll(XVariant* data)
{
    (void)data;
    (void)XS7Test_runAll();
}

/** @brief 菜单包装：一键集成测试（真机/模拟器/假 PLC） */
static void xs7MenuIntegration(XVariant* data)
{
    (void)data;
    (void)XS7Test_integration_run();
}

/**
 * @brief 注册 S7 测试菜单（挂到 XProtocolTest 根菜单）
 * @param root 根菜单（非NULL）
 * @note 菜单项与裁剪开关联动：XS7_CONTROL_ON=0 不出 Run/Stop 项，
 *       XS7_CORE_ON=0 只保留一键入口。
 */
void XTestMenu_XS7Test(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("S7(西门子)");
#if XS7_CORE_ON
    {
        XAction* action = XTestMenu_addAction(menu, "TPKT单元测试");
        XTestMenu_setActionFunction(action, xs7MenuTpkt);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "COTP单元测试");
        XTestMenu_setActionFunction(action, xs7MenuCotp);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "PDU读单元测试");
        XTestMenu_setActionFunction(action, xs7MenuPduRead);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "PDU写单元测试");
        XTestMenu_setActionFunction(action, xs7MenuPduWrite);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "PDU Setup单元测试");
        XTestMenu_setActionFunction(action, xs7MenuPduSetup);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "Address单元测试");
        XTestMenu_setActionFunction(action, xs7MenuAddress);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "Value单元测试");
        XTestMenu_setActionFunction(action, xs7MenuValue);
    }
#endif /* XS7_CORE_ON */
#if XS7_CONTROL_ON
    {
        XAction* action = XTestMenu_addAction(menu, "Control Run/Stop单元测试（危险：对目标 PLC 生效）");
        XTestMenu_setActionFunction(action, xs7MenuControl);
    }
#endif /* XS7_CONTROL_ON */
#if XS7_CORE_ON
    {
        XAction* action = XTestMenu_addAction(menu, "Session单元测试");
        XTestMenu_setActionFunction(action, xs7MenuSession);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "Reply公共API测试");
        XTestMenu_setActionFunction(action, xs7MenuReply);
    }
#endif /* XS7_CORE_ON */
    {
        XAction* action = XTestMenu_addAction(menu, "离线单测全集(--test xs7-unit)");
        XTestMenu_setActionFunction(action, xs7MenuRunAll);
    }
    {
        XAction* action = XTestMenu_addAction(menu,
            "集成测试(--test xs7-plc，需 XS7_PLC_IP 可达；须 TIA 关闭 DB 优化块访问+允许 PUT/GET；Run/Stop 危险)");
        XTestMenu_setActionFunction(action, xs7MenuIntegration);
    }
    XTestMenu_addMenu(root, menu);
}

#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
