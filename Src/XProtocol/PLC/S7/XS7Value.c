#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON
#include "XS7Value.h"
#include "XMemory.h"
#include "XString.h"
#include "XChar.h"

/**
 * @file XS7Value.c
 * @brief S7 值编解码实现（类型 ⇄ 大端字节，纯函数，无IO无状态）
 * @details 编解码规则（与 XS7Value.h 契约一一对应）：
 * - 大端一律经 XMemory_read_data / XMemory_write_data(XBYTE_ORDER_BIG_ENDIAN)；
 * - float 经 uint32 位型中转（XMemcpy 位拷贝，禁 *(float*)&u32 强转）；
 * - S7 STRING(n) 布局 = [maxLen][curLen][data...]，总长 n+2 且偶数补齐
 *   （补齐字节恒为 0）；读长度 = n+2（count 即容量 n）；
 * - Bool 走位传输语义：多元素按"LSB 在前位打包"承载（bit k → 字节 k>>3
 *   的 k&7 位，载荷长 ceil(N/8) 字节），与 XS7Pdu 的 0x03/0x04 数据分支
 *   解析及真实 CPU 行为一致；单元素仍为 1 字节（0x00/0x01），对应请求 ANY
 *   transport=0x01、写数据 transport=0x03 的数据分支；Byte/Word/DWord/Real
 *   对应 0x04 字节串分支；String 对应 0x09 八位组分支。
 *
 * 单元素与多元素契约（供 W4/W5 对照）：
 * - count==1：value/out 为标量 XVariant（bool/uint8/uint16/uint32/float/
 *   String，数值型经 XVariant_to* 数值转换族互转）；
 * - count>1（非 String）：value 为 XByteArray 承载的主机序元素序列，
 *   encode 逐元素转大端追加、decode 逐元素转回主机序写入；长度不足判失败。
 *
 * @note 与 W1 的 XS7Pdu.c 无依赖关系，仅依赖 XS7Types.h 与本文件头契约。
 */

/******************************************************************************************
 * 内部常量
 ******************************************************************************************/

/** @brief S7 STRING 容量上限：[maxLen] 字节为 uint8，且 STEP7 限定 1..254。 */
#define XS7VALUE_STRING_MAX_CAPACITY    254

/******************************************************************************************
 * 内部辅助
 ******************************************************************************************/

/**
 * @brief float ⇄ uint32 位型中转：取 float 的 IEEE754 位型
 * @param f 输入浮点
 * @return 位型（uint32）
 * @note 用 XMemcpy 位拷贝实现，禁 *(float*)&u32 类 strict-aliasing 强转
 */
static uint32_t s7_value_real_to_bits(float f)
{
    uint32_t bits = 0;
    XMemcpy(&bits, &f, sizeof(bits));
    return bits;
}

/**
 * @brief float ⇄ uint32 位型中转：由位型还原 float
 * @param bits 输入位型
 * @return 还原的浮点
 * @note 用 XMemcpy 位拷贝实现，禁 *(float*)&u32 类 strict-aliasing 强转
 */
static float s7_value_bits_to_real(uint32_t bits)
{
    float f = 0.0f;
    XMemcpy(&f, &bits, sizeof(f));
    return f;
}

/**
 * @brief 追加一个按大端编码的标量到字节数组尾部
 * @param out 目标字节数组（非NULL）
 * @param val 主机序标量地址（非NULL）
 * @param size 标量字节数（>=1）
 * @return 成功返回 true；扩容或写入失败返回 false
 */
static bool s7_value_append_be(XByteArray* out, const void* val, size_t size)
{
    const size_t oldSize = XByteArray_size_base((const XContainer*)out);
    if (!XByteArray_resize_base((XVector*)out, oldSize + size)) {
        return false;
    }
    return XMemory_write_data(XByteArray_data(out) + oldSize, XBYTE_ORDER_BIG_ENDIAN,
                              (const uint8_t*)val, size);
}

/**
 * @brief 取源变体承载的主机序字节块（count>1 多元素契约）
 * @param value 源变体（须为 XVariantType_ByteArray 且长度足够）
 * @param needBytes 需要的字节数
 * @param outRaw 输出数据指针（借用，变体存活期内有效）
 * @return 满足条件返回 true
 */
static bool s7_value_host_bytes_from_variant(const XVariant* value, size_t needBytes,
                                             const uint8_t** outRaw)
{
    XByteArray* src = XByteArray_fromVariant_ref(value);   /* 借用，不释放 */
    if (!src || XByteArray_size_base((const XContainer*)src) < needBytes) {
        return false;
    }
    *outRaw = XByteArray_data(src);
    return *outRaw != NULL;   /* 调用方 needBytes 恒 >= 1，数据指针必非空 */
}

/**
 * @brief 把解码结果字节块以 move 语义写入输出变体并回收外壳
 * @param out 输出变体（非NULL）
 * @param arr 结果字节数组（本函数接管其内容后释放外壳）
 * @return 恒返回 true（arr 为 NULL 时由调用方先行处理）
 */
static void s7_value_set_byte_array_result(XVariant* out, XByteArray* arr)
{
    if (!arr) {
        return;
    }
    XByteArray_setVariant_move(out, arr);   /* move 进变体自持成员 */
    XClassDelete((XClass*)arr);   /* 仅释放已搬空的外壳 */
}

/**
 * @brief 预建结果字节数组（创建+扩容，失败自清理）
 * @param size 目标字节数
 * @param outArr 输出数组指针
 * @param outRaw 输出可写数据指针
 * @return 成功返回 true；失败返回 false（outArr 置 NULL）
 */
static bool s7_value_make_result_array(size_t size, XByteArray** outArr, uint8_t** outRaw)
{
    *outArr = NULL;
    *outRaw = NULL;
    XByteArray* arr = XByteArray_create();
    if (!arr || !XByteArray_resize_base((XVector*)arr, size)) {
        if (arr) {
            XClassDelete((XClass*)arr);
        }
        return false;
    }
    *outArr = arr;
    *outRaw = XByteArray_data(arr);
    return *outRaw != NULL;
}

/******************************************************************************************
 * 公开 API：编码（XVariant → 大端字节，追加到 out 尾部）
 ******************************************************************************************/

bool XS7Value_encode(XS7ValueType type, int count, const XVariant* value, XByteArray* out)
{
    if (count <= 0 || !value || !out) {
        return false;
    }

    switch (type) {
    case XS7Value_Bool: {
        if (count == 1) {
            /* 位传输数据分支（transport 0x03）：单字节，位值取最低位 */
            const uint8_t byte = XVariant_toBool(value) ? 0x01u : 0x00u;
            return XByteArray_push_back_1(out, byte);
        }
        /* 多元素：源为主机序字节块（每元素取非 0 判位），LSB 在前打包为
         * ceil(N/8) 字节（bit k → [k>>3] 字节的 k&7 位），与 CPU 位读回
         * 打包口径一致（见 XS7Pdu 0x03/0x04 数据分支与 FakePlc 同款实现） */
        const uint8_t* raw = NULL;
        if (!s7_value_host_bytes_from_variant(value, (size_t)count, &raw)) {
            return false;
        }
        const size_t packedLen = ((size_t)count + 7u) / 8u;
        const size_t oldSize = XByteArray_size_base((const XContainer*)out);
        if (!XByteArray_resize_base((XVector*)out, oldSize + packedLen)) {
            return false;
        }
        uint8_t* buf = XByteArray_data(out) + oldSize;
        XMemset(buf, 0, packedLen);
        for (int i = 0; i < count; ++i) {
            if (raw[i]) {
                buf[(size_t)i >> 3] |= (uint8_t)(1u << ((size_t)i & 7u));
            }
        }
        return true;
    }

    case XS7Value_Byte: {
        if (count == 1) {
            return XByteArray_push_back_1(out, XVariant_toUint8(value));
        }
        /* 多元素：源字节块原样追加（单字节无字节序问题） */
        const uint8_t* raw = NULL;
        if (!s7_value_host_bytes_from_variant(value, (size_t)count, &raw)) {
            return false;
        }
        const size_t oldSize = XByteArray_size_base((const XContainer*)out);
        if (!XByteArray_resize_base((XVector*)out, oldSize + (size_t)count)) {
            return false;
        }
        XMemcpy(XByteArray_data(out) + oldSize, raw, (size_t)count);
        return true;
    }

    case XS7Value_Word: {
        if (count == 1) {
            const uint16_t val = XVariant_toUint16(value);
            return s7_value_append_be(out, &val, sizeof(val));
        }
        /* 多元素：源为主机序 uint16 序列，逐元素转大端 */
        const uint8_t* raw = NULL;
        if (!s7_value_host_bytes_from_variant(value, (size_t)count * sizeof(uint16_t), &raw)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            uint16_t val = 0;
            XMemory_read_data(raw + (size_t)i * sizeof(uint16_t), XBYTE_ORDER_NATIVE,
                              (uint8_t*)&val, sizeof(val));
            if (!s7_value_append_be(out, &val, sizeof(val))) {
                return false;
            }
        }
        return true;
    }

    case XS7Value_DWord: {
        if (count == 1) {
            const uint32_t val = XVariant_toUint32(value);
            return s7_value_append_be(out, &val, sizeof(val));
        }
        /* 多元素：源为主机序 uint32 序列，逐元素转大端 */
        const uint8_t* raw = NULL;
        if (!s7_value_host_bytes_from_variant(value, (size_t)count * sizeof(uint32_t), &raw)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            uint32_t val = 0;
            XMemory_read_data(raw + (size_t)i * sizeof(uint32_t), XBYTE_ORDER_NATIVE,
                              (uint8_t*)&val, sizeof(val));
            if (!s7_value_append_be(out, &val, sizeof(val))) {
                return false;
            }
        }
        return true;
    }

    case XS7Value_Real: {
        if (count == 1) {
            /* 位型中转：float → uint32 位型 → 大端字节 */
            const uint32_t bits = s7_value_real_to_bits(XVariant_toFloat(value));
            return s7_value_append_be(out, &bits, sizeof(bits));
        }
        /* 多元素：源为主机序 float 位型序列，逐元素取位型转大端 */
        const uint8_t* raw = NULL;
        if (!s7_value_host_bytes_from_variant(value, (size_t)count * sizeof(float), &raw)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            uint32_t bits = 0;
            XMemory_read_data(raw + (size_t)i * sizeof(float), XBYTE_ORDER_NATIVE,
                              (uint8_t*)&bits, sizeof(bits));
            if (!s7_value_append_be(out, &bits, sizeof(bits))) {
                return false;
            }
        }
        return true;
    }

    case XS7Value_String: {
        if (count > XS7VALUE_STRING_MAX_CAPACITY) {
            return false;   /* [maxLen] 为 uint8 且 STEP7 限定 1..254 */
        }
        const XString* str = XString_fromVariant_const(value);
        if (!str) {
            return false;   /* 源不是 String 型变体 */
        }
        const size_t capacity = (size_t)count;
        const size_t charCount = XString_length_base((const XContainer*)str);
        if (charCount > capacity) {
            return false;   /* 容量不足（头契约：返回 false，不静默截断） */
        }
        /* 非 Latin-1 字符预检（S7 STRING 为单字节域，无法无损承载） */
        for (size_t i = 0; i < charCount; ++i) {
            if (XString_at(str, i) > 0xFFu) {
                return false;
            }
        }
        /* 布局 [maxLen][curLen][data...]，总长 capacity+2 偶数补齐（补 0） */
        const size_t totalLen = capacity + 2u;
        const size_t paddedLen = totalLen + (totalLen & 1u);
        const size_t oldSize = XByteArray_size_base((const XContainer*)out);
        if (!XByteArray_resize_base((XVector*)out, oldSize + paddedLen)) {
            return false;
        }
        uint8_t* buf = XByteArray_data(out) + oldSize;
        XMemset(buf, 0, paddedLen);
        buf[0] = (uint8_t)capacity;
        buf[1] = (uint8_t)charCount;
        for (size_t i = 0; i < charCount; ++i) {
            buf[2 + i] = (uint8_t)XChar_toLatin1(XString_at(str, i));
        }
        return true;
    }

    default:
        return false;   /* 未知类型 */
    }
}

/******************************************************************************************
 * 公开 API：解码（大端字节 → XVariant，写入 out）
 ******************************************************************************************/

bool XS7Value_decode(XS7ValueType type, int count, const uint8_t* data, size_t dataLen, XVariant* out)
{
    if (count <= 0 || !data || !out) {
        return false;
    }

    switch (type) {
    case XS7Value_Bool: {
        /* 位打包载荷：ceil(N/8) 字节（与 CPU 位读回、XS7Pdu 数据分支同口径） */
        const size_t packedLen = ((size_t)count + 7u) / 8u;
        if (dataLen < packedLen) {
            return false;
        }
        if (count == 1) {
            XVariant_setValue_bool(out, (data[0] & 0x01u) != 0u);
            return true;
        }
        /* LSB 在前解包：bit k ← [k>>3] 字节的 k&7 位，逐元素展开为 0/1 字节 */
        XByteArray* arr = NULL;
        uint8_t* raw = NULL;
        if (!s7_value_make_result_array((size_t)count, &arr, &raw)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            raw[i] = (data[(size_t)i >> 3] >> ((size_t)i & 7u)) & 0x01u;
        }
        s7_value_set_byte_array_result(out, arr);
        return true;
    }

    case XS7Value_Byte: {
        if (dataLen < (size_t)count) {
            return false;
        }
        if (count == 1) {
            XVariant_setValue_uint8(out, data[0]);
            return true;
        }
        XByteArray* arr = NULL;
        uint8_t* raw = NULL;
        if (!s7_value_make_result_array((size_t)count, &arr, &raw)) {
            return false;
        }
        XMemcpy(raw, data, (size_t)count);
        s7_value_set_byte_array_result(out, arr);
        return true;
    }

    case XS7Value_Word: {
        const size_t need = (size_t)count * sizeof(uint16_t);
        if (dataLen < need) {
            return false;
        }
        if (count == 1) {
            uint16_t val = 0;
            XMemory_read_data(data, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&val, sizeof(val));
            XVariant_setValue_uint16(out, val);
            return true;
        }
        /* 多元素：逐元素大端 → 主机序写入结果块 */
        XByteArray* arr = NULL;
        uint8_t* raw = NULL;
        if (!s7_value_make_result_array(need, &arr, &raw)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            uint16_t val = 0;
            XMemory_read_data(data + (size_t)i * sizeof(uint16_t), XBYTE_ORDER_BIG_ENDIAN,
                              (uint8_t*)&val, sizeof(val));
            XMemory_write_data(raw + (size_t)i * sizeof(uint16_t), XBYTE_ORDER_NATIVE,
                               (const uint8_t*)&val, sizeof(val));
        }
        s7_value_set_byte_array_result(out, arr);
        return true;
    }

    case XS7Value_DWord: {
        const size_t need = (size_t)count * sizeof(uint32_t);
        if (dataLen < need) {
            return false;
        }
        if (count == 1) {
            uint32_t val = 0;
            XMemory_read_data(data, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&val, sizeof(val));
            XVariant_setValue_uint32(out, val);
            return true;
        }
        XByteArray* arr = NULL;
        uint8_t* raw = NULL;
        if (!s7_value_make_result_array(need, &arr, &raw)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            uint32_t val = 0;
            XMemory_read_data(data + (size_t)i * sizeof(uint32_t), XBYTE_ORDER_BIG_ENDIAN,
                              (uint8_t*)&val, sizeof(val));
            XMemory_write_data(raw + (size_t)i * sizeof(uint32_t), XBYTE_ORDER_NATIVE,
                               (const uint8_t*)&val, sizeof(val));
        }
        s7_value_set_byte_array_result(out, arr);
        return true;
    }

    case XS7Value_Real: {
        const size_t need = (size_t)count * sizeof(float);
        if (dataLen < need) {
            return false;
        }
        if (count == 1) {
            /* 位型中转：大端字节 → uint32 位型 → float */
            uint32_t bits = 0;
            XMemory_read_data(data, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&bits, sizeof(bits));
            XVariant_setValue_float(out, s7_value_bits_to_real(bits));
            return true;
        }
        XByteArray* arr = NULL;
        uint8_t* raw = NULL;
        if (!s7_value_make_result_array(need, &arr, &raw)) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            uint32_t bits = 0;
            XMemory_read_data(data + (size_t)i * sizeof(float), XBYTE_ORDER_BIG_ENDIAN,
                              (uint8_t*)&bits, sizeof(bits));
            XMemory_write_data(raw + (size_t)i * sizeof(float), XBYTE_ORDER_NATIVE,
                               (const uint8_t*)&bits, sizeof(bits));
        }
        s7_value_set_byte_array_result(out, arr);
        return true;
    }

    case XS7Value_String: {
        if (dataLen < 2) {
            return false;   /* 至少 [maxLen][curLen] */
        }
        const uint8_t maxLen = data[0];
        const uint8_t curLen = data[1];
        if ((size_t)curLen + 2u > dataLen) {
            return false;   /* 载荷长度不足 */
        }
        if (curLen > maxLen) {
            return false;   /* S7 布局校验：curLen 不得超过 maxLen */
        }
        if ((size_t)curLen > (size_t)count) {
            return false;   /* 超出申请容量 n（count 即 STRING 容量） */
        }
        /* S7 STRING 字节域按 Latin-1 解释（ASCII 域无损） */
        XString* str = XString_create_with_length_latin1(data + 2, (size_t)curLen);
        if (!str) {
            return false;
        }
        XString_setVariant_move(out, str);
        XClassDelete((XClass*)str);
        return true;
    }

    default:
        return false;   /* 未知类型 */
    }
}

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
