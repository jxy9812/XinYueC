/**
 * @file        XFtpCommand.c
 * @brief       XFtpCommand 实现
 */

#include "XFtpCommand.h"
#include "XMemory.h"
#include "XString.h"
#include "XObject.h"
#include <string.h>

#if XPROTOCOL_ON
#if XFTP_ON

static void VXFtpCommand_deinit(XFtpCommand* cmd)
{
    if (!cmd) return;
    if (cmd->m_rawCmd) {
        XClass_delete_base((XClass*)cmd->m_rawCmd);
        cmd->m_rawCmd = NULL;
    }
    if (cmd->m_rawCmds) {
        for (size_t i = 0; i < XVector_size_base(cmd->m_rawCmds); i++) {
            char* s = *(char**)XVector_at_base(cmd->m_rawCmds, i);
            if (s) XFree(s, XMEMORY_TYPE_SYSTEM);
        }
        XClass_delete_base((XClass*)cmd->m_rawCmds);
        cmd->m_rawCmds = NULL;
    }
    if (cmd->m_data) {
        XClass_delete_base((XClass*)cmd->m_data);
        cmd->m_data = NULL;
    }
    cmd->m_device = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)cmd);
}

static XVtable* s_xftpCommand_vtable = NULL;
static bool s_xftpCommand_inited = false;

XVtable* XFtpCommand_class_init(void)
{
    if (s_xftpCommand_inited && s_xftpCommand_vtable) return s_xftpCommand_vtable;
    XVtable_init(s_xftpCommand_vtable = XVtable_create());
    XVTABLE_SET_NAME(s_xftpCommand_vtable, "XFtpCommand");
    XVtable_append_vtable(s_xftpCommand_vtable, XObject_class_init());
    XVTABLE_OVERLOAD(s_xftpCommand_vtable, EXClass_Deinit, VXFtpCommand_deinit);
    s_xftpCommand_inited = true;
    return s_xftpCommand_vtable;
}

XFtpCommand* XFtpCommand_create_ex(XMemoryType memory, int id, XFtpCommand_Type cmdType)
{
    /* 必须按 XFtpCommand 大小分配，否则后续填字段会越界写堆 */
    XFtpCommand* cmd = (XFtpCommand*)XMemory_malloc(sizeof(XFtpCommand), memory);
    if (!cmd) return NULL;
    memset(cmd, 0, sizeof(XFtpCommand));
    // 初始化基类 XObject
    XObject_init((XObject*)cmd);
    XClassGetVtable(cmd) = XFtpCommand_class_init();
    Set_Class_Memory(cmd, memory); Set_Class_IsHeap(cmd, true);
    // 设置 XFtpCommand 自身字段
    cmd->m_id = id;
    cmd->m_command = cmdType;
    cmd->m_rawCmd = XString_create();
    cmd->m_rawCmds = XVector_Create(const char*);
    if (!cmd->m_rawCmd || !cmd->m_rawCmds) {
        XFtpCommand_delete(cmd);
        return NULL;
    }
    cmd->m_data = NULL;
    cmd->m_device = NULL;
    cmd->m_openMode = 0;
    cmd->m_restOffset = 0;
    return cmd;
}

void XFtpCommand_delete(XFtpCommand* cmd)
{
    if (!cmd) return;
    XClass_delete_base((XClass*)cmd);
}

void XFtpCommand_addRawArg(XFtpCommand* cmd, const char* arg)
{
    if (!cmd || !arg) return;
    if (!cmd->m_rawCmds) {
        cmd->m_rawCmds = XVector_Create(const char*);
        if (!cmd->m_rawCmds) return;
    }
    size_t len = strlen(arg);
    char* dup = (char*)XMalloc(len + 1, XMEMORY_TYPE_SYSTEM);
    if (!dup) return;
    memcpy(dup, arg, len + 1);
    size_t before = XVector_size_base(cmd->m_rawCmds);
    XVector_push_back_1_base(cmd->m_rawCmds, &dup);
    if (XVector_size_base(cmd->m_rawCmds) != before + 1)
        XFree(dup, XMEMORY_TYPE_SYSTEM);
}

#endif /* XFTP_ON */
#endif /* XPROTOCOL_ON */
