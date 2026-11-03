/**
 * @file       XDataStream.c
 * @brief      二进制数据流实现（对标 Qt 6.8 QDataStream）。
 * @details    线格式对齐 qtbase/src/corelib/serialization/qdatastream.cpp 与
 *             qstring.cpp/qbytearray.cpp 的流操作符：长度前缀（quint32，
 *             扩展/空标记）、按字节序的定长整型、浮点位模式、字符串按
 *             UTF-16 码元序列。实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XDataStream.h"

#include "XMemory.h"
#include "XChar.h"
#include <string.h>

/* 对齐 QDataStreamPrivate 的两个常量（qdatastream.h:239-240）。 */
#define XDS_NULL_CODE 0xffffffffu
#define XDS_EXTENDED_SIZE 0xfffffffeu

/* ==================== 生命周期 ==================== */

static void VXDataStream_deinit(XDataStream* self)
{
    if (!self) return;
    /* 设备/数组均为借用，不释放；仅清引用防悬垂。 */
    self->m_device = NULL;
    self->m_array = NULL;
    self->m_arrayPos = 0;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

XVtable* XDataStream_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDataStream)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDataStream_deinit);
    return XVTABLE_DEFAULT;
}

void XDataStream_init(XDataStream* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XDataStream);
    self->m_device = NULL;
    self->m_array = NULL;
    self->m_arrayPos = 0;
    /* 缺省值对齐 Qt：版本=编译期缺省、大端、双精度、状态 Ok。 */
    self->m_version = (int)XDataStream_Version_Default;
    self->m_byteOrder = (int)XDataStream_ByteOrder_BigEndian;
    self->m_fpPrecision = (int)XDataStream_FloatingPointPrecision_Double;
    self->m_status = (int)XDataStream_Status_Ok;
    self->m_transactionDepth = 0;
    self->m_readOnly = 0;
}

void XDataStream_init_device(XDataStream* self, XIODevice* device)
{
    if (!self) return;
    XDataStream_init(self);
    self->m_device = device;
}

void XDataStream_init_array(XDataStream* self, XByteArray* array)
{
    if (!self) return;
    XDataStream_init(self);
    self->m_array = array;
    /* 写模式：游标停在数组末尾（追加写，对齐 Qt 写模式）。 */
    self->m_arrayPos = array
        ? (int64_t)XByteArray_size_base(array) : 0;
}

void XDataStream_init_arrayReadOnly(XDataStream* self,
                                    const XByteArray* array)
{
    if (!self) return;
    XDataStream_init(self);
    /* 借用承载：const 来源按只读记录，写操作将被拒绝。 */
    self->m_array = (XByteArray*)array;
    self->m_arrayPos = 0;
    self->m_readOnly = 1;
}

XDataStream* XDataStream_create_ex(XMemoryType memory)
{
    XDataStream* self = (XDataStream*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDataStream_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 承载与状态 ==================== */

XIODevice* XDataStream_device(const XDataStream* self)
{ return self ? self->m_device : NULL; }

void XDataStream_setDevice(XDataStream* self, XIODevice* device)
{
    if (!self) return;
    self->m_device = device;
    /* 对齐 Qt：换绑设备后不再走数组模式。 */
    self->m_array = NULL;
    self->m_arrayPos = 0;
}

bool XDataStream_atEnd(const XDataStream* self)
{
    if (!self) return true;
    if (self->m_device) return XIODevice_atEnd_base(self->m_device);
    if (self->m_array)
        return self->m_arrayPos >= (int64_t)XByteArray_size_base(
                                        self->m_array);
    return true;
}

int XDataStream_status(const XDataStream* self)
{ return self ? self->m_status : (int)XDataStream_Status_Ok; }

void XDataStream_setStatus(XDataStream* self, int status)
{ if (self) self->m_status = status; }

void XDataStream_resetStatus(XDataStream* self)
{ if (self) self->m_status = (int)XDataStream_Status_Ok; }

int XDataStream_floatingPointPrecision(const XDataStream* self)
{
    return self ? self->m_fpPrecision
                : (int)XDataStream_FloatingPointPrecision_Double;
}

void XDataStream_setFloatingPointPrecision(XDataStream* self, int precision)
{ if (self) self->m_fpPrecision = precision; }

int XDataStream_byteOrder(const XDataStream* self)
{
    return self ? self->m_byteOrder
                : (int)XDataStream_ByteOrder_BigEndian;
}

void XDataStream_setByteOrder(XDataStream* self, int order)
{ if (self) self->m_byteOrder = order; }

int XDataStream_version(const XDataStream* self)
{ return self ? self->m_version : (int)XDataStream_Version_Default; }

void XDataStream_setVersion(XDataStream* self, int version)
{ if (self) self->m_version = version; }

/* ==================== 原始字节 ==================== */

int64_t XDataStream_writeRawData(XDataStream* self, const char* data,
                                 int64_t len)
{
    if (!self) return -1;
    if (len < 0) {
        self->m_status = (int)XDataStream_Status_WriteFailed;
        return -1;
    }
    if (len == 0) return 0;
    if (!data) {
        self->m_status = (int)XDataStream_Status_WriteFailed;
        return -1;
    }
    if (self->m_readOnly) {
        self->m_status = (int)XDataStream_Status_WriteFailed;
        return -1;
    }
    if (self->m_device) {
        int64_t w = XIODevice_write_1(self->m_device, data, len);
        if (w < 0 || w != len) {
            self->m_status = (int)XDataStream_Status_WriteFailed;
            return w < 0 ? -1 : w;
        }
        return w;
    }
    if (self->m_array) {
        /* 追加写（对齐 Qt 写模式：写到数组末尾）。 */
        if (!XVector_push_back_2((XVector*)self->m_array, data,
                                 (size_t)len)) {
            self->m_status = (int)XDataStream_Status_WriteFailed;
            return -1;
        }
        self->m_arrayPos += len;
        return len;
    }
    self->m_status = (int)XDataStream_Status_WriteFailed;
    return -1;
}

int64_t XDataStream_readRawData(XDataStream* self, char* data, int64_t len)
{
    int64_t got = 0;
    if (!self) return -1;
    if (len <= 0) return 0;
    if (!data) {
        self->m_status = (int)XDataStream_Status_ReadCorruptData;
        return -1;
    }
    if (self->m_device) {
        got = XIODevice_read_1(self->m_device, data, len);
        if (got < 0) got = 0;
        if (got < len)
            self->m_status = (int)XDataStream_Status_ReadPastEnd;
        return got;
    }
    if (self->m_array) {
        int64_t total = (int64_t)XByteArray_size_base(self->m_array);
        int64_t avail = total - self->m_arrayPos;
        if (avail <= 0) {
            self->m_status = (int)XDataStream_Status_ReadPastEnd;
            return 0;
        }
        got = avail < len ? avail : len;
        XMemcpy(data, XByteArray_data(self->m_array) + self->m_arrayPos,
                (size_t)got);
        self->m_arrayPos += got;
        if (got < len)
            self->m_status = (int)XDataStream_Status_ReadPastEnd;
        return got;
    }
    self->m_status = (int)XDataStream_Status_ReadPastEnd;
    return 0;
}

int64_t XDataStream_skipRawData(XDataStream* self, int64_t len)
{
    char scratch[256];
    int64_t left;
    int64_t done = 0;
    if (!self) return -1;
    if (len <= 0) return 0;
    if (self->m_device && !XIODevice_isSequential(self->m_device)) {
        /* 可 seek 设备直接定位（对齐 Qt skipRawData 的实现取向）。 */
        int64_t pos = XIODevice_pos_base(self->m_device);
        if (pos >= 0 && XIODevice_seek_base(self->m_device, pos + len))
            return len;
    }
    if (self->m_device) {
        int64_t s = XIODevice_skip(self->m_device, len);
        if (s < 0) s = 0;
        if (s < len) self->m_status = (int)XDataStream_Status_ReadPastEnd;
        return s;
    }
    if (self->m_array) {
        int64_t total = (int64_t)XByteArray_size_base(self->m_array);
        int64_t avail = total - self->m_arrayPos;
        if (avail <= 0) {
            self->m_status = (int)XDataStream_Status_ReadPastEnd;
            return 0;
        }
        done = avail < len ? avail : len;
        self->m_arrayPos += done;
        if (done < len)
            self->m_status = (int)XDataStream_Status_ReadPastEnd;
        return done;
    }
    /* 无承载：逐块读实现跳过（与 Qt 语义一致：读到多少算多少）。 */
    left = len;
    while (left > 0) {
        int64_t chunk = left > (int64_t)sizeof(scratch) ? (int64_t)sizeof(scratch)
                                                        : left;
        int64_t r = XDataStream_readRawData(self, scratch, chunk);
        if (r <= 0) break;
        done += r;
        left -= r;
    }
    return done;
}

/* ==================== 长度前缀字节块 ==================== */

/** @brief 写尺寸类型（对齐 `QDataStream::writeQSizeType`）。
 *  @details `value <= 0xfffffffd` 写 quint32；否则写 0xfffffffe 标记 +
 *           quint64（负数按 null 标记 0xffffffff 写，对齐 Qt）。 */
static bool xds_writeQSizeType(XDataStream* self, int64_t value)
{
    if (value < 0) {
        if (!XDataStream_writeUInt32(self, XDS_NULL_CODE)) return false;
        return true;
    }
    if (value <= (int64_t)(XDS_EXTENDED_SIZE - 1)) {
        if (!XDataStream_writeUInt32(self, (uint32_t)value)) return false;
        return true;
    }
    if (!XDataStream_writeUInt32(self, XDS_EXTENDED_SIZE)) return false;
    return XDataStream_writeUInt64(self, (uint64_t)value);
}

/** @brief 读尺寸类型（对齐 `QDataStream::readQSizeType`）。 */
static int64_t xds_readQSizeType(XDataStream* self)
{
    uint32_t tag = 0;
    uint64_t ext = 0;
    if (!XDataStream_readUInt32(self, &tag)) return -1;
    if (tag == XDS_NULL_CODE) return -1;          /* null 标记。 */
    if (tag == XDS_EXTENDED_SIZE) {
        if (!XDataStream_readUInt64(self, &ext)) return -1;
        if (ext > 0x7fffffffffffffffULL) {
            self->m_status = (int)XDataStream_Status_SizeLimitExceeded;
            return -1;
        }
        return (int64_t)ext;
    }
    return (int64_t)tag;
}

bool XDataStream_writeBytes(XDataStream* self, const char* data, int64_t len)
{
    if (!self) return false;
    if (len < 0) {
        self->m_status = (int)XDataStream_Status_WriteFailed;
        return false;
    }
    if (!xds_writeQSizeType(self, len)) return false;
    if (len > 0 && !XDataStream_writeRawData(self, data, len)) return false;
    return true;
}

bool XDataStream_readBytes(XDataStream* self, char* out, int64_t* lenInOut)
{
    int64_t len;
    if (!self || !lenInOut) return false;
    len = xds_readQSizeType(self);
    if (len < 0) {
        /* null 标记：长度 0；其余失败状态已置。 */
        *lenInOut = 0;
        return self->m_status == (int)XDataStream_Status_Ok;
    }
    if (len > *lenInOut) {
        /* 容量不足：按损坏数据处理（不截断，避免静默丢数据）。 */
        self->m_status = (int)XDataStream_Status_ReadCorruptData;
        *lenInOut = 0;
        return false;
    }
    if (len > 0) {
        int64_t got = XDataStream_readRawData(self, out, len);
        if (got != len) {
            *lenInOut = got > 0 ? got : 0;
            return false;
        }
    }
    *lenInOut = len;
    return true;
}

/* ==================== 定长整型 ==================== */

/** @brief 按流字节序写无符号整数（width = 1/2/4/8）。 */
static bool xds_writeUInt(XDataStream* self, uint64_t value, int width)
{
    uint8_t buf[8];
    int i;
    if (!self) return false;
    if (self->m_byteOrder == (int)XDataStream_ByteOrder_LittleEndian) {
        for (i = 0; i < width; ++i)
            buf[i] = (uint8_t)((value >> (8 * i)) & 0xFFu);
    } else {
        /* 大端：高位在前。 */
        for (i = 0; i < width; ++i)
            buf[i] = (uint8_t)((value >> (8 * (width - 1 - i))) & 0xFFu);
    }
    return XDataStream_writeRawData(self, (const char*)buf, width) == width;
}

/** @brief 按流字节序读无符号整数（width = 1/2/4/8）。 */
static bool xds_readUInt(XDataStream* self, uint64_t* out, int width)
{
    uint8_t buf[8];
    int i;
    uint64_t value = 0;
    if (!self || !out) return false;
    if (XDataStream_readRawData(self, (char*)buf, width) != width)
        return false;
    if (self->m_byteOrder == (int)XDataStream_ByteOrder_LittleEndian) {
        for (i = 0; i < width; ++i)
            value |= ((uint64_t)buf[i]) << (8 * i);
    } else {
        for (i = 0; i < width; ++i)
            value |= ((uint64_t)buf[i]) << (8 * (width - 1 - i));
    }
    *out = value;
    return true;
}

bool XDataStream_writeInt8(XDataStream* self, int8_t value)
{ return xds_writeUInt(self, (uint64_t)(uint8_t)value, 1); }
bool XDataStream_readInt8(XDataStream* self, int8_t* out)
{
    uint64_t v = 0;
    if (!xds_readUInt(self, &v, 1)) return false;
    if (out) *out = (int8_t)(uint8_t)v;
    return true;
}
bool XDataStream_writeUInt8(XDataStream* self, uint8_t value)
{ return xds_writeUInt(self, (uint64_t)value, 1); }
bool XDataStream_readUInt8(XDataStream* self, uint8_t* out)
{
    uint64_t v = 0;
    if (!xds_readUInt(self, &v, 1)) return false;
    if (out) *out = (uint8_t)v;
    return true;
}
bool XDataStream_writeInt16(XDataStream* self, int16_t value)
{ return xds_writeUInt(self, (uint64_t)(uint16_t)value, 2); }
bool XDataStream_readInt16(XDataStream* self, int16_t* out)
{
    uint64_t v = 0;
    if (!xds_readUInt(self, &v, 2)) return false;
    if (out) *out = (int16_t)(uint16_t)v;
    return true;
}
bool XDataStream_writeUInt16(XDataStream* self, uint16_t value)
{ return xds_writeUInt(self, (uint64_t)value, 2); }
bool XDataStream_readUInt16(XDataStream* self, uint16_t* out)
{
    uint64_t v = 0;
    if (!xds_readUInt(self, &v, 2)) return false;
    if (out) *out = (uint16_t)v;
    return true;
}
bool XDataStream_writeInt32(XDataStream* self, int32_t value)
{ return xds_writeUInt(self, (uint64_t)(uint32_t)value, 4); }
bool XDataStream_readInt32(XDataStream* self, int32_t* out)
{
    uint64_t v = 0;
    if (!xds_readUInt(self, &v, 4)) return false;
    if (out) *out = (int32_t)(uint32_t)v;
    return true;
}
bool XDataStream_writeUInt32(XDataStream* self, uint32_t value)
{ return xds_writeUInt(self, (uint64_t)value, 4); }
bool XDataStream_readUInt32(XDataStream* self, uint32_t* out)
{
    uint64_t v = 0;
    if (!xds_readUInt(self, &v, 4)) return false;
    if (out) *out = (uint32_t)v;
    return true;
}
bool XDataStream_writeInt64(XDataStream* self, int64_t value)
{ return xds_writeUInt(self, (uint64_t)value, 8); }
bool XDataStream_readInt64(XDataStream* self, int64_t* out)
{
    uint64_t v = 0;
    if (!xds_readUInt(self, &v, 8)) return false;
    if (out) *out = (int64_t)v;
    return true;
}
bool XDataStream_writeUInt64(XDataStream* self, uint64_t value)
{ return xds_writeUInt(self, value, 8); }
bool XDataStream_readUInt64(XDataStream* self, uint64_t* out)
{
    uint64_t v = 0;
    if (!xds_readUInt(self, &v, 8)) return false;
    if (out) *out = v;
    return true;
}

/* ==================== 布尔与浮点 ==================== */

/** @brief 布尔统一按 1 字节写出（对齐 Qt 的 `operator<<(bool)`）。 */
bool XDataStream_writeBool(XDataStream* self, bool value)
{ return XDataStream_writeInt8(self, value ? 1 : 0); }

bool XDataStream_readBool(XDataStream* self, bool* out)
{
    int8_t v = 0;
    if (!XDataStream_readInt8(self, &v)) return false;
    if (out) *out = (v != 0);
    return true;
}

bool XDataStream_writeFloat(XDataStream* self, float value)
{
    uint32_t bits;
    /* IEEE-754 位模式直写（对齐 Qt bswap/位拷贝口径）。 */
    XMemcpy(&bits, &value, sizeof(bits));
    return XDataStream_writeUInt32(self, bits);
}

bool XDataStream_readFloat(XDataStream* self, float* out)
{
    uint32_t bits = 0;
    float v;
    if (!XDataStream_readUInt32(self, &bits)) return false;
    XMemcpy(&v, &bits, sizeof(v));
    if (out) *out = v;
    return true;
}

bool XDataStream_writeDouble(XDataStream* self, double value)
{
    uint64_t bits;
    if (!self) return false;
    /* 单精度模式：降精度为 float 写 4 字节（对齐 Qt 精度语义）。 */
    if (self->m_fpPrecision ==
        (int)XDataStream_FloatingPointPrecision_Single) {
        float f = (float)value;
        uint32_t fb;
        XMemcpy(&fb, &f, sizeof(fb));
        return XDataStream_writeUInt32(self, fb);
    }
    XMemcpy(&bits, &value, sizeof(bits));
    return XDataStream_writeUInt64(self, bits);
}

bool XDataStream_readDouble(XDataStream* self, double* out)
{
    if (!self) return false;
    if (self->m_fpPrecision ==
        (int)XDataStream_FloatingPointPrecision_Single) {
        uint32_t fb = 0;
        float f;
        if (!XDataStream_readUInt32(self, &fb)) return false;
        XMemcpy(&f, &fb, sizeof(f));
        if (out) *out = (double)f;
        return true;
    }
    {
        uint64_t bits = 0;
        double v;
        if (!XDataStream_readUInt64(self, &bits)) return false;
        XMemcpy(&v, &bits, sizeof(v));
        if (out) *out = v;
        return true;
    }
}

/* ==================== 字符串与字节数组 ==================== */

/** @brief 写 UTF-16 码元序列（length 为码元数，按字节序逐 2 字节）。 */
static bool xds_writeUtf16(XDataStream* self, const uint16_t* units,
                           int64_t count)
{
    int64_t i;
    if (!self) return false;
    for (i = 0; i < count; ++i)
        if (!xds_writeUInt(self, (uint64_t)units[i], 2)) return false;
    return true;
}

/** @brief 读 UTF-16 码元序列到调用方缓冲。 */
static bool xds_readUtf16(XDataStream* self, uint16_t* units, int64_t count)
{
    int64_t i;
    uint64_t v = 0;
    if (!self || (!units && count > 0)) return false;
    for (i = 0; i < count; ++i) {
        if (!xds_readUInt(self, &v, 2)) return false;
        units[i] = (uint16_t)v;
    }
    return true;
}

/** @brief 求 XString 的 UTF-16 码元数与首址。 */
static const uint16_t* xds_stringUnits(const XString* text, int64_t* countOut)
{
    const uint16_t* units;
    if (!text) { if (countOut) *countOut = 0; return NULL; }
    units = XString_utf16(text);
    if (countOut)
        *countOut = (int64_t)XContainer_size_base((const XContainer*)text);
    return units;
}

bool XDataStream_writeString(XDataStream* self, const XString* text)
{
    const uint16_t* units;
    int64_t count = 0;
    int ver;
    if (!self) return false;
    ver = self->m_version;
    units = xds_stringUnits(text, &count);

    /* 版本 1：走 Latin-1 字节数组路径（对齐 Qt qstring.cpp:9584-9585）。 */
    if (ver == (int)XDataStream_Version_1) {
        int64_t i;
        int64_t cap;
        char* latin;
        bool ok;
        if (!text || count == 0) return XDataStream_writeBytes(self, NULL, 0);
        /* UTF-16 → Latin-1：超出 0xFF 的码元按 '?' 落盘（Qt 亦为有损）。 */
        latin = (char*)XMalloc_System((size_t)count + 1);
        if (!latin) {
            self->m_status = (int)XDataStream_Status_WriteFailed;
            return false;
        }
        for (i = 0; i < count; ++i) {
            uint16_t u = units[i];
            /* Qt 取每码元低字节（toLatin1 对不可表示字符给 0x3F）。 */
            latin[i] = (u <= 0xFFu) ? (char)u : '?';
        }
        cap = count;
        ok = XDataStream_writeBytes(self, latin, cap);
        XFree_System(latin);
        return ok;
    }

    /* 版本 >= 3 且字符串为 null：写 null 标记（qstring.cpp:9597-9599）。 */
    if (!text && ver >= (int)XDataStream_Version_3)
        return xds_writeQSizeType(self, -1);

    /* 常规：长度为**字节数**（码元数 × 2），内容按字节序逐 16 位。 */
    if (!xds_writeQSizeType(self, count * 2)) return false;
    if (count > 0 && !xds_writeUtf16(self, units, count)) return false;
    return true;
}

XString* XDataStream_readString(XDataStream* self)
{
    XString* out;
    int ver;
    if (!self) return NULL;
    ver = self->m_version;
    /* 版本 1：读 Latin-1 字节数组再转字符串。 */
    if (ver == (int)XDataStream_Version_1) {
        int64_t len = 0;
        char* buf;
        XByteArray* tmp = XDataStream_readByteArray(self);
        if (!tmp) return NULL;
        len = (int64_t)XByteArray_size_base(tmp);
        if (len <= 0) {
            XClassDelete((XClass*)tmp);
            return XString_create();
        }
        /* Latin-1 → UTF-16：逐字节扩成码元（对齐 Qt toLatin1 逆转）。 */
        buf = (char*)XMalloc_System((size_t)len + 1);
        if (!buf) {
            XClassDelete((XClass*)tmp);
            return NULL;
        }
        XMemcpy(buf, XByteArray_data(tmp), (size_t)len);
        buf[len] = '\0';
        {
            uint16_t* units = (uint16_t*)XMalloc_System(
                sizeof(uint16_t) * ((size_t)len + 1));
            if (!units) {
                XFree_System(buf);
                XClassDelete((XClass*)tmp);
                return NULL;
            }
            {
                int64_t i;
                for (i = 0; i < len; ++i)
                    units[i] = (uint16_t)(uint8_t)buf[i];
                units[len] = 0;
            }
            out = XString_create_utf16(units);
            XFree_System(units);
        }
        XFree_System(buf);
        XClassDelete((XClass*)tmp);
        return out;
    }

    {
        int64_t bytes = xds_readQSizeType(self);
        int64_t count;
        uint16_t* units;
        if (bytes < 0) {
            /* null 标记：返回空串对象（非 NULL）。 */
            if (self->m_status != (int)XDataStream_Status_Ok) return NULL;
            return XString_create();
        }
        if ((bytes % 2) != 0) {
            self->m_status = (int)XDataStream_Status_ReadCorruptData;
            return NULL;
        }
        count = bytes / 2;
        if (count <= 0) return XString_create();
        units = (uint16_t*)XMalloc_System(
            sizeof(uint16_t) * ((size_t)count + 1));
        if (!units) return NULL;
        if (!xds_readUtf16(self, units, count)) {
            XFree_System(units);
            return NULL;
        }
        units[count] = 0;
        out = XString_create_utf16(units);
        XFree_System(units);
        return out;
    }
}

bool XDataStream_writeByteArray(XDataStream* self, const XByteArray* array)
{
    if (!self) return false;
    if (!array) {
        /* null 数组：版本 >= 3 写 null 标记；否则写 0 长度。 */
        if (self->m_version >= (int)XDataStream_Version_3)
            return xds_writeQSizeType(self, -1);
        return XDataStream_writeBytes(self, NULL, 0);
    }
    return XDataStream_writeBytes(
        self, (const char*)XByteArray_data((XByteArray*)array),
        (int64_t)XByteArray_size_base(array));
}

XByteArray* XDataStream_readByteArray(XDataStream* self)
{
    int64_t len;
    XByteArray* out;
    if (!self) return NULL;
    len = xds_readQSizeType(self);
    if (len < 0) {
        if (self->m_status != (int)XDataStream_Status_Ok) return NULL;
        return XByteArray_create();/* null 标记：空数组对象。 */
    }
    out = XByteArray_create();
    if (!out) return NULL;
    if (len > 0) {
        char* buf = (char*)XMalloc_System((size_t)len);
        if (!buf) {
            XClassDelete((XClass*)out);
            return NULL;
        }
        if (XDataStream_readRawData(self, buf, len) != len) {
            XFree_System(buf);
            XClassDelete((XClass*)out);
            return NULL;
        }
        if (!XVector_push_back_2((XVector*)out, buf, (size_t)len)) {
            XFree_System(buf);
            XClassDelete((XClass*)out);
            return NULL;
        }
        XFree_System(buf);
    }
    return out;
}

/* ==================== 事务 ==================== */

void XDataStream_startTransaction(XDataStream* self)
{
    if (!self) return;
    /* 对齐 Qt StreamStateSaver：非嵌套事务下复位状态。 */
    if (self->m_transactionDepth == 0) self->m_status =
        (int)XDataStream_Status_Ok;
    if (self->m_transactionDepth < 0xFFFF) ++self->m_transactionDepth;
}

bool XDataStream_commitTransaction(XDataStream* self)
{
    bool ok;
    if (!self || self->m_transactionDepth == 0) return false;
    ok = (self->m_status == (int)XDataStream_Status_Ok);
    --self->m_transactionDepth;
    return ok;
}

void XDataStream_rollbackTransaction(XDataStream* self)
{
    if (!self || self->m_transactionDepth == 0) return;
    --self->m_transactionDepth;
    /* 数组模式可还原游标（设备模式无通用回滚点，仅退栈）。 */
    if (self->m_array && self->m_readOnly) self->m_arrayPos = 0;
}

void XDataStream_abortTransaction(XDataStream* self)
{ XDataStream_rollbackTransaction(self); }

bool XDataStream_isDeviceTransactionStarted(const XDataStream* self)
{ return self ? (self->m_transactionDepth > 0) : false; }
