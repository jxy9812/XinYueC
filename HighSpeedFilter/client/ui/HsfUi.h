/**
 * @file       HsfUi.h
 * @brief      高速过滤器客户端主窗组装与启动入口。
 * @details    三段式主窗（仿 XGuiWindowDemo 布局化 chrome）：第一行标题栏
 *             （HsfTitleBar）+ 中间堆栈窗口（HsfStack）+ 底部黑色状态栏
 *             （HsfStatusBar），根容器垂直布局。各部件独立模块（ui/ 目录
 *             各自 .h/.c），本模块只做装配与页序语义（切页/标题/状态同步）。
 */
#ifndef HSFUI_H
#define HSFUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

/**
 * @brief 启动客户端 UI：建主窗并进入 GUI 事件循环（阻塞至窗口关闭）。
 * @param autoExitSeconds >0 时该秒数后自动退出（CI/截图验收用）；0 常驻。
 * @param shotPath 非空=离屏截图模式：窗口开到屏外静默渲染，出图存该
 *        路径（UTF-8）后自动退出（不打扰用户桌面）。
 * @param shotWidth 截图模式窗口宽（0=缺省 480）。
 * @param shotHeight 截图模式窗口高（0=缺省 320）。
 * @param startPage 启动页索引（-1=主界面；截图/调试用）。
 * @param thenPage 出图前切到的目标页（-1=不切；验证切页残留用）。
 * @return 事件循环退出码（0=正常）。
 * @note 调用方需已创建 XGuiApplication（main_client.c 负责）。
 */
int HsfUi_start(int autoExitSeconds, const char* shotPath,
                int shotWidth, int shotHeight, int startPage,
                int thenPage);

#ifdef __cplusplus
}
#endif

#endif /* HSFUI_H */
