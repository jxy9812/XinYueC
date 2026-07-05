#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__

#include "lwip/err.h"
#include "lwip/netif.h"
//网络接口输入任务 (netif input task) 的栈大小，单位为字节
#define NETIF_IN_TASK_STACK_SIZE    ( 1024 )
//网络接口输入任务的优先级
#define NETIF_IN_TASK_PRIORITY      ( 2 )
 
//网卡名
#define IFNAME0 'e'
#define IFNAME1 'n'
 

err_t ethernetif_init(struct netif *netif);

#endif
