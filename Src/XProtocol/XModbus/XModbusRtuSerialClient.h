#ifndef XMODBUSRTUSERIALCLIENT_H
#define XMODBUSRTUSERIALCLIENT_H

#include "XModbusClient.h"
#include "XModbusPdu.h"
#include "XSerialPort.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusRtuSerialClient.h
 * @brief Modbus RTU串口客户端（对齐Qt6 QModbusRtuSerialClient）
 * @details 实现基于串口通信的Modbus RTU主站功能
 *
 * @par 功能特性
 * - 支持标准Modbus RTU协议
 * - 自动计算帧间延迟（基于波特率）
 * - 半双工模式，同一时间只处理一个请求
 * - 支持超时重试机制
 *
 * @par 使用示例
 * @code
 * // 创建RTU客户端
 * XModbusRtuSerialClient* client = XModbusRtuSerialClient_create();
 *
 * // 配置串口
 * XSerialPort* port = XModbusRtuSerialClient_serialPort(client);
 * XSerialPort_setPortName(port, "COM1");
 * XSerialPort_setBaudRate(port, 9600);
 *
 * // 打开设备
 * if (XModbusRtuSerialClient_open_base((XModbusDevice*)client)) {
 *     // 发送请求
 *     XModbusDataUnit data;
 *     XModbusDataUnit_init(&data, XModbusHoldingRegisters, 0, 10);
 *     XModbusReply* reply = XModbusClient_sendReadRequest((XModbusClient*)client, &data, 1);
 * }
 *
 * // 清理
 * XModbusRtuSerialClient_deleteLater(client);
 * @endcode
 */

XCLASS_DEFINE_BEGING(XModbusRtuSerialClient)
XCLASS_DEFINE_EXTEND_END(XModbusRtuSerialClient, XModbusClient)

/**
 * @brief Modbus RTU串口客户端结构体
 * @details 继承自XModbusClient，实现RTU串口通信的主站功能
 *
 * @par 成员说明
 * | 成员 | 类型 | 说明 |
 * |------|------|------|
 * | m_base | XModbusClient | 基类，继承自XModbusClient |
 * | m_interFrameDelay | int | 帧间延迟（微秒），0表示自动计算 |
 * | m_turnaroundDelay | int | 响应延迟（毫秒） |
 * | m_currentReply | XModbusReply* | 当前等待响应的Reply对象 |
 * | m_timeoutTimer | XTimerId | 超时定时器ID |
 * | m_receiveBuffer | XByteArray* | 接收缓冲区 |
 * | m_currentServerAddress | uint8_t | 当前请求的从站地址 |
 */
typedef struct XModbusRtuSerialClient {
    XModbusClient m_base;           ///< 继承自XModbusClient
    int m_interFrameDelay;          ///< 帧间延迟（微秒），0表示自动计算
    int m_turnaroundDelay;          ///< 响应延迟（毫秒）
    XModbusReply* m_currentReply;   ///< 当前等待响应的Reply对象

    XTimerId m_interFrameTimer;     ///< 帧间延迟定时器ID
    uint8_t m_currentServerAddress; ///< 当前请求的从站地址
    uint8_t m_retryCount;           ///< 当前重试次数
    bool m_waitingForTurnaround;    ///< 是否正在等待广播响应延迟
    XByteArray* m_receiveBuffer;
    XByteArray* m_requestData;       ///< 请求数据（用于重试）
    XQueueBase* m_queue;//任务队列
} XModbusRtuSerialClient;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XModbusRtuSerialClient的虚函数表
  * @return 初始化完成的虚函数表指针
  * @note 该函数是线程安全的，多次调用返回同一虚表实例
  */
XVtable* XModbusRtuSerialClient_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusRtuSerialClient实例
 * @return 成功返回指向新分配XModbusRtuSerialClient对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 释放
 * @par 内存管理
 * - 创建时会自动创建关联的XSerialPort对象
 * - 删除时会自动释放关联的XSerialPort对象
 */
XModbusRtuSerialClient* XModbusRtuSerialClient_create(void);

/**
 * @brief 初始化一个已分配的XModbusRtuSerialClient实例
 * @param client 待初始化的XModbusRtuSerialClient对象指针（非NULL）
 * @note 该函数会初始化基类成员、创建串口对象、设置默认参数
 * @par 默认参数
 * - 帧间延迟：自动计算（基于波特率）
 * - 响应延迟：100毫秒
 * - 超时时间：1000毫秒（继承自基类）
 * - 重试次数：3次（继承自基类）
 */
void XModbusRtuSerialClient_init(XModbusRtuSerialClient* client);

/******************************************************************************************
 * RTU特有属性接口
 ******************************************************************************************/

 /**
  * @brief 获取帧间延迟时间
  * @param client XModbusRtuSerialClient实例指针
  * @return 帧间延迟时间（微秒）
  * @par 返回值说明
  * - 如果设置了非零值，返回设置的值
  * - 否则返回根据波特率自动计算的值
  * - client为NULL时返回默认值1750微秒
  * @par Modbus规范
  * RTU帧间延迟应为3.5个字符时间，每个字符11位（1起始+8数据+奇偶+1停止）
  * - 9600波特率：约4ms
  * - 19200波特率：约2ms
  * - 38400及以上：固定1.75ms
  */
int XModbusRtuSerialClient_interFrameDelay(const XModbusRtuSerialClient* client);

/**
 * @brief 设置帧间延迟时间
 * @param client XModbusRtuSerialClient实例指针（非NULL）
 * @param microseconds 延迟时间（微秒）
 * @par 特殊值
 * - 设置为0：使用自动计算值（根据波特率）
 * - 设置为-1：使用自动计算值
 * - 设置值小于自动计算值：使用自动计算值
 * @note 已建立的连接不受此设置影响
 */
void XModbusRtuSerialClient_setInterFrameDelay(XModbusRtuSerialClient* client, int microseconds);

/**
 * @brief 获取响应延迟时间
 * @param client XModbusRtuSerialClient实例指针
 * @return 响应延迟时间（毫秒），client为NULL时返回默认值100
 * @par 说明
 * 响应延迟用于广播消息后等待从站处理的时间
 * 典型值为100-200毫秒
 */
int XModbusRtuSerialClient_turnaroundDelay(const XModbusRtuSerialClient* client);

/**
 * @brief 设置响应延迟时间
 * @param client XModbusRtuSerialClient实例指针（非NULL）
 * @param turnaroundDelay 延迟时间（毫秒）
 * @note 建议范围：100-200毫秒
 */
void XModbusRtuSerialClient_setTurnaroundDelay(XModbusRtuSerialClient* client, int turnaroundDelay);

/**
 * @brief 获取关联的串口对象
 * @param client XModbusRtuSerialClient实例指针
 * @return 串口对象指针，client为NULL或串口未创建时返回NULL
 * @note 返回的串口对象由XModbusRtuSerialClient管理，不需要手动释放
 * @par 使用示例
 * @code
 * XSerialPort* port = XModbusRtuSerialClient_serialPort(client);
 * XSerialPort_setPortName(port, "COM1");
 * XSerialPort_setBaudRate(port, 9600);
 * XSerialPort_setDataBits(port, XSerialPort_Data8);
 * XSerialPort_setParity(port, XSerialPort_NoParity);
 * XSerialPort_setStopBits(port, XSerialPort_OneStop);
 * @endcode
 */
XSerialPort* XModbusRtuSerialClient_serialPort(const XModbusRtuSerialClient* client);

/******************************************************************************************
 * 继承自父类的API（使用宏定义）
 ******************************************************************************************/

 /**
  * @brief 发送原始Modbus请求（虚函数，通过虚函数表调用）
  * @param client XModbusRtuSerialClient实例指针
  * @param request 原始请求PDU
  * @param serverAddress 目标从站地址
  * @return 成功返回XModbusReply指针，失败返回NULL
  * @note 调用者负责释放返回的Reply对象
  */
#define XModbusRtuSerialClient_sendRawRequest       XModbusClient_sendRawRequest_base

      /**
       * @brief 发送Modbus读取请求
       * @param client XModbusRtuSerialClient实例指针
       * @param read 要读取的数据单元
       * @param serverAddress 目标从站地址
       * @return 成功返回XModbusReply指针，失败返回NULL
       */
#define XModbusRtuSerialClient_sendReadRequest      XModbusClient_sendReadRequest

       /**
        * @brief 发送Modbus写入请求
        * @param client XModbusRtuSerialClient实例指针
        * @param write 要写入的数据单元
        * @param serverAddress 目标从站地址
        * @return 成功返回XModbusReply指针，失败返回NULL
        */
#define XModbusRtuSerialClient_sendWriteRequest     XModbusClient_sendWriteRequest

        /**
         * @brief 发送Modbus读写组合请求（功能码0x17）
         * @param client XModbusRtuSerialClient实例指针
         * @param read 读取部分的数据单元
         * @param write 写入部分的数据单元
         * @param serverAddress 目标从站地址
         * @return 成功返回XModbusReply指针，失败返回NULL
         */
#define XModbusRtuSerialClient_sendReadWriteRequest XModbusClient_sendReadWriteRequest

         /**
          * @brief 获取请求超时时间
          * @param client XModbusRtuSerialClient实例指针
          * @return 超时时间（毫秒）
          */
#define XModbusRtuSerialClient_timeout              XModbusClient_timeout

          /**
           * @brief 设置请求超时时间
           * @param client XModbusRtuSerialClient实例指针
           * @param newTimeout 新的超时时间（毫秒）
           */
#define XModbusRtuSerialClient_setTimeout           XModbusClient_setTimeout

           /**
            * @brief 获取请求重试次数
            * @param client XModbusRtuSerialClient实例指针
            * @return 重试次数
            */
#define XModbusRtuSerialClient_numberOfRetries      XModbusClient_numberOfRetries

            /**
             * @brief 设置请求重试次数
             * @param client XModbusRtuSerialClient实例指针
             * @param number 新的重试次数
             */
#define XModbusRtuSerialClient_setNumberOfRetries   XModbusClient_setNumberOfRetries

             /******************************************************************************************
              * 设备操作接口（虚函数）
              ******************************************************************************************/

              /**
               * @brief 打开设备（虚函数）
               * @param device XModbusRtuSerialClient实例指针（转换为XModbusDevice*）
               * @return 成功返回true，失败返回false
               * @par 说明
               * - 打开时会清除串口缓冲区中的现有数据
               * - 打开成功后设备状态变为XModbusDevice_ConnectedState
               */
#define XModbusRtuSerialClient_open_base         XModbusDevice_open_base

               /**
                * @brief 关闭设备（虚函数）
                * @param device XModbusRtuSerialClient实例指针（转换为XModbusDevice*）
                * @par 说明
                * - 关闭时会停止所有正在进行的请求
                * - 关闭后设备状态变为XModbusDevice_UnconnectedState
                */
#define XModbusRtuSerialClient_close_base        XModbusDevice_close_base

                /******************************************************************************************
                 * 内存管理宏
                 ******************************************************************************************/

                 /**
                  * @brief 析构函数（延迟删除）
                  * @param obj XModbusRtuSerialClient实例指针
                  * @note 将对象加入待删除队列，在事件循环中删除
                  */
#define XModbusRtuSerialClient_deinitLater   XModbusClient_deinitLater

                  /**
                   * @brief 删除对象（延迟删除）
                   * @param obj XModbusRtuSerialClient实例指针
                   * @note 将对象加入待删除队列，在事件循环中删除
                   */
#define XModbusRtuSerialClient_deleteLater   XModbusClient_deleteLater

#ifdef __cplusplus
}
#endif

#endif // XMODBUSRTUSERIALCLIENT_H