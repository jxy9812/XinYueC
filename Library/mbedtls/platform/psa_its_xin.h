/**
 * \file  psa_its_xin.h
 * \brief PSA Internal Trusted Storage 统一桥接声明（XFile 后端）。
 *
 * MBEDTLS_PSA_ITS_FILE_C（stdio 直写文件）全平台停用后，PSA 密钥持久化
 * 走本统一实现：psa_its_xfile.c 经库内跨平台文件设备 API
 * （XDevice_open(XDeviceType_File) / XDevice_read|write|seek|close）读写。
 * 桌面与裸机同一代码路径——裸机端 XDeviceFile 自动桥接 FatFs 后端
 * （Library/Fatfs/XDeviceFile_fatfs.c），密钥落盘即用，无需任何额外
 * 接入步骤。
 *
 * 函数签名复用库内 PSA ITS API 头（psa/psa_crypto_its.h，类型与顺序
 * 完全一致），供 library/xcryptographic/psa_crypto_storage.c 调用；
 * 原生分支所需的 psa/internal_trusted_storage.h 在本 mbedtls 树中
 * 不存在，由本头承担同等职责。
 */
#ifndef PSA_ITS_XIN_H
#define PSA_ITS_XIN_H

#include "psa/psa_crypto_its.h"

#endif /* PSA_ITS_XIN_H */
