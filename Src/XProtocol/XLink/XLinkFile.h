/** @file       XLinkFile.h
 * @brief      XLink 文件传输扩展：File 消息类载荷编解码（纯函数）。
 * @details    XLINK_FILE_ON 下的线上格式层，只负责「结构体 <-> 字节」：
 *               - 七个操作码（Open/OpenAck/Data/Ack/Finish/FinishAck/Abort）
 *                 的编解码，逐字段小端序列化，解析侧不信任任何长度字段；
 *               - 文件名走 XString（UTF-8），线上为长度前缀 + 字节。
 *
 *             设计要点（与 XLinkProto.h 的 XLinkFileCode 对应）：
 *               - DATA 帧载荷 = [u64 offset][bytes...]，偏移显式携带，
 *                 乱序/重传都能自描述，接收侧据此落盘定位；
 *               - 流控用 credit（接收方在 ACK 里授予字节额度），
 *                 发送方额度耗尽即停，避免把接收缓冲/磁盘打爆；
 *               - 断点续传：Open/OpenAck 携带 startOffset，双方从该偏移继续；
 *               - 完整性：Finish 携带整文件 SHA-256，接收方复算比对。
 *
 *             本层无状态、无堆分配；状态机与磁盘 IO 在 XLinkFileTransfer 层。
 * @author     XinYueC 团队
 */
#ifndef XLINKFILE_H
#define XLINKFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XLink_config.h"

#if XLINK_ON && XLINK_FILE_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XLinkProto.h"

/** @brief Open 载荷固定头部字节数。
 *  @details 布局：mode(1)+flags(1)+nameLen(2)+chunkBytes(4)+windowBytes(4)
 *           +totalSize(8)+startOffset(8) = **28 字节**，其后紧跟文件名。
 *           任何字段增删都必须同步本常量与编解码两侧。 */
#define XLINK_FILE_OPEN_HEADER 28u

/** @brief OpenAck 载荷固定头部字节数。
 *  @details 布局：accepted(1)+status(1)+textLen(2)+chunkBytes(4)
 *           +windowBytes(4)+startOffset(8)+totalSize(8) = **28 字节**，
 *           其后紧跟文本。totalSize 用于下载方向：发起方（接收方）
 *           在 Open 时还不知道文件多大，必须由服务端在 OpenAck 里告知。 */
#define XLINK_FILE_OPEN_ACK_HEADER 28u

/** @brief DATA 载荷偏移字段字节数（u64）。 */
#define XLINK_FILE_DATA_OFFSET_BYTES 8u

/* ========================================================================== */
/*                        载荷编解码                                         */
/* ========================================================================== */

/**
 * @brief      编码 Open 载荷。
 * @param      msg 消息（借用）；m_nameLen 为文件名有效字节数。
 * @param      buf 输出缓冲（调用方提供）；不得为 NULL。
 * @param      capacity 输出缓冲容量。
 * @param      outLen 实际写入字节数（可为 NULL）。
 * @return     成功返回 true；参数非法/容量不足（不产生半写）返回 false。
 */
bool XLinkFile_encodeOpen(const XLinkFileOpenPayload* msg, uint8_t* buf,
                          size_t capacity, size_t* outLen);

/**
 * @brief      解码 Open 载荷。
 * @param      buf 输入缓冲（借用）。
 * @param      length 输入字节数。
 * @param      out 输出消息（调用方提供存储）。
 * @return     成功返回 true；畸形（长度不符/方向非法/文件名越界）返回 false。
 * @note       out->m_name 为定长内联缓冲（非终止符保证，m_nameLen 权威）；
 *             同时保证 m_name[m_nameLen] 处写 '\0'（容量内）。
 */
bool XLinkFile_decodeOpen(const uint8_t* buf, size_t length,
                          XLinkFileOpenPayload* out);

/**
 * @brief      编码 OpenAck 载荷。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkFile_encodeOpenAck(const XLinkFileOpenAckPayload* msg, uint8_t* buf,
                             size_t capacity, size_t* outLen);

/**
 * @brief      解码 OpenAck 载荷。
 * @return     成功返回 true；畸形返回 false。
 */
bool XLinkFile_decodeOpenAck(const uint8_t* buf, size_t length,
                             XLinkFileOpenAckPayload* out);

/**
 * @brief      编码 DATA 载荷头 + 数据。
 * @param      offset 数据在文件内的起始偏移。
 * @param      data 业务数据（借用）；可为 NULL（length 为 0 时）。
 * @param      length 业务数据字节数。
 * @param      buf 输出缓冲。
 * @param      capacity 输出缓冲容量。
 * @param      outLen 实际写入字节数（可为 NULL）。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkFile_encodeData(uint64_t offset, const uint8_t* data, uint32_t length,
                          uint8_t* buf, size_t capacity, size_t* outLen);

/**
 * @brief      解码 DATA 载荷。
 * @param      buf 输入缓冲（借用）。
 * @param      length 输入字节数（含 8 字节偏移）。
 * @param      offsetOut 输出偏移（可为 NULL）。
 * @param      dataOut 输出数据指针（借用，指向 buf 内部；可为 NULL）。
 * @param      dataLenOut 输出数据长度（可为 NULL）。
 * @return     成功返回 true；长度不足 8 字节返回 false。
 */
bool XLinkFile_decodeData(const uint8_t* buf, size_t length,
                          uint64_t* offsetOut, const uint8_t** dataOut,
                          uint32_t* dataLenOut);

/**
 * @brief      编码 Ack 载荷。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkFile_encodeAck(const XLinkFileAckPayload* msg, uint8_t* buf,
                         size_t capacity, size_t* outLen);

/**
 * @brief      解码 Ack 载荷。
 * @return     成功返回 true；畸形返回 false。
 */
bool XLinkFile_decodeAck(const uint8_t* buf, size_t length,
                         XLinkFileAckPayload* out);

/**
 * @brief      编码 Finish 载荷（总大小 + 整文件 SHA-256）。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkFile_encodeFinish(const XLinkFileFinishPayload* msg, uint8_t* buf,
                            size_t capacity, size_t* outLen);

/**
 * @brief      解码 Finish 载荷。
 * @return     成功返回 true；畸形返回 false。
 */
bool XLinkFile_decodeFinish(const uint8_t* buf, size_t length,
                            XLinkFileFinishPayload* out);

/**
 * @brief      编码 FinishAck 载荷。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkFile_encodeFinishAck(const XLinkFileFinishAckPayload* msg,
                               uint8_t* buf, size_t capacity, size_t* outLen);

/**
 * @brief      解码 FinishAck 载荷。
 * @return     成功返回 true；畸形返回 false。
 */
bool XLinkFile_decodeFinishAck(const uint8_t* buf, size_t length,
                               XLinkFileFinishAckPayload* out);

/**
 * @brief      编码 Abort 载荷。
 * @return     成功返回 true；容量不足返回 false。
 */
bool XLinkFile_encodeAbort(const XLinkFileAbortPayload* msg, uint8_t* buf,
                           size_t capacity, size_t* outLen);

/**
 * @brief      解码 Abort 载荷。
 * @return     成功返回 true；畸形返回 false。
 */
bool XLinkFile_decodeAbort(const uint8_t* buf, size_t length,
                           XLinkFileAbortPayload* out);

/* ========================================================================== */
/*                        文件名安全校验                                     */
/* ========================================================================== */

/**
 * @brief      校验远程文件名是否安全（服务端沙箱第一道闸）。
 * @param      name 文件名（UTF-8，长度以 nameLen 为准或 NUL 终止）。
 * @param      nameLen 有效字节数；传 0 表示按 NUL 终止串处理。
 * @return     安全返回 true；拒绝返回 false。
 * @note       拒绝规则（任一命中即拒）：
 *               - 空名、超长（>= XLINK_FILE_NAME_BYTES）、含 NUL/控制字符；
 *               - 绝对路径（以 '/' 或 '\\' 或盘符 "X:" 开头）；
 *               - 含 ".." 路径分量（防目录穿越）；
 *               - 以 '.' 开头的隐藏名（避免覆盖配置/隐藏文件）。
 *             通过本校验不代表最终安全——调用方仍须对规范化后的绝对路径
 *             做根目录前缀校验（软链接逃逸由 resolvePath 承担）。
 */
bool XLinkFile_isSafeName(const char* name, size_t nameLen);

/**
 * @brief      求远程文件名的最后一段（去掉路径分量）。
 * @param      name 文件名（UTF-8）。
 * @return     借用指针（指向 name 内部）；name 为 NULL 返回 NULL。
 * @note       仅用于展示/日志；落盘路径必须用完整相对路径并经沙箱校验。
 */
const char* XLinkFile_baseName_const(const char* name);

#endif /* XLINK_ON && XLINK_FILE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKFILE_H */
