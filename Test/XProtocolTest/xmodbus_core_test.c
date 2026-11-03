/**
 * @file       xmodbus_core_test.c
 * @brief      XModbus 核心值类型组合测试（Pdu/Adu/DataUnit/CommEvent/
 *             DeviceIdentification/Reply，纯逻辑、无传输依赖）。
 * @details    [2026-10-31 覆盖补强] 此前六类核心值类型仅有零散间接覆盖：
 *             DeviceIdentification 全库零覆盖、Pdu 异常语义与 isValid
 *             边界、Adu ASCII 路径与 CRC 篡改、DataUnit 容器切换、Reply
 *             NoError 复位/中间错误/地址截断均未测。本文件补齐组合矩阵。
 *             纯值语义：Windows/WSL 任意平台可跑（`--test xmodbus-core`）。
 */
#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XPrintf.h"
#include "XModbusPdu.h"
#include "XModbusAdu.h"
#include "XModbusDataUnit.h"
#include "XModbusCommEvent.h"
#include "XModbusDeviceIdentification.h"
#include "XModbusReply.h"
#include "XModbusReply_Protected.h"
#include "XModbusDevice.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#if XPROTOCOL_ON && XMODBUS_ON && XMODBUS_CORE_ON

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, desc) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/* ==================== Pdu：FC/异常/边界 ==================== */

static void testPdu(void)
{
    XModbusPdu* pdu = XModbusPdu_create_with_code(XModbusPdu_ReadHoldingRegisters);
    uint8_t data[4] = {0x00, 0x0A, 0x00, 0x02};
    uint8_t big[260];
    XByteArray* bytes;
    size_t i;

    CHECK(pdu != NULL, "pdu create_with_code");
    CHECK((int)XModbusPdu_functionCode(pdu) == (int)XModbusPdu_ReadHoldingRegisters,
          "FC 保持");
    XModbusPdu_setData(pdu, data, 4);
    bytes = XModbusPdu_data(pdu); /* 新对象，调用方释放。 */
    CHECK(bytes != NULL && XByteArray_size_base((XContainer*)bytes) == 4,
          "setData/data 长度往返");
    {
        const uint8_t* dp = (const uint8_t*)XContainerDataAddr((XContainer*)bytes);
        CHECK(bytes != NULL && dp != NULL && memcmp(dp, data, 4) == 0,
              "setData/data 内容往返");
    }
    if (bytes) XClassDelete(bytes);

    /* 异常语义：FC|0x80 → isException；functionCode 剥离 / Raw 保留。 */
    XModbusPdu_setFunctionCode(pdu,
        (XModbusPdu_FunctionCode)((int)XModbusPdu_ReadHoldingRegisters | 0x80));
    {
        uint8_t exCode = 0x02; /* IllegalDataAddress。 */
        XModbusPdu_setData(pdu, &exCode, 1);
    }
    CHECK(XModbusPdu_isException(pdu), "异常位识别");
    CHECK((int)XModbusPdu_functionCode(pdu) == (int)XModbusPdu_ReadHoldingRegisters,
          "functionCode 剥离异常位");
    CHECK((int)XModbusPdu_functionCodeRaw(pdu) ==
          ((int)XModbusPdu_ReadHoldingRegisters | 0x80), "functionCodeRaw 保留异常位");
    CHECK((int)XModbusPdu_exceptionCode(pdu) == 0x02, "exceptionCode=数据首字节");

    /* isValid 边界：数据 ≤252 合法，253 拒。 */
    for (i = 0; i < sizeof(big); ++i) big[i] = (uint8_t)(i & 0xFF);
    XModbusPdu_setFunctionCode(pdu, XModbusPdu_ReadHoldingRegisters);
    XModbusPdu_setData(pdu, big, 252);
    CHECK(XModbusPdu_isValid(pdu), "数据 252 字节合法");
    XModbusPdu_setData(pdu, big, 253);
    CHECK(!XModbusPdu_isValid(pdu), "数据 253 字节拒绝");
    XClassDelete(pdu);
}

/* ==================== Adu：RTU/ASCII 组帧解析往返 ==================== */

static void testAdu(void)
{
    XModbusRequest* req = XModbusRequest_create_with_code(XModbusPdu_ReadHoldingRegisters);
    uint8_t rdata[4] = {0x00, 0x0A, 0x00, 0x02};
    uint8_t frame[300];
    uint8_t ascii[300];
    XByteArray* frameBytes;
    const uint8_t* fp;
    uint16_t crc;
    XModbusAdu* adu;
    size_t asize;

    CHECK(req != NULL, "request create_with_code");
    XModbusPdu_setData((XModbusPdu*)req, rdata, 4);

    /* RTU 组帧：[addr][fc][data4][crc2] = 8 字节，CRC 小端附加。 */
    frameBytes = XModbusAdu_createRtuFrame(1, (const XModbusPdu*)req);
    CHECK(frameBytes != NULL &&
          XByteArray_size_base((XContainer*)frameBytes) == 8,
          "RTU 帧长 8");
    if (!frameBytes) { XClassDelete(req); return; }
    fp = (const uint8_t*)XContainerDataAddr((XContainer*)frameBytes);
    CHECK(fp[0] == 1 && fp[1] == 0x03, "RTU 帧地址与 FC");
    crc = XModbusAdu_calculateCRC(fp, 6);
    CHECK(fp[6] == (uint8_t)(crc & 0xFF) && fp[7] == (uint8_t)(crc >> 8),
          "RTU CRC 小端附加");
    /* parse 往返。 */
    adu = XModbusAdu_parseRtu(fp, 8);
    CHECK(adu != NULL && adu->m_checksumValid, "parse 往返 CRC 通过");
    CHECK(adu != NULL && (int)XModbusAdu_serverAddress(adu) == 1, "parse 地址");
    CHECK(adu != NULL && XModbusAdu_size(adu) == 6, "size=地址+FC+数据");
    if (adu) {
        XModbusPdu out;
        memset(&out, 0, sizeof(out));
        CHECK(XModbusAdu_pdu(adu, &out) &&
              (int)XModbusPdu_functionCode(&out) == (int)XModbusPdu_ReadHoldingRegisters,
              "pdu() 提取 FC");
        /* pdu() 经 init_with_code 填充栈上 out（拥有 m_data）：用完必须
         * XClassDeinit，否则泄漏内嵌 XByteArray（ASan 实证）。 */
        XClassDeinit((XClass*)&out);
        XModbusAdu_delete(adu);
    }
    /* CRC 篡改 1 字节 → checksumValid=false（不拒收，标记）。 */
    memcpy(frame, fp, 8);
    XClassDelete(frameBytes);
    frame[3] ^= 0xFF;
    adu = XModbusAdu_parseRtu(frame, 8);
    CHECK(adu != NULL && !adu->m_checksumValid, "CRC 篡改标记 false");
    if (adu) XModbusAdu_delete(adu);
    /* 截断帧（<4B）拒。 */
    CHECK(XModbusAdu_parseRtu(frame, 3) == NULL, "截断帧拒收");

    /* ASCII 路径：组帧→解析往返（':' 起、LRC 校验、分隔符收尾）。 */
    frameBytes = XModbusAdu_createAsciiFrame(1, (const XModbusPdu*)req, '\r');
    CHECK(frameBytes != NULL, "ASCII 组帧");
    if (!frameBytes) { XClassDelete(req); return; }
    asize = (size_t)XByteArray_size_base((XContainer*)frameBytes);
    {
        const uint8_t* ap = (const uint8_t*)XContainerDataAddr((XContainer*)frameBytes);
        CHECK(asize >= 9 && ap != NULL && ap[0] == ':', "ASCII 帧以 ':' 起始");
        if (ap) memcpy(ascii, ap, asize);
    }
    adu = XModbusAdu_parseAscii(ascii, asize);
    CHECK(adu != NULL && adu->m_checksumValid, "ASCII parse 往返 LRC 通过");
    CHECK(adu != NULL && (int)XModbusAdu_serverAddress(adu) == 1, "ASCII parse 地址");
    if (adu) XModbusAdu_delete(adu);
    ascii[3] ^= 0x01; /* 破坏一个 HEX 字符 → LRC 失配或 hex 非法。 */
    adu = XModbusAdu_parseAscii(ascii, asize);
    CHECK(adu == NULL || !adu->m_checksumValid, "ASCII 篡改后不通过");
    if (adu) XModbusAdu_delete(adu);
    XClassDelete(frameBytes);
    XClassDelete(req);
}

/* ==================== DataUnit：容器/边界/切换 ==================== */

static void testDataUnit(void)
{
    XModbusDataUnit* u = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                   XModbusCoils, 0, 8);
    XModbusDataUnit* hr = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                    XModbusHoldingRegisters, 0, 4);
    XModbusDataUnit* inv = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                     XModbusInvalid, 0xFFFF, 0);

    CHECK(u != NULL && XModbusDataUnit_isValid(u), "位容器有效");
    CHECK(hr != NULL && XModbusDataUnit_isValid(hr), "寄存器容器有效");
    CHECK(inv == NULL || !XModbusDataUnit_isValid(inv),
          "Invalid+0xFFFF 哨兵为无效语义");

    /* 位区：置位/清零/越界。 */
    CHECK(XModbusDataUnit_setValue(u, 3, 1), "位区 setValue");
    CHECK(XModbusDataUnit_value(u, 3) == 1, "位区置位读回");
    CHECK(XModbusDataUnit_value(u, 4) == 0, "未置位读回 0");
    CHECK(XModbusDataUnit_value(u, 8) == 0, "越界读返回 0");
    CHECK(XModbusDataUnit_value(u, 999) == 0, "远越界读返回 0");

    /* 寄存器区：全 16 位值。 */
    CHECK(XModbusDataUnit_setValue(hr, 0, 0xA55A), "寄存器 setValue");
    CHECK(XModbusDataUnit_value(hr, 0) == 0xA55A, "寄存器 16 位读回");

    /* 位→寄存器切换：容器重建，内容不保留。 */
    XModbusDataUnit_setRegisterType(u, XModbusHoldingRegisters);
    CHECK(XModbusDataUnit_value(u, 3) == 0, "切换后容器重建内容归零");
    CHECK(XModbusDataUnit_setValue(u, 3, 0xBEEF) &&
          XModbusDataUnit_value(u, 3) == 0xBEEF, "切换后可正常写读");

    XClassDelete(u);
    XClassDelete(hr);
    if (inv) XClassDelete(inv);
}

/* ==================== CommEvent：字节组合 ==================== */

static void testCommEvent(void)
{
    XModbusCommEvent ev = XModbusCommEvent_fromUint8(0);
    XModbusCommEvent ev2 = XModbusCommEvent_create(XModbusCommEvent_SentEvent);
    XModbusCommEvent evNull;

    CHECK(XModbusCommEvent_toUint8(&ev) == 0, "fromUint8(0)→toUint8 往返");
    CHECK(XModbusCommEvent_toUint8(&ev2) == 0x40, "create(SentEvent=0x40) 原样存储");
    CHECK(XModbusCommEvent_toUint8(&ev2) == 0x40,
          "create(SentEvent=0x40) 原样存储");

    /* NULL 容错。 */
    memset(&evNull, 0, sizeof(evNull));
    CHECK(XModbusCommEvent_toUint8(NULL) == 0, "NULL toUint8=0");
    (void)evNull;
}

/* ==================== DeviceIdentification：必填/边界/取值 ==================== */

static void testDeviceIdentification(void)
{
    XModbusDeviceIdentification* id = XModbusDeviceIdentification_create();
    XModbusDeviceIdentification* big;
    XByteArray* vendor;
    uint8_t payload[246];
    uint8_t big245[245];
    size_t i;

    for (i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)('A' + (i % 26));
    memset(big245, 'B', sizeof(big245));

    CHECK(id != NULL, "identification create");
    CHECK(id != NULL && !XModbusDeviceIdentification_isValid(id),
          "未填三必填 → 无效");
    CHECK(id != NULL &&
          XModbusDeviceIdentification_conformityLevel(id) ==
          XModbusDeviceIdentification_BasicConformityLevel,
          "默认符合性等级 Basic");
    CHECK(id != NULL &&
          XModbusDeviceIdentification_value(id,
              XModbusDeviceIdentification_VendorNameObjectId) == NULL,
          "未登记对象取值 NULL");

    /* 三必填登记 → 有效。 */
    CHECK(id != NULL &&
          XModbusDeviceIdentification_insert(id,
              XModbusDeviceIdentification_VendorNameObjectId,
              (const uint8_t*)"XTest", 5), "insert VendorName");
    CHECK(id != NULL &&
          XModbusDeviceIdentification_insert(id,
              XModbusDeviceIdentification_ProductCodeObjectId,
              (const uint8_t*)"XC01", 4), "insert ProductCode");
    CHECK(id != NULL &&
          XModbusDeviceIdentification_insert(id,
              XModbusDeviceIdentification_MajorMinorRevisionObjectId,
              (const uint8_t*)"1.0.0", 5), "insert Revision");
    CHECK(id != NULL && XModbusDeviceIdentification_isValid(id),
          "三必填齐 → 有效");

    /* 取值：新 XByteArray，内容一致；未登记对象 NULL。 */
    vendor = id ? XModbusDeviceIdentification_value(id,
                XModbusDeviceIdentification_VendorNameObjectId) : NULL;
    CHECK(vendor != NULL &&
          XByteArray_size_base((XContainer*)vendor) == 5 &&
          memcmp((const uint8_t*)XContainerDataAddr((XContainer*)vendor),
                 "XTest", 5) == 0,
          "value 内容往返");
    if (vendor) XClassDelete(vendor);
    CHECK(id != NULL &&
          XModbusDeviceIdentification_value(id, 0x55) == NULL,
          "未登记对象取值 NULL（登记后复查）");

    /* 245 上限：246 拒、245 收。 */
    CHECK(id != NULL &&
          !XModbusDeviceIdentification_insert(id, 0x80, payload, sizeof(payload)),
          "超 245 拒绝");
    CHECK(id != NULL &&
          XModbusDeviceIdentification_insert(id, 0x81, big245, sizeof(big245)),
          "恰 245 接受");

    XClassDelete(id);
    (void)big;
}

/* ==================== Reply：NoError 复位/中间错误/地址截断 ==================== */

static void testReply(void)
{
    XModbusReply* r = XModbusReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             XModbusReply_Common, 300);
    XVector* inter;
    XString* err;

    CHECK(r != NULL, "reply create");
    /* serverAddress 按 uint8_t 存储：300 截断为 44（实现口径，注释记录）。 */
    CHECK(r != NULL && XModbusReply_serverAddress(r) == 44,
          "serverAddress 截断口径（300→44）");
    CHECK(r != NULL && (int)XModbusReply_type(r) == (int)XModbusReply_Common,
          "ReplyType 保持");

    /* 中间错误：NULL → 追加 → 列表。 */
    inter = r ? XModbusReply_intermediateErrors(r) : NULL;
    CHECK(inter == NULL, "无中间错误返回 NULL");
    if (r) XModbusReply_addIntermediateError(r, XModbusDevice_ResponseCrcError);
    inter = r ? XModbusReply_intermediateErrors(r) : NULL;
    CHECK(inter != NULL && XVector_size_base((XContainer*)inter) == 1,
          "追加一条中间错误");
    if (inter) XClassDelete(inter);

    /* NoError 复位路径：清描述、状态不变。 */
    if (r) {
        XModbusReply_setError_2(r, XModbusDevice_NoError, NULL);
        err = XModbusReply_errorString(r);
        CHECK(err == NULL || XString_size_base((XContainer*)err) == 0,
              "NoError 复位清空描述");
        if (err) XClassDelete(err);
        CHECK((int)XModbusReply_error(r) == (int)XModbusDevice_NoError,
              "NoError 复位错误码");
    }
    if (r) XClassDelete(r);
}


/* ==================== setter 三态所有权契约（copy / move / ref） ==================== */
/*
 * 锁住 2026-11-03 裁定并写入文档的 setter 三态语义：
 *   - setResult       ：深拷贝，入参仍归调用方，调用方必须释放
 *   - setResult_move  ：抽取资源，入参变空壳，**外壳仍由调用方释放**
 *   - setResult_ref   ：指针接管，所有权转移，调用方**不得再释放**
 * 三种形态各跑一遍，配合 ASan/LSan 即可证明：既不泄漏也不双重释放。
 * 任何一条语义被改错，ASan 会直接报 leak 或 double-free。
 */
static void testSetterOwnershipTriple(void)
{
    /* ---- 1. copy：设备/回复持有副本，调用方保留所有权 ---- */
    {
        XModbusReply* r = XModbusReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 XModbusReply_Common, 301);
        XModbusDataUnit* u = XModbusDataUnit_create();
        CHECK(r != NULL && u != NULL, "copy: 创建回复与数据单元");
        XModbusReply_setResult(r, u);
        /* 副本已登记，原始 u 仍归调用方 */
        CHECK(XModbusReply_result_const(r) != NULL, "copy: 结果已登记");
        XClassDelete((XClass*)u);   /* 调用方释放自己的对象：不得影响回复内的副本 */
        CHECK(XModbusReply_result_const(r) != NULL, "copy: 释放入参后回复副本仍有效");
        XClassDelete((XClass*)r);   /* 回复析构释放副本 */
    }

    /* ---- 2. move：资源转移，外壳由调用方释放 ---- */
    {
        XModbusReply* r = XModbusReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 XModbusReply_Common, 302);
        XModbusDataUnit* u = XModbusDataUnit_create();
        CHECK(r != NULL && u != NULL, "move: 创建回复与数据单元");
        XModbusReply_setResult_move(r, u);
        CHECK(XModbusReply_result_const(r) != NULL, "move: 结果已接管");
        /* 空壳必须由调用方释放（不释放即泄漏，ASan 直接抓） */
        XClassDelete((XClass*)u);
        CHECK(XModbusReply_result_const(r) != NULL, "move: 释放空壳后回复结果仍有效");
        XClassDelete((XClass*)r);
    }

    /* ---- 3. ref：指针接管，调用方不得再释放 ---- */
    {
        XModbusReply* r = XModbusReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 XModbusReply_Common, 303);
        XModbusDataUnit* u = XModbusDataUnit_create();
        CHECK(r != NULL && u != NULL, "ref: 创建回复与数据单元");
        XModbusReply_setResult_ref(r, u);
        CHECK(XModbusReply_result_const(r) == u, "ref: 回复直接持有传入指针本身（零拷贝）");
        /* u 不得释放：由回复析构负责（此处释放即 double-free，ASan 抓） */
        XClassDelete((XClass*)r);
    }

    /* ---- 4. ref 覆盖：只释放自己此前接管的对象 ---- */
    {
        XModbusReply* r = XModbusReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 XModbusReply_Common, 304);
        XModbusDataUnit* first = XModbusDataUnit_create();
        XModbusDataUnit* second = XModbusDataUnit_create();
        CHECK(r && first && second, "ref 覆盖: 创建回复与两个数据单元");
        XModbusReply_setResult_ref(r, first);
        XModbusReply_setResult_ref(r, second);   /* 内部应释放 first，登记 second */
        CHECK(XModbusReply_result_const(r) == second, "ref 覆盖: 登记新指针");
        XClassDelete((XClass*)r);                /* 析构释放 second */
    }

    /* ---- 5. ref 传 NULL：仅清空 ---- */
    {
        XModbusReply* r = XModbusReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 XModbusReply_Common, 305);
        XModbusDataUnit* u = XModbusDataUnit_create();
        XModbusReply_setResult_ref(r, u);
        XModbusReply_setResult_ref(r, NULL);
        CHECK(XModbusReply_result_const(r) == NULL, "ref(NULL): 仅清空结果");
        XClassDelete((XClass*)r);
    }
}

/* ==================== 入口 ==================== */

int XModbusCoreTest_run(void)
{
    /* 主程序分派场景下 XCoreApplication 为单例语义：create 返回 main
     * 已持有的实例，**不得 delete**（否则 main 退出清理时 UAF，ASan
     * 实证于 2026-10-31）。信号无连接时同步空转发，纯逻辑测试不需要
     * 事件循环；创建仅为对齐仓库测试惯例。 */
    XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);

    printf("=== XModbus 核心值类型组合测试 ===\n"); fflush(stdout);

    testPdu();
    testAdu();
    testDataUnit();
    testCommEvent();
    testDeviceIdentification();
    testReply();
    testSetterOwnershipTriple();

    printf("\n===== XModbusCoreTest: %d passed, %d failed =====\n",
           g_pass, g_fail); fflush(stdout);
    return g_fail;
}

#endif /* XPROTOCOL_ON && XMODBUS_ON && XMODBUS_CORE_ON */
