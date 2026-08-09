#include "XProtocolTest.h"
#include "XCanBus.h"
#include "XCanBusFrame.h"
#include "XCanBusDevice.h"
#include "XCanBusDeviceInfo.h"
#include "XCanBusDevice_Protected.h"
#include "XCanCommonDefinitions.h"
#include "XCanMessageDescription.h"
#include "XCanSignalDescription.h"
#include "XCanUniqueIdDescription.h"
#include "XCanFrameProcessor.h"
#include "XCanDbcFileParser.h"
#include "XMemory.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XString.h"
#include "XStringList.h"
#include "XVector.h"
#include "XMap.h"
#include "XByteArray.h"
#include "XVariant.h"
#include <string.h>
#include <stdio.h>

/******************************************************************************************
 * @brief XCanBusFrame 单元测试
 * @details 测试 CAN 帧的创建、初始化、属性设置/获取、标志位操作、时间戳、字符串表示等
 ******************************************************************************************/
static void XCanBusFrameTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanBusFrame 单元测试 ==========\n");

    // ========== 1. 创建与初始化测试 ==========
    {
        // 测试栈上 init
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);
        if (XCanBusFrame_frameType(&frame) == XCanBusFrame_DataFrame) {
            XPrintf("  [通过] XCanBusFrame_init(DataFrame) 类型正确\n");
            pass++;
        } else {
            XPrintf("  [失败] XCanBusFrame_init(DataFrame) 类型错误\n");
            fail++;
        }
        XCanBusFrame_deinit(&frame);

        // 测试堆上 create
        XCanBusFrame* f1 = XCanBusFrame_create(XCanBusFrame_DataFrame);
        if (f1 && XCanBusFrame_isValid(f1)) {
            XPrintf("  [通过] XCanBusFrame_create(DataFrame) 创建成功\n");
            pass++;
        } else {
            XPrintf("  [失败] XCanBusFrame_create(DataFrame) 创建失败\n");
            fail++;
        }
        XCanBusFrame_delete(f1);

        // 测试 create_with_data
        uint8_t testData[] = {0x11, 0x22, 0x33, 0x44};
        XCanBusFrame* f2 = XCanBusFrame_create_with_data(0x123, testData, 4);
        if (f2 && XCanBusFrame_frameId(f2) == 0x123) {
            XPrintf("  [通过] XCanBusFrame_create_with_data ID 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] XCanBusFrame_create_with_data ID 错误\n");
            fail++;
        }
        XCanBusFrame_delete(f2);
    }

    // ========== 2. 帧 ID 测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);

        // 标准帧 ID
        XCanBusFrame_setFrameId(&frame, 0x7FF);
        if (XCanBusFrame_frameId(&frame) == 0x7FF) {
            XPrintf("  [通过] setFrameId(0x7FF) 标准帧 ID 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setFrameId(0x7FF) = 0x%X, 期望 0x7FF\n",
                    XCanBusFrame_frameId(&frame));
            fail++;
        }

        // 扩展帧 ID（自动设置扩展标志）
        XCanBusFrame_setFrameId(&frame, 0x1FFFFFFF);
        if (XCanBusFrame_hasExtendedFrameFormat(&frame)) {
            XPrintf("  [通过] 扩展帧 ID 自动设置扩展标志\n");
            pass++;
        } else {
            XPrintf("  [失败] 扩展帧 ID 未自动设置扩展标志\n");
            fail++;
        }

        // 无效 ID
        XCanBusFrame_setFrameId(&frame, 0x20000000);
        if (!XCanBusFrame_isValid(&frame)) {
            XPrintf("  [通过] 无效 ID 导致 isValid 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] 无效 ID 未导致 isValid 返回 false\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 3. 负载数据测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);

        uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        XCanBusFrame_setPayload(&frame, data, 8);

        XByteArray* payload = XCanBusFrame_payload(&frame);
        if (payload && XByteArray_size_base(payload) == 8) {
            const uint8_t* pdata = XByteArray_data(payload);
            bool match = true;
            for (int i = 0; i < 8; i++) {
                if (pdata[i] != data[i]) { match = false; break; }
            }
            if (match) {
                XPrintf("  [通过] setPayload 8 字节数据正确\n");
                pass++;
            } else {
                XPrintf("  [失败] setPayload 数据不匹配\n");
                fail++;
            }
        } else {
            XPrintf("  [失败] setPayload 返回 NULL 或长度错误\n");
            fail++;
        }
        XByteArray_delete_base(payload);

        // 测试 setPayload_from_array
        XByteArray* ba = XByteArray_create();
        XByteArray_append_2(ba, data, 8);
        XCanBusFrame_setPayload_from_array(&frame, ba);
        XByteArray_delete_base(ba);

        XByteArray* payload2 = XCanBusFrame_payload(&frame);
        if (payload2 && XByteArray_size_base(payload2) == 8) {
            XPrintf("  [通过] setPayload_from_array 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setPayload_from_array 失败\n");
            fail++;
        }
        XByteArray_delete_base(payload2);

        XCanBusFrame_deinit(&frame);
    }

    // ========== 4. 帧类型测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_RemoteRequestFrame);
        if (XCanBusFrame_frameType(&frame) == XCanBusFrame_RemoteRequestFrame) {
            XPrintf("  [通过] RemoteRequestFrame 类型正确\n");
            pass++;
        } else {
            XPrintf("  [失败] RemoteRequestFrame 类型错误\n");
            fail++;
        }

        XCanBusFrame_setFrameType(&frame, XCanBusFrame_ErrorFrame);
        if (XCanBusFrame_frameType(&frame) == XCanBusFrame_ErrorFrame) {
            XPrintf("  [通过] setFrameType(ErrorFrame) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setFrameType(ErrorFrame) 错误\n");
            fail++;
        }

        XCanBusFrame_setFrameType(&frame, XCanBusFrame_InvalidFrame);
        if (!XCanBusFrame_isValid(&frame)) {
            XPrintf("  [通过] InvalidFrame 导致 isValid 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] InvalidFrame 未导致 isValid 返回 false\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 5. 扩展帧格式测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);

        XCanBusFrame_setExtendedFrameFormat(&frame, true);
        if (XCanBusFrame_hasExtendedFrameFormat(&frame)) {
            XPrintf("  [通过] setExtendedFrameFormat(true) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setExtendedFrameFormat(true) 未生效\n");
            fail++;
        }

        XCanBusFrame_setExtendedFrameFormat(&frame, false);
        if (!XCanBusFrame_hasExtendedFrameFormat(&frame)) {
            XPrintf("  [通过] setExtendedFrameFormat(false) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setExtendedFrameFormat(false) 未生效\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 6. CAN FD 标志测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);

        XCanBusFrame_setFlexibleDataRateFormat(&frame, true);
        if (XCanBusFrame_hasFlexibleDataRateFormat(&frame)) {
            XPrintf("  [通过] setFlexibleDataRateFormat(true) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setFlexibleDataRateFormat(true) 未生效\n");
            fail++;
        }

        // 位速率切换
        XCanBusFrame_setBitrateSwitch(&frame, true);
        if (XCanBusFrame_hasBitrateSwitch(&frame) && XCanBusFrame_hasFlexibleDataRateFormat(&frame)) {
            XPrintf("  [通过] setBitrateSwitch(true) 自动启用 CAN FD\n");
            pass++;
        } else {
            XPrintf("  [失败] setBitrateSwitch(true) 未自动启用 CAN FD\n");
            fail++;
        }

        // 错误状态指示
        XCanBusFrame_setErrorStateIndicator(&frame, true);
        if (XCanBusFrame_hasErrorStateIndicator(&frame)) {
            XPrintf("  [通过] setErrorStateIndicator(true) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setErrorStateIndicator(true) 未生效\n");
            fail++;
        }

        // 禁用 CAN FD 时清除相关标志
        XCanBusFrame_setFlexibleDataRateFormat(&frame, false);
        if (!XCanBusFrame_hasBitrateSwitch(&frame) && !XCanBusFrame_hasErrorStateIndicator(&frame)) {
            XPrintf("  [通过] 禁用 CAN FD 清除位速率切换和错误状态指示\n");
            pass++;
        } else {
            XPrintf("  [失败] 禁用 CAN FD 未清除相关标志\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 7. 本地回显测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);

        XCanBusFrame_setLocalEcho(&frame, true);
        if (XCanBusFrame_hasLocalEcho(&frame)) {
            XPrintf("  [通过] setLocalEcho(true) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setLocalEcho(true) 未生效\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 8. 时间戳测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);

        XCanBusFrame_TimeStamp ts = XCanBusFrame_TimeStamp_fromMicroSeconds(1234567);
        XCanBusFrame_setTimeStamp(&frame, ts);

        XCanBusFrame_TimeStamp got = XCanBusFrame_timeStamp(&frame);
        if (got.m_secs == 1 && got.m_usecs == 234567) {
            XPrintf("  [通过] 时间戳 fromMicroSeconds(1234567) = 1s 234567us\n");
            pass++;
        } else {
            XPrintf("  [失败] 时间戳 = %lds %ldus, 期望 1s 234567us\n",
                    (long)got.m_secs, (long)got.m_usecs);
            fail++;
        }

        // 测试 TimeStamp_seconds / TimeStamp_microSeconds
        if (XCanBusFrame_TimeStamp_seconds(ts) == 1 &&
            XCanBusFrame_TimeStamp_microSeconds(ts) == 234567) {
            XPrintf("  [通过] TimeStamp_seconds/microSeconds 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] TimeStamp_seconds/microSeconds 错误\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 9. 错误帧测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_ErrorFrame);

        XCanBusFrame_setError(&frame, XCanBusFrame_BusOffError);
        uint32_t err = XCanBusFrame_error(&frame);
        if (err == XCanBusFrame_BusOffError) {
            XPrintf("  [通过] 错误帧 setError(BusOffError) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] 错误帧 error = 0x%X, 期望 0x%X\n",
                    err, XCanBusFrame_BusOffError);
            fail++;
        }

        // 非错误帧返回 NoError
        XCanBusFrame_setFrameType(&frame, XCanBusFrame_DataFrame);
        if (XCanBusFrame_error(&frame) == XCanBusFrame_NoError) {
            XPrintf("  [通过] 非错误帧返回 NoError\n");
            pass++;
        } else {
            XPrintf("  [失败] 非错误帧未返回 NoError\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 10. toString 测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);
        XCanBusFrame_setFrameId(&frame, 0x7FF);
        uint8_t data[] = {0x01};
        XCanBusFrame_setPayload(&frame, data, 1);

        XString* str = XCanBusFrame_toString(&frame);
        if (str) {
            XPrintf("  [通过] toString = \"%s\"\n", XString_toUtf8(str));
            pass++;
            XString_delete_base(str);
        } else {
            XPrintf("  [失败] toString 返回 NULL\n");
            fail++;
        }

        // 错误帧 toString
        XCanBusFrame_setFrameType(&frame, XCanBusFrame_ErrorFrame);
        XString* errStr = XCanBusFrame_toString(&frame);
        if (errStr) {
            XPrintf("  [通过] ErrorFrame toString = \"%s\"\n", XString_toUtf8(errStr));
            pass++;
            XString_delete_base(errStr);
        } else {
            XPrintf("  [失败] ErrorFrame toString 返回 NULL\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 11. isValid 综合测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);
        XCanBusFrame_setFrameId(&frame, 0x123);

        if (XCanBusFrame_isValid(&frame)) {
            XPrintf("  [通过] 有效数据帧 isValid 返回 true\n");
            pass++;
        } else {
            XPrintf("  [失败] 有效数据帧 isValid 返回 false\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
    }

    // ========== 12. NULL 指针安全性测试 ==========
    {
        if (XCanBusFrame_isValid(NULL) == false) {
            XPrintf("  [通过] isValid(NULL) 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] isValid(NULL) 未返回 false\n");
            fail++;
        }

        if (XCanBusFrame_frameId(NULL) == 0) {
            XPrintf("  [通过] frameId(NULL) 返回 0\n");
            pass++;
        } else {
            XPrintf("  [失败] frameId(NULL) 未返回 0\n");
            fail++;
        }

        if (XCanBusFrame_payload(NULL) == NULL) {
            XPrintf("  [通过] payload(NULL) 返回 NULL\n");
            pass++;
        } else {
            XPrintf("  [失败] payload(NULL) 未返回 NULL\n");
            fail++;
        }

        if (XCanBusFrame_toString(NULL) != NULL) {
            XPrintf("  [通过] toString(NULL) 返回非 NULL\n");
            pass++;
        } else {
            XPrintf("  [失败] toString(NULL) 返回 NULL\n");
            fail++;
        }

        // NULL 指针的 init/deinit/delete 不应崩溃
        XCanBusFrame_init(NULL, XCanBusFrame_DataFrame);
        XCanBusFrame_deinit(NULL);
        XCanBusFrame_delete(NULL);
        XPrintf("  [通过] NULL 指针 init/deinit/delete 不崩溃\n");
        pass++;
    }

    // ========== 13. create_copy 测试 ==========
    {
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);
        XCanBusFrame_setFrameId(&frame, 0x456);
        uint8_t data[] = {0xAA, 0xBB};
        XCanBusFrame_setPayload(&frame, data, 2);

        XCanBusFrame* copy = XCanBusFrame_create_copy(&frame);
        if (copy && XCanBusFrame_frameId(copy) == 0x456) {
            XByteArray* cp = XCanBusFrame_payload(copy);
            bool match = cp && XByteArray_size_base(cp) == 2;
            XByteArray_delete_base(cp);
            if (match) {
                XPrintf("  [通过] create_copy 深拷贝正确\n");
                pass++;
            } else {
                XPrintf("  [失败] create_copy 负载不匹配\n");
                fail++;
            }
        } else {
            XPrintf("  [失败] create_copy 失败\n");
            fail++;
        }
        XCanBusFrame_delete(copy);
        XCanBusFrame_deinit(&frame);
    }

    XPrintf("========== XCanBusFrame 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCanBusDeviceInfo 单元测试
 * @details 测试设备信息的初始化、属性访问、深拷贝等
 ******************************************************************************************/
static void XCanBusDeviceInfoTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanBusDeviceInfo 单元测试 ==========\n");

    // ========== 1. init/deinit 测试 ==========
    {
        XCanBusDeviceInfo info;
        XCanBusDeviceInfo_init(&info);
        if (info.m_channel == 0 && info.m_hasFlexibleDataRate == false && info.m_isVirtual == false) {
            XPrintf("  [通过] init 默认值正确\n");
            pass++;
        } else {
            XPrintf("  [失败] init 默认值错误\n");
            fail++;
        }
        XCanBusDeviceInfo_deinit(&info);
    }

    // ========== 2. 属性设置与获取测试 ==========
    {
        XCanBusDeviceInfo info;
        XCanBusDeviceInfo_init(&info);

        // 使用工厂方法设置
        XCanBusDevice_createDeviceInfo(&info, "socketcan", "can0", false, true);

        XString* plugin = XCanBusDeviceInfo_plugin(&info);
        XString* name = XCanBusDeviceInfo_name(&info);

        if (plugin && name &&
            strcmp(XString_toUtf8(plugin), "socketcan") == 0 &&
            strcmp(XString_toUtf8(name), "can0") == 0) {
            XPrintf("  [通过] createDeviceInfo plugin/name 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] createDeviceInfo plugin/name 错误\n");
            fail++;
        }
        XString_delete_base(plugin);
        XString_delete_base(name);

        if (XCanBusDeviceInfo_hasFlexibleDataRate(&info) == true &&
            XCanBusDeviceInfo_isVirtual(&info) == false) {
            XPrintf("  [通过] hasFlexibleDataRate/isVirtual 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] hasFlexibleDataRate/isVirtual 错误\n");
            fail++;
        }

        XCanBusDeviceInfo_deinit(&info);
    }

    // ========== 3. createDeviceInfo_full 测试 ==========
    {
        XCanBusDeviceInfo info;
        XCanBusDeviceInfo_init(&info);

        XCanBusDevice_createDeviceInfo_full(&info,
            "peakcan", "PCAN_USB_0",
            "123456", "PCAN USB Pro FD",
            "mycan", 0, false, true);

        XString* desc = XCanBusDeviceInfo_description(&info);
        XString* serial = XCanBusDeviceInfo_serialNumber(&info);
        XString* alias = XCanBusDeviceInfo_alias(&info);

        bool ok = desc && serial && alias &&
            strcmp(XString_toUtf8(desc), "PCAN USB Pro FD") == 0 &&
            strcmp(XString_toUtf8(serial), "123456") == 0 &&
            strcmp(XString_toUtf8(alias), "mycan") == 0 &&
            XCanBusDeviceInfo_channel(&info) == 0;

        XString_delete_base(desc);
        XString_delete_base(serial);
        XString_delete_base(alias);

        if (ok) {
            XPrintf("  [通过] createDeviceInfo_full 完整属性正确\n");
            pass++;
        } else {
            XPrintf("  [失败] createDeviceInfo_full 属性错误\n");
            fail++;
        }

        XCanBusDeviceInfo_deinit(&info);
    }

    // ========== 4. copy 深拷贝测试 ==========
    {
        XCanBusDeviceInfo info1, info2;
        XCanBusDeviceInfo_init(&info1);
        XCanBusDeviceInfo_init(&info2);

        XCanBusDevice_createDeviceInfo(&info1, "virtualcan", "vcan0", true, false);
        XCanBusDeviceInfo_copy(&info2, &info1);

        XString* name1 = XCanBusDeviceInfo_name(&info1);
        XString* name2 = XCanBusDeviceInfo_name(&info2);

        bool ok = name1 && name2 &&
            strcmp(XString_toUtf8(name1), XString_toUtf8(name2)) == 0 &&
            XCanBusDeviceInfo_isVirtual(&info2) == true;

        XString_delete_base(name1);
        XString_delete_base(name2);

        if (ok) {
            XPrintf("  [通过] copy 深拷贝正确\n");
            pass++;
        } else {
            XPrintf("  [失败] copy 深拷贝错误\n");
            fail++;
        }

        XCanBusDeviceInfo_deinit(&info1);
        XCanBusDeviceInfo_deinit(&info2);
    }

    // ========== 5. NULL 指针安全性测试 ==========
    {
        if (XCanBusDeviceInfo_plugin(NULL) != NULL) {
            XPrintf("  [通过] plugin(NULL) 返回非 NULL 空字符串\n");
            pass++;
        } else {
            XPrintf("  [失败] plugin(NULL) 返回 NULL\n");
            fail++;
        }

        if (XCanBusDeviceInfo_channel(NULL) == 0) {
            XPrintf("  [通过] channel(NULL) 返回 0\n");
            pass++;
        } else {
            XPrintf("  [失败] channel(NULL) 未返回 0\n");
            fail++;
        }

        XCanBusDeviceInfo_init(NULL);
        XCanBusDeviceInfo_deinit(NULL);
        XCanBusDeviceInfo_copy(NULL, NULL);
        XPrintf("  [通过] NULL 指针 init/deinit/copy 不崩溃\n");
        pass++;
    }

    XPrintf("========== XCanBusDeviceInfo 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCanSignalDescription 单元测试
 * @details 测试信号描述的初始化、属性设置/获取、深拷贝等
 ******************************************************************************************/
static void XCanSignalDescriptionTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanSignalDescription 单元测试 ==========\n");

    // ========== 1. init/deinit 测试 ==========
    {
        XCanSignalDescription sig;
        XCanSignalDescription_init(&sig);
        if (sig.m_factor == 1.0 && sig.m_offset == 0.0 &&
            sig.m_dataSource == XCanBus_Payload && sig.m_dataEndian == 1) {
            XPrintf("  [通过] init 默认值正确\n");
            pass++;
        } else {
            XPrintf("  [失败] init 默认值错误\n");
            fail++;
        }
        XCanSignalDescription_deinit(&sig);
    }

    // ========== 2. 属性设置与获取测试 ==========
    {
        XCanSignalDescription sig;
        XCanSignalDescription_init(&sig);

        XCanSignalDescription_setName(&sig, "EngineSpeed");
        XCanSignalDescription_setStartBit(&sig, 0);
        XCanSignalDescription_setBitLength(&sig, 16);
        XCanSignalDescription_setFactor(&sig, 0.125);
        XCanSignalDescription_setOffset(&sig, 0.0);
        XCanSignalDescription_setRange(&sig, 0.0, 8000.0);
        XCanSignalDescription_setPhysicalUnit(&sig, "rpm");
        XCanSignalDescription_setReceiver(&sig, "ECU");

        XString* name = XCanSignalDescription_name(&sig);
        XString* unit = XCanSignalDescription_physicalUnit(&sig);
        XString* receiver = XCanSignalDescription_receiver(&sig);

        bool ok = name && unit && receiver &&
            strcmp(XString_toUtf8(name), "EngineSpeed") == 0 &&
            strcmp(XString_toUtf8(unit), "rpm") == 0 &&
            strcmp(XString_toUtf8(receiver), "ECU") == 0 &&
            XCanSignalDescription_startBit(&sig) == 0 &&
            XCanSignalDescription_bitLength(&sig) == 16 &&
            XCanSignalDescription_factor(&sig) == 0.125 &&
            XCanSignalDescription_offset(&sig) == 0.0 &&
            XCanSignalDescription_minimum(&sig) == 0.0 &&
            XCanSignalDescription_maximum(&sig) == 8000.0;

        XString_delete_base(name);
        XString_delete_base(unit);
        XString_delete_base(receiver);

        if (ok) {
            XPrintf("  [通过] 信号属性设置/获取正确\n");
            pass++;
        } else {
            XPrintf("  [失败] 信号属性设置/获取错误\n");
            fail++;
        }

        XCanSignalDescription_deinit(&sig);
    }

    // ========== 3. isValid 测试 ==========
    {
        XCanSignalDescription sig;
        XCanSignalDescription_init(&sig);

        // 无名称且无位长度，无效
        if (XCanSignalDescription_isValid(&sig) == false) {
            XPrintf("  [通过] 空信号 isValid 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] 空信号 isValid 未返回 false\n");
            fail++;
        }

        // 设置名称后有效
        XCanSignalDescription_setName(&sig, "TestSig");
        XCanSignalDescription_setBitLength(&sig, 8);
        if (XCanSignalDescription_isValid(&sig)) {
            XPrintf("  [通过] 有效信号 isValid 返回 true\n");
            pass++;
        } else {
            XPrintf("  [失败] 有效信号 isValid 返回 false\n");
            fail++;
        }

        XCanSignalDescription_deinit(&sig);
    }

    // ========== 4. dataSource/dataFormat 测试 ==========
    {
        XCanSignalDescription sig;
        XCanSignalDescription_init(&sig);

        XCanSignalDescription_setDataSource(&sig, XCanBus_FrameId);
        XCanSignalDescription_setDataFormat(&sig, XCanBus_SignedInteger);
        XCanSignalDescription_setDataEndian(&sig, 0);

        if (XCanSignalDescription_dataSource(&sig) == XCanBus_FrameId &&
            XCanSignalDescription_dataFormat(&sig) == XCanBus_SignedInteger &&
            XCanSignalDescription_dataEndian(&sig) == 0) {
            XPrintf("  [通过] dataSource/dataFormat/dataEndian 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] dataSource/dataFormat/dataEndian 错误\n");
            fail++;
        }

        XCanSignalDescription_deinit(&sig);
    }

    // ========== 5. copy 深拷贝测试 ==========
    {
        XCanSignalDescription sig1, sig2;
        XCanSignalDescription_init(&sig1);
        XCanSignalDescription_init(&sig2);

        XCanSignalDescription_setName(&sig1, "CopyTest");
        XCanSignalDescription_setBitLength(&sig1, 32);
        XCanSignalDescription_setFactor(&sig1, 2.0);

        XCanSignalDescription_copy(&sig2, &sig1);

        XString* name2 = XCanSignalDescription_name(&sig2);

        bool ok = name2 &&
            strcmp(XString_toUtf8(name2), "CopyTest") == 0 &&
            XCanSignalDescription_bitLength(&sig2) == 32 &&
            XCanSignalDescription_factor(&sig2) == 2.0;

        XString_delete_base(name2);

        if (ok) {
            XPrintf("  [通过] copy 深拷贝正确\n");
            pass++;
        } else {
            XPrintf("  [失败] copy 深拷贝错误\n");
            fail++;
        }

        XCanSignalDescription_deinit(&sig1);
        XCanSignalDescription_deinit(&sig2);
    }

    // ========== 6. NULL 指针安全性测试 ==========
    {
        if (XCanSignalDescription_name(NULL) != NULL) {
            XPrintf("  [通过] name(NULL) 返回非 NULL 空字符串\n");
            pass++;
        } else {
            XPrintf("  [失败] name(NULL) 返回 NULL\n");
            fail++;
        }

        if (XCanSignalDescription_isValid(NULL) == false) {
            XPrintf("  [通过] isValid(NULL) 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] isValid(NULL) 未返回 false\n");
            fail++;
        }

        XCanSignalDescription_init(NULL);
        XCanSignalDescription_deinit(NULL);
        XCanSignalDescription_copy(NULL, NULL);
        XPrintf("  [通过] NULL 指针 init/deinit/copy 不崩溃\n");
        pass++;
    }

    XPrintf("========== XCanSignalDescription 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCanMessageDescription 单元测试
 * @details 测试消息描述的初始化、属性设置/获取、信号管理、深拷贝等
 ******************************************************************************************/
static void XCanMessageDescriptionTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanMessageDescription 单元测试 ==========\n");

    // ========== 1. init/deinit 测试 ==========
    {
        XCanMessageDescription msg;
        XCanMessageDescription_init(&msg);
        if (msg.m_uniqueId == 0 && msg.m_size == 0 && msg.m_name == NULL) {
            XPrintf("  [通过] init 默认值正确\n");
            pass++;
        } else {
            XPrintf("  [失败] init 默认值错误\n");
            fail++;
        }
        XCanMessageDescription_deinit(&msg);
    }

    // ========== 2. 属性设置与获取测试 ==========
    {
        XCanMessageDescription msg;
        XCanMessageDescription_init(&msg);

        XCanMessageDescription_setUniqueId(&msg, 0x123);
        XCanMessageDescription_setName(&msg, "EngineData");
        XCanMessageDescription_setSize(&msg, 8);
        XCanMessageDescription_setTransmitter(&msg, "ECU");
        XCanMessageDescription_setComment(&msg, "Engine status data");

        XString* name = XCanMessageDescription_name(&msg);
        XString* transmitter = XCanMessageDescription_transmitter(&msg);
        XString* comment = XCanMessageDescription_comment(&msg);

        bool ok = name && transmitter && comment &&
            strcmp(XString_toUtf8(name), "EngineData") == 0 &&
            strcmp(XString_toUtf8(transmitter), "ECU") == 0 &&
            strcmp(XString_toUtf8(comment), "Engine status data") == 0 &&
            XCanMessageDescription_uniqueId(&msg) == 0x123 &&
            XCanMessageDescription_size(&msg) == 8;

        XString_delete_base(name);
        XString_delete_base(transmitter);
        XString_delete_base(comment);

        if (ok) {
            XPrintf("  [通过] 消息属性设置/获取正确\n");
            pass++;
        } else {
            XPrintf("  [失败] 消息属性设置/获取错误\n");
            fail++;
        }

        XCanMessageDescription_deinit(&msg);
    }

    // ========== 3. isValid 测试 ==========
    {
        XCanMessageDescription msg;
        XCanMessageDescription_init(&msg);

        // 空消息无效
        if (XCanMessageDescription_isValid(&msg) == false) {
            XPrintf("  [通过] 空消息 isValid 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] 空消息 isValid 未返回 false\n");
            fail++;
        }

        // 设置名称后有效
        XCanMessageDescription_setName(&msg, "TestMsg");
        if (XCanMessageDescription_isValid(&msg)) {
            XPrintf("  [通过] 有名称的消息 isValid 返回 true\n");
            pass++;
        } else {
            XPrintf("  [失败] 有名称的消息 isValid 返回 false\n");
            fail++;
        }

        XCanMessageDescription_deinit(&msg);
    }

    // ========== 4. 信号管理测试 ==========
    {
        XCanMessageDescription msg;
        XCanMessageDescription_init(&msg);

        // 创建并添加信号
        XCanSignalDescription sig1, sig2;
        XCanSignalDescription_init(&sig1);
        XCanSignalDescription_init(&sig2);

        XCanSignalDescription_setName(&sig1, "Speed");
        XCanSignalDescription_setStartBit(&sig1, 0);
        XCanSignalDescription_setBitLength(&sig1, 16);

        XCanSignalDescription_setName(&sig2, "Temp");
        XCanSignalDescription_setStartBit(&sig2, 16);
        XCanSignalDescription_setBitLength(&sig2, 8);

        XCanMessageDescription_addSignalDescription(&msg, &sig1);
        XCanMessageDescription_addSignalDescription(&msg, &sig2);

        // 通过名称查找信号
        XCanSignalDescription found;
        XCanSignalDescription_init(&found);
        bool foundOk = XCanMessageDescription_signalDescriptionForName(&msg, "Speed", &found);

        if (foundOk) {
            XString* foundName = XCanSignalDescription_name(&found);
            bool match = foundName &&
                strcmp(XString_toUtf8(foundName), "Speed") == 0 &&
                XCanSignalDescription_startBit(&found) == 0;
            XString_delete_base(foundName);

            if (match) {
                XPrintf("  [通过] signalDescriptionForName 找到信号\n");
                pass++;
            } else {
                XPrintf("  [失败] signalDescriptionForName 信号属性错误\n");
                fail++;
            }
        } else {
            XPrintf("  [失败] signalDescriptionForName 未找到信号\n");
            fail++;
        }
        XCanSignalDescription_deinit(&found);

        // 查找不存在的信号
        XCanSignalDescription_init(&found);
        if (XCanMessageDescription_signalDescriptionForName(&msg, "NonExistent", &found) == false) {
            XPrintf("  [通过] signalDescriptionForName 不存在的信号返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] signalDescriptionForName 不存在的信号未返回 false\n");
            fail++;
        }
        XCanSignalDescription_deinit(&found);

        // 获取信号列表
        XVector* sigs = XCanMessageDescription_signalDescriptions(&msg);
        if (sigs && XVector_size_base(sigs) == 2) {
            XPrintf("  [通过] signalDescriptions 返回 2 个信号\n");
            pass++;
        } else {
            XPrintf("  [失败] signalDescriptions 返回 %zu 个信号, 期望 2\n",
                    sigs ? XVector_size_base(sigs) : 0);
            fail++;
        }
        XVector_delete_base(sigs);

        // 清除信号
        XCanMessageDescription_clearSignalDescriptions(&msg);
        XVector* sigs2 = XCanMessageDescription_signalDescriptions(&msg);
        if (sigs2 && XVector_size_base(sigs2) == 0) {
            XPrintf("  [通过] clearSignalDescriptions 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] clearSignalDescriptions 后信号数不为 0\n");
            fail++;
        }
        XVector_delete_base(sigs2);

        XCanSignalDescription_deinit(&sig1);
        XCanSignalDescription_deinit(&sig2);
        XCanMessageDescription_deinit(&msg);
    }

    // ========== 5. copy 深拷贝测试 ==========
    {
        XCanMessageDescription msg1, msg2;
        XCanMessageDescription_init(&msg1);
        XCanMessageDescription_init(&msg2);

        XCanMessageDescription_setUniqueId(&msg1, 0x456);
        XCanMessageDescription_setName(&msg1, "CopyMsg");

        XCanMessageDescription_copy(&msg2, &msg1);

        XString* name2 = XCanMessageDescription_name(&msg2);

        bool ok = name2 &&
            strcmp(XString_toUtf8(name2), "CopyMsg") == 0 &&
            XCanMessageDescription_uniqueId(&msg2) == 0x456;

        XString_delete_base(name2);

        if (ok) {
            XPrintf("  [通过] copy 深拷贝正确\n");
            pass++;
        } else {
            XPrintf("  [失败] copy 深拷贝错误\n");
            fail++;
        }

        XCanMessageDescription_deinit(&msg1);
        XCanMessageDescription_deinit(&msg2);
    }

    // ========== 6. NULL 指针安全性测试 ==========
    {
        if (XCanMessageDescription_isValid(NULL) == false) {
            XPrintf("  [通过] isValid(NULL) 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] isValid(NULL) 未返回 false\n");
            fail++;
        }

        if (XCanMessageDescription_uniqueId(NULL) == 0) {
            XPrintf("  [通过] uniqueId(NULL) 返回 0\n");
            pass++;
        } else {
            XPrintf("  [失败] uniqueId(NULL) 未返回 0\n");
            fail++;
        }

        XCanMessageDescription_init(NULL);
        XCanMessageDescription_deinit(NULL);
        XCanMessageDescription_copy(NULL, NULL);
        XPrintf("  [通过] NULL 指针 init/deinit/copy 不崩溃\n");
        pass++;
    }

    XPrintf("========== XCanMessageDescription 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCanUniqueIdDescription 单元测试
 * @details 测试唯一 ID 描述的初始化、属性设置/获取等
 ******************************************************************************************/
static void XCanUniqueIdDescriptionTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanUniqueIdDescription 单元测试 ==========\n");

    // ========== 1. init 测试 ==========
    {
        XCanUniqueIdDescription desc;
        XCanUniqueIdDescription_init(&desc);
        if (desc.m_source == XCanBus_Payload && desc.m_startBit == 0 &&
            desc.m_bitLength == 0 && desc.m_endian == 1) {
            XPrintf("  [通过] init 默认值正确\n");
            pass++;
        } else {
            XPrintf("  [失败] init 默认值错误\n");
            fail++;
        }
    }

    // ========== 2. 属性设置与获取测试 ==========
    {
        XCanUniqueIdDescription desc;
        XCanUniqueIdDescription_init(&desc);

        XCanUniqueIdDescription_setSource(&desc, XCanBus_FrameId);
        XCanUniqueIdDescription_setStartBit(&desc, 0);
        XCanUniqueIdDescription_setBitLength(&desc, 29);
        XCanUniqueIdDescription_setEndian(&desc, 1);

        if (XCanUniqueIdDescription_source(&desc) == XCanBus_FrameId &&
            XCanUniqueIdDescription_startBit(&desc) == 0 &&
            XCanUniqueIdDescription_bitLength(&desc) == 29 &&
            XCanUniqueIdDescription_endian(&desc) == 1) {
            XPrintf("  [通过] 属性设置/获取正确\n");
            pass++;
        } else {
            XPrintf("  [失败] 属性设置/获取错误\n");
            fail++;
        }
    }

    // ========== 3. isValid 测试 ==========
    {
        XCanUniqueIdDescription desc;
        XCanUniqueIdDescription_init(&desc);

        // 默认无效（bitLength == 0）
        if (XCanUniqueIdDescription_isValid(&desc) == false) {
            XPrintf("  [通过] 默认描述 isValid 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] 默认描述 isValid 未返回 false\n");
            fail++;
        }

        // 设置 bitLength 后有效
        XCanUniqueIdDescription_setBitLength(&desc, 8);
        if (XCanUniqueIdDescription_isValid(&desc)) {
            XPrintf("  [通过] 有效描述 isValid 返回 true\n");
            pass++;
        } else {
            XPrintf("  [失败] 有效描述 isValid 返回 false\n");
            fail++;
        }
    }

    // ========== 4. NULL 指针安全性测试 ==========
    {
        if (XCanUniqueIdDescription_isValid(NULL) == false) {
            XPrintf("  [通过] isValid(NULL) 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] isValid(NULL) 未返回 false\n");
            fail++;
        }

        XCanUniqueIdDescription_init(NULL);
        XPrintf("  [通过] init(NULL) 不崩溃\n");
        pass++;
    }

    XPrintf("========== XCanUniqueIdDescription 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCanFrameProcessor 单元测试
 * @details 测试帧处理器的初始化、帧解析、帧编码等
 ******************************************************************************************/
static void XCanFrameProcessorTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanFrameProcessor 单元测试 ==========\n");

    // ========== 1. init/deinit 测试 ==========
    {
        XCanFrameProcessor processor;
        XCanFrameProcessor_init(&processor);
        if (processor.m_error == XCanFrameProcessor_Error_None) {
            XPrintf("  [通过] init 默认状态正确\n");
            pass++;
        } else {
            XPrintf("  [失败] init 默认状态错误\n");
            fail++;
        }
        XCanFrameProcessor_deinit(&processor);
    }

    // ========== 2. 设置唯一 ID 描述测试 ==========
    {
        XCanFrameProcessor processor;
        XCanFrameProcessor_init(&processor);

        XCanUniqueIdDescription uidDesc;
        XCanUniqueIdDescription_init(&uidDesc);
        XCanUniqueIdDescription_setSource(&uidDesc, XCanBus_FrameId);
        XCanUniqueIdDescription_setBitLength(&uidDesc, 29);
        XCanFrameProcessor_setUniqueIdDescription(&processor, &uidDesc);

        XCanUniqueIdDescription out;
        XCanUniqueIdDescription_init(&out);
        XCanFrameProcessor_uniqueIdDescription(&processor, &out);

        if (XCanUniqueIdDescription_source(&out) == XCanBus_FrameId &&
            XCanUniqueIdDescription_bitLength(&out) == 29) {
            XPrintf("  [通过] setUniqueIdDescription/uniqueIdDescription 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setUniqueIdDescription/uniqueIdDescription 错误\n");
            fail++;
        }

        XCanFrameProcessor_deinit(&processor);
    }

    // ========== 3. 消息描述管理测试 ==========
    {
        XCanFrameProcessor processor;
        XCanFrameProcessor_init(&processor);

        // 创建消息描述
        XCanMessageDescription msgDesc;
        XCanMessageDescription_init(&msgDesc);
        XCanMessageDescription_setUniqueId(&msgDesc, 0x123);
        XCanMessageDescription_setName(&msgDesc, "TestMessage");
        XCanMessageDescription_setSize(&msgDesc, 8);

        // 添加信号
        XCanSignalDescription sigDesc;
        XCanSignalDescription_init(&sigDesc);
        XCanSignalDescription_setName(&sigDesc, "TestSignal");
        XCanSignalDescription_setStartBit(&sigDesc, 0);
        XCanSignalDescription_setBitLength(&sigDesc, 16);
        XCanSignalDescription_setFactor(&sigDesc, 1.0);
        XCanMessageDescription_addSignalDescription(&msgDesc, &sigDesc);
        XCanSignalDescription_deinit(&sigDesc);

        // 添加到处理器
        XVector* descs = XVector_create(sizeof(XCanMessageDescription));
        XVector_push_back_1_base(descs, &msgDesc);
        XCanFrameProcessor_addMessageDescriptions(&processor, descs);
        XVector_delete_base(descs);

        // 查询消息描述
        XVector* result = XCanFrameProcessor_messageDescriptions(&processor);
        if (result && XVector_size_base(result) == 1) {
            XPrintf("  [通过] addMessageDescriptions 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] addMessageDescriptions 结果错误\n");
            fail++;
        }
        XVector_delete_base(result);

        // 清除消息描述
        XCanFrameProcessor_clearMessageDescriptions(&processor);
        XVector* empty = XCanFrameProcessor_messageDescriptions(&processor);
        if (empty && XVector_size_base(empty) == 0) {
            XPrintf("  [通过] clearMessageDescriptions 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] clearMessageDescriptions 后仍有消息\n");
            fail++;
        }
        XVector_delete_base(empty);

        XCanMessageDescription_deinit(&msgDesc);
        XCanFrameProcessor_deinit(&processor);
    }

    // ========== 4. parseFrame 测试 ==========
    {
        XCanFrameProcessor processor;
        XCanFrameProcessor_init(&processor);

        // 设置唯一 ID 描述（从帧 ID 提取）
        XCanUniqueIdDescription uidDesc;
        XCanUniqueIdDescription_init(&uidDesc);
        XCanUniqueIdDescription_setSource(&uidDesc, XCanBus_FrameId);
        XCanUniqueIdDescription_setBitLength(&uidDesc, 29);
        XCanFrameProcessor_setUniqueIdDescription(&processor, &uidDesc);

        // 创建消息描述
        XCanMessageDescription msgDesc;
        XCanMessageDescription_init(&msgDesc);
        XCanMessageDescription_setUniqueId(&msgDesc, 0x123);
        XCanMessageDescription_setName(&msgDesc, "TestMsg");
        XCanMessageDescription_setSize(&msgDesc, 8);

        // 添加信号
        XCanSignalDescription sigDesc;
        XCanSignalDescription_init(&sigDesc);
        XCanSignalDescription_setName(&sigDesc, "Speed");
        XCanSignalDescription_setStartBit(&sigDesc, 0);
        XCanSignalDescription_setBitLength(&sigDesc, 16);
        XCanSignalDescription_setFactor(&sigDesc, 1.0);
        XCanSignalDescription_setOffset(&sigDesc, 0.0);
        XCanMessageDescription_addSignalDescription(&msgDesc, &sigDesc);
        XCanSignalDescription_deinit(&sigDesc);

        // 添加到处理器
        XVector* descs = XVector_create(sizeof(XCanMessageDescription));
        XVector_push_back_1_base(descs, &msgDesc);
        XCanFrameProcessor_addMessageDescriptions(&processor, descs);
        XVector_delete_base(descs);

        // 创建 CAN 帧
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);
        XCanBusFrame_setFrameId(&frame, 0x123);
        uint8_t data[] = {0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        XCanBusFrame_setPayload(&frame, data, 8);

        // 解析帧
        XCanFrameProcessor_ParseResult result;
        XCanFrameProcessor_ParseResult_init(&result);
        bool parseOk = XCanFrameProcessor_parseFrame(&processor, &frame, &result);

        if (parseOk && result.m_uniqueId == 0x123) {
            XPrintf("  [通过] parseFrame 解析成功, uniqueId=0x%X\n", result.m_uniqueId);
            pass++;
        } else {
            XPrintf("  [失败] parseFrame 解析失败\n");
            fail++;
        }

        XCanFrameProcessor_ParseResult_deinit(&result);
        XCanBusFrame_deinit(&frame);
        XCanMessageDescription_deinit(&msgDesc);
        XCanFrameProcessor_deinit(&processor);
    }

    // ========== 5. prepareFrame 测试 ==========
    {
        XCanFrameProcessor processor;
        XCanFrameProcessor_init(&processor);

        // 创建消息描述
        XCanMessageDescription msgDesc;
        XCanMessageDescription_init(&msgDesc);
        XCanMessageDescription_setUniqueId(&msgDesc, 0x456);
        XCanMessageDescription_setName(&msgDesc, "OutputMsg");
        XCanMessageDescription_setSize(&msgDesc, 8);

        XCanSignalDescription sigDesc;
        XCanSignalDescription_init(&sigDesc);
        XCanSignalDescription_setName(&sigDesc, "Value");
        XCanSignalDescription_setStartBit(&sigDesc, 0);
        XCanSignalDescription_setBitLength(&sigDesc, 16);
        XCanSignalDescription_setFactor(&sigDesc, 1.0);
        XCanMessageDescription_addSignalDescription(&msgDesc, &sigDesc);
        XCanSignalDescription_deinit(&sigDesc);

        XVector* descs = XVector_create(sizeof(XCanMessageDescription));
        XVector_push_back_1_base(descs, &msgDesc);
        XCanFrameProcessor_addMessageDescriptions(&processor, descs);
        XVector_delete_base(descs);

        // 准备信号值映射
        XMap* signalValues = XMap_create(sizeof(XString), sizeof(XVariant), XString_compare);
        XMapBaseSetKeyCopyMethod(signalValues, XString_copy_base);
        XMapBaseSetKeyMoveMethod(signalValues, XString_move_base);
        XMapBaseSetKeyDeinitMethod(signalValues, XString_deinit_base);

        XString sigName;
        XString_init(&sigName);
        XString_assign_utf8(&sigName, "Value");

        XVariant var;
        XVariant_init(&var, NULL, 0, XVariantType_Double);
        XVariant_setValue_double(&var, 255.0);
        XMapBase_insert_base((XMapBase*)signalValues, &sigName, &var);
        XClass_deinit_base((XClass*)&var);
        XClass_deinit_base((XClass*)&sigName);

        // 编码帧
        XCanBusFrame* encoded = XCanFrameProcessor_prepareFrame(&processor, 0x456, signalValues);
        if (encoded && XCanBusFrame_isValid(encoded)) {
            XPrintf("  [通过] prepareFrame 编码成功\n");
            pass++;
        } else {
            XPrintf("  [失败] prepareFrame 编码失败\n");
            fail++;
        }
        XCanBusFrame_delete(encoded);
        XMap_delete_base(signalValues);
        XCanMessageDescription_deinit(&msgDesc);
        XCanFrameProcessor_deinit(&processor);
    }

    // ========== 6. 错误/警告查询测试 ==========
    {
        XCanFrameProcessor processor;
        XCanFrameProcessor_init(&processor);

        if (XCanFrameProcessor_error(&processor) == XCanFrameProcessor_Error_None) {
            XPrintf("  [通过] error 初始为 None\n");
            pass++;
        } else {
            XPrintf("  [失败] error 初始不为 None\n");
            fail++;
        }

        XString* errStr = XCanFrameProcessor_errorString(&processor);
        if (errStr) {
            XPrintf("  [通过] errorString 初始非 NULL\n");
            pass++;
            XString_delete_base(errStr);
        } else {
            XPrintf("  [失败] errorString 初始为 NULL\n");
            fail++;
        }

        XStringList* warnings = XCanFrameProcessor_warnings(&processor);
        if (warnings) {
            XPrintf("  [通过] warnings 初始非 NULL\n");
            pass++;
            XStringList_delete_base(warnings);
        } else {
            XPrintf("  [失败] warnings 初始为 NULL\n");
            fail++;
        }

        XCanFrameProcessor_deinit(&processor);
    }

    // ========== 7. NULL 指针安全性测试 ==========
    {
        XCanFrameProcessor_init(NULL);
        XCanFrameProcessor_deinit(NULL);
        XCanFrameProcessor_ParseResult_init(NULL);
        XCanFrameProcessor_ParseResult_deinit(NULL);

        if (XCanFrameProcessor_error(NULL) == XCanFrameProcessor_Error_None) {
            XPrintf("  [通过] error(NULL) 返回 None\n");
            pass++;
        } else {
            XPrintf("  [失败] error(NULL) 未返回 None\n");
            fail++;
        }

        XPrintf("  [通过] NULL 指针 init/deinit 不崩溃\n");
        pass++;
    }

    XPrintf("========== XCanFrameProcessor 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCanDbcFileParser 单元测试
 * @details 测试 DBC 文件解析器的初始化、数据解析、结果查询等
 ******************************************************************************************/
static void XCanDbcFileParserTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanDbcFileParser 单元测试 ==========\n");

    // ========== 1. init/deinit 测试 ==========
    {
        XCanDbcFileParser parser;
        XCanDbcFileParser_init(&parser);
        if (parser.m_error == XCanDbcFileParser_Error_None) {
            XPrintf("  [通过] init 默认状态正确\n");
            pass++;
        } else {
            XPrintf("  [失败] init 默认状态错误\n");
            fail++;
        }
        XCanDbcFileParser_deinit(&parser);
    }

    // ========== 2. parseData 测试（最小 DBC 数据） ==========
    {
        XCanDbcFileParser parser;
        XCanDbcFileParser_init(&parser);

        // 最小 DBC 数据：一个消息和一个信号
        const char* dbcData =
            "VERSION \"\"\n"
            "\n"
            "NS_ :\n"
            "\tBA_\n"
            "\tBA_DEF_\n"
            "\tBA_DEF_DEF_\n"
            "\tBA_DEF_DEF_REL_\n"
            "\tBA_DEF_REL_\n"
            "\tBA_DEF_SGTYPE_\n"
            "\tBA_SGTYPE_\n"
            "\tBO_TX_BU_\n"
            "\tBU_BO_REL_\n"
            "\tBU_EV_REL_\n"
            "\tBU_SG_REL_\n"
            "\tCAT_\n"
            "\tCAT_DEF_\n"
            "\tCM_\n"
            "\tENVVAR_DATA_\n"
            "\tEV_DATA_\n"
            "\tFILTER\n"
            "\tNS_DESC_\n"
            "\tSGTYPE_\n"
            "\tSGTYPE_VAL_\n"
            "\tSIGTYPE_VAL_\n"
            "\tSIG_VALTYPE_\n"
            "\tSIG_VALTYPE_\n"
            "\tVAL_\n"
            "\tVAL_TABLE_\n"
            "\n"
            "BS_:\n"
            "\n"
            "BU_: ECU\n"
            "\n"
            "BO_ 123 EngineData: 8 ECU\n"
            " SG_ Speed : 0|16@1+ (1,0) [0|8000] \"rpm\" ECU\n"
            " SG_ Temp : 16|8@1+ (1,0) [0|250] \"degC\" ECU\n"
            "\n";

        bool ok = XCanDbcFileParser_parseData(&parser, dbcData);
        if (ok) {
            XPrintf("  [通过] parseData 解析成功\n");
            pass++;
        } else {
            XString* errStr = XCanDbcFileParser_errorString(&parser);
            XPrintf("  [失败] parseData 解析失败: %s\n",
                    errStr ? XString_toUtf8(errStr) : "unknown");
            if (errStr) XString_delete_base(errStr);
            fail++;
        }

        // 获取消息描述列表
        XVector* messages = XCanDbcFileParser_messageDescriptions(&parser);
        if (messages) {
            XPrintf("  [通过] messageDescriptions 返回 %zu 条消息\n", XVector_size_base(messages));
            pass++;
        } else {
            XPrintf("  [失败] messageDescriptions 返回 NULL\n");
            fail++;
        }
        XVector_delete_base(messages);

        // 检查错误码
        if (XCanDbcFileParser_error(&parser) == XCanDbcFileParser_Error_None) {
            XPrintf("  [通过] error 为 None\n");
            pass++;
        } else {
            XPrintf("  [失败] error 不为 None\n");
            fail++;
        }

        // 检查警告
        XStringList* warnings = XCanDbcFileParser_warnings(&parser);
        if (warnings) {
            XPrintf("  [通过] warnings 非 NULL (%zu 条)\n", XStringList_size_base(warnings));
            pass++;
            XStringList_delete_base(warnings);
        } else {
            XPrintf("  [失败] warnings 为 NULL\n");
            fail++;
        }

        XCanDbcFileParser_deinit(&parser);
    }

    // ========== 3. parseData 无效数据测试 ==========
    {
        XCanDbcFileParser parser;
        XCanDbcFileParser_init(&parser);

        // 空数据
        bool ok = XCanDbcFileParser_parseData(&parser, "");
        if (!ok) {
            XPrintf("  [通过] 空数据 parseData 返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] 空数据 parseData 未返回 false\n");
            fail++;
        }

        XCanDbcFileParser_deinit(&parser);
    }

    // ========== 4. uniqueIdDescription 静态方法测试 ==========
    {
        XCanUniqueIdDescription uidDesc;
        XCanUniqueIdDescription_init(&uidDesc);
        XCanDbcFileParser_uniqueIdDescription(&uidDesc);

        if (XCanUniqueIdDescription_source(&uidDesc) == XCanBus_FrameId &&
            XCanUniqueIdDescription_bitLength(&uidDesc) == 29 &&
            XCanUniqueIdDescription_endian(&uidDesc) == 1) {
            XPrintf("  [通过] uniqueIdDescription 静态方法正确\n");
            pass++;
        } else {
            XPrintf("  [失败] uniqueIdDescription 静态方法错误\n");
            fail++;
        }
    }

    // ========== 5. NULL 指针安全性测试 ==========
    {
        XCanDbcFileParser_init(NULL);
        XCanDbcFileParser_deinit(NULL);

        if (XCanDbcFileParser_error(NULL) == XCanDbcFileParser_Error_None) {
            XPrintf("  [通过] error(NULL) 返回 None\n");
            pass++;
        } else {
            XPrintf("  [失败] error(NULL) 未返回 None\n");
            fail++;
        }

        XCanDbcFileParser_uniqueIdDescription(NULL);
        XPrintf("  [通过] NULL 指针 init/deinit/uniqueIdDescription 不崩溃\n");
        pass++;
    }

    XPrintf("========== XCanDbcFileParser 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCanBus 单元测试
 * @details 测试 CAN 总线单例工厂的实例获取、插件管理等
 ******************************************************************************************/
static void XCanBusTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanBus 单元测试 ==========\n");

    // ========== 1. 单例访问测试 ==========
    {
        XCanBus* canBus = XCanBus_instance();
        if (canBus != NULL) {
            XPrintf("  [通过] XCanBus_instance 返回非 NULL\n");
            pass++;
        } else {
            XPrintf("  [失败] XCanBus_instance 返回 NULL\n");
            fail++;
        }

        // 多次调用应返回同一实例
        XCanBus* canBus2 = XCanBus_instance();
        if (canBus == canBus2) {
            XPrintf("  [通过] 多次调用返回同一实例\n");
            pass++;
        } else {
            XPrintf("  [失败] 多次调用返回不同实例\n");
            fail++;
        }
    }

    // ========== 2. plugins 测试（初始为空） ==========
    {
        XCanBus* canBus = XCanBus_instance();
        XStringList* plugins = XCanBus_plugins(canBus);
        if (plugins != NULL) {
            XPrintf("  [通过] plugins 返回非 NULL (%zu 个插件)\n", XStringList_size_base(plugins));
            pass++;
            XStringList_delete_base(plugins);
        } else {
            XPrintf("  [失败] plugins 返回 NULL\n");
            fail++;
        }
    }

    // ========== 3. availableDevices 测试（无插件时） ==========
    {
        XCanBus* canBus = XCanBus_instance();
        char* errorMsg = NULL;
        XVector* devices = XCanBus_availableDevices(canBus, "nonexistent", &errorMsg);
        if (devices == NULL && errorMsg != NULL) {
            XPrintf("  [通过] availableDevices(不存在的插件) 返回 NULL 并设置错误\n");
            pass++;
            XFree_System(errorMsg);
        } else {
            XPrintf("  [失败] availableDevices(不存在的插件) 行为错误\n");
            if (devices) XVector_delete_base(devices);
            if (errorMsg) XFree_System(errorMsg);
            fail++;
        }
    }

    // ========== 4. createDevice 测试（无插件时） ==========
    {
        XCanBus* canBus = XCanBus_instance();
        char* errorMsg = NULL;
        XCanBusDevice* device = XCanBus_createDevice(canBus, "nonexistent", "can0", &errorMsg);
        if (device == NULL && errorMsg != NULL) {
            XPrintf("  [通过] createDevice(不存在的插件) 返回 NULL 并设置错误\n");
            pass++;
            XFree_System(errorMsg);
        } else {
            XPrintf("  [失败] createDevice(不存在的插件) 行为错误\n");
            if (device) XCanBusDevice_deleteLater(device);
            if (errorMsg) XFree_System(errorMsg);
            fail++;
        }
    }

    // ========== 5. NULL 参数安全性测试 ==========
    {
        if (XCanBus_plugins(NULL) != NULL) {
            XPrintf("  [通过] plugins(NULL) 返回非 NULL\n");
            pass++;
        } else {
            XPrintf("  [失败] plugins(NULL) 返回 NULL\n");
            fail++;
        }

        if (XCanBus_createDevice(NULL, "socketcan", "can0", NULL) == NULL) {
            XPrintf("  [通过] createDevice(NULL,...) 返回 NULL\n");
            pass++;
        } else {
            XPrintf("  [失败] createDevice(NULL,...) 未返回 NULL\n");
            fail++;
        }
    }

    XPrintf("========== XCanBus 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCanBusDevice 单元测试
 * @details 测试 CAN 总线设备的创建、状态管理、配置参数等
 ******************************************************************************************/
static void XCanBusDeviceTest(void)
{
    int pass = 0, fail = 0;
    XPrintf("========== XCanBusDevice 单元测试 ==========\n");

    // ========== 1. 创建与初始化测试 ==========
    {
        XCanBusDevice* dev = XCanBusDevice_create();
        if (dev != NULL) {
            XPrintf("  [通过] XCanBusDevice_create 成功\n");
            pass++;
        } else {
            XPrintf("  [失败] XCanBusDevice_create 失败\n");
            fail++;
        }

        // 检查初始状态
        if (XCanBusDevice_state(dev) == XCanBusDevice_UnconnectedState) {
            XPrintf("  [通过] 初始状态为 UnconnectedState\n");
            pass++;
        } else {
            XPrintf("  [失败] 初始状态 = %d, 期望 %d\n",
                    XCanBusDevice_state(dev), XCanBusDevice_UnconnectedState);
            fail++;
        }

        // 检查初始错误
        if (XCanBusDevice_error(dev) == XCanBusDevice_NoError) {
            XPrintf("  [通过] 初始错误为 NoError\n");
            pass++;
        } else {
            XPrintf("  [失败] 初始错误 = %d, 期望 %d\n",
                    XCanBusDevice_error(dev), XCanBusDevice_NoError);
            fail++;
        }

        XCanBusDevice_deleteLater(dev);
    }

    // ========== 2. 状态设置测试 ==========
    {
        XCanBusDevice* dev = XCanBusDevice_create();

        XCanBusDevice_setState(dev, XCanBusDevice_ConnectedState);
        if (XCanBusDevice_state(dev) == XCanBusDevice_ConnectedState) {
            XPrintf("  [通过] setState(ConnectedState) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setState(ConnectedState) 后 state = %d\n",
                    XCanBusDevice_state(dev));
            fail++;
        }

        XCanBusDevice_setState(dev, XCanBusDevice_UnconnectedState);
        if (XCanBusDevice_state(dev) == XCanBusDevice_UnconnectedState) {
            XPrintf("  [通过] setState(UnconnectedState) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setState(UnconnectedState) 后 state = %d\n",
                    XCanBusDevice_state(dev));
            fail++;
        }

        XCanBusDevice_deleteLater(dev);
    }

    // ========== 3. 错误设置测试 ==========
    {
        XCanBusDevice* dev = XCanBusDevice_create();

        XCanBusDevice_setError(dev, XCanBusDevice_ConnectionError, "Connection refused");
        if (XCanBusDevice_error(dev) == XCanBusDevice_ConnectionError) {
            XPrintf("  [通过] setError(ConnectionError) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setError 后 error = %d\n", XCanBusDevice_error(dev));
            fail++;
        }

        XString* errStr = XCanBusDevice_errorString(dev);
        if (errStr && strstr(XString_toUtf8(errStr), "Connection refused") != NULL) {
            XPrintf("  [通过] errorString 包含 \"Connection refused\"\n");
            pass++;
            XString_delete_base(errStr);
        } else {
            XPrintf("  [失败] errorString 不正确\n");
            if (errStr) XString_delete_base(errStr);
            fail++;
        }

        // 清除错误
        XCanBusDevice_clearError(dev);
        if (XCanBusDevice_error(dev) == XCanBusDevice_NoError) {
            XPrintf("  [通过] clearError 后 error 为 NoError\n");
            pass++;
        } else {
            XPrintf("  [失败] clearError 后 error = %d\n", XCanBusDevice_error(dev));
            fail++;
        }

        XCanBusDevice_deleteLater(dev);
    }

    // ========== 4. 配置参数测试 ==========
    {
        XCanBusDevice* dev = XCanBusDevice_create();

        // 设置配置参数
        XVariant* rawFilter = XVariant_create_utf8_str("123");
        XCanBusDevice_setConfigurationParameter(dev, XCanBusDevice_RawFilterKey, rawFilter);
        XVariant_delete_base(rawFilter);

        XVariant* val = XCanBusDevice_configurationParameter(dev, XCanBusDevice_RawFilterKey);
        if (val != NULL) {
            XPrintf("  [通过] setConfigurationParameter/getConfigurationParameter 正确\n");
            pass++;
            XVariant_delete_base(val);
        } else {
            XPrintf("  [失败] getConfigurationParameter 返回 NULL\n");
            fail++;
        }

        // 不存在的键
        XVariant* noVal = XCanBusDevice_configurationParameter(dev, (XCanBusDevice_ConfigurationKey)999);
        if (noVal == NULL) {
            XPrintf("  [通过] getConfigurationParameter(不存在的键) 返回 NULL\n");
            pass++;
        } else {
            XPrintf("  [失败] getConfigurationParameter(不存在的键) 未返回 NULL\n");
            XVariant_delete_base(noVal);
            fail++;
        }

        XCanBusDevice_deleteLater(dev);
    }

    // ========== 5. 帧队列测试 ==========
    {
        XCanBusDevice* dev = XCanBusDevice_create();

        // 初始队列为空
        if (XCanBusDevice_framesAvailable(dev) == 0) {
            XPrintf("  [通过] 初始 framesAvailable 为 0\n");
            pass++;
        } else {
            XPrintf("  [失败] 初始 framesAvailable = %lld\n",
                    (long long)XCanBusDevice_framesAvailable(dev));
            fail++;
        }

        if (XCanBusDevice_framesToWrite(dev) == 0) {
            XPrintf("  [通过] 初始 framesToWrite 为 0\n");
            pass++;
        } else {
            XPrintf("  [失败] 初始 framesToWrite = %lld\n",
                    (long long)XCanBusDevice_framesToWrite(dev));
            fail++;
        }

        // 入队接收帧
        XCanBusFrame frame;
        XCanBusFrame_init(&frame, XCanBusFrame_DataFrame);
        XCanBusFrame_setFrameId(&frame, 0x123);

        XVector* frames = XVector_create(sizeof(XCanBusFrame*));
        XCanBusFrame* framePtr = &frame;
        XVector_push_back_1_base(frames, &framePtr);
        XCanBusDevice_enqueueReceivedFrames(dev, frames);
        XVector_delete_base(frames);

        if (XCanBusDevice_framesAvailable(dev) > 0) {
            XPrintf("  [通过] enqueueReceivedFrames 后 framesAvailable > 0\n");
            pass++;
        } else {
            XPrintf("  [失败] enqueueReceivedFrames 后 framesAvailable = 0\n");
            fail++;
        }

        XCanBusDevice_setState(dev, XCanBusDevice_ConnectedState);
        // 读取帧
        XCanBusFrame* readFrame = XCanBusDevice_readFrame(dev);
        if (readFrame != NULL) {
            XPrintf("  [通过] readFrame 返回非 NULL\n");
            pass++;
            XCanBusFrame_delete(readFrame);
        } else {
            XPrintf("  [失败] readFrame 返回 NULL\n");
            fail++;
        }

        // 入队发送帧
        XCanBusDevice_enqueueOutgoingFrame(dev, &frame);
        if (XCanBusDevice_hasOutgoingFrames(dev)) {
            XPrintf("  [通过] enqueueOutgoingFrame 后 hasOutgoingFrames 为 true\n");
            pass++;
        } else {
            XPrintf("  [失败] enqueueOutgoingFrame 后 hasOutgoingFrames 为 false\n");
            fail++;
        }

        // 出队发送帧
        XCanBusFrame* outFrame = XCanBusDevice_dequeueOutgoingFrame(dev);
        if (outFrame != NULL) {
            XPrintf("  [通过] dequeueOutgoingFrame 返回非 NULL\n");
            pass++;
            XCanBusFrame_delete(outFrame);
        } else {
            XPrintf("  [失败] dequeueOutgoingFrame 返回 NULL\n");
            fail++;
        }

        XCanBusFrame_deinit(&frame);
        XCanBusDevice_deleteLater(dev);
    }

    // ========== 6. connectDevice/disconnectDevice 测试（抽象类） ==========
    {
        XCanBusDevice* dev = XCanBusDevice_create();

        // connectDevice 应失败（未实现 open）
        bool connected = XCanBusDevice_connectDevice(dev);
        if (!connected) {
            XPrintf("  [通过] connectDevice（未实现 open）返回 false\n");
            pass++;
        } else {
            XPrintf("  [失败] connectDevice（未实现 open）返回 true\n");
            fail++;
        }

        // disconnectDevice 不应崩溃
        XCanBusDevice_disconnectDevice(dev);
        XPrintf("  [通过] disconnectDevice 不崩溃\n");
        pass++;

        XCanBusDevice_deleteLater(dev);
    }

    // ========== 7. 清除帧队列测试 ==========
    {
        XCanBusDevice* dev = XCanBusDevice_create();

        XCanBusDevice_setState(dev, XCanBusDevice_ConnectedState);
        XCanBusDevice_clear(dev, XCanBusDevice_AllDirections);
        if (XCanBusDevice_framesAvailable(dev) == 0 &&
            XCanBusDevice_framesToWrite(dev) == 0) {
            XPrintf("  [通过] clear(AllDirections) 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] clear(AllDirections) 后队列不为空\n");
            fail++;
        }

        XCanBusDevice_deleteLater(dev);
    }

    // ========== 8. NULL 指针安全性测试 ==========
    {
        if (XCanBusDevice_state(NULL) == XCanBusDevice_UnconnectedState) {
            XPrintf("  [通过] state(NULL) 返回 UnconnectedState\n");
            pass++;
        } else {
            XPrintf("  [失败] state(NULL) 未返回 UnconnectedState\n");
            fail++;
        }

        if (XCanBusDevice_error(NULL) == XCanBusDevice_UnknownError) {
            XPrintf("  [通过] error(NULL) 返回 UnknownError\n");
            pass++;
        } else {
            XPrintf("  [失败] error(NULL) 未返回 UnknownError\n");
            fail++;
        }

        XCanBusDevice_init(NULL);
        XCanBusDevice_setState(NULL, XCanBusDevice_ConnectedState);
        XCanBusDevice_setError(NULL, XCanBusDevice_NoError, NULL);
        XCanBusDevice_clearError(NULL);
        XCanBusDevice_enqueueReceivedFrames(NULL, NULL);
        XCanBusDevice_enqueueOutgoingFrame(NULL, NULL);
        XCanBusDevice_dequeueOutgoingFrame(NULL);
        XCanBusDevice_hasOutgoingFrames(NULL);
        XCanBusDevice_connectDevice(NULL);
        XCanBusDevice_disconnectDevice(NULL);
        XCanBusDevice_clear(NULL, XCanBusDevice_AllDirections);
        XPrintf("  [通过] NULL 指针所有 API 不崩溃\n");
        pass++;
    }

    XPrintf("========== XCanBusDevice 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/******************************************************************************************
 * @brief XCan 协议栈综合测试入口
 * @details 注册所有 XCan 测试到菜单
 ******************************************************************************************/
void XMenu_XCanTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XCan(can)");
    {
        XAction* action = XMenu_addAction(menu, "CanBusFrame单元测试");
        XAction_setAction(action, XCanBusFrameTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "CanBusDeviceInfo单元测试");
        XAction_setAction(action, XCanBusDeviceInfoTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "CanSignalDescription单元测试");
        XAction_setAction(action, XCanSignalDescriptionTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "CanMessageDescription单元测试");
        XAction_setAction(action, XCanMessageDescriptionTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "CanUniqueIdDescription单元测试");
        XAction_setAction(action, XCanUniqueIdDescriptionTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "CanFrameProcessor单元测试");
        XAction_setAction(action, XCanFrameProcessorTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "CanDbcFileParser单元测试");
        XAction_setAction(action, XCanDbcFileParserTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "CanBus单例单元测试");
        XAction_setAction(action, XCanBusTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "CanBusDevice单元测试");
        XAction_setAction(action, XCanBusDeviceTest);
    }
    XMenu_addMenu(root, menu);
}
