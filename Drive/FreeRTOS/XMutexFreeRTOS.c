#ifdef __FreeRTOS__
#include "XRecursiveMutex.h"
#include "XMemory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// FreeRTOS平台具体结构体定义
struct XMutex {
    SemaphoreHandle_t sem;
    XMutex_Type type;
    uint32_t recursive_count;
    TaskHandle_t owner_task;
};
size_t XMutex_geTypetSize()
{
    return sizeof(struct XMutex);
}

void XRecursiveMutex_init(XRecursiveMutex* m_mutex) 
{
    if (!m_mutex) return;

    m_mutex->type = XMutex_Recursive;
    m_mutex->recursive_count = 0;
    m_mutex->owner_task = NULL;

    if (m_mutex->type == XMutex_Recursive) {
        m_mutex->sem = xSemaphoreCreateRecursiveMutex();
    }
    else {
        m_mutex->sem = xSemaphoreCreateMutex();
    }
}

XRecursiveMutex* XRecursiveMutex_create()
{
    XMutex* m_mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (m_mutex) {
        XRecursiveMutex_init(m_mutex);
        if (!m_mutex->sem) {
            XMemory_free(m_mutex);
            return NULL;
        }
    }
    return m_mutex;
}

void XMutex_init(XMutex* m_mutex) {
    if (!m_mutex) return;

    m_mutex->type = XMutex_Normal;
    m_mutex->recursive_count = 0;
    m_mutex->owner_task = NULL;

    if (m_mutex->type == XMutex_Recursive) {
        m_mutex->sem = xSemaphoreCreateRecursiveMutex();
    }
    else {
        m_mutex->sem = xSemaphoreCreateMutex();
    }
}

void XMutex_deinit(XMutex* m_mutex) {
    if (!m_mutex || !m_mutex->sem) return;
    vSemaphoreDelete(m_mutex->sem);
    m_mutex->sem = NULL;
}

XMutex* XMutex_create() 
{
    XMutex* m_mutex = (XMutex*)XMemory_malloc(sizeof(XMutex));
    if (m_mutex) {
        XMutex_init(m_mutex);
        if (!m_mutex->sem) {
            XMemory_free(m_mutex);
            return NULL;
        }
    }
    return m_mutex;
}

void XMutex_delete(XMutex* m_mutex) {
    if (m_mutex) {
        XMutex_deinit(m_mutex);
        XMemory_free(m_mutex);
    }
}

void XMutex_lock(XMutex* m_mutex) {
    if (!m_mutex || !m_mutex->sem) return;

    if (m_mutex->type == XMutex_Recursive) {
        xSemaphoreTakeRecursive(m_mutex->sem, portMAX_DELAY);
    }
    else {
        xSemaphoreTake(m_mutex->sem, portMAX_DELAY);
    }
}

bool XMutex_tryLock(XMutex* m_mutex) {
    if (!m_mutex || !m_mutex->sem) return false;

    BaseType_t result;
    if (m_mutex->type == XMutex_Recursive) {
        result = xSemaphoreTakeRecursive(m_mutex->sem, 0);
    }
    else {
        result = xSemaphoreTake(m_mutex->sem, 0);
    }

    return result == pdTRUE;
}

bool XMutex_tryLockTimeout(XMutex* m_mutex, uint32_t timeout) {
    if (!m_mutex || !m_mutex->sem) return false;

    TickType_t ticks = pdMS_TO_TICKS(timeout);
    BaseType_t result;

    if (m_mutex->type == XMutex_Recursive) {
        result = xSemaphoreTakeRecursive(m_mutex->sem, ticks);
    }
    else {
        result = xSemaphoreTake(m_mutex->sem, ticks);
    }

    return result == pdTRUE;
}

void XMutex_unlock(XMutex* m_mutex) {
    if (!m_mutex || !m_mutex->sem) return;

    if (m_mutex->type == XMutex_Recursive) {
        xSemaphoreGiveRecursive(m_mutex->sem);
    }
    else {
        xSemaphoreGive(m_mutex->sem);
    }
}

bool XMutex_isRecursive(XMutex* m_mutex) {
    return m_mutex ? (m_mutex->type == XMutex_Recursive) : false;
}
XMutex_Type XMutex_type(XMutex* m_mutex)
{
    return m_mutex->type;
}
#endif