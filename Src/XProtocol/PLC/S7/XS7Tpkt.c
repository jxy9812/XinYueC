#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON
#include "XS7Tpkt.h"
#include <string.h>

/**
 * @file XS7Tpkt.c
 * @brief TPKT 层（RFC1006）组帧与半包判定实现
 * @details 纯函数无状态：wrap 组帧写入输出数组尾部；
 *          peekLength 只读前 4 字节判定整帧长度，供 XS7Session_feed 分帧。
 */

// =============== 内部辅助 ===============

/*
 * XByteArray 首成员即 XVector（XByteArray.h 结构定义），库内自带内联包装
 * （XByteArray_contains_byte 等）同样做 (XVector*) 显式转换；
 * 此处沿用该仓库习惯用法调用向量接口，规避宏转发接口的 C4133 形参类型告警。
 */

/** @brief 追加 n 字节到字节数组尾部 */
static inline bool xs7TpktAppend(XByteArray* array, const void* data, size_t n)
{
    return XVector_push_back_2((XVector*)array, data, n);
}

/** @brief 扩容字节数组到 n 字节 */
static inline bool xs7TpktResize(XByteArray* array, size_t n)
{
    return XVector_resize_base((XVector*)array, n);
}

/** @brief 取当前字节数 */
static inline size_t xs7TpktSize(const XByteArray* array)
{
    return XContainer_size_base((const XContainer*)array);
}

size_t XS7Tpkt_wrap(XByteArray* out, const uint8_t* payload, size_t payloadLen)
{
    if (!out || !payload) return 0;
    if (payloadLen == 0) return 0;

    size_t total = 4 + payloadLen;
    if (total > XS7_MAX_FRAME) return 0;

    /* 一次扩容到最终大小后直写缓冲（对齐 XModbusAdu.c 单次分配组帧惯用法） */
    {
        size_t old = xs7TpktSize(out);
        if (!xs7TpktResize(out, old + total)) return 0;

        uint8_t* p = XByteArray_data(out);
        p[old] = 0x03;                       /* ver=3 */
        p[old + 1] = 0x00;                   /* reserved */
        p[old + 2] = (uint8_t)((total >> 8) & 0xFF);  /* length BE high */
        p[old + 3] = (uint8_t)(total & 0xFF);         /* length BE low  */
        memcpy(p + old + 4, payload, payloadLen);
    }
    return total;
}

bool XS7Tpkt_peekLength(const uint8_t* data, size_t len, size_t* total)
{
    if (!data || !total) return false;
    if (len < 4) return false;           /* 半包：头部未到齐 */

    if (data[0] != 0x03) return false;   /* ver != 3（RFC1006） */

    size_t frameLen = ((size_t)data[2] << 8) | (size_t)data[3];
    if (frameLen < 4) return false;      /* 长度必须包含头自身 */
    if (frameLen > XS7_MAX_FRAME) return false;   /* 防失控上限 */

    *total = frameLen;
    return true;
}

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
