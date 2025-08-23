#include"XCrc.h"
#if XCrc_ON
#include"XAlgorithm.h"
#include <stddef.h>
#include <stdint.h>
#if XCrc16_ON
/* ----------------------- 平台相关头文件 --------------------------------*/
// CRC16 表
static const uint16_t crc16_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

void XCrc_set16Data(uint8_t* dst, uint16_t crc16, XCRCByteOrder order)
{
    if (order == XCRC_BYTE_ORDER_NATIVE)
    {
        *((uint16_t*)dst) = crc16;
    }
    else
    {
#if IS_BIG_ENDIAN
        if (order== XCRC_BYTE_ORDER_BIG_ENDIAN)
        {
            // 大端模式：如果当前是大端，无需转换，直接返回原数据
            *((uint16_t*)dst) = crc16;
        }
        else 
        {
            // 小端模式：如果当前不是小端，需要转换
            *((uint16_t*)dst) = (crc16 << 8) | (crc16 >> 8);
        }
#else
        if (order == XCRC_BYTE_ORDER_BIG_ENDIAN) 
        {
            // 大端模式：如果当前不是大端，需要转换
            // 交换字节顺序
            *((uint16_t*)dst) = (crc16 << 8) | (crc16 >> 8);
        }
        else
        {
            // 小端模式：如果当前是小端，无需转换，直接返回原数据
            *((uint16_t*)dst) = crc16;
        }
    }
#endif
}


// 使用查表法计算 CRC16 校验码
uint16_t XCrc_get16(uint8_t* data, uint16_t length) 
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}
#if XVector_ON
#include"XVector.h"
bool XVector_append_crc16(XVector* data, XCRCByteOrder order)
{
    if (data == NULL || XVector_isEmpty_base(data))
        return false;
    if (XContainerSize(data) + 2 > XContainerCapacity(data))
        XVector_resize_base(data, XContainerSize(data) + 2);
    else
        XContainerSize(data) += 2;
    size_t size = XContainerSize(data);//加上校验大小
    XCrc_set16Data(((uint8_t*)XContainerDataPtr(data))+size-2, XCrc_get16(XContainerDataPtr(data),size-2), order);
    return true;
}
#endif // XVector_ON
#endif // XCrc16_ON
/* ----------------------- CRC32 实现（纯C优化版本）-----------------------*/
#define GF2_DIM 32
#if XCrc32_ON
#define BYFOUR

/* 扩展CRC表支持批量处理（8个子表） */
static uint32_t crc_table[8][256];
static int table_initialized = 0;
static XCRC32Polynomial current_polynomial = XCRC32_IEEE_802_3;

/* 常用CRC32多项式/初始值/输出异或值（保持不变） */
static const uint32_t CRC32_POLYNOMIALS[] = {
    0x04C11DB7,  /* XCRC32_IEEE_802_3 */
    0x1EDC6F41,  /* XCRC32_CASTAGNOLI */
    0x741B8CD7,  /* XCRC32_KOOPMAN */
    0x04C11DB7,  /* XCRC32_ISO_HDLC */
    0x04C11DB7,  /* XCRC32_MPEG2 */
    0x04C11DB7   /* XCRC32_POSIX */
};

static const uint32_t CRC32_INIT_VALUES[] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};

static const uint32_t CRC32_XOROUT_VALUES[] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF
};

/* 字节序交换辅助宏（纯C实现） */
#define ZSWAP32(c) ((((c) >> 24) & 0xff) | (((c) >> 8) & 0xff00) | \
                    (((c) << 8) & 0xff0000) | (((c) << 24) & 0xff000000))

/* 批量处理宏定义 */
#ifdef BYFOUR
#define DOLIT4 \
    c ^= *buf4++; \
    c = crc_table[0][c & 0xff] ^ crc_table[1][(c >> 8) & 0xff] ^ \
        crc_table[2][(c >> 16) & 0xff] ^ crc_table[3][c >> 24];

#define DOLIT32 \
    DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4;

#define DOBIG4 \
    c ^= ZSWAP32(*buf4++); \
    c = crc_table[4][c & 0xff] ^ crc_table[5][(c >> 8) & 0xff] ^ \
        crc_table[6][(c >> 16) & 0xff] ^ crc_table[7][c >> 24];

#define DOBIG32 \
    DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4;
#endif

/* 小端模式批量处理（纯C函数） */
static uint32_t crc32_little(uint32_t crc, const uint8_t* buf, size_t len) {
    register uint32_t c = crc;
    register const uint32_t* buf4;

    while (len && ((ptrdiff_t)buf & 3)) {
        c = crc_table[0][(c ^ *buf++) & 0xff] ^ (c >> 8);
        len--;
    }

    buf4 = (const uint32_t*)buf;
    while (len >= 32) {
        DOLIT32;
        len -= 32;
    }
    while (len >= 4) {
        DOLIT4;
        len -= 4;
    }
    buf = (const uint8_t*)buf4;

    if (len) do {
        c = crc_table[0][(c ^ *buf++) & 0xff] ^ (c >> 8);
    } while (--len);

    return c;
}

/* 大端模式批量处理（纯C函数） */
static uint32_t crc32_big(uint32_t crc, const uint8_t* buf, size_t len) {
    register uint32_t c = ZSWAP32(crc);
    register const uint32_t* buf4;

    while (len && ((ptrdiff_t)buf & 3)) {
        c = crc_table[4][(c >> 24) ^ *buf++] ^ (c << 8);
        len--;
    }

    buf4 = (const uint32_t*)buf;
    while (len >= 32) {
        DOBIG32;
        len -= 32;
    }
    while (len >= 4) {
        DOBIG4;
        len -= 4;
    }
    buf = (const uint8_t*)buf4;

    if (len) do {
        c = crc_table[4][(c >> 24) ^ *buf++] ^ (c << 8);
    } while (--len);

    return ZSWAP32(c);
}

/* GF(2)矩阵乘法（纯C实现） */
static uint32_t gf2_matrix_times(uint32_t* mat, uint32_t vec) {
    uint32_t sum = 0;
    while (vec) {
        if (vec & 1) sum ^= *mat;
        vec >>= 1;
        mat++;
    }
    return sum;
}

/* GF(2)矩阵平方（纯C实现） */
static void gf2_matrix_square(uint32_t* square, uint32_t* mat) {
    int n;
    for (n = 0; n < 32; n++) {
        square[n] = gf2_matrix_times(mat, mat[n]);
    }
}

uint32_t** XCrc32_get_crc_table()
{
    return crc_table;
}

/**
 * 初始化CRC32查找表（支持批量处理）
 */
void XCrc32_init_table(XCRC32Polynomial polynomial) {
    uint32_t poly = CRC32_POLYNOMIALS[polynomial];
    int n, k;

    if (table_initialized && polynomial == current_polynomial) {
        return;
    }

    current_polynomial = polynomial;

    /* 生成基础表 */
    for (n = 0; n < 256; n++) {
        uint32_t c = n;
        for (k = 0; k < 8; k++) {
            c = c & 1 ? (c >> 1) ^ poly : c >> 1;
        }
        crc_table[0][n] = c;
    }

#ifdef BYFOUR
    /* 生成批量处理用扩展表 */
    for (n = 0; n < 256; n++) {
        uint32_t c = crc_table[0][n];
        crc_table[4][n] = ZSWAP32(c);

        for (k = 1; k < 4; k++) {
            c = crc_table[0][c & 0xff] ^ (c >> 8);
            crc_table[k][n] = c;
            crc_table[k + 4][n] = ZSWAP32(c);
        }
    }
#endif

    table_initialized = 1;
}

/**
 * 计算数据的CRC32校验值（整合批量处理优化）
 */
uint32_t XCrc32_calculate(const uint8_t* data, size_t length, XCRCByteOrder order) {
    uint32_t crc = CRC32_INIT_VALUES[current_polynomial];

    if (!table_initialized) {
        XCrc32_init_table(current_polynomial);
    }

#ifdef BYFOUR
    if (sizeof(void*) == sizeof(ptrdiff_t)) {
        uint32_t endian = 1;
        crc = ~crc;
        if (*(uint8_t*)&endian)  // 小端模式
            crc = crc32_little(crc, data, length);
        else  // 大端模式
            crc = crc32_big(crc, data, length);
        crc = ~crc;
    }
    else {
#endif
        /* 标准逐字节处理 */
        for (size_t i = 0; i < length; i++) {
            crc = (crc >> 8) ^ crc_table[0][(crc & 0xFF) ^ data[i]];
        }
#ifdef BYFOUR
    }
#endif

    crc ^= CRC32_XOROUT_VALUES[current_polynomial];
    return XCrc32_finalize(crc, order);
}

/**
 * 更新CRC32校验值（用于流式计算）
 */
uint32_t XCrc32_update(uint32_t crc, const uint8_t* data, size_t length) {
    if (!table_initialized) {
        XCrc32_init_table(current_polynomial);
    }

#ifdef BYFOUR
    if (sizeof(void*) == sizeof(ptrdiff_t)) {
        uint32_t endian = 1;
        if (*(uint8_t*)&endian)
            return crc32_little(crc, data, length);
        else
            return crc32_big(crc, data, length);
    }
    else {
#endif
        for (size_t i = 0; i < length; i++) {
            crc = (crc >> 8) ^ crc_table[0][(crc & 0xFF) ^ data[i]];
        }
        return crc;
#ifdef BYFOUR
    }
#endif
}

/**
 * 获取CRC32校验值（根据字节序调整）
 */
uint32_t XCrc32_finalize(uint32_t crc, XCRCByteOrder order) {
    uint32_t result = crc;

    if (order == XCRC_BYTE_ORDER_BIG_ENDIAN) {
        result = ((result << 24) & 0xFF000000) |
            ((result << 8) & 0x00FF0000) |
            ((result >> 8) & 0x0000FF00) |
            ((result >> 24) & 0x000000FF);
    }

    return result;
}

/**
 * CRC32拼接功能（纯C实现）
 */
uint32_t XCrc32_combine(uint32_t crc1, uint32_t crc2, size_t len2) {
    uint32_t even[GF2_DIM], odd[GF2_DIM];
    uint32_t row, poly = CRC32_POLYNOMIALS[current_polynomial];
    int n;

    if (len2 <= 0) return crc1;

    /* 初始化矩阵 */
    odd[0] = poly;
    row = 1;
    for (n = 1; n < GF2_DIM; n++) {
        odd[n] = row;
        row <<= 1;
    }

    /* 矩阵平方运算 */
    gf2_matrix_square(even, odd);
    gf2_matrix_square(odd, even);

    /* 应用长度算子 */
    do {
        gf2_matrix_square(even, odd);
        if (len2 & 1) {
            crc1 = gf2_matrix_times(even, crc1);
        }
        len2 >>= 1;

        if (len2 == 0) break;

        gf2_matrix_square(odd, even);
        if (len2 & 1) {
            crc1 = gf2_matrix_times(odd, crc1);
        }
        len2 >>= 1;
    } while (len2 != 0);

    return crc1 ^ crc2;
}

///**
// * 设置当前使用的CRC32多项式
// */
//void crc32_set_polynomial(XCRC32Polynomial polynomial) {
//    XCrc32_init_table(polynomial);
//}

#endif // XCrc32_ON
#endif // XCrc_ON