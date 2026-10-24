/* ==================== 用户业务代码（模板） ====================
 * 覆盖 app.c 的两个弱符号即可接入：
 *   user_setup : 事件循环启动前调用一次（XCoreApplication/shell/WiFi 已就绪）
 *   user_tick  : 事件循环每拍（~5ms）调用；只做非阻塞轮询，勿长阻塞
 *
 * 可用设施：
 *   - XMemory：XNew/XDelete 或 XMalloc_Hybrid/XFree_Hybrid（小对象走
 *     MultiPool 快路径，大对象走 arena）
 *   - 定时器：XTimer/XTimeWheelGroup（不要在 tick 里自建循环延时）
 *   - 网络：XDeviceNetwork_*（SoftAP 已由 app.c 拉起，lwip 就绪）
 *   - 串口：shell 已挂 UART0 115200
 *
 * 下面给出最小示例：注册一个 1s 周期业务定时器 + 一个计数 tick。
 */
#include "XMemory.h"

static unsigned int biz_count = 0;

void user_setup(void)
{
    /* TODO: 业务初始化——创建对象、注册定时器、启动客户端… */
    biz_count = 0;
}

void user_tick(void)
{
    /* TODO: 每拍非阻塞轮询（~5ms 一拍） */
    if ((++biz_count & 0xFFu) == 0u) {
        /* 约 1.28s 一次：放你的周期业务 */
    }
}
