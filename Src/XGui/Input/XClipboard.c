/******************************************************************************
 * @file       XClipboard.c
 * @brief      XClipboard 剪贴板类实现（对标 Qt 6.8 QClipboard）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XClipboard.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"
#include "XGuiApplication.h"

#if XCLIPBOARD_ON

#include "XPixmap.h"
#include "XImageCodec.h"
/** @brief 单个剪贴板模式的数据单元。 */
static XClipboardBackend g_clipboardBackend;
static int g_clipboardBackendInstalled = 0;
/** @brief 当前生效的 INCR 读超时（前端进程内记录，装后端时下发；
 *  先 set 后 install 的顺序同样生效）。 */
static int g_clipboardIncrTimeoutMs = XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS;

void XClipboard_installBackend(const XClipboardBackend* backend)
{
    if (backend) {
        g_clipboardBackend = *backend;
        g_clipboardBackendInstalled = 1;
        if (g_clipboardBackend.setIncrTimeoutMs)
            g_clipboardBackend.setIncrTimeoutMs(g_clipboardBackend.ud,
                                                g_clipboardIncrTimeoutMs);
    } else {
        XMemset(&g_clipboardBackend, 0, sizeof(g_clipboardBackend));
        g_clipboardBackendInstalled = 0;
    }
}

void XClipboard_setIncrTimeoutMs(int ms)
{
    g_clipboardIncrTimeoutMs = (ms > 0) ? ms : XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS;
    if (g_clipboardBackendInstalled && g_clipboardBackend.setIncrTimeoutMs)
        g_clipboardBackend.setIncrTimeoutMs(g_clipboardBackend.ud,
                                            g_clipboardIncrTimeoutMs);
}

int XClipboard_incrTimeoutMs(void)
{
    return g_clipboardIncrTimeoutMs;
}

static bool xclipboard_backendActive(void)
{
    return g_clipboardBackendInstalled && g_clipboardBackend.text != NULL;
}

typedef struct XClipboardModeData
{
    XString*   m_text; /**< 纯文本（深拷贝）。 */
    XMimeData* m_mime; /**< MIME 数据（拥有）。 */
    bool       m_owns; /**< 数据是否由本进程写入。 */
    bool       m_externalMerged; /**< 非本进程所有时是否已并入后端外部格式（只做一次，避免每次读取往返 TARGETS）。 */
} XClipboardModeData;

/** @brief XClipboard 私有数据块。 */
struct XClipboardPrivate
{
    XClipboardModeData m_modes[XClipboardMode_LastMode + 1]; /**< 三种模式各自的数据。 */
};

/** @brief 释放单个模式单元的全部资源。 */
static void clipboard_clearModeData(XClipboardModeData* data)
{
    if (!data)
        return;
    if (data->m_text) { XString_delete_base(data->m_text); data->m_text = NULL; }
    if (data->m_mime) { XMimeData_delete_base(data->m_mime); data->m_mime = NULL; }
    data->m_owns = false;
    data->m_externalMerged = false;
}

static void VXClipboard_deinit(XClipboard* self)
{
    int mode;
    if (!self) return;
    if (self->m_data) {
        for (mode = 0; mode <= (int)XClipboardMode_LastMode; ++mode)
            clipboard_clearModeData(&self->m_data->m_modes[mode]);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XClipboard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XClipboard)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClipboard_deinit);
    return XVTABLE_DEFAULT;
}

void XClipboard_init(XClipboard* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(XClipboard));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XClipboard);
    self->m_data = (XClipboardPrivate*)XMalloc_System(sizeof(XClipboardPrivate));
    if (!self->m_data) return;
    XMemset(self->m_data, 0, sizeof(XClipboardPrivate));
}

XClipboard* XClipboard_create_ex(XMemoryType memory)
{
    XClipboard* self = (XClipboard*)XMemory_malloc(sizeof(XClipboard), memory);
    if (!self) return NULL;
    XClipboard_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/** @brief 校验模式取值范围；越界归一到 Clipboard。 */
static XClipboardMode clipboard_normalizeMode(XClipboardMode mode)
{
    if (mode < XClipboardMode_Clipboard || mode > XClipboardMode_LastMode)
        return XClipboardMode_Clipboard;
    return mode;
}

/** @brief 发射信号并管理参数列表生命周期（与 XScreen/XWindow 相同模式）。 */
static void clipboard_emit(XClipboard* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

/** @brief 数据变更统一出口：先发模式专用信号，再发 changed（与 Qt 顺序一致）。 */
static void clipboard_emitChanged(XClipboard* self, XClipboardMode mode)
{
    if (!self)
        return;
    if (mode == XClipboardMode_Clipboard)
        XClipboard_dataChanged_signal(self);
    else if (mode == XClipboardMode_Selection)
        XClipboard_selectionChanged_signal(self);
    else if (mode == XClipboardMode_FindBuffer)
        XClipboard_findBufferChanged_signal(self);
    XClipboard_changed_signal(self, mode);
}

void XClipboard_backendSelectionRevoked(void* ud, int mode)
{
    XGuiApplication* app;
    XClipboard* self;
    (void)ud;
    if (mode < XClipboardMode_Clipboard || mode > (int)XClipboardMode_LastMode)
        return;
    /* 定位进程内剪贴板单例但不惰性创建：所有权被夺之前必然执行过
     * setText，即单例已存在（对标 QXcbClipboard 直接持有 m_clipboard，
     * C 分层下经应用单例间接定位）。 */
    app = XGuiApplication_instance();
    if (!app || !app->m_clipboard || !app->m_clipboard->m_data)
        return;
    self = app->m_clipboard;
    /* 对标 QXcbClipboard::handleSelectionClearRequest：清空 ownerData
     * 并 setMimeData(nullptr, mode)——这里复位 owns 与该模式全部数据，
     * 再按 Qt 顺序发射模式专用信号与 changed(mode)。 */
    clipboard_clearModeData(&self->m_data->m_modes[mode]);
    clipboard_emitChanged(self, (XClipboardMode)mode);
}

bool XClipboard_supportsSelection(const XClipboard* self)
{
    (void)self;
    /* 对标 QPlatformClipboard::supportsSelection：能力由平台后端声明，
     * X11 后端接入 PRIMARY 选择区时置 true。 */
    return xclipboard_backendActive() && g_clipboardBackend.supportsSelection;
}

bool XClipboard_supportsFindBuffer(const XClipboard* self)
{
    (void)self;
    return false;
}

bool XClipboard_ownsClipboard(const XClipboard* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_modes[XClipboardMode_Clipboard].m_owns;
}

bool XClipboard_ownsSelection(const XClipboard* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_modes[XClipboardMode_Selection].m_owns;
}

bool XClipboard_ownsFindBuffer(const XClipboard* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_modes[XClipboardMode_FindBuffer].m_owns;
}

void XClipboard_clear(XClipboard* self, XClipboardMode mode)
{
    XClipboardModeData* data;
    if (!self || !self->m_data)
        return;
    mode = clipboard_normalizeMode(mode);
    data = &self->m_data->m_modes[mode];
    if (xclipboard_backendActive() && g_clipboardBackend.clear)
        g_clipboardBackend.clear(g_clipboardBackend.ud, (int)mode);
    clipboard_clearModeData(data);
    clipboard_emitChanged(self, mode);
}

XString* XClipboard_text(XClipboard* self, XClipboardMode mode)
{
    XClipboardModeData* data;
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    data = &self->m_data->m_modes[mode];
    if (xclipboard_backendActive() && g_clipboardBackend.text) {
        char* raw = NULL;
        if (g_clipboardBackend.text(g_clipboardBackend.ud, (int)mode,
                                    &raw) && raw) {
            XString* out = XString_create_utf8(raw);
            XFree_System(raw);
            return out;
        }
    }
    if (data->m_text)
        return XString_create_copy(data->m_text);
#if XMIMEDATA_ON
    /* Qt 通过 mimeData()->text() 读取 setMimeData() 写入的纯文本。 */
    if (data->m_mime && XMimeData_hasText(data->m_mime))
        return XMimeData_text(data->m_mime);
#endif /* XMIMEDATA_ON */
    return NULL;
}

/** @brief 大小写不敏感的 ASCII 字符串比较（子类型名匹配，MIME 类型名规则）。 */
static int clipboard_asciiIcmp(const char* a, const char* b)
{
    unsigned char ca, cb;
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    while (*a && *b) {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return (int)ca - (int)cb;
        ++a; ++b;
    }
    ca = (unsigned char)*a; cb = (unsigned char)*b;
    if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
    return (int)ca - (int)cb;
}

#if XMIMEDATA_ON
/** @brief 拼接 "text/<subtype>" 完整 MIME 名；超长或子类型为空返回 false。 */
static bool clipboard_buildTextMimeName(const char* subtype, char* buffer,
                                        size_t capacity)
{
    const char* prefix = "text/";
    size_t i = 0;
    size_t j = 0;
    if (!subtype || !subtype[0] || !buffer || capacity < 6)
        return false;
    while (prefix[i] && i < capacity - 1) {
        buffer[i] = prefix[i];
        ++i;
    }
    while (subtype[j] && i < capacity - 1) {
        buffer[i] = subtype[j];
        ++i; ++j;
    }
    buffer[i] = '\0';
    /* 子类型必须完整拷入（留出结束符），否则视为查询失败。 */
    return subtype[j] == '\0' && i < capacity - 1;
}
#endif /* XMIMEDATA_ON */

XString* XClipboard_text_subtype(XClipboard* self, XString** subtype,
                              XClipboardMode mode)
{
    XClipboardModeData* data;
    const char* requested = NULL;
    XString* text;
#if XMIMEDATA_ON
    /* 先读显式请求（对标 QClipboard::text(QString&, Mode) 的 in/out
     * subtype）：非空 *subtype 表示只尝试 "text/<请求子类型>"；此时
     * 不清空调用方指针（原地复用，调用方继续持有所有权）。 */
    if (subtype && *subtype) {
        requested = XString_toUtf8(*subtype);
        if (requested && !requested[0])
            requested = NULL;
    }
#endif /* XMIMEDATA_ON */
    if (!self || !self->m_data) {
        if (subtype)
            *subtype = NULL;
        return NULL;
    }
    if (!requested && subtype)
        *subtype = NULL; /* 空请求：先复位输出（无文本置 NULL 契约）。 */
    mode = clipboard_normalizeMode(mode);
    data = &self->m_data->m_modes[mode];

    if (requested && clipboard_asciiIcmp(requested, "plain") != 0) {
        /* 显式请求非 plain 子类型（如 "html"）：按 Qt 语义查
         * foundType = "text/" + subtype，命中才返回；*subtype 原地复用
         * （Qt 的 QString& 语义），调用方继续持有所有权。 */
#if XMIMEDATA_ON
        char wanted[128];
        if (data->m_mime &&
            clipboard_buildTextMimeName(requested, wanted, sizeof(wanted))) {
            text = XMimeData_data(data->m_mime, wanted);
            if (text)
                return text;
        }
#endif /* XMIMEDATA_ON */
        return NULL;
    }

    /* 1) 纯文本链（text/plain 优先，对标 Qt formats() 顺序）。 */
    if (data->m_text)
        text = XString_create_copy(data->m_text);
#if XMIMEDATA_ON
    else if (data->m_mime && XMimeData_hasText(data->m_mime))
        text = XMimeData_text(data->m_mime);
#endif /* XMIMEDATA_ON */
    else
        text = NULL;
    if (text) {
        if (subtype)
            *subtype = XString_create_utf8("plain");
        return text;
    }
    if (requested)
        return NULL; /* 显式请求 plain 而无 plain：不继续回退 html。 */

#if XMIMEDATA_ON
    /* 2) 空请求且无纯文本时回退 text/html（对标 Qt text(QString&, Mode)
     * 依次尝试 "text/*" 格式的顺序：formats() 中 plain 在前、html 在后）。 */
    if (data->m_mime && XMimeData_hasHtml(data->m_mime)) {
        text = XMimeData_html(data->m_mime);
        if (text) {
            if (subtype)
                *subtype = XString_create_utf8("html");
            return text;
        }
    }
#endif /* XMIMEDATA_ON */
    return NULL;
}

void XClipboard_setText(XClipboard* self, const XString* text, XClipboardMode mode)
{
#if XMIMEDATA_ON
    XMimeData* mime;
    if (!self || !self->m_data)
        return;
    if (xclipboard_backendActive() && g_clipboardBackend.setText) {
        /* 平台后端接管 OS 剪贴板；进程内镜像仍保留（本进程内
         * 复制粘贴回环不再依赖平台往返）。 */
        const char* utf8 = text ? XString_toUtf8(text) : "";
        g_clipboardBackend.setText(g_clipboardBackend.ud, (int)mode, utf8);
    }
    /* QClipboard::setText() 的 Qt 实现先构造 QMimeData，再转移所有权。 */
    mime = XMimeData_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!mime)
        return;
    XMimeData_setText(mime, text);
    XClipboard_setMimeData(self, mime, mode);
#else
    XClipboardModeData* data;
    if (!self || !self->m_data)
        return;
    mode = clipboard_normalizeMode(mode);
    data = &self->m_data->m_modes[mode];
    clipboard_clearModeData(data);
    data->m_text = XString_create_copy(text);
    if (!data->m_text)
        data->m_text = XString_create_utf8("");
    data->m_owns = true;
    clipboard_emitChanged(self, mode);
#endif /* XMIMEDATA_ON */
}

const XMimeData* XClipboard_mimeData(const XClipboard* self, XClipboardMode mode)
{
    XClipboardModeData* slot;
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    slot = &self->m_data->m_modes[mode];
#if XMIMEDATA_ON
    /* 对标 QClipboard::mimeData() 读取外部数据：Qt 直接返回平台后端
     * 的 QMimeData（QXcbClipboard::mimeData 返回 m_systemClip）。这里
     * 在非本进程所有时惰性地把后端 formats 枚举并入进程内镜像：格式
     * 名单来自后端 formats 回调，字节经 mimeData 回调借用后接收缓冲
     * 拷入。本进程所有（m_owns）或已合并过（只合并一次，避免每次读
     * 取都往返 TARGETS；对标 Qt 系统剪贴板内容变化经事件驱动刷新，
     * 本批刷新时机跟随 clear/revoke 复位）时跳过。 */
    if (!slot->m_owns && !slot->m_externalMerged &&
        xclipboard_backendActive() && g_clipboardBackend.formats) {
        char fmts[XCLIPBOARD_MAX_FORMATS][XCLIPBOARD_FORMAT_NAME_MAX];
        int n = g_clipboardBackend.formats(g_clipboardBackend.ud, (int)mode,
                                           fmts, XCLIPBOARD_MAX_FORMATS);
        int i;
        slot->m_externalMerged = true;
        if (n > 0) {
            if (!slot->m_mime)
                slot->m_mime = XMimeData_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            for (i = 0; slot->m_mime && i < n; ++i) {
                const unsigned char* bytes = NULL;
                int len = 0;
                if (!fmts[i][0] || XMimeData_hasFormat(slot->m_mime, fmts[i]))
                    continue;
                if (!g_clipboardBackend.mimeData ||
                    !g_clipboardBackend.mimeData(g_clipboardBackend.ud,
                                                 (int)mode, fmts[i],
                                                 &bytes, &len) ||
                    !bytes || len <= 0)
                    continue;
                {
                    /* 对标 QMimeData 的 QByteArray 载荷：外部字节经
                     * setData_bytes 深拷贝入 XByteArray 通道，逐字节透明
                     * （image/png 的 0x89 魔数等二进制不再经 XString 的
                     * UTF-8 转换，写入读回精确）。 */
                    XMimeData_setData_bytes(slot->m_mime, fmts[i], bytes, len);
                }
            }
        }
    }
#endif /* XMIMEDATA_ON */
    return slot->m_mime;
}

#if XMIMEDATA_ON
#if XIMAGECODEC_ON
/** @brief 把自有图像（application/x-qt-image）PNG 编码后推送平台镜像。
 *  @details 对标 Qt：QClipboard::setImage 后 QXcbClipboard 经图像插件把
 *           QMimeData 内的图像编码为 image/png 原子登记进 TARGETS，外部
 *           应用因此能看到 image/png 并按需取回字节。本框架复用库内
 *           XImageCodec 自研 PNG 编码算法（XIMAGECODEC_PNG_ON 裁剪门控，
 *           canEncode 运行时复核；编码失败时平台侧不出现图像原子，
 *           进程内自有 mime 语义不受影响）。
 *           注意：Qt 的 QMimeData::formats() 不列出 application/x-qt-image
 *           （QMimeDataPrivate::formats 剔除 application/x-qt* 内部类型；
 *           批次二十二模块决策，XMimeData_formats 自此对齐剔除）；
 *           为保持平台镜像对外原子集合与 Qt 一致（TARGETS 只见
 *           image/png），该内部类型在推送前被显式消费为 image/png 派生
 *           原子而不以原名登记。mime 已显式携带自定义 image/png 时跳过
 *           派生，交由通用循环照常镜像，避免同名原子重复登记。 */
static void clipboard_pushImagePngToBackend(int mode, const XMimeData* mime)
{
    XImage* image;
    XByteArray* png;
    if (!xclipboard_backendActive() || !g_clipboardBackend.setMimeData)
        return;
    /* 能力矩阵门闸：codec 裁剪 PNG 编码（canEncode=false）时如实跳过，
     * 不硬造字节（对标 Qt 无对应图像插件时不登记原子）。 */
    if (!XImageCodec_canEncode(XImageCodecFormat_Png))
        return;
    if (XMimeData_hasFormat(mime, "image/png"))
        return;
    image = XMimeData_imageData(mime);
    if (!image)
        return;
    if (XImage_isNull(image)) {
        XImage_delete_base((XClass*)image);
        return;
    }
    png = XByteArray_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, true);
    if (png) {
        /* quality=-1：PNG 无损，质量参数无意义（对标 Qt image/png 默认
         * 压缩）；字节经后端 setMimeData 深拷贝进平台镜像。 */
        if (XImageCodec_encode(image, XImageCodecFormat_Png, -1, png) &&
            XByteArray_size_base((const XContainer*)png) > 0) {
            g_clipboardBackend.setMimeData(g_clipboardBackend.ud, mode,
                "image/png",
                (const unsigned char*)XByteArray_constData(png),
                (int)XByteArray_size_base((const XContainer*)png));
        }
        XByteArray_delete_base((XClass*)png);
    }
    XImage_delete_base((XClass*)image);
}
#endif /* XIMAGECODEC_ON */

/** @brief 把 mime 全部格式经后端 setMimeData 回调写入平台剪贴板。
 *  @details 对标 QXcbClipboard::setMimeData：Qt 先把新 QMimeData 整体
 *           挂到 m_owner[mode]（旧内容随之整体替换），再按格式映射为
 *           X11 TARGETS 原子供其他应用协商。这里先经既有 clear 回调
 *           清平台镜像再逐格式写入，保证平台侧只含本次内容。
 *           application/x-qt-image 内部类型在循环前经
 *           XMimeData_hasImage（内部查询，不依赖 formats() 列表——
 *           formats 已按批次二十二决策剔除 x-qt* 内部类型）识别并派生
 *           为 image/png 原子推送（PNG 编码见
 *           clipboard_pushImagePngToBackend，对标 Qt 平台层对图像格式
 *           的特殊处理）；application/x-color 仍无对应 X11 目标约定，
 *           跳过；自定义格式（如 image/png 字节）以原子名直传。
 *           后端未注册 setMimeData 时不做任何事。 */
static void clipboard_pushMimeToBackend(int mode, const XMimeData* mime)
{
    XStringList* formats;
    int64_t i, n;
    if (!xclipboard_backendActive() || !g_clipboardBackend.setMimeData || !mime)
        return;
    if (g_clipboardBackend.clear)
        g_clipboardBackend.clear(g_clipboardBackend.ud, mode);
#if XIMAGECODEC_ON
    /* 内部图像类型派生推送：hasImage 走存储检查（对标 Qt hasFormat 命中
     * 内部类型不依赖 formats 列表），platform 镜像只见 image/png 原子。 */
    if (XMimeData_hasImage(mime))
        clipboard_pushImagePngToBackend(mode, mime);
#endif /* XIMAGECODEC_ON */
    formats = XMimeData_formats((XMimeData*)mime);
    if (!formats)
        return;
    n = XStringList_size_base((const XStringList*)formats);
    for (i = 0; i < n; ++i) {
        XString* name = (XString*)XStringList_at_base((XVector*)formats, i);
        const char* fmt = name ? XString_toUtf8(name) : NULL;
        XByteArray* bytes;
        if (!fmt || !fmt[0])
            continue;
        if (clipboard_asciiIcmp(fmt, "application/x-color") == 0)
            continue; /* 颜色格式暂无对应 X11 目标约定，维持跳过。 */
        /* 字节通道取载荷（二进制透明；对标 Qt 平台层透传 QMimeData
         * 保存的原始字节），镜像侧按字节流深拷贝。 */
        bytes = XMimeData_data_bytes((XMimeData*)mime, fmt);
        if (!bytes)
            continue;
        if (XByteArray_size_base((const XContainer*)bytes) > 0)
            g_clipboardBackend.setMimeData(g_clipboardBackend.ud, mode, fmt,
                (const unsigned char*)XByteArray_constData(bytes),
                (int)XByteArray_size_base((const XContainer*)bytes));
        XByteArray_delete_base((XClass*)bytes);
    }
    XStringList_delete_base((XClass*)formats);
}
#endif /* XMIMEDATA_ON */

void XClipboard_setMimeData(XClipboard* self, XMimeData* data, XClipboardMode mode)
{
    XClipboardModeData* slot;
    if (!self || !self->m_data)
        return;
    mode = clipboard_normalizeMode(mode);
    slot = &self->m_data->m_modes[mode];
    clipboard_clearModeData(slot);
    slot->m_mime = data; /* 接管所有权 */
    slot->m_owns = data != NULL;
#if XMIMEDATA_ON
    /* 对标 Qt：setMimeData 时把各 mime 格式同步进平台剪贴板。 */
    clipboard_pushMimeToBackend((int)mode, data);
#endif /* XMIMEDATA_ON */
    clipboard_emitChanged(self, mode);
}

#if XIMAGECODEC_ON
/** @brief 外部图像原子原始字节 → XImage 解码（剪贴板图像读取兜底）。
 *  @details 对标 QXcbClipboardMime::readImage：外部所有者提供图像原子
 *           （image/png、image/bmp、image/jpeg）时，取回原始字节交
 *           XImageCodec 对应格式的自研解码算法。非法数据或解码失败
 *           返回 NULL（空图像语义），零副作用。 */
static XImage* clipboard_decodeImageBytes(const unsigned char* bytes, int len,
                                          XImageCodecFormat format)
{
    XImage* image;
    bool ok;
    if (!bytes || len <= 0)
        return NULL;
    image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!image)
        return NULL;
    ok = XImageCodec_decode(bytes, (size_t)len, format, image);
    /* 解码失败或得到空图像（尺寸非法等）都按"无图像"处理。 */
    if (!ok || XImage_isNull(image)) {
        XImage_delete_base((XClass*)image);
        return NULL;
    }
    return image;
}
#endif /* XIMAGECODEC_ON */

XImage* XClipboard_image(const XClipboard* self, XClipboardMode mode)
{
    const XMimeData* mime;
    XImage* image;
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    /* 对标 QClipboard::image() 经 mimeData() 取数：非本进程所有时惰性
     * 并入后端外部格式（含图像原子；本进程所有或已合并则直返镜像，
     * owns 状态语义与既有一致，不做额外往返）。 */
    mime = XClipboard_mimeData(self, mode);
    if (!mime)
        return NULL;
    /* 1) 自有图像优先：application/x-qt-image（setImage/setMimeData
     *    写入路径；对标 Qt mimeData 返回的 QMimeData 内图像优先）。 */
    image = XMimeData_imageData(mime);
    if (image)
        return image;
#if XIMAGECODEC_ON
    /* 2) 外部图像原子原始字节 → 解码为 XImage（对标 QXcbClipboardMime
     *    的图像按需读取兜底；解码失败回落下一个原子，全部失败返回
     *    NULL，零副作用）。识别顺序对标 Qt 剪贴板图像原子优先序
     *    （image/png 置首，其后 image/bmp、image/jpeg），取第一个
     *    可解码者。字节经合并镜像的 XByteArray 通道取回（二进制
     *    透明：镜像载荷本批起不再经 XString 的 UTF-8 转换）。 */
    {
        static const struct
        {
            const char*       m_mime; /**< X11 图像原子对应的 MIME 名。 */
            XImageCodecFormat m_fmt;  /**< 库内解码算法格式。 */
        } kImageAtoms[] = {
            /* 对标 Qt：image/png 为首选图像原子（Qt 平台剪贴板图像格式序之首）。 */
            { "image/png",  XImageCodecFormat_Png  },
            /* 对标 Qt：image/bmp 次优先（Qt 图像原子序 png 之后）。 */
            { "image/bmp",  XImageCodecFormat_Bmp  },
            /* 对标 Qt：image/jpeg 第三优先（Qt 图像原子序 bmp 之后）。 */
            { "image/jpeg", XImageCodecFormat_Jpeg }
        };
        int k;
        for (k = 0;
             k < (int)(sizeof(kImageAtoms) / sizeof(kImageAtoms[0])); ++k) {
            XByteArray* bytes;
            XImage* decoded;
            /* 能力矩阵门闸：库内 codec 裁剪该格式（canDecode=false）
             * 时如实跳过，不硬造解码（对标 Qt 无对应插件时不登记）。 */
            if (!XImageCodec_canDecode(kImageAtoms[k].m_fmt))
                continue;
            if (!XMimeData_hasFormat(mime, kImageAtoms[k].m_mime))
                continue;
            bytes = XMimeData_data_bytes(mime, kImageAtoms[k].m_mime);
            if (!bytes)
                continue;
            decoded = clipboard_decodeImageBytes(
                XByteArray_constData(bytes),
                (int)XByteArray_size_base((const XContainer*)bytes),
                kImageAtoms[k].m_fmt);
            XByteArray_delete_base((XClass*)bytes);
            if (decoded)
                return decoded;
        }
    }
#endif /* XIMAGECODEC_ON */
    return NULL;
}

void XClipboard_setImage(XClipboard* self, const XImage* image, XClipboardMode mode)
{
#if XMIMEDATA_ON
    XMimeData* mime;
    if (!self)
        return;
    mime = XMimeData_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!mime)
        return;
    XMimeData_setImageData(mime, image);
    XClipboard_setMimeData(self, mime, mode);
#else
    (void)self; (void)image; (void)mode;
#endif /* XMIMEDATA_ON */
}

XPixmap* XClipboard_pixmap(const XClipboard* self, XClipboardMode mode)
{
    XImage* image;
    XPixmap* pixmap;
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    image = XClipboard_image(self, mode);
    if (!image)
        return NULL;
    pixmap = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (pixmap)
        XPixmap_fromImage(image, 0, pixmap);
    XImage_delete_base((XClass*)image);
    return pixmap;
}

void XClipboard_setPixmap(XClipboard* self, const XPixmap* pixmap, XClipboardMode mode)
{
    XImage* image;
    XPixmap* copy;
    if (!self || !pixmap)
        return;
    image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!image)
        return;
    copy = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (copy) {
        XCopy(copy, pixmap);
        XPixmap_toImage(copy, image);
        XPixmap_delete_base((XClass*)copy);
    }
    XClipboard_setImage(self, image, mode);
    XImage_delete_base((XClass*)image);
}

/* ==================== 信号（4 个，对标 QClipboard 全部信号） ==================== */

void* XClipboard_changed_signal(XClipboard* self, XClipboardMode mode)
{
    if (!self) return (void*)(size_t)XClipboard_changed_signal;
    clipboard_emit(self, (size_t)XClipboard_changed_signal,
                   XVarList_Create(XVar(XClipboardMode, mode)));
    return (void*)(size_t)XClipboard_changed_signal;
}

void* XClipboard_selectionChanged_signal(XClipboard* self)
{
    if (!self) return (void*)(size_t)XClipboard_selectionChanged_signal;
    clipboard_emit(self, (size_t)XClipboard_selectionChanged_signal, NULL);
    return (void*)(size_t)XClipboard_selectionChanged_signal;
}

void* XClipboard_findBufferChanged_signal(XClipboard* self)
{
    if (!self) return (void*)(size_t)XClipboard_findBufferChanged_signal;
    clipboard_emit(self, (size_t)XClipboard_findBufferChanged_signal, NULL);
    return (void*)(size_t)XClipboard_findBufferChanged_signal;
}

void* XClipboard_dataChanged_signal(XClipboard* self)
{
    if (!self) return (void*)(size_t)XClipboard_dataChanged_signal;
    clipboard_emit(self, (size_t)XClipboard_dataChanged_signal, NULL);
    return (void*)(size_t)XClipboard_dataChanged_signal;
}

#endif /* XCLIPBOARD_ON */
