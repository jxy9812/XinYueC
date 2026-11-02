/**
 * @file       XVfdAtv61_Modbus.c
 * @brief      施耐德 ATV61 的 **Modbus 绑定表**（地址知识）。
 * @details    [2026-11-02 分层收口] 与 XVfdM512_Modbus.c 同款：品牌主文件
 *             XVfdAtv61.c 只留语义（role/type/div/caps/signed/tag），地址全部
 *             集中在本文件。换总线只需新增 XVfdAtv61_<Bus>.c。
 *
 *             ATV61 的地址语义（Drivecom / CiA402 口径）：
 *               8501 CMD   命令字（写）
 *               8502 LFRD  频率给定（写，0.1Hz）
 *               3201 ETA   状态字（读）
 *               3202 RFR   输出频率（读，0.1Hz）
 *               3204 LCR   电机电流（读，0.1A）
 *               3205 OTR   电机转矩（读，0.1%）
 *               7121 LFT   最近故障代码（读）
 *               ...
 */
#include "XVfdAtv61.h"
#include "XVfdBind.h"

#if XVFD_ON && XVFD_ATV61_ON

#include "XIoPoint.h"

/* ==================== 点绑定表（role → 寄存器） ==================== */

static const XVfdPointBinding s_atv61PointBinds[] = {
    /* role                     addr                   bitOff bitW */
    { XVfdRole_StatusWord,     XVfdAtv61_AddrEta,     0, 0 },
    { XVfdRole_AuxStateWord,   XVfdAtv61_AddrEti,     0, 0 },
    { XVfdRole_FaultCode,      XVfdAtv61_AddrLft,     0, 0 },
    { XVfdRole_OutputFreq,     XVfdAtv61_AddrRfr,     0, 0 },
    { XVfdRole_SetFreq,        XVfdAtv61_AddrFrh,     0, 0 },
    { XVfdRole_OutputCurrent,  XVfdAtv61_AddrLcr,     0, 0 },
    { XVfdRole_OutputVoltage,  XVfdAtv61_AddrUop,     0, 0 },
    { XVfdRole_MainsVoltage,   XVfdAtv61_AddrUln,     0, 0 },
    { XVfdRole_BusVoltage,     XVfdAtv61_AddrVbus,    0, 0 },
    { XVfdRole_Torque,         XVfdAtv61_AddrOtr,     0, 0 },
    { XVfdRole_SpeedRpm,       XVfdAtv61_AddrRfrd,    0, 0 },
    { XVfdRole_DriveThermal,   XVfdAtv61_AddrThd,     0, 0 },
    { XVfdRole_MotorThermal,   XVfdAtv61_AddrThr,     0, 0 },
    { XVfdRole_CommandWord,    XVfdAtv61_AddrCmd,     0, 0 },
    { XVfdRole_FreqSet,        XVfdAtv61_AddrLfr1,    0, 0 },
    { XVfdRole_SpeedSet,       XVfdAtv61_AddrLfrd,    0, 0 },
    { XVfdRole_AccelTime,      XVfdAtv61_AddrAcc,     0, 0 },
    { XVfdRole_DecelTime,      XVfdAtv61_AddrDec,     0, 0 },
};

#define ATV61_POINT_BIND_COUNT ((int)(sizeof(s_atv61PointBinds) / sizeof(s_atv61PointBinds[0])))

/* ==================== 导出 ==================== */

/**
 * @brief 取 ATV61 的 Modbus 绑定表。
 * @return 静态常量绑定表指针；不可释放。
 * @note ATV61 无特殊从站约束（m_protocol=NULL）、无通信组划分
 *       （m_groups=NULL，装配层按地址连续性自行切段）。
 */
const XVfdBindTable* XVfdAtv61_modbusBinding(void)
{
    static XVfdBindTable table;
    static bool inited = false;
    if (!inited) {
        table.m_points = s_atv61PointBinds;
        table.m_pointCount = ATV61_POINT_BIND_COUNT;
        table.m_params = NULL;
        table.m_paramCount = 0;
        table.m_groups = NULL;
        table.m_groupCount = 0;
        table.m_protocol = NULL;
        inited = true;
    }
    return &table;
}

#endif /* XVFD_ON && XVFD_ATV61_ON */
