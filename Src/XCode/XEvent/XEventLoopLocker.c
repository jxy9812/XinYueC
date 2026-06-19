#include "XEventLoopLocker.h"
//#include "XApplication.h"
#include <stdlib.h>
#include <stdint.h>

// 内部类型编码（模仿 Qt 的指针低位标记技巧）
typedef enum {
    XEventLoopLocker_Type_EventLoop = 0,  // 指向 XEventLoop*
    XEventLoopLocker_Type_Thread = 1,  // 指向 XThread*
    XEventLoopLocker_Type_Application = 2,  // 全局应用（无指针）
} XEventLoopLocker_Type;

// 使用最低 2 位存储类型，其余位存储指针（要求指针对齐到 4 字节）
#define XEL_TYPE_MASK       ((uintptr_t)0x3)
#define XEL_PTR_MASK        (~XEL_TYPE_MASK)

struct XEventLoopLocker {
    uintptr_t data; // 编码后的指针 + 类型
};

// 辅助函数：从 data 解码类型
static inline XEventLoopLocker_Type get_type(uintptr_t data) {
    return (XEventLoopLocker_Type)(data & XEL_TYPE_MASK);
}

// 辅助函数：从 data 解码指针（仅对 EventLoop/Thread 有效）
static inline void* get_pointer(uintptr_t data) {
    return (void*)(data & XEL_PTR_MASK);
}

// 辅助函数：加锁（根据类型）
static void acquire_lock(XEventLoopLocker_Type type, void* ptr) {
    switch (type) {
    case XEventLoopLocker_Type_EventLoop: {
        XEventLoop* loop = (XEventLoop*)ptr;
        if (loop) {
            //XEventLoop_locker_acquire(loop);
        }
        break;
    }
    case XEventLoopLocker_Type_Thread: {
        XThread* thread = (XThread*)ptr;
        if (thread) {
            //XEventLoop* loop = XThread_eventLoop(thread); // 需要你实现此函数
            //if (loop) {
            //    XEventLoop_locker_acquire(loop);
            //}
        }
        break;
    }
    case XEventLoopLocker_Type_Application: {
       /* XApplication* app = XApplication_instance();
        if (app && app->mainLoop) {
            XEventLoop_locker_acquire(app->mainLoop);
        }*/
        break;
    }
    }
}

// 辅助函数：解锁（根据类型）
static void release_lock(XEventLoopLocker_Type type, void* ptr) {
    switch (type) {
    case XEventLoopLocker_Type_EventLoop: {
      /*  XEventLoop* loop = (XEventLoop*)ptr;
        if (loop) {
            XEventLoop_locker_release(loop);
        }*/
        break;
    }
    case XEventLoopLocker_Type_Thread: {
        XThread* thread = (XThread*)ptr;
        /*if (thread) {
            XEventLoop* loop = XThread_eventLoop(thread);
            if (loop) {
                XEventLoop_locker_release(loop);
            }
        }*/
        break;
    }
    case XEventLoopLocker_Type_Application: {
        //XApplication* app = XApplication_instance();
       /* if (app && app->mainLoop) {
            XEventLoop_locker_release(app->mainLoop);
        }*/
        break;
    }
    }
}

// --- 构造函数 ---
XEventLoopLocker* XEventLoopLocker_create(void) {
    XEventLoopLocker* self = (XEventLoopLocker*)malloc(sizeof(XEventLoopLocker));
    if (!self) return NULL;
    self->data = (uintptr_t)XEventLoopLocker_Type_Application; // 无指针，仅类型
    acquire_lock(XEventLoopLocker_Type_Application, NULL);
    return self;
}

XEventLoopLocker* XEventLoopLocker_createForLoop(XEventLoop* loop) {
    if (((uintptr_t)loop & XEL_TYPE_MASK) != 0) {
        // 指针未对齐！这在正常分配下不会发生，但需防御
        return NULL;
    }
    XEventLoopLocker* self = (XEventLoopLocker*)malloc(sizeof(XEventLoopLocker));
    if (!self) return NULL;
    self->data = ((uintptr_t)loop) | (uintptr_t)XEventLoopLocker_Type_EventLoop;
    acquire_lock(XEventLoopLocker_Type_EventLoop, loop);
    return self;
}

XEventLoopLocker* XEventLoopLocker_createForThread(XThread* thread) {
    if (((uintptr_t)thread & XEL_TYPE_MASK) != 0) {
        return NULL;
    }
    XEventLoopLocker* self = (XEventLoopLocker*)malloc(sizeof(XEventLoopLocker));
    if (!self) return NULL;
    self->data = ((uintptr_t)thread) | (uintptr_t)XEventLoopLocker_Type_Thread;
    acquire_lock(XEventLoopLocker_Type_Thread, thread);
    return self;
}

// --- 析构函数 ---
void XEventLoopLocker_destroy(XEventLoopLocker* self) {
    if (!self) return;
    XEventLoopLocker_Type type = get_type(self->data);
    void* ptr = get_pointer(self->data);
    release_lock(type, ptr);
    free(self);
}

// --- 移动语义（模拟 C++ move）---
XEventLoopLocker* XEventLoopLocker_move(XEventLoopLocker* other) {
    if (!other) return NULL;
    XEventLoopLocker* self = (XEventLoopLocker*)malloc(sizeof(XEventLoopLocker));
    if (!self) return NULL;
    self->data = other->data;
    // 将 other 置为空（避免重复释放）
    //other->data = (uintptr_t)XEventLoopLocker_Type_Application; // safe empty state
    return self;
}

// --- 交换 ---
void XEventLoopLocker_swap(XEventLoopLocker* a, XEventLoopLocker* b) {
    if (!a || !b) return;
    uintptr_t tmp = a->data;
    a->data = b->data;
    b->data = tmp;
}