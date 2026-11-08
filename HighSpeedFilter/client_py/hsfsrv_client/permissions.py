# -*- coding: utf-8 -*-
"""客户端侧权限镜像（角色 → 权限位图），对齐服务端 ``hsfsrv/permissions.py``。

[为什么客户端也要一份] 界面需要提前把"不能做的事"灰掉/隐藏，不能让用户
点了才知道被拒。**这份表只影响可达性**，真正的门在服务端（每帧校验）；
两边缺省表必须逐位一致，改一侧就要同步另一侧。

四档角色（值见 ``proto.ROLE_*``；数值不单调，Operator=3 为追加值）：

    匿名     —— 只能查看
    普通用户 —— 查看 + 下载
    操作者   —— 普通 + 设备控制
    管理员   —— 全部
"""

from . import proto

DEFAULTS = {
    proto.ROLE_ANONYMOUS: proto.PERM_VIEW,
    proto.ROLE_USER: proto.PERM_VIEW | proto.PERM_DOWNLOAD,
    proto.ROLE_ADMIN: proto.PERM_ALL,
    proto.ROLE_OPERATOR: (proto.PERM_VIEW | proto.PERM_DOWNLOAD |
                          proto.PERM_DEVICE_CONTROL),
}

ROLE_NAMES = {
    proto.ROLE_ANONYMOUS: "匿名",
    proto.ROLE_USER: "普通用户",
    proto.ROLE_ADMIN: "管理员",
    proto.ROLE_OPERATOR: "操作者",
}


def role_name(role):
    """角色中文名（与服务端同口径）。"""
    return ROLE_NAMES.get(int(role or 0), "未知")


def can_role(role, perm):
    """角色是否持有 perm 的全部位（未知角色=缺省拒绝）。"""
    if not perm:
        return True
    return (DEFAULTS.get(int(role or 0), 0) & int(perm)) == int(perm)
