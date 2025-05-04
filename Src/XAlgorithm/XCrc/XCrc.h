/* 
 * 自由Modbus库：一个用于Modbus ASCII/RTU协议的可移植实现。
 * 版权所有 (c) 2006-2018 克里斯蒂安·沃尔特 <cwalter@embedded-solutions.at>
 * 保留所有权利。
 * 
 * 允许以源代码和二进制形式进行再分发和使用，无论是否进行修改，但需满足以下条件：
 * 1. 源代码的再分发必须保留上述版权声明、此条件列表和以下免责声明。
 * 2. 二进制形式的再分发必须在分发时提供的文档和/或其他材料中复制上述版权声明、此条件列表和以下免责声明。
 * 3. 未经作者事先明确书面许可，不得使用作者的姓名来认可或推广基于此软件衍生的产品。
 * 
 * 本软件由作者“按原样”提供，任何明示或暗示的保证，均不予以保证。
 * 在任何情况下，作者均不对因使用本软件而产生的直接或间接损害承担责任。
 */

#ifndef XCRC_H
#define XCRC_H
#ifdef __cplusplus
extern "C" {
#endif
#include"stdint.h"
//#include"port.h"
 /*!
  * @brief 计算Modbus RTU协议的CRC-16校验值
  * 
  * 该函数实现Modbus RTU标准的CRC-16算法，用于验证数据帧的完整性。
  * CRC-16校验覆盖数据帧的所有字节（从站地址、功能码、数据字段），
  * 计算结果存储为2字节，低字节在前，高字节在后。
  * 
  * @param pucFrame 指向待计算CRC的数据帧首地址
  * @param usLen 数据帧长度（单位：字节）
  * @return 16位CRC校验值（低字节在前，高字节在后）
  * 
  * @note 算法步骤：
  *  1. 初始化CRC为0xFFFF
  *  2. 对每个字节进行处理：CRC ^= 字节值
  *  3. 进行8次移位，每次检查最低位，为1时CRC ^= 0xA001
  *  4. 处理完所有字节后，返回CRC的反码（即高低字节交换后的值）
  * 
  * @example 典型用法：
  *          USHORT crc = usMBCRC16(frame, len);
  *          // 将crc填入帧尾的两个字节（低字节在前，高字节在后）
  */
 uint16_t          XCrc_get16( uint8_t * pucFrame, uint16_t usLen );
 //设置CRC16  mode为1是大端,0小端
 void XCrc_set16Data(uint8_t* pData, uint16_t crc16, uint8_t mode);
 // 宏函数用于设置 CRC16 数据，mode 为 1 表示大端模式，为 0 表示小端模式
#define XCrc_SET16DATA(pData, crc16, mode) \
    do { \
        if (mode) { \
            *(pData) = (uint8_t)((crc16) >> 8); \
            *(pData + 1) = (uint8_t)((crc16) & 0xFF); \
        } else { \
            *(pData) = (uint8_t)((crc16) & 0xFF); \
            *(pData + 1) = (uint8_t)((crc16) >> 8); \
        } \
    } while (0)
#ifdef __cplusplus
}
#endif
 #endif