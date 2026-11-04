
/** @file       xhsf_codec_test.c
 * @brief       HSF 网络编解码专项单测：变频器/电磁阀 状态与控制 ↔ JSON/BSON。
 * @details     覆盖 XVfdCodec / XSolenoidValveCodec 的核心契约：
 *                1) 状态 BSON 往返（键列表裁剪 + 掩码跳过无效字段）；
 *                2) 状态 JSON 往返（键名与 BSON 同表）；
 *                3) 控制请求 BSON/JSON 往返 + applyControl 受理口径；
 *                4) 电磁阀快照/控制编解码。
 *              退出码 0=全部通过；非 0=存在失败用例。
 */
#include "CXinYueConfig.h"

#if XINDUSTRY_ON

#include "XVfdCodec.h"
#include "XSolenoidValveCodec.h"
#include "XVfdM512.h"
#include "XCoreApplication.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <stdio.h>
#include <string.h>

static int g_case = 0;
static int g_fail = 0;

/** @brief 断言：失败打印用例号与说明并计失败（不中断，跑完全部用例）。 */
#define CHECK(cond, msg) \
    do { \
        ++g_case; \
        if (!(cond)) { \
            ++g_fail; \
            XPrintf("  [FAIL] #%d %s\n", g_case, msg); \
        } \
    } while (0)

/**
 * @brief 填一个已知遥测快照（只置部分字段位，模拟品牌未提供的字段）。
 */
static void codecFillStatus(XVfdStatus* st)
{
    memset(st, 0, sizeof(*st));
    st->m_valid = true;
    st->m_validMask = (uint64_t)XVfdField_RunState |
                      (uint64_t)XVfdField_Running |
                      (uint64_t)XVfdField_Faulted |
                      (uint64_t)XVfdField_SetFreqHz |
                      (uint64_t)XVfdField_OutputFreqHz |
                      (uint64_t)XVfdField_OutputCurrent |
                      (uint64_t)XVfdField_BusVoltage |
                      (uint64_t)XVfdField_TorquePercent |
                      (uint64_t)XVfdField_FaultCode |
                      (uint64_t)XVfdField_Remote;
    st->m_timestampMs = 123456789ULL;
    st->m_runState = XVfdRunState_RunningForward;
    st->m_running = true;
    st->m_faulted = false;
    st->m_remote = true;
    st->m_setFreqHz = 25.5;
    st->m_outputFreqHz = 24.9;
    st->m_outputCurrentA = 3.25;
    st->m_busVoltageV = 558.6;
    st->m_torquePercent = 42.0;
    st->m_faultCode = 0x0000;
}

/**
 * @brief 用例 1：状态 BSON 往返（遥测键列表）。
 */
static void codecTestStatusBson(void)
{
    XVfdStatus st;
    XVfdStatus back;
    int keyCount = 0;
    const uint64_t* keys = XVfdCodec_telemetryKeys(&keyCount);
    XByteArray* bytes;

    codecFillStatus(&st);
    CHECK(keys != NULL && keyCount > 0, "遥测键列表非空");

    bytes = XVfdCodec_statusToBson(&st, keys, keyCount);
    CHECK(bytes != NULL, "statusToBson 产出载荷");
    CHECK(bytes && XByteArray_size_base(bytes) > 4, "BSON 载荷非空");

    CHECK(XVfdCodec_statusFromBson(&back,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   XByteArray_size_base(bytes)),
          "statusFromBson 解析成功");
    CHECK(back.m_valid && back.m_validMask == st.m_validMask, "有效掩码往返一致");
    CHECK(back.m_runState == st.m_runState, "运行状态往返一致");
    CHECK(back.m_running == true && back.m_faulted == false, "布尔字段往返一致");
    CHECK(back.m_setFreqHz == st.m_setFreqHz, "设定频率往返一致");
    CHECK(back.m_outputFreqHz == st.m_outputFreqHz, "输出频率往返一致");
    CHECK(back.m_outputCurrentA == st.m_outputCurrentA, "输出电流往返一致");
    CHECK(back.m_faultCode == st.m_faultCode, "故障码往返一致");
    CHECK(back.m_timestampMs == st.m_timestampMs, "时间戳往返一致");
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 用例 2：键列表裁剪（未列入键不编码、解码掩码不含其位）。
 */
static void codecTestKeySubset(void)
{
    XVfdStatus st;
    XVfdStatus back;
    const uint64_t keys[2] = { (uint64_t)XVfdField_OutputFreqHz,
                               (uint64_t)XVfdField_Faulted };
    XByteArray* bytes;

    codecFillStatus(&st);
    bytes = XVfdCodec_statusToBson(&st, keys, 2);
    CHECK(bytes != NULL, "裁剪编码成功");
    memset(&back, 0, sizeof(back));
    CHECK(XVfdCodec_statusFromBson(&back,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   XByteArray_size_base(bytes)),
          "裁剪解析成功");
    CHECK(back.m_validMask == ((uint64_t)XVfdField_OutputFreqHz |
                               (uint64_t)XVfdField_Faulted),
          "掩码只含键列表位");
    CHECK(back.m_outputFreqHz == st.m_outputFreqHz, "在列字段值正确");
    CHECK(back.m_outputCurrentA == 0.0, "不在列字段保持零值");
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 用例 3：无效字段跳过（掩码外的键即使列入也不编码）。
 */
static void codecTestMaskSkip(void)
{
    XVfdStatus st;
    XVfdStatus back;
    const uint64_t keys[3] = { (uint64_t)XVfdField_OutputFreqHz,
                               (uint64_t)XVfdField_SpeedRpm,
                               (uint64_t)XVfdField_Faulted };
    XByteArray* bytes;

    codecFillStatus(&st);          /* SpeedRpm 不在掩码内。 */
    bytes = XVfdCodec_statusToBson(&st, keys, 3);
    CHECK(bytes != NULL, "编码成功");
    memset(&back, 0, sizeof(back));
    CHECK(XVfdCodec_statusFromBson(&back,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   XByteArray_size_base(bytes)),
          "解析成功");
    CHECK(back.m_validMask == ((uint64_t)XVfdField_OutputFreqHz |
                               (uint64_t)XVfdField_Faulted),
          "无效字段未编码（掩码不回位）");
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 用例 4：状态 JSON 往返（键名与 BSON 同表）。
 */
static void codecTestStatusJson(void)
{
    XVfdStatus st;
    XVfdStatus back;
    int keyCount = 0;
    const uint64_t* keys = XVfdCodec_telemetryKeys(&keyCount);
    XByteArray* bytes;

    codecFillStatus(&st);
    bytes = XVfdCodec_statusToJson(&st, keys, keyCount);
    CHECK(bytes != NULL, "statusToJson 产出载荷");
    CHECK(bytes && XByteArray_size_base(bytes) > 2 &&
          ((const char*)XByteArray_constData(bytes))[0] == '{',
          "JSON 载荷以对象开头");

    CHECK(XVfdCodec_statusFromJson(&back,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   XByteArray_size_base(bytes)),
          "statusFromJson 解析成功");
    CHECK(back.m_validMask == st.m_validMask, "JSON 掩码往返一致");
    CHECK(back.m_outputFreqHz == st.m_outputFreqHz, "JSON 输出频率一致");
    CHECK(back.m_running == true, "JSON 布尔一致");
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 用例 5：控制请求 BSON/JSON 往返。
 */
static void codecTestControl(void)
{
    XVfdControlRequest req;
    XVfdControlRequest back;
    XByteArray* bytes;

    /* 频率给定。 */
    memset(&req, 0, sizeof(req));
    req.m_op = XVfdOperation_SetFrequency;
    req.m_valueHz = 12.5;
    bytes = XVfdCodec_controlToBson(&req);
    CHECK(bytes != NULL, "controlToBson 产出载荷");
    CHECK(XVfdCodec_controlFromBson(&back,
                                    (const uint8_t*)XByteArray_constData(bytes),
                                    XByteArray_size_base(bytes)),
          "controlFromBson 解析成功");
    CHECK(back.m_op == XVfdOperation_SetFrequency, "动作往返一致");
    CHECK(back.m_valueHz == 12.5, "频率载荷往返一致");
    XClassDelete((XClass*)bytes);

    /* 正转启动（方向位）。 */
    memset(&req, 0, sizeof(req));
    req.m_op = XVfdOperation_Start;
    req.m_reverse = true;
    bytes = XVfdCodec_controlToBson(&req);
    CHECK(bytes != NULL, "启动编码成功");
    memset(&back, 0, sizeof(back));
    CHECK(XVfdCodec_controlFromBson(&back,
                                    (const uint8_t*)XByteArray_constData(bytes),
                                    XByteArray_size_base(bytes)),
          "启动解析成功");
    CHECK(back.m_op == XVfdOperation_Start && back.m_reverse == true,
          "启动+方向往返一致");
    XClassDelete((XClass*)bytes);

    /* JSON 同表。 */
    memset(&req, 0, sizeof(req));
    req.m_op = XVfdOperation_Jog;
    req.m_reverse = false;
    bytes = XVfdCodec_controlToJson(&req);
    CHECK(bytes != NULL, "controlToJson 产出载荷");
    memset(&back, 0, sizeof(back));
    CHECK(XVfdCodec_controlFromJson(&back,
                                    (const uint8_t*)XByteArray_constData(bytes),
                                    XByteArray_size_base(bytes)),
          "controlFromJson 解析成功");
    CHECK(back.m_op == XVfdOperation_Jog, "JSON 动作往返一致");
    XClassDelete((XClass*)bytes);

    /* 非法载荷拒绝。 */
    CHECK(!XVfdCodec_controlFromBson(&back, NULL, 0), "空载荷拒绝");
    {
        const uint8_t junk[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        CHECK(!XVfdCodec_controlFromBson(&back, junk, sizeof(junk)),
              "垃圾字节拒绝");
    }
    {
        const char* badJson = "{\"op\":999}";
        CHECK(!XVfdCodec_controlFromJson(&back,
                                         (const uint8_t*)badJson,
                                         strlen(badJson)),
              "未知动作码拒绝");
    }
}

/**
 * @brief 用例 6：applyControl 受理口径（未挂通道组的设备一律拒绝）。
 */
static void codecTestApplyControl(void)
{
    XVfdM512* dev = XVfdM512_create();
    XVfdControlRequest req;
    CHECK(dev != NULL, "M512 设备创建");
    if (!dev) return;

    memset(&req, 0, sizeof(req));
    req.m_op = XVfdOperation_Stop;
    CHECK(!XVfdCodec_applyControl((XVfdDevice*)dev, &req),
          "未挂组设备拒绝动作");
    CHECK(!XVfdCodec_applyControl(NULL, &req), "空设备拒绝");
    CHECK(!XVfdCodec_applyControl((XVfdDevice*)dev, NULL), "空请求拒绝");
    XClassDelete((XClass*)dev);
}

/**
 * @brief 用例 7：电磁阀快照/控制编解码。
 */
static void codecTestValve(void)
{
    XSolenoidValve* valve = XSolenoidValve_create();
    XSolenoidValveSnapshot snap;
    int keyCount = 0;
    const XSolenoidValveField* keys = XSolenoidValveCodec_allKeys(&keyCount);
    XByteArray* bytes;
    bool open = false;

    CHECK(valve != NULL, "电磁阀创建");
    if (!valve) return;
    CHECK(keys != NULL && keyCount == 5, "全键列表 5 项");

    bytes = XSolenoidValveCodec_stateToBson(valve, keys, keyCount);
    CHECK(bytes != NULL, "stateToBson 产出载荷");
    CHECK(XSolenoidValveCodec_stateFromBson(
              &snap, (const uint8_t*)XByteArray_constData(bytes),
              XByteArray_size_base(bytes)),
          "stateFromBson 解析成功");
    CHECK(snap.m_state == (uint8_t)XSolenoidValveState_Unknown,
          "未挂通道时状态 Unknown");
    CHECK(!snap.m_open && !snap.m_closed, "到位标志均为假");
    XClassDelete((XClass*)bytes);

    /* JSON。 */
    bytes = XSolenoidValveCodec_stateToJson(valve, keys, keyCount);
    CHECK(bytes != NULL, "stateToJson 产出载荷");
    CHECK(XSolenoidValveCodec_stateFromJson(
              &snap, (const uint8_t*)XByteArray_constData(bytes),
              XByteArray_size_base(bytes)),
          "stateFromJson 解析成功");
    CHECK(snap.m_state == (uint8_t)XSolenoidValveState_Unknown,
          "JSON 状态一致");
    XClassDelete((XClass*)bytes);

    /* 控制方向解析（未挂出口 → 解析成功但受理拒绝）。 */
    bytes = XSolenoidValveCodec_controlToBson(true);
    CHECK(bytes != NULL, "controlToBson 产出载荷");
    CHECK(!XSolenoidValveCodec_applyControl(valve,
                                            (const uint8_t*)XByteArray_constData(bytes),
                                            XByteArray_size_base(bytes),
                                            &open),
          "未挂出口受理拒绝");
    CHECK(open == true, "方向解析为开");
    XClassDelete((XClass*)bytes);

    XClassDelete((XClass*)valve);
}

/**
 * @brief 跑一轮全部编解码用例（泄漏压测的循环体）。
 */
static void codecRunAll(void)
{
    codecTestStatusBson();
    codecTestKeySubset();
    codecTestMaskSkip();
    codecTestStatusJson();
    codecTestControl();
    codecTestApplyControl();
    codecTestValve();
}

/**
 * @brief 泄漏压测：全部用例循环 iterations 轮，堆在用字节数必须回基线。
 * @note  统计被构建裁剪时跳过（记 SKIP）。基线在压测前取样，压测后比對——
 *        应用级对象（app）在基线前已建，稳定。
 */
static void codecTestLeakLoop(int iterations)
{
    XMemoryStatistics base;
    XMemoryStatistics now;
    int i;
    if (!XMemory_statisticsEnabled()) {
        ++g_case;
        XPrintf("  [SKIP] 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    base = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < iterations; ++i)
        codecRunAll();
    now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    ++g_case;
    if (now.systemBytes != base.systemBytes) {
        ++g_fail;
        XPrintf("  [FAIL] #%d 泄漏：%d 轮后堆在用 %llu -> %llu 字节（差 %lld）\n",
                g_case, iterations,
                (unsigned long long)base.systemBytes,
                (unsigned long long)now.systemBytes,
                (long long)now.systemBytes - (long long)base.systemBytes);
    }
}

int main(int argc, char** argv)
{
    XCoreApplication* app;
    (void)argc;
    (void)argv;

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("xhsf-codec-test: 事件循环创建失败\n");
        return 2;
    }

    XPrintf("xhsf-codec-test: 开始\n");
    codecRunAll();
    XPrintf("xhsf-codec-test: %d 用例，失败 %d\n", g_case, g_fail);

    /* 泄漏压测：全部路径再走 500 轮，统计须回零。 */
    XPrintf("xhsf-codec-test: 泄漏压测 500 轮…\n");
    codecTestLeakLoop(500);
    XPrintf("xhsf-codec-test: 共 %d 用例，失败 %d\n", g_case, g_fail);

    XClassDelete((XClass*)app);
    return g_fail ? 1 : 0;
}

#endif /* XINDUSTRY_ON */
