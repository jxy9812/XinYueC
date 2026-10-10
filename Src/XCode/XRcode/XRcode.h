#ifndef XRCODE_H
#define XRCODE_H

#include "XByteArray.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 不透明的二维码编码器句柄
 */
typedef struct XRcode XRcode;

/**
 * @brief 创建一个新的二维码编码器实例
 * @return 成功返回非空指针，失败返回 NULL
 */
XRcode* XRcode_create(void);

/**
 * @brief 销毁二维码编码器实例，释放所有资源
 * @param qr 编码器指针（可为 NULL）
 */
void XRcode_delete(XRcode* qr);
/**
 * @brief 将数据编码为二维码矩阵
 * @param qr 编码器实例
 * @param data 待编码的字节数据（不能为空）
 * @param reserved_blank 中心预留的空白正方形边长（模块数），0 表示不预留
 * @param version 指定二维码版本（1~9），若为 0 则自动选择最小合适版本
 * @return 成功返回 true，失败返回 false（数据过长、版本不支持或空白过大）
 */
bool XRcode_encode(XRcode* qr, const XByteArray* data, int reserved_blank, int version);

/**
 * @brief 将数据编码为二维码矩阵（可选纠错等级；2026-10-08 新增）
 * @param qr 编码器实例
 * @param data 待编码的字节数据（不能为空）
 * @param reserved_blank 中心预留的空白正方形边长（模块数），0 表示不预留
 * @param version 指定二维码版本（1~9），若为 0 则自动选择最小合适版本
 * @param level 纠错等级：0=L（约 7% 冗余，历史缺省）1=M 2=Q（暂未内置，
 *              传入即失败）3=H（约 30% 冗余，中心内嵌图/logo 场景必须
 *              使用——L 级冗余不足以吃掉预留空白，真机不可识）
 * @return 成功返回 true，失败返回 false（数据过长、等级/版本不支持或空白过大）
 * @note 等级越高容量越小，同一内容可能自动升高版本；H 级 v5/v7/v8/v9
 *       为不等长 RS 分块（短块在前），交织按规范列优先处理。
 */
bool XRcode_encode_ex(XRcode* qr, const XByteArray* data, int reserved_blank,
                      int version, int level);
/**
 * @brief 获取当前二维码矩阵的边长（点数）
 * @param qr 编码器实例
 * @return 边长（1~53），若尚未编码则返回 0
 */
int XRcode_size(const XRcode* qr);

/**
 * @brief 获取当前二维码矩阵数据（一维数组，按行优先存储）
 * @param qr 编码器实例
 * @return 指向内部 XByteArray 的指针，每个元素为 0 或 1（0=白，1=黑）
 * @note 返回的数组由编码器内部管理，不可修改或释放
 */
const XByteArray* XRcode_matrix(const XRcode* qr);

/**
 * @brief 获取当前编码的最终码字序列（数据交织+纠错后、摆放前）
 * @param qr 编码器实例
 * @return 指向内部 XByteArray 的指针，每字节一个码字；未编码返回 NULL
 * @note 调试/对拍用；数组由编码器内部管理，不可修改或释放（2026-10-08）
 */
const XByteArray* XRcode_codeWord(const XRcode* qr);

/** @brief 获取当前编码的码字总数；未编码返回 0（2026-10-08）。 */
int XRcode_codeWordCount(const XRcode* qr);

/**
 * @brief 将当前二维码矩阵以文本形式打印到控制台（调试用）
 * @param qr 编码器实例
 */
void XRcode_print_matrix(const XRcode* qr);

#ifdef __cplusplus
}
#endif

#endif // XRCODE_H