# -*- coding: utf-8 -*-
"""角色 → 权限位图（RBAC），与两端缺省表逐位一致。

[为什么 webui 也要一份] 网页侧的门禁是**下限**（服务端的闸门仍然生效，
它看到的永远是网页服务器那一个上游身份）。判定一律 `can_role(role, perm)`
查位表；**禁止**写 `role >= ROLE_ADMIN` 数值比较——角色值不单调
（Operator=3 是追加值，proto.ROLE_*），数值比较会把操作者误放行成管理员
（旧骨架 auth.py:69-70 / api.py:607 的真锅，2026-11-13 根修）。

镜像源（改一侧同步三侧）：
    server_py/hsfsrv/permissions.py   （服务端门禁 + DENIED_TEXT 原文）
    client_py/hsfsrv_client/permissions.py （界面置灰口径）
    本文件                            （web 网关预检）

四档角色（值见 proto.ROLE_*）：

    匿名     —— 只能查看
    普通用户 —— 查看 + 下载（导出）
    操作者   —— 普通 + 设备控制（变频器/DO/阀/泵）
    管理员   —— 全部（模式切换/自动运行/账户与配置）
"""

from . import proto

#: 角色 → 缺省权限位图（与 server_py permissions.DEFAULTS 逐位一致）。
DEFAULTS = {
    proto.ROLE_ANONYMOUS: proto.PERM_VIEW,
    proto.ROLE_USER: proto.PERM_VIEW | proto.PERM_DOWNLOAD,
    proto.ROLE_ADMIN: proto.PERM_ALL,
    proto.ROLE_OPERATOR: (proto.PERM_VIEW | proto.PERM_DOWNLOAD |
                          proto.PERM_DEVICE_CONTROL),
}

#: 角色中文名（日志/界面/状态栏同口径）。
ROLE_NAMES = {
    proto.ROLE_ANONYMOUS: "匿名",
    proto.ROLE_USER: "普通用户",
    proto.ROLE_ADMIN: "管理员",
    proto.ROLE_OPERATOR: "操作者",
}

#: 权限不足提示原文（与 server_py permissions.DENIED_TEXT / C 端
#: hsfAppPermDeniedText 同口径——拒绝文案逐字相等是双端对齐回归锁的一项）。
DENIED_TEXT = {
    proto.PERM_VIEW: "权限不足：查看数据需要已认证会话",
    proto.PERM_DOWNLOAD: "权限不足：下载数据需要普通用户及以上账户",
    proto.PERM_DEVICE_CONTROL: "权限不足：设备控制需要操作者或管理员账户",
    proto.PERM_MODE_SWITCH: "权限不足：切换运行模式需要管理员账户",
    proto.PERM_AUTO_CONTROL: "权限不足：自动运行控制需要管理员账户",
    proto.PERM_ACCOUNT_ADMIN: "权限不足：修改服务端配置需要管理员账户",
}


def role_name(role):
    """角色中文名（未知角色="未知"，不编造）。"""
    return ROLE_NAMES.get(int(role or 0), "未知")


def perms_of(role):
    """角色 → 权限位图（未知角色=0，缺省拒绝）。"""
    return DEFAULTS.get(int(role or 0), 0)


def can_role(role, perm):
    """角色是否持有 perm 的**全部**位（perm=0 恒 True）。"""
    if not perm:
        return True
    return (perms_of(role) & int(perm)) == int(perm)


def denied_text(perm):
    """权限不足提示原文。"""
    return DENIED_TEXT.get(int(perm), "权限不足")
