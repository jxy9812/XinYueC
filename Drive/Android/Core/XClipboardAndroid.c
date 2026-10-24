/******************************************************************************
 * @file       XClipboardAndroid.c
 * @brief      XClipboard 安卓平台后端（JNI ClipboardManager 单格式文本）。
 * @details    [2026-10-24 安卓剪贴板接入] 把系统 ClipboardManager 接进
 *             XClipboardBackend 契约（对标 win32 xpwn 后端的最小文本形态）：
 *             - text/setText/clear：纯文本读写（getPrimaryClip 首项
 *               coerceToText 宽收 / setPrimaryClip(newPlainText)）；
 *             - formats/mimeData/setMimeData：仅协商 "text/plain"（安卓
 *               剪贴板无多格式枚举语义，格式恒单一）；
 *             - Selection：安卓无 PRIMARY 选择区概念，supportsSelection
 *               恒 false，mode!=Clipboard 的调用一律按 no-op/空处理。
 *             底层 JNI 在 XSystemAndroid.c（XAndroid_clipboard* 三件套），
 *             VM/Activity 引用由壳层注入。未注入 VM（无 Java 环境）时
 *             各回调返回失败，上层自动回落进程内存储（零回归）。
 * @note       平台 API 只出现在 Drive；本文件仅在 __ANDROID__ 下编译出
 *             实体，其他平台整文件为空。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformNativeWindow.h"

#if defined(__ANDROID__) && XGUI_ON

#include "XClipboard.h"
#include "XMemory.h"
#include <string.h>

/* XSystemAndroid.c 提供的 JNI 三件套。 */
bool XAndroid_clipboardGetText(char** outText);
bool XAndroid_clipboardSetText(const char* utf8);
bool XAndroid_clipboardClear(void);

static bool xclip_android_text(void* ud, int mode, char** outText)
{
    (void)ud;
    if (!outText || mode != (int)XClipboardMode_Clipboard) return false;
    return XAndroid_clipboardGetText(outText);
}

static bool xclip_android_setText(void* ud, int mode, const char* text)
{
    (void)ud;
    if (mode != (int)XClipboardMode_Clipboard) return true; /* 选择区 no-op */
    return XAndroid_clipboardSetText(text);
}

static bool xclip_android_clear(void* ud, int mode)
{
    (void)ud;
    if (mode != (int)XClipboardMode_Clipboard) return true;
    return XAndroid_clipboardClear();
}

static int xclip_android_formats(void* ud, int mode,
                                 char outFormats[][64], int max)
{
    char* text = NULL;
    int count = 0;
    (void)ud;
    if (!outFormats || max <= 0 || mode != (int)XClipboardMode_Clipboard)
        return 0;
    /* 安卓剪贴板无法廉价探测"是否非空"（读全文才可知）；有文本即申报
       text/plain，读失败/空板不申报（与 win32 IsClipboardFormatAvailable
       的门控语义等价）。 */
    if (XAndroid_clipboardGetText(&text) && text && text[0] != '\0') {
        strncpy(outFormats[0], "text/plain", 63);
        outFormats[0][63] = '\0';
        count = 1;
    }
    if (text) XFree_System(text);
    return count;
}

static bool xclip_android_mimeData(void* ud, int mode, const char* format,
                                   const unsigned char** data, int* len)
{
    char* text = NULL;
    (void)ud;
    if (!data || !len || mode != (int)XClipboardMode_Clipboard) return false;
    *data = NULL;
    *len = 0;
    if (!format || strcmp(format, "text/plain") != 0) return false;
    if (!XAndroid_clipboardGetText(&text) || !text) return false;
    /* 借用语义：静态镜像持有最近一次读出文本，下次后端调用前有效
       （剪贴板单格式模型下镜像恒最新，无失效窗口）。 */
    {
        static char* s_mirror = NULL;
        if (s_mirror) XFree_System(s_mirror);
        s_mirror = text;
        *data = (const unsigned char*)s_mirror;
        *len = (int)strlen(s_mirror);
    }
    return true;
}

static bool xclip_android_setMimeData(void* ud, int mode, const char* format,
                                      const unsigned char* data, int len)
{
    char* text;
    bool ok;
    (void)ud;
    if (mode != (int)XClipboardMode_Clipboard) return true;
    if (!format || strcmp(format, "text/plain") != 0) return false;
    if (len < 0) return false;
    if (len == 0) return XAndroid_clipboardClear();
    text = (char*)XMalloc_System((size_t)len + 1u);
    if (!text) return false;
    memcpy(text, data, (size_t)len);
    text[len] = '\0';
    ok = XAndroid_clipboardSetText(text);
    XFree_System(text);
    return ok;
}

static const XClipboardBackend g_xclipAndroidBackend = {
    NULL,                     /* ud */
    xclip_android_text,
    xclip_android_setText,
    xclip_android_clear,
    false,                    /* supportsSelection：安卓无 PRIMARY */
    NULL,                     /* selectionRevoked */
    xclip_android_formats,
    xclip_android_mimeData,
    xclip_android_setMimeData,
    NULL                      /* setIncrTimeoutMs：无 INCR 语义 */
};

void XAndroidClipboard_installBackend(void);

/** @brief 安装安卓剪贴板后端（XPlatformNativeWindow_installClipboardBackend
 *         转发；应用初始化帧调用一次）。 */
void XAndroidClipboard_installBackend(void)
{
    XClipboard_installBackend(&g_xclipAndroidBackend);
}

#endif /* __ANDROID__ && XGUI_ON */
