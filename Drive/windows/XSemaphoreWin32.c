#ifdef _WIN32
#include "XSemaphore.h"
#include "XMemory.h"
#include <windows.h>
#include <stdio.h>

// Windows平台具体结构体定义（参考XWaitConditionWin32.c）
struct XSemaphore {
    HANDLE handle; // 平台原生信号量句柄
};

// 实现类型大小获取接口
size_t XSemaphore_typeSize() {
    return sizeof(struct XSemaphore);
}

void XSemaphore_init(XSemaphore* sem, int32_t initial, int32_t maximum) {
    if (!sem || initial < 0 || maximum <= 0 || initial > maximum) return;
    sem->handle = CreateSemaphore(
        NULL,               // 安全属性
        initial,            // 初始计数
        maximum,            // 最大计数
        NULL                // 匿名信号量
    );
    if (sem->handle == NULL) {
        printf("XSemaphore create failed: %d\n", GetLastError());
    }
}

void XSemaphore_deinit(XSemaphore* sem) {
    if (sem && sem->handle) {
        CloseHandle(sem->handle);
        sem->handle = NULL;
    }
}

XSemaphore* XSemaphore_create(int32_t initial, int32_t maximum) {
    XSemaphore* sem = (XSemaphore*)XMemory_malloc(sizeof(struct XSemaphore));
    if (sem) {
        XSemaphore_init(sem, initial, maximum);
        if (sem->handle == NULL) {
            XMemory_free(sem);
            return NULL;
        }
    }
    return sem;
}

void XSemaphore_delete(XSemaphore* sem) {
    if (sem) {
        XSemaphore_deinit(sem);
        XMemory_free(sem);
    }
}

void XSemaphore_acquire(XSemaphore* sem, int32_t n) {
    if (!sem || sem->handle == NULL || n <= 0) return;
    for (int32_t i = 0; i < n; i++) {
        WaitForSingleObject(sem->handle, INFINITE);
    }
}

void XSemaphore_release(XSemaphore* sem, int32_t n) {
    if (!sem || sem->handle == NULL || n <= 0) return;
    ReleaseSemaphore(sem->handle, n, NULL);
}

bool XSemaphore_tryAcquire(XSemaphore* sem, int32_t n) {
    if (!sem || sem->handle == NULL || n <= 0) return false;
    for (int32_t i = 0; i < n; i++) {
        if (WaitForSingleObject(sem->handle, 0) != WAIT_OBJECT_0) {
            // 部分获取失败时回滚
            if (i > 0) {
                ReleaseSemaphore(sem->handle, i, NULL);
            }
            return false;
        }
    }
    return true;
}

bool XSemaphore_tryAcquireTimeout(XSemaphore* sem, int32_t n, uint32_t timeout) {
    if (!sem || sem->handle == NULL || n <= 0) return false;
    uint32_t start = GetTickCount();
    for (int32_t i = 0; i < n; i++) {
        uint32_t elapsed = GetTickCount() - start;
        uint32_t remaining = (elapsed >= timeout) ? 0 : (timeout - elapsed);
        DWORD result = WaitForSingleObject(sem->handle, remaining);
        if (result != WAIT_OBJECT_0) {
            // 部分获取失败时回滚
            if (i > 0) {
                ReleaseSemaphore(sem->handle, i, NULL);
            }
            return false;
        }
    }
    return true;
}

int32_t XSemaphore_available(XSemaphore* sem) {
    if (!sem || sem->handle == NULL) return -1;
    LONG prevCount;
    if (ReleaseSemaphore(sem->handle, 0, &prevCount)) {
        return prevCount;
    }
    return -1;
}

#endif