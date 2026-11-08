# -*- coding: utf-8 -*-
"""角色 → 权限位图（RBAC），对齐 C 侧 Src/Industry/XAccessControl/XPermission。

[为什么两端各有一份] C 服务端用 XPermPolicy（位图 O(1) 判定），Python 服务端
用本模块；**缺省表必须逐位一致**，否则同一个账户在两端能做不同的事。修改任
一侧的唯一正确姿势是：改默认表 + 改 ``tests/test_permissions.py`` 的镜像断言。

四档角色（值见 proto.ROLE_*；**数值不单调**：Operator=3 是追加值）：

    匿名     None     —— 只能查看（订阅遥测/快照/历史分页）
    普通用户 User     —— 查看 + 下载（导出）
    操作者   Operator —— 普通 + 设备控制（变频器/DO/阀/泵）
    管理员   Admin    —— 全部（模式切换/自动运行/账户与配置）

[上限表] 每档有权限上限（``CEILINGS``）：即使运维把策略改宽，也不能让匿名
拿到控制位——"匿名只能看"是产品约束，不是可配置项。与 C 侧
``XPermPolicy_setRolePerms`` 的上限校验同口径。

[判定入口] 服务端命令处理里一律用 ``can(session, PERM_*)``；**禁止**写
``session.role() >= 某角色``（角色值不单调，且语义会把 Operator 与 Admin 搞混）。
"""

from . import proto

#: 角色 → 缺省权限位图（下标严格等于 proto.ROLE_* 值）。
DEFAULTS = {
    proto.ROLE_ANONYMOUS: proto.PERM_VIEW,
    proto.ROLE_USER: proto.PERM_VIEW | proto.PERM_DOWNLOAD,
    proto.ROLE_ADMIN: proto.PERM_ALL,
    proto.ROLE_OPERATOR: (proto.PERM_VIEW | proto.PERM_DOWNLOAD |
                          proto.PERM_DEVICE_CONTROL),
}

#: 角色 → 允许的最大权限位（越界位一律丢弃/拒绝）。
CEILINGS = {
    proto.ROLE_ANONYMOUS: proto.PERM_VIEW,
    proto.ROLE_USER: proto.PERM_VIEW | proto.PERM_DOWNLOAD,
    proto.ROLE_ADMIN: proto.PERM_ALL,
    proto.ROLE_OPERATOR: (proto.PERM_VIEW | proto.PERM_DOWNLOAD |
                          proto.PERM_DEVICE_CONTROL),
}

#: 角色中文名（日志/界面同口径）。
ROLE_NAMES = {
    proto.ROLE_ANONYMOUS: "匿名",
    proto.ROLE_USER: "普通用户",
    proto.ROLE_ADMIN: "管理员",
    proto.ROLE_OPERATOR: "操作者",
}

#: 权限不足提示原文（与 C 侧 HsfServerApp.c hsfAppPermDeniedText 同口径）。
DENIED_TEXT = {
    proto.PERM_VIEW: "权限不足：查看数据需要已认证会话",
    proto.PERM_DOWNLOAD: "权限不足：下载数据需要普通用户及以上账户",
    proto.PERM_DEVICE_CONTROL: "权限不足：设备控制需要操作者或管理员账户",
    proto.PERM_MODE_SWITCH: "权限不足：切换运行模式需要管理员账户",
    proto.PERM_AUTO_CONTROL: "权限不足：自动运行控制需要管理员账户",
    proto.PERM_ACCOUNT_ADMIN: "权限不足：修改服务端配置需要管理员账户",
}


def role_of(session):
    """取会话角色（未认证/异常一律按匿名）。"""
    try:
        role = session.role()
    except Exception:                                  # noqa: BLE001 - 防御式
        return proto.ROLE_ANONYMOUS
    return role if role in DEFAULTS else proto.ROLE_ANONYMOUS


def perms_of(role):
    """角色 → 权限位图（未知角色=0，缺省拒绝）。"""
    return DEFAULTS.get(role, 0)


def role_name(role):
    return ROLE_NAMES.get(role, "未知")


def ceiling_of(role):
    return CEILINGS.get(role, 0)


def can_role(role, perm):
    """纯函数判定：角色是否持有 perm 的**全部**位。"""
    if not perm:
        return True
    return (perms_of(role) & perm) == perm


def can(session, perm):
    """会话判定（服务端命令门）。"""
    return can_role(role_of(session), perm)


def denied_text(perm):
    """权限不足提示原文。"""
    return DENIED_TEXT.get(perm, "权限不足")


def set_role_perms(role, mask):
    """覆盖某角色权限（带上限校验；Admin 恒为全权）。

    返回 True=已生效；False=角色非法/越上限。策略变更需调用方广播
    （当前命令面未开放远端改策略，本函数供测试与运维脚本使用）。
    """
    mask = int(mask) & proto.PERM_ALL
    if role not in DEFAULTS:
        return False
    if mask & ~CEILINGS[role]:
        return False
    if role == proto.ROLE_ADMIN and mask != proto.PERM_ALL:
        return False
    DEFAULTS[role] = mask
    return True


def reset_policy():
    """回缺省表（与 C 侧 XPermPolicy_resetPolicy 同口径）。"""
    DEFAULTS[proto.ROLE_ANONYMOUS] = proto.PERM_VIEW
    DEFAULTS[proto.ROLE_USER] = proto.PERM_VIEW | proto.PERM_DOWNLOAD
    DEFAULTS[proto.ROLE_ADMIN] = proto.PERM_ALL
    DEFAULTS[proto.ROLE_OPERATOR] = (proto.PERM_VIEW | proto.PERM_DOWNLOAD |
                                     proto.PERM_DEVICE_CONTROL)
