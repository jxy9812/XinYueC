/**
 * @file       XRemoteSessionBar.h
 * @brief      XRemoteSessionBar 向日葵式悬浮会话工具条(冻结契约, 只声明
 *             API 不含实现; 2026-10-03 新增)。
 * @details    XWidget 派生本地控件: 持 XGuiClient 借用引用, 全部经其
 *             既有公开 API 工作(断开 disconnectFromServer / 档位切换
 *             requestProfileId / 统计 statistics / 状态信号
 *             connected·disconnected·errorOccurred), 不引入任何新
 *             XGuiClient 契约面(冻结头只增不改的口径延续)。
 *             形态与交互(向日葵口径):
 *               - 吸附态: 展开工具条吸附在 attachView 视图区顶部居中;
 *               - 自动收起: 鼠标离开条/球区域约 2 秒收起为半透明小
 *                 悬浮球; 悬停或点击悬浮球展开;
 *               - 悬浮球可拖动换位(拖到哪停哪), 双击回顶部居中;
 *               - 展开态: [断开连接] [档位 性能|资源 切换]
 *                 [统计: 帧数/RTT/丢帧(实时刷新)] [键盘 切换] [收起]
 *                 (键盘钮为私有实现: 显式弹出/收起应用虚拟键盘单例,
 *                 与远程客户端页关断 autoPopup 的「RC 页全权接管」
 *                 口径配套, 冻结契约零新增公有 API);
 *               - 断开连接语义: 只断会话(BYE(NORMAL), 不触发自动
 *                 重连), 不杀进程不退出应用; 会话断开后本条自动隐藏
 *                 (经 disconnected 信号), 由宿主页面恢复可配置态。
 *             交互红线: 本条/球是纯本地控件, 全部鼠标/滚轮事件本地
 *             消费(XEvent_accept), 结构上不经过 XGuiClient 的输入
 *             转发路径(独立控件不命中远端视图); 按下时经 grabMouse
 *             置顶接管抓取, 与 XGuiClient 拖拽抓取互不干扰。
 *             「丢帧」口径: 协议未向客户端暴露服务端丢批遥测
 *             (XGuiServer 丢批仅本端信号, 不发 ERROR 帧), 故统计区
 *             丢帧计数 = 本会话帧类传输错误事件数(errorOccurred 的
 *             BUFFER_OVERFLOW / FRAME_TOO_LARGE), 健康会话恒 0。
 * @note       模块开关: 随 XGUI_REMOTE_ON(见 XGuiRemoteProto.h)整体
 *             裁剪; 头文件为新增加法式, 不动 XGuiClient.h 既有声明。
 * @author     XinYueC 团队
 */
#ifndef XREMOTESESSIONBAR_H
#define XREMOTESESSIONBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XWidget.h"
#include "XGuiRemoteProto.h"

#if XWIDGET_ON && XGUI_REMOTE_ON

#include "XGuiClient.h"

struct XPushButton;
struct XLabel;
/** @brief 私有实现块(实现文件内定义; 契约不暴露)。 */
typedef struct XRemoteSessionBarPrivate XRemoteSessionBarPrivate;

/** @brief XRemoteSessionBar 虚函数枚举(继承 XWidget, 无新增公共槽)。 */
XCLASS_DEFINE_BEGING(XRemoteSessionBar)
XCLASS_DEFINE_EXTEND_END(XRemoteSessionBar, XWidget)

/**
 * @brief      XRemoteSessionBar 控件对象; m_base 必须是第一个成员。
 */
typedef struct XRemoteSessionBar {
    XWidget            m_base;   /**< 基类成员; 必须是第一个。 */
    XRemoteSessionBarPrivate* m_d; /**< 私有实现块(拥有)。 */
} XRemoteSessionBar;

/* ==================== 生命周期 ==================== */

/**
 * @brief  初始化类虚函数表。
 * @return 共享 XVtable 指针。
 */
XVtable* XRemoteSessionBar_class_init(void);

/**
 * @brief  初始化控件(对标构造函数)。
 * @details 默认收起为悬浮球并吸附 parent 坐标系 (0,0) 视图区; 宿主
 *          在 setClient + attachView 后经 expand/show 摆位。
 * @param  self 尚未初始化的控件; 不得为 NULL。
 * @param  parent 父控件(借用, 语义同 XWidget_init)。
 * @param  flags 控件标志(XWidgetFlags, 原样传给 XWidget_init)。
 * @return 无。
 */
void XRemoteSessionBar_init(XRemoteSessionBar* self, XWidget* parent,
                            XWidgetFlags flags);

/** @brief 默认内存类型创建便捷宏(仓库惯例)。 */
#define XRemoteSessionBar_create(parent, flags) \
    XRemoteSessionBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief  按指定内存类型创建控件实例。
 * @param  memory 内存类型。
 * @param  parent 父控件(借用, 语义同 XWidget_init)。
 * @param  flags 控件标志(XWidgetFlags, 原样传给 XWidget_init)。
 * @return 新控件; 分配失败返回 NULL(XClass_delete_base /
 *         XObject_deleteLater 释放)。
 */
XRemoteSessionBar* XRemoteSessionBar_create_ex(XMemoryType memory,
                                               XWidget* parent,
                                               XWidgetFlags flags);

/** @brief 析构/反初始化映射(仓库惯例)。 */
/** @brief 延迟释放别名(事件循环内安全自删)。 */
#define XRemoteSessionBar_deleteLater XObject_deleteLater

/* ==================== 客户端绑定与吸附 ==================== */

/**
 * @brief      绑定/换绑 XGuiClient(借用, 生命周期归调用方)。
 * @details    换绑时解除旧 client 的状态信号连接; 绑定后经
 *             connected/disconnected 信号自管理可见性(连上浮现并
 *             展开, 断开隐藏——「断开=回未连接态」语义的一半, 视图
 *             占位与页面可配置恢复由宿主页面负责)。传 NULL 仅解绑。
 * @param      self 目标控件; 传入 NULL 时不执行任何操作。
 * @param      client 目标客户端(借用, 生命周期归调用方); NULL 仅解绑
 *             既有绑定。
 * @return     无。
 */
void XRemoteSessionBar_setClient(XRemoteSessionBar* self,
                                 XGuiClient* client);

/**
 * @brief      查询当前绑定的 XGuiClient。
 * @param      self 目标控件; 不得为 NULL。
 * @return     当前绑定的 XGuiClient(借用, 生命周期归调用方, 禁止释放);
 *             未绑定返回 NULL。
 */
XGuiClient* XRemoteSessionBar_client(const XRemoteSessionBar* self);

/**
 * @brief      设置吸附参照视图区(parent 坐标系)。
 * @details    展开条吸附 viewRect 顶部居中; 悬浮球默认位=视图区顶部
 *             居中(拖动换位/双击回位均以此为基准)。
 * @param      self 目标控件; 传入 NULL 时不执行任何操作。
 * @param      viewRect 视图区矩形(借用, 只在调用期间读取); 不得为 NULL。
 * @return     无。
 */
void XRemoteSessionBar_attachView(XRemoteSessionBar* self,
                                  const XRect* viewRect);

/* ==================== 展开态与动作 ==================== */

/**
 * @brief      展开为吸附工具条(视图区顶部居中)。
 * @param      self 目标控件; 传入 NULL 时不执行任何操作。
 * @return     无。
 */
void XRemoteSessionBar_expand(XRemoteSessionBar* self);

/**
 * @brief      收起为半透明小悬浮球。
 * @param      self 目标控件; 传入 NULL 时不执行任何操作。
 * @return     无。
 */
void XRemoteSessionBar_collapse(XRemoteSessionBar* self);

/**
 * @brief      查询当前是否为展开工具条态。
 * @param      self 目标控件; 不得为 NULL。
 * @return     true 展开工具条态; false 悬浮球态。
 */
bool XRemoteSessionBar_isExpanded(const XRemoteSessionBar* self);

/**
 * @brief      查询悬浮球是否已被用户拖离默认位。
 * @param      self 目标控件; 不得为 NULL。
 * @return     true 用户已拖动换位(拖到哪停哪); false 仍在默认位
 *             (展开与双击回位逻辑以此判定)。
 */
bool XRemoteSessionBar_isBallUserPlaced(const XRemoteSessionBar* self);

/**
 * @brief      请求档位切换(经 XGuiClient_requestProfileId)。
 * @details    展开条档位下拉与回归测试共用的唯一入口; 未绑定 client
 *             或未连接时仅登记界面选择(与演示页「未连接仅更新界面
 *             值」口径一致), 连接协商结果以远端 FB_META 为准。
 * @param      self 目标控件; 传入 NULL 时不执行任何操作。
 * @param      id 目标档位 id(XGuiRemoteProfileId)。
 * @return     无。
 */
void XRemoteSessionBar_requestProfile(XRemoteSessionBar* self,
                                      XGuiRemoteProfileId id);

/**
 * @brief      查询当前界面档位选择。
 * @param      self 目标控件; 不得为 NULL。
 * @return     当前界面档位选择(协商生效值以远端 FB_META 为准)。
 */
XGuiRemoteProfileId XRemoteSessionBar_profile(
    const XRemoteSessionBar* self);

/* ==================== 回归测试/宿主直达(借用) ==================== */

/**
 * @brief      断开连接按钮(回归测试合成点击直达)。
 * @param      self 目标控件; 不得为 NULL。
 * @return     内部断开连接按钮(借用, 属本控件子控件, 生命周期归本控件,
 *             禁止在外部释放); 未初始化返回 NULL。
 */
struct XPushButton* XRemoteSessionBar_disconnectButton(
    const XRemoteSessionBar* self);

/**
 * @brief      统计文本标签(「帧 N | RTT N ms | 丢帧 N」)。
 * @param      self 目标控件; 不得为 NULL。
 * @return     内部统计标签(借用, 属本控件子控件, 生命周期归本控件,
 *             禁止在外部释放); 未初始化返回 NULL。
 */
struct XLabel* XRemoteSessionBar_statsLabel(
    const XRemoteSessionBar* self);

#endif /* XWIDGET_ON && XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XREMOTESESSIONBAR_H */
