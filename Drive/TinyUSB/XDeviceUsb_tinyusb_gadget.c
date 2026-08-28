/**
 * @file       XDeviceUsb_tinyusb_gadget.c
 * @brief      TinyUSB 嵌入式 Gadget/Device 后端。
 * @details    将 XDeviceUsbGadget 平台接口映射到 TinyUSB Device API（tud_*）。
 *             支持 STM32F4（DWC2）和 ESP32-S3（DWC2）。
 *
 * 已实现：
 * - 描述符回调：设备 / 配置 / BOS / 字符串（UTF-8→UTF-16LE）
 * - 控制传输回调桥接（Setup / Data / Status 三阶段）
 * - 状态回调：mount / umount / suspend / resume → 框架事件
 * - 端点发现：从配置描述符自动解析所有非控制端点
 * - 端点同步传输（Bulk / Interrupt / Isochronous）
 * - 端点异步传输 + 传输 ID 管理 + processEvents 中完成回调
 * - 端点 STALL / Clear Stall / Abort / FIFO 清空
 * - 远程唤醒
 * - Claim / Release 接口位图管理（与类驱动并行时需注意）
 */
#include "XDeviceUsb_tinyusb.h"
#include "XAbstractEventDispatcher.h"
#include "XMemory.h"

#if defined(XINYUE_C_HAS_TINYUSB) && CFG_TUD_ENABLED

#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------
 * 常量
 * ------------------------------------------------------------------ */

#define XTUSB_MAX_ENDPOINTS    16u  /* 最多 16 个非控制端点 */
#define XTUSB_STRING_BUF_LEN   128u /* 字符串描述符临时缓冲区（UTF-16） */

/* 描述符类型 */
#define XTUSB_DESC_DEVICE      0x01u
#define XTUSB_DESC_CONFIG      0x02u
#define XTUSB_DESC_STRING      0x03u
#define XTUSB_DESC_INTERFACE   0x04u
#define XTUSB_DESC_ENDPOINT    0x05u

/* 端点类型 */
#define XTUSB_EP_ATTR_CONTROL    0x00u
#define XTUSB_EP_ATTR_ISOCHRONOUS 0x01u
#define XTUSB_EP_ATTR_BULK       0x02u
#define XTUSB_EP_ATTR_INTERRUPT  0x03u

/* ------------------------------------------------------------------
 * 传输节点
 * ------------------------------------------------------------------ */

typedef struct XTuGadgetTransfer
{
    XDeviceUsbTransferId m_id;
    XDeviceUsbEndpointAddress m_endpoint;
    uint8_t* m_data;
    size_t m_length;
    size_t m_transferred;
    XDeviceUsbTransferCallback m_callback;
    void* m_userData;
    int32_t m_timeoutMs;
    bool m_inProgress;
    bool m_isIn;
    struct XTuGadgetTransfer* m_next;
} XTuGadgetTransfer;

/* ------------------------------------------------------------------
 * 端点上下文
 * ------------------------------------------------------------------ */

typedef struct XTuGadgetEndpoint
{
    XDeviceUsbGadgetEndpointInfo m_info;
    XTuGadgetTransfer* m_transfers;  /* 待处理传输链表头 */
} XTuGadgetEndpoint;

/* ------------------------------------------------------------------
 * 控制器上下文
 * ------------------------------------------------------------------ */

typedef struct XTuGadgetController
{
    XDeviceUsbGadgetConfig m_config;
    XDeviceUsbGadgetState m_state;
    XDeviceUsbError m_lastError;
    int m_lastNativeError;

    XDeviceUsbGadgetSetupCallback m_setupCallback;
    void* m_setupUserData;

    XDeviceUsbGadgetEventCallback m_eventCallback;
    void* m_eventUserData;

    XTuGadgetEndpoint m_endpoints[XTUSB_MAX_ENDPOINTS];
    size_t m_endpointCount;

    XDeviceUsbTransferId m_nextTransferId;

    /* 字符串描述符 UTF-16 临时缓冲区（tud 要求静态） */
    uint16_t m_stringBuf[XTUSB_STRING_BUF_LEN];
    uint8_t m_stringBufLen; /* 以字节为单位的总长度 */

    uint32_t m_claimedInterfaces;

    bool m_started;
    bool m_configured;
        bool m_suspended;
    XHandle m_pollHandle;
} XTuGadgetController;

/* ------------------------------------------------------------------
 * 单例
 * ------------------------------------------------------------------ */

static XTuGadgetController* s_controller = NULL;

static XTuGadgetController* xTuGadgetCast(void* handle)
{
    return (XTuGadgetController*)handle;
}

static void xTuGadgetSetError(XTuGadgetController* c, tusb_error_t err)
{
    if (!c) return;
    c->m_lastNativeError = (int)err;
    switch (err) {
    case TUSB_ERROR_NONE:        c->m_lastError = XDeviceUsbError_None; break;
    case TUSB_ERROR_INVALID_PARA: c->m_lastError = XDeviceUsbError_InvalidArgument; break;
    case TUSB_ERROR_NOT_SUPPORTED: c->m_lastError = XDeviceUsbError_Unsupported; break;
    case TUSB_ERROR_NOT_FOUND:   c->m_lastError = XDeviceUsbError_NoDevice; break;
    default:                     c->m_lastError = XDeviceUsbError_Io; break;
    }
}

XDeviceUsbError xTinyUsbMapError(tusb_error_t err)
{
    switch (err) {
    case TUSB_ERROR_NONE: return XDeviceUsbError_None;
    case TUSB_ERROR_INVALID_PARA: return XDeviceUsbError_InvalidArgument;
    case TUSB_ERROR_NOT_SUPPORTED: return XDeviceUsbError_Unsupported;
    case TUSB_ERROR_NOT_FOUND: return XDeviceUsbError_NoDevice;
    default: return XDeviceUsbError_Io;
    }
}

/* ------------------------------------------------------------------
 * 端点查找
 * ------------------------------------------------------------------ */

static XTuGadgetEndpoint* xTuGadgetFindEndpoint(XTuGadgetController* c,
                                                  XDeviceUsbEndpointAddress address)
{
    size_t i;
    if (!c || address == 0) return NULL;
    for (i = 0; i < c->m_endpointCount; ++i)
        if (c->m_endpoints[i].m_info.m_address == address)
            return &c->m_endpoints[i];
    return NULL;
}

/* ------------------------------------------------------------------
 * UTF-8 到 UTF-16LE 转换（用于字符串描述符）
 * 返回转换后的 UTF-16 字符数（不包括 0 结尾），失败返回 0。
 * ------------------------------------------------------------------ */

static size_t xTuGadgetUtf8ToUtf16le(const char* utf8, uint16_t* out, size_t outMaxChars)
{
    size_t i = 0;
    size_t outIdx = 0;
    if (!utf8 || !out || outMaxChars == 0) return 0;
    while (utf8[i] && outIdx < outMaxChars - 1) {
        uint8_t b = (uint8_t)utf8[i];
        uint32_t cp;
        if (b < 0x80u) {
            cp = b;
            i += 1;
        } else if ((b & 0xE0u) == 0xC0u) {
            cp = ((uint32_t)(b & 0x1Fu) << 6) | ((uint32_t)(uint8_t)utf8[i + 1] & 0x3Fu);
            i += 2;
        } else if ((b & 0xF0u) == 0xE0u) {
            cp = ((uint32_t)(b & 0x0Fu) << 12) |
                 ((uint32_t)(uint8_t)utf8[i + 1] & 0x3Fu) << 6 |
                 ((uint32_t)(uint8_t)utf8[i + 2] & 0x3Fu);
            i += 3;
        } else {
            /* 4 字节及以上或非法序列，替换为 U+FFFD（简化处理） */
            cp = 0xFFFDu;
            i += 1;
        }
        /* 代理对：BMP 以内直接存，超过则拆成代理对 */
        if (cp <= 0xFFFFu) {
            out[outIdx++] = (uint16_t)cp;
        } else if (cp <= 0x10FFFFu && outIdx + 2 <= outMaxChars) {
            cp -= 0x10000u;
            out[outIdx++] = (uint16_t)(0xD800u | (cp >> 10));
            out[outIdx++] = (uint16_t)(0xDC00u | (cp & 0x3FFu));
        } else {
            break;
        }
    }
    return outIdx;
}

/* ------------------------------------------------------------------
 * 端点发现：从配置描述符解析非控制端点
 * ------------------------------------------------------------------ */

static bool xTuGadgetParseEndpoints(XTuGadgetController* c,
    const uint8_t* configDesc, size_t configLen)
{
    size_t offset = 0;
    uint8_t interfaceNumber = 0;
    uint8_t alternateSetting = 0;

    c->m_endpointCount = 0;
    if (!configDesc || configLen < 9u) return false;

    /* 跳过配置描述符本身 */
    offset = configDesc[0];

    while (offset + 2u <= configLen) {
        uint8_t bLength = configDesc[offset];
        uint8_t bDescriptorType = configDesc[offset + 1];
        if (bLength < 2u || offset + bLength > configLen) return false;

        if (bDescriptorType == XTUSB_DESC_INTERFACE && bLength >= 9u) {
            interfaceNumber = configDesc[offset + 2];
            alternateSetting = configDesc[offset + 3];
        } else if (bDescriptorType == XTUSB_DESC_ENDPOINT && bLength >= 7u) {
            if (c->m_endpointCount < XTUSB_MAX_ENDPOINTS) {
                XTuGadgetEndpoint* ep = &c->m_endpoints[c->m_endpointCount];
                memset(ep, 0, sizeof(*ep));
                ep->m_info.m_address = configDesc[offset + 2];
                ep->m_info.m_transferType = (XDeviceUsbTransferType)(configDesc[offset + 3] & 0x03u);
                ep->m_info.m_maxPacketSize =
                    (uint16_t)configDesc[offset + 4] |
                    ((uint16_t)configDesc[offset + 5] << 8);
                ep->m_info.m_interval = configDesc[offset + 6];
                ep->m_info.m_interfaceNumber = interfaceNumber;
                ep->m_info.m_alternateSetting = alternateSetting;
                ep->m_info.m_maxTransferSize = 0u;
                ep->m_transfers = NULL;
                ++c->m_endpointCount;
            }
        }
        offset += bLength;
    }
    return c->m_endpointCount > 0u;
}

/* ------------------------------------------------------------------
 * 字符串查找
 * ------------------------------------------------------------------ */

static const XDeviceUsbGadgetStringDescriptor* xTuGadgetFindString(
    XTuGadgetController* c, uint8_t index, uint16_t languageId)
{
    size_t i;
    if (!c || !c->m_config.m_strings) return NULL;
    for (i = 0; i < c->m_config.m_stringCount; ++i) {
        const XDeviceUsbGadgetStringDescriptor* s = &c->m_config.m_strings[i];
        if (s->m_index == index &&
            (s->m_languageId == 0 || s->m_languageId == languageId))
            return s;
    }
    return NULL;
}

/* ------------------------------------------------------------------
 * TinyUSB 描述符回调
 * ------------------------------------------------------------------ */

uint8_t const * tud_descriptor_device_cb(void)
{
    if (!s_controller || !s_controller->m_config.m_deviceDescriptor.m_data)
        return NULL;
    return s_controller->m_config.m_deviceDescriptor.m_data;
}

uint8_t const * tud_descriptor_bos_cb(void)
{
    if (!s_controller || !s_controller->m_config.m_bosDescriptor)
        return NULL;
    return s_controller->m_config.m_bosDescriptor;
}

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
    XTuGadgetController* c = s_controller;
    if (!c || !c->m_config.m_configurations) return NULL;
    if (index >= c->m_config.m_configurationCount) return NULL;
    return c->m_config.m_configurations[index].m_descriptor;
}

/* 字符串描述符：返回 uint16_t 数组，第一个元素是 (bLength << 8) | bDescriptorType
   TinyUSB 期望返回值是 uint16_t 指针，第 0 项的低字节是 bDescriptorType (0x03)，
   高字节是 bLength（总字节数）。 */
uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    XTuGadgetController* c = s_controller;
    if (!c) return NULL;

    if (index == 0) {
        /* LANGID 列表：只有一个语言 ID (0x0409 = 英语-美国) */
        c->m_stringBuf[0] = (uint16_t)(4u << 8) | 0x03u;  /* bLength=4, bType=0x03 */
        c->m_stringBuf[1] = langid ? langid : 0x0409u;
        return c->m_stringBuf;
    }

    {
        const XDeviceUsbGadgetStringDescriptor* s =
            xTuGadgetFindString(c, index, langid);
        size_t charCount;
        size_t byteLength;

        /* 如果指定语言找不到，试默认（languageId=0） */
        if (!s && langid != 0) s = xTuGadgetFindString(c, index, 0);
        if (!s || !s->m_utf8) return NULL;

        charCount = xTuGadgetUtf8ToUtf16le(s->m_utf8,
                        c->m_stringBuf + 1, XTUSB_STRING_BUF_LEN - 1);
        if (charCount == 0) return NULL;

        byteLength = 2u + charCount * 2u;  /* bLength + bType + 字符串 */
        if (byteLength > XTUSB_STRING_BUF_LEN * 2u) return NULL;

        c->m_stringBuf[0] = (uint16_t)(byteLength << 8) | 0x03u;
        return c->m_stringBuf;
    }
}

/* ------------------------------------------------------------------
 * TinyUSB 控制传输回调
 * ------------------------------------------------------------------ */

bool tud_control_xfer_cb(uint8_t rhport, uint8_t stage,
                         tusb_control_request_t const * request)
{
    XTuGadgetController* c = s_controller;
    XDeviceUsbSetupRequest req;
    XDeviceUsbSetupStage stageFrame;
    (void)rhport;

    if (!c || !c->m_setupCallback) return false;

    memset(&req, 0, sizeof(req));
    req.m_bmRequestType = (uint8_t)request->bmRequestType_bit.recipient |
                          ((uint8_t)request->bmRequestType_bit.type << 5) |
                          (request->bmRequestType_bit.direction ? 0x80u : 0u);
    req.m_bRequest = request->bRequest;
    req.m_wValue = request->wValue;
    req.m_wIndex = request->wIndex;
    req.m_wLength = request->wLength;

    switch (stage) {
    case CONTROL_STAGE_SETUP:  stageFrame = XDeviceUsbSetupStage_Setup; break;
    case CONTROL_STAGE_DATA:
        stageFrame = request->bmRequestType_bit.direction ?
            XDeviceUsbSetupStage_DataIn : XDeviceUsbSetupStage_DataOut;
        break;
    case CONTROL_STAGE_ACK:    stageFrame = XDeviceUsbSetupStage_Status; break;
    default: return false;
    }

    return c->m_setupCallback(c, &req, stageFrame, NULL, 0, c->m_setupUserData);
}

/* ------------------------------------------------------------------
 * TinyUSB 设备状态回调
 * ------------------------------------------------------------------ */

void tud_mount_cb(void)
{
    XTuGadgetController* c = s_controller;
    if (!c) return;
    c->m_configured = true;
    c->m_state = XDeviceUsbGadgetState_Configured;
    if (c->m_eventCallback) {
        XDeviceUsbGadgetEvent e;
        memset(&e, 0, sizeof(e));
        e.m_type = XDeviceUsbGadgetEventType_Connected;
        c->m_eventCallback(c, &e, c->m_eventUserData);
    }
}

void tud_umount_cb(void)
{
    XTuGadgetController* c = s_controller;
    if (!c) return;
    c->m_configured = false;
    c->m_state = XDeviceUsbGadgetState_Powered;
    if (c->m_eventCallback) {
        XDeviceUsbGadgetEvent e;
        memset(&e, 0, sizeof(e));
        e.m_type = XDeviceUsbGadgetEventType_Disconnected;
        c->m_eventCallback(c, &e, c->m_eventUserData);
    }
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    XTuGadgetController* c = s_controller;
    (void)remote_wakeup_en;
    if (!c) return;
    c->m_suspended = true;
    if (c->m_eventCallback) {
        XDeviceUsbGadgetEvent e;
        memset(&e, 0, sizeof(e));
        e.m_type = XDeviceUsbGadgetEventType_Suspend;
        c->m_eventCallback(c, &e, c->m_eventUserData);
    }
}

void tud_resume_cb(void)
{
    XTuGadgetController* c = s_controller;
    if (!c) return;
    c->m_suspended = false;
    if (c->m_eventCallback) {
        XDeviceUsbGadgetEvent e;
        memset(&e, 0, sizeof(e));
        e.m_type = XDeviceUsbGadgetEventType_Resume;
        c->m_eventCallback(c, &e, c->m_eventUserData);
    }
}

/* ------------------------------------------------------------------
 * 传输完成处理（在 processEvents 中调用）
 * ------------------------------------------------------------------ */

static void xTuGadgetPollTransfers(XTuGadgetController* c)
{
    size_t i;
    if (!c) return;

    for (i = 0; i < c->m_endpointCount; ++i) {
        XTuGadgetEndpoint* ep = &c->m_endpoints[i];
        XTuGadgetTransfer* head;
        if (!ep->m_transfers) continue;

        head = ep->m_transfers;

        /* IN 端点：检查是否发送完成 */
        if (XDEVICE_USB_ENDPOINT_IS_IN(ep->m_info.m_address)) {
            if (tud_edpt_ready(ep->m_info.m_address)) {
                /* 传输完成：从链表移除并回调 */
                if (head->m_inProgress) {
                    XTuGadgetTransfer* done = head;
                    ep->m_transfers = head->m_next;
                    done->m_transferred = done->m_length;
                    if (done->m_callback) {
                        done->m_callback(c, done->m_id,
                            XDeviceUsbTransferResult_Ok,
                            done->m_data, done->m_transferred,
                            done->m_userData);
                    }
                    XFree_System(done);
                }
            }
        }
        /* OUT 端点：检查是否有数据可读 */
        else {
            if (head->m_inProgress) {
                uint32_t avail = tud_edpt_available(ep->m_info.m_address);
                if (avail > 0 && head->m_data && head->m_length > 0) {
                    uint32_t count = tud_edpt_read(ep->m_info.m_address,
                                                   head->m_data, head->m_length);
                    XTuGadgetTransfer* done = head;
                    ep->m_transfers = head->m_next;
                    done->m_transferred = (size_t)count;
                    if (done->m_callback) {
                        done->m_callback(c, done->m_id,
                            XDeviceUsbTransferResult_Ok,
                            done->m_data, done->m_transferred,
                            done->m_userData);
                    }
                    XFree_System(done);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------
 * 创建/打开/关闭/删除
 * ------------------------------------------------------------------ */

void* XDeviceUsbGadget_platformCreate(const XDeviceUsbGadgetConfig* config, int* error)
{
    XTuGadgetController* c;
    (void)config;
    c = (XTuGadgetController*)XCalloc_System(1, sizeof(*c));
    if (!c) {
        if (error) *error = XDeviceUsbError_Resource;
        return NULL;
    }
    c->m_state = XDeviceUsbGadgetState_Closed;
    c->m_lastError = XDeviceUsbError_None;
    c->m_nextTransferId = 1;
    c->m_pollHandle = NULL;
    if (error) *error = XDeviceUsbError_None;
    return c;
}

bool XDeviceUsbGadget_platformOpen(void* controller,
                                    const XDeviceUsbGadgetConfig* config, int* error)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c || !config) { if (error) *error = XDeviceUsbError_InvalidArgument; return false; }

    c->m_config = *config;
    c->m_state = XDeviceUsbGadgetState_Opened;

    /* 从第一配置描述符解析端点 */
    if (config->m_configurations && config->m_configurationCount > 0 &&
        config->m_configurations[0].m_descriptor) {
        xTuGadgetParseEndpoints(c,
            config->m_configurations[0].m_descriptor,
            config->m_configurations[0].m_descriptorLength);
    }

    if (error) *error = XDeviceUsbError_None;
    return true;
}

void XDeviceUsbGadget_platformClose(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c) return;
    XDeviceUsbGadget_platformStop(c);
    c->m_state = XDeviceUsbGadgetState_Closed;
}

void XDeviceUsbGadget_platformDelete(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    size_t i;
    if (!c) return;
    XDeviceUsbGadget_platformClose(c);
    /* 清理所有传输节点 */
    for (i = 0; i < c->m_endpointCount; ++i) {
        XTuGadgetTransfer* t = c->m_endpoints[i].m_transfers;
        while (t) {
            XTuGadgetTransfer* next = t->m_next;
            XFree_System(t);
            t = next;
        }
    }
    if (s_controller == c) s_controller = NULL;
    XFree_System(c);
}

/* ------------------------------------------------------------------
 * 状态查询
 * ------------------------------------------------------------------ */

bool XDeviceUsbGadget_platformIsStarted(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    return c && c->m_started;
}

bool XDeviceUsbGadget_platformIsConfigured(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    return c && c->m_configured;
}

bool XDeviceUsbGadget_platformGetConfig(void* controller,
                                         XDeviceUsbGadgetConfig* config)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c || !config) return false;
    *config = c->m_config;
    return true;
}

bool XDeviceUsbGadget_platformConfigure(void* controller,
                                         const XDeviceUsbGadgetConfig* config)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c || !config || c->m_started) return false;
    c->m_config = *config;
    /* 重新解析端点 */
    memset(c->m_endpoints, 0, sizeof(c->m_endpoints));
    c->m_endpointCount = 0;
    if (config->m_configurations && config->m_configurationCount > 0 &&
        config->m_configurations[0].m_descriptor) {
        xTuGadgetParseEndpoints(c,
            config->m_configurations[0].m_descriptor,
            config->m_configurations[0].m_descriptorLength);
    }
    return true;
}

XDeviceUsbGadgetState XDeviceUsbGadget_platformStatus(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    return c ? c->m_state : XDeviceUsbGadgetState_Closed;
}

/* ------------------------------------------------------------------
 * 启动/停止
 * ------------------------------------------------------------------ */

/* 事件循环轮询回调：每次事件循环调用一次 tud_task() */
static bool xTuGadgetEventLoopPoll(void* userData)
{
    XTuGadgetController* c = (XTuGadgetController*)userData;
    if (!c || !c->m_started) return false;
    XDeviceUsbGadget_platformProcessEvents(c, 0);
    return true;
}
bool XDeviceUsbGadget_platformStart(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c || c->m_started) return false;

    if (s_controller && s_controller != c) return false;
    s_controller = c;

    if (!tud_init(TUD_OPT_RHPORT)) {
        xTuGadgetSetError(c, TUSB_ERROR_FAILED);
        s_controller = NULL;
        return false;
    }

    c->m_started = true;
    c->m_state = XDeviceUsbGadgetState_Powered;
    /* 注册到事件循环，自动轮询 tud_task() */
    c->m_pollHandle = XAbstractEventDispatcher_addPollCallback(
        xTuGadgetEventLoopPoll, c);
    xTuGadgetSetError(c, TUSB_ERROR_NONE);
    return true;
}

bool XDeviceUsbGadget_platformStop(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    size_t i;
    if (!c || !c->m_started) return false;

    /* 取消所有进行中的传输 */
    for (i = 0; i < c->m_endpointCount; ++i) {
        XTuGadgetEndpoint* ep = &c->m_endpoints[i];
        XTuGadgetTransfer* t = ep->m_transfers;
        while (t) {
            XTuGadgetTransfer* next = t->m_next;
            if (t->m_callback) {
                t->m_callback(c, t->m_id,
                    XDeviceUsbTransferResult_Cancelled,
                    t->m_data, t->m_transferred, t->m_userData);
            }
            XFree_System(t);
            t = next;
        }
        ep->m_transfers = NULL;
    }

    /* 注销事件循环轮询回调 */
    if (c->m_pollHandle) {
        XAbstractEventDispatcher_removePollCallback(c->m_pollHandle);
        c->m_pollHandle = NULL;
    }
    c->m_started = false;
    c->m_configured = false;
    c->m_suspended = false;
    c->m_state = XDeviceUsbGadgetState_Opened;

    if (s_controller == c) s_controller = NULL;
    return true;
}

/* ------------------------------------------------------------------
 * 回调设置
 * ------------------------------------------------------------------ */

bool XDeviceUsbGadget_platformSetSetupCallback(void* controller,
    XDeviceUsbGadgetSetupCallback callback, void* userData)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c) return false;
    c->m_setupCallback = callback;
    c->m_setupUserData = userData;
    return true;
}

bool XDeviceUsbGadget_platformSetEventCallback(void* controller,
    XDeviceUsbGadgetEventCallback callback, void* userData)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c) return false;
    c->m_eventCallback = callback;
    c->m_eventUserData = userData;
    return true;
}

/* ------------------------------------------------------------------
 * 事件循环
 * ------------------------------------------------------------------ */

XDeviceUsbProcessResult XDeviceUsbGadget_platformProcessEvents(
    void* controller, int32_t timeoutMs)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    (void)timeoutMs;
    if (!c || !c->m_started) return XDeviceUsbProcessResult_Error;

    tud_task();
    xTuGadgetPollTransfers(c);

    return XDeviceUsbProcessResult_Event;
}

/* ------------------------------------------------------------------
 * 端点信息
 * ------------------------------------------------------------------ */

bool XDeviceUsbGadget_platformGetEndpointInfo(void* controller,
    XDeviceUsbGadgetEndpointInfo* endpoint)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    XTuGadgetEndpoint* ep;
    if (!c || !endpoint) return false;
    ep = xTuGadgetFindEndpoint(c, endpoint->m_address);
    if (!ep) return false;
    *endpoint = ep->m_info;
    return true;
}

/* ------------------------------------------------------------------
 * 同步传输
 * ------------------------------------------------------------------ */

XDeviceUsbTransferResult XDeviceUsbGadget_platformTransfer(void* controller,
    XDeviceUsbEndpointAddress endpoint, void* data, size_t length,
    size_t* transferred, int32_t timeoutMs)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    XTuGadgetEndpoint* ep;
    (void)timeoutMs;
    if (!c || !c->m_started || !data || length == 0)
        return XDeviceUsbTransferResult_InvalidArgument;

    ep = xTuGadgetFindEndpoint(c, endpoint);
    if (!ep) return XDeviceUsbTransferResult_InvalidArgument;

    if (XDEVICE_USB_ENDPOINT_IS_IN(endpoint)) {
        if (!tud_edpt_ready(endpoint)) return XDeviceUsbTransferResult_Busy;
        if (!tud_edpt_write(endpoint, data, length))
            return XDeviceUsbTransferResult_IoError;
        /* 等待发送完成（同步语义）——但此处不阻塞，
           只确保数据已写入 FIFO。TinyUSB 在 tud_task() 中实际发送。 */
        if (transferred) *transferred = length;
        return XDeviceUsbTransferResult_Ok;
    }

    /* OUT 端点：同步读 */
    {
        uint32_t count = tud_edpt_read(endpoint, data, length);
        if (transferred) *transferred = (size_t)count;
        return count > 0 ? XDeviceUsbTransferResult_Ok : XDeviceUsbTransferResult_Busy;
    }
}

/* ------------------------------------------------------------------
 * 异步传输
 * ------------------------------------------------------------------ */

XDeviceUsbTransferId XDeviceUsbGadget_platformSubmitTransfer(void* controller,
    const XDeviceUsbGadgetTransferRequest* request,
    XDeviceUsbTransferCallback callback, void* userData)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    XTuGadgetEndpoint* ep;
    XTuGadgetTransfer* t;
    XTuGadgetTransfer** p;

    if (!c || !c->m_started || !request || !callback)
        return XDEVICE_USB_INVALID_TRANSFER_ID;

    ep = xTuGadgetFindEndpoint(c, request->m_endpoint);
    if (!ep) return XDEVICE_USB_INVALID_TRANSFER_ID;

    t = (XTuGadgetTransfer*)XCalloc_System(1, sizeof(*t));
    if (!t) return XDEVICE_USB_INVALID_TRANSFER_ID;

    t->m_id = c->m_nextTransferId++;
    if (c->m_nextTransferId == 0) c->m_nextTransferId = 1;
    c->m_pollHandle = NULL;
    t->m_endpoint = request->m_endpoint;
    t->m_data = (uint8_t*)request->m_data;
    t->m_length = request->m_length;
    t->m_transferred = 0;
    t->m_callback = callback;
    t->m_userData = userData;
    t->m_timeoutMs = request->m_timeoutMs;
    t->m_isIn = XDEVICE_USB_ENDPOINT_IS_IN(request->m_endpoint) ? true : false;
    t->m_next = NULL;
    t->m_inProgress = true;

    /* IN 端点：立即写入 FIFO，等待完成通知 */
    if (t->m_isIn) {
        if (!tud_edpt_ready(request->m_endpoint) ||
            !tud_edpt_write(request->m_endpoint, t->m_data, t->m_length)) {
            XFree_System(t);
            return XDEVICE_USB_INVALID_TRANSFER_ID;
        }
    }

    /* 加入端点传输队列尾部 */
    p = &ep->m_transfers;
    while (*p) p = &(*p)->m_next;
    *p = t;

    return t->m_id;
}

XDeviceUsbTransferResult XDeviceUsbGadget_platformCancelTransfer(void* controller,
    XDeviceUsbTransferId id)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    size_t i;
    if (!c || !id) return XDeviceUsbTransferResult_InvalidArgument;

    for (i = 0; i < c->m_endpointCount; ++i) {
        XTuGadgetEndpoint* ep = &c->m_endpoints[i];
        XTuGadgetTransfer** pp = &ep->m_transfers;
        while (*pp) {
            if ((*pp)->m_id == id) {
                XTuGadgetTransfer* t = *pp;
                *pp = t->m_next;
                /* Abort 端点 */
                tud_edpt_abort(ep->m_info.m_address);
                if (t->m_callback) {
                    t->m_callback(c, t->m_id,
                        XDeviceUsbTransferResult_Cancelled,
                        t->m_data, t->m_transferred, t->m_userData);
                }
                XFree_System(t);
                return XDeviceUsbTransferResult_Cancelled;
            }
            pp = &(*pp)->m_next;
        }
    }
    return XDeviceUsbTransferResult_NoDevice;
}

/* ------------------------------------------------------------------
 * 端点 STALL / 清空队列
 * ------------------------------------------------------------------ */

bool XDeviceUsbGadget_platformSetEndpointStalled(void* controller,
    XDeviceUsbEndpointAddress endpoint, bool stalled)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c || !c->m_started) return false;
    if (stalled) {
        tud_edpt_stall(endpoint);
    } else {
        tud_edpt_clear_stall(endpoint);
    }
    return true;
}

bool XDeviceUsbGadget_platformClearEndpointQueue(void* controller,
    XDeviceUsbEndpointAddress endpoint)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    XTuGadgetEndpoint* ep;
    XTuGadgetTransfer* t;
    if (!c || !c->m_started) return false;
    ep = xTuGadgetFindEndpoint(c, endpoint);
    if (!ep) return false;

    /* Abort 端点传输 */
    tud_edpt_abort(endpoint);

    /* 取消所有排队中的传输 */
    t = ep->m_transfers;
    while (t) {
        XTuGadgetTransfer* next = t->m_next;
        if (t->m_callback) {
            t->m_callback(c, t->m_id,
                XDeviceUsbTransferResult_Cancelled,
                t->m_data, t->m_transferred, t->m_userData);
        }
        XFree_System(t);
        t = next;
    }
    ep->m_transfers = NULL;
    return true;
}

/* ------------------------------------------------------------------
 * 远程唤醒
 * ------------------------------------------------------------------ */

bool XDeviceUsbGadget_platformRemoteWakeup(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (!c || !c->m_started || !c->m_suspended) return false;
    tud_remote_wakeup();
    return true;
}

/* ------------------------------------------------------------------
 * 特性/句柄/错误
 * ------------------------------------------------------------------ */

XDeviceUsbGadgetFeatures XDeviceUsbGadget_platformFeatures(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    XDeviceUsbGadgetFeatures f =
        XDeviceUsbGadgetFeature_DeviceMode |
        XDeviceUsbGadgetFeature_FullSpeed |
        XDeviceUsbGadgetFeature_EndpointTransfer |
        XDeviceUsbGadgetFeature_AsyncTransfer |
        XDeviceUsbGadgetFeature_ControlCallback |
        XDeviceUsbGadgetFeature_EventCallback |
        XDeviceUsbGadgetFeature_ProcessEvents |
        XDeviceUsbGadgetFeature_SuspendResume |
        XDeviceUsbGadgetFeature_NativeHandle |
        XDeviceUsbGadgetFeature_EndpointStall;
    if (!c) return XDeviceUsbGadgetFeature_None;
#if TUD_OPT_HIGH_SPEED
    f |= XDeviceUsbGadgetFeature_HighSpeed;
#endif
    if (c->m_config.m_bosDescriptor && c->m_config.m_bosDescriptorLength > 0)
        f |= XDeviceUsbGadgetFeature_BosDescriptor;
    return f;
}

XDeviceUsbNativeHandle XDeviceUsbGadget_platformHandle(void* controller)
{
    (void)controller;
    return XDEVICE_USB_INVALID_NATIVE_HANDLE;
}

XDeviceUsbError XDeviceUsbGadget_platformLastError(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    return c ? c->m_lastError : XDeviceUsbError_NotOpen;
}

int32_t XDeviceUsbGadget_platformNativeError(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    return c ? c->m_lastNativeError : -1;
}

void XDeviceUsbGadget_platformClearError(void* controller)
{
    XTuGadgetController* c = xTuGadgetCast(controller);
    if (c) { c->m_lastError = XDeviceUsbError_None; c->m_lastNativeError = 0; }
}

#endif /* XINYUE_C_HAS_TINYUSB && CFG_TUD_ENABLED */
