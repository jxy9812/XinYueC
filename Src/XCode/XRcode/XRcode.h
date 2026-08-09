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
 * @brief 获取当前二维码矩阵的边长（点数）
 * @param qr 编码器实例
 * @return 边长（1~53），若尚未编码则返回 0
 */
int XRcode_size(const XRcode* qr);

/**
 * @brief 获取二维码矩阵数据（一维数组，按行优先存储）
 * @param qr 编码器实例
 * @return 指向内部 XByteArray 的指针，每个元素为 0 或 1（0=白，1=黑）
 * @note 返回的数组由编码器内部管理，不可修改或释放
 */
const XByteArray* XRcode_matrix(const XRcode* qr);

/**
 * @brief 将当前二维码矩阵以文本形式打印到控制台（调试用）
 * @param qr 编码器实例
 */
void XRcode_print_matrix(const XRcode* qr);

#ifdef __cplusplus
}
#endif

#endif // XRCODE_H