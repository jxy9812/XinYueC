/**
 * @file        XFtpCommand.h
 * @brief       FTP 命令对象（描述一条挂起或正在执行的 FTP 命令）
 */

#ifndef XFTP_COMMAND_H
#define XFTP_COMMAND_H

#include "CXinYueConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XByteArray.h"
#include "XVector.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(XNETWORK_ON) || 1

/**
 * @brief FTP 命令类型
 */
typedef enum XFtpCommand_Type {
    XFtpCommand_None = 0,
    XFtpCommand_ConnectToHost,
    XFtpCommand_Login,
    XFtpCommand_Close,
    XFtpCommand_List,
    XFtpCommand_Cd,
    XFtpCommand_Get,
    XFtpCommand_Put,
    XFtpCommand_Remove,
    XFtpCommand_Rename,
    XFtpCommand_Mkdir,
    XFtpCommand_Rmdir,
    XFtpCommand_RawCommand,
    XFtpCommand_Feat,            ///< FEAT 特性协商
    XFtpCommand_Opts,            ///< OPTS 选项设置（UTF8 等）
    XFtpCommand_TypeSet,         ///< TYPE I/A 切换
    XFtpCommand_Pasv,            ///< PASV/EPSV 单独执行
    XFtpCommand_Port,            ///< PORT/EPRT 单独执行
    XFtpCommand_Abor,            ///< ABOR 中断传输
    XFtpCommand_Size,            ///< SIZE 文件大小查询
    XFtpCommand_Mdtm,            ///< MDTM 文件修改时间
    XFtpCommand_Mlst             ///< MLST 单文件元信息（RFC 3659）
} XFtpCommand_Type;

typedef struct XFtpCommand {
    XObject m_class;

    int m_id;                          ///< 命令ID
    XFtpCommand_Type m_command;        ///< 命令类型
    XString* m_rawCmd;                 ///< 原始命令字符串（已组装好）
    XVector* m_rawCmds;                ///< 命令参数列表（const char* 列表）

    // 数据传输相关（Get/Put 用）
    XByteArray* m_data;                ///< 上传数据（Put）
    void* m_device;                    ///< 目标设备（Get，XFile* 等）
    int m_openMode;                    ///< 设备打开方式
    uint8_t m_append;                  ///< Put 追加模式（APPE，区别于 STOR）

    // 断点续传（Get）
    int64_t m_restOffset;              ///< REST 偏移
} XFtpCommand;

XFtpCommand* XFtpCommand_create(int id, XFtpCommand_Type cmdType);
void XFtpCommand_delete(XFtpCommand* cmd);
void XFtpCommand_addRawArg(XFtpCommand* cmd, const char* arg);

#endif // XNETWORK_ON && XNETWORK_FTP_ON

#ifdef __cplusplus
}
#endif

#endif // XFTP_COMMAND_H
