 #ifndef XDATAFRAMECOMMCONFIG_H
 #define XDATAFRAMECOMMCONFIG_H
 
#ifdef __cplusplus
extern "C" {
#endif
//io设备接收缓冲区默认大小
#define XDFC_DEVICE_RECV_BUFFER_SIZE			   	 (  128 )
//io设备发送缓冲区默认大小
#define XDFC_DEVICE_SEND_BUFFER_SIZE		    	 (  128 )
//发送帧队列最大元素个数
#define XDFC_FRAME_SEND_QUEUE_COUNT		    		(  30 )
//接收帧队列最大元素个数
#define XDFC_FRAME_RECV_QUEUE_COUNT		    		(  30 )
//事件队列最大元素个数
#define XDFC_EVENT_QUEUE_COUNT		    			(  30 )
//接收缓冲区默认大小
#define XDFC_RECV_BUFFER_SIZE						 (  1024 )
//半双工下发送等待时间
#define XDFC_HALF_DUPLEX_SEND_WAIT_TIME             (2)
//一帧数据接收结束事件
#define XDFC_FRAME_END_TIMEOUT_TIME					(2)
//主站接收返回超时时间 (ms)
#define XDFC_MASTER_RECV_OUT_TIME					(  1000 )
//是否完整的帧一起发送
#define XDFC_IS_COMP_SEND_FRAME						 (  1 )
//定期发送的帧是否要拷贝
#define XDFC_SEND_FRAME_REGULARLY_COPY				 (  1 )
//接收帧16进制显示
#define XDFC_RECV_FRAME_16HEX_SHOW					 (  1 )
//接收帧字符串显示
#define XDFC_RECV_FRAME_STR_SHOW					 (  0 )
 //发送帧16进制显示
#define XDFC_SEND_FRAME_16HEX_SHOW					 (  1 )
//发送帧字符串显示
#define XDFC_SEND_FRAME_STR_SHOW					 (  0 )
//枚举可以转String
#define XDFC_ENUM_TO_STRING							 (  1 )
//显示处理的事件
#define XDFC_EVENT_HANDLE_SHOW						 (  0 )
//队列溢出(满)提示信息
#define XDFC_QUEUE_FULL_SHOW						 (  1 )
 /*! @} */
#ifdef __cplusplus
}
#endif
 #endif