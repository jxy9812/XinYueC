#include "XCanSignalDescription.h"
#include "XMemory.h"
#include <string.h>

// =============== 初始化与清理 ===============

void XCanSignalDescription_init(XCanSignalDescription* sig)
{
    if (!sig) return;
    memset(sig, 0, sizeof(XCanSignalDescription));
    sig->m_dataSource = XCanBus_Payload;
    sig->m_dataEndian = 1; // 默认大端
    sig->m_dataFormat = XCanBus_UnsignedInteger;
    sig->m_factor = 1.0;
    sig->m_offset = 0.0;
    sig->m_scaling = 1.0;
    sig->m_minimum = 0.0;
    sig->m_maximum = 0.0;
    sig->m_multiplexState = XCanBus_MultiplexState_None;
}

void XCanSignalDescription_deinit(XCanSignalDescription* sig)
{
    if (!sig) return;
    if (sig->m_name) { XString_delete_base(sig->m_name); sig->m_name = NULL; }
    if (sig->m_physicalUnit) { XString_delete_base(sig->m_physicalUnit); sig->m_physicalUnit = NULL; }
    if (sig->m_receiver) { XString_delete_base(sig->m_receiver); sig->m_receiver = NULL; }
    if (sig->m_comment) { XString_delete_base(sig->m_comment); sig->m_comment = NULL; }
    if (sig->m_multiplexSignals) { XMap_delete_base(sig->m_multiplexSignals); sig->m_multiplexSignals = NULL; }
}

void XCanSignalDescription_copy(XCanSignalDescription* dest, const XCanSignalDescription* src)
{
    if (!dest || !src) return;
    XCanSignalDescription_deinit(dest);

    if (src->m_name) dest->m_name = XString_create_copy(src->m_name);
    if (src->m_physicalUnit) dest->m_physicalUnit = XString_create_copy(src->m_physicalUnit);
    if (src->m_receiver) dest->m_receiver = XString_create_copy(src->m_receiver);
    if (src->m_comment) dest->m_comment = XString_create_copy(src->m_comment);

    dest->m_dataSource = src->m_dataSource;
    dest->m_dataEndian = src->m_dataEndian;
    dest->m_dataFormat = src->m_dataFormat;
    dest->m_startBit = src->m_startBit;
    dest->m_bitLength = src->m_bitLength;
    dest->m_factor = src->m_factor;
    dest->m_offset = src->m_offset;
    dest->m_scaling = src->m_scaling;
    dest->m_minimum = src->m_minimum;
    dest->m_maximum = src->m_maximum;
    dest->m_multiplexState = src->m_multiplexState;

    if (src->m_multiplexSignals)
        dest->m_multiplexSignals = XMap_create_copy(src->m_multiplexSignals);
}

void XCanSignalDescription_move(XCanSignalDescription* dest, XCanSignalDescription* src)
{
    if (!dest || !src) return;
    XCanSignalDescription_deinit(dest);
    /* 转移指针所有权 */
    dest->m_name = src->m_name;
    dest->m_physicalUnit = src->m_physicalUnit;
    dest->m_receiver = src->m_receiver;
    dest->m_comment = src->m_comment;
    dest->m_multiplexSignals = src->m_multiplexSignals;
    /* 拷贝标量值 */
    dest->m_dataSource = src->m_dataSource;
    dest->m_dataEndian = src->m_dataEndian;
    dest->m_dataFormat = src->m_dataFormat;
    dest->m_startBit = src->m_startBit;
    dest->m_bitLength = src->m_bitLength;
    dest->m_factor = src->m_factor;
    dest->m_offset = src->m_offset;
    dest->m_scaling = src->m_scaling;
    dest->m_minimum = src->m_minimum;
    dest->m_maximum = src->m_maximum;
    dest->m_multiplexState = src->m_multiplexState;
    /* 清空源对象，防止双重释放 */
    memset(src, 0, sizeof(XCanSignalDescription));
}

bool XCanSignalDescription_isValid(const XCanSignalDescription* sig)
{
    if (!sig) return false;
    return sig->m_name != NULL && sig->m_bitLength > 0;
}

// =============== 属性访问 ===============

XString* XCanSignalDescription_name(const XCanSignalDescription* sig)
{
    if (!sig || !sig->m_name) return XString_create();
    return XString_create_copy(sig->m_name);
}

void XCanSignalDescription_setName(XCanSignalDescription* sig, const char* name)
{
    if (!sig) return;
    if (sig->m_name) { XString_delete_base(sig->m_name); sig->m_name = NULL; }
    if (name) sig->m_name = XString_create_utf8(name);
}

XString* XCanSignalDescription_physicalUnit(const XCanSignalDescription* sig)
{
    if (!sig || !sig->m_physicalUnit) return XString_create();
    return XString_create_copy(sig->m_physicalUnit);
}

void XCanSignalDescription_setPhysicalUnit(XCanSignalDescription* sig, const char* unit)
{
    if (!sig) return;
    if (sig->m_physicalUnit) { XString_delete_base(sig->m_physicalUnit); sig->m_physicalUnit = NULL; }
    if (unit) sig->m_physicalUnit = XString_create_utf8(unit);
}

XString* XCanSignalDescription_receiver(const XCanSignalDescription* sig)
{
    if (!sig || !sig->m_receiver) return XString_create();
    return XString_create_copy(sig->m_receiver);
}

void XCanSignalDescription_setReceiver(XCanSignalDescription* sig, const char* receiver)
{
    if (!sig) return;
    if (sig->m_receiver) { XString_delete_base(sig->m_receiver); sig->m_receiver = NULL; }
    if (receiver) sig->m_receiver = XString_create_utf8(receiver);
}

XString* XCanSignalDescription_comment(const XCanSignalDescription* sig)
{
    if (!sig || !sig->m_comment) return XString_create();
    return XString_create_copy(sig->m_comment);
}

void XCanSignalDescription_setComment(XCanSignalDescription* sig, const char* text)
{
    if (!sig) return;
    if (sig->m_comment) { XString_delete_base(sig->m_comment); sig->m_comment = NULL; }
    if (text) sig->m_comment = XString_create_utf8(text);
}

XCanBus_DataSource XCanSignalDescription_dataSource(const XCanSignalDescription* sig)
{
    return sig ? sig->m_dataSource : XCanBus_Payload;
}

void XCanSignalDescription_setDataSource(XCanSignalDescription* sig, XCanBus_DataSource source)
{
    if (sig) sig->m_dataSource = source;
}

uint8_t XCanSignalDescription_dataEndian(const XCanSignalDescription* sig)
{
    return sig ? sig->m_dataEndian : 1;
}

void XCanSignalDescription_setDataEndian(XCanSignalDescription* sig, uint8_t endian)
{
    if (sig) sig->m_dataEndian = endian;
}

XCanBus_DataFormat XCanSignalDescription_dataFormat(const XCanSignalDescription* sig)
{
    return sig ? sig->m_dataFormat : XCanBus_UnsignedInteger;
}

void XCanSignalDescription_setDataFormat(XCanSignalDescription* sig, XCanBus_DataFormat format)
{
    if (sig) sig->m_dataFormat = format;
}

uint16_t XCanSignalDescription_startBit(const XCanSignalDescription* sig)
{
    return sig ? sig->m_startBit : 0;
}

void XCanSignalDescription_setStartBit(XCanSignalDescription* sig, uint16_t bit)
{
    if (sig) sig->m_startBit = bit;
}

uint16_t XCanSignalDescription_bitLength(const XCanSignalDescription* sig)
{
    return sig ? sig->m_bitLength : 0;
}

void XCanSignalDescription_setBitLength(XCanSignalDescription* sig, uint16_t length)
{
    if (sig) sig->m_bitLength = length;
}

double XCanSignalDescription_factor(const XCanSignalDescription* sig)
{
    return sig ? sig->m_factor : 1.0;
}

void XCanSignalDescription_setFactor(XCanSignalDescription* sig, double factor)
{
    if (sig) sig->m_factor = factor;
}

double XCanSignalDescription_offset(const XCanSignalDescription* sig)
{
    return sig ? sig->m_offset : 0.0;
}

void XCanSignalDescription_setOffset(XCanSignalDescription* sig, double offset)
{
    if (sig) sig->m_offset = offset;
}

double XCanSignalDescription_scaling(const XCanSignalDescription* sig)
{
    return sig ? sig->m_scaling : 1.0;
}

void XCanSignalDescription_setScaling(XCanSignalDescription* sig, double scaling)
{
    if (sig) sig->m_scaling = scaling;
}

double XCanSignalDescription_minimum(const XCanSignalDescription* sig)
{
    return sig ? sig->m_minimum : 0.0;
}

double XCanSignalDescription_maximum(const XCanSignalDescription* sig)
{
    return sig ? sig->m_maximum : 0.0;
}

void XCanSignalDescription_setRange(XCanSignalDescription* sig, double minimum, double maximum)
{
    if (sig) {
        sig->m_minimum = minimum;
        sig->m_maximum = maximum;
    }
}

XCanBus_MultiplexState XCanSignalDescription_multiplexState(const XCanSignalDescription* sig)
{
    return sig ? sig->m_multiplexState : XCanBus_MultiplexState_None;
}

void XCanSignalDescription_setMultiplexState(XCanSignalDescription* sig, XCanBus_MultiplexState state)
{
    if (sig) sig->m_multiplexState = state;
}

XMap* XCanSignalDescription_multiplexSignals(const XCanSignalDescription* sig)
{
    if (!sig || !sig->m_multiplexSignals) {
        XMap* map = XMap_create(sizeof(XString), sizeof(XCanSignalDescription_MultiplexValueRange), XString_compare);
        XMapBaseSetKeyCopyMethod(map, XString_copy_base);
        XMapBaseSetKeyMoveMethod(map, XString_move_base);
        XMapBaseSetKeyDeinitMethod(map, XClass_deinit_base);
        return map;
    }
    return XMap_create_copy(sig->m_multiplexSignals);
}

void XCanSignalDescription_clearMultiplexSignals(XCanSignalDescription* sig)
{
    if (!sig || !sig->m_multiplexSignals) return;
    XMap_clear_base(sig->m_multiplexSignals);
}

void XCanSignalDescription_setMultiplexSignals(XCanSignalDescription* sig, const XMap* multiplexorSignals)
{
    if (!sig) return;
    if (sig->m_multiplexSignals) {
        XMap_delete_base(sig->m_multiplexSignals);
        sig->m_multiplexSignals = NULL;
    }
    if (multiplexorSignals)
        sig->m_multiplexSignals = XMap_create_copy(multiplexorSignals);
}

void XCanSignalDescription_addMultiplexSignal(XCanSignalDescription* sig,
    const char* name, const XVector* ranges)
{
    if (!sig || !name || !ranges) return;
    if (!sig->m_multiplexSignals) {
        sig->m_multiplexSignals = XMap_create(sizeof(XString), sizeof(XCanSignalDescription_MultiplexValueRange), XString_compare);
        XMapBaseSetKeyCopyMethod(sig->m_multiplexSignals, XString_copy_base);
        XMapBaseSetKeyMoveMethod(sig->m_multiplexSignals, XString_move_base);
        XMapBaseSetKeyDeinitMethod(sig->m_multiplexSignals, XClass_deinit_base);
    }

    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, name);

    XCanSignalDescription_MultiplexValueRange range;
    memset(&range, 0, sizeof(range));
    XMapBase_insert_base((XMapBase*)sig->m_multiplexSignals, &key, &range);

    XClass_deinit_base((XClass*)&key);
}

void XCanSignalDescription_addMultiplexSignal_value(XCanSignalDescription* sig,
    const char* name, const XVariant* value)
{
    if (!sig || !name || !value) return;
    if (!sig->m_multiplexSignals) {
        sig->m_multiplexSignals = XMap_create(sizeof(XString), sizeof(XCanSignalDescription_MultiplexValueRange), XString_compare);
        XMapBaseSetKeyCopyMethod(sig->m_multiplexSignals, XString_copy_base);
        XMapBaseSetKeyMoveMethod(sig->m_multiplexSignals, XString_move_base);
        XMapBaseSetKeyDeinitMethod(sig->m_multiplexSignals, XClass_deinit_base);
    }

    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, name);

    XCanSignalDescription_MultiplexValueRange range;
    memset(&range, 0, sizeof(range));
    range.m_minimum = XVariant_create_copy(value);
    range.m_maximum = XVariant_create_copy(value);
    XMapBase_insert_base((XMapBase*)sig->m_multiplexSignals, &key, &range);

    XClass_deinit_base((XClass*)&key);
}
