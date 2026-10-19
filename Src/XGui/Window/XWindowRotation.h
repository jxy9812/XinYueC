/******************************************************************************
 * @file       XWindowRotation.h
 * @brief      窗口内容四方位旋转的公共换算（0/90/180/270，纯 inline）。
 * @details    本头文件是**像素下标口径**的旋转换算唯一实现点，供
 *             X11 上屏（XPlatformNativeWindow_posix.c）、fbdev 直写
 *             （XPlatformBackingStore_posix.c）、fbdev 触摸输入
 *             （XPlatformFbInput_posix.c）与 XWidget 坐标映射共用，
 *             保证呈现与命中两端口径逐位一致。
 *
 *             口径定义（**与 XScreen_transformBetween 差一个 -1，严禁混用**）：
 *               - 本文件：像素**下标**语义。W/H 取整窗逻辑宽高，正变换把
 *                 逻辑像素 (x,y) 映到设备像素下标。
 *               - XScreen.h:578-582（实现 XScreen.c case 90）：连续坐标仿射
 *                 语义，90° 为 x'=w-y, y'=x，**无 -1**。两套坐标语义不同，
 *                 混用会产生整窗 1px 错位。
 *
 *             deg 语义：顺时针旋转屏幕内容（右手系正面观察）：
 *               0°   恒等；(Wd,Hd)=(W,H)
 *               90°  (x,y) → (H-1-y, x)        ；(Wd,Hd)=(H,W)
 *               180° (x,y) → (W-1-x, H-1-y)    ；(Wd,Hd)=(W,H)
 *               270° (x,y) → (y, W-1-x)        ；(Wd,Hd)=(H,W)
 *
 *             ⚠ 旋转只对调宽高，**原点（窗口 x/y）不随旋转变**——这是全部
 *             几何推导（footprint / 命中测试 / 输入减原点）的基石。
 * @note       全部函数无状态、无分配、无副作用；头文件 inline 可被任意
 *             平台层直接包含，不引入额外链接依赖。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XWINDOW_ROTATION_H
#define XWINDOW_ROTATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/** @brief 角度归一：把任意整数折算到 0/90/180/270；非法角度返回 false。 */
static inline bool XWindow_rotationNormalize(int deg, int* outDeg)
{
    int d;
    if (!outDeg) return false;
    /* 先按 360 取模再判合法：deg=360/-90 等价 0/270 需先归一。 */
    d = deg % 360;
    if (d < 0) d += 360;
    if (d != 0 && d != 90 && d != 180 && d != 270) return false;
    *outDeg = d;
    return true;
}

/** @brief 该角度是否对调宽高轴（90/270 为 true）。
 *  @note  公共薄封装 XWindow_rotationSwapsAxes（XWindow.c）转调本函数，
 *         避免同名 inline 与 extern 符号冲突。 */
static inline bool XWindow_rotationSwapsAxesInline(int deg)
{
    return deg == 90 || deg == 270;
}

/** @brief 旋转后设备侧尺寸：90/270 对调 W/H，其余恒等。 */
static inline void XWindow_rotationDeviceSize(int deg, int W, int H,
                                              int* outW, int* outH)
{
    if (!outW || !outH) return;
    if (XWindow_rotationSwapsAxesInline(deg)) { *outW = H; *outH = W; }
    else { *outW = W; *outH = H; }
}

/** @brief 正变换：逻辑像素下标 → 设备像素下标。 @param W 整窗逻辑宽 @param H 整窗逻辑高。 */
static inline void XWindow_rotationMapPoint(int deg, int W, int H,
                                            int x, int y, int* outX, int* outY)
{
    int nx = x;
    int ny = y;
    switch (deg) {
    case 90:  nx = H - 1 - y; ny = x;               break;
    case 180: nx = W - 1 - x; ny = H - 1 - y;       break;
    case 270: nx = y;         ny = W - 1 - x;       break;
    default: break;                                  /* 0（含非法角）：恒等 */
    }
    if (outX) *outX = nx;
    if (outY) *outY = ny;
}

/** @brief 逆变换：设备像素下标 → 逻辑像素下标（present 落笔 / 输入命中共用）。
 *  @details **不做任何钳位**：负坐标域合法（按住拖出窗界的输入路径依赖
 *           -1 式自然连续外插，见 XPlatformFbInput_posix.c 注）。 */
static inline void XWindow_rotationUnmapPoint(int deg, int W, int H,
                                              int x, int y, int* outX, int* outY)
{
    int nx = x;
    int ny = y;
    switch (deg) {
    case 90:  nx = y;         ny = H - 1 - x;       break;
    case 180: nx = W - 1 - x; ny = H - 1 - y;       break;
    case 270: nx = W - 1 - y; ny = x;               break;
    default: break;
    }
    if (outX) *outX = nx;
    if (outY) *outY = ny;
}

/** @brief 正变换：逻辑矩形 → 设备矩形（轴对齐；旋转 90/270 宽高对调）。
 *  @details 面积守恒，逐字段与正变换逐像素一致。 */
static inline void XWindow_rotationMapRect(int deg, int W, int H,
                                           const int* inX, const int* inY,
                                           const int* inW, const int* inH,
                                           int* outX, int* outY,
                                           int* outW, int* outH)
{
    int x, y, w, h;
    if (!inX || !inY || !inW || !inH) return;
    x = *inX; y = *inY; w = *inW; h = *inH;
    switch (deg) {
    case 90:
        if (outX) *outX = H - y - h;
        if (outY) *outY = x;
        if (outW) *outW = h;
        if (outH) *outH = w;
        return;
    case 180:
        if (outX) *outX = W - x - w;
        if (outY) *outY = H - y - h;
        if (outW) *outW = w;
        if (outH) *outH = h;
        return;
    case 270:
        if (outX) *outX = y;
        if (outY) *outY = W - x - w;
        if (outW) *outW = h;
        if (outH) *outH = w;
        return;
    default:
        if (outX) *outX = x;
        if (outY) *outY = y;
        if (outW) *outW = w;
        if (outH) *outH = h;
        return;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* XWINDOW_ROTATION_H */
