#include "XATComm.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XVariantList.h"
#include <string.h>

static void VXATComm_processResponse(XATComm* comm);
static void VXATComm_timerEvent(XObject* self, XEventTimer* event);
static void VXATComm_deinit(XATComm* comm);

// readyRead 信号回调 → 通过虚函数表分发到子类实现
static void VXATComm_readyReadSlot(XATComm* comm)
{
    if (!comm) return;
    XClassGetVirtualFunc(comm, EXATComm_ProcessResponse, void(*)(XATComm*))(comm);
}

// ========== 虚函数表 ==========
XVtable* XATComm_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XATComm))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        VXATComm_processResponse
    };
    //追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载 ProcessResponse 虚函数（默认实现）
    //XVTABLE_OVERLOAD_DEFAULT(EXATComm_ProcessResponse, VXATComm_processResponse);
    // 重载 TimerEvent
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXATComm_timerEvent);
    // 重载 Deinit
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXATComm_deinit);

#if SHOWCONTAINERSIZE
    printf("XATComm size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// ========== 构造与析构 ==========

void XATComm_init(XATComm* comm, XIODevice* io)
{
    if (ISNULL(comm, "comm is NULL")) return;

    memset(((XObject*)comm) + 1, 0, sizeof(XATComm) - sizeof(XObject));
    XObject_init(&comm->m_base);
    XClassGetVtable(comm) = XATComm_class_init();

    comm->m_io = io;
    comm->m_timeoutId = XFD_INVALID;
    comm->m_operationResult = 0;
    comm->m_currentOp = 0;
    comm->m_responseBuffer = XByteArray_create();

    // 绑定 readyRead，通过虚函数表分发到子类重载的 ProcessResponse
    if (comm->m_io) {
        XObject_connect_1(comm->m_io,
            XIODevice_readyRead_signal,
            comm,
            VXATComm_readyReadSlot, XConnectionType_Auto);
    }
}

XATComm* XATComm_create(XIODevice* io)
{
    XATComm* comm = XMalloc_System(sizeof(XATComm));
    if (ISNULL(comm, "malloc failed")) return NULL;
    XATComm_init(comm, io);
    Set_Class_MemoryFree(comm, XFree_System);
    return comm;
}

static void VXATComm_deinit(XATComm* comm)
{
    if (!comm) return;

    if (comm->m_timeoutId != XFD_INVALID) {
        XObject_killTimer(&comm->m_base, comm->m_timeoutId);
        comm->m_timeoutId = XFD_INVALID;
    }
    if (comm->m_responseBuffer) {
        XByteArray_delete_base(comm->m_responseBuffer);
        comm->m_responseBuffer = NULL;
    }

    XClass_Deinit_Parent(XObject, comm);
}

// ========== AT 指令发送 ==========

bool XATComm_sendCommand(XATComm* comm, const char* cmd, int opType, int msecs)
{
    if (ISNULL(comm, "comm is NULL")) return false;

    // 停止当前超时定时器
    if (comm->m_timeoutId != XFD_INVALID) {
        XObject_killTimer(&comm->m_base, comm->m_timeoutId);
        comm->m_timeoutId = XFD_INVALID;
    }

    comm->m_currentOp = opType;
    XByteArray_clear_base(comm->m_responseBuffer);
    comm->m_operationResult = 0;

    if (cmd) {
        char atCmd[256];
        snprintf(atCmd, sizeof(atCmd), "%s\r\n", cmd);
        size_t sent = XIODevice_write_1(comm->m_io, atCmd, strlen(atCmd));
        if (sent != strlen(atCmd)) {
            XDEBUG_PRINTF("XATComm: send failed: %s", cmd);
            return false;
        }
    }

    if (msecs > 0) {
        comm->m_timeoutId = XObject_startTimer_ms(&comm->m_base, msecs, XTimerType_CoarseTimer);
        uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + msecs;
        while (XDateTime_currentMSecsSinceEpoch() < deadline) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if (comm->m_currentOp == 0/*||comm->m_operationResult*/)
                break;
        }
        if (comm->m_timeoutId != XFD_INVALID) {
            XObject_killTimer(&comm->m_base, comm->m_timeoutId);
            comm->m_timeoutId = XFD_INVALID;
        }
    }
    else if (msecs == -1) {
        while (!comm->m_operationResult) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if (comm->m_currentOp == 0) break;
        }
    }

    if (msecs > 0 || msecs == -1)
        return comm->m_operationResult != 0;
    return true;
}

// ========== 响应处理（虚函数默认实现，子类可重载）==========

/**
 * @brief ProcessResponse 虚函数默认实现
 * @details 从底层 m_io 读取数据到 m_responseBuffer，emit response/ok/error 信号。
 *          子类可重载此函数添加自定义逻辑（如 ESP8266 的 AT 响应解析），
 *          但应调用父类实现以维持基础功能。
 */
void VXATComm_processResponse(XATComm* comm)
{
    if (ISNULL(comm, "comm is NULL") || ISNULL(comm->m_io, "m_io is NULL")) return;

    size_t available = XIODevice_bytesAvailable_base(comm->m_io);
    if (available == 0) return;

    size_t oldSize = XByteArray_size_base(comm->m_responseBuffer);
    XByteArray_resize_base(comm->m_responseBuffer, oldSize + available);
    char* buf = (char*)XByteArray_data(comm->m_responseBuffer);

    available = XIODevice_read_1(comm->m_io, buf + oldSize, available);
    if (available == 0) return;
    XByteArray_resize_base(comm->m_responseBuffer, oldSize + available);

    XPrintf("\n||XATComm<<%s>>||\n", buf + oldSize);

    const char* data = (const char*)XByteArray_data(comm->m_responseBuffer);

    // 发射 response 信号
    XATComm_response_signal(comm, data);

    // 检查 OK / ERROR
    if (data && strstr(data, "OK")) {
        comm->m_operationResult = 1;
        XATComm_ok_signal(comm);
    }
    else if (data && strstr(data, "ERROR")) {
        XATComm_error_signal(comm, data);
    }
}

// ========== 定时器事件（超时处理） ==========

static void VXATComm_timerEvent(XObject* self, XEventTimer* event)
{
    (void)event;
    XATComm* comm = (XATComm*)self;
    if (!comm) return;

    comm->m_operationResult = 0;
    int op = comm->m_currentOp;
    comm->m_currentOp = 0;
    XATComm_timeout_signal(comm, op);
}

// ========== 信号实现 ==========

void* XATComm_response_signal(XATComm* comm, const char* data)
{
    char* data_ptr = (char*)data;
    XVarList* list = XVarList_Create(XVar(char*, data_ptr));
    XEmitSignal(comm, XATComm_response_signal, list, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XATComm_ok_signal(XATComm* comm)
{
    XEmitSignal(comm, XATComm_ok_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XATComm_error_signal(XATComm* comm, const char* errorMsg)
{
    char* msg_ptr = (char*)errorMsg;
    XVarList* list = XVarList_Create(XVar(char*, msg_ptr));
    XEmitSignal(comm, XATComm_error_signal, list, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XATComm_timeout_signal(XATComm* comm, int opType)
{
    XVarList* list = XVarList_Create(XVar(int, opType));
    XEmitSignal(comm, XATComm_timeout_signal, list, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
