#ifndef XS7BLOCK_H
#define XS7BLOCK_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XVector.h"
#include "XByteArray.h"
#include "XS7Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_BLOCK_ON

/**
 * @file XS7Block.h
 * @brief S7 运维：块列表/上传/下载多包序列的组包与状态推进辅助（纯函数）
 * @details 块传输是多 PDU 序列（序列内串行，由 XS7TcpClient 状态机推进），
 *          本头只提供各步报文构造与应答解析。
 *
 * @warning 功能码待实测：块服务功能码（0x1A/0x1B/0x1C/0x1E/0x1F/0x20、
 *          0x12/0x13/0x14 候选）是本方案唯一未经双参考交叉验证的常量区，
 *          阶段 0 须用 Wireshark s7comm 解码器或 snap7 抓包核对后，
 *          方可在 XS7Types.h 补入 #define 并启用本模块实现。
 */

/**
 * @brief 块信息结构（块列表结果元素）
 * @details XVector<XS7BlockInfo> 的元素类型
 */
typedef struct XS7BlockInfo {
    char     name[8];      ///< 块名（NUL 结尾，如 "OB1"/"DB1"/"FC1"）
    uint8_t  blockType;    ///< 块类型码（OB/DB/SB/FC/FB…，待实测核对）
    uint32_t size;         ///< 块大小（字节，未知填 0）
} XS7BlockInfo;

/**
 * @brief 构造 ListBlocks 请求
 * @param out 输出缓冲（写入 S7 PDU，不含 TPKT/COTP）
 * @param pduRef PDU 引用号
 * @return 写入字节数，失败返回0（功能码待实测后实现）
 */
size_t XS7Block_buildListBlocks(uint8_t* out, uint16_t pduRef);

/**
 * @brief 解析 ListBlocks 应答
 * @param s7 S7 PDU 起始
 * @param len 可用长度
 * @param outBlocks 输出块信息数组（XVector<XS7BlockInfo>，由调用者创建并传入）
 * @return 成功返回true，失败返回false
 */
bool XS7Block_parseListBlocksAck(const uint8_t* s7, size_t len, XVector* outBlocks);

/**
 * @brief 构造 StartUpload 请求（上传序列第一步）
 * @param out 输出缓冲（写入 S7 PDU）
 * @param pduRef PDU 引用号
 * @param blockName 块名（NUL 结尾，如 "DB1"）
 * @return 写入字节数，失败返回0
 */
size_t XS7Block_buildStartUpload(uint8_t* out, uint16_t pduRef, const char* blockName);

/**
 * @brief 构造 Upload 请求（上传序列中间步，多包推进）
 * @param out 输出缓冲（写入 S7 PDU）
 * @param pduRef PDU 引用号
 * @return 写入字节数，失败返回0
 */
size_t XS7Block_buildUpload(uint8_t* out, uint16_t pduRef);

/**
 * @brief 构造 EndUpload 请求（上传序列结束步）
 * @param out 输出缓冲（写入 S7 PDU）
 * @param pduRef PDU 引用号
 * @param blockName 块名（NUL 结尾）
 * @return 写入字节数，失败返回0
 */
size_t XS7Block_buildEndUpload(uint8_t* out, uint16_t pduRef, const char* blockName);

/**
 * @brief 解析 Upload 应答（多包推进辅助）
 * @param s7 S7 PDU 起始
 * @param len 可用长度
 * @param outData 输出：本包块数据追加目标（调用者持有）
 * @param outFinished 输出：序列是否结束
 * @return 成功返回true，失败返回false
 */
bool XS7Block_parseUploadAck(const uint8_t* s7, size_t len, XByteArray* outData, bool* outFinished);

/**
 * @brief 构造 RequestDownload 请求（下载序列第一步）
 * @param out 输出缓冲（写入 S7 PDU）
 * @param pduRef PDU 引用号
 * @param blockName 块名（NUL 结尾）
 * @param blockSize 块总字节数
 * @return 写入字节数，失败返回0
 */
size_t XS7Block_buildRequestDownload(uint8_t* out, uint16_t pduRef, const char* blockName, uint32_t blockSize);

/**
 * @brief 构造 Download 分包数据请求（下载序列中间步）
 * @param out 输出缓冲（写入 S7 PDU）
 * @param pduRef PDU 引用号
 * @param offset 本包在块内的字节偏移
 * @param data 本包数据
 * @param dataLen 本包字节数
 * @param last 是否为最后一包
 * @return 写入字节数，失败返回0
 */
size_t XS7Block_buildDownload(uint8_t* out, uint16_t pduRef, uint32_t offset,
                              const uint8_t* data, size_t dataLen, bool last);

/**
 * @brief 解析 Download 应答
 * @param s7 S7 PDU 起始
 * @param len 可用长度
 * @return 成功返回true，失败返回false
 */
bool XS7Block_parseDownloadAck(const uint8_t* s7, size_t len);

#endif /* XS7_BLOCK_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7BLOCK_H
