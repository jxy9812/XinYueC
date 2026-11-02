/**
 * @file       xvfd_slave_emu.h
 * @brief      变频器从站模拟器（进程内 TCP 版，仅 Test 域使用）。
 * @details    [2026-11-01 通道化重构] 原端到端测试用 Python 从站 + pty 虚拟串口
 *             （本沙箱无 /dev/ptmx 无法运行），改为**同进程 TCP 环回**从站：
 *               XModbusTcpServer（自动端口，寄存器映射）+ 本模拟器按品牌语义
 *               在测试泵循环里 tick（读命令字/给定 → 推进状态机 → 回写状态/
 *               遥测），客户端走 XModbusTcpClient → Hub → 点 → 通道组 → 设备。
 *             语义复刻自 mbtest_atv61_slave.py / mbtest_m512_slave.py：
 *               - ATV61：CiA402 状态机（0x0006/0x0007/0x000F 推进、bit7 复位）、
 *                 LFT 故障注入、RFR 跟随 LFR1；
 *               - M512：2000H 命令字（1/2/3/4/5/6/7）、3000H 状态、8000H 故障、
 *                 1001H 遥测跟随 1000H 设定值（P0.10 百分比换算）。
 *             双站隔离：每站一对（server+client），Hub 按映射段分流。
 */
#ifndef XVFD_SLAVE_EMU_H
#define XVFD_SLAVE_EMU_H

#include "CXinYueConfig.h"
#include "XModbus_config.h"

#if XMODBUS_ON && XMODBUS_CLIENT_ON

#include <string.h>
#include "XModbusTcpServer.h"
#include "XModbusTcpClient.h"
#include "XModbusServer.h"
#include "XModbusDataUnit.h"
#include "XModbusDevice.h"
#include "XTcpServer.h"
#include "XVariant.h"

/** @brief 品牌。 */
#define XVFD_EMU_ATV61 0
#define XVFD_EMU_M512 1

/**
 * @brief 从站模拟器。
 */
typedef struct XvfdSlaveEmu {
    XModbusTcpServer* m_server;     /**< TCP 从站（映射宿主） */
    XModbusDataUnitMap* m_map;      /**< 暂存的映射表（setMap_move 后归 server） */
    int m_brand;                    /**< XVFD_EMU_ATV61 / XVFD_EMU_M512 */
    /* ---- ATV61 ---- */
    int m_state;                    /**< CiA402 状态（2/3/4/5） */
    int m_fault;                    /**< LFT 故障码 */
    int m_reverse;                  /**< 反转（CMD bit11） */
    int m_refHz10;                  /**< 频率给定（0.1Hz 定点） */
    int m_refRpm;                   /**< 转速给定 */
    int m_driveThermal;             /**< 变频器热状态 % */
    int m_motorThermal;             /**< 电机热状态 % */
    /* ---- M512 ---- */
    int m_m512Fault;                /**< 8000H 故障值 */
    int m_m512State;                /**< 3000H 状态（1/2/3，缺省 3） */
    int m_setpoint;                 /**< 1000H 通讯设定值（±10000 = ±100%） */
    int m_maxFreq;                  /**< P0.10 原始码（0.01Hz，缺省 5000） */
    uint16_t m_lastCmd;             /**< 命令字边沿检测缓存 */
} XvfdSlaveEmu;

/**
 * @brief 在服务器映射上登记一段保持寄存器。
 * @param server TCP 从站。
 * @param start 起始地址。
 * @param count 点数。
 * @return 成功返回 true。
 */
static bool xvfdEmuAddRange(XvfdSlaveEmu* em, uint16_t start, uint16_t count)
{
    static XModbusRegisterType key = XModbusHoldingRegisters;
    XModbusDataUnit* unit;
    if (!em) return false;
    if (!em->m_map) {
        em->m_map = XModbusDataUnitMap_create();
        if (!em->m_map) return false;
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, key, start, count);
    if (!unit) return false;
    XMap_insert_base((XMapBase*)em->m_map, &key, unit);
    XClassDelete((XClass*)unit);
    return true;
}

/**
 * @brief 写服务器映射一个保持寄存器。
 * @param em 模拟器。
 * @param addr 地址。
 * @param v 值。
 * @return 无。
 */
static void xvfdEmuPut(XvfdSlaveEmu* em, uint16_t addr, uint16_t v)
{
    XModbusServer_setData2((XModbusServer*)em->m_server, XModbusHoldingRegisters,
                           addr, v);
}

/**
 * @brief 读服务器映射一个保持寄存器。
 * @param em 模拟器。
 * @param addr 地址。
 * @return 寄存器值（未映射返回 0）。
 */
static uint16_t xvfdEmuGet(XvfdSlaveEmu* em, uint16_t addr)
{
    uint16_t v = 0;
    if (!XModbusServer_data2((XModbusServer*)em->m_server, XModbusHoldingRegisters,
                             addr, &v)) return 0;
    return v;
}

/**
 * @brief 创建模拟器（TCP 从站上线监听）。
 * @param em 模拟器。
 * @param brand 品牌（XVFD_EMU_*）。
 * @param outPort 输出监听端口；可为 NULL。
 * @return 成功返回 true。
 */
static bool xvfdSlaveEmuCreate(XvfdSlaveEmu* em, int brand, uint16_t* outPort)
{
    XVariant* a;
    XVariant* p;
    memset(em, 0, sizeof(*em));
    em->m_brand = brand;
    em->m_state = 2;            /* ATV61：Switch on disabled */
    em->m_m512State = 3;        /* M512：停机 */
    em->m_driveThermal = 25;
    em->m_motorThermal = 30;
    em->m_maxFreq = 5000;       /* M512：P0.10 = 50.00Hz */
    em->m_server = XModbusTcpServer_create();
    if (!em->m_server) return false;
    a = XVariant_create_utf8_str("127.0.0.1");
    p = XVariant_create_int(0);
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)em->m_server,
        XModbusDevice_NetworkAddressParameter, a);
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)em->m_server,
        XModbusDevice_NetworkPortParameter, p);
    XClassDelete((XClass*)a);
    XClassDelete((XClass*)p);
    /* XModbusDataUnitMap 每寄存器类型一条 unit（xindustry 同款）：HR 用
     * 一条覆盖全部用址的大段（ATV61: 3201..9002；M512: 0x1000..0xFD05）。 */
    if (brand == XVFD_EMU_ATV61) {
        if (!xvfdEmuAddRange(em, 3201, 9002 - 3201 + 1)) return false;
    } else {
        if (!xvfdEmuAddRange(em, 0x1000, 0xFD06 - 0x1000)) return false;
    }
    /* 对齐 xindustry_modbus_test 的成熟用法：setMap_ref_base（深拷贝进服务器）。 */
    if (!em->m_map ||
        !XModbusServer_setMap_ref_base((XModbusServer*)em->m_server, em->m_map)) {
        return false;
    }
    em->m_map = NULL;              /* 深拷贝完成，本侧副本释放（destroy 不再管） */
    if (!XModbusDevice_connectDevice((XModbusDevice*)em->m_server)) return false;
    if (outPort && em->m_server->m_tcpServer) {
        *outPort = XTcpServer_serverPort(em->m_server->m_tcpServer);
    }
    return true;
}

/**
 * @brief ATV61 故障注入（等价 python 的 fault 文件语义）。
 * @param em 模拟器。
 * @param code 故障码（0=清除）。
 * @return 无。
 */
static void xvfdEmuAtv61InjectFault(XvfdSlaveEmu* em, int code)
{
    em->m_fault = code;
    if (code) em->m_state = 2;
}

/**
 * @brief M512 故障注入。
 * @param em 模拟器。
 * @param code 故障码（0=清除）。
 * @return 无。
 */
static void xvfdEmuM512InjectFault(XvfdSlaveEmu* em, int code)
{
    em->m_m512Fault = code;
}

/**
 * @brief M512 命令字状态机（复刻 python apply_cmd）。
 * @param em 模拟器。
 * @param cmd 命令字。
 * @return 无。
 */
static void xvfdEmuM512ApplyCmd(XvfdSlaveEmu* em, uint16_t cmd)
{
    if (cmd == 1 || cmd == 3) em->m_m512State = 1;
    else if (cmd == 2 || cmd == 4) em->m_m512State = 2;
    else if (cmd == 5 || cmd == 6) em->m_m512State = 3;
    else if (cmd == 7) { em->m_m512Fault = 0; em->m_m512State = 3; }
}

/**
 * @brief ATV61 命令字状态机（复刻 python apply_cmd）。
 * @param em 模拟器。
 * @param cmd 命令字。
 * @return 无。
 */
static void xvfdEmuAtv61ApplyCmd(XvfdSlaveEmu* em, uint16_t cmd)
{
    int bit7 = (cmd >> 7) & 1;
    int bit3 = (cmd >> 3) & 1;
    int bit1 = (cmd >> 1) & 1;
    int bit0 = cmd & 1;
    em->m_reverse = ((cmd >> 11) & 1) ? 1 : 0;
    if (em->m_fault && !bit7) return;
    if (bit7) {                    /* Fault reset 上升沿 */
        em->m_fault = 0;
        em->m_state = 2;
        return;
    }
    if (!bit1 && !bit0) {          /* Disable voltage → 状态 2 */
        em->m_state = 2;
    } else if (!bit3) {            /* Shutdown / Switch on / Disable operation */
        em->m_state = (bit0 && bit1) ? 4 : 3;
    } else {                       /* Enable operation */
        em->m_state = 5;
    }
}

/**
 * @brief ATV61 ETA 状态字（复刻 python eta()）。
 * @param em 模拟器。
 * @return ETA 值。
 */
static uint16_t xvfdEmuAtv61Eta(const XvfdSlaveEmu* em)
{
    uint16_t eta = 0x0010;         /* Voltage enabled */
    switch (em->m_state) {
    case 2: eta |= 0x0040; break;
    case 3: eta |= 0x0021; break;
    case 4: eta |= 0x0023; break;
    case 5: eta |= 0x0027; break;
    default: break;
    }
    if (em->m_state != 2) eta |= 0x0020;   /* Quick stop 未激活 */
    if (em->m_fault) eta |= 0x0008;
    if (em->m_state == 5) {
        eta |= 0x0200;             /* Remote */
        eta |= 0x0400;             /* Target reached */
    }
    if (em->m_reverse) eta |= 0x4000;
    return eta;
}

/**
 * @brief 模拟器单拍：读命令/给定 → 推进状态机 → 回写状态与遥测。
 * @param em 模拟器。
 * @return 无。
 * @note 由测试泵循环（pump）每轮调用；无真实串口时序，语义等价即可。
 */
static void xvfdSlaveEmuTick(XvfdSlaveEmu* em)
{
    if (!em || !em->m_server) return;
    if (em->m_brand == XVFD_EMU_ATV61) {
        uint16_t cmd = xvfdEmuGet(em, 8501);
        if (cmd != em->m_lastCmd) {
            em->m_lastCmd = cmd;
            xvfdEmuAtv61ApplyCmd(em, cmd);
        }
        em->m_refHz10 = (int16_t)xvfdEmuGet(em, 8521);
        em->m_refRpm = (int16_t)xvfdEmuGet(em, 8602);
        xvfdEmuPut(em, 3201, xvfdEmuAtv61Eta(em));   /* ETA */
        xvfdEmuPut(em, 3202, (uint16_t)em->m_refHz10); /* RFR 0.1Hz */
        xvfdEmuPut(em, 3203, (uint16_t)em->m_refHz10); /* FRH */
        xvfdEmuPut(em, 3204, 100);                     /* LCR 电流 */
        xvfdEmuPut(em, 3205, 69);                      /* OTR 6.9% */
        xvfdEmuPut(em, 3206, 0);                       /* ETI */
        xvfdEmuPut(em, 3207, 240);                     /* ULN 24.0V */
        xvfdEmuPut(em, 3208, 240);                     /* UOP */
        xvfdEmuPut(em, 3209, (uint16_t)em->m_driveThermal);
        xvfdEmuPut(em, 3243, 0x0BB8);                  /* VBUS 300.0V */
        xvfdEmuPut(em, 7121, (uint16_t)em->m_fault);   /* LFT */
        xvfdEmuPut(em, 8604, (uint16_t)em->m_refRpm);  /* RFRD */
        xvfdEmuPut(em, 9630, (uint16_t)em->m_motorThermal);
    } else {
        uint16_t cmd = xvfdEmuGet(em, 0x2000);
        int hz100;
        if (cmd != em->m_lastCmd) {
            em->m_lastCmd = cmd;
            xvfdEmuM512ApplyCmd(em, cmd);
        }
        em->m_setpoint = (int16_t)xvfdEmuGet(em, 0x1000);
        /* 运行频率 = 设定% × 最大频率（0.01Hz 口径） */
        hz100 = (int)((double)em->m_setpoint / 10000.0 * (em->m_maxFreq / 100.0) * 100.0);
        xvfdEmuPut(em, 0x1001, (uint16_t)(hz100 & 0xFFFF));
        xvfdEmuPut(em, 0x1002, 5400);          /* 母线电压 540.0V */
        xvfdEmuPut(em, 0x1003, (uint16_t)(em->m_setpoint < 0 ? 0 : 1000));
        xvfdEmuPut(em, 0x1004, 100);           /* 电流 1.00A */
        xvfdEmuPut(em, 0x1005, 75);            /* 功率 7.5kW */
        xvfdEmuPut(em, 0x1006, 690);           /* 转矩 69.0% */
        /* 复刻 python：故障时状态字按设定方向显示运行，否则显示状态机状态 */
        xvfdEmuPut(em, 0x3000, (uint16_t)(em->m_m512Fault
            ? (em->m_setpoint >= 0 ? 1 : 2)
            : em->m_m512State));
        xvfdEmuPut(em, 0x8000, (uint16_t)em->m_m512Fault);
        xvfdEmuPut(em, 0xF00A, (uint16_t)em->m_maxFreq);
    }
}

/**
 * @brief 拆除模拟器（断开并删除 TCP 从站）。
 * @param em 模拟器。
 * @return 无。
 */
static void xvfdSlaveEmuDestroy(XvfdSlaveEmu* em)
{
    if (!em) return;
    if (em->m_map) {
        XClassDelete((XClass*)em->m_map);
        em->m_map = NULL;
    }
    if (em->m_server) {
        XModbusDevice_disconnectDevice((XModbusDevice*)em->m_server);
        XObject_deleteLater((XObject*)em->m_server);
        em->m_server = NULL;
    }
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */
#endif /* XVFD_SLAVE_EMU_H */
