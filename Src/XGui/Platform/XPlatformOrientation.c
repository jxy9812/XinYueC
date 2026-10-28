/******************************************************************************
 * @file       XPlatformOrientation.c
 * @brief      平台显示方向公共中转（能力缓存 + 影子值 + 参数校验）。
 * @details    契约声明见 XPlatformOrientation.h。本文件是公共层与平台层
 *             之间的唯一中转点，承担三件事：
 *
 *             1. 能力缓存：平台能力是静态属性（安卓 Activity 是否就绪、
 *                X11 是否链上 Xrandr、Win32 是否有可旋转的显示设备），
 *                不随每次调用反复探测；探测一次即缓存。
 *             2. 影子值：记录本应用最近一次**成功受理**的方向申请，供
 *                界面回显——平台侧多数只给「动作下发」而无「设定值
 *                查询」面（安卓 setRequestedOrientation 无返回值、
 *                Win32 DEVMODE 需自行缓存），故公共层持权威副本。
 *             3. 参数校验：合法角度只有 0/90/180/270 与 AUTO（-1），
 *                非法值在进平台层前即被拦下（平台原语对非法角度的
 *                行为未定义：Win32 DEVMODE 写非法 orientation 会得到
 *                不可预期的模式）。
 * @note       本文件不引用任何平台 API（平台调用一律经 Driver_ 钩子），
 *             嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XPlatformOrientation.h"

typedef struct {
    unsigned m_caps;            /**< 能力位缓存（0=尚未就绪，每次重探）。 */
    bool     m_defaultsApplied; /**< 缺省影子值是否已建立。 */
    int      m_lastRequested;    /**< 影子值：最近一次成功受理的申请角
                                 *   （XPLATFORM_ORIENTATION_AUTO=跟随
                                 *   系统）。 */
} XPlatformOrientationState;

static XPlatformOrientationState g_xori;

/** @brief 首次使用时建立缺省影子值。
 *  @details 静态结构体是**零初始化**，而「零」在本契约不是缺省语义：
 *           0 是合法角度「横屏」，若缺省取 0，界面在用户从未申请过时
 *           会回显成「横屏」——而真实缺省是「跟随系统」。故缺省必须
 *           显式建立为 AUTO（零初始化 ≠ 缺省语义，教训同 XPlatformPower
 *           的唤醒来源缺省）。
 */
static void xori_ensureDefaults(void)
{
    if (g_xori.m_defaultsApplied) return;
    g_xori.m_defaultsApplied = true;
    g_xori.m_lastRequested = XPLATFORM_ORIENTATION_AUTO;
}

/** @brief 角度是否合法（0/90/180/270 或 AUTO）。 */
static bool xori_isValidDegrees(int degrees)
{
    if (degrees == XPLATFORM_ORIENTATION_AUTO) return true;
    return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

/* ==================== 公共契约 ==================== */

unsigned XPlatformOrientation_capabilities(void)
{
    xori_ensureDefaults();
    /* 能力缓存，但**0 值不复用**：0 在本契约里是「尚未就绪/查不到」
       （安卓 Activity 迟到就绪、X 连接尚未建立），把它当成最终结论
       缓存下来会让「窗口方向」控件被永久置灰。故 0 时每次重新探测，
       非 0 时才沿用缓存（平台能力在设备生命周期内不变，探测开销可忽略）。 */
    if (g_xori.m_caps == 0u) {
        g_xori.m_caps = XPlatformOrientationDriver_capabilities();
    }
    return g_xori.m_caps;
}

bool XPlatformOrientation_supports(XPlatformOrientationCapability cap)
{
    return (XPlatformOrientation_capabilities() & (unsigned)cap) != 0u;
}

bool XPlatformOrientation_setOrientation(int degrees)
{
    unsigned caps;
    if (!xori_isValidDegrees(degrees)) return false;
    caps = XPlatformOrientation_capabilities();
    if (degrees == XPLATFORM_ORIENTATION_AUTO) {
        /* 「跟随系统」是独立能力：平台可能只支持指定方向、不支持交回
         * 系统（如某些嵌入式旋转开关只能设不能清）。 */
        if ((caps & (unsigned)XPlatformOrientationCap_Auto) == 0u)
            return false;
    } else if ((caps & (unsigned)XPlatformOrientationCap_Request) == 0u) {
        return false;
    }
    if (!XPlatformOrientationDriver_setOrientation(degrees)) return false;
    /* 影子只在受理成功后更新：平台拒绝时不得让界面回显一个没生效的
     * 值（否则界面显示「已转竖屏」而屏幕纹丝不动）。 */
    g_xori.m_lastRequested = degrees;
    return true;
}

int XPlatformOrientation_currentOrientation(void)
{
    if ((XPlatformOrientation_capabilities() &
         (unsigned)XPlatformOrientationCap_Query) == 0u)
        return -1;
    return XPlatformOrientationDriver_currentOrientation();
}

int XPlatformOrientation_lastRequested(void)
{
    xori_ensureDefaults();
    return g_xori.m_lastRequested;
}

#endif /* XGUI_ON */
