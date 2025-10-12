#ifdef _WIN32
#include "XEpoll.h"
#include "XMemory.h"
#include "XHashMap.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <errno.h>

#pragma comment(lib, "ws2_32.lib")

// 事件表项
typedef struct XEpollEventEntry {
    SOCKET fd;
    XEpollEvent event;
    bool in_use;
} XEpollEventEntry;

// XEpoll结构体定义（平衡优化版）
struct XEpoll {
    XEpollEventEntry* events;       // 事件表
    int max_size;                   // 最大容量
    int count;                      // 当前事件数（已使用数量）

    // 动态索引数组：存储已使用的索引（替代链表，减少内存占用）
    int* used_indices;              // 已使用索引数组
    int used_capacity;              // 数组容量（预分配，避免频繁扩容）

    WSAPOLLFD* pollfds;             // WSAPoll数组
    XHashMap* fdToIndexMap;         // fd到索引的映射（O(1)查找）
    int* free_indices;              // 空闲索引数组（替代链表，连续内存）
    int free_top;                   // 空闲索引栈顶（模拟栈操作，O(1)存取）
};

// 哈希表比较和哈希函数
static int XCompare_int(const void* a, const void* b) {
    const int* ia = (const int*)a;
    const int* ib = (const int*)b;
    return (*ia - *ib);
}

static uint32_t XHash_int(const void* key) {
    const int* k = (const int*)key;
    return (uint32_t)(*k ^ (*k >> 16));
}

// 动态数组扩容（仅在必要时扩容，减少内存浪费）
static bool expand_used_indices(XEpoll* epoll) {
    if (!epoll) return false;

    // 扩容策略：每次增加当前容量的50%（平衡内存与扩容次数）
    int new_capacity = epoll->used_capacity * 3 / 2;
    if (new_capacity <= epoll->used_capacity) new_capacity = epoll->used_capacity + 16;

    int* new_indices = (int*)XMemory_realloc(
        epoll->used_indices, new_capacity * sizeof(int)
    );
    if (!new_indices) return false;

    epoll->used_indices = new_indices;
    epoll->used_capacity = new_capacity;
    return true;
}

// 从已使用数组中移除指定索引（保持数组紧凑）
static void remove_used_index(XEpoll* epoll, int index) {
    if (!epoll || epoll->count == 0) return;

    // 查找索引位置
    int pos = -1;
    for (int i = 0; i < epoll->count; i++) {
        if (epoll->used_indices[i] == index) {
            pos = i;
            break;
        }
    }

    if (pos == -1) return;

    // 用最后一个元素覆盖当前位置，减少移动开销（无序数组，不影响遍历）
    epoll->used_indices[pos] = epoll->used_indices[epoll->count - 1];
    epoll->count--;  // 直接减少计数，无需清空最后一个元素
}

size_t XEpoll_getTypeSize() {
    return sizeof(struct XEpoll);
}

bool XEpoll_init(XEpoll* epoll, int size) {
    if (!epoll || size <= 0) return false;

    epoll->max_size = size;
    epoll->count = 0;  // 当前已使用事件数

    // 初始化已使用索引数组（初始容量为size的1/4，减少初始内存占用）
    epoll->used_capacity = (size + 3) / 4;  // 向上取整
    epoll->used_indices = (int*)XMemory_malloc(epoll->used_capacity * sizeof(int));
    if (!epoll->used_indices) return false;

    // 初始化事件表和WSAPoll数组
    epoll->events = (XEpollEventEntry*)XMemory_calloc(size, sizeof(XEpollEventEntry));
    epoll->pollfds = XMemory_calloc(size, sizeof(WSAPOLLFD));
    if (!epoll->events || !epoll->pollfds) {
        XMemory_free(epoll->used_indices);
        XMemory_free(epoll->events);
        XMemory_free(epoll->pollfds);
        return false;
    }

    // 初始化fd映射表
    epoll->fdToIndexMap = XHashMap_create(
        sizeof(int), sizeof(int),
        XCompare_int, XHash_int
    );
    if (!epoll->fdToIndexMap) {
        XMemory_free(epoll->used_indices);
        XMemory_free(epoll->events);
        XMemory_free(epoll->pollfds);
        return false;
    }

    // 初始化空闲索引栈（用数组模拟栈，O(1)存取）
    epoll->free_indices = (int*)XMemory_malloc(size * sizeof(int));
    epoll->free_top = 0;
    if (!epoll->free_indices) {
        XHashMap_delete_base(epoll->fdToIndexMap);
        XMemory_free(epoll->used_indices);
        XMemory_free(epoll->events);
        XMemory_free(epoll->pollfds);
        return false;
    }
    // 填充空闲索引（0 ~ size-1）
    for (int i = 0; i < size; i++) {
        epoll->free_indices[epoll->free_top++] = i;
    }

    return true;
}

XEpoll* XEpoll_create(int size) {
    XEpoll* epoll = (XEpoll*)XMemory_malloc(sizeof(struct XEpoll));
    if (epoll && !XEpoll_init(epoll, size)) {
        XMemory_free(epoll);
        return NULL;
    }
    return epoll;
}

void XEpoll_deinit(XEpoll* epoll) {
    if (epoll) {
        XMemory_free(epoll->events);
        XMemory_free(epoll->pollfds);
        XMemory_free(epoll->used_indices);  // 释放已使用索引数组
        XMemory_free(epoll->free_indices);  // 释放空闲索引数组

        if (epoll->fdToIndexMap) {
            XHashMap_delete_base(epoll->fdToIndexMap);
        }

        epoll->events = NULL;
        epoll->pollfds = NULL;
        epoll->used_indices = NULL;
        epoll->free_indices = NULL;
        epoll->fdToIndexMap = NULL;
        epoll->max_size = 0;
        epoll->count = 0;
        epoll->used_capacity = 0;
        epoll->free_top = 0;
    }
}

void XEpoll_delete(XEpoll* epoll) {
    if (epoll) {
        XEpoll_deinit(epoll);
        XMemory_free(epoll);
    }
}

int XEpoll_ctl(XEpoll* epoll, int op, int fd, XEpollEvent* event) {
    if (!epoll || fd < 0 || !event) {
        errno = EINVAL;
        return -1;
    }

    SOCKET s = (SOCKET)fd;
    int index = -1;

    // 从哈希表获取索引（O(1)操作）
    if (op != XEPOLL_CTL_ADD) {
        int* pIndex = (int*)XHashMap_value_base(epoll->fdToIndexMap, &fd);
        if (pIndex) index = *pIndex;
    }

    switch (op) {
    case XEPOLL_CTL_ADD:
        if (index != -1) {  // 已存在
            errno = EEXIST;
            return -1;
        }

        // 从空闲栈获取索引（O(1)操作）
        if (epoll->free_top <= 0) {
            errno = ENOMEM;
            return -1;
        }
        index = epoll->free_indices[--epoll->free_top];

        // 检查已使用数组是否需要扩容
        if (epoll->count >= epoll->used_capacity && !expand_used_indices(epoll)) {
            epoll->free_indices[epoll->free_top++] = index;  // 回退空闲索引
            errno = ENOMEM;
            return -1;
        }

        // 更新事件表
        epoll->events[index].fd = s;
        epoll->events[index].event = *event;
        epoll->events[index].in_use = true;

        // 记录已使用索引
        epoll->used_indices[epoll->count++] = index;
        XHashMap_insert_base(epoll->fdToIndexMap, &fd, &index);
        break;

    case XEPOLL_CTL_MOD:
        if (index == -1) {  // 不存在
            errno = ENOENT;
            return -1;
        }
        epoll->events[index].event = *event;
        break;

    case XEPOLL_CTL_DEL:
        if (index == -1) {  // 不存在
            errno = ENOENT;
            return -1;
        }
        epoll->events[index].in_use = false;

        // 回收索引到空闲栈
        epoll->free_indices[epoll->free_top++] = index;
        XHashMap_remove_base(epoll->fdToIndexMap, &fd);

        // 从已使用数组移除
        remove_used_index(epoll, index);
        break;

    default:
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int XEpoll_wait(XEpoll* epoll, XEpollEvent* events, int maxevents, int timeout) {
    if (!epoll || !events || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    // 填充WSAPoll数组：仅遍历已使用的索引（count次，而非max_size次）
    int poll_count = 0;
    for (int i = 0; i < epoll->count && poll_count < maxevents; i++) {
        int idx = epoll->used_indices[i];
        if (epoll->events[idx].in_use) {  // 双重校验（防止并发删除）
            epoll->pollfds[poll_count].fd = epoll->events[idx].fd;
            epoll->pollfds[poll_count].events = 0;

            // 转换事件类型
            if (epoll->events[idx].event.events & XEPOLLIN) {
                epoll->pollfds[poll_count].events |= POLLIN;
            }
            if (epoll->events[idx].event.events & XEPOLLOUT) {
                epoll->pollfds[poll_count].events |= POLLOUT;
            }
            if (epoll->events[idx].event.events & XEPOLLPRI) {
                epoll->pollfds[poll_count].events |= POLLPRI;
            }

            poll_count++;
        }
    }

    // 无事件需要监测时，直接返回0（避免WSAPoll调用）
    if (poll_count == 0) {
        return 0;
    }

    // 等待事件
    int ret = WSAPoll(epoll->pollfds, poll_count, timeout);
    if (ret <= 0) {
        return ret;
    }

    // 转换结果
    int event_count = 0;
    for (int i = 0; i < poll_count && event_count < maxevents; i++) {
        if (epoll->pollfds[i].revents != 0) {
            int fd = epoll->pollfds[i].fd;
            int* pIndex = (int*)XHashMap_value_base(epoll->fdToIndexMap, &fd);
            if (!pIndex) continue;
            int idx = *pIndex;

            events[event_count] = epoll->events[idx].event;
            events[event_count].fd = fd;

            // 转换返回事件类型
            events[event_count].events = 0;
            if (epoll->pollfds[i].revents & POLLIN)  events[event_count].events |= XEPOLLIN;
            if (epoll->pollfds[i].revents & POLLOUT) events[event_count].events |= XEPOLLOUT;
            if (epoll->pollfds[i].revents & POLLPRI) events[event_count].events |= XEPOLLPRI;
            if (epoll->pollfds[i].revents & POLLERR) events[event_count].events |= XEPOLLERR;
            if (epoll->pollfds[i].revents & POLLHUP) events[event_count].events |= XEPOLLHUP;

            event_count++;
        }
    }

    return event_count;
}

#endif
