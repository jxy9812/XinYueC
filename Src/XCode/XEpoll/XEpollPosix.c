#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XEpoll.h"
#include "XMemory.h"
#include "XHashMap.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

// 事件表项：存储完整的XEpollEvent
typedef struct {
    XEpollEvent event;  // 完整存储用户传入的事件（含fd、events、data）
    bool in_use;        // 标记是否被使用
} XEpollEventEntry;

// XEpoll结构体定义
struct XEpoll {
    int epoll_fd;                  // epoll文件描述符
    int max_size;                  // 最大容量
    int count;                     // 当前事件数

    XEpollEventEntry* events;      // 事件表（存储完整XEpollEvent）
    int* used_indices;             // 已使用索引数组
    int used_capacity;             // 已使用数组容量

    XHashMap* fdToIndexMap;        // fd到索引的映射（O(1)查找）
    int* free_indices;             // 空闲索引栈
    int free_top;                  // 栈顶指针
};

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

    // 分配事件表（存储完整XEpollEvent）
    epoll->events = (XEpollEventEntry*)XMemory_calloc(
        size, sizeof(XEpollEventEntry)
    );
    if (!epoll->events) return false;

    // 初始化已使用索引数组
    epoll->used_capacity = (size + 3) / 4;
    epoll->used_indices = (int*)XMemory_malloc(epoll->used_capacity * sizeof(int));
    if (!epoll->used_indices) {
        XMemory_free(epoll->events);
        return false;
    }

    // 创建epoll实例
    epoll->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll->epoll_fd == -1) {
        XMemory_free(epoll->events);
        XMemory_free(epoll->used_indices);
        return false;
    }

    // 初始化fd映射表
    epoll->fdToIndexMap = XHashMap_Create(int, int, XCompare_int);
    if (!epoll->fdToIndexMap) {
        close(epoll->epoll_fd);
        XMemory_free(epoll->events);
        XMemory_free(epoll->used_indices);
        return false;
    }

    // 初始化空闲索引栈
    epoll->free_indices = (int*)XMemory_malloc(size * sizeof(int));
    epoll->free_top = 0;
    if (!epoll->free_indices) {
        XHashMap_delete_base(epoll->fdToIndexMap);
        close(epoll->epoll_fd);
        XMemory_free(epoll->events);
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

        XHashMap_delete_base(epoll->fdToIndexMap);
        XMemory_free(epoll->events);
        XMemory_free(epoll->used_indices);
        XMemory_free(epoll->free_indices);

        epoll->fdToIndexMap = NULL;
        epoll->events = NULL;
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
    if (!epoll || epoll->epoll_fd == -1 || fd < 0 ) {
        errno = EINVAL;
        return -1;
    }

    int index = -1;
    // 从哈希表获取已有索引（修改/删除操作）
    if (op != XEPOLL_CTL_ADD) {
        int* pIndex = (int*)XHashMap_value_base(epoll->fdToIndexMap, &fd);
        if (pIndex) index = *pIndex;
    }

    struct epoll_event ep_event;
    switch (op) {
    case XEPOLL_CTL_ADD:
        if(!event)
        {
            errno = EINVAL;
            return -1;
        }
        ep_event.events = event->events;  // 传递事件类型
        if (index != -1) {  // fd已存在
            errno = EEXIST;
            return -1;
        }

        // 从空闲栈获取索引
        if (epoll->free_top <= 0) {
            errno = ENOMEM;
            return -1;
        }
        index = epoll->free_indices[--epoll->free_top];

        // 已使用数组扩容检查
        if (epoll->count >= epoll->used_capacity && !expand_used_indices(epoll)) {
            epoll->free_indices[epoll->free_top++] = index;  // 回退索引
            errno = ENOMEM;
            return -1;
        }

        // 关键：存储完整的XEpollEvent到事件表
        epoll->events[index].event = *event;  // 拷贝完整事件（含fd、data）
        epoll->events[index].event.fd = fd;   // 确保fd正确（覆盖可能的错误值）
        epoll->events[index].in_use = true;

        // 关联epoll_event到事件表项（通过指针直接访问）
        ep_event.data.ptr = &epoll->events[index].event;  // 指向存储的XEpollEvent

        // 添加到epoll实例
        if (epoll_ctl(epoll->epoll_fd, EPOLL_CTL_ADD, fd, &ep_event) == -1) {
            epoll->free_indices[epoll->free_top++] = index;
            return -1;
        }

        // 更新已使用数组和哈希表
        epoll->used_indices[epoll->count++] = index;
        XHashMap_insert_base(epoll->fdToIndexMap, &fd, &index);
        break;

    case XEPOLL_CTL_MOD:
        if(!event)
        {
            errno = EINVAL;
            return -1;
        }
        ep_event.events = event->events;  // 传递事件类型
        if (index == -1) {  // fd不存在
            errno = ENOENT;
            return -1;
        }

        // 更新事件表中的XEpollEvent
        epoll->events[index].event.events = event->events;  // 更新事件类型
        if (event->data != NULL) {
            epoll->events[index].event.data = event->data;  // 更新用户数据（可选）
        }

        // 关联到事件表项
        ep_event.data.ptr = &epoll->events[index].event;

        // 调用epoll_ctl修改
        if (epoll_ctl(epoll->epoll_fd, EPOLL_CTL_MOD, fd, &ep_event) == -1) {
            return -1;
        }
        break;

    case XEPOLL_CTL_DEL:
        if (index == -1) {  // fd不存在
            errno = ENOENT;
            return -1;
        }

        // 标记为未使用
        epoll->events[index].in_use = false;

        // 调用epoll_ctl删除（data可任意，内核仅需fd）
        if (epoll_ctl(epoll->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
            return -1;
        }

        // 回收索引
        epoll->free_indices[epoll->free_top++] = index;
        XHashMap_remove_base(epoll->fdToIndexMap, &fd);
        remove_used_index(epoll, index);
        break;

    default:
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int XEpoll_wait(XEpoll* epoll, XEpollEvent* events, int maxevents, int timeout) {
    if (!epoll || epoll->epoll_fd == -1 || !events || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    // 无事件时直接返回
    if (epoll->count == 0) {
        return 0;
    }

    // 分配临时事件数组
    struct epoll_event* ep_events = (struct epoll_event*)XMemory_malloc(
        maxevents * sizeof(struct epoll_event)
    );
    if (!ep_events) return -1;

    // 等待事件
    int ret = epoll_wait(epoll->epoll_fd, ep_events, maxevents, timeout);
    if (ret > 0) {
        for (int i = 0; i < ret; i++) {
            // 关键：通过ptr直接获取存储的完整XEpollEvent
            XEpollEvent* stored_event = (XEpollEvent*)ep_events[i].data.ptr;
            if (stored_event) {
                // 完整拷贝所有字段（fd、events、data）
                events[i].fd = stored_event->fd;
                events[i].events = 0;
                events[i].data = stored_event->data;

                // 映射事件类型（保持与内核事件一致）
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
    }

    XMemory_free(ep_events);
    return ret;
}

#endif
    