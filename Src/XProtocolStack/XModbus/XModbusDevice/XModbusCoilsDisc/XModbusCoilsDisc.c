#include "XModbusCoilsDisc.h"
#include "XMemory.h"
#include "XByteArray.h"
#include"XModbusFrame.h"
#include"XModbus.h"
#include <string.h>
/*
* @brief  一个字节中按偏移量设置一个比特位
* @param  ByteBuff:uint8_t* 类型 指向要操作的字节
* @param  Offset:偏移量(0~7)
* @param  State:要设置的状态(0或1)
* @retval
*/
#define XMODBUS_UINT8_SET_BITS(ByteBuff,Offset,State) (*(ByteBuff)=((State<<Offset)|(*(ByteBuff))))
/*
* @brief  一个字节中按偏移量查看一个比特位
* @param  ByteBuff:uint8_t* 类型 指向要查看的字节
* @param  Offset:偏移量(0~7)
* @retval 状态(0或1)
*/
#define XMODBUS_UINT8_GET_BITS(ByteBuff,Offset) (((*(ByteBuff))>>Offset)&0x1) 

XModbusCoilsDisc* XModbusCoilsDisc_create(uint16_t count)
{
    if (count == 0)
        return NULL;
    uint16_t size = (count % 8 == 0) ? (count / 8) : ((count / 8) + 1);
    XModbusCoilsDisc* ptr = XMemory_malloc(sizeof(XModbusCoilsDisc));
    ptr->count = count;
    ptr->parent.data = XVector_Create(char);
    XVector_resize_base(ptr->parent.data, size);
    return ptr;
}

void XModbusCoilsDisc_delete(XModbusCoilsDisc* pRegHandler)
{
    if (pRegHandler)
    {
        if (pRegHandler->parent.data)
            XVector_delete_base(pRegHandler->parent.data);
        XMemory_free(pRegHandler);
    }
}
// 定义MIN宏
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
bool XModbusCoilsDisc_write(XModbusCoilsDisc* pRegHandler, uint16_t address, uint16_t count, const uint8_t* writeArray)
{
    if (!pRegHandler || !pRegHandler->parent.data || !writeArray || !count)
        return false;

    uint16_t max_address = address + count;
    if (max_address > pRegHandler->count)
        return false;

    uint8_t* data = XContainerDataPtr(pRegHandler->parent.data);
    uint16_t byte_count = (max_address + 7) / 8 - (address / 8);

    // 计算起始位偏移
    uint8_t bit_offset = address % 8;

    // 情况1: 完全字节对齐 - 直接整字节复制
    if (bit_offset == 0 && count % 8 == 0) {
        memcpy(data + (address / 8), writeArray, byte_count);
        return true;
    }

    // 情况2: 部分对齐或完全不对齐 - 优化处理
    uint16_t processed = 0;

    // 处理起始处可能的非对齐位
    if (bit_offset) {
        uint16_t bits_to_process = MIN(8 - bit_offset, count);
        for (uint16_t i = 0; i < bits_to_process; i++) {
            uint8_t bit_val = (writeArray[i / 8] >> (i % 8)) & 1;
            if (bit_val) data[address / 8] |= (1 << (bit_offset + i));
            else data[address / 8] &= ~(1 << (bit_offset + i));
        }
        processed += bits_to_process;
    }

    // 批量复制中间对齐的字节
    uint16_t aligned_bytes = (count - processed) / 8;
    if (aligned_bytes) {
        uint16_t src_idx = processed / 8;
        uint16_t dst_idx = (address + processed) / 8;
        memcpy(data + dst_idx, writeArray + src_idx, aligned_bytes);
        processed += aligned_bytes * 8;
    }

    // 处理结束处可能的非对齐位
    if (processed < count) {
        uint16_t remaining = count - processed;
        uint16_t src_idx = processed / 8;
        uint16_t dst_idx = (address + processed) / 8;

        for (uint16_t i = 0; i < remaining; i++) {
            uint8_t bit_val = (writeArray[src_idx + i / 8] >> (i % 8)) & 1;
            if (bit_val) data[dst_idx] |= (1 << i);
            else data[dst_idx] &= ~(1 << i);
        }
    }

    return true;
}

bool XModbusCoilsDisc_read(XModbusCoilsDisc* pRegHandler, uint16_t address, uint16_t count, uint8_t* readArray, uint16_t readArraySize)
{
    if (!pRegHandler || !pRegHandler->parent.data || !readArray || !count)
        return false;

    uint16_t max_address = address + count;
    if (max_address > pRegHandler->count)
        return false;

    const uint8_t* data = XContainerDataPtr(pRegHandler->parent.data);
    uint16_t bytes_needed = (count + 7) / 8;

    if (readArraySize < bytes_needed)
        return false;

    memset(readArray, 0, bytes_needed);

    // 计算起始位偏移
    uint8_t bit_offset = address % 8;

    // 情况1: 完全字节对齐 - 直接整字节复制
    if (bit_offset == 0 && count % 8 == 0) {
        memcpy(readArray, data + (address / 8), bytes_needed);
        return true;
    }

    // 情况2: 部分对齐或完全不对齐 - 优化处理
    uint16_t processed = 0;

    // 处理起始处可能的非对齐位
    if (bit_offset) {
        uint16_t bits_to_process = MIN(8 - bit_offset, count);
        for (uint16_t i = 0; i < bits_to_process; i++) {
            uint8_t bit_val = (data[address / 8] >> (bit_offset + i)) & 1;
            if (bit_val) readArray[i / 8] |= (1 << (i % 8));
        }
        processed += bits_to_process;
    }

    // 批量复制中间对齐的字节
    uint16_t aligned_bytes = (count - processed) / 8;
    if (aligned_bytes) {
        uint16_t src_idx = (address + processed) / 8;
        uint16_t dst_idx = processed / 8;
        memcpy(readArray + dst_idx, data + src_idx, aligned_bytes);
        processed += aligned_bytes * 8;
    }

    // 处理结束处可能的非对齐位
    if (processed < count) {
        uint16_t remaining = count - processed;
        uint16_t src_idx = (address + processed) / 8;
        uint16_t dst_idx = processed / 8;

        for (uint16_t i = 0; i < remaining; i++) {
            uint8_t bit_val = (data[src_idx] >> i) & 1;
            if (bit_val) readArray[dst_idx + i / 8] |= (1 << (i % 8));
        }
    }

    return true;
}

bool XModbusCoilsDisc_at(XModbusCoilsDisc* pRegHandler, uint16_t regAddress)
{
    if (!pRegHandler || !pRegHandler->parent.data || regAddress >= pRegHandler->count)
        return false;

    const uint8_t* data = XContainerDataPtr(pRegHandler->parent.data);
    uint16_t byte_index = regAddress / 8;
    uint8_t bit_index = regAddress % 8;

    return (data[byte_index] & (1 << bit_index)) != 0;
}

void XModbusCoilsDisc_0x01_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
    if (modbus == NULL || recvFrame == NULL || hand == NULL)
        return;
    XModbusCoilsDisc* coilsFunc = hand;
    XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
    if (rtu == NULL)
        return;

    if (rtu->data != NULL)
    {
        XModbusCoilsDisc_write(coilsFunc, rtu->regAddress, rtu->regCount,XContainerDataPtr(rtu->data));
    }
}

void XModbusCoilsDisc_0x02_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
    XModbusCoilsDisc_0x01_RTU_masterRecvHandCb(math,modbus,recvFrame,hand);
}

void XModbusCoilsDisc_0x01_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
    if (modbus == NULL || recvFrame == NULL || hand == NULL)
        return;
    XModbusCoilsDisc* coilsFunc = hand;
    XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
    XModbusFrame* sendFrame = XModbusFrame_create(modbus->m_mode);
    XVector* data = coilsFunc->parent.data;//寄存器数据
    if ((((rtu->regAddress) + (rtu->coilsCount)) <= coilsFunc->count) && ((rtu->coilsCount) > 0))
    {
        //void* readStart = XVector_at_base(data, rtu->coilsAddress);//寄存器数据缓冲区
        size_t buffSize = rtu->coilsCount / 8 + 1;
        uint8_t* buff = XMemory_malloc(buffSize);
        if(XModbusCoilsDisc_read(hand, rtu->address, rtu->coilsCount, buff, buffSize))
            XModbusFrameRTU_setFrameData_0x01_reply(sendFrame, rtu->address, buff, rtu->coilsCount);
        else
            XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_READ_COILS, MB_EX_ILLEGAL_DATA_ADDRESS);
        XMemory_free(buff);
    }
    else
    {//参数有问题
        XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_READ_COILS, MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    XModbus_sendData_base(modbus, sendFrame);
}

void XModbusCoilsDisc_0x02_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
    if (modbus == NULL || recvFrame == NULL || hand == NULL)
        return;
    XModbusCoilsDisc* coilsFunc = hand;
    XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
    XModbusFrame* sendFrame = XModbusFrame_create(modbus->m_mode);
    XVector* data = coilsFunc->parent.data;//寄存器数据
    if ((((rtu->regAddress) + (rtu->coilsCount)) <= coilsFunc->count) && ((rtu->coilsCount) > 0))
    {
        //void* readStart = XVector_at_base(data, rtu->coilsAddress);//寄存器数据缓冲区
        size_t buffSize = rtu->coilsCount / 8 + 1;
        uint8_t* buff = XMemory_malloc(buffSize);
        if (XModbusCoilsDisc_read(hand, rtu->address, rtu->coilsCount, buff, buffSize))
            XModbusFrameRTU_setFrameData_0x02_reply(sendFrame, rtu->address, buff, rtu->coilsCount);
        else
            XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_READ_DISCRETE_INPUTS, MB_EX_ILLEGAL_DATA_ADDRESS);
        XMemory_free(buff);
    }
    else
    {//参数有问题
        XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_READ_DISCRETE_INPUTS, MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    XModbus_sendData_base(modbus, sendFrame);
}

void XModbusCoilsDisc_0x05_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
    if (modbus == NULL || recvFrame == NULL || hand == NULL)
        return;
    XModbusCoilsDisc* coilsFunc = hand;
    XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
    if (rtu == NULL)
        return;

    XByteArray* sendFrame = XByteArray_create(0);
    if (rtu->data != NULL)
    {
        uint8_t buff;
        uint16_t cmp = XMODBUS_COILS_STATE_ON;
        if(memcmp(XContainerDataPtr(rtu->data),&cmp,2)==0)
            XMODBUS_UINT8_SET_BITS(&buff,0,true);
        else
            XMODBUS_UINT8_SET_BITS(&buff, 0, false);
        if (XModbusCoilsDisc_write(coilsFunc, rtu->regAddress, 1,&buff))
        {//写入成功 将数据帧再次发送回去
            XVector_swap_base(recvFrame->frameData, sendFrame);
        }
        else
        {
            XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_WRITE_SINGLE_COIL, MB_EX_SLAVE_DEVICE_FAILURE);//写入失败设备故障
        }
    }
    else
    {//参数有问题
        XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_WRITE_SINGLE_COIL, MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    XModbus_sendData_base(modbus, sendFrame);
}

void XModbusCoilsDisc_0x0F_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
    if (modbus == NULL || recvFrame == NULL || hand == NULL)
        return;
    XModbusCoilsDisc* coilsFunc = hand;
    XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
    if (rtu == NULL)
        return;
    //获取寄存器地址
    uint16_t coilsAddress = rtu->coilsAddress;
    //寄存器数量
    uint16_t coilsCount = rtu->coilsCount;

    XModbusFrame* sendFrame = XModbusFrame_create(modbus->m_mode);
    if (rtu->data != NULL)
    {
        if(XModbusCoilsDisc_write(coilsFunc, coilsAddress, coilsCount,XContainerDataPtr(rtu->data)))
            XModbusFrameRTU_setFrameData_0x0F_reply(sendFrame, rtu->address, coilsAddress, coilsCount);
        else
            XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_WRITE_MULTIPLE_COILS, MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    else
    {//参数有问题
        XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_WRITE_MULTIPLE_COILS, MB_EX_ILLEGAL_DATA_ADDRESS);
    }
    XModbus_sendData_base(modbus, sendFrame);
    //printf("写多个线圈\n");
}
