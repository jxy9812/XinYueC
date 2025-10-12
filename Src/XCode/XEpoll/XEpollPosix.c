#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XEpoll.h"
#include "XMemory.h"
#include "XHashMap.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// XEpoll结构体定义（平衡优化版）
struct XEpoll {
    int epoll_fd;                  // epoll文件描述符
    int max_size;                  // 最大容量
    int count;                     // 当前事件数

    // 动态数组存储已使用索引（减少内存占用）
    int* used_indices;             // 已使用索引数组
    int used_capacity;             // 数组容量

    XHashMap* fdToIndexMap;        // fd到索引的映射
    int* free_indices;             // 空闲索引栈（数组模拟）
    int free_top;                  // 栈顶指针
};

// 哈希表工具函数
static int XCompare_int(const void* a, const void* b) {
    const int* ia = (const int*)a;
    const int* ib = (const int*)b;
    return (*ia - *ib);
}

static uint32_t XHash_int(const void* key) {
    const int* k = (const int*)key;
    return (uint32_t)(*k ^ (*k >> 16));
}

// 已使用索引数组扩容
static bool expand_used_indices(XEpoll* epoll) {
    if (!epoll) return false;

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

// 从已使用数组移除索引
static void remove_used_index(XEpoll* epoll, int index) {
    if (!epoll || epoll->count == 0) return;

    int pos = -1;
    for (int i = 0; i < epoll->count; i++) {
        if (epoll->used_indices[i] == index) {
            pos = i;
            break;
        }
    }

    if (pos != -1) {
        epoll->used_indices[pos] = epoll->used_indices[epoll->count - 1];
        epoll->count--;
    }
}

size_t XEpoll_getTypeSize() {
    return sizeof(struct XEpoll);
}

bool XEpoll_init(XEpoll* epoll, int size) {
    if (!epoll || size <= 0) return false;

    epoll->max_size = size;
    epoll->count = 0;
    epoll->used_capacity = (size + 3) / 4;  // 初始容量为size的1/4
    epoll->used_indices = (int*)XMemory_malloc(epoll->used_capacity * sizeof(int));
    if (!epoll->used_indices) return false;

    // 创建epoll实例
    epoll->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll->epoll_fd == -1) {
        XMemory_free(epoll->used_indices);
        return false;
    }

    // 初始化fd映射表
    epoll->fdToIndexMap = XHashMap_create(
        sizeof(int), sizeof(int),
        XCompare_int, XHash_int
    );
    if (!epoll->fdToIndexMap) {
        close(epoll->epoll_fd);
        XMemory_free(epoll->used_indices);
        return false;
    }

    // 初始化空闲索引栈
    epoll->free_indices = (int*)XMemory_malloc(size * sizeof(int));
    epoll->free_top = 0;
    if (!epoll->free_indices) {
        XHashMap_delete(epoll->fdToIndexMap);
        close(epoll->epoll_fd);
        XMemory_free(epoll->used_indices);
        return false;
    }
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
        if (epoll->epoll_fd != -1) {
            close(epoll->epoll_fd);
            epoll->epoll_fd = -1;
        }

        XHashMap_delete(epoll->fdToIndexMap);
        XMemory_free(epoll->used_indices);
        XMemory_free(epoll->free_indices);

        epoll->fdToIndexMap = NULL;
        epoll->used_indices = NULL;
        epoll->free_indices = NULL;
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
    if (!epoll || epoll->epoll_fd == -1 || fd < 0 || !event) {
        errno = EINVAL;
        return -1;
    }

    int index = -1;
    if (op != XEPOLL_CTL_ADD) {
        int* pIndex = (int*)XHashMap_get(epoll->fdToIndexMap, &fd);
        if (pIndex) index = *pIndex;
    }

    struct epoll_event ep_event;
    ep_event.events = event->events;
    ep_event.data.fd = fd;  // 存储fd供wait时使用

    int ret = epoll_ctl(epoll->epoll_fd, op, fd, &ep_event);
    if (ret == 0) {
        switch (op) {
        case XEPOLL_CTL_ADD:
            if (epoll->free_top <= 0) {
                errno = ENOMEM;
                return -1;
            }
            index = epoll->free_indices[--epoll->free_top];

            if (epoll->count >= epoll->used_capacity && !expand_used_indices(epoll)) {
                epoll->free_indices[epoll->free_top++] = index;
                errno = ENOMEM;
                return -1;
            }

            epoll->used_indices[epoll->count++] = index;
            XHashMap_insert(epoll->fdToIndexMap, &fd, &index);
            break;

        case XEPOLL_CTL_DEL:
            if (index != -1) {
                epoll->free_indices[epoll->free_top++] = index;
                XHashMap_remove(epoll->fdToIndexMap, &fd);
                remove_used_index(epoll, index);
            }
            break;
        }
    }

    return ret;
}

int XEpoll_wait(XEpoll* epoll, XEpollEvent* events, int maxevents, int timeout) {
    if (!epoll || epoll->epoll_fd == -1 || !events || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    // 无事件时直接返回（避免系统调用）
    if (epoll->count == 0) {
        return 0;
    }

    struct epoll_event* ep_events = (struct epoll_event*)XMemory_malloc(
        maxevents * sizeof(struct epoll_event)
    );
    if (!ep_events) return -1;

    int ret = epoll_wait(epoll->epoll_fd, ep_events, maxevents, timeout);
    if (ret > 0) {
        for (int i = 0; i < ret; i++) {
            events[i].fd = ep_events[i].data.fd;
            events[i].events = 0;

            // 完整映射事件类型
            if (ep_events[i].events & EPOLLIN)      events[i].events |= XEPOLLIN;
            if (ep_events[i].events & EPOLLPRI)     events[i].events |= XEPOLLPRI;
            if (ep_events[i].events & EPOLLOUT)     events[i].events |= XEPOLLOUT;
            if (ep_events[i].events & EPOLLERR)     events[i].events |= XEPOLLERR;
            if (ep_events[i].events & EPOLLHUP)     events[i].events |= XEPOLLHUP;
            if (ep_events[i].events & EPOLLRDHUP)   events[i].events |= XEPOLLRDHUP;
            if (ep_events[i].events & EPOLLONESHOT) events[i].events |= XEPOLLONESHOT;
            if (ep_events[i].events & EPOLLET)      events[i].events |= EPOLLET;
        }
    }

    XMemory_free(ep_events);
    return ret;
}

#endif
