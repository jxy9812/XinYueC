#include "netif/ethernetif.h" 
#include "lan8720.h"  
#include "netif/etharp.h" 
#include "lwip/sys.h"
#include "string.h"  
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"


/* 定义一个信号量 */
xSemaphoreHandle s_xSemaphore = NULL;


/* Forward declarations. */
void  ethernetif_input(void *pParams);


//由ethernetif_init()调用用于初始化硬件
//netif:网卡结构体指针 
//返回值:ERR_OK,正常
//       其他,失败
static void low_level_init(struct netif *netif)
{
	#ifdef CHECKSUM_BY_HARDWARE
		int i; 
	#endif 
		netif->hwaddr_len = ETHARP_HWADDR_LEN; //设置MAC地址长度,为6个字节
		// // //初始化MAC地址,设置什么地址由用户自己设置,但是不能与网络中其他设备MAC地址重复
		// netif->hwaddr[0]=MAC_ADDR0; 
		// netif->hwaddr[1]=MAC_ADDR1;
		// netif->hwaddr[2]=MAC_ADDR2;
		// netif->hwaddr[3]=MAC_ADDR3;
		// netif->hwaddr[4]=MAC_ADDR4;
		// netif->hwaddr[5]=MAC_ADDR5;
		
		  netif->mtu=1500; //最大允许传输单元,允许该网卡广播和ARP功能

		netif->flags = NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_LINK_UP;
		
		ETH_MACAddressConfig(ETH_MAC_Address0, netif->hwaddr); //向STM32F4的MAC地址寄存器中写入MAC地址
		ETH_DMATxDescChainInit(DMATxDscrTab, Tx_Buff, ETH_TXBUFNB);
		ETH_DMARxDescChainInit(DMARxDscrTab, Rx_Buff, ETH_RXBUFNB);
	#ifdef CHECKSUM_BY_HARDWARE 	//使用硬件帧校验
		for(i=0;i<ETH_TXBUFNB;i++)	//使能TCP,UDP和ICMP的发送帧校验,TCP,UDP和ICMP的接收帧校验在DMA中配置了
		{
			ETH_DMATxDescChecksumInsertionConfig(&DMATxDscrTab[i], ETH_DMATxDesc_ChecksumTCPUDPICMPFull);
		}
	#endif
		
		/* 创建一个信号量 */
		if(s_xSemaphore == NULL)
		{
			s_xSemaphore = xSemaphoreCreateBinary();
			xSemaphoreTake( s_xSemaphore, 0);
		}
		
		/* 创建处理ETH_MAC的任务 */
		sys_thread_new("eth_thread",
									ethernetif_input,        /* 任务入口函数 */
									netif,                   /* 任务入口函数参数 */
									NETIF_IN_TASK_STACK_SIZE,/* 任务栈大小 */
									NETIF_IN_TASK_PRIORITY); /* 任务的优先级 */
		
		ETH_Start(); //开启MAC和DMA				
} 
//用于发送数据包的最底层函数(lwip通过netif->linkoutput指向该函数)
//netif:网卡结构体指针
//p:pbuf数据结构体指针
//返回值:ERR_OK,发送正常
//       ERR_MEM,发送失败
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
	u8 res;
	struct pbuf *q;
	int l = 0;
	
	u8 *buffer=(u8 *)(DMATxDescToSet->Buffer1Addr); //获取当前要发送的DMA描述符中的缓冲区地址
	
	for(q=p;q!=NULL;q=q->next) 
	{
		memcpy((u8_t*)&buffer[l], q->payload, q->len);
		l=l+q->len;
	} 
	
	res=ETH_Tx_Packet(l); //调用ETH_Tx_Packet函数发送数据
	if(res==ETH_ERROR)return ERR_MEM;//返回错误状态
	return ERR_OK;
} 

///用于接收数据包的最底层函数
//neitif:网卡结构体指针
//返回值:pbuf数据结构体指针
static struct pbuf * low_level_input(struct netif *netif)
{  
	struct pbuf *p= NULL, *q;
	u16_t len;
	int l =0;
	FrameTypeDef frame;
	u8 *buffer;
	
	frame=ETH_Rx_Packet();
	len=frame.length;//得到包大小
	buffer=(u8 *)frame.buffer;//得到包数据地址 
	
	if(len > 0)
	{
		p=pbuf_alloc(PBUF_RAW,len,PBUF_POOL);//pbufs内存池分配pbuf
	}
	
	if(p!=NULL)
	{
		/* 将接收描述符中Rx Buffer的数据拷贝到pbuf中 */
		for(q=p;q!=NULL;q=q->next)
		{
			memcpy((u8_t*)q->payload,(u8_t*)&buffer[l], q->len);
			l=l+q->len;
		}    
	}
	
	frame.descriptor->Status=ETH_DMARxDesc_OWN;//设置Rx描述符OWN位,buffer重归ETH DMA 
	if((ETH->DMASR&ETH_DMASR_RBUS)!=(u32)RESET)//当Rx Buffer不可用位(RBUS)被设置的时候,重置它.恢复传输
	{
		ETH->DMASR=ETH_DMASR_RBUS;//重置ETH DMA RBUS位 
		ETH->DMARPDR=0;//恢复DMA接收
	}
	return p;
}


void ethernetif_input(void *pParams)
{
	struct netif *netif;
	struct pbuf *p = NULL;
	netif = (struct netif*) pParams;
  LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
	
  while(1)
  {
    if (xSemaphoreTake( s_xSemaphore, portMAX_DELAY)==pdTRUE)
    {
			/* move received packet into a new pbuf */
      taskENTER_CRITICAL();
TRY_GET_NEXT_FRAME:
      p = low_level_input( netif );//调用low_level_input函数接收数据
	taskEXIT_CRITICAL();
			
      if(p != NULL)
      {
		taskENTER_CRITICAL();
				
        if (ERR_OK != netif->input( p, netif))//调用netif结构体中的input字段(一个函数)来处理数据包
        {
          LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
          pbuf_free(p);
          p = NULL;
        }
        else
        {
			xSemaphoreTake( s_xSemaphore, 0);
          goto TRY_GET_NEXT_FRAME;
        }	
		taskEXIT_CRITICAL();
      }
    }
  }
} 
//使用low_level_init()函数来初始化网络
//netif:网卡结构体指针
//返回值:ERR_OK,正常
//       其他,失败
err_t ethernetif_init(struct netif *netif)
{
	LWIP_ASSERT("netif!=NULL",(netif!=NULL));
#if LWIP_NETIF_HOSTNAME			//LWIP_NETIF_HOSTNAME 
	netif->hostname="lwip8720A";  	//初始化名称
#endif 
	netif->name[0]=IFNAME0; 	//初始化变量netif的name字段
	netif->name[1]=IFNAME1; 	//在文件外定义这里不用关心具体值
	netif->output=etharp_output;//IP层发送数据包函数
	netif->linkoutput=low_level_output;//ARP模块发送数据包函数
	low_level_init(netif); 		//底层硬件初始化函数
	return ERR_OK;
}
