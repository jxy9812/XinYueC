#ifndef XEPOLL_H
#define XEPOLL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

    // 事件类型定义（与epoll保持兼容）
#define XEPOLLIN      0x001          // 可读事件
#define XEPOLLPRI     0x002          // 高优先级数据可读
#define XEPOLLOUT     0x004          // 可写事件
#define XEPOLLERR     0x008          // 错误事件（自动触发）
#define XEPOLLHUP     0x010          // 挂起事件（自动触发）
#define XEPOLLRDHUP   0x2000         // 读端关闭事件
#define XEPOLLONESHOT 0x40000000     // 一次性事件
//#define EPOLLET       0x80000000     // 边缘触发模式

// 操作类型
#define XEPOLL_CTL_ADD 1             // 添加事件
#define XEPOLL_CTL_MOD 2             // 修改事件
#define XEPOLL_CTL_DEL 3             // 删除事件

// 事件结构体
typedef struct XEpollEvent 
{
    uint32_t events;  // 事件掩码
    int fd;           // 文件描述符
    void* data;       // 用户数据
} XEpollEvent;

// 前向声明
typedef struct XEpoll XEpoll;

// 获取结构体大小
size_t XEpoll_getTypeSize();

/**
 * @brief 初始化epoll对象（栈分配）
 * @param epoll XEpoll对象指针
 * @param size 初始事件容量
 * @return 成功返回true
 */
bool XEpoll_init(XEpoll* epoll, int size);

/**
 * @brief 创建epoll对象（堆分配）
 * @param size 初始事件容量
 * @return 成功返回XEpoll指针，失败返回NULL
 */
XEpoll* XEpoll_create(int size);

/**
 * @brief 销毁epoll对象（栈分配）
 * @param epoll XEpoll对象指针
 */
void XEpoll_deinit(XEpoll* epoll);

/**
 * @brief 销毁并释放epoll对象（堆分配）
 * @param epoll XEpoll对象指针
 */
void XEpoll_delete(XEpoll* epoll);

/**
 * @brief 控制epoll事件
 * @param epoll XEpoll对象指针
 * @param op 操作类型（XEPOLL_CTL_ADD/MOD/DEL）
 * @param fd 目标文件描述符
 * @param event 事件结构
 * @return 成功返回0，失败返回-1
 */
int XEpoll_ctl(XEpoll* epoll, int op, int fd, XEpollEvent* event);

/**
 * @brief 等待事件发生
 * @param epoll XEpoll对象指针
 * @param events 输出事件数组
 * @param maxevents 最大事件数
 * @param timeout 超时时间（毫秒，-1表示无限等待，0表示不阻塞）
 * @return 成功返回事件数，失败返回-1
 */
int XEpoll_wait(XEpoll* epoll, XEpollEvent* events, int maxevents, int timeout);

#ifdef __cplusplus
}
#endif

#endif // XEPOLL_H
