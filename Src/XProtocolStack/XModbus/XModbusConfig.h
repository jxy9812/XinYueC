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
 * 本软件由作者“按原样”提供，任何明示或暗示的保证，包括但不限于适销性和特定用途适用性的暗示保证，均不予以保证。
 * 在任何情况下，作者均不对因使用本软件而产生的任何直接、间接、偶然、特殊、惩戒性或后果性损害（包括但不限于替代商品或服务的采购、使用损失、数据或利润损失、或业务中断）承担责任，无论这种损害是如何引起的，也无论责任理论是合同责任、严格责任还是侵权责任（包括疏忽或其他原因），即使已被告知可能发生此类损害。
 */

 #ifndef _MB_CONFIG_H
 #define _MB_CONFIG_H
 
#ifdef __cplusplus
extern "C" {
#endif

 /* ----------------------- 宏定义 ------------------------------------------*/
 /*! \defgroup modbus_cfg Modbus配置
  *
  * 协议栈中的大多数模块都是完全可选的，可以排除。这在目标资源非常有限且需要节省程序内存空间时尤为重要。<br>
  *
  * 所有这些设置都在文件 <code>mbconfig.h</code> 中定义
  */
 /*! \addtogroup modbus_cfg
  *  @{
  */
 /*! \brief 启用Modbus ASCII协议支持 */
 #define MB_ASCII_ENABLED                        (  0 )
 
 /*! \brief 启用Modbus RTU协议支持 */
 #define MB_RTU_ENABLED                          (  1 )
 
 /*! \brief 启用Modbus TCP协议支持（0表示禁用） */
 #define MB_TCP_ENABLED                          (  0 )
 
 /*! \brief Modbus ASCII协议的字符超时时间（秒）
  *
  * 字符超时时间对于Modbus ASCII协议不是固定的，因此是一个配置选项。
  * 应设置为网络预期的最大延迟时间。
  */
 #define MB_ASCII_TIMEOUT_SEC                    (  1 )
 
 /*! \brief ASCII模式下发送前的等待超时（毫秒）
  *
  * 如果定义此宏，函数会调用vMBPortSerialDelay并传入MB_ASCII_TIMEOUT_WAIT_BEFORE_SEND_MS参数，
  * 以在启用串口发送器前引入延迟。这是因为某些目标设备速度过快，导致接收和发送帧之间没有间隔。
  * 如果主机启用接收器的速度过慢，可能无法正确接收响应。
  */
 #ifndef MB_ASCII_TIMEOUT_WAIT_BEFORE_SEND_MS
 #define MB_ASCII_TIMEOUT_WAIT_BEFORE_SEND_MS    ( 0 )
 #endif
 
 /*! \brief 协议栈支持的最大Modbus功能码数量
  *
  * 支持的最大功能码数量必须大于本文件中所有启用功能和自定义功能处理程序的总和。
  * 如果设置过小，添加更多功能将失败。
  */
 #define MB_FUNC_HANDLERS_MAX                    ( 16 )
 
 /*! \brief 为“报告从机ID”命令分配的字节数
  *
  * 该数值限制了报告从机ID功能中附加段的最大大小。
  * 有关如何设置此值的详细信息，请参阅eMBSetSlaveID()函数。
  * 仅在MB_FUNC_OTHER_REP_SLAVEID_ENABLED设置为1时使用。
  */
 #define MB_FUNC_OTHER_REP_SLAVEID_BUF           ( 32 )
 
 /*! \brief 启用“报告从机ID”功能 */
 #define MB_FUNC_OTHER_REP_SLAVEID_ENABLED       (  1 )
 
 /*! \brief 启用“读取输入寄存器”功能（功能码0x04） */
 #define MB_FUNC_READ_INPUT_ENABLED              (  1 )
 
 /*! \brief 启用“读取保持寄存器”功能（功能码0x03） */
 #define MB_FUNC_READ_HOLDING_ENABLED            (  1 )
 
 /*! \brief 启用“写入单个寄存器”功能（功能码0x06） */
 #define MB_FUNC_WRITE_HOLDING_ENABLED           (  1 )
 
 /*! \brief 启用“写入多个寄存器”功能（功能码0x10） */
 #define MB_FUNC_WRITE_MULTIPLE_HOLDING_ENABLED  (  1 )
 
 /*! \brief 启用“读取线圈”功能（功能码0x01） */
 #define MB_FUNC_READ_COILS_ENABLED              (  1 )
 
 /*! \brief 启用“写入单个线圈”功能（功能码0x05） */
 #define MB_FUNC_WRITE_COIL_ENABLED              (  1 )
 
 /*! \brief 启用“写入多个线圈”功能（功能码0x0F） */
 #define MB_FUNC_WRITE_MULTIPLE_COILS_ENABLED    (  1 )
 
 /*! \brief 启用“读取离散输入”功能（功能码0x02） */
 #define MB_FUNC_READ_DISCRETE_INPUTS_ENABLED    (  1 )
 
 /*! \brief 启用“读写多个寄存器”功能（功能码0x17） */
 #define MB_FUNC_READWRITE_HOLDING_ENABLED       (  1 )
//io设备接收缓冲区默认大小
 #define XMB_DEVICE_RECV_BUFFER_SIZE			 (  128 )
//io设备发送缓冲区默认大小
 #define XMB_DEVICE_SEND_BUFFER_SIZE		   	 (  128 )
//发送帧队列最大元素个数
#define XMB_FRAME_SEND_QUEUE_COUNT		    	 (  30 )
//接收帧队列最大元素个数
#define XMB_FRAME_RECV_QUEUE_COUNT		    	 (  30 )
//事件队列最大元素个数
#define XMB_EVENT_QUEUE_COUNT		    		 (  30 )
//接收缓冲区默认大小
 #define XMB_RECV_BUFFER_SIZE					 (  1024 )
//主站接收等待时间(发送队列发送数据间隔时间倍数)  
 #define XMB_MASTER_RECV_WAIT_TIME				 (  5 )
//主站接收返回超时时间 (ms)
 #define XMB_MASTER_RECV_OUT_TIME				 (  1000 )
//是否完整的帧一起发送
 #define XMB_IS_COMP_SEND_FRAME					 (  1 )
 //接收帧显示
#define XMB_RECV_FRAME_16HEX_SHOW				 (  0 )
#define XMB_RECV_FRAME_STR_SHOW				     (  0 )
 //发送帧显示
 #define XMB_SEND_FRAME_SHOW				     (  0 )
//枚举可以转String
 #define XMB_ENUM_TO_STRING						 (  1 )
//显示处理的事件
 #define XMB_EVENT_HANDLE_SHOW				     (  0 )
//队列溢出(满)提示信息
 #define XMB_QUEUE_FULL_SHOW					 (  1 )
 /*! @} */

//宏定义检查
//#if XMB_MASTER_RECV_OUT_TIME <XMB_MASTER_RECV_WAIT_TIME
//#error "MB_MASTER_RECV_OUT_TIME 需要大于 MB_MASTER_RECV_WAIT_TIME"  
//#endif
#ifdef __cplusplus
}
#endif
 #endif