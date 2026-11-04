
/**
 * @file       HsfVfdProviderRemote.h
 * @brief      变频器页远端数据提供者（XLink 后端）。
 * @details    与 HsfVfdProvider_demo 同一接口契约的「真后端」实现：遥测
 *             来自 HsfLinkClient 的 BSON 推送（XVfdCodec 解码），控制动作
 *             打包为 XVfdControlRequest 经 XLink Request/Response 下发。
 *             无本地设备对象——快照/能力走 provider 表的 snapshot/caps
 *             扩展槽（页面已按 NULL 降级口径回落）。
 */
#ifndef HSFVFDPROVIDERREMOTE_H
#define HSFVFDPROVIDERREMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XVFD_ON

#include "HsfVfdProvider.h"

/**
 * @brief 取远端 provider（进程内单例；懒接线回调）。
 * @return 静态 provider 表指针；禁释放。整表可用。
 * @note  需先经 HsfLinkClient_open 建立连接（登录窗流程）；连接未就绪时
 *        provider 仍可用——快照 m_valid=false，页面按无效降级显示。
 */
const HsfVfdProvider* HsfVfdProvider_remote(void);

#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif
#endif /* HSFVFDPROVIDERREMOTE_H */
