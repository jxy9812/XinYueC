/******************************************************************************
 * @file       XPlatformPowerBoardProbe.c
 * @brief      板端电源契约探针（真实驱动路径，POSIX/X11 专属）。
 * @details    XPlatformPowerTest 是宿主桩驱动（白盒、只验状态机）；本
 *             探针走**真实平台驱动**（X11 面：libXext DPMS 置屏 + 服务端
 *             DPMS 档位读回），在板子上直接观测「影子态 ↔ 硬件实况」的
 *             收敛过程，用于「外部唤醒后能否再正常休眠」的 A/B 实证。
 *
 *             用法：XPlatformPowerBoardProbe [熄屏超时毫秒，默认 3000]
 *                   运行中从另一终端执行 `xset dpms force on` 模拟外部
 *                   唤醒（VNC/x11vnc 侧唤醒的等价动作），观察：
 *                     - 影子态应从 OFF 翻回 ON（跟随硬件）；
 *                     - 空闲超时后硬件应再次被置为 OFF（能再休眠）。
 *                   旧行为：影子永久卡在 OFF ⇒ 硬件永不再被置 OFF。
 *
 *             输出每 500ms 一行：影子态 + connector 的 dpms 属性（面板
 *             供电真源，板端实测与 DPMS 档位一致）。
 * @note       本探针自带 main()，是独立可执行目标，不得卷入主程序
 *             （见 Test/CMakeLists.txt 的 REMOVE_ITEM 纪律）；POSIX 专属
 *             （Windows 无 X11 DPMS 路径）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformPower.h"

#include "XCoreApplication.h" /* processEvents（驱动平台轮询回调的派发器）。 */
#include "XThread.h"
#include "XPrintf.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>

/** @brief 读已连接 connector 的 dpms 属性（面板供电真源）。
 *  @return "On"/"Off"/"?"（读不到）。 */
static const char* boardConnectorDpms(void)
{
    static char result[16] = "?";
    DIR* dir = opendir("/sys/class/drm");
    struct dirent* ent;
    if (!dir) return result;
    result[0] = '\0';
    while ((ent = readdir(dir)) != NULL) {
        char statusPath[288];
        char dpmsPath[288];
        char buf[32];
        FILE* f;
        if (ent->d_name[0] == '.') continue;
        snprintf(statusPath, sizeof(statusPath), "/sys/class/drm/%s/status",
                 ent->d_name);
        f = fopen(statusPath, "rb");
        if (!f) continue;
        if (!fgets(buf, sizeof(buf), f)) { fclose(f); continue; }
        fclose(f);
        { size_t n = strlen(buf); while (n && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0; }
        if (strcmp(buf, "connected") != 0) continue;
        snprintf(dpmsPath, sizeof(dpmsPath), "/sys/class/drm/%s/dpms",
                 ent->d_name);
        f = fopen(dpmsPath, "rb");
        if (!f) continue;
        if (!fgets(buf, sizeof(buf), f)) { fclose(f); continue; }
        fclose(f);
        { size_t n = strlen(buf); while (n && (buf[n-1]=='\n'||buf[n-1]=='\r')) buf[--n]=0; }
        snprintf(result, sizeof(result), "%s", buf);
    }
    closedir(dir);
    if (!result[0]) snprintf(result, sizeof(result), "?");
    return result;
}

int main(int argc, char** argv)
{
    XPlatformPowerPolicy policy;
    long long screenOffMs = (argc > 1) ? atoll(argv[1]) : 3000;
    bool pushMode = (argc > 2 && strcmp(argv[2], "push") == 0);
    int i;

    if (pushMode) {
        /* 纯平台推送模式：**不调 XPlatformPower_tick()** ⇒ 公共层的 1Hz
         * 对账不参与，影子态只能被「平台侧自观测 → 上报入口」这条链推动
         * （X11 面 200ms 节流轮询服务端 DPMS 档位）。
         * 场景：本探针自己熄屏 → 外部 `xset dpms force on` → 平台推送应
         * 在 ≲200ms 内把影子拉回「亮」。
         * ⚠ 两处前置条件（缺一则推送不发生）：
         *   ① 轮询回调挂在事件分发器的 processEvents 上 ⇒ 必须泵事件；
         *   ② 分发器的取得**经应用单例**（XAbstractEventDispatcher_instance
         *      → XCoreApplication_eventDispatcher → g_app）⇒ 应用实例须
         *      先于**驱动的首次能力探测**存在（平台在能力探测/实况读回
         *      时惰性注册推送；探测早于应用实例 ⇒ 注册被跳过）。故这里
         *      先建应用再建策略/探能力。
         *      建 XCoreApplication 而非 XGuiApplication：后者会挂 1Hz
         *      电源巡检定时器，1Hz 对账就会参与，隔离不出「纯平台推送」。 */
        XCoreApplication* core = XCoreApplication_create(0, NULL);
        if (!core) {
            XPrintf("probe: 核心应用实例创建失败（平台推送需要应用实例）\n");
            return 2;
        }
    }
    XPlatformPower_policy(&policy);
    policy.m_screenOffMs = (int64_t)screenOffMs;
    policy.m_systemSuspendMs = 0;
    policy.m_wakeInputs = (unsigned)XPlatformPowerInputAll;
    XPlatformPower_setPolicy(&policy);
    XPrintf("probe: screenOffMs=%lld caps=0x%x mode=%s\n", screenOffMs,
            XPlatformPower_capabilities(), pushMode ? "push" : "tick");
    if (pushMode) {
        XPlatformPower_setScreenState(XPlatformPowerScreenState_Off);
        for (i = 0; i < 40; ++i) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XPrintf("probe[%02d] shadow=%-3s connector=%s\n", i,
                    XPlatformPower_isScreenOff() ? "OFF" : "ON",
                    boardConnectorDpms());
            XThread_msleep(500);
        }
        return 0;
    }
    for (i = 0; i < 60; ++i) {
        XPlatformPower_tick();
        XPrintf("probe[%02d] shadow=%-3s connector=%s\n", i,
                XPlatformPower_isScreenOff() ? "OFF" : "ON",
                boardConnectorDpms());
        XThread_msleep(500);
    }
    return 0;
}
