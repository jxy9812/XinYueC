#ifdef USE_STDPERIPH_DRIVER
#include"XSerialPortSTM32.h"
#include"XInterrupt.h"
#include"XQueueBase.h"
static void USARTCallback(XSerialPort* serial);
static void VXSerialPort_NVIC_Init(XSerialPort* serial);//中断初始化
static bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XSerialPort* serial, const char* data, size_t maxSize);//写入
static size_t VXIODevice_writeFull(XSerialPort* serial);//将剩余的数据刷入设备
static size_t VXIODevice_read(XSerialPort* serial, char* data, size_t maxSize);//读取
static void VXIODevice_close(XSerialPort* serial);
static void VXIODevice_poll(XSerialPort* serial);
static void VXIODevice_setWriteBuffer(XSerialPort* serial, size_t count);
static void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count);
static size_t VXIODevice_getBytesAvailable(XSerialPort* serial);
XVtable* XSerialPort_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XSERIALPORT_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXSerialPort_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Write, VXIODevice_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_WriteFull, VXIODevice_writeFull);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read, VXIODevice_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXIODevice_close);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, NULL);
    //XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_SetWriteBuffer, VXIODevice_setWriteBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_SetReadBuffer, VXIODevice_setReadBuffer);
    //XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_GetBytesAvailable, VXIODevice_getBytesAvailable);
#if SHOWCONTAINERSIZE
    printf("XSerialPort size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

void XSerialPort_init(XSerialPort* serial)
{
    if (serial == NULL)
        return;
    memset(((XSerialPortBase*)serial) + 1, 0, sizeof(XSerialPort) - sizeof(XSerialPortBase));
    XSerialPortBase_init(serial, NULL);
    XClassGetVtable(serial) = XSerialPort_class_init();
}
#endif

#ifdef USE_STDPERIPH_DRIVER
XSerialPort* XSerialPort_create(XUsartGPIO* TX, XUsartGPIO* RX)
{
    XSerialPort* serial = XMemory_malloc(sizeof(XSerialPort));
    if (serial == NULL)
        return serial;
    XSerialPort_init(serial);
    serial->TX = *TX;
    serial->RX = *RX;
    return serial;
}
#endif


//启用标准库
#ifdef USE_STDPERIPH_DRIVER
typedef struct Usart
{
    uint8_t   GPIO_AF_USARTX;
    uint32_t  USARTX;
    uint32_t  USARTX_Clock;
}Usart;

//启用了F4系列
#ifdef STM32F40_41xxx
#include"stm32f4xx.h"
#define USART_COUNT 6 //
static XSerialPort* openUsart[USART_COUNT] = { 0 };//打开的串口
XSerialPort* XSerialPort_global(uint8_t port)
{
    if (port < 1 || port>6)
        return NULL;
    return openUsart[port - 1];
}
void VXSerialPort_NVIC_Init(XSerialPort* serial)
{
    if (serial == NULL || serial->USARTX == 0)
        return;
    USART_ClearFlag(serial->USARTX, USART_FLAG_TC);
    if (((XIODeviceBase*)serial)->m_readBuffer != NULL)
    {
        USART_ITConfig(serial->USARTX, USART_IT_RXNE, ENABLE);//开启相关中断
        NVIC_InitTypeDef NVIC_InitStructure;
        //Usart1 NVIC 配置
        if (serial->m_parent.m_portNum == 1)
            NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;//串口1中断通道
        else if (serial->m_parent.m_portNum == 2)
            NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;//串口2中断通道
        else if (serial->m_parent.m_portNum == 3)
            NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;//串口3中断通道
        else if (serial->m_parent.m_portNum == 6)
            NVIC_InitStructure.NVIC_IRQChannel = USART6_IRQn;//串口6中断通道
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;//抢占优先级
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		//子优先级
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
        NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器、
        XInterrupt_addUSARTxCallback(serial->m_parent.m_portNum, USARTCallback, serial);
    }
    else
    {
        USART_ITConfig(serial->USARTX, USART_IT_RXNE, DISABLE);//关闭接收相关中断
        XInterrupt_removeUSARTxCallback(serial->m_parent.m_portNum, USARTCallback, serial);
    }
}
bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode)
{
    if (XSerialPort_isOpen(serial))
        return true;//已经打开了
    uint8_t portIndex = ((XSerialPortBase*)serial)->m_portNum - 1;
    if (openUsart[portIndex] != NULL || (!(mode & XIODeviceBase_ReadOnly | mode & XIODeviceBase_WriteOnly)))
        return false;//已经被打开了打开失败

    Usart Usart1 = { GPIO_AF_USART1,USART1,RCC_APB2Periph_USART1 };
    Usart Usart2 = { GPIO_AF_USART2,USART2,RCC_APB1Periph_USART2 };
    Usart Usart3 = { GPIO_AF_USART3,USART3,RCC_APB1Periph_USART3 };
    Usart Usart6 = { GPIO_AF_USART6,USART6,RCC_APB2Periph_USART6 };
    Usart usart[] = { Usart1,Usart2,Usart3,{0},{0},Usart6 };
    //GPIO端口设置
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    //NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(serial->TX.GPIO_Clock, ENABLE); //使能GPIOA时钟
    RCC_AHB1PeriphClockCmd(serial->RX.GPIO_Clock, ENABLE); //使能GPIOA时钟
    if (portIndex == 0 || portIndex == 5)
    {//RCC_APB2Periph_USART1 USART1和USART6 在总线APB2
        RCC_APB2PeriphClockCmd(usart[portIndex].USARTX_Clock, ENABLE);//使能USART时钟
    }
    else
    {//RCC_APB1Periph_USART2 USART2和USART3 在总线APB2
        RCC_APB1PeriphClockCmd(usart[portIndex].USARTX_Clock, ENABLE);//使能USART时钟
    }

    //串口1对应引脚复用映射
    GPIO_PinAFConfig(serial->TX.GPIOX, serial->TX.GPIO_PinSourceX, usart[portIndex].GPIO_AF_USARTX); //GPIOA9复用为USART1
    GPIO_PinAFConfig(serial->RX.GPIOX, serial->RX.GPIO_PinSourceX, usart[portIndex].GPIO_AF_USARTX); //GPIOA10复用为USART1

    //USART1端口配置
    GPIO_InitStructure.GPIO_Pin = serial->TX.GPIO_Pin_X; //GPIO
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用功能
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;	//速度100MHz
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; //推挽复用输出
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //上拉
    GPIO_Init(serial->TX.GPIOX, &GPIO_InitStructure); //初始化

    GPIO_InitStructure.GPIO_Pin = serial->RX.GPIO_Pin_X; //GPIO
    GPIO_Init(serial->RX.GPIOX, &GPIO_InitStructure); //初始化

    //USART1 初始化设置
    USART_InitStructure.USART_BaudRate = ((XSerialPortBase*)serial)->m_baudRate;//波特率设置
    //数据位设置
    if (((XSerialPortBase*)serial)->m_dataBits == SP_DB_Nine)
        USART_InitStructure.USART_WordLength = USART_WordLength_9b;//字长为9位数据格式
    else
        USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
    //停止位设置
    if (((XSerialPortBase*)serial)->m_stopBits == SP_ST_One)
        USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
    else if (((XSerialPortBase*)serial)->m_stopBits == SP_ST_ZeroPointFive)
        USART_InitStructure.USART_StopBits = USART_StopBits_0_5;//0.5个停止位
    else if (((XSerialPortBase*)serial)->m_stopBits == SP_ST_OnePointFive)
        USART_InitStructure.USART_StopBits = USART_StopBits_1_5;//1.5个停止位
    else if (((XSerialPortBase*)serial)->m_stopBits == SP_ST_Two)
        USART_InitStructure.USART_StopBits = USART_StopBits_2;//2个停止位
    //奇偶校验位
    if (((XSerialPortBase*)serial)->m_parity == SP_PAR_NONE)
    {
        USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
        USART_ITConfig(usart[portIndex].USARTX, USART_IT_PE, DISABLE);//关闭奇偶校验错中断
    }
    else if (((XSerialPortBase*)serial)->m_parity == SP_PAR_ODD)
    {
        USART_InitStructure.USART_Parity = USART_Parity_Odd;//奇校验位
        USART_ITConfig(usart[portIndex].USARTX, USART_IT_PE, ENABLE);//使能奇偶校验错中断
    }
    else if (((XSerialPortBase*)serial)->m_parity == SP_PAR_EVEN)
    {
        USART_InitStructure.USART_Parity = USART_Parity_Even;//偶校验位
        USART_ITConfig(usart[portIndex].USARTX, USART_IT_PE, ENABLE);//使能奇偶校验错中断
    }
    //硬件数据流控制
    if (((XSerialPortBase*)serial)->m_flowControl == SP_FC_None)
        USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
    else if (((XSerialPortBase*)serial)->m_flowControl == SP_FC_Hardware)
        USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_RTS_CTS;//硬件流控制（RTS/CTS）
    //收发模式
    USART_InitStructure.USART_Mode = ((mode & XIODeviceBase_ReadOnly) ? USART_Mode_Rx : 0) | ((mode & XIODeviceBase_WriteOnly) ? USART_Mode_Tx : 0);	//收发模式
    USART_Init(usart[portIndex].USARTX, &USART_InitStructure); //初始化串口

    USART_Cmd(usart[portIndex].USARTX, ENABLE);  //使能串口

    serial->USARTX = usart[portIndex].USARTX;
    ((XIODeviceBase*)serial)->m_mode = mode;
    openUsart[portIndex] = serial;
    VXSerialPort_NVIC_Init(serial);
    return true;
}
size_t VXIODevice_write(XSerialPort* serial, const char* data, size_t maxSize)
{
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_mode & XIODeviceBase_WriteOnly == 0)
        return 0;
    if (io->m_writeBuffer == NULL)
    {//没有写入缓冲区
        while (USART_GetFlagStatus(serial->USARTX, USART_FLAG_TC) == RESET);
        for (size_t i = 0; i < maxSize; i++)
        {
            USART_SendData(serial->USARTX, data[i]);
            while (USART_GetFlagStatus(serial->USARTX, USART_FLAG_TC) == RESET);
        }
        return maxSize;
    }
    //调用父类方法初始化缓冲区
    return XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_Write, size_t(*)(XSerialPort*, const char*, size_t))(serial, data, maxSize);
}
size_t VXIODevice_writeFull(XSerialPort* serial)
{
    XIODeviceBase* io = serial;
    if (io->m_writeBuffer == NULL)
        return 0;
    char c;
    size_t count = 0;
    while (USART_GetFlagStatus(serial->USARTX, USART_FLAG_TC) == RESET);
    while (XQueueBase_receive_base(io->m_writeBuffer, &c))
    {
        USART_SendData(serial->USARTX, c);
        while (USART_GetFlagStatus(serial->USARTX, USART_FLAG_TC) == RESET);
    }

    return count;
}
size_t VXIODevice_read(XSerialPort* serial, char* data, size_t maxSize)
{
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_mode & XIODeviceBase_ReadOnly == 0)
        return 0;
    if (io->m_readBuffer == NULL)
    {//没有输入缓冲区
        if (USART_GetFlagStatus(serial->USARTX, USART_FLAG_RXNE) == SET)
        {
            *data = (char)USART_ReceiveData(serial->USARTX);
            return 1;
        }
        return 0;
    }
    //调用父类方法初始化缓冲区
    return XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_Read, size_t(*)(XSerialPort*, char*, size_t))(serial, data, maxSize);
}
void VXIODevice_close(XSerialPort* serial)
{
    if (!XSerialPort_isOpen(serial))
        return true;//已经关闭了
    XIODeviceBase_writeFull_base(serial);
    USART_ITConfig(serial->USARTX, USART_IT_RXNE, DISABLE);//关闭接收相关中断
    USART_Cmd(serial->USARTX, DISABLE);  //关闭串口
    serial->USARTX = NULL;
    openUsart[serial->m_parent.m_portNum - 1] = NULL;
    ((XIODeviceBase*)serial)->m_mode = XIODeviceBase_NotOpen;
}
void USARTCallback(XSerialPort* serial)
{
    if (USART_GetITStatus(serial->USARTX, USART_IT_RXNE) != RESET)
    {
        uint8_t r = USART_ReceiveData(serial->USARTX);
        XQueueBase_push_base(((XIODeviceBase*)openUsart[serial->m_parent.m_portNum - 1])->m_readBuffer, &r); \
    }
}
void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count)
{
    //调用父类方法初始化缓冲区
    XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_SetReadBuffer, void(*)(XSerialPort*, size_t))(serial, count);
    VXSerialPort_NVIC_Init(serial);
}
#endif
#endif

