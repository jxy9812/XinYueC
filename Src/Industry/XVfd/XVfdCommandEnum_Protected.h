/**
 * @file       XVfdCommandEnum_Protected.h
 * @brief      离散命令枚举通用基类内部接口（仅供子类与内部实现使用）。
 * @details    集中放置品牌子类需要的「按动作查码并写命令槽」模板。
 * @note       约束：本头自包含（显式 include 自己的公开头），可单独包含。
 */
#ifndef XVFDCOMMANDENUM_PROTECTED_H
#define XVFDCOMMANDENUM_PROTECTED_H

#include "XVfdCommandEnum.h"

#if XVFD_ON
#if XVFD_COMMANDENUM_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 离散命令写入模板：查命令码 → 经命令语义槽下发 → 挂动作监督。
 * @param dev 实例；不能为 NULL。
 * @param op 动作类型（XVfdOperation）。
 * @param reverse 是否反转方向（命令码表按 (op, reverse) 查）。
 * @param role 命令语义槽（品牌点表里承载命令字的槽；通常是 XVfdRole_CommandWord）。
 * @return 受理返回 true；命令码未命中 / 通道拒绝返回 false。
 * @note 复用基类的单飞与落定监督：写落定后由 ActionWriteDone 虚槽收口。
 */
bool XVfdCommandEnum_write(XVfdCommandEnum* dev, XVfdOperation op, bool reverse,
                           XVfdPointRole role);

#ifdef __cplusplus
}
#endif

#endif /* XVFD_COMMANDENUM_ON */
#endif /* XVFD_ON */
#endif /* XVFDCOMMANDENUM_PROTECTED_H */
