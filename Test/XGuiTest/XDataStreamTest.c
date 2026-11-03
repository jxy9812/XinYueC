/**
 * @file       XDataStreamTest.c
 * @brief      XDataStream 专项测试（对标 Qt tst_QDataStream 的线格式用例）。
 * @details    逐项验证：字节序、定长整型宽度、布尔 1 字节、浮点位模式、
 *             浮点精度、长度前缀的 null/扩展标记、字节块、字符串 UTF-16
 *             线格式与版本差异、字节数组、原始读写/跳过、事务、错误状态。
 *             所有断言以 `[XDS-FAIL]` 前缀输出。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XDataStreamTest.h"

#include "XDataStream.h"
#include "XByteArray.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"
#include <stdio.h>
#include <string.h>

static int xds_failures = 0;

static void xds_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[XDS-FAIL] %s\n", what ? what : "");
        ++xds_failures;
    }
}

/** @brief 取数组原始字节（只读视图）。 */
static const uint8_t* xds_bytes(const XByteArray* a)
{ return (const uint8_t*)XByteArray_data((XByteArray*)a); }

/* ==================== 字节序与定长整型 ==================== */

static void xds_testByteOrderAndInts(void)
{
    XByteArray* buf = XByteArray_create();
    XDataStream ws;
    XDataStream rs;
    uint32_t u32 = 0;
    int16_t i16 = 0;
    const uint8_t* b;

    xds_expect(buf != NULL, "ints: 数组创建");
    if (!buf) return;

    XDataStream_init_array(&ws, buf);
    XDataStream_writeUInt32(&ws, 0x01020304u);
    b = xds_bytes(buf);
    /* 缺省大端：高位在前。 */
    xds_expect(XByteArray_size_base(buf) == 4 &&
               b[0] == 0x01 && b[1] == 0x02 && b[2] == 0x03 && b[3] == 0x04,
               "ints: 缺省大端写出 0x01020304");

    XDataStream_init_arrayReadOnly(&rs, buf);
    XDataStream_readUInt32(&rs, &u32);
    xds_expect(u32 == 0x01020304u, "ints: 大端读回一致");

    /* 切小端：低位在前。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_setByteOrder(&ws, XDataStream_ByteOrder_LittleEndian);
    XDataStream_writeUInt32(&ws, 0x01020304u);
    b = xds_bytes(buf);
    xds_expect(b[0] == 0x04 && b[1] == 0x03 && b[2] == 0x02 && b[3] == 0x01,
               "ints: 小端写出字节序正确");

    XDataStream_init_arrayReadOnly(&rs, buf);
    XDataStream_setByteOrder(&rs, XDataStream_ByteOrder_LittleEndian);
    u32 = 0;
    XDataStream_readUInt32(&rs, &u32);
    xds_expect(u32 == 0x01020304u, "ints: 小端读回一致");

    /* 各宽度字节数。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeInt8(&ws, -1);
    xds_expect(XByteArray_size_base(buf) == 1, "ints: qint8 占 1 字节");
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeInt16(&ws, -2);
    xds_expect(XByteArray_size_base(buf) == 2, "ints: qint16 占 2 字节");
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeInt64(&ws, -3);
    xds_expect(XByteArray_size_base(buf) == 8, "ints: qint64 占 8 字节");

    /* 有符号往返（含负值）。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeInt16(&ws, -12345);
    XDataStream_init_arrayReadOnly(&rs, buf);
    XDataStream_readInt16(&rs, &i16);
    xds_expect(i16 == -12345, "ints: qint16 负值往返");

    XClassDeinit((XClass*)&ws);
    XClassDeinit((XClass*)&rs);
    XClassDelete((XClass*)buf);
}

/* ==================== 布尔与浮点 ==================== */

static void xds_testBoolAndFloat(void)
{
    XByteArray* buf = XByteArray_create();
    XDataStream ws;
    XDataStream rs;
    bool bv = false;
    float fv = 0.0f;
    double dv = 0.0;
    uint32_t bits32 = 0;

    if (!buf) { xds_expect(false, "float: 数组创建"); return; }

    /* 布尔恒 1 字节（对齐 Qt，与版本无关）。 */
    XDataStream_init_array(&ws, buf);
    XDataStream_writeBool(&ws, true);
    xds_expect(XByteArray_size_base(buf) == 1, "bool: 恒占 1 字节");
    XDataStream_init_arrayReadOnly(&rs, buf);
    XDataStream_readBool(&rs, &bv);
    xds_expect(bv == true, "bool: true 往返");

    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeBool(&ws, false);
    XDataStream_init_arrayReadOnly(&rs, buf);
    bv = true;
    XDataStream_readBool(&rs, &bv);
    xds_expect(bv == false, "bool: false 往返");

    /* 非零即 true（对齐 Qt 的 v != 0）。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeUInt8(&ws, 42);
    XDataStream_init_arrayReadOnly(&rs, buf);
    bv = false;
    XDataStream_readBool(&rs, &bv);
    xds_expect(bv == true, "bool: 非零字节读为 true");

    /* float：4 字节，位模式直写。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeFloat(&ws, 1.5f);
    xds_expect(XByteArray_size_base(buf) == 4, "float: 占 4 字节");
    XDataStream_init_arrayReadOnly(&rs, buf);
    XDataStream_readFloat(&rs, &fv);
    xds_expect(fv == 1.5f, "float: 1.5 往返");

    /* float 不受浮点精度设置影响（对齐 Qt）。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_setFloatingPointPrecision(
        &ws, XDataStream_FloatingPointPrecision_Single);
    XDataStream_writeFloat(&ws, 2.25f);
    xds_expect(XByteArray_size_base(buf) == 4,
               "float: Single 精度下仍占 4 字节");

    /* double 双精度：8 字节。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeDouble(&ws, 3.14159265358979);
    xds_expect(XByteArray_size_base(buf) == 8, "double: 双精度占 8 字节");
    XDataStream_init_arrayReadOnly(&rs, buf);
    XDataStream_readDouble(&rs, &dv);
    xds_expect(dv == 3.14159265358979, "double: 双精度往返");

    /* double 单精度：降为 4 字节（对齐 Qt FloatingPointPrecision）。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_setFloatingPointPrecision(
        &ws, XDataStream_FloatingPointPrecision_Single);
    XDataStream_writeDouble(&ws, 1.5);
    xds_expect(XByteArray_size_base(buf) == 4,
               "double: Single 精度降为 4 字节");
    XDataStream_init_arrayReadOnly(&rs, buf);
    XDataStream_setFloatingPointPrecision(
        &rs, XDataStream_FloatingPointPrecision_Single);
    dv = 0.0;
    XDataStream_readDouble(&rs, &dv);
    xds_expect(dv == 1.5, "double: Single 精度往返");
    (void)bits32;

    XClassDeinit((XClass*)&ws);
    XClassDeinit((XClass*)&rs);
    XClassDelete((XClass*)buf);
}

/* ==================== 长度前缀字节块 ==================== */

static void xds_testBytesBlock(void)
{
    XByteArray* buf = XByteArray_create();
    XDataStream ws;
    XDataStream rs;
    int64_t len;
    char out[64];
    const uint8_t* b;

    if (!buf) { xds_expect(false, "bytes: 数组创建"); return; }

    /* 长度 3 + 内容 "abc" → 4 + 3 = 7 字节。 */
    XDataStream_init_array(&ws, buf);
    xds_expect(XDataStream_writeBytes(&ws, "abc", 3), "bytes: writeBytes 成功");
    xds_expect(XByteArray_size_base(buf) == 7,
               "bytes: quint32 长度前缀占 4 字节");
    b = xds_bytes(buf);
    xds_expect(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 3,
               "bytes: 长度前缀大端为 3");

    len = (int64_t)sizeof(out);
    XDataStream_init_arrayReadOnly(&rs, buf);
    xds_expect(XDataStream_readBytes(&rs, out, &len) && len == 3 &&
               memcmp(out, "abc", 3) == 0,
               "bytes: readBytes 往返");

    /* 空内容：仅 4 字节长度前缀。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeBytes(&ws, NULL, 0);
    xds_expect(XByteArray_size_base(buf) == 4, "bytes: 空内容仅写长度");

    /* null 标记：0xFFFFFFFF（对齐 Qt NullCode）。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeByteArray(&ws, NULL);
    b = xds_bytes(buf);
    xds_expect(XByteArray_size_base(buf) == 4 &&
               b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF && b[3] == 0xFF,
               "bytes: null 数组写 0xFFFFFFFF 标记");

    /* 读 null 标记 → 空数组对象（非 NULL）。 */
    {
        XByteArray* got;
        XDataStream_init_arrayReadOnly(&rs, buf);
        got = XDataStream_readByteArray(&rs);
        xds_expect(got != NULL &&
                   XByteArray_size_base(got) == 0,
                   "bytes: null 标记读为空数组");
        if (got) XClassDelete((XClass*)got);
    }

    /* 容量不足 → ReadCorruptData（不截断）。 */
    {
        XByteArray* big = XByteArray_create();
        XDataStream ws2;
        XDataStream rs2;
        int64_t small = 2;
        char tiny[2];
        if (big) {
            XDataStream_init_array(&ws2, big);
            XDataStream_writeBytes(&ws2, "abcdef", 6);
            XDataStream_init_arrayReadOnly(&rs2, big);
            xds_expect(!XDataStream_readBytes(&rs2, tiny, &small) &&
                       XDataStream_status(&rs2) ==
                           XDataStream_Status_ReadCorruptData,
                       "bytes: 缓冲不足报 ReadCorruptData（不静默截断）");
            XClassDeinit((XClass*)&ws2);
            XClassDeinit((XClass*)&rs2);
            XClassDelete((XClass*)big);
        }
    }

    XClassDeinit((XClass*)&ws);
    XClassDeinit((XClass*)&rs);
    XClassDelete((XClass*)buf);
}

/* ==================== 字符串线格式与版本 ==================== */

static void xds_testString(void)
{
    XByteArray* buf = XByteArray_create();
    XDataStream ws;
    XDataStream rs;
    XString* s;
    const uint8_t* b;

    if (!buf) { xds_expect(false, "string: 数组创建"); return; }

    /* 缺省版本：长度=字节数（UTF-16 每字符 2 字节）。"AB" → 4 + 4 = 8。 */
    s = XString_create_utf8("AB");
    XDataStream_init_array(&ws, buf);
    xds_expect(XDataStream_writeString(&ws, s), "string: 写入成功");
    xds_expect(XByteArray_size_base(buf) == 8,
               "string: 长度前缀按字节数计（2 字符 = 4 字节）");
    b = xds_bytes(buf);
    xds_expect(b[3] == 4, "string: 长度字段为 4 字节");
    /* 大端下 'A'=0x0041 写作 00 41。 */
    xds_expect(b[4] == 0x00 && b[5] == 0x41, "string: 'A' 大端为 00 41");

    XDataStream_init_arrayReadOnly(&rs, buf);
    {
        XString* got = XDataStream_readString(&rs);
        xds_expect(got != NULL &&
                   strcmp(XString_toUtf8(got), "AB") == 0,
                   "string: 往返一致");
        if (got) XClassDelete((XClass*)got);
    }

    /* 非 ASCII（CJK）往返。 */
    XByteArray_clear_base(buf);
    XClassDelete((XClass*)s);
    s = XString_create_utf8("中文");
    XDataStream_init_array(&ws, buf);
    XDataStream_writeString(&ws, s);
    XDataStream_init_arrayReadOnly(&rs, buf);
    {
        XString* got = XDataStream_readString(&rs);
        xds_expect(got != NULL &&
                   strcmp(XString_toUtf8(got), "中文") == 0,
                   "string: CJK 往返");
        if (got) XClassDelete((XClass*)got);
    }

    /* 版本 >= 3 且 null 字符串 → null 标记 0xFFFFFFFF。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeString(&ws, NULL);
    b = xds_bytes(buf);
    xds_expect(XByteArray_size_base(buf) == 4 &&
               b[0] == 0xFF && b[3] == 0xFF,
               "string: null 字符串写 null 标记（版本 >= 3）");
    {
        XString* got;
        XDataStream_init_arrayReadOnly(&rs, buf);
        got = XDataStream_readString(&rs);
        /* 注意：XString_toUtf8 对**空串**返回 NULL（XString.c:512），
         * 不能直接解引用——先判空再取首字符。 */
        xds_expect(got != NULL, "string: null 标记读回非 NULL");
        if (got) {
            const char* u8 = XString_toUtf8(got);
            xds_expect(u8 == NULL || u8[0] == '\0',
                       "string: null 标记读为空串对象（非 NULL）");
            XClassDelete((XClass*)got);
        }
    }

    /* 版本 1：走 Latin-1 单字节路径（"AB" → 4 + 2 = 6 字节）。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_setVersion(&ws, XDataStream_Version_1);
    XDataStream_writeString(&ws, s);
    xds_expect(XByteArray_size_base(buf) == 4 + XString_length_base(s),
               "string: 版本 1 走 Latin-1（每字符 1 字节）");
    XDataStream_init_arrayReadOnly(&rs, buf);
    XDataStream_setVersion(&rs, XDataStream_Version_1);
    {
        XString* got = XDataStream_readString(&rs);
        /* CJK 在 Latin-1 下按 '?' 落盘（有损，对齐 Qt toLatin1）。 */
        xds_expect(got != NULL, "string: 版本 1 读回不崩溃");
        if (got) XClassDelete((XClass*)got);
    }

    /* 奇数长度 → ReadCorruptData。 */
    XByteArray_clear_base(buf);
    XDataStream_init_array(&ws, buf);
    XDataStream_writeUInt32(&ws, 3);   /* 非法：非 2 的倍数 */
    XDataStream_writeUInt8(&ws, 0);
    XDataStream_writeUInt8(&ws, 0);
    XDataStream_writeUInt8(&ws, 0);
    XDataStream_init_arrayReadOnly(&rs, buf);
    {
        XString* got = XDataStream_readString(&rs);
        xds_expect(got == NULL &&
                   XDataStream_status(&rs) ==
                       XDataStream_Status_ReadCorruptData,
                   "string: 奇数长度报 ReadCorruptData");
        if (got) XClassDelete((XClass*)got);
    }

    XClassDelete((XClass*)s);
    XClassDeinit((XClass*)&ws);
    XClassDeinit((XClass*)&rs);
    XClassDelete((XClass*)buf);
}

/* ==================== 原始读写 / 跳过 / 边界 ==================== */

static void xds_testRawAndErrors(void)
{
    XByteArray* buf = XByteArray_create();
    XDataStream ws;
    XDataStream rs;
    char out[16];

    if (!buf) { xds_expect(false, "raw: 数组创建"); return; }

    XDataStream_init_array(&ws, buf);
    xds_expect(XDataStream_writeRawData(&ws, "hello", 5) == 5,
               "raw: writeRawData 返回写入字节数");
    xds_expect(XDataStream_writeRawData(&ws, "x", -1) == -1 &&
               XDataStream_status(&ws) == XDataStream_Status_WriteFailed,
               "raw: 负长度报 WriteFailed");

    /* 读超过剩余 → 只返回可用部分并置 ReadPastEnd。 */
    XDataStream_init_arrayReadOnly(&rs, buf);
    xds_expect(XDataStream_readRawData(&rs, out, 10) == 5 &&
               XDataStream_status(&rs) == XDataStream_Status_ReadPastEnd,
               "raw: 读越界返回可用字节并置 ReadPastEnd");

    /* 跳过：数组模式推进游标。 */
    XDataStream_init_arrayReadOnly(&rs, buf);
    xds_expect(XDataStream_skipRawData(&rs, 2) == 2 &&
               XDataStream_readRawData(&rs, out, 3) == 3 &&
               memcmp(out, "llo", 3) == 0,
               "raw: skipRawData 推进游标");

    /* skip 超过剩余 → ReadPastEnd。 */
    XDataStream_init_arrayReadOnly(&rs, buf);
    {
        XDataStream_status(&rs);
        xds_expect(XDataStream_skipRawData(&rs, 100) == 5 &&
                   XDataStream_status(&rs) ==
                       XDataStream_Status_ReadPastEnd,
                   "raw: skip 越界置 ReadPastEnd");
    }

    /* 只读模式拒绝写。 */
    XDataStream_init_arrayReadOnly(&rs, buf);
    xds_expect(XDataStream_writeRawData(&rs, "z", 1) == -1 &&
               XDataStream_status(&rs) == XDataStream_Status_WriteFailed,
               "raw: 只读流拒写");

    /* 未绑定承载：读置 ReadPastEnd、写置 WriteFailed。 */
    {
        XDataStream bare;
        XDataStream_init(&bare);
        xds_expect(XDataStream_atEnd(&bare), "raw: 空流 atEnd 为 true");
        xds_expect(XDataStream_readRawData(&bare, out, 1) == 0 &&
                   XDataStream_status(&bare) ==
                       XDataStream_Status_ReadPastEnd,
                   "raw: 无承载读置 ReadPastEnd");
        xds_expect(XDataStream_writeRawData(&bare, "q", 1) == -1 &&
                   XDataStream_status(&bare) ==
                       XDataStream_Status_WriteFailed,
                   "raw: 无承载写置 WriteFailed");
        XClassDeinit((XClass*)&bare);
    }

    /* atEnd 语义。 */
    XDataStream_init_arrayReadOnly(&rs, buf);
    xds_expect(!XDataStream_atEnd(&rs), "raw: 有数据时 atEnd 为 false");
    XDataStream_readRawData(&rs, out, 5);
    xds_expect(XDataStream_atEnd(&rs), "raw: 读完后 atEnd 为 true");

    XClassDeinit((XClass*)&ws);
    XClassDeinit((XClass*)&rs);
    XClassDelete((XClass*)buf);
}

/* ==================== 事务 ==================== */

static void xds_testTransaction(void)
{
    XByteArray* buf = XByteArray_create();
    XDataStream ws;
    XDataStream rs;

    if (!buf) { xds_expect(false, "tx: 数组创建"); return; }

    XDataStream_init_array(&ws, buf);
    xds_expect(!XDataStream_isDeviceTransactionStarted(&ws),
               "tx: 初始不在事务中");
    XDataStream_startTransaction(&ws);
    xds_expect(XDataStream_isDeviceTransactionStarted(&ws),
               "tx: startTransaction 后处于事务中");
    xds_expect(XDataStream_commitTransaction(&ws),
               "tx: 无错误时 commit 成功");
    xds_expect(!XDataStream_isDeviceTransactionStarted(&ws),
               "tx: commit 后退出事务");

    /* 未在事务中 commit → false。 */
    xds_expect(!XDataStream_commitTransaction(&ws),
               "tx: 未开始就 commit 返回 false");

    /* 事务内出错 → commit 返回 false。 */
    {
        XDataStream bare;
        XDataStream_init(&bare);
        XDataStream_startTransaction(&bare);
        XDataStream_writeRawData(&bare, "x", 1); /* 无承载 → 失败 */
        xds_expect(!XDataStream_commitTransaction(&bare),
                   "tx: 事务内有错误时 commit 返回 false");
        XClassDeinit((XClass*)&bare);
    }

    /* startTransaction 复位状态（对齐 Qt StreamStateSaver）。 */
    {
        XDataStream bare;
        XDataStream_init(&bare);
        XDataStream_setStatus(&bare, XDataStream_Status_ReadCorruptData);
        XDataStream_startTransaction(&bare);
        xds_expect(XDataStream_status(&bare) == XDataStream_Status_Ok,
                   "tx: startTransaction 复位状态为 Ok");
        XDataStream_abortTransaction(&bare);
        XClassDeinit((XClass*)&bare);
    }

    /* abort 是 rollback 的别名。 */
    XDataStream_startTransaction(&ws);
    XDataStream_abortTransaction(&ws);
    xds_expect(!XDataStream_isDeviceTransactionStarted(&ws),
               "tx: abortTransaction 退出事务");

    (void)rs;
    XClassDeinit((XClass*)&ws);
    XClassDelete((XClass*)buf);
}

/* ==================== 流属性 ==================== */

static void xds_testProperties(void)
{
    XDataStream s;
    XDataStream_init(&s);

    xds_expect(XDataStream_version(&s) == XDataStream_Version_Default,
               "prop: 缺省版本为 Default(22)");
    xds_expect(XDataStream_byteOrder(&s) ==
                   XDataStream_ByteOrder_BigEndian,
               "prop: 缺省字节序为大端");
    xds_expect(XDataStream_floatingPointPrecision(&s) ==
                   XDataStream_FloatingPointPrecision_Double,
               "prop: 缺省浮点精度为双精度");
    xds_expect(XDataStream_status(&s) == XDataStream_Status_Ok,
               "prop: 缺省状态为 Ok");
    xds_expect(XDataStream_device(&s) == NULL, "prop: 缺省无绑定设备");

    XDataStream_setVersion(&s, XDataStream_Version_5);
    xds_expect(XDataStream_version(&s) == XDataStream_Version_5,
               "prop: setVersion 往返");
    XDataStream_setByteOrder(&s, XDataStream_ByteOrder_LittleEndian);
    xds_expect(XDataStream_byteOrder(&s) ==
                   XDataStream_ByteOrder_LittleEndian,
               "prop: setByteOrder 往返");
    XDataStream_setStatus(&s, XDataStream_Status_WriteFailed);
    XDataStream_resetStatus(&s);
    xds_expect(XDataStream_status(&s) == XDataStream_Status_Ok,
               "prop: resetStatus 复位");

    /* NULL 安全。 */
    xds_expect(XDataStream_version(NULL) == XDataStream_Version_Default,
               "prop: version(NULL) 返回缺省");
    xds_expect(XDataStream_device(NULL) == NULL, "prop: device(NULL) 为 NULL");

    XClassDeinit((XClass*)&s);
}

/* ==================== 入口 ==================== */

int XDataStreamTest_runAll(void)
{
    xds_failures = 0;
xds_testProperties();
xds_testByteOrderAndInts();
xds_testBoolAndFloat();
xds_testBytesBlock();
xds_testString();
xds_testRawAndErrors();
xds_testTransaction();

    return xds_failures;
}
