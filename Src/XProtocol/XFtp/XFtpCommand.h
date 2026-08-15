/**
 * @file        XFtpCommand.h
 * @brief       FTP 命令对象（描述一条挂起或正在执行的 FTP 命令）
 */

#ifndef XFTP_COMMAND_H
#define XFTP_COMMAND_H
#include "XFtp_config.h"

#include "CXinYueConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XByteArray.h"
#include "XVector.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XFTP_ON

/**
 * @brief FTP 命令类型
 */
typedef enum XFtpCommand_Type {
    XFtpCommand_None = 0,        ///< No command / no active command
    XFtpCommand_ConnectToHost,   ///< Connect the control channel
    XFtpCommand_Login,           ///< Authenticate with USER/PASS
    XFtpCommand_Close,           ///< Close the control channel with QUIT
    XFtpCommand_List,            ///< LIST or MLSD directory transfer
    XFtpCommand_Cd,              ///< Change working directory with CWD
    XFtpCommand_Get,             ///< Download with RETR
    XFtpCommand_Put,             ///< Upload with STOR or APPE
    XFtpCommand_Remove,          ///< Delete a remote file with DELE
    XFtpCommand_Rename,          ///< Rename with RNFR/RNTO
    XFtpCommand_Mkdir,           ///< Create a directory with MKD
    XFtpCommand_Rmdir,           ///< Remove a directory with RMD
    XFtpCommand_RawCommand,      ///< Send a caller-supplied command
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
    void* m_device;                    ///< 目标设备（Get，XIODevice 派生对象；不归命令所有）
    int m_openMode;                    ///< 设备打开方式（XIODeviceBaseMode）
    uint8_t m_append;                  ///< Put 追加模式（APPE，区别于 STOR）

    // 断点续传（Get）
    int64_t m_restOffset;              ///< REST 偏移
} XFtpCommand;

/**
 * @brief 返回 XFtpCommand 类的共享虚函数表。
 * @return 类虚函数表；首次调用时完成初始化，生命周期由类系统管理。
 */
XVtable* XFtpCommand_class_init(void);

/**
 * @brief 创建 FTP 命令对象。
 * @param[in] id       调用方分配的命令 ID
 * @param[in] cmdType  命令类型
 * @return 新对象；分配失败返回 NULL。调用方负责 XFtpCommand_delete。
 */
XFtpCommand* XFtpCommand_create_ex(XMemoryType memory,  int id, XFtpCommand_Type cmdType);

/**
 * @brief 销毁 FTP 命令对象及其参数/上传缓冲。
 * @param[in] cmd 命令对象；NULL 安全
 * @note m_device 指向的外部设备不由此函数释放。
 */
void XFtpCommand_delete(XFtpCommand* cmd);

/**
 * @brief 添加一个命令参数。
 * @param[in,out] cmd 命令对象
 * @param[in] arg     UTF-8 参数；函数会复制字符串内容
 * @note 分配失败时参数不会被添加；函数本身无返回值，调用方如需确认应检查 m_rawCmds 长度。
 */
void XFtpCommand_addRawArg(XFtpCommand* cmd, const char* arg);

#endif /* XFTP_ON */
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XFtpCommand_create
#define XFtpCommand_create(...) XFtpCommand_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XFTP_COMMAND_H
