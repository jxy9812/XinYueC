/**
 * @file        XFtpCommand.c
 * @brief       XFtpCommand 实现
 */

#include "XFtpCommand.h"
#include "XMemory.h"
#include "XString.h"
#include "XObject.h"
#include "XStringUtils.h" /* strlen 直出改经 XStrlen/XStrdup（外部依赖约束） */
#include <string.h>

#if XPROTOCOL_ON
#if XFTP_ON

static void VXFtpCommand_deinit(XFtpCommand* cmd)
{
    if (!cmd) return;
    if (cmd->m_rawCmd) {
        XClassDelete((XClass*)cmd->m_rawCmd);
        cmd->m_rawCmd = NULL;
    }
    if (cmd->m_rawCmds) {
        for (size_t i = 0; i < XVector_size_base(cmd->m_rawCmds); i++) {
            char* s = *(char**)XVector_at_base(cmd->m_rawCmds, i);
            if (s) XFree(s, XMEMORY_TYPE_SYSTEM);
        }
        XClassDelete((XClass*)cmd->m_rawCmds);
        cmd->m_rawCmds = NULL;
    }
    if (cmd->m_data) {
        XClassDelete((XClass*)cmd->m_data);
        cmd->m_data = NULL;
    }
    cmd->m_device = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)cmd);
}

XVtable* XFtpCommand_class_init(void)
{
    /* XFtpCommand 头文件未新增虚函数槽位，虚表容量沿用 XObject 的槽位总数。 */
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XObject))
    XCLASS_SET_CLASS_NAME_DEFAULT("XFtpCommand");
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFtpCommand_deinit);
    return XVTABLE_DEFAULT;
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
        XClassDelete(cmd);
        return NULL;
    }
    cmd->m_data = NULL;
    cmd->m_device = NULL;
    cmd->m_openMode = 0;
    cmd->m_restOffset = 0;
    return cmd;
}

void XFtpCommand_addRawArg(XFtpCommand* cmd, const char* arg)
{
    if (!cmd || !arg) return;
    if (!cmd->m_rawCmds) {
        cmd->m_rawCmds = XVector_Create(const char*);
        if (!cmd->m_rawCmds) return;
    }
    char* dup = XStrdup(arg);
    if (!dup) return;
    size_t before = XVector_size_base(cmd->m_rawCmds);
    XVector_push_back_1_base(cmd->m_rawCmds, &dup);
    if (XVector_size_base(cmd->m_rawCmds) != before + 1)
        XFree_System(dup);
}

#endif /* XFTP_ON */
#endif /* XPROTOCOL_ON */
