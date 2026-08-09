#include "XCan_config.h"
#if XPROTOCOL_ON
#if XCAN_ON
#if XCAN_DBC_ON
#include "XCanMessageDescription.h"
#include "XMemory.h"
#include <string.h>

// =============== 初始化与清理 ===============

void XCanMessageDescription_init(XCanMessageDescription* msg)
{
    if (!msg) return;
    memset(msg, 0, sizeof(XCanMessageDescription));
    msg->m_uniqueId = 0;
    msg->m_size = 0;
}

void XCanMessageDescription_deinit(XCanMessageDescription* msg)
{
    if (!msg) return;
    if (msg->m_name) { XString_delete_base(msg->m_name); msg->m_name = NULL; }
    if (msg->m_transmitter) { XString_delete_base(msg->m_transmitter); msg->m_transmitter = NULL; }
    if (msg->m_comment) { XString_delete_base(msg->m_comment); msg->m_comment = NULL; }
    if (msg->m_signalDescriptions) { XVector_delete_base(msg->m_signalDescriptions); msg->m_signalDescriptions = NULL; }
}

void XCanMessageDescription_copy(XCanMessageDescription* dest, const XCanMessageDescription* src)
{
    if (!dest || !src) return;
    XCanMessageDescription_deinit(dest);

    dest->m_uniqueId = src->m_uniqueId;
    dest->m_size = src->m_size;

    if (src->m_name) dest->m_name = XString_create_copy(src->m_name);
    if (src->m_transmitter) dest->m_transmitter = XString_create_copy(src->m_transmitter);
    if (src->m_comment) dest->m_comment = XString_create_copy(src->m_comment);

    if (src->m_signalDescriptions) {
        dest->m_signalDescriptions = XVector_create_copy(src->m_signalDescriptions);
    }
}

void XCanMessageDescription_move(XCanMessageDescription* dest, XCanMessageDescription* src)
{
    if (!dest || !src) return;
    XCanMessageDescription_deinit(dest);
    /* 转移指针所有权 */
    dest->m_name = src->m_name;
    dest->m_transmitter = src->m_transmitter;
    dest->m_comment = src->m_comment;
    dest->m_signalDescriptions = src->m_signalDescriptions;
    /* 拷贝标量值 */
    dest->m_uniqueId = src->m_uniqueId;
    dest->m_size = src->m_size;
    /* 清空源对象，防止双重释放 */
    memset(src, 0, sizeof(XCanMessageDescription));
}

bool XCanMessageDescription_isValid(const XCanMessageDescription* msg)
{
    if (!msg) return false;
    return msg->m_uniqueId > 0 || (msg->m_name != NULL);
}

// =============== 属性访问 ===============

XCanBus_UniqueId XCanMessageDescription_uniqueId(const XCanMessageDescription* msg)
{
    return msg ? msg->m_uniqueId : 0;
}

void XCanMessageDescription_setUniqueId(XCanMessageDescription* msg, XCanBus_UniqueId id)
{
    if (msg) msg->m_uniqueId = id;
}

XString* XCanMessageDescription_name(const XCanMessageDescription* msg)
{
    if (!msg || !msg->m_name) return XString_create();
    return XString_create_copy(msg->m_name);
}

void XCanMessageDescription_setName(XCanMessageDescription* msg, const char* name)
{
    if (!msg) return;
    if (msg->m_name) { XString_delete_base(msg->m_name); msg->m_name = NULL; }
    if (name) msg->m_name = XString_create_utf8(name);
}

uint8_t XCanMessageDescription_size(const XCanMessageDescription* msg)
{
    return msg ? msg->m_size : 0;
}

void XCanMessageDescription_setSize(XCanMessageDescription* msg, uint8_t size)
{
    if (msg) msg->m_size = size;
}

XString* XCanMessageDescription_transmitter(const XCanMessageDescription* msg)
{
    if (!msg || !msg->m_transmitter) return XString_create();
    return XString_create_copy(msg->m_transmitter);
}

void XCanMessageDescription_setTransmitter(XCanMessageDescription* msg, const char* transmitter)
{
    if (!msg) return;
    if (msg->m_transmitter) { XString_delete_base(msg->m_transmitter); msg->m_transmitter = NULL; }
    if (transmitter) msg->m_transmitter = XString_create_utf8(transmitter);
}

XString* XCanMessageDescription_comment(const XCanMessageDescription* msg)
{
    if (!msg || !msg->m_comment) return XString_create();
    return XString_create_copy(msg->m_comment);
}

void XCanMessageDescription_setComment(XCanMessageDescription* msg, const char* text)
{
    if (!msg) return;
    if (msg->m_comment) { XString_delete_base(msg->m_comment); msg->m_comment = NULL; }
    if (text) msg->m_comment = XString_create_utf8(text);
}

XVector* XCanMessageDescription_signalDescriptions(const XCanMessageDescription* msg)
{
    if (!msg || !msg->m_signalDescriptions)
        return XVector_create(sizeof(XCanSignalDescription));
    return XVector_create_copy(msg->m_signalDescriptions);
}

bool XCanMessageDescription_signalDescriptionForName(const XCanMessageDescription* msg,
    const char* name, XCanSignalDescription* out)
{
    if (!msg || !name || !out || !msg->m_signalDescriptions) return false;

    size_t count = XVector_size_base(msg->m_signalDescriptions);
    for (size_t i = 0; i < count; i++) {
        XCanSignalDescription* sig = (XCanSignalDescription*)XVector_at_base(msg->m_signalDescriptions, i);
        if (sig && sig->m_name) {
            const char* sigName = XString_toUtf8(sig->m_name);
            if (sigName && strcmp(sigName, name) == 0) {
                XCanSignalDescription_copy(out, sig);
                return true;
            }
        }
    }
    return false;
}

void XCanMessageDescription_clearSignalDescriptions(XCanMessageDescription* msg)
{
    if (!msg || !msg->m_signalDescriptions) return;
    XVector_clear_base(msg->m_signalDescriptions);
}

void XCanMessageDescription_addSignalDescription(XCanMessageDescription* msg,
    const XCanSignalDescription* description)
{
    if (!msg || !description) return;
    if (!msg->m_signalDescriptions) {
        msg->m_signalDescriptions = XVector_create(sizeof(XCanSignalDescription));
        /* 设置深拷贝/移动/析构方法，确保容器内元素正确管理生命周期 */
        XContainerSetDataCopyMethod(msg->m_signalDescriptions, XCanSignalDescription_copy);
        XContainerSetDataMoveMethod(msg->m_signalDescriptions, XCanSignalDescription_move);
        XContainerSetDataDeinitMethod(msg->m_signalDescriptions, XCanSignalDescription_deinit);
    }
    XVector_push_back_1_base(msg->m_signalDescriptions, (void*)description);
}

void XCanMessageDescription_setSignalDescriptions(XCanMessageDescription* msg,
    const XVector* descriptions)
{
    if (!msg) return;
    if (msg->m_signalDescriptions) {
        XVector_delete_base(msg->m_signalDescriptions);
        msg->m_signalDescriptions = NULL;
    }
    if (descriptions)
        msg->m_signalDescriptions = XVector_create_copy(descriptions);
}

#endif /* XCAN_DBC_ON */
#endif /* XCAN_ON */
#endif /* XPROTOCOL_ON */
