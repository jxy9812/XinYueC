/**
 * @file       HsfMonitorDigital.h
 * @brief      数字量监视/操作页：圆点网格看板（DI/DO 分区段）+ 统一
 *             操作闸 + 全量 API。
 * @details    页面为「看板」：数字量点位的状态/名称/数量/通道号由业务
 *             层经本头公开 API 写入（内存模型随点数惰性重建），点击
 *             操作只在 HsfMonitorDigital_setOperationEnabled 放行且
 *             点位为输出（DO）时经 HsfMonitorDigital_setWriteHandler
 *             回调业务层（UI 不落任何业务语义，恒为回显模型 + 回调
 *             出口）。看板分两区段显示：输入（DI）区在上、输出（DO）
 *             区在下，各自成行不混排，空区段整段隐藏；绘制走文件内
 *             自定义控件（paintEvent 自绘 + mousePressEvent 点击，
 *             对标 Qt 的 QWidget 子类习惯），外层复用主窗统一滚动壳。
 *             兼容自测钩子：HsfMonitorDigital_autotest（环境变量
 *             XHSF_DIGITAL_AUTOTEST 触发，走 API 写入验证回显，不改
 *             业务层语义）。
 */
#ifndef HSFMONITORDIGITAL_H
#define HSFMONITORDIGITAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** @brief 单点数字量状态（圆点颜色语义）。 */
typedef enum HsfDigitalState
{
    HSF_DIGITAL_OFF = 0, /**< 断开（红）。 */
    HSF_DIGITAL_ON  = 1  /**< 通（绿）。 */
} HsfDigitalState;

/**
 * @brief 单点数字量类型（输入/输出，决定 UI 形态与可否界面操作）。
 * @details 输入（DI）只读监视：UI 画**空心环**+「DI」标签，点击不产生
 *          写操作；输出（DO）可操作：UI 画**实心圆**+「DO」标签，统一
 *          闸放行时点击经写回调出 UI（写出口还须已注册）。
 */
typedef enum HsfDigitalKind
{
    HSF_DIGITAL_INPUT  = 0, /**< 输入（DI）：只读。 */
    HSF_DIGITAL_OUTPUT = 1  /**< 输出（DO）：可操作（仍受统一闸约束）。 */
} HsfDigitalKind;

/**
 * @brief      写操作回调（用户点击 DO 圆点且统一闸放行时触发；业务层在
 *             此对接真实下发）。
 * @param userData setWriteHandler 透传的用户数据。
 * @param index  点位下标（0 起始，恒为 [0,count) 内合法值）。
 * @param target 请求目标态（HSF_DIGITAL_ON/OFF）。
 * @note  回调返回后 UI 侧不自动翻转状态：业务层确认后应显式回写
 *        HsfMonitorDigital_setValue（真实通道是异步的，回显以业务为准）。
 */
typedef void (*HsfDigitalWriteFn)(void* userData, int index,
                                  HsfDigitalState target);

/**
 * @brief      创建数字量叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（builders 契约保留；本页返回语义由导航树承担）。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfMonitorDigital_create(XWidget* parent, HsfMenuNavFn navFn,
                                  void* userData);

/**
 * @brief      形态自适应（HsfUi adapt 表按页调用；横竖屏分档）。
 * @details    竖屏：列宽收窄/触控目标加高/边距收窄/顶行纵向堆叠；
 *             横屏恢复宽列与横向顶行。幂等、廉价，可重复调用。
 * @param page 页根控件（HsfMonitorDigital_create 返回值）。
 * @param compact true=紧凑/竖屏形态（false=宽屏）。
 */
void HsfMonitorDigital_adapt(XWidget* page, bool compact);

/* ---- 数据供给 API（业务层唯一入口；调用前页可未创建，安全空操作） ---- */

/**
 * @brief      设置数字量点数（读写）。
 * @details    模型惰性重建：扩点新增 OFF、缩点截尾；已建页立即重排重绘。
 * @param count 新点数；<=0 视为 0（清空看板）。
 */
void HsfMonitorDigital_setCount(int count);

/** @brief 取当前数字量点数（未建页返回 0）。 */
int HsfMonitorDigital_count(void);

/**
 * @brief      写单点状态（读写 API；业务层轮询/订阅回显用）。
 * @param index 点位下标；越界忽略。
 * @param state 目标态（断开=红 / 通=绿）。
 */
void HsfMonitorDigital_setValue(int index, HsfDigitalState state);

/** @brief 读单点状态；越界返回 HSF_DIGITAL_OFF。 */
HsfDigitalState HsfMonitorDigital_value(int index);

/**
 * @brief      写单点类型（读写 API；DI=只读空心环 / DO=可写实心圆）。
 * @details    新建点缺省 DO（可操作）；随点数扩容新增的点同为 DO。
 *             看板显示分两个区段：DI 区在上、DO 区在下，各点归入其
 *             类型区段（改类型即时挪段）。
 * @param index 点位下标；越界忽略。
 * @param kind  目标类型。
 */
void HsfMonitorDigital_setKind(int index, HsfDigitalKind kind);

/** @brief 读单点类型；越界返回 HSF_DIGITAL_OUTPUT。 */
HsfDigitalKind HsfMonitorDigital_kind(int index);

/**
 * @brief      写单点名称（UTF-8；内部深拷贝）。越界忽略。
 * @param index 点位下标。
 * @param name  名称文本；NULL 置空（显示退化为「点 N」缺省名）。
 */
void HsfMonitorDigital_setName(int index, const char* name);

/**
 * @brief      读单点名称到调用方缓冲。
 * @param index 点位下标；越界输出缺省名「点 N」。
 * @param buf   输出缓冲（UTF-8，恒 NUL 收尾）；NULL 忽略。
 * @param size  缓冲字节数；<=0 忽略。
 */
void HsfMonitorDigital_name(int index, char* buf, int size);

/**
 * @brief      写单点通道号（读写 API；显示在名称前，如「CH3 泵启停」）。
 * @param index 点位下标；越界忽略。
 * @param channel 通道号；<0 清除（不显示通道前缀）。
 */
void HsfMonitorDigital_setChannel(int index, int channel);

/** @brief 读单点通道号；越界或未设置返回 -1。 */
int HsfMonitorDigital_channel(int index);

/**
 * @brief      统一操作闸（读写 API）：是否允许界面操作修改数字量。
 * @details    false（缺省）时点击 DO 圆点仅在状态栏提示「操作被禁止」；
 *             true 时点击经写回调出 UI。只影响操作放行，不影响回显；
 *             DI 点恒只读，不受本闸影响。
 * @param enabled true=允许界面操作；false=只读监视。
 */
void HsfMonitorDigital_setOperationEnabled(bool enabled);

/** @brief 查询统一操作闸当前状态。 */
bool HsfMonitorDigital_operationEnabled(void);

/**
 * @brief      界面交互模式（读写 API；对标「自动运行锁界面」场景）。
 * @details    interactive=false（自动运行中）：勾选框整体隐藏、操作闸
 *             强制只读（API setOperationEnabled 也被锁），看板仅可
 *             查看，点击 DO 圆点在状态栏提示「自动运行中: 界面只读」；
 *             interactive=true（缺省）：恢复显示勾选框，闸值回到勾选框
 *             此前状态。仅影响操作放行与闸 UI 显隐，不影响状态回显。
 * @param interactive true=允许界面交互（手动模式）；false=自动运行只读。
 */
void HsfMonitorDigital_setInteractiveMode(bool interactive);

/** @brief 查询界面交互模式（true=可交互；false=自动运行只读）。 */
bool HsfMonitorDigital_interactiveMode(void);

/**
 * @brief      注册写操作出口（UI → 业务层唯一通道；多次注册以最后为准）。
 * @param handler 写回调；NULL=注销（此时即使闸放行也不出 UI）。
 * @param userData 透传回调的用户数据。
 */
void HsfMonitorDigital_setWriteHandler(HsfDigitalWriteFn handler,
                                       void* userData);

/**
 * @brief      状态栏反馈挂接（由 HsfUi.c 在建页后调用；业务勿用）。
 * @param setStatus HsfPages_setStatus 同款回调；可 NULL（断开反馈）。
 * @param user     归属主窗指针。
 */
void HsfMonitorDigital_attachStatus(void (*setStatus)(void*, const char*),
                                    void* user);

#if XGUI_ON
/**
 * @brief      兼容自测钩子（截图/CI 用；业务勿用）。
 * @details    环境变量 XHSF_DIGITAL_AUTOTEST=1 时执行：走公开 API 灌
 *             16 点演示数据（名称/交错红绿），操作闸置开，返回写入
 *             点数；未触发返回 -1。仅改 UI 模型，不碰业务层。
 * @param page 页根控件（HsfMonitorDigital_create 返回值；仅校验非 NULL）。
 * @return     执行时灌入的点数（>=0）；未触发 -1。
 */
int HsfMonitorDigital_autotest(XWidget* page);
#endif /* XGUI_ON */

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFMONITORDIGITAL_H */
