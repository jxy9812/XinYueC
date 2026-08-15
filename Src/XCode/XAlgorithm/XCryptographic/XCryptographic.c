/**
 * @file XCryptographic.c
 * @brief 加密算法与哈希实现（哈希部分对齐 Qt 6.8 QCryptographicHash）
 *
 * 支持的算法：MD4, MD5, SHA-1, SHA-224/256/384/512, SHA3, Keccak, Blake2
 */

#include "XCryptographic.h"
#include "XByteArray.h"
#include "XMemory.h"
#include "XIODevice.h"
#include "XRandomGenerator.h"
#include "XCryptographicHash.h"
#include <string.h>
#include <stdlib.h>

/*
 * Hash/KDF temporary storage follows the same pool as XCryptographicHash.
 * Keep this adapter local to the implementation so the public API remains
 * independent from a particular allocator.
 */
static const XMemory* xcryptographic_hash_memory_method(void)
{
    return XMemory_method(XCRYPTOGRAPHIC_HASH_MEMORY_POOL_TYPE);
}

static void* xcryptographic_hash_malloc(size_t size)
{
    const XMemory* memory = xcryptographic_hash_memory_method();
    return memory && memory->malloc ? memory->malloc(size) : NULL;
}

static void xcryptographic_hash_free(void* data)
{
    const XMemory* memory = xcryptographic_hash_memory_method();
    if (data && memory && memory->free) memory->free(data);
}


/* 动态大整数（小端 32 位 limb）。 */
typedef struct XCryptographic_BigInt {
    uint32_t *d;
    size_t n;   /* 有效 limb 数 */
    size_t cap; /* 已分配 limb 数 */
} XCryptographic_BigInt;

/* 大整数扩展（RSA 块）前向声明，供 FFDH 的 Montgomery 指数和 RSA 复用。 */
static bool xcbig_mod(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                      const XCryptographic_BigInt *m);

static void xcryptographic_chacha20_block(const uint8_t key[32], uint32_t counter,
                                          const uint8_t nonce[12], uint8_t output[64]);

// ==================== SHAKE128/256 XOF 实现 ====================

static bool xcryptographic_xof_enabled(XCryptographic_XofAlgorithm algorithm)
{
#if XCRYPTOGRAPHIC_SHAKE_ON
    return algorithm == XCryptographic_XofAlgorithm_Shake128 ||
           algorithm == XCryptographic_XofAlgorithm_Shake256;
#else
    (void)algorithm;
    return false;
#endif
}

static void xcryptographic_xof_init_state(XCryptographic_XofOperation* operation,
                                               XCryptographic_XofAlgorithm algorithm)
{
    memset(operation, 0, sizeof(*operation));
    operation->algorithm = algorithm;
    operation->delimitedSuffix = 0x1F; /* SHAKE 使用 0x1F 分隔符 */
    /* 速率 = (1600 - 2*安全强度) / 8 */
    operation->rate = (algorithm == XCryptographic_XofAlgorithm_Shake128) ? 168u : 136u;
    operation->active = true;
}

static void xcryptographic_xof_absorb(XCryptographic_XofOperation* operation,
                                           const uint8_t* data, size_t len)
{
    size_t offset = 0;

    while (len > 0) {
        size_t copyLen = operation->rate - operation->blockLen;
        if (copyLen > len) copyLen = len;
        for (size_t i = 0; i < copyLen; ++i)
            operation->block[operation->blockLen + i] ^= data[offset + i];
        operation->blockLen += copyLen;
        offset += copyLen;
        len -= copyLen;

        if (operation->blockLen == operation->rate) {
            for (size_t i = 0; i < operation->rate / 8; ++i)
                operation->state[i] ^= ((uint64_t*)operation->block)[i];
            XCryptographicHash_keccakPermute(operation->state);
            operation->blockLen = 0;
            memset(operation->block, 0, operation->rate);
        }
    }
}

static void xcryptographic_xof_finalize(XCryptographic_XofOperation* operation)
{
    if (operation->finalized) return;
    operation->block[operation->blockLen] ^= operation->delimitedSuffix;
    operation->block[operation->rate - 1] ^= 0x80;
    for (size_t i = 0; i < operation->rate / 8; ++i)
        operation->state[i] ^= ((uint64_t*)operation->block)[i];
    XCryptographicHash_keccakPermute(operation->state);
    operation->finalized = true;
    operation->outputOffset = 0;
}

static void xcryptographic_xof_squeeze(XCryptographic_XofOperation* operation,
                                           uint8_t* output, size_t outputSize)
{
    size_t produced = 0;
    while (produced < outputSize) {
        if (operation->outputOffset == operation->rate) {
            XCryptographicHash_keccakPermute(operation->state);
            operation->outputOffset = 0;
        }
        {
            size_t copySize = operation->rate - operation->outputOffset;
            if (copySize > outputSize - produced) copySize = outputSize - produced;
            memcpy(output + produced, (uint8_t*)operation->state + operation->outputOffset, copySize);
            operation->outputOffset += copySize;
            produced += copySize;
        }
    }
}

bool XCryptographic_xofSetup(XCryptographic_XofOperation* operation,
                                  XCryptographic_XofAlgorithm algorithm)
{
    if (!operation || !xcryptographic_xof_enabled(algorithm)) return false;
    xcryptographic_xof_init_state(operation, algorithm);
    return true;
}

bool XCryptographic_xofUpdateInto(XCryptographic_XofOperation* operation,
                                  XByteArrayView data)
{
    if (!operation || !operation->active || operation->finalized ||
        data.m_size < 0 || (!data.m_data && data.m_size != 0)) return false;
    if (data.m_size != 0)
        xcryptographic_xof_absorb(operation, data.m_data, (size_t)data.m_size);
    return true;
}

XByteArrayView XCryptographic_xofOutputInto(
    XCryptographic_XofOperation* operation, char* buffer, size_t bufferSize,
    size_t outputSize)
{
    XByteArrayView empty = { NULL, 0 };
    if (!operation || !operation->active || !buffer || bufferSize < outputSize)
        return empty;
    xcryptographic_xof_finalize(operation);
    xcryptographic_xof_squeeze(operation, (uint8_t*)buffer, outputSize);
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)outputSize };
        return result;
    }
}

void XCryptographic_xofAbort(XCryptographic_XofOperation* operation)
{
    if (operation) memset(operation, 0, sizeof(*operation));
}

/* ==================== 对称加密与密钥算法 ==================== */
/* ==================== 对称加密与密钥算法 ==================== */

/* ==================== provider-independent primitives ==================== */

static const uint8_t xcryptographic_aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static uint8_t xcryptographic_aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b));
}

static void xcryptographic_aes_add_round(uint8_t state[16],
                                              const uint8_t* roundKey)
{
    size_t i;
    for (i = 0; i < 16; ++i) state[i] ^= roundKey[i];
}

static void xcryptographic_aes_sub_bytes(uint8_t state[16])
{
    size_t i;
    for (i = 0; i < 16; ++i) state[i] = xcryptographic_aes_sbox[state[i]];
}

static void xcryptographic_aes_shift_rows(uint8_t state[16])
{
    uint8_t temp[16];
    size_t row, col;
    memcpy(temp, state, sizeof(temp));
    for (row = 0; row < 4; ++row)
        for (col = 0; col < 4; ++col)
            state[row + 4 * col] = temp[row + 4 * ((col + row) & 3u)];
}

static void xcryptographic_aes_mix_columns(uint8_t state[16])
{
    size_t col;
    for (col = 0; col < 4; ++col) {
        uint8_t* a = state + 4 * col;
        uint8_t t = a[0] ^ a[1] ^ a[2] ^ a[3];
        uint8_t u = a[0];
        a[0] ^= t ^ xcryptographic_aes_xtime((uint8_t)(a[0] ^ a[1]));
        a[1] ^= t ^ xcryptographic_aes_xtime((uint8_t)(a[1] ^ a[2]));
        a[2] ^= t ^ xcryptographic_aes_xtime((uint8_t)(a[2] ^ a[3]));
        a[3] ^= t ^ xcryptographic_aes_xtime((uint8_t)(a[3] ^ u));
    }
}

static void xcryptographic_aes_expand(const uint8_t* key, size_t keyLen,
                                           uint8_t roundKeys[240],
                                           uint8_t* rounds)
{
    static const uint8_t rcon[10] =
        { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36 };
    size_t words = keyLen / 4;
    size_t totalWords = 4 * (words + 7);
    size_t i;
    memcpy(roundKeys, key, keyLen);
    for (i = words; i < totalWords; ++i) {
        uint8_t temp[4];
        size_t j;
        memcpy(temp, roundKeys + 4 * (i - 1), 4);
        if (i % words == 0) {
            uint8_t t = temp[0];
            temp[0] = xcryptographic_aes_sbox[temp[1]] ^ rcon[i / words - 1];
            temp[1] = xcryptographic_aes_sbox[temp[2]];
            temp[2] = xcryptographic_aes_sbox[temp[3]];
            temp[3] = xcryptographic_aes_sbox[t];
        } else if (words > 6 && i % words == 4) {
            for (j = 0; j < 4; ++j) temp[j] = xcryptographic_aes_sbox[temp[j]];
        }
        for (j = 0; j < 4; ++j)
            roundKeys[4 * i + j] = roundKeys[4 * (i - words) + j] ^ temp[j];
    }
    *rounds = (uint8_t)(words + 6);
}

static void xcryptographic_aes_block(const uint8_t roundKeys[240],
                                          uint8_t rounds, const uint8_t input[16],
                                          uint8_t output[16])
{
    uint8_t state[16];
    uint8_t round;
    memcpy(state, input, sizeof(state));
    xcryptographic_aes_add_round(state, roundKeys);
    for (round = 1; round < rounds; ++round) {
        xcryptographic_aes_sub_bytes(state);
        xcryptographic_aes_shift_rows(state);
        xcryptographic_aes_mix_columns(state);
        xcryptographic_aes_add_round(state, roundKeys + 16 * round);
    }
    xcryptographic_aes_sub_bytes(state);
    xcryptographic_aes_shift_rows(state);
    xcryptographic_aes_add_round(state, roundKeys + 16 * rounds);
    memcpy(output, state, sizeof(state));
}

static uint8_t xcryptographic_aes_inv_sbox(uint8_t value)
{
    size_t index;
    for (index = 0; index < 256; ++index) {
        if (xcryptographic_aes_sbox[index] == value) return (uint8_t)index;
    }
    return 0;
}

static uint8_t xcryptographic_aes_mul(uint8_t a, uint8_t b)
{
    uint8_t result = 0;
    while (b != 0) {
        if (b & 1u) result ^= a;
        a = xcryptographic_aes_xtime(a);
        b >>= 1;
    }
    return result;
}

static void xcryptographic_aes_inv_sub_bytes(uint8_t state[16])
{
    size_t index;
    for (index = 0; index < 16; ++index) state[index] = xcryptographic_aes_inv_sbox(state[index]);
}

static void xcryptographic_aes_inv_shift_rows(uint8_t state[16])
{
    uint8_t temp[16];
    size_t row, col;
    memcpy(temp, state, sizeof(temp));
    for (row = 0; row < 4; ++row)
        for (col = 0; col < 4; ++col)
            state[row + 4 * col] = temp[row + 4 * ((col + 4u - row) & 3u)];
}

static void xcryptographic_aes_inv_mix_columns(uint8_t state[16])
{
    size_t col;
    for (col = 0; col < 4; ++col) {
        uint8_t* a = state + 4 * col;
        uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
        a[0] = xcryptographic_aes_mul(a0, 0x0e) ^ xcryptographic_aes_mul(a1, 0x0b) ^
               xcryptographic_aes_mul(a2, 0x0d) ^ xcryptographic_aes_mul(a3, 0x09);
        a[1] = xcryptographic_aes_mul(a0, 0x09) ^ xcryptographic_aes_mul(a1, 0x0e) ^
               xcryptographic_aes_mul(a2, 0x0b) ^ xcryptographic_aes_mul(a3, 0x0d);
        a[2] = xcryptographic_aes_mul(a0, 0x0d) ^ xcryptographic_aes_mul(a1, 0x09) ^
               xcryptographic_aes_mul(a2, 0x0e) ^ xcryptographic_aes_mul(a3, 0x0b);
        a[3] = xcryptographic_aes_mul(a0, 0x0b) ^ xcryptographic_aes_mul(a1, 0x0d) ^
               xcryptographic_aes_mul(a2, 0x09) ^ xcryptographic_aes_mul(a3, 0x0e);
    }
}

static void xcryptographic_aes_inv_block(const uint8_t roundKeys[240],
                                         uint8_t rounds, const uint8_t input[16],
                                         uint8_t output[16])
{
    uint8_t state[16];
    uint8_t round;
    memcpy(state, input, sizeof(state));
    xcryptographic_aes_add_round(state, roundKeys + 16 * rounds);
    for (round = rounds; round > 1; --round) {
        xcryptographic_aes_inv_shift_rows(state);
        xcryptographic_aes_inv_sub_bytes(state);
        xcryptographic_aes_add_round(state, roundKeys + 16 * (round - 1));
        xcryptographic_aes_inv_mix_columns(state);
    }
    xcryptographic_aes_inv_shift_rows(state);
    xcryptographic_aes_inv_sub_bytes(state);
    xcryptographic_aes_add_round(state, roundKeys);
    memcpy(output, state, sizeof(state));
}


/* =============== ARIA 分组密码（RFC 5794） =============== */

static uint32_t xcryptographic_load_u32_le_aria(const uint8_t* input)
{
    return ((uint32_t)input[0]) | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static void xcryptographic_store_u32_le_aria(uint8_t* output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint32_t xcryptographic_aria_p1(uint32_t x)
{
    return ((((x) >> 8) & 0x00FF00FFu) ^ (((x) & 0x00FF00FFu) << 8));
}

static uint32_t xcryptographic_aria_p2(uint32_t x)
{
    return ((x) >> 16) ^ ((x) << 16);
}

static uint32_t xcryptographic_aria_p3(uint32_t x)
{
    return (((x) & 0xFFu) << 24) | (((x) & 0xFF00u) << 8) |
           (((x) >> 8) & 0xFF00u) | ((x) >> 24);
}

static void xcryptographic_aria_a(uint32_t* a, uint32_t* b,
                                  uint32_t* c, uint32_t* d)
{
    uint32_t ta, tb, tc;
    ta  =  *b;
    *b  =  *a;
    *a  =  xcryptographic_aria_p2(ta);
    tb  =  xcryptographic_aria_p2(*d);
    *d  =  xcryptographic_aria_p1(*c);
    *c  =  xcryptographic_aria_p1(tb);
    ta  ^= *d;
    tc  =  xcryptographic_aria_p2(*b);
    ta  =  xcryptographic_aria_p1(ta) ^ tc ^ *c;
    tb  ^= xcryptographic_aria_p2(*d);
    tc  ^= xcryptographic_aria_p1(*a);
    *b  ^= ta ^ tb;
    tb  =  xcryptographic_aria_p2(tb) ^ ta;
    *a  ^= xcryptographic_aria_p1(tb);
    ta  =  xcryptographic_aria_p2(ta);
    *d  ^= xcryptographic_aria_p1(ta) ^ tc;
    tc  =  xcryptographic_aria_p2(tc);
    *c  ^= xcryptographic_aria_p1(tc) ^ ta;
}

static void xcryptographic_aria_sl(uint32_t* a, uint32_t* b,
                                   uint32_t* c, uint32_t* d,
                                   const uint8_t sa[256],
                                   const uint8_t sb[256],
                                   const uint8_t sc[256],
                                   const uint8_t sd[256])
{
    *a = ((uint32_t) sa[*a & 0xFFu]) ^
         (((uint32_t) sb[(*a >> 8) & 0xFFu]) <<  8) ^
         (((uint32_t) sc[(*a >> 16) & 0xFFu]) << 16) ^
         (((uint32_t) sd[(*a >> 24) & 0xFFu]) << 24);
    *b = ((uint32_t) sa[*b & 0xFFu]) ^
         (((uint32_t) sb[(*b >> 8) & 0xFFu]) <<  8) ^
         (((uint32_t) sc[(*b >> 16) & 0xFFu]) << 16) ^
         (((uint32_t) sd[(*b >> 24) & 0xFFu]) << 24);
    *c = ((uint32_t) sa[*c & 0xFFu]) ^
         (((uint32_t) sb[(*c >> 8) & 0xFFu]) <<  8) ^
         (((uint32_t) sc[(*c >> 16) & 0xFFu]) << 16) ^
         (((uint32_t) sd[(*c >> 24) & 0xFFu]) << 24);
    *d = ((uint32_t) sa[*d & 0xFFu]) ^
         (((uint32_t) sb[(*d >> 8) & 0xFFu]) <<  8) ^
         (((uint32_t) sc[(*d >> 16) & 0xFFu]) << 16) ^
         (((uint32_t) sd[(*d >> 24) & 0xFFu]) << 24);
}

static const uint8_t xcryptographic_aria_sb1[256] =
{
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16,
};

static const uint8_t xcryptographic_aria_sb2[256] =
{
    0xE2, 0x4E, 0x54, 0xFC, 0x94, 0xC2, 0x4A, 0xCC, 0x62, 0x0D, 0x6A, 0x46,
    0x3C, 0x4D, 0x8B, 0xD1, 0x5E, 0xFA, 0x64, 0xCB, 0xB4, 0x97, 0xBE, 0x2B,
    0xBC, 0x77, 0x2E, 0x03, 0xD3, 0x19, 0x59, 0xC1, 0x1D, 0x06, 0x41, 0x6B,
    0x55, 0xF0, 0x99, 0x69, 0xEA, 0x9C, 0x18, 0xAE, 0x63, 0xDF, 0xE7, 0xBB,
    0x00, 0x73, 0x66, 0xFB, 0x96, 0x4C, 0x85, 0xE4, 0x3A, 0x09, 0x45, 0xAA,
    0x0F, 0xEE, 0x10, 0xEB, 0x2D, 0x7F, 0xF4, 0x29, 0xAC, 0xCF, 0xAD, 0x91,
    0x8D, 0x78, 0xC8, 0x95, 0xF9, 0x2F, 0xCE, 0xCD, 0x08, 0x7A, 0x88, 0x38,
    0x5C, 0x83, 0x2A, 0x28, 0x47, 0xDB, 0xB8, 0xC7, 0x93, 0xA4, 0x12, 0x53,
    0xFF, 0x87, 0x0E, 0x31, 0x36, 0x21, 0x58, 0x48, 0x01, 0x8E, 0x37, 0x74,
    0x32, 0xCA, 0xE9, 0xB1, 0xB7, 0xAB, 0x0C, 0xD7, 0xC4, 0x56, 0x42, 0x26,
    0x07, 0x98, 0x60, 0xD9, 0xB6, 0xB9, 0x11, 0x40, 0xEC, 0x20, 0x8C, 0xBD,
    0xA0, 0xC9, 0x84, 0x04, 0x49, 0x23, 0xF1, 0x4F, 0x50, 0x1F, 0x13, 0xDC,
    0xD8, 0xC0, 0x9E, 0x57, 0xE3, 0xC3, 0x7B, 0x65, 0x3B, 0x02, 0x8F, 0x3E,
    0xE8, 0x25, 0x92, 0xE5, 0x15, 0xDD, 0xFD, 0x17, 0xA9, 0xBF, 0xD4, 0x9A,
    0x7E, 0xC5, 0x39, 0x67, 0xFE, 0x76, 0x9D, 0x43, 0xA7, 0xE1, 0xD0, 0xF5,
    0x68, 0xF2, 0x1B, 0x34, 0x70, 0x05, 0xA3, 0x8A, 0xD5, 0x79, 0x86, 0xA8,
    0x30, 0xC6, 0x51, 0x4B, 0x1E, 0xA6, 0x27, 0xF6, 0x35, 0xD2, 0x6E, 0x24,
    0x16, 0x82, 0x5F, 0xDA, 0xE6, 0x75, 0xA2, 0xEF, 0x2C, 0xB2, 0x1C, 0x9F,
    0x5D, 0x6F, 0x80, 0x0A, 0x72, 0x44, 0x9B, 0x6C, 0x90, 0x0B, 0x5B, 0x33,
    0x7D, 0x5A, 0x52, 0xF3, 0x61, 0xA1, 0xF7, 0xB0, 0xD6, 0x3F, 0x7C, 0x6D,
    0xED, 0x14, 0xE0, 0xA5, 0x3D, 0x22, 0xB3, 0xF8, 0x89, 0xDE, 0x71, 0x1A,
    0xAF, 0xBA, 0xB5, 0x81,
};

static const uint8_t xcryptographic_aria_is1[256] =
{
    0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38, 0xBF, 0x40, 0xA3, 0x9E,
    0x81, 0xF3, 0xD7, 0xFB, 0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87,
    0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB, 0x54, 0x7B, 0x94, 0x32,
    0xA6, 0xC2, 0x23, 0x3D, 0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E,
    0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2, 0x76, 0x5B, 0xA2, 0x49,
    0x6D, 0x8B, 0xD1, 0x25, 0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65, 0xB6, 0x92, 0x6C, 0x70, 0x48, 0x50,
    0xFD, 0xED, 0xB9, 0xDA, 0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D, 0x84,
    0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A, 0xF7, 0xE4, 0x58, 0x05,
    0xB8, 0xB3, 0x45, 0x06, 0xD0, 0x2C, 0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02,
    0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B, 0x3A, 0x91, 0x11, 0x41,
    0x4F, 0x67, 0xDC, 0xEA, 0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73,
    0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85, 0xE2, 0xF9, 0x37, 0xE8,
    0x1C, 0x75, 0xDF, 0x6E, 0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89,
    0x6F, 0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B, 0xFC, 0x56, 0x3E, 0x4B,
    0xC6, 0xD2, 0x79, 0x20, 0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4,
    0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31, 0xB1, 0x12, 0x10, 0x59,
    0x27, 0x80, 0xEC, 0x5F, 0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D,
    0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF, 0xA0, 0xE0, 0x3B, 0x4D,
    0xAE, 0x2A, 0xF5, 0xB0, 0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6, 0x26, 0xE1, 0x69, 0x14, 0x63,
    0x55, 0x21, 0x0C, 0x7D,
};

static const uint8_t xcryptographic_aria_is2[256] =
{
    0x30, 0x68, 0x99, 0x1B, 0x87, 0xB9, 0x21, 0x78, 0x50, 0x39, 0xDB, 0xE1,
    0x72, 0x09, 0x62, 0x3C, 0x3E, 0x7E, 0x5E, 0x8E, 0xF1, 0xA0, 0xCC, 0xA3,
    0x2A, 0x1D, 0xFB, 0xB6, 0xD6, 0x20, 0xC4, 0x8D, 0x81, 0x65, 0xF5, 0x89,
    0xCB, 0x9D, 0x77, 0xC6, 0x57, 0x43, 0x56, 0x17, 0xD4, 0x40, 0x1A, 0x4D,
    0xC0, 0x63, 0x6C, 0xE3, 0xB7, 0xC8, 0x64, 0x6A, 0x53, 0xAA, 0x38, 0x98,
    0x0C, 0xF4, 0x9B, 0xED, 0x7F, 0x22, 0x76, 0xAF, 0xDD, 0x3A, 0x0B, 0x58,
    0x67, 0x88, 0x06, 0xC3, 0x35, 0x0D, 0x01, 0x8B, 0x8C, 0xC2, 0xE6, 0x5F,
    0x02, 0x24, 0x75, 0x93, 0x66, 0x1E, 0xE5, 0xE2, 0x54, 0xD8, 0x10, 0xCE,
    0x7A, 0xE8, 0x08, 0x2C, 0x12, 0x97, 0x32, 0xAB, 0xB4, 0x27, 0x0A, 0x23,
    0xDF, 0xEF, 0xCA, 0xD9, 0xB8, 0xFA, 0xDC, 0x31, 0x6B, 0xD1, 0xAD, 0x19,
    0x49, 0xBD, 0x51, 0x96, 0xEE, 0xE4, 0xA8, 0x41, 0xDA, 0xFF, 0xCD, 0x55,
    0x86, 0x36, 0xBE, 0x61, 0x52, 0xF8, 0xBB, 0x0E, 0x82, 0x48, 0x69, 0x9A,
    0xE0, 0x47, 0x9E, 0x5C, 0x04, 0x4B, 0x34, 0x15, 0x79, 0x26, 0xA7, 0xDE,
    0x29, 0xAE, 0x92, 0xD7, 0x84, 0xE9, 0xD2, 0xBA, 0x5D, 0xF3, 0xC5, 0xB0,
    0xBF, 0xA4, 0x3B, 0x71, 0x44, 0x46, 0x2B, 0xFC, 0xEB, 0x6F, 0xD5, 0xF6,
    0x14, 0xFE, 0x7C, 0x70, 0x5A, 0x7D, 0xFD, 0x2F, 0x18, 0x83, 0x16, 0xA5,
    0x91, 0x1F, 0x05, 0x95, 0x74, 0xA9, 0xC1, 0x5B, 0x4A, 0x85, 0x6D, 0x13,
    0x07, 0x4F, 0x4E, 0x45, 0xB2, 0x0F, 0xC9, 0x1C, 0xA6, 0xBC, 0xEC, 0x73,
    0x90, 0x7B, 0xCF, 0x59, 0x8F, 0xA1, 0xF9, 0x2D, 0xF2, 0xB1, 0x00, 0x94,
    0x37, 0x9F, 0xD0, 0x2E, 0x9C, 0x6E, 0x28, 0x3F, 0x80, 0xF0, 0x3D, 0xD3,
    0x25, 0x8A, 0xB5, 0xE7, 0x42, 0xB3, 0xC7, 0xEA, 0xF7, 0x4C, 0x11, 0x33,
    0x03, 0xA2, 0xAC, 0x60,
};

static void xcryptographic_aria_fo_xor(uint32_t r[4], const uint32_t p[4],
                                       const uint32_t k[4], const uint32_t x[4])
{
    uint32_t a, b, c, d;
    a = p[0] ^ k[0];
    b = p[1] ^ k[1];
    c = p[2] ^ k[2];
    d = p[3] ^ k[3];
    xcryptographic_aria_sl(&a, &b, &c, &d,
                           xcryptographic_aria_sb1, xcryptographic_aria_sb2,
                           xcryptographic_aria_is1, xcryptographic_aria_is2);
    xcryptographic_aria_a(&a, &b, &c, &d);
    r[0] = a ^ x[0];
    r[1] = b ^ x[1];
    r[2] = c ^ x[2];
    r[3] = d ^ x[3];
}

static void xcryptographic_aria_fe_xor(uint32_t r[4], const uint32_t p[4],
                                       const uint32_t k[4], const uint32_t x[4])
{
    uint32_t a, b, c, d;
    a = p[0] ^ k[0];
    b = p[1] ^ k[1];
    c = p[2] ^ k[2];
    d = p[3] ^ k[3];
    xcryptographic_aria_sl(&a, &b, &c, &d,
                           xcryptographic_aria_is1, xcryptographic_aria_is2,
                           xcryptographic_aria_sb1, xcryptographic_aria_sb2);
    xcryptographic_aria_a(&a, &b, &c, &d);
    r[0] = a ^ x[0];
    r[1] = b ^ x[1];
    r[2] = c ^ x[2];
    r[3] = d ^ x[3];
}

static void xcryptographic_aria_rot128(uint32_t r[4], const uint32_t a[4],
                                       const uint32_t b[4], uint8_t n)
{
    uint8_t i, j;
    uint32_t t, u;
    const uint8_t n1 = (uint8_t)(n % 32);
    const uint8_t n2 = n1 ? (uint8_t)(32 - n1) : 0;
    j = (uint8_t)((n / 32) % 4);
    t = xcryptographic_aria_p3(b[j]);
    for (i = 0; i < 4; i++) {
        j = (uint8_t)((j + 1) % 4);
        u = xcryptographic_aria_p3(b[j]);
        t <<= n1;
        t |= u >> n2;
        t = xcryptographic_aria_p3(t);
        r[i] = a[i] ^ t;
        t = u;
    }
}

static bool xcryptographic_aria_enabled(size_t keyLen)
{
    return XCRYPTOGRAPHIC_ARIA_ON != 0 &&
           (keyLen == 16 || keyLen == 24 || keyLen == 32);
}

static void xcryptographic_aria_setkey_enc(uint32_t rk[68], uint8_t* nr,
                                           const uint8_t* key, size_t keyLen)
{
    static const uint32_t rc[3][4] =
    {
        { 0xB7C17C51u, 0x940A2227u, 0xE8AB13FEu, 0xE06E9AFAu },
        { 0xCC4AB16Du, 0x20C8219Eu, 0xD5B128FFu, 0xB0E25DEFu },
        { 0x1D3792DBu, 0x70E92621u, 0x75972403u, 0x0EC9E804u }
    };
    int i;
    uint32_t w[4][4], *w2;

    w[0][0] = xcryptographic_load_u32_le_aria(key + 0);
    w[0][1] = xcryptographic_load_u32_le_aria(key + 4);
    w[0][2] = xcryptographic_load_u32_le_aria(key + 8);
    w[0][3] = xcryptographic_load_u32_le_aria(key + 12);
    memset(w[1], 0, sizeof(w[1]));
    if (keyLen >= 24) {
        w[1][0] = xcryptographic_load_u32_le_aria(key + 16);
        w[1][1] = xcryptographic_load_u32_le_aria(key + 20);
    }
    if (keyLen == 32) {
        w[1][2] = xcryptographic_load_u32_le_aria(key + 24);
        w[1][3] = xcryptographic_load_u32_le_aria(key + 28);
    }

    i = (int)((keyLen - 16) >> 3);
    *nr = (uint8_t)(12 + 2 * i);

    xcryptographic_aria_fo_xor(w[1], w[0], rc[i], w[1]);
    i = i < 2 ? i + 1 : 0;
    xcryptographic_aria_fe_xor(w[2], w[1], rc[i], w[0]);
    i = i < 2 ? i + 1 : 0;
    xcryptographic_aria_fo_xor(w[3], w[2], rc[i], w[1]);

    for (i = 0; i < 4; i++) {
        w2 = w[(i + 1) & 3];
        xcryptographic_aria_rot128(&rk[i * 4], w[i], w2, (uint8_t)(128 - 19));
        xcryptographic_aria_rot128(&rk[(i + 4) * 4], w[i], w2, (uint8_t)(128 - 31));
        xcryptographic_aria_rot128(&rk[(i + 8) * 4], w[i], w2, 61);
        xcryptographic_aria_rot128(&rk[(i + 12) * 4], w[i], w2, 31);
    }
    xcryptographic_aria_rot128(&rk[16 * 4], w[0], w[1], 19);
    memset(w, 0, sizeof(w));
}

static void xcryptographic_aria_setkey_dec(uint32_t rk[68], uint8_t* nr,
                                           const uint8_t* key, size_t keyLen)
{
    int i, j, k;
    xcryptographic_aria_setkey_enc(rk, nr, key, keyLen);
    for (i = 0, j = *nr; i < j; i++, j--) {
        for (k = 0; k < 4; k++) {
            uint32_t t = rk[i * 4 + k];
            rk[i * 4 + k] = rk[j * 4 + k];
            rk[j * 4 + k] = t;
        }
    }
    for (i = 1; i < *nr; i++) {
        xcryptographic_aria_a(&rk[i * 4 + 0], &rk[i * 4 + 1],
                              &rk[i * 4 + 2], &rk[i * 4 + 3]);
    }
}

static void xcryptographic_aria_crypt(const uint32_t rk[68], uint8_t nr,
                                      const uint8_t input[16],
                                      uint8_t output[16])
{
    int i = 0;
    uint32_t a, b, c, d;
    a = xcryptographic_load_u32_le_aria(input + 0);
    b = xcryptographic_load_u32_le_aria(input + 4);
    c = xcryptographic_load_u32_le_aria(input + 8);
    d = xcryptographic_load_u32_le_aria(input + 12);
    while (1) {
        a ^= rk[i * 4 + 0];
        b ^= rk[i * 4 + 1];
        c ^= rk[i * 4 + 2];
        d ^= rk[i * 4 + 3];
        i++;
        xcryptographic_aria_sl(&a, &b, &c, &d,
                               xcryptographic_aria_sb1, xcryptographic_aria_sb2,
                               xcryptographic_aria_is1, xcryptographic_aria_is2);
        xcryptographic_aria_a(&a, &b, &c, &d);
        a ^= rk[i * 4 + 0];
        b ^= rk[i * 4 + 1];
        c ^= rk[i * 4 + 2];
        d ^= rk[i * 4 + 3];
        i++;
        xcryptographic_aria_sl(&a, &b, &c, &d,
                               xcryptographic_aria_is1, xcryptographic_aria_is2,
                               xcryptographic_aria_sb1, xcryptographic_aria_sb2);
        if (i >= nr) break;
        xcryptographic_aria_a(&a, &b, &c, &d);
    }
    a ^= rk[i * 4 + 0];
    b ^= rk[i * 4 + 1];
    c ^= rk[i * 4 + 2];
    d ^= rk[i * 4 + 3];
    xcryptographic_store_u32_le_aria(output + 0, a);
    xcryptographic_store_u32_le_aria(output + 4, b);
    xcryptographic_store_u32_le_aria(output + 8, c);
    xcryptographic_store_u32_le_aria(output + 12, d);
}

/* =============== ARIA 分组密码结束 =============== */


/* =============== Camellia 分组密码（RFC 3713） =============== */

static const uint8_t xcryptographic_camellia_sigma[6][8] =
{
    { 0xa0, 0x9e, 0x66, 0x7f, 0x3b, 0xcc, 0x90, 0x8b },
    { 0xb6, 0x7a, 0xe8, 0x58, 0x4c, 0xaa, 0x73, 0xb2 },
    { 0xc6, 0xef, 0x37, 0x2f, 0xe9, 0x4f, 0x82, 0xbe },
    { 0x54, 0xff, 0x53, 0xa5, 0xf1, 0xd3, 0x6f, 0x1c },
    { 0x10, 0xe5, 0x27, 0xfa, 0xde, 0x68, 0x2d, 0x1d },
    { 0xb0, 0x56, 0x88, 0xc2, 0xb3, 0xe6, 0xc1, 0xfd }
};

static const uint8_t xcryptographic_camellia_fsb[256] =
{
    112, 130,  44, 236, 179,  39, 192, 229, 228, 133,  87,  53,
    234,  12, 174,  65,  35, 239, 107, 147,  69,  25, 165,  33,
    237,  14,  79,  78,  29, 101, 146, 189, 134, 184, 175, 143,
    124, 235,  31, 206,  62,  48, 220,  95,  94, 197,  11,  26,
    166, 225,  57, 202, 213,  71,  93,  61, 217,   1,  90, 214,
     81,  86, 108,  77, 139,  13, 154, 102, 251, 204, 176,  45,
    116,  18,  43,  32, 240, 177, 132, 153, 223,  76, 203, 194,
     52, 126, 118,   5, 109, 183, 169,  49, 209,  23,   4, 215,
     20,  88,  58,  97, 222,  27,  17,  28,  50,  15, 156,  22,
     83,  24, 242,  34, 254,  68, 207, 178, 195, 181, 122, 145,
     36,   8, 232, 168,  96, 252, 105,  80, 170, 208, 160, 125,
    161, 137,  98, 151,  84,  91,  30, 149, 224, 255, 100, 210,
     16, 196,   0,  72, 163, 247, 117, 219, 138,   3, 230, 218,
      9,  63, 221, 148, 135,  92, 131,   2, 205,  74, 144,  51,
    115, 103, 246, 243, 157, 127, 191, 226,  82, 155, 216,  38,
    200,  55, 198,  59, 129, 150, 111,  75,  19, 190,  99,  46,
    233, 121, 167, 140, 159, 110, 188, 142,  41, 245, 249, 182,
     47, 253, 180,  89, 120, 152,   6, 106, 231,  70, 113, 186,
    212,  37, 171,  66, 136, 162, 141, 250, 114,   7, 185,  85,
    248, 238, 172,  10,  54,  73,  42, 104,  60,  56, 241, 164,
     64,  40, 211, 123, 187, 201,  67, 193,  21, 227, 173, 244,
    119, 199, 128, 158,
};

static const uint8_t xcryptographic_camellia_fsb2[256] =
{
    224,   5,  88, 217, 103,  78, 129, 203, 201,  11, 174, 106,
    213,  24,  93, 130,  70, 223, 214,  39, 138,  50,  75,  66,
    219,  28, 158, 156,  58, 202,  37, 123,  13, 113,  95,  31,
    248, 215,  62, 157, 124,  96, 185, 190, 188, 139,  22,  52,
     77, 195, 114, 149, 171, 142, 186, 122, 179,   2, 180, 173,
    162, 172, 216, 154,  23,  26,  53, 204, 247, 153,  97,  90,
    232,  36,  86,  64, 225,  99,   9,  51, 191, 152, 151, 133,
    104, 252, 236,  10, 218, 111,  83,  98, 163,  46,   8, 175,
     40, 176, 116, 194, 189,  54,  34,  56, 100,  30,  57,  44,
    166,  48, 229,  68, 253, 136, 159, 101, 135, 107, 244,  35,
     72,  16, 209,  81, 192, 249, 210, 160,  85, 161,  65, 250,
     67,  19, 196,  47, 168, 182,  60,  43, 193, 255, 200, 165,
     32, 137,   0, 144,  71, 239, 234, 183,  21,   6, 205, 181,
     18, 126, 187,  41,  15, 184,   7,   4, 155, 148,  33, 102,
    230, 206, 237, 231,  59, 254, 127, 197, 164,  55, 177,  76,
    145, 110, 141, 118,   3,  45, 222, 150,  38, 125, 198,  92,
    211, 242,  79,  25,  63, 220, 121,  29,  82, 235, 243, 109,
     94, 251, 105, 178, 240,  49,  12, 212, 207, 140, 226, 117,
    169,  74,  87, 132,  17,  69,  27, 245, 228,  14, 115, 170,
    241, 221,  89,  20, 108, 146,  84, 208, 120, 112, 227,  73,
    128,  80, 167, 246, 119, 147, 134, 131,  42, 199,  91, 233,
    238, 143,   1,  61,
};

static const uint8_t xcryptographic_camellia_fsb3[256] =
{
     56,  65,  22, 118, 217, 147,  96, 242, 114, 194, 171, 154,
    117,   6,  87, 160, 145, 247, 181, 201, 162, 140, 210, 144,
    246,   7, 167,  39, 142, 178,  73, 222,  67,  92, 215, 199,
     62, 245, 143, 103,  31,  24, 110, 175,  47, 226, 133,  13,
     83, 240, 156, 101, 234, 163, 174, 158, 236, 128,  45, 107,
    168,  43,  54, 166, 197, 134,  77,  51, 253, 102,  88, 150,
     58,   9, 149,  16, 120, 216,  66, 204, 239,  38, 229,  97,
     26,  63,  59, 130, 182, 219, 212, 152, 232, 139,   2, 235,
     10,  44,  29, 176, 111, 141, 136,  14,  25, 135,  78,  11,
    169,  12, 121,  17, 127,  34, 231,  89, 225, 218,  61, 200,
     18,   4, 116,  84,  48, 126, 180,  40,  85, 104,  80, 190,
    208, 196,  49, 203,  42, 173,  15, 202, 112, 255,  50, 105,
      8,  98,   0,  36, 209, 251, 186, 237,  69, 129, 115, 109,
    132, 159, 238,  74, 195,  46, 193,   1, 230,  37,  72, 153,
    185, 179, 123, 249, 206, 191, 223, 113,  41, 205, 108,  19,
    100, 155,  99, 157, 192,  75, 183, 165, 137,  95, 177,  23,
    244, 188, 211,  70, 207,  55,  94,  71, 148, 250, 252,  91,
    151, 254,  90, 172,  60,  76,   3,  53, 243,  35, 184,  93,
    106, 146, 213,  33,  68,  81, 198, 125,  57, 131, 220, 170,
    124, 119,  86,   5,  27, 164,  21,  52,  30,  28, 248,  82,
     32,  20, 233, 189, 221, 228, 161, 224, 138, 241, 214, 122,
    187, 227,  64,  79,
};

static const uint8_t xcryptographic_camellia_fsb4[256] =
{
    112,  44, 179, 192, 228,  87, 234, 174,  35, 107,  69, 165,
    237,  79,  29, 146, 134, 175, 124,  31,  62, 220,  94,  11,
    166,  57, 213,  93, 217,  90,  81, 108, 139, 154, 251, 176,
    116,  43, 240, 132, 223, 203,  52, 118, 109, 169, 209,   4,
     20,  58, 222,  17,  50, 156,  83, 242, 254, 207, 195, 122,
     36, 232,  96, 105, 170, 160, 161,  98,  84,  30, 224, 100,
     16,   0, 163, 117, 138, 230,   9, 221, 135, 131, 205, 144,
    115, 246, 157, 191,  82, 216, 200, 198, 129, 111,  19,  99,
    233, 167, 159, 188,  41, 249,  47, 180, 120,   6, 231, 113,
    212, 171, 136, 141, 114, 185, 248, 172,  54,  42,  60, 241,
     64, 211, 187,  67,  21, 173, 119, 128, 130, 236,  39, 229,
    133,  53,  12,  65, 239, 147,  25,  33,  14,  78, 101, 189,
    184, 143, 235, 206,  48,  95, 197,  26, 225, 202,  71,  61,
      1, 214,  86,  77,  13, 102, 204,  45,  18,  32, 177, 153,
     76, 194, 126,   5, 183,  49,  23, 215,  88,  97,  27,  28,
     15,  22,  24,  34,  68, 178, 181, 145,   8, 168, 252,  80,
    208, 125, 137, 151,  91, 149, 255, 210, 196,  72, 247, 219,
      3, 218,  63, 148,  92,   2,  74,  51, 103, 243, 127, 226,
    155,  38,  55,  59, 150,  75, 190,  46, 121, 140, 110, 142,
    245, 182, 253,  89, 152, 106,  70, 186,  37,  66, 162, 250,
      7,  85, 238,  10,  73, 104,  56, 164,  40, 123, 201, 193,
    227, 244, 199, 158,
};

#define XC_CAM_SBOX1(n) xcryptographic_camellia_fsb[(n)]
#define XC_CAM_SBOX2(n) xcryptographic_camellia_fsb2[(n)]
#define XC_CAM_SBOX3(n) xcryptographic_camellia_fsb3[(n)]
#define XC_CAM_SBOX4(n) xcryptographic_camellia_fsb4[(n)]

static const uint8_t xcryptographic_camellia_shifts[2][4][4] =
{
    {
        { 1, 1, 1, 1 },
        { 0, 0, 0, 0 },
        { 1, 1, 1, 1 },
        { 0, 0, 0, 0 }
    },
    {
        { 1, 0, 1, 1 },
        { 1, 1, 0, 1 },
        { 1, 1, 1, 0 },
        { 1, 1, 0, 1 }
    }
};

static const int8_t xcryptographic_camellia_indexes[2][4][20] =
{
    {
        {  0,  1,  2,  3,  8,  9, 10, 11, 38, 39,
           36, 37, 23, 20, 21, 22, 27, -1, -1, 26 },
        { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
          -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        {  4,  5,  6,  7, 12, 13, 14, 15, 16, 17,
           18, 19, -1, 24, 25, -1, 31, 28, 29, 30 },
        { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
          -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
    },
    {
        {  0,  1,  2,  3, 61, 62, 63, 60, -1, -1,
           -1, -1, 27, 24, 25, 26, 35, 32, 33, 34 },
        { -1, -1, -1, -1,  8,  9, 10, 11, 16, 17,
          18, 19, -1, -1, -1, -1, 39, 36, 37, 38 },
        { -1, -1, -1, -1, 12, 13, 14, 15, 58, 59,
          56, 57, 31, 28, 29, 30, -1, -1, -1, -1 },
        {  4,  5,  6,  7, 65, 66, 67, 64, 20, 21,
           22, 23, -1, -1, -1, -1, 43, 40, 41, 42 }
    }
};

static const int8_t xcryptographic_camellia_transposes[2][20] =
{
    {
        21, 22, 23, 20,
        -1, -1, -1, -1,
        18, 19, 16, 17,
        11,  8,  9, 10,
        15, 12, 13, 14
    },
    {
        25, 26, 27, 24,
        29, 30, 31, 28,
        18, 19, 16, 17,
        -1, -1, -1, -1,
        -1, -1, -1, -1
    }
};

static uint32_t xcryptographic_load_u32_be_cam(uint8_t const* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void xcryptographic_store_u32_be_cam(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void xcryptographic_camellia_rotl(uint32_t dst[4], const uint32_t src[4],
                                         uint32_t shift)
{
    dst[0] = (src[0] << shift) ^ (src[1] >> (32 - shift));
    dst[1] = (src[1] << shift) ^ (src[2] >> (32 - shift));
    dst[2] = (src[2] << shift) ^ (src[3] >> (32 - shift));
    dst[3] = (src[3] << shift) ^ (src[0] >> (32 - shift));
}

static void xcryptographic_camellia_fl(uint32_t xl, uint32_t xr,
                                       uint32_t kl, uint32_t kr,
                                       uint32_t* yl, uint32_t* yr)
{
    xr = ((((xl & kl) << 1) | ((xl & kl) >> 31)) ^ xr);
    xl = ((xr | kr) ^ xl);
    *yl = xl;
    *yr = xr;
}

static void xcryptographic_camellia_flinv(uint32_t yl, uint32_t yr,
                                          uint32_t kl, uint32_t kr,
                                          uint32_t* xl, uint32_t* xr)
{
    yl = ((yr | kr) ^ yl);
    yr = ((((yl & kl) << 1) | ((yl & kl) >> 31)) ^ yr);
    *xl = yl;
    *xr = yr;
}

static void xcryptographic_camellia_feistel(const uint32_t x[2],
                                            const uint32_t k[2],
                                            uint32_t z[2])
{
    uint32_t i0, i1;
    i0 = x[0] ^ k[0];
    i1 = x[1] ^ k[1];
    i0 = ((uint32_t) XC_CAM_SBOX1((i0 >> 24) & 0xFFu) << 24) |
         ((uint32_t) XC_CAM_SBOX2((i0 >> 16) & 0xFFu) << 16) |
         ((uint32_t) XC_CAM_SBOX3((i0 >> 8) & 0xFFu) <<  8) |
         ((uint32_t) XC_CAM_SBOX4(i0 & 0xFFu));
    i1 = ((uint32_t) XC_CAM_SBOX2((i1 >> 24) & 0xFFu) << 24) |
         ((uint32_t) XC_CAM_SBOX3((i1 >> 16) & 0xFFu) << 16) |
         ((uint32_t) XC_CAM_SBOX4((i1 >> 8) & 0xFFu) <<  8) |
         ((uint32_t) XC_CAM_SBOX1(i1 & 0xFFu));
    i0 ^= (i1 << 8) | (i1 >> 24);
    i1 ^= (i0 << 16) | (i0 >> 16);
    i0 ^= (i1 >> 8) | (i1 << 24);
    i1 ^= (i0 >> 8) | (i0 << 24);
    z[0] ^= i1;
    z[1] ^= i0;
}

static void xcryptographic_camellia_shift_and_place(uint32_t rk[68], int index,
                                                    int offset,
                                                    const uint32_t kc[16])
{
    uint32_t tk[20];
    int i;
    tk[0] = kc[offset * 4 + 0];
    tk[1] = kc[offset * 4 + 1];
    tk[2] = kc[offset * 4 + 2];
    tk[3] = kc[offset * 4 + 3];
    for (i = 1; i <= 4; i++)
        if (xcryptographic_camellia_shifts[index][offset][i - 1])
            xcryptographic_camellia_rotl(tk + i * 4, tk, (uint32_t)((15 * i) % 32));
    for (i = 0; i < 20; i++)
        if (xcryptographic_camellia_indexes[index][offset][i] != -1)
            rk[xcryptographic_camellia_indexes[index][offset][i]] = tk[i];
}

static bool xcryptographic_camellia_enabled(size_t keyLen)
{
    return XCRYPTOGRAPHIC_CAMELLIA_ON != 0 &&
           (keyLen == 16 || keyLen == 24 || keyLen == 32);
}

static void xcryptographic_camellia_setkey_enc(uint32_t rk[68], uint8_t* nr,
                                               const uint8_t* key, size_t keyLen)
{
    int idx;
    size_t i;
    uint32_t sigma[6][2];
    uint32_t kc[16];
    uint32_t tk[20] = { 0 };
    unsigned char t[64] = { 0 };

    memset(rk, 0, 68 * sizeof(uint32_t));
    if (keyLen == 16) {
        *nr = 3;
        idx = 0;
    } else {
        *nr = 4;
        idx = 1;
    }

    memcpy(t, key, keyLen);
    if (keyLen == 24) {
        for (i = 0; i < 8; i++) t[24 + i] = (unsigned char)~t[16 + i];
    }

    for (i = 0; i < 6; i++) {
        sigma[i][0] = xcryptographic_load_u32_be_cam(xcryptographic_camellia_sigma[i] + 0);
        sigma[i][1] = xcryptographic_load_u32_be_cam(xcryptographic_camellia_sigma[i] + 4);
    }

    memset(kc, 0, sizeof(kc));
    for (i = 0; i < 8; i++) kc[i] = xcryptographic_load_u32_be_cam(t + i * 4);

    for (i = 0; i < 4; ++i) kc[8 + i] = kc[i] ^ kc[4 + i];
    xcryptographic_camellia_feistel(kc + 8, sigma[0], kc + 10);
    xcryptographic_camellia_feistel(kc + 10, sigma[1], kc + 8);
    for (i = 0; i < 4; ++i) kc[8 + i] ^= kc[i];
    xcryptographic_camellia_feistel(kc + 8, sigma[2], kc + 10);
    xcryptographic_camellia_feistel(kc + 10, sigma[3], kc + 8);

    if (keyLen > 16) {
        for (i = 0; i < 4; ++i) kc[12 + i] = kc[4 + i] ^ kc[8 + i];
        xcryptographic_camellia_feistel(kc + 12, sigma[4], kc + 14);
        xcryptographic_camellia_feistel(kc + 14, sigma[5], kc + 12);
    }

    xcryptographic_camellia_shift_and_place(rk, idx, 0, kc);
    if (keyLen > 16) xcryptographic_camellia_shift_and_place(rk, idx, 1, kc);
    xcryptographic_camellia_shift_and_place(rk, idx, 2, kc);
    if (keyLen > 16) xcryptographic_camellia_shift_and_place(rk, idx, 3, kc);

    for (i = 0; i < 20; i++) {
        if (xcryptographic_camellia_transposes[idx][i] != -1) {
            rk[32 + 12 * idx + i] = rk[xcryptographic_camellia_transposes[idx][i]];
        }
    }
    memset(t, 0, sizeof(t));
    memset(sigma, 0, sizeof(sigma));
    memset(kc, 0, sizeof(kc));
    memset(tk, 0, sizeof(tk));
}

static void xcryptographic_camellia_setkey_dec(uint32_t rk[68], uint8_t* nr,
                                               const uint8_t* key, size_t keyLen)
{
    uint32_t sk[68];
    uint8_t cty_nr = 0;
    int idx;
    size_t i;
    uint32_t* rkp = rk;
    uint32_t* skp;

    memset(sk, 0, sizeof(sk));
    xcryptographic_camellia_setkey_enc(sk, &cty_nr, key, keyLen);
    *nr = cty_nr;
    idx = (cty_nr == 4);
    skp = sk + 24 * 2 + 8 * idx * 2;

    *rkp++ = *skp++;
    *rkp++ = *skp++;
    *rkp++ = *skp++;
    *rkp++ = *skp++;
    for (i = 22 + 8 * idx, skp -= 6; i > 0; i--, skp -= 4) {
        *rkp++ = *skp++;
        *rkp++ = *skp++;
    }
    skp -= 2;
    *rkp++ = *skp++;
    *rkp++ = *skp++;
    *rkp++ = *skp++;
    *rkp++ = *skp++;
    memset(sk, 0, sizeof(sk));
}

static void xcryptographic_camellia_crypt(const uint32_t rk[68], uint8_t nr,
                                          const uint8_t input[16],
                                          uint8_t output[16])
{
    const uint32_t* rkp = rk;
    uint32_t x[4];
    uint32_t xl0, xr0, xl1, xr1;
    int cc = nr;

    (void)cc;
    x[0] = xcryptographic_load_u32_be_cam(input + 0);
    x[1] = xcryptographic_load_u32_be_cam(input + 4);
    x[2] = xcryptographic_load_u32_be_cam(input + 8);
    x[3] = xcryptographic_load_u32_be_cam(input + 12);

    x[0] ^= *rkp++;
    x[1] ^= *rkp++;
    x[2] ^= *rkp++;
    x[3] ^= *rkp++;

    while (cc) {
        --cc;
        xcryptographic_camellia_feistel(x, rkp, x + 2);
        rkp += 2;
        xcryptographic_camellia_feistel(x + 2, rkp, x);
        rkp += 2;
        xcryptographic_camellia_feistel(x, rkp, x + 2);
        rkp += 2;
        xcryptographic_camellia_feistel(x + 2, rkp, x);
        rkp += 2;
        xcryptographic_camellia_feistel(x, rkp, x + 2);
        rkp += 2;
        xcryptographic_camellia_feistel(x + 2, rkp, x);
        rkp += 2;
        if (cc) {
            xcryptographic_camellia_fl(x[0], x[1], rkp[0], rkp[1], &xl0, &xr0);
            x[0] = xl0;
            x[1] = xr0;
            rkp += 2;
            xcryptographic_camellia_flinv(x[2], x[3], rkp[0], rkp[1], &xl1, &xr1);
            x[2] = xl1;
            x[3] = xr1;
            rkp += 2;
        }
    }

    x[2] ^= *rkp++;
    x[3] ^= *rkp++;
    x[0] ^= *rkp++;
    x[1] ^= *rkp++;

    xcryptographic_store_u32_be_cam(output + 0, x[2]);
    xcryptographic_store_u32_be_cam(output + 4, x[3]);
    xcryptographic_store_u32_be_cam(output + 8, x[0]);
    xcryptographic_store_u32_be_cam(output + 12, x[1]);
}

/* =============== Camellia 分组密码结束 =============== */

static void xcryptographic_increment_counter(uint8_t counter[16])
{
    int i;
    for (i = 15; i >= 0; --i)
        if (++counter[i] != 0) break;
}

static bool xcryptographic_random(void* output, size_t size)
{
    return output && (size == 0 || XRandomGenerator_fillSecure(output, size));
}

static bool xcryptographic_aes_ctr_enabled(size_t keyLen)
{
    switch (keyLen) {
    case 16: return XCRYPTOGRAPHIC_AES128_CTR_ON != 0;
    case 24: return XCRYPTOGRAPHIC_AES192_CTR_ON != 0;
    case 32: return XCRYPTOGRAPHIC_AES256_CTR_ON != 0;
    default: return false;
    }
}

static bool xcryptographic_aes_ctr_import_key(const void* key, size_t keyLen,
                                                   XCryptographic_Key* result)
{
    if (!key || !result || !xcryptographic_aes_ctr_enabled(keyLen)) return false;
    memset(result, 0, sizeof(*result));
    result->type = XCryptographic_KeyType_AesCtr;
    memcpy(result->symmetricKey, key, keyLen);
    result->symmetricKeyLen = keyLen;
    return true;
}

static bool xcryptographic_aes_ctr_setup(XCryptographic_CipherOperation* operation,
                                              XCryptographic_Key key, bool encrypt,
                                              const void* iv, size_t ivLen)
{
    if (!operation || key.type != XCryptographic_KeyType_AesCtr || !iv || ivLen != 16)
        return false;
    memset(operation, 0, sizeof(*operation));
    xcryptographic_aes_expand(key.symmetricKey, key.symmetricKeyLen,
                                   operation->roundKeys, &operation->rounds);
    memcpy(operation->counter, iv, 16);
    operation->streamOffset = 16;
    operation->active = true;
    (void)encrypt;
    return true;
}

static bool xcryptographic_aes_ctr_update(XCryptographic_CipherOperation* operation,
                                               const void* input, size_t inputLen,
                                               void* output, size_t outputCap,
                                               size_t* outputLen)
{
    size_t i;
    uint8_t* out = (uint8_t*)output;
    const uint8_t* in = (const uint8_t*)input;
    if (!operation || !operation->active || (!input && inputLen) ||
        !output || !outputLen || outputCap < inputLen) return false;
    for (i = 0; i < inputLen; ++i) {
        if (operation->streamOffset == 16) {
            xcryptographic_aes_block(operation->roundKeys, operation->rounds,
                                          operation->counter, operation->stream);
            xcryptographic_increment_counter(operation->counter);
            operation->streamOffset = 0;
        }
        out[i] = in[i] ^ operation->stream[operation->streamOffset++];
    }
    *outputLen = inputLen;
    return true;
}

void XCryptographic_aesCtrAbort(XCryptographic_CipherOperation* operation)
{
    if (operation) memset(operation, 0, sizeof(*operation));
}

static bool xcryptographic_aes_block_enabled(size_t keyLen,
                                             XCryptographic_BlockCipherMode mode)
{
    if (mode == XCryptographic_BlockCipherMode_EcbNoPadding) {
        switch (keyLen) {
        case 16: return XCRYPTOGRAPHIC_AES128_ECB_ON != 0;
        case 24: return XCRYPTOGRAPHIC_AES192_ECB_ON != 0;
        case 32: return XCRYPTOGRAPHIC_AES256_ECB_ON != 0;
        default: return false;
        }
    }
    if (mode == XCryptographic_BlockCipherMode_CbcNoPadding) {
        switch (keyLen) {
        case 16: return XCRYPTOGRAPHIC_AES128_CBC_ON != 0;
        case 24: return XCRYPTOGRAPHIC_AES192_CBC_ON != 0;
        case 32: return XCRYPTOGRAPHIC_AES256_CBC_ON != 0;
        default: return false;
        }
    }
    if (mode == XCryptographic_BlockCipherMode_CbcPkcs7) {
        switch (keyLen) {
        case 16: return XCRYPTOGRAPHIC_AES128_CBC_PKCS7_ON != 0;
        case 24: return XCRYPTOGRAPHIC_AES192_CBC_PKCS7_ON != 0;
        case 32: return XCRYPTOGRAPHIC_AES256_CBC_PKCS7_ON != 0;
        default: return false;
        }
    }
    if (mode == XCryptographic_BlockCipherMode_Cfb) {
        switch (keyLen) {
        case 16: return XCRYPTOGRAPHIC_AES128_CFB_ON != 0;
        case 24: return XCRYPTOGRAPHIC_AES192_CFB_ON != 0;
        case 32: return XCRYPTOGRAPHIC_AES256_CFB_ON != 0;
        default: return false;
        }
    }
    if (mode == XCryptographic_BlockCipherMode_Ofb) {
        switch (keyLen) {
        case 16: return XCRYPTOGRAPHIC_AES128_OFB_ON != 0;
        case 24: return XCRYPTOGRAPHIC_AES192_OFB_ON != 0;
        case 32: return XCRYPTOGRAPHIC_AES256_OFB_ON != 0;
        default: return false;
        }
    }
    if (mode == XCryptographic_BlockCipherMode_Xts) {
        if (keyLen == 32) return XCRYPTOGRAPHIC_AES128_XTS_ON != 0;
        if (keyLen == 64) return XCRYPTOGRAPHIC_AES256_XTS_ON != 0;
        return false;
    }
    return false;
}

static bool xcryptographic_block_enabled(size_t keyLen, XCryptographic_BlockCipherMode mode,
                                    XCryptographic_BlockCipherAlgorithm algorithm)
{
    if (mode == XCryptographic_BlockCipherMode_Xts &&
        algorithm != XCryptographic_BlockCipherAlgorithm_Aes)
        return false;
    if (algorithm == XCryptographic_BlockCipherAlgorithm_Aria)
        return xcryptographic_aria_enabled(keyLen);
    if (algorithm == XCryptographic_BlockCipherAlgorithm_Camellia)
        return xcryptographic_camellia_enabled(keyLen);
    return xcryptographic_aes_block_enabled(keyLen, mode);
}

static void xcryptographic_xts_mul_x(uint8_t value[16])
{
    uint8_t carry = (uint8_t)(value[15] >> 7);
    size_t index;
    for (index = 15; index > 0; --index)
        value[index] = (uint8_t)((value[index] << 1) | (value[index - 1] >> 7));
    value[0] = (uint8_t)(value[0] << 1);
    if (carry) value[0] ^= 0x87u;
}

static void xcryptographic_xts_crypt_block(
    XCryptographic_BlockCipherOperation* operation, const uint8_t tweak[16],
    const uint8_t input[16], uint8_t output[16])
{
    size_t index;
    for (index = 0; index < 16; ++index) output[index] = input[index] ^ tweak[index];
    if (operation->encrypt)
        xcryptographic_aes_block(operation->roundKeys, operation->rounds, output, output);
    else
        xcryptographic_aes_inv_block(operation->roundKeys, operation->rounds, output, output);
    for (index = 0; index < 16; ++index) output[index] ^= tweak[index];
}

bool XCryptographic_blockCipherSetup(
    XCryptographic_BlockCipherOperation* operation,
    XCryptographic_BlockCipherAlgorithm algorithm, XByteArrayView key,
    XCryptographic_BlockCipherMode mode, bool encrypt)
{
    if (!operation || key.m_size < 0 || (!key.m_data && key.m_size != 0) ||
        !xcryptographic_block_enabled((size_t)key.m_size, mode, algorithm)) {
        return false;
    }
    memset(operation, 0, sizeof(*operation));
    operation->algorithm = algorithm;
    if (algorithm == XCryptographic_BlockCipherAlgorithm_Aria) {
        xcryptographic_aria_setkey_enc(operation->keySchedule,
                                       &operation->rounds,
                                       key.m_data, (size_t)key.m_size);
        if (!encrypt) {
            xcryptographic_aria_setkey_dec(operation->decryptKeySchedule,
                                           &operation->rounds,
                                           key.m_data, (size_t)key.m_size);
        }
    } else if (algorithm == XCryptographic_BlockCipherAlgorithm_Camellia) {
        xcryptographic_camellia_setkey_enc(operation->keySchedule,
                                           &operation->rounds,
                                           key.m_data, (size_t)key.m_size);
        if (!encrypt) {
            xcryptographic_camellia_setkey_dec(operation->decryptKeySchedule,
                                               &operation->rounds,
                                               key.m_data, (size_t)key.m_size);
        }
    } else if (mode == XCryptographic_BlockCipherMode_Xts) {
        size_t half = (size_t)key.m_size / 2u;
        xcryptographic_aes_expand(key.m_data, half,
                                  operation->roundKeys, &operation->rounds);
        xcryptographic_aes_expand(key.m_data + half, half,
                                  operation->xtsRoundKeys, &operation->xtsRounds);
    } else {
        xcryptographic_aes_expand(key.m_data, (size_t)key.m_size,
                                  operation->roundKeys, &operation->rounds);
    }
    operation->mode = mode;
    operation->encrypt = encrypt;
    operation->active = mode == XCryptographic_BlockCipherMode_EcbNoPadding;
    return true;
}

bool XCryptographic_blockCipherSetIv(
    XCryptographic_BlockCipherOperation* operation, XByteArrayView iv)
{
    if (!operation || operation->mode == XCryptographic_BlockCipherMode_EcbNoPadding ||
        iv.m_size != 16 || !iv.m_data) {
        return false;
    }
    memcpy(operation->iv, iv.m_data, sizeof(operation->iv));
    if (operation->mode == XCryptographic_BlockCipherMode_Xts) {
        xcryptographic_aes_block(operation->xtsRoundKeys, operation->xtsRounds,
                                 operation->iv, operation->tweak);
    }
    operation->streamOffset = 16;
    operation->active = true;
    return true;
}

static void xcryptographic_block_cipher_crypt(
    XCryptographic_BlockCipherOperation* operation, const uint8_t input[16],
    uint8_t output[16])
{
    switch (operation->algorithm) {
    case XCryptographic_BlockCipherAlgorithm_Aes:
        xcryptographic_aes_block(operation->roundKeys, operation->rounds,
                                 input, output);
        break;
    case XCryptographic_BlockCipherAlgorithm_Aria:
        xcryptographic_aria_crypt(operation->keySchedule, operation->rounds,
                                  input, output);
        break;
    case XCryptographic_BlockCipherAlgorithm_Camellia:
        xcryptographic_camellia_crypt(operation->keySchedule, operation->rounds,
                                      input, output);
        break;
    default:
        memset(output, 0, 16);
        break;
    }
}

static void xcryptographic_block_cipher_inv_crypt(
    XCryptographic_BlockCipherOperation* operation, const uint8_t input[16],
    uint8_t output[16])
{
    switch (operation->algorithm) {
    case XCryptographic_BlockCipherAlgorithm_Aes:
        xcryptographic_aes_inv_block(operation->roundKeys, operation->rounds,
                                     input, output);
        break;
    case XCryptographic_BlockCipherAlgorithm_Aria:
        xcryptographic_aria_crypt(operation->decryptKeySchedule, operation->rounds,
                                  input, output);
        break;
    case XCryptographic_BlockCipherAlgorithm_Camellia:
        xcryptographic_camellia_crypt(operation->decryptKeySchedule, operation->rounds,
                                      input, output);
        break;
    default:
        memset(output, 0, 16);
        break;
    }
}

static void xcryptographic_block_cipher_process(
    XCryptographic_BlockCipherOperation* operation, const uint8_t input[16],
    uint8_t output[16])
{
    uint8_t block[16];
    size_t index;

    if (operation->mode == XCryptographic_BlockCipherMode_Xts) {
        xcryptographic_xts_crypt_block(operation, operation->tweak, input, output);
        xcryptographic_xts_mul_x(operation->tweak);
        memset(block, 0, sizeof(block));
        return;
    }
    if (operation->encrypt) {
        memcpy(block, input, sizeof(block));
        if (operation->mode == XCryptographic_BlockCipherMode_CbcNoPadding ||
            operation->mode == XCryptographic_BlockCipherMode_CbcPkcs7) {
            for (index = 0; index < sizeof(block); ++index) block[index] ^= operation->iv[index];
        }
        xcryptographic_block_cipher_crypt(operation, block, output);
        if (operation->mode == XCryptographic_BlockCipherMode_CbcNoPadding ||
            operation->mode == XCryptographic_BlockCipherMode_CbcPkcs7)
            memcpy(operation->iv, output, sizeof(operation->iv));
    } else {
        xcryptographic_block_cipher_inv_crypt(operation, input, block);
        if (operation->mode == XCryptographic_BlockCipherMode_CbcNoPadding ||
            operation->mode == XCryptographic_BlockCipherMode_CbcPkcs7) {
            for (index = 0; index < sizeof(block); ++index) output[index] = block[index] ^ operation->iv[index];
            memcpy(operation->iv, input, sizeof(operation->iv));
        } else {
            memcpy(output, block, sizeof(block));
        }
    }
    memset(block, 0, sizeof(block));
}

XByteArrayView XCryptographic_blockCipherUpdateInto(
    XCryptographic_BlockCipherOperation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = { (const uint8_t*)buffer, 0 };
    size_t consumed = 0;
    size_t produced = 0;

    if (!operation || !operation->active || data.m_size < 0 ||
        (!data.m_data && data.m_size != 0) || (!buffer && bufferSize != 0)) {
        return empty;
    }
    if ((operation->mode == XCryptographic_BlockCipherMode_Cfb ||
         operation->mode == XCryptographic_BlockCipherMode_Ofb)) {
        size_t index;
        if (bufferSize < (size_t)data.m_size) return empty;
        for (index = 0; index < (size_t)data.m_size; ++index) {
            if (operation->streamOffset == 16) {
                xcryptographic_block_cipher_crypt(operation, operation->iv,
                                                  operation->stream);
                if (operation->mode == XCryptographic_BlockCipherMode_Ofb) {
                    memcpy(operation->iv, operation->stream, sizeof(operation->iv));
                }
                operation->streamOffset = 0;
            }
            ((uint8_t*)buffer)[index] = data.m_data[index] ^ operation->stream[operation->streamOffset];
            if (operation->mode == XCryptographic_BlockCipherMode_Cfb) {
                operation->iv[operation->streamOffset] = operation->encrypt ?
                    ((uint8_t*)buffer)[index] : data.m_data[index];
            }
            ++operation->streamOffset;
        }
        result.m_size = data.m_size;
        if (result.m_size == 0 && !result.m_data) {
            result.m_data = (const uint8_t*)"";
        }
        return result;
    }
    if (operation->mode == XCryptographic_BlockCipherMode_Xts) {
        size_t total = operation->pendingSize + (size_t)data.m_size;
        size_t remainder = total % 16u;
        size_t keep = 16u + (remainder != 0 ? remainder : 0u);
        size_t produce = total > keep ? ((total - keep) / 16u) * 16u : 0u;
        size_t pendingOffset = 0;
        size_t inputOffset = 0;
        size_t produced = 0;
        uint8_t tail[32];
        if (bufferSize < produce) return empty;
        while (produced < produce) {
            uint8_t block[16];
            size_t takePending = operation->pendingSize - pendingOffset;
            size_t need = sizeof(block);
            if (takePending > need) takePending = need;
            if (takePending != 0) {
                memcpy(block, operation->pending + pendingOffset, takePending);
                pendingOffset += takePending;
                need -= takePending;
            }
            if (need != 0) {
                memcpy(block + sizeof(block) - need, data.m_data + inputOffset, need);
                inputOffset += need;
            }
            xcryptographic_block_cipher_process(operation, block,
                                                (uint8_t*)buffer + produced);
            produced += sizeof(block);
            memset(block, 0, sizeof(block));
        }
        {
            size_t tailSize = total - produce;
            size_t tailOffset = 0;
            while (tailOffset < tailSize && pendingOffset < operation->pendingSize)
                tail[tailOffset++] = operation->pending[pendingOffset++];
            if (tailOffset < tailSize) {
                size_t count = tailSize - tailOffset;
                memcpy(tail + tailOffset, data.m_data + inputOffset, count);
            }
            memcpy(operation->pending, tail, tailSize);
            operation->pendingSize = tailSize;
            memset(tail, 0, sizeof(tail));
        }
        result.m_size = (int64_t)produced;
        return result;
    }
    if (bufferSize < ((operation->pendingSize + (size_t)data.m_size) / 16u) * 16u) {
        return empty;
    }
    if (operation->mode == XCryptographic_BlockCipherMode_CbcPkcs7 &&
        !operation->encrypt) {
        size_t dataLen = (size_t)data.m_size;
        size_t remaining;

        /* 解密时保留最后一个整块待 Finish 去填充；
         * 若上一轮已暂存一个整块而本轮又有新数据，则该暂存块不再是末块，先处理它。 */
        if (operation->pendingSize == 16 && dataLen > 0) {
            xcryptographic_block_cipher_process(
                operation, operation->pending, (uint8_t*)buffer + produced);
            operation->pendingSize = 0;
            produced += 16;
        }
        while (operation->pendingSize < 16 && consumed < dataLen) {
            size_t copySize = 16 - operation->pendingSize;
            if (copySize > dataLen - consumed) copySize = dataLen - consumed;
            memcpy(operation->pending + operation->pendingSize,
                   data.m_data + consumed, copySize);
            operation->pendingSize += copySize;
            consumed += copySize;
            if (operation->pendingSize == 16) {
                if (consumed == dataLen) break; /* 这一整块留作可能的末块 */
                xcryptographic_block_cipher_process(
                    operation, operation->pending, (uint8_t*)buffer + produced);
                operation->pendingSize = 0;
                produced += 16;
            }
        }
        remaining = dataLen - consumed;
        while (remaining > 16) {
            xcryptographic_block_cipher_process(
                operation, data.m_data + consumed, (uint8_t*)buffer + produced);
            consumed += 16;
            remaining -= 16;
            produced += 16;
        }
        if (remaining > 0) {
            memcpy(operation->pending, data.m_data + consumed, remaining);
            operation->pendingSize = remaining;
            consumed += remaining;
        }
        result.m_size = (int64_t)produced;
        return result;
    }
    while (consumed < (size_t)data.m_size) {
        size_t copySize = 16 - operation->pendingSize;
        if (copySize > (size_t)data.m_size - consumed) copySize = (size_t)data.m_size - consumed;
        memcpy(operation->pending + operation->pendingSize, data.m_data + consumed, copySize);
        operation->pendingSize += copySize;
        consumed += copySize;
        if (operation->pendingSize == 16) {
            xcryptographic_block_cipher_process(operation, operation->pending,
                                                    (uint8_t*)buffer + produced);
            operation->pendingSize = 0;
            produced += 16;
        }
    }
    result.m_size = (int64_t)produced;
    return result;
}

XByteArrayView XCryptographic_blockCipherFinishInto(
    XCryptographic_BlockCipherOperation* operation, char* buffer,
    size_t bufferSize)
{
    XByteArrayView empty = { NULL, 0 };
    static const uint8_t completed = 0;
    XByteArrayView result = { &completed, 0 };
    (void)bufferSize;
    (void)buffer;

    if (!operation || !operation->active) return empty;

    if (operation->mode == XCryptographic_BlockCipherMode_Xts) {
        uint8_t block[16];
        uint8_t nextTweak[16];
        uint8_t currentTweak[16];
        size_t partial = operation->pendingSize % 16u;
        if (operation->pendingSize < 16 || operation->pendingSize > 31 ||
            bufferSize < operation->pendingSize) return empty;
        if (partial == 0) {
            xcryptographic_block_cipher_process(operation, operation->pending,
                                                (uint8_t*)buffer);
        } else {
            size_t index;
            uint8_t* output = (uint8_t*)buffer;
            memcpy(currentTweak, operation->tweak, sizeof(currentTweak));
            memcpy(nextTweak, currentTweak, sizeof(nextTweak));
            xcryptographic_xts_mul_x(nextTweak);
            if (operation->encrypt) {
                uint8_t cc[16];
                xcryptographic_xts_crypt_block(operation, currentTweak,
                                               operation->pending, cc);
                memcpy(output + 16, cc, partial);
                memcpy(block, operation->pending + 16, partial);
                memcpy(block + partial, cc + partial, 16 - partial);
                xcryptographic_xts_crypt_block(operation, nextTweak, block, output);
                memset(cc, 0, sizeof(cc));
            } else {
                uint8_t pp[16];
                xcryptographic_xts_crypt_block(operation, nextTweak,
                                               operation->pending, pp);
                memcpy(block, operation->pending + 16, partial);
                memcpy(block + partial, pp + partial, 16 - partial);
                xcryptographic_xts_crypt_block(operation, currentTweak, block, output);
                memcpy(output + 16, pp, partial);
                memset(pp, 0, sizeof(pp));
            }
            memset(block, 0, sizeof(block));
        }
        XCryptographic_blockCipherAbort(operation);
        return XByteArrayView_create_data((const uint8_t*)buffer,
                                          (int64_t)(partial == 0 ? 16 : 16 + partial));
    }
    if (operation->mode == XCryptographic_BlockCipherMode_CbcPkcs7) {
        uint8_t block[16];
        size_t padSize;
        uint8_t padValue;
        size_t index;

        if (operation->pendingSize > 16) return empty;
        if (operation->encrypt) {
            /* 加密：PKCS7 总是补充 1..16 字节填充，即使输入已经是整块。 */
            padSize = 16 - operation->pendingSize;
            if (padSize == 0) padSize = 16;
            padValue = (uint8_t)padSize;
            memcpy(block, operation->pending, operation->pendingSize);
            for (index = operation->pendingSize; index < 16; ++index)
                block[index] = padValue;
            if (bufferSize < 16) return empty;
            xcryptographic_block_cipher_process(operation, block,
                                                    (uint8_t*)buffer);
            result = XByteArrayView_create_data((const uint8_t*)buffer, 16);
        } else {
            /* 解密：密文必须是整块，最后一整块包含 PKCS7 填充。 */
            if (operation->pendingSize != 16 || bufferSize < 16) return empty;
            {
                uint8_t lastBlock[16];
                xcryptographic_block_cipher_process(
                    operation, operation->pending, lastBlock);
                padValue = lastBlock[15];
                if (padValue == 0 || padValue > 16) return empty;
                for (index = 16 - padValue; index < 16; ++index)
                    if (lastBlock[index] != padValue) return empty;
                memcpy(buffer, lastBlock, 16 - padValue);
                result = XByteArrayView_create_data((const uint8_t*)buffer,
                                                    16 - (size_t)padValue);
            }
        }
        XCryptographic_blockCipherAbort(operation);
        return result;
    }

    if ((operation->mode == XCryptographic_BlockCipherMode_EcbNoPadding ||
         operation->mode == XCryptographic_BlockCipherMode_CbcNoPadding) &&
        operation->pendingSize != 0) return empty;
    XCryptographic_blockCipherAbort(operation);
    return result;
}

void XCryptographic_blockCipherAbort(
    XCryptographic_BlockCipherOperation* operation)
{
    if (operation) memset(operation, 0, sizeof(*operation));
}

static void xcryptographic_key_wrap_xor_t(uint8_t a[8], uint64_t t)
{
    size_t index;
    for (index = 0; index < 8; ++index) {
        a[index] ^= (uint8_t)(t >> (56u - index * 8u));
    }
}

static bool xcryptographic_key_wrap_setup(
    const XByteArrayView kek, uint8_t roundKeys[240], uint8_t *rounds)
{
    if (!XCRYPTOGRAPHIC_AES_KEY_WRAP_ON || !kek.m_data || kek.m_size < 0 ||
        (kek.m_size != 16 && kek.m_size != 24 && kek.m_size != 32)) {
        return false;
    }
    xcryptographic_aes_expand(kek.m_data, (size_t)kek.m_size, roundKeys, rounds);
    return true;
}

bool XCryptographic_aesKeyWrapInto(
    XCryptographic_KeyWrapMode mode, XByteArrayView kek,
    XByteArrayView input, char* buffer, size_t bufferSize,
    size_t* outputSize)
{
    static const uint8_t kwIcv[8] = { 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6 };
    static const uint8_t kwpIcvPrefix[4] = { 0xa6, 0x59, 0x59, 0xa6 };
    uint8_t roundKeys[240] = { 0 };
    uint8_t rounds = 0;
    uint8_t block[16];
    uint8_t encrypted[16];
    uint8_t a[8];
    uint8_t *r;
    size_t inputSize;
    size_t paddedSize = 0;
    size_t semiblocks;
    size_t i;
    size_t j;
    uint64_t t;
    bool result = false;

    if (!outputSize) return false;
    *outputSize = 0;
    if (!xcryptographic_key_wrap_setup(kek, roundKeys, &rounds)) goto cleanup;
    if (!buffer || input.m_size < 0 || (!input.m_data && input.m_size != 0)) goto cleanup;
    inputSize = (size_t)input.m_size;
    if (mode == XCryptographic_KeyWrapMode_Kw) {
        if (inputSize < 16 || (inputSize & 7u) != 0 || inputSize > SIZE_MAX - 8u) goto cleanup;
        paddedSize = inputSize;
        memcpy(a, kwIcv, sizeof(a));
    } else if (mode == XCryptographic_KeyWrapMode_Kwp) {
        if (inputSize == 0 || inputSize > UINT32_MAX || inputSize > SIZE_MAX - 15u) goto cleanup;
        paddedSize = (inputSize + 7u) & ~(size_t)7u;
        memcpy(a, kwpIcvPrefix, 4);
        a[4] = (uint8_t)(inputSize >> 24);
        a[5] = (uint8_t)(inputSize >> 16);
        a[6] = (uint8_t)(inputSize >> 8);
        a[7] = (uint8_t)inputSize;
    } else {
        goto cleanup;
    }
    if (bufferSize < paddedSize + 8u) goto cleanup;
    memmove((uint8_t *)buffer + 8, input.m_data, inputSize);
    if (paddedSize > inputSize) memset((uint8_t *)buffer + 8 + inputSize, 0, paddedSize - inputSize);
    r = (uint8_t *)buffer + 8;
    semiblocks = paddedSize / 8u;

    if (mode == XCryptographic_KeyWrapMode_Kwp && semiblocks == 1) {
        memcpy(block, a, 8);
        memcpy(block + 8, r, 8);
        xcryptographic_aes_block(roundKeys, rounds, block, encrypted);
        memcpy(buffer, encrypted, 16);
        *outputSize = 16;
        result = true;
        goto cleanup;
    }
    for (j = 0; j < 6; ++j) {
        for (i = 1; i <= semiblocks; ++i) {
            memcpy(block, a, 8);
            memcpy(block + 8, r + (i - 1u) * 8u, 8);
            xcryptographic_aes_block(roundKeys, rounds, block, encrypted);
            memcpy(a, encrypted, 8);
            t = (uint64_t)(semiblocks * j + i);
            xcryptographic_key_wrap_xor_t(a, t);
            memcpy(r + (i - 1u) * 8u, encrypted + 8, 8);
        }
    }
    memcpy(buffer, a, 8);
    *outputSize = paddedSize + 8u;
    result = true;

cleanup:
    if (!result && buffer && paddedSize <= bufferSize && paddedSize <= SIZE_MAX - 8u)
        memset(buffer, 0, paddedSize + 8u);
    memset(roundKeys, 0, sizeof(roundKeys));
    return result;
}

bool XCryptographic_aesKeyUnwrapInto(
    XCryptographic_KeyWrapMode mode, XByteArrayView kek,
    XByteArrayView input, char* buffer, size_t bufferSize,
    size_t* outputSize)
{
    static const uint8_t kwIcv[8] = { 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6 };
    static const uint8_t kwpIcvPrefix[4] = { 0xa6, 0x59, 0x59, 0xa6 };
    uint8_t roundKeys[240] = { 0 };
    uint8_t rounds = 0;
    uint8_t block[16];
    uint8_t decrypted[16];
    uint8_t a[8];
    uint8_t *r;
    size_t inputSize;
    size_t semiblocks;
    size_t outputLength = 0;
    size_t i;
    size_t j;
    uint64_t t;
    uint8_t diff = 0;
    uint32_t messageLength;
    size_t padLength;
    bool result = false;

    if (!outputSize) return false;
    *outputSize = 0;
    if (!xcryptographic_key_wrap_setup(kek, roundKeys, &rounds)) goto failure;
    if (!buffer || input.m_size < 0 || !input.m_data) goto failure;
    inputSize = (size_t)input.m_size;
    if ((inputSize & 7u) != 0 || inputSize < 16) goto failure;
    semiblocks = inputSize / 8u - 1u;
    if (mode == XCryptographic_KeyWrapMode_Kw) {
        if (semiblocks < 2) goto failure;
        outputLength = semiblocks * 8u;
    } else if (mode == XCryptographic_KeyWrapMode_Kwp) {
        if (semiblocks < 1) goto failure;
        outputLength = semiblocks * 8u;
    } else {
        goto failure;
    }
    if (bufferSize < outputLength) goto failure;
    if (mode == XCryptographic_KeyWrapMode_Kwp && semiblocks == 1) {
        memcpy(block, input.m_data, 16);
        xcryptographic_aes_inv_block(roundKeys, rounds, block, decrypted);
        memcpy(a, decrypted, 8);
        memcpy(buffer, decrypted + 8, 8);
    } else {
        memcpy(a, input.m_data, 8);
        memmove(buffer, input.m_data + 8, outputLength);
        r = (uint8_t *)buffer + outputLength - 8u;
        for (j = 6; j-- > 0;) {
            for (i = semiblocks; i > 0; --i) {
                t = (uint64_t)(semiblocks * j + i);
                memcpy(block, a, 8);
                xcryptographic_key_wrap_xor_t(block, t);
                memcpy(block + 8, r, 8);
                xcryptographic_aes_inv_block(roundKeys, rounds, block, decrypted);
                memcpy(a, decrypted, 8);
                memcpy(r, decrypted + 8, 8);
                r = (r == (uint8_t *)buffer) ?
                    (uint8_t *)buffer + outputLength - 8u : r - 8u;
            }
        }
    }
    if (mode == XCryptographic_KeyWrapMode_Kw) {
        for (i = 0; i < sizeof(a); ++i) diff |= (uint8_t)(a[i] ^ kwIcv[i]);
        if (diff != 0) goto failure;
        *outputSize = outputLength;
    } else {
        for (i = 0; i < 4; ++i) diff |= (uint8_t)(a[i] ^ kwpIcvPrefix[i]);
        messageLength = ((uint32_t)a[4] << 24) | ((uint32_t)a[5] << 16) |
                        ((uint32_t)a[6] << 8) | a[7];
        if (messageLength == 0 || messageLength > outputLength) goto failure;
        padLength = outputLength - messageLength;
        if (padLength > 7) goto failure;
        for (i = messageLength; i < outputLength; ++i) diff |= ((uint8_t *)buffer)[i];
        if (diff != 0) goto failure;
        *outputSize = messageLength;
    }
    result = true;
failure:
    if (!result && buffer && outputLength != 0) memset(buffer, 0, outputLength);
    memset(roundKeys, 0, sizeof(roundKeys));
    return result;
}

bool XCryptographic_chacha20Setup(XCryptographic_ChaCha20Operation* operation,
                                  XByteArrayView key, XByteArrayView nonce)
{
    if (!operation || !XCRYPTOGRAPHIC_CHACHA20_ON || key.m_size != 32 ||
        nonce.m_size != 12 || !key.m_data || !nonce.m_data) return false;
    memset(operation, 0, sizeof(*operation));
    memcpy(operation->key, key.m_data, sizeof(operation->key));
    memcpy(operation->nonce, nonce.m_data, sizeof(operation->nonce));
    operation->counter = 0;
    operation->streamOffset = 64;
    operation->active = true;
    return true;
}

XByteArrayView XCryptographic_chacha20UpdateInto(
    XCryptographic_ChaCha20Operation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = { (const uint8_t*)buffer, data.m_size };
    size_t index;
    if (!operation || !operation->active || data.m_size < 0 ||
        (!data.m_data && data.m_size != 0) || !buffer ||
        bufferSize < (size_t)data.m_size) return empty;
    for (index = 0; index < (size_t)data.m_size; ++index) {
        if (operation->streamOffset == sizeof(operation->stream)) {
            xcryptographic_chacha20_block(operation->key, operation->counter++,
                                          operation->nonce, operation->stream);
            operation->streamOffset = 0;
        }
        ((uint8_t*)buffer)[index] = data.m_data[index] ^ operation->stream[operation->streamOffset++];
    }
    if (result.m_size == 0 && !result.m_data) result.m_data = (const uint8_t*)"";
    return result;
}

void XCryptographic_chacha20Abort(XCryptographic_ChaCha20Operation* operation)
{
    if (operation) memset(operation, 0, sizeof(*operation));
}

static bool xcryptographic_aes_cmac_enabled(size_t keyLen)
{
    switch (keyLen) {
    case 16: return XCRYPTOGRAPHIC_AES128_CMAC_ON != 0;
    case 24: return XCRYPTOGRAPHIC_AES192_CMAC_ON != 0;
    case 32: return XCRYPTOGRAPHIC_AES256_CMAC_ON != 0;
    default: return false;
    }
}

static void xcryptographic_aes_cmac_double(const uint8_t input[16], uint8_t output[16])
{
    uint8_t carry = 0;
    size_t i;
    for (i = 16; i != 0; --i) {
        uint8_t value = input[i - 1];
        output[i - 1] = (uint8_t)((value << 1) | carry);
        carry = value >> 7;
    }
    if (carry != 0) output[15] ^= 0x87u;
}

XByteArrayView XCryptographic_aesCmacInto(
    XByteArrayView key, XByteArrayView message, char* buffer, size_t bufferSize)
{
    uint8_t roundKeys[240] = { 0 };
    uint8_t rounds = 0;
    uint8_t zero[16] = { 0 };
    uint8_t firstSubkey[16];
    uint8_t secondSubkey[16];
    uint8_t state[16] = { 0 };
    uint8_t last[16] = { 0 };
    size_t blockCount;
    size_t block;
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = { (const uint8_t*)buffer, 16 };

    if (key.m_size < 0 || message.m_size < 0 || (!key.m_data && key.m_size != 0) ||
        (!message.m_data && message.m_size != 0) || !buffer || bufferSize < 16 ||
        !xcryptographic_aes_cmac_enabled((size_t)key.m_size))
        return empty;
    xcryptographic_aes_expand(key.m_data, (size_t)key.m_size, roundKeys, &rounds);
    xcryptographic_aes_block(roundKeys, rounds, zero, firstSubkey);
    xcryptographic_aes_cmac_double(firstSubkey, firstSubkey);
    xcryptographic_aes_cmac_double(firstSubkey, secondSubkey);

    blockCount = message.m_size == 0 ? 1u : ((size_t)message.m_size + 15u) / 16u;
    for (block = 0; block + 1u < blockCount; ++block) {
        size_t i;
        for (i = 0; i < 16; ++i) state[i] ^= message.m_data[block * 16u + i];
        xcryptographic_aes_block(roundKeys, rounds, state, state);
    }
    {
        size_t finalSize = message.m_size == 0 ? 0 : (size_t)message.m_size - (blockCount - 1u) * 16u;
        size_t i;
        if (finalSize == 16) {
            memcpy(last, message.m_data + (blockCount - 1u) * 16u, 16);
            for (i = 0; i < 16; ++i) last[i] ^= firstSubkey[i];
        } else {
            if (finalSize != 0) memcpy(last, message.m_data + (blockCount - 1u) * 16u, finalSize);
            last[finalSize] = 0x80;
            for (i = 0; i < 16; ++i) last[i] ^= secondSubkey[i];
        }
        for (i = 0; i < 16; ++i) state[i] ^= last[i];
    }
    xcryptographic_aes_block(roundKeys, rounds, state, (uint8_t*)buffer);
    memset(roundKeys, 0, sizeof(roundKeys));
    memset(firstSubkey, 0, sizeof(firstSubkey));
    memset(secondSubkey, 0, sizeof(secondSubkey));
    memset(state, 0, sizeof(state));
    memset(last, 0, sizeof(last));
    return result;
}

XByteArray* XCryptographic_aesCmac(XByteArrayView key, XByteArrayView message)
{
    XByteArray* result = XByteArray_create();
    if (!result || !XByteArray_resize_base((XVector*)result, 16) ||
        !XCryptographic_aesCmacInto(key, message, (char*)XByteArray_data(result), 16).m_data) {
        if (result) XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

bool XCryptographic_aesCmacSetup(XCryptographic_CmacOperation* operation,
                                 XByteArrayView key)
{
    uint8_t zero[16] = { 0 };

    if (!operation || key.m_size < 0 || (!key.m_data && key.m_size != 0) ||
        !xcryptographic_aes_cmac_enabled((size_t)key.m_size)) {
        return false;
    }
    memset(operation, 0, sizeof(*operation));
    xcryptographic_aes_expand(key.m_data, (size_t)key.m_size,
                              operation->roundKeys, &operation->rounds);
    xcryptographic_aes_block(operation->roundKeys, operation->rounds,
                             zero, operation->firstSubkey);
    xcryptographic_aes_cmac_double(operation->firstSubkey, operation->firstSubkey);
    xcryptographic_aes_cmac_double(operation->firstSubkey, operation->secondSubkey);
    operation->active = true;
    return true;
}

bool XCryptographic_aesCmacUpdate(XCryptographic_CmacOperation* operation,
                                  XByteArrayView message)
{
    size_t index;

    if (!operation || !operation->active || message.m_size < 0 ||
        (!message.m_data && message.m_size != 0)) {
        return false;
    }
    for (index = 0; index < (size_t)message.m_size; ++index) {
        size_t byte;
        if (operation->pendingSize == sizeof(operation->pending)) {
            for (byte = 0; byte < sizeof(operation->state); ++byte) {
                operation->state[byte] ^= operation->pending[byte];
            }
            xcryptographic_aes_block(operation->roundKeys, operation->rounds,
                                     operation->state, operation->state);
            operation->pendingSize = 0;
        }
        operation->pending[operation->pendingSize++] = message.m_data[index];
    }
    return true;
}

XByteArrayView XCryptographic_aesCmacFinishInto(
    XCryptographic_CmacOperation* operation, char* buffer, size_t bufferSize)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = { (const uint8_t*)buffer, 16 };
    uint8_t last[16] = { 0 };
    size_t index;

    if (!operation || !operation->active || !buffer || bufferSize < 16) {
        return empty;
    }
    if (operation->pendingSize == sizeof(operation->pending)) {
        memcpy(last, operation->pending, sizeof(last));
        for (index = 0; index < sizeof(last); ++index) {
            last[index] ^= operation->firstSubkey[index];
        }
    } else {
        if (operation->pendingSize != 0) {
            memcpy(last, operation->pending, operation->pendingSize);
        }
        last[operation->pendingSize] = 0x80;
        for (index = 0; index < sizeof(last); ++index) {
            last[index] ^= operation->secondSubkey[index];
        }
    }
    for (index = 0; index < sizeof(last); ++index) {
        operation->state[index] ^= last[index];
    }
    xcryptographic_aes_block(operation->roundKeys, operation->rounds,
                             operation->state, (uint8_t*)buffer);
    memset(last, 0, sizeof(last));
    XCryptographic_aesCmacAbort(operation);
    return result;
}

void XCryptographic_aesCmacAbort(XCryptographic_CmacOperation* operation)
{
    if (operation) {
        memset(operation, 0, sizeof(*operation));
    }
}

/* ==================== AEAD 与 KDF 原语 ==================== */

static bool xcryptographic_aes_gcm_enabled(size_t keyLen)
{
    switch (keyLen) {
    case 16: return XCRYPTOGRAPHIC_AES128_GCM_ON != 0;
    case 24: return XCRYPTOGRAPHIC_AES192_GCM_ON != 0;
    case 32: return XCRYPTOGRAPHIC_AES256_GCM_ON != 0;
    default: return false;
    }
}

static bool xcryptographic_aes_ccm_enabled(size_t keyLen)
{
    switch (keyLen) {
    case 16: return XCRYPTOGRAPHIC_AES128_CCM_ON != 0;
    case 24: return XCRYPTOGRAPHIC_AES192_CCM_ON != 0;
    case 32: return XCRYPTOGRAPHIC_AES256_CCM_ON != 0;
    default: return false;
    }
}

static bool xcryptographic_aes_ccm_tag_size_valid(size_t tagSize)
{
    return tagSize >= 4 && tagSize <= 16 && (tagSize & 1u) == 0;
}

static void xcryptographic_increment_counter32(uint8_t counter[16])
{
    int i;
    for (i = 15; i >= 12; --i)
        if (++counter[i] != 0) break;
}

static void xcryptographic_gcm_multiply(uint8_t value[16], const uint8_t hashSubkey[16])
{
    uint8_t product[16] = { 0 };
    uint8_t factor[16];
    size_t bit;
    memcpy(factor, hashSubkey, sizeof(factor));
    for (bit = 0; bit < 128; ++bit) {
        size_t byteIndex = bit / 8;
        uint8_t bitMask = (uint8_t)(0x80u >> (bit & 7u));
        if ((value[byteIndex] & bitMask) != 0) {
            size_t i;
            for (i = 0; i < sizeof(product); ++i) product[i] ^= factor[i];
        }
        {
            uint8_t carry = (uint8_t)(factor[15] & 1u);
            int i;
            for (i = 15; i > 0; --i)
                factor[i] = (uint8_t)((factor[i] >> 1) | (factor[i - 1] << 7));
            factor[0] >>= 1;
            if (carry) factor[0] ^= 0xe1u;
        }
    }
    memcpy(value, product, sizeof(product));
    memset(product, 0, sizeof(product));
    memset(factor, 0, sizeof(factor));
}

static void xcryptographic_gcm_ghash_update(uint8_t state[16],
                                             const uint8_t hashSubkey[16],
                                             const uint8_t* data, size_t dataLen)
{
    while (dataLen >= 16) {
        size_t i;
        for (i = 0; i < 16; ++i) state[i] ^= data[i];
        xcryptographic_gcm_multiply(state, hashSubkey);
        data += 16;
        dataLen -= 16;
    }
    if (dataLen != 0) {
        uint8_t block[16] = { 0 };
        size_t i;
        memcpy(block, data, dataLen);
        for (i = 0; i < 16; ++i) state[i] ^= block[i];
        xcryptographic_gcm_multiply(state, hashSubkey);
        memset(block, 0, sizeof(block));
    }
}

static bool xcryptographic_length_to_bits(size_t length, uint64_t* result)
{
    if (!result || length > UINT64_MAX / 8u) return false;
    *result = (uint64_t)length * 8u;
    return true;
}

static bool xcryptographic_gcm_message_length_valid(size_t length)
{
    return (uint64_t)length <= ((UINT64_C(1) << 36) - 32u);
}

static void xcryptographic_store_u64_be(uint8_t output[8], uint64_t value)
{
    int i;
    for (i = 7; i >= 0; --i) {
        output[i] = (uint8_t)value;
        value >>= 8;
    }
}

static bool xcryptographic_aead_block_key_algorithm(
    XCryptographic_Key key, bool gcm,
    XCryptographic_BlockCipherAlgorithm* result)
{
    if (!result) return false;
    if (gcm) {
        switch (key.type) {
        case XCryptographic_KeyType_AesGcm:
            if (!xcryptographic_aes_gcm_enabled(key.symmetricKeyLen)) return false;
            *result = XCryptographic_BlockCipherAlgorithm_Aes;
            return true;
        case XCryptographic_KeyType_AriaGcm:
            if (!xcryptographic_aria_enabled(key.symmetricKeyLen)) return false;
            *result = XCryptographic_BlockCipherAlgorithm_Aria;
            return true;
        case XCryptographic_KeyType_CamelliaGcm:
            if (!xcryptographic_camellia_enabled(key.symmetricKeyLen)) return false;
            *result = XCryptographic_BlockCipherAlgorithm_Camellia;
            return true;
        default:
            return false;
        }
    }
    switch (key.type) {
    case XCryptographic_KeyType_AesCcm:
        if (!xcryptographic_aes_ccm_enabled(key.symmetricKeyLen)) return false;
        *result = XCryptographic_BlockCipherAlgorithm_Aes;
        return true;
    case XCryptographic_KeyType_AriaCcm:
        if (!xcryptographic_aria_enabled(key.symmetricKeyLen)) return false;
        *result = XCryptographic_BlockCipherAlgorithm_Aria;
        return true;
    case XCryptographic_KeyType_CamelliaCcm:
        if (!xcryptographic_camellia_enabled(key.symmetricKeyLen)) return false;
        *result = XCryptographic_BlockCipherAlgorithm_Camellia;
        return true;
    default:
        return false;
    }
}

static bool xcryptographic_aead_block_setup(
    XCryptographic_BlockCipherOperation* operation,
    XCryptographic_Key key, XCryptographic_BlockCipherAlgorithm algorithm)
{
    return XCryptographic_blockCipherSetup(
        operation, algorithm,
        XByteArrayView_create_data(key.symmetricKey, (int64_t)key.symmetricKeyLen),
        XCryptographic_BlockCipherMode_EcbNoPadding, true);
}

static void xcryptographic_aead_block_crypt(
    XCryptographic_BlockCipherOperation* operation,
    const uint8_t input[16], uint8_t output[16])
{
    xcryptographic_block_cipher_crypt(operation, input, output);
}

static bool xcryptographic_gcm_initial_counter(const uint8_t* nonce, size_t nonceLen,
                                                const uint8_t hashSubkey[16],
                                                uint8_t initialCounter[16])
{
    if (!nonce || nonceLen == 0) return false;
    if (nonceLen == 12) {
        memcpy(initialCounter, nonce, nonceLen);
        initialCounter[12] = 0;
        initialCounter[13] = 0;
        initialCounter[14] = 0;
        initialCounter[15] = 1;
        return true;
    }
    {
        uint8_t lengthBlock[16] = { 0 };
        uint64_t nonceBits;
        if (!xcryptographic_length_to_bits(nonceLen, &nonceBits)) return false;
        memset(initialCounter, 0, 16);
        xcryptographic_gcm_ghash_update(initialCounter, hashSubkey, nonce, nonceLen);
        xcryptographic_store_u64_be(lengthBlock + 8, nonceBits);
        xcryptographic_gcm_ghash_update(initialCounter, hashSubkey, lengthBlock,
                                        sizeof(lengthBlock));
        memset(lengthBlock, 0, sizeof(lengthBlock));
    }
    return true;
}

static bool xcryptographic_aes_gcm_crypt(XCryptographic_Key key,
                                          const uint8_t* nonce, size_t nonceLen,
                                          const uint8_t* associatedData, size_t associatedDataLen,
                                          const uint8_t* input, size_t inputLen,
                                          uint8_t* output, size_t outputCap,
                                          uint8_t* tag, size_t tagSize)
{
    XCryptographic_BlockCipherOperation blockOperation;
    XCryptographic_BlockCipherAlgorithm algorithm;
    uint8_t zero[16] = { 0 };
    uint8_t hashSubkey[16];
    uint8_t initialCounter[16];
    uint8_t counter[16];
    uint8_t auth[16] = { 0 };
    uint8_t lengthBlock[16] = { 0 };
    uint8_t encryptedInitialCounter[16];
    uint64_t associatedDataBits;
    uint64_t inputBits;
    size_t offset = 0;
    bool ok = false;

    memset(&blockOperation, 0, sizeof(blockOperation));
    if (!xcryptographic_aead_block_key_algorithm(key, true, &algorithm) ||
        !nonce || nonceLen == 0 || (!associatedData && associatedDataLen != 0) ||
        (!input && inputLen != 0) || !output || outputCap < inputLen ||
        !tag || tagSize < 4 || tagSize > 16 ||
        !xcryptographic_gcm_message_length_valid(inputLen) ||
        !xcryptographic_length_to_bits(associatedDataLen, &associatedDataBits) ||
        !xcryptographic_length_to_bits(inputLen, &inputBits))
        goto cleanup;

    if (!xcryptographic_aead_block_setup(&blockOperation, key, algorithm))
        goto cleanup;
    xcryptographic_aead_block_crypt(&blockOperation, zero, hashSubkey);
    if (!xcryptographic_gcm_initial_counter(nonce, nonceLen, hashSubkey, initialCounter))
        goto cleanup;
    memcpy(counter, initialCounter, sizeof(counter));
    xcryptographic_increment_counter32(counter);

    while (offset < inputLen) {
        uint8_t stream[16];
        size_t i;
        size_t blockSize = inputLen - offset;
        if (blockSize > sizeof(stream)) blockSize = sizeof(stream);
        xcryptographic_aead_block_crypt(&blockOperation, counter, stream);
        for (i = 0; i < blockSize; ++i) output[offset + i] = input[offset + i] ^ stream[i];
        memset(stream, 0, sizeof(stream));
        xcryptographic_increment_counter32(counter);
        offset += blockSize;
    }

    xcryptographic_gcm_ghash_update(auth, hashSubkey, associatedData, associatedDataLen);
    xcryptographic_gcm_ghash_update(auth, hashSubkey, output, inputLen);
    xcryptographic_store_u64_be(lengthBlock, associatedDataBits);
    xcryptographic_store_u64_be(lengthBlock + 8, inputBits);
    xcryptographic_gcm_ghash_update(auth, hashSubkey, lengthBlock, sizeof(lengthBlock));
    xcryptographic_aead_block_crypt(&blockOperation, initialCounter, encryptedInitialCounter);
    for (offset = 0; offset < tagSize; ++offset) tag[offset] = auth[offset] ^ encryptedInitialCounter[offset];
    ok = true;

cleanup:
    XCryptographic_blockCipherAbort(&blockOperation);
    memset(hashSubkey, 0, sizeof(hashSubkey));
    memset(initialCounter, 0, sizeof(initialCounter));
    memset(counter, 0, sizeof(counter));
    memset(auth, 0, sizeof(auth));
    memset(lengthBlock, 0, sizeof(lengthBlock));
    memset(encryptedInitialCounter, 0, sizeof(encryptedInitialCounter));
    return ok;
}

static bool xcryptographic_aes_gcm_tag(XCryptographic_Key key,
                                       const uint8_t* nonce, size_t nonceLen,
                                       const uint8_t* associatedData, size_t associatedDataLen,
                                       const uint8_t* cipherText, size_t cipherTextLen,
                                       uint8_t tag[16])
{
    XCryptographic_BlockCipherOperation blockOperation;
    XCryptographic_BlockCipherAlgorithm algorithm;
    uint8_t zero[16] = { 0 };
    uint8_t hashSubkey[16];
    uint8_t initialCounter[16];
    uint8_t auth[16] = { 0 };
    uint8_t lengthBlock[16] = { 0 };
    uint8_t encryptedInitialCounter[16];
    uint64_t associatedDataBits;
    uint64_t cipherTextBits;
    size_t i;
    bool result = false;

    memset(&blockOperation, 0, sizeof(blockOperation));
    if (!xcryptographic_aead_block_key_algorithm(key, true, &algorithm) ||
        !nonce || nonceLen == 0 ||
        (!associatedData && associatedDataLen != 0) ||
        (!cipherText && cipherTextLen != 0) || !tag ||
        !xcryptographic_gcm_message_length_valid(cipherTextLen) ||
        !xcryptographic_length_to_bits(associatedDataLen, &associatedDataBits) ||
        !xcryptographic_length_to_bits(cipherTextLen, &cipherTextBits))
        goto cleanup;

    if (!xcryptographic_aead_block_setup(&blockOperation, key, algorithm))
        goto cleanup;
    xcryptographic_aead_block_crypt(&blockOperation, zero, hashSubkey);
    if (!xcryptographic_gcm_initial_counter(nonce, nonceLen, hashSubkey, initialCounter))
        goto cleanup;
    xcryptographic_gcm_ghash_update(auth, hashSubkey, associatedData, associatedDataLen);
    xcryptographic_gcm_ghash_update(auth, hashSubkey, cipherText, cipherTextLen);
    xcryptographic_store_u64_be(lengthBlock, associatedDataBits);
    xcryptographic_store_u64_be(lengthBlock + 8, cipherTextBits);
    xcryptographic_gcm_ghash_update(auth, hashSubkey, lengthBlock, sizeof(lengthBlock));
    xcryptographic_aead_block_crypt(&blockOperation, initialCounter, encryptedInitialCounter);
    for (i = 0; i < 16; ++i) tag[i] = auth[i] ^ encryptedInitialCounter[i];
    result = true;

cleanup:
    XCryptographic_blockCipherAbort(&blockOperation);
    memset(hashSubkey, 0, sizeof(hashSubkey));
    memset(initialCounter, 0, sizeof(initialCounter));
    memset(auth, 0, sizeof(auth));
    memset(lengthBlock, 0, sizeof(lengthBlock));
    memset(encryptedInitialCounter, 0, sizeof(encryptedInitialCounter));
    return result;
}

static bool xcryptographic_aes_gcm_authenticate(XCryptographic_Key key,
                                                 const uint8_t* nonce, size_t nonceLen,
                                                 const uint8_t* associatedData, size_t associatedDataLen,
                                                 const uint8_t* cipherText, size_t cipherTextLen,
                                                 const uint8_t* tag, size_t tagSize)
{
    uint8_t calculatedTag[16];
    uint8_t difference = 0;
    size_t i;
    bool result;
    if (!tag || tagSize < 4 || tagSize > sizeof(calculatedTag)) return false;
    result = xcryptographic_aes_gcm_tag(key, nonce, nonceLen, associatedData,
                                        associatedDataLen, cipherText, cipherTextLen,
                                        calculatedTag);
    if (result) {
        for (i = 0; i < tagSize; ++i)
            difference |= (uint8_t)(tag[i] ^ calculatedTag[i]);
        result = difference == 0;
    }
    memset(calculatedTag, 0, sizeof(calculatedTag));
    return result;
}

static bool xcryptographic_aes_gcm_decrypt(XCryptographic_Key key,
                                            const uint8_t* nonce, size_t nonceLen,
                                            const uint8_t* cipherText, size_t cipherTextLen,
                                            uint8_t* output, size_t outputCap)
{
    XCryptographic_BlockCipherOperation blockOperation;
    XCryptographic_BlockCipherAlgorithm algorithm;
    uint8_t hashSubkey[16];
    uint8_t initialCounter[16];
    uint8_t counter[16];
    uint8_t zero[16] = { 0 };
    size_t offset = 0;
    bool result = false;
    memset(&blockOperation, 0, sizeof(blockOperation));
    if (!xcryptographic_aead_block_key_algorithm(key, true, &algorithm) ||
        !nonce || nonceLen == 0 ||
        (!cipherText && cipherTextLen != 0) || !output || outputCap < cipherTextLen ||
        !xcryptographic_gcm_message_length_valid(cipherTextLen))
        goto cleanup;
    if (!xcryptographic_aead_block_setup(&blockOperation, key, algorithm))
        goto cleanup;
    xcryptographic_aead_block_crypt(&blockOperation, zero, hashSubkey);
    if (!xcryptographic_gcm_initial_counter(nonce, nonceLen, hashSubkey, initialCounter))
        goto cleanup;
    memcpy(counter, initialCounter, sizeof(counter));
    xcryptographic_increment_counter32(counter);
    while (offset < cipherTextLen) {
        uint8_t stream[16];
        size_t i;
        size_t blockSize = cipherTextLen - offset;
        if (blockSize > sizeof(stream)) blockSize = sizeof(stream);
        xcryptographic_aead_block_crypt(&blockOperation, counter, stream);
        for (i = 0; i < blockSize; ++i) output[offset + i] = cipherText[offset + i] ^ stream[i];
        memset(stream, 0, sizeof(stream));
        xcryptographic_increment_counter32(counter);
        offset += blockSize;
    }
    result = true;
cleanup:
    XCryptographic_blockCipherAbort(&blockOperation);
    memset(hashSubkey, 0, sizeof(hashSubkey));
    memset(initialCounter, 0, sizeof(initialCounter));
    memset(counter, 0, sizeof(counter));
    return result;
}

static bool xcryptographic_ccm_message_length_valid(size_t messageLen, size_t q)
{
    size_t i;
    if (q < 2 || q > 8) return false;
    for (i = 0; i < q && messageLen != 0; ++i) messageLen >>= 8;
    return messageLen == 0;
}

static void xcryptographic_ccm_store_length(uint8_t* output, size_t count, size_t value)
{
    size_t i;
    for (i = count; i > 0; --i) {
        output[i - 1] = (uint8_t)value;
        value >>= 8;
    }
}

static void xcryptographic_ccm_mac_block(XCryptographic_BlockCipherOperation* operation,
                                         uint8_t state[16], const uint8_t block[16])
{
    uint8_t input[16];
    size_t i;
    for (i = 0; i < sizeof(input); ++i) input[i] = state[i] ^ block[i];
    xcryptographic_aead_block_crypt(operation, input, state);
    memset(input, 0, sizeof(input));
}

static void xcryptographic_ccm_mac_data(XCryptographic_BlockCipherOperation* operation,
                                        uint8_t state[16], const uint8_t* data, size_t dataLen)
{
    while (dataLen >= 16) {
        xcryptographic_ccm_mac_block(operation, state, data);
        data += 16;
        dataLen -= 16;
    }
    if (dataLen != 0) {
        uint8_t block[16] = { 0 };
        memcpy(block, data, dataLen);
        xcryptographic_ccm_mac_block(operation, state, block);
        memset(block, 0, sizeof(block));
    }
}

static bool xcryptographic_ccm_mac_aad(XCryptographic_BlockCipherOperation* operation,
                                       uint8_t state[16], const uint8_t* associatedData,
                                       size_t associatedDataLen)
{
    uint8_t firstBlock[16] = { 0 };
    size_t prefixSize;
    size_t firstCopy;
    if (associatedDataLen == 0) return true;
    if (!associatedData) return false;
    if (associatedDataLen < 0xff00u) {
        firstBlock[0] = (uint8_t)(associatedDataLen >> 8);
        firstBlock[1] = (uint8_t)associatedDataLen;
        prefixSize = 2;
    } else if (associatedDataLen <= UINT32_MAX) {
        firstBlock[0] = 0xff;
        firstBlock[1] = 0xfe;
        firstBlock[2] = (uint8_t)(associatedDataLen >> 24);
        firstBlock[3] = (uint8_t)(associatedDataLen >> 16);
        firstBlock[4] = (uint8_t)(associatedDataLen >> 8);
        firstBlock[5] = (uint8_t)associatedDataLen;
        prefixSize = 6;
    } else {
        firstBlock[0] = 0xff;
        firstBlock[1] = 0xff;
        xcryptographic_store_u64_be(firstBlock + 2, (uint64_t)associatedDataLen);
        prefixSize = 10;
    }
    firstCopy = associatedDataLen;
    if (firstCopy > sizeof(firstBlock) - prefixSize) firstCopy = sizeof(firstBlock) - prefixSize;
    memcpy(firstBlock + prefixSize, associatedData, firstCopy);
    xcryptographic_ccm_mac_block(operation, state, firstBlock);
    associatedData += firstCopy;
    associatedDataLen -= firstCopy;
    xcryptographic_ccm_mac_data(operation, state, associatedData, associatedDataLen);
    memset(firstBlock, 0, sizeof(firstBlock));
    return true;
}

static bool xcryptographic_aes_ccm_mac(XCryptographic_Key key,
                                       const uint8_t* nonce, size_t nonceLen,
                                       const uint8_t* associatedData, size_t associatedDataLen,
                                       const uint8_t* plainText, size_t plainTextLen,
                                       size_t tagSize, uint8_t tag[16])
{
    XCryptographic_BlockCipherOperation blockOperation;
    XCryptographic_BlockCipherAlgorithm algorithm;
    uint8_t block[16] = { 0 };
    uint8_t state[16] = { 0 };
    uint8_t counter[16] = { 0 };
    uint8_t stream[16];
    size_t q;
    size_t i;
    bool result = false;

    memset(&blockOperation, 0, sizeof(blockOperation));
    if (!xcryptographic_aead_block_key_algorithm(key, false, &algorithm) || !nonce ||
        nonceLen < 7 || nonceLen > 13 || (!associatedData && associatedDataLen != 0) ||
        (!plainText && plainTextLen != 0) || !xcryptographic_aes_ccm_tag_size_valid(tagSize) ||
        !tag)
        goto cleanup;
    q = 15 - nonceLen;
    if (!xcryptographic_ccm_message_length_valid(plainTextLen, q)) goto cleanup;
    if (!xcryptographic_aead_block_setup(&blockOperation, key, algorithm))
        goto cleanup;

    block[0] = (uint8_t)((associatedDataLen != 0 ? 0x40u : 0u) |
                         (((tagSize - 2u) / 2u) << 3) | (q - 1u));
    memcpy(block + 1, nonce, nonceLen);
    xcryptographic_ccm_store_length(block + 16 - q, q, plainTextLen);
    xcryptographic_ccm_mac_block(&blockOperation, state, block);
    if (!xcryptographic_ccm_mac_aad(&blockOperation, state, associatedData, associatedDataLen))
        goto cleanup;
    xcryptographic_ccm_mac_data(&blockOperation, state, plainText, plainTextLen);

    counter[0] = (uint8_t)(q - 1u);
    memcpy(counter + 1, nonce, nonceLen);
    xcryptographic_aead_block_crypt(&blockOperation, counter, stream);
    for (i = 0; i < tagSize; ++i) tag[i] = state[i] ^ stream[i];
    result = true;

cleanup:
    XCryptographic_blockCipherAbort(&blockOperation);
    memset(block, 0, sizeof(block));
    memset(state, 0, sizeof(state));
    memset(counter, 0, sizeof(counter));
    memset(stream, 0, sizeof(stream));
    return result;
}

static bool xcryptographic_aes_ccm_crypt(XCryptographic_Key key,
                                         const uint8_t* nonce, size_t nonceLen,
                                         const uint8_t* input, size_t inputLen,
                                         uint8_t* output, size_t outputCap)
{
    XCryptographic_BlockCipherOperation blockOperation;
    XCryptographic_BlockCipherAlgorithm algorithm;
    uint8_t counter[16] = { 0 };
    size_t q;
    size_t offset = 0;
    bool result = false;
    memset(&blockOperation, 0, sizeof(blockOperation));
    if (!xcryptographic_aead_block_key_algorithm(key, false, &algorithm) || !nonce ||
        nonceLen < 7 || nonceLen > 13 || (!input && inputLen != 0) ||
        !output || outputCap < inputLen)
        goto cleanup;
    q = 15 - nonceLen;
    if (!xcryptographic_ccm_message_length_valid(inputLen, q)) goto cleanup;
    if (!xcryptographic_aead_block_setup(&blockOperation, key, algorithm))
        goto cleanup;
    counter[0] = (uint8_t)(q - 1u);
    memcpy(counter + 1, nonce, nonceLen);
    while (offset < inputLen) {
        uint8_t stream[16];
        size_t i;
        size_t blockSize = inputLen - offset;
        if (blockSize > sizeof(stream)) blockSize = sizeof(stream);
        xcryptographic_ccm_store_length(counter + 16 - q, q, offset / 16u + 1u);
        xcryptographic_aead_block_crypt(&blockOperation, counter, stream);
        for (i = 0; i < blockSize; ++i) output[offset + i] = input[offset + i] ^ stream[i];
        memset(stream, 0, sizeof(stream));
        offset += blockSize;
    }
    result = true;
cleanup:
    XCryptographic_blockCipherAbort(&blockOperation);
    memset(counter, 0, sizeof(counter));
    return result;
}

static bool xcryptographic_aes_ccm_decrypt_authenticated(
    XCryptographic_Key key, const uint8_t* nonce, size_t nonceLen,
    const uint8_t* associatedData, size_t associatedDataLen,
    const uint8_t* cipherText, size_t cipherTextLen,
    const uint8_t* tag, size_t tagSize, uint8_t* output, size_t outputCap)
{
    uint8_t calculatedTag[16];
    uint8_t* plainText;
    uint8_t difference = 0;
    size_t i;
    bool result = false;
    if (!tag || !output || outputCap < cipherTextLen ||
        !xcryptographic_aes_ccm_tag_size_valid(tagSize))
        goto cleanup;
    plainText = (uint8_t*)XMalloc_System(cipherTextLen == 0 ? 1 : cipherTextLen);
    if (!plainText) goto cleanup;
    if (!xcryptographic_aes_ccm_crypt(key, nonce, nonceLen, cipherText, cipherTextLen,
                                      plainText, cipherTextLen) ||
        !xcryptographic_aes_ccm_mac(key, nonce, nonceLen, associatedData, associatedDataLen,
                                    plainText, cipherTextLen, tagSize, calculatedTag))
        goto free_plain_text;
    for (i = 0; i < tagSize; ++i)
        difference |= (uint8_t)(tag[i] ^ calculatedTag[i]);
    if (difference != 0) goto free_plain_text;
    if (cipherTextLen != 0) memcpy(output, plainText, cipherTextLen);
    result = true;
free_plain_text:
    memset(plainText, 0, cipherTextLen == 0 ? 1 : cipherTextLen);
    XFree_System(plainText);
cleanup:
    memset(calculatedTag, 0, sizeof(calculatedTag));
    return result;
}

bool XCryptographic_aesCcmStarNoTagSetup(
    XCryptographic_CcmStarNoTagOperation* operation,
    XByteArrayView key, XByteArrayView nonce)
{
    uint8_t roundKeys[240] = { 0 };
    uint8_t rounds = 0;
    size_t q;
    if (!operation || !XCRYPTOGRAPHIC_CCM_STAR_NO_TAG_ON ||
        (key.m_size != 16 && key.m_size != 24 && key.m_size != 32) ||
        nonce.m_size != 13 || !key.m_data || !nonce.m_data) return false;
    q = 15u - (size_t)nonce.m_size;
    memset(operation, 0, sizeof(*operation));
    xcryptographic_aes_expand(key.m_data, (size_t)key.m_size, roundKeys, &rounds);
    memcpy(operation->roundKeys, roundKeys, sizeof(operation->roundKeys));
    memcpy(operation->key, key.m_data, (size_t)key.m_size);
    operation->keyLen = (uint8_t)key.m_size;
    operation->rounds = rounds;
    operation->counter[0] = (uint8_t)(q - 1u);
    memcpy(operation->counter + 1, nonce.m_data, (size_t)nonce.m_size);
    operation->streamOffset = 16;
    operation->blockCounter = 0;
    operation->active = true;
    memset(roundKeys, 0, sizeof(roundKeys));
    return true;
}

XByteArrayView XCryptographic_aesCcmStarNoTagUpdateInto(
    XCryptographic_CcmStarNoTagOperation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = { (const uint8_t*)buffer, data.m_size };
    size_t index;
    if (!operation || !operation->active || data.m_size < 0 ||
        (!data.m_data && data.m_size != 0) || !buffer ||
        bufferSize < (size_t)data.m_size) return empty;
    for (index = 0; index < (size_t)data.m_size; ++index) {
        size_t q = 15u - 13u; /* PSA CCM_STAR_NO_TAG 固定 13 字节 nonce */
        if (operation->streamOffset == 16) {
            ++operation->blockCounter;
            xcryptographic_ccm_store_length(
                operation->counter + 16u - q, q, operation->blockCounter);
            xcryptographic_aes_block(operation->roundKeys, operation->rounds,
                                     operation->counter, operation->stream);
            operation->streamOffset = 0;
        }
        ((uint8_t*)buffer)[index] = data.m_data[index] ^ operation->stream[operation->streamOffset++];
    }
    if (result.m_size == 0 && !result.m_data) result.m_data = (const uint8_t*)"";
    return result;
}

void XCryptographic_aesCcmStarNoTagAbort(
    XCryptographic_CcmStarNoTagOperation* operation)
{
    if (operation) memset(operation, 0, sizeof(*operation));
}

static uint32_t xcryptographic_load_u32_le(const uint8_t* input)
{
    return ((uint32_t)input[0]) |
           ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) |
           ((uint32_t)input[3] << 24);
}

static void xcryptographic_store_u32_le(uint8_t* output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void xcryptographic_store_u64_le(uint8_t* output, uint64_t value)
{
    int i;
    for (i = 0; i < 8; ++i) {
        output[i] = (uint8_t)value;
        value >>= 8;
    }
}

static uint32_t xcryptographic_chacha20_rotate(uint32_t value, uint32_t bits)
{
    return (value << bits) | (value >> (32u - bits));
}

static void xcryptographic_chacha20_quarter_round(uint32_t state[16],
                                                   size_t a, size_t b,
                                                   size_t c, size_t d)
{
    state[a] += state[b];
    state[d] ^= state[a];
    state[d] = xcryptographic_chacha20_rotate(state[d], 16);
    state[c] += state[d];
    state[b] ^= state[c];
    state[b] = xcryptographic_chacha20_rotate(state[b], 12);
    state[a] += state[b];
    state[d] ^= state[a];
    state[d] = xcryptographic_chacha20_rotate(state[d], 8);
    state[c] += state[d];
    state[b] ^= state[c];
    state[b] = xcryptographic_chacha20_rotate(state[b], 7);
}

static void xcryptographic_chacha20_block(const uint8_t key[32], uint32_t counter,
                                           const uint8_t nonce[12], uint8_t output[64])
{
    static const uint32_t constants[4] = {
        UINT32_C(0x61707865), UINT32_C(0x3320646e),
        UINT32_C(0x79622d32), UINT32_C(0x6b206574)
    };
    uint32_t state[16];
    uint32_t working[16];
    size_t i;
    memcpy(state, constants, sizeof(constants));
    for (i = 0; i < 8; ++i) state[4 + i] = xcryptographic_load_u32_le(key + i * 4);
    state[12] = counter;
    state[13] = xcryptographic_load_u32_le(nonce);
    state[14] = xcryptographic_load_u32_le(nonce + 4);
    state[15] = xcryptographic_load_u32_le(nonce + 8);
    memcpy(working, state, sizeof(working));
    for (i = 0; i < 10; ++i) {
        xcryptographic_chacha20_quarter_round(working, 0, 4, 8, 12);
        xcryptographic_chacha20_quarter_round(working, 1, 5, 9, 13);
        xcryptographic_chacha20_quarter_round(working, 2, 6, 10, 14);
        xcryptographic_chacha20_quarter_round(working, 3, 7, 11, 15);
        xcryptographic_chacha20_quarter_round(working, 0, 5, 10, 15);
        xcryptographic_chacha20_quarter_round(working, 1, 6, 11, 12);
        xcryptographic_chacha20_quarter_round(working, 2, 7, 8, 13);
        xcryptographic_chacha20_quarter_round(working, 3, 4, 9, 14);
    }
    for (i = 0; i < 16; ++i) xcryptographic_store_u32_le(output + i * 4, working[i] + state[i]);
    memset(state, 0, sizeof(state));
    memset(working, 0, sizeof(working));
}

static void xcryptographic_poly1305_init(XCryptographic_Poly1305* context,
                                          const uint8_t key[32])
{
    context->r[0] = xcryptographic_load_u32_le(key) & 0x0fffffffu;
    context->r[1] = xcryptographic_load_u32_le(key + 4) & 0x0ffffffcu;
    context->r[2] = xcryptographic_load_u32_le(key + 8) & 0x0ffffffcu;
    context->r[3] = xcryptographic_load_u32_le(key + 12) & 0x0ffffffcu;
    context->pad[0] = xcryptographic_load_u32_le(key + 16);
    context->pad[1] = xcryptographic_load_u32_le(key + 20);
    context->pad[2] = xcryptographic_load_u32_le(key + 24);
    context->pad[3] = xcryptographic_load_u32_le(key + 28);
    memset(context->h, 0, sizeof(context->h));
    memset(context->buffer, 0, sizeof(context->buffer));
    context->bufferLen = 0;
}

static void xcryptographic_poly1305_blocks(XCryptographic_Poly1305* context,
                                             const uint8_t* message, size_t messageLen,
                                             bool fullBlock)
{
    uint64_t d0, d1, d2, d3;
    uint32_t acc0 = context->h[0], acc1 = context->h[1], acc2 = context->h[2];
    uint32_t acc3 = context->h[3], acc4 = context->h[4];
    uint32_t r0 = context->r[0], r1 = context->r[1], r2 = context->r[2], r3 = context->r[3];
    uint32_t rs1 = r1 + (r1 >> 2u), rs2 = r2 + (r2 >> 2u), rs3 = r3 + (r3 >> 2u);
    size_t offset = 0;
    size_t blockCount = messageLen / 16u;
    size_t i;
    for (i = 0; i < blockCount; ++i) {
        uint32_t t0 = xcryptographic_load_u32_le(message + offset);
        uint32_t t1 = xcryptographic_load_u32_le(message + offset + 4);
        uint32_t t2 = xcryptographic_load_u32_le(message + offset + 8);
        uint32_t t3 = xcryptographic_load_u32_le(message + offset + 12);
        d0 = (uint64_t)t0 + acc0;
        d1 = (uint64_t)t1 + acc1 + (d0 >> 32u);
        d2 = (uint64_t)t2 + acc2 + (d1 >> 32u);
        d3 = (uint64_t)t3 + acc3 + (d2 >> 32u);
        acc0 = (uint32_t)d0; acc1 = (uint32_t)d1; acc2 = (uint32_t)d2; acc3 = (uint32_t)d3;
        acc4 += (uint32_t)(d3 >> 32u) + (fullBlock ? 1u : 0u);
        d0 = (uint64_t)acc0 * r0 + (uint64_t)acc1 * rs3 + (uint64_t)acc2 * rs2 + (uint64_t)acc3 * rs1;
        d1 = (uint64_t)acc0 * r1 + (uint64_t)acc1 * r0 + (uint64_t)acc2 * rs3 + (uint64_t)acc3 * rs2 + (uint64_t)acc4 * rs1;
        d2 = (uint64_t)acc0 * r2 + (uint64_t)acc1 * r1 + (uint64_t)acc2 * r0 + (uint64_t)acc3 * rs3 + (uint64_t)acc4 * rs2;
        d3 = (uint64_t)acc0 * r3 + (uint64_t)acc1 * r2 + (uint64_t)acc2 * r1 + (uint64_t)acc3 * r0 + (uint64_t)acc4 * rs3;
        acc4 *= r0;
        d1 += d0 >> 32u; d2 += d1 >> 32u; d3 += d2 >> 32u;
        acc0 = (uint32_t)d0; acc1 = (uint32_t)d1; acc2 = (uint32_t)d2; acc3 = (uint32_t)d3;
        acc4 += (uint32_t)(d3 >> 32u);
        d0 = (uint64_t)acc0 + (acc4 >> 2u) + (acc4 & 0xfffffffcu);
        acc4 &= 3u; acc0 = (uint32_t)d0;
        d0 = (uint64_t)acc1 + (d0 >> 32u); acc1 = (uint32_t)d0;
        d0 = (uint64_t)acc2 + (d0 >> 32u); acc2 = (uint32_t)d0;
        d0 = (uint64_t)acc3 + (d0 >> 32u); acc3 = (uint32_t)d0;
        d0 = (uint64_t)acc4 + (d0 >> 32u); acc4 = (uint32_t)d0;
        offset += 16;
    }
    context->h[0] = acc0; context->h[1] = acc1; context->h[2] = acc2;
    context->h[3] = acc3; context->h[4] = acc4;
}

static void xcryptographic_poly1305_update(XCryptographic_Poly1305* context,
                                            const uint8_t* message, size_t messageLen)
{
    size_t copySize;
    if (!message || messageLen == 0) return;
    if (context->bufferLen != 0) {
        copySize = sizeof(context->buffer) - context->bufferLen;
        if (copySize > messageLen) copySize = messageLen;
        memcpy(context->buffer + context->bufferLen, message, copySize);
        context->bufferLen += copySize;
        message += copySize;
        messageLen -= copySize;
        if (context->bufferLen == sizeof(context->buffer)) {
            xcryptographic_poly1305_blocks(context, context->buffer,
                                           sizeof(context->buffer), true);
            context->bufferLen = 0;
        }
    }
    if (messageLen >= sizeof(context->buffer)) {
        size_t fullLen = messageLen & ~(size_t)15;
        xcryptographic_poly1305_blocks(context, message, fullLen, true);
        message += fullLen;
        messageLen -= fullLen;
    }
    if (messageLen != 0) {
        memcpy(context->buffer, message, messageLen);
        context->bufferLen = messageLen;
    }
}

static void xcryptographic_poly1305_finish(XCryptographic_Poly1305* context,
                                             uint8_t tag[16])
{
    uint64_t d;
    uint32_t g0, g1, g2, g3, g4, mask, maskInverse;
    uint32_t h0 = context->h[0], h1 = context->h[1], h2 = context->h[2];
    uint32_t h3 = context->h[3], h4 = context->h[4];
    if (context->bufferLen != 0) {
        uint8_t block[16] = { 0 };
        memcpy(block, context->buffer, context->bufferLen);
        block[context->bufferLen] = 1;
        xcryptographic_poly1305_blocks(context, block, sizeof(block), false);
        memset(block, 0, sizeof(block));
        context->bufferLen = 0;
        h0 = context->h[0]; h1 = context->h[1]; h2 = context->h[2];
        h3 = context->h[3]; h4 = context->h[4];
    }
    d = (uint64_t)h0 + 5u; g0 = (uint32_t)d;
    d = (uint64_t)h1 + (d >> 32u); g1 = (uint32_t)d;
    d = (uint64_t)h2 + (d >> 32u); g2 = (uint32_t)d;
    d = (uint64_t)h3 + (d >> 32u); g3 = (uint32_t)d;
    g4 = h4 + (uint32_t)(d >> 32u);
    mask = (uint32_t)0u - (g4 >> 2u);
    maskInverse = ~mask;
    h0 = (h0 & maskInverse) | (g0 & mask);
    h1 = (h1 & maskInverse) | (g1 & mask);
    h2 = (h2 & maskInverse) | (g2 & mask);
    h3 = (h3 & maskInverse) | (g3 & mask);
    d = (uint64_t)h0 + context->pad[0]; h0 = (uint32_t)d;
    d = (uint64_t)h1 + context->pad[1] + (d >> 32u); h1 = (uint32_t)d;
    d = (uint64_t)h2 + context->pad[2] + (d >> 32u); h2 = (uint32_t)d;
    d = (uint64_t)h3 + context->pad[3] + (d >> 32u); h3 = (uint32_t)d;
    xcryptographic_store_u32_le(tag, h0);
    xcryptographic_store_u32_le(tag + 4, h1);
    xcryptographic_store_u32_le(tag + 8, h2);
    xcryptographic_store_u32_le(tag + 12, h3);
    memset(context, 0, sizeof(*context));
}

static bool xcryptographic_chacha20_crypt(XCryptographic_Key key, const uint8_t nonce[12],
                                           const uint8_t* input, size_t inputLen,
                                           uint8_t* output, size_t outputCap)
{
    uint32_t counter = 1;
    size_t offset = 0;
    if (key.type != XCryptographic_KeyType_ChaCha20Poly1305 ||
        !XCRYPTOGRAPHIC_CHACHA20_POLY1305_ON || !nonce ||
        (!input && inputLen != 0) || !output || outputCap < inputLen ||
        inputLen > ((uint64_t)UINT32_MAX - 1u) * 64u)
        return false;
    while (offset < inputLen) {
        uint8_t stream[64];
        size_t i, blockSize = inputLen - offset;
        if (blockSize > sizeof(stream)) blockSize = sizeof(stream);
        xcryptographic_chacha20_block(key.symmetricKey, counter++, nonce, stream);
        for (i = 0; i < blockSize; ++i) output[offset + i] = input[offset + i] ^ stream[i];
        memset(stream, 0, sizeof(stream));
        offset += blockSize;
    }
    return true;
}

static bool xcryptographic_chacha20_poly1305_tag(XCryptographic_Key key, const uint8_t nonce[12],
                                                  const uint8_t* associatedData, size_t associatedDataLen,
                                                  const uint8_t* cipherText, size_t cipherTextLen,
                                                  uint8_t tag[16])
{
    uint8_t firstBlock[64];
    uint8_t polyKey[32];
    uint8_t lengths[16];
    XCryptographic_Poly1305 poly;
    size_t i;
    if (key.type != XCryptographic_KeyType_ChaCha20Poly1305 ||
        !XCRYPTOGRAPHIC_CHACHA20_POLY1305_ON || !nonce ||
        (!associatedData && associatedDataLen != 0) ||
        (!cipherText && cipherTextLen != 0) || !tag)
        return false;
    xcryptographic_chacha20_block(key.symmetricKey, 0, nonce, firstBlock);
    memcpy(polyKey, firstBlock, sizeof(polyKey));
    xcryptographic_poly1305_init(&poly, polyKey);
    xcryptographic_poly1305_update(&poly, associatedData, associatedDataLen);
    if ((associatedDataLen & 15u) != 0) {
        uint8_t padding[16] = { 0 };
        xcryptographic_poly1305_update(&poly, padding, 16 - (associatedDataLen & 15u));
    }
    xcryptographic_poly1305_update(&poly, cipherText, cipherTextLen);
    if ((cipherTextLen & 15u) != 0) {
        uint8_t padding[16] = { 0 };
        xcryptographic_poly1305_update(&poly, padding, 16 - (cipherTextLen & 15u));
    }
    xcryptographic_store_u64_le(lengths, (uint64_t)associatedDataLen);
    xcryptographic_store_u64_le(lengths + 8, (uint64_t)cipherTextLen);
    xcryptographic_poly1305_update(&poly, lengths, sizeof(lengths));
    xcryptographic_poly1305_finish(&poly, tag);
    for (i = 0; i < sizeof(firstBlock); ++i) firstBlock[i] = 0;
    memset(polyKey, 0, sizeof(polyKey));
    memset(lengths, 0, sizeof(lengths));
    return true;
}

bool XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm algorithm,
                                  XByteArrayView key, XCryptographic_Key* result)
{
    bool enabled;
    XCryptographic_KeyType keyType;
    if (!result || key.m_size < 0 || (!key.m_data && key.m_size != 0))
        return false;
    if (algorithm == XCryptographic_AeadAlgorithm_AesGcm) {
        enabled = xcryptographic_aes_gcm_enabled((size_t)key.m_size);
        keyType = XCryptographic_KeyType_AesGcm;
    } else if (algorithm == XCryptographic_AeadAlgorithm_AesCcm) {
        enabled = xcryptographic_aes_ccm_enabled((size_t)key.m_size);
        keyType = XCryptographic_KeyType_AesCcm;
    } else if (algorithm == XCryptographic_AeadAlgorithm_AriaGcm) {
        enabled = xcryptographic_aria_enabled((size_t)key.m_size);
        keyType = XCryptographic_KeyType_AriaGcm;
    } else if (algorithm == XCryptographic_AeadAlgorithm_AriaCcm) {
        enabled = xcryptographic_aria_enabled((size_t)key.m_size);
        keyType = XCryptographic_KeyType_AriaCcm;
    } else if (algorithm == XCryptographic_AeadAlgorithm_CamelliaGcm) {
        enabled = xcryptographic_camellia_enabled((size_t)key.m_size);
        keyType = XCryptographic_KeyType_CamelliaGcm;
    } else if (algorithm == XCryptographic_AeadAlgorithm_CamelliaCcm) {
        enabled = xcryptographic_camellia_enabled((size_t)key.m_size);
        keyType = XCryptographic_KeyType_CamelliaCcm;
    } else if (algorithm == XCryptographic_AeadAlgorithm_ChaCha20Poly1305) {
        enabled = XCRYPTOGRAPHIC_CHACHA20_POLY1305_ON && key.m_size == 32;
        keyType = XCryptographic_KeyType_ChaCha20Poly1305;
    } else {
        return false;
    }
    if (!enabled) return false;
    memset(result, 0, sizeof(*result));
    result->type = keyType;
    result->symmetricKeyLen = (size_t)key.m_size;
    memcpy(result->symmetricKey, key.m_data, result->symmetricKeyLen);
    return true;
}

static void xcryptographic_aead_gcm_auth_block(XCryptographic_AeadOperation* operation,
                                                const uint8_t block[16])
{
    size_t i;
    for (i = 0; i < 16; ++i) operation->gcmAuth[i] ^= block[i];
    xcryptographic_gcm_multiply(operation->gcmAuth, operation->gcmHashSubkey);
}

static void xcryptographic_aead_gcm_auth_update(XCryptographic_AeadOperation* operation,
                                                 const uint8_t* data, size_t dataLen)
{
    size_t copySize;
    if (!data || dataLen == 0) return;
    while (dataLen != 0) {
        copySize = sizeof(operation->gcmPending) - operation->gcmPendingLen;
        if (copySize > dataLen) copySize = dataLen;
        memcpy(operation->gcmPending + operation->gcmPendingLen, data, copySize);
        operation->gcmPendingLen += copySize;
        data += copySize;
        dataLen -= copySize;
        if (operation->gcmPendingLen == sizeof(operation->gcmPending)) {
            xcryptographic_aead_gcm_auth_block(operation, operation->gcmPending);
            operation->gcmPendingLen = 0;
        }
    }
}

static void xcryptographic_aead_gcm_auth_flush(XCryptographic_AeadOperation* operation)
{
    uint8_t block[16] = { 0 };
    if (operation->gcmPendingLen != 0) {
        memcpy(block, operation->gcmPending, operation->gcmPendingLen);
        xcryptographic_aead_gcm_auth_block(operation, block);
        operation->gcmPendingLen = 0;
    }
    memset(block, 0, sizeof(block));
}

static void xcryptographic_aead_gcm_auth_finish(XCryptographic_AeadOperation* operation)
{
    uint8_t block[16] = { 0 };
    uint64_t associatedDataBits;
    uint64_t dataBits;
    xcryptographic_aead_gcm_auth_flush(operation);
    xcryptographic_length_to_bits(operation->associatedDataLength, &associatedDataBits);
    xcryptographic_length_to_bits(operation->dataLength, &dataBits);
    xcryptographic_store_u64_be(block, associatedDataBits);
    xcryptographic_store_u64_be(block + 8, dataBits);
    xcryptographic_aead_gcm_auth_block(operation, block);
    memset(block, 0, sizeof(block));
}

static bool xcryptographic_aead_ccm_feed(XCryptographic_AeadOperation* operation,
                                         const uint8_t* data, size_t dataLen)
{
    size_t copySize;
    if (!data && dataLen != 0) return false;
    while (dataLen != 0) {
        copySize = sizeof(operation->ccmPending) - operation->ccmPendingLen;
        if (copySize > dataLen) copySize = dataLen;
        memcpy(operation->ccmPending + operation->ccmPendingLen, data, copySize);
        operation->ccmPendingLen += copySize;
        data += copySize;
        dataLen -= copySize;
        if (operation->ccmPendingLen == sizeof(operation->ccmPending)) {
            xcryptographic_ccm_mac_block(&operation->blockCipher, operation->ccmState,
                                         operation->ccmPending);
            operation->ccmPendingLen = 0;
        }
    }
    return true;
}

static bool xcryptographic_aead_ccm_finish_aad(XCryptographic_AeadOperation* operation)
{
    uint8_t block[16] = { 0 };
    if (operation->associatedDataFinalized) return true;
    if (operation->ccmPendingLen != 0) {
        memcpy(block, operation->ccmPending, operation->ccmPendingLen);
        xcryptographic_ccm_mac_block(&operation->blockCipher, operation->ccmState, block);
        operation->ccmPendingLen = 0;
    }
    operation->associatedDataFinalized = true;
    memset(block, 0, sizeof(block));
    return true;
}

static bool xcryptographic_aead_ccm_init_lengths(XCryptographic_AeadOperation* operation)
{
    uint8_t block[16] = { 0 };
    size_t q;
    if (operation->nonceLen < 7 || operation->nonceLen > 13 ||
        !xcryptographic_aes_ccm_tag_size_valid(operation->tagSize)) return false;
    q = 15u - operation->nonceLen;
    if (!xcryptographic_ccm_message_length_valid(operation->expectedDataLength, q)) return false;
    block[0] = (uint8_t)((operation->expectedAssociatedDataLength != 0 ? 0x40u : 0u) |
                         (((operation->tagSize - 2u) / 2u) << 3) | (q - 1u));
    memcpy(block + 1, operation->nonce, operation->nonceLen);
    xcryptographic_ccm_store_length(block + 16u - q, q, operation->expectedDataLength);
    xcryptographic_ccm_mac_block(&operation->blockCipher, operation->ccmState, block);
    operation->ccmPendingLen = 0;
    operation->ccmAdPrefixLen = 0;
    operation->ccmAdPrefixOffset = 0;
    if (operation->expectedAssociatedDataLength != 0) {
        size_t adLen = operation->expectedAssociatedDataLength;
        if (adLen < 0xff00u) {
            operation->ccmAdPrefix[0] = (uint8_t)(adLen >> 8);
            operation->ccmAdPrefix[1] = (uint8_t)adLen;
            operation->ccmAdPrefixLen = 2;
        } else if (adLen <= UINT32_MAX) {
            operation->ccmAdPrefix[0] = 0xff;
            operation->ccmAdPrefix[1] = 0xfe;
            operation->ccmAdPrefix[2] = (uint8_t)(adLen >> 24);
            operation->ccmAdPrefix[3] = (uint8_t)(adLen >> 16);
            operation->ccmAdPrefix[4] = (uint8_t)(adLen >> 8);
            operation->ccmAdPrefix[5] = (uint8_t)adLen;
            operation->ccmAdPrefixLen = 6;
        } else {
            operation->ccmAdPrefix[0] = 0xff;
            operation->ccmAdPrefix[1] = 0xff;
            xcryptographic_store_u64_be(operation->ccmAdPrefix + 2, (uint64_t)adLen);
            operation->ccmAdPrefixLen = 10;
        }
    }
    operation->ccmCounter[0] = (uint8_t)(q - 1u);
    memcpy(operation->ccmCounter + 1, operation->nonce, operation->nonceLen);
    operation->ccmBlockCounter = 1;
    operation->ccmStreamOffset = sizeof(operation->ccmStream);
    memset(block, 0, sizeof(block));
    return true;
}

bool XCryptographic_aeadSetup(XCryptographic_AeadOperation* operation,
                              XCryptographic_AeadAlgorithm algorithm,
                              XCryptographic_Key key, bool encrypt)
{
    XCryptographic_BlockCipherAlgorithm blockAlgorithm;
    if (!operation) return false;
    memset(operation, 0, sizeof(*operation));
    operation->algorithm = algorithm;
    operation->key = key;
    operation->encrypt = encrypt;
    operation->tagSize = 16;
    operation->active = true;
    if (algorithm == XCryptographic_AeadAlgorithm_ChaCha20Poly1305)
        return XCRYPTOGRAPHIC_CHACHA20_POLY1305_ON != 0 &&
               key.type == XCryptographic_KeyType_ChaCha20Poly1305 &&
               key.symmetricKeyLen == 32;
    if (!xcryptographic_aead_block_key_algorithm(key,
            algorithm == XCryptographic_AeadAlgorithm_AesGcm ||
            algorithm == XCryptographic_AeadAlgorithm_AriaGcm ||
            algorithm == XCryptographic_AeadAlgorithm_CamelliaGcm,
            &blockAlgorithm) ||
        !xcryptographic_aead_block_setup(&operation->blockCipher, key, blockAlgorithm)) {
        memset(operation, 0, sizeof(*operation));
        return false;
    }
    return true;
}

bool XCryptographic_aeadSetNonce(XCryptographic_AeadOperation* operation,
                                 XByteArrayView nonce)
{
    uint8_t zero[16] = { 0 };
    uint8_t firstBlock[64] = { 0 };
    if (!operation || !operation->active || nonce.m_size < 0 ||
        (!nonce.m_data && nonce.m_size != 0) || nonce.m_size > (int64_t)sizeof(operation->nonce))
        return false;
    if (operation->algorithm == XCryptographic_AeadAlgorithm_ChaCha20Poly1305) {
        if (nonce.m_size != 12 || !XCryptographic_chacha20Setup(
                &operation->chacha20,
                XByteArrayView_create_data(operation->key.symmetricKey, 32), nonce)) return false;
        operation->chacha20.counter = 1;
        operation->chacha20.streamOffset = sizeof(operation->chacha20.stream);
        xcryptographic_chacha20_block(operation->key.symmetricKey, 0, nonce.m_data, firstBlock);
        xcryptographic_poly1305_init(&operation->poly1305, firstBlock);
    } else {
        uint8_t hashSubkey[16];
        if (nonce.m_size == 0 || (!nonce.m_data && nonce.m_size != 0)) return false;
        operation->nonceLen = (size_t)nonce.m_size;
        memcpy(operation->nonce, nonce.m_data, operation->nonceLen);
        if (operation->algorithm == XCryptographic_AeadAlgorithm_AesCcm ||
            operation->algorithm == XCryptographic_AeadAlgorithm_AriaCcm ||
            operation->algorithm == XCryptographic_AeadAlgorithm_CamelliaCcm) {
            operation->nonceLen = (size_t)nonce.m_size;
        } else {
            xcryptographic_aead_block_crypt(&operation->blockCipher, zero, hashSubkey);
            memcpy(operation->gcmHashSubkey, hashSubkey, sizeof(hashSubkey));
            if (!xcryptographic_gcm_initial_counter(operation->nonce, operation->nonceLen,
                                                     operation->gcmHashSubkey,
                                                     operation->gcmInitialCounter)) return false;
            memcpy(operation->gcmCounter, operation->gcmInitialCounter, 16);
            xcryptographic_increment_counter32(operation->gcmCounter);
            operation->gcmStreamOffset = sizeof(operation->gcmStream);
            memset(hashSubkey, 0, sizeof(hashSubkey));
        }
    }
    operation->nonceLen = (size_t)nonce.m_size;
    memset(zero, 0, sizeof(zero));
    memset(firstBlock, 0, sizeof(firstBlock));
    return true;
}

bool XCryptographic_aeadSetLengths(XCryptographic_AeadOperation* operation,
                                   size_t associatedDataLength, size_t dataLength)
{
    if (!operation || !operation->active || operation->nonceLen == 0 ||
        operation->lengthsSet) return false;
    operation->expectedAssociatedDataLength = associatedDataLength;
    operation->expectedDataLength = dataLength;
    operation->tagSize = operation->algorithm == XCryptographic_AeadAlgorithm_ChaCha20Poly1305 ?
                         16 : operation->tagSize;
    if (operation->algorithm == XCryptographic_AeadAlgorithm_AesCcm ||
        operation->algorithm == XCryptographic_AeadAlgorithm_AriaCcm ||
        operation->algorithm == XCryptographic_AeadAlgorithm_CamelliaCcm) {
        if (!xcryptographic_aead_ccm_init_lengths(operation)) return false;
    }
    operation->lengthsSet = true;
    return true;
}

bool XCryptographic_aeadSetTagSize(XCryptographic_AeadOperation* operation,
                                   size_t tagSize)
{
    if (!operation || !operation->active || operation->lengthsSet || tagSize < 4 || tagSize > 16)
        return false;
    if ((operation->algorithm == XCryptographic_AeadAlgorithm_AesCcm ||
         operation->algorithm == XCryptographic_AeadAlgorithm_AriaCcm ||
         operation->algorithm == XCryptographic_AeadAlgorithm_CamelliaCcm) &&
        !xcryptographic_aes_ccm_tag_size_valid(tagSize)) return false;
    if (operation->algorithm == XCryptographic_AeadAlgorithm_ChaCha20Poly1305 && tagSize != 16)
        return false;
    operation->tagSize = tagSize;
    return true;
}

bool XCryptographic_aeadUpdateAssociatedData(XCryptographic_AeadOperation* operation,
                                             XByteArrayView associatedData)
{
    size_t remaining;
    if (!operation || !operation->active || associatedData.m_size < 0 ||
        (!associatedData.m_data && associatedData.m_size != 0)) return false;
    if (operation->algorithm == XCryptographic_AeadAlgorithm_ChaCha20Poly1305) {
        xcryptographic_poly1305_update(&operation->poly1305, associatedData.m_data,
                                       (size_t)associatedData.m_size);
    } else if (operation->algorithm == XCryptographic_AeadAlgorithm_AesGcm ||
               operation->algorithm == XCryptographic_AeadAlgorithm_AriaGcm ||
               operation->algorithm == XCryptographic_AeadAlgorithm_CamelliaGcm) {
        xcryptographic_aead_gcm_auth_update(operation, associatedData.m_data,
                                            (size_t)associatedData.m_size);
    } else {
        if (!operation->lengthsSet || operation->associatedDataFinalized ||
            operation->ccmAdPrefixOffset > operation->ccmAdPrefixLen) return false;
        remaining = operation->ccmAdPrefixLen - operation->ccmAdPrefixOffset;
        if (remaining != 0) {
            if (!xcryptographic_aead_ccm_feed(operation,
                    operation->ccmAdPrefix + operation->ccmAdPrefixOffset, remaining)) return false;
            operation->ccmAdPrefixOffset = operation->ccmAdPrefixLen;
        }
        if (!xcryptographic_aead_ccm_feed(operation, associatedData.m_data,
                                          (size_t)associatedData.m_size)) return false;
    }
    operation->associatedDataLength += (size_t)associatedData.m_size;
    return operation->expectedAssociatedDataLength == 0 ||
           operation->associatedDataLength <= operation->expectedAssociatedDataLength;
}

XByteArrayView XCryptographic_aeadUpdateInto(XCryptographic_AeadOperation* operation,
                                             char* buffer, size_t bufferSize,
                                             XByteArrayView data)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = { (const uint8_t*)buffer, data.m_size };
    size_t i;
    if (!operation || !operation->active || data.m_size < 0 ||
        (!data.m_data && data.m_size != 0) || !buffer || bufferSize < (size_t)data.m_size ||
        (operation->lengthsSet && operation->dataLength + (size_t)data.m_size > operation->expectedDataLength))
        return empty;
    if (operation->algorithm == XCryptographic_AeadAlgorithm_ChaCha20Poly1305) {
        if (!operation->associatedDataFinalized) {
            if ((operation->associatedDataLength & 15u) != 0) {
                uint8_t padding[16] = { 0 };
                xcryptographic_poly1305_update(&operation->poly1305, padding,
                                               16u - (operation->associatedDataLength & 15u));
            }
            operation->associatedDataFinalized = true;
        }
        result = XCryptographic_chacha20UpdateInto(&operation->chacha20, buffer, bufferSize, data);
        if (!result.m_data && data.m_size != 0) return empty;
        xcryptographic_poly1305_update(&operation->poly1305,
            operation->encrypt ? (const uint8_t*)buffer : data.m_data, (size_t)data.m_size);
    } else if (operation->algorithm == XCryptographic_AeadAlgorithm_AesGcm ||
               operation->algorithm == XCryptographic_AeadAlgorithm_AriaGcm ||
               operation->algorithm == XCryptographic_AeadAlgorithm_CamelliaGcm) {
        if (!operation->associatedDataFinalized) {
            xcryptographic_aead_gcm_auth_flush(operation);
            operation->associatedDataFinalized = true;
        }
        for (i = 0; i < (size_t)data.m_size; ++i) {
            if (operation->gcmStreamOffset == sizeof(operation->gcmStream)) {
                xcryptographic_aead_block_crypt(&operation->blockCipher,
                                                operation->gcmCounter, operation->gcmStream);
                xcryptographic_increment_counter32(operation->gcmCounter);
                operation->gcmStreamOffset = 0;
            }
            ((uint8_t*)buffer)[i] = data.m_data[i] ^ operation->gcmStream[operation->gcmStreamOffset++];
        }
        xcryptographic_aead_gcm_auth_update(operation,
            operation->encrypt ? (const uint8_t*)buffer : data.m_data, (size_t)data.m_size);
    } else {
        if (!operation->lengthsSet || !xcryptographic_aead_ccm_finish_aad(operation)) return empty;
        for (i = 0; i < (size_t)data.m_size; ++i) {
            if (operation->ccmStreamOffset == sizeof(operation->ccmStream)) {
                size_t q = 15u - operation->nonceLen;
                xcryptographic_ccm_store_length(operation->ccmCounter + 16u - q, q,
                                                operation->ccmBlockCounter++);
                xcryptographic_aead_block_crypt(&operation->blockCipher,
                                                operation->ccmCounter, operation->ccmStream);
                operation->ccmStreamOffset = 0;
            }
            ((uint8_t*)buffer)[i] = data.m_data[i] ^ operation->ccmStream[operation->ccmStreamOffset++];
        }
        if (!xcryptographic_aead_ccm_feed(operation,
                operation->encrypt ? (const uint8_t*)data.m_data : (const uint8_t*)buffer,
                (size_t)data.m_size)) return empty;
    }
    operation->dataLength += (size_t)data.m_size;
    if (result.m_size == 0 && !result.m_data) result.m_data = (const uint8_t*)"";
    return result;
}

bool XCryptographic_aeadFinishInto(XCryptographic_AeadOperation* operation,
                                   char* output, size_t outputSize, size_t* outputLength,
                                   char* tag, size_t tagSize, size_t* tagLength)
{
    uint8_t fullTag[16] = { 0 };
    uint8_t block[16] = { 0 };
    size_t i;
    (void)output;
    (void)outputSize;
    if (!operation || !operation->active || !tag || !tagLength || !outputLength ||
        tagSize < operation->tagSize ||
        (operation->lengthsSet && operation->dataLength != operation->expectedDataLength) ||
        (operation->lengthsSet && operation->expectedAssociatedDataLength != 0 &&
         operation->associatedDataLength != operation->expectedAssociatedDataLength)) return false;
    *outputLength = 0;
    if (operation->algorithm == XCryptographic_AeadAlgorithm_ChaCha20Poly1305) {
        uint8_t zero[16] = { 0 };
        if (!operation->associatedDataFinalized) {
            if ((operation->associatedDataLength & 15u) != 0)
                xcryptographic_poly1305_update(&operation->poly1305, zero,
                                               16u - (operation->associatedDataLength & 15u));
            operation->associatedDataFinalized = true;
        }
        if ((operation->dataLength & 15u) != 0)
            xcryptographic_poly1305_update(&operation->poly1305, zero,
                                           16u - (operation->dataLength & 15u));
        xcryptographic_store_u64_le(block, (uint64_t)operation->associatedDataLength);
        xcryptographic_store_u64_le(block + 8, (uint64_t)operation->dataLength);
        xcryptographic_poly1305_update(&operation->poly1305, block, sizeof(block));
        xcryptographic_poly1305_finish(&operation->poly1305, fullTag);
    } else if (operation->algorithm == XCryptographic_AeadAlgorithm_AesGcm ||
               operation->algorithm == XCryptographic_AeadAlgorithm_AriaGcm ||
               operation->algorithm == XCryptographic_AeadAlgorithm_CamelliaGcm) {
        xcryptographic_aead_gcm_auth_finish(operation);
        xcryptographic_aead_block_crypt(&operation->blockCipher,
                                        operation->gcmInitialCounter, block);
        for (i = 0; i < sizeof(fullTag); ++i) fullTag[i] = operation->gcmAuth[i] ^ block[i];
    } else {
        if (!xcryptographic_aead_ccm_finish_aad(operation)) return false;
        if (operation->ccmPendingLen != 0) {
            memset(block, 0, sizeof(block));
            memcpy(block, operation->ccmPending, operation->ccmPendingLen);
            xcryptographic_ccm_mac_block(&operation->blockCipher, operation->ccmState, block);
            operation->ccmPendingLen = 0;
        }
        memset(operation->ccmCounter, 0, sizeof(operation->ccmCounter));
        operation->ccmCounter[0] = (uint8_t)(15u - operation->nonceLen - 1u);
        memcpy(operation->ccmCounter + 1, operation->nonce, operation->nonceLen);
        xcryptographic_aead_block_crypt(&operation->blockCipher, operation->ccmCounter, block);
        for (i = 0; i < sizeof(fullTag); ++i) fullTag[i] = operation->ccmState[i] ^ block[i];
    }
    memcpy(tag, fullTag, operation->tagSize);
    *tagLength = operation->tagSize;
    memset(fullTag, 0, sizeof(fullTag));
    memset(block, 0, sizeof(block));
    return true;
}

void XCryptographic_aeadAbort(XCryptographic_AeadOperation* operation)
{
    if (operation) {
        XCryptographic_blockCipherAbort(&operation->blockCipher);
        XCryptographic_chacha20Abort(&operation->chacha20);
        memset(operation, 0, sizeof(*operation));
    }
}

XByteArrayView XCryptographic_aeadEncryptInto(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView plainText,
    size_t tagSize, char* buffer, size_t bufferSize)
{
    XByteArrayView empty = { NULL, 0 };
    size_t outputSize;
    uint8_t* tag;
    if (nonce.m_size <= 0 || associatedData.m_size < 0 || plainText.m_size < 0 ||
        (!nonce.m_data) || (!associatedData.m_data && associatedData.m_size != 0) ||
        (!plainText.m_data && plainText.m_size != 0) || tagSize > SIZE_MAX - (size_t)plainText.m_size ||
        (key.type == XCryptographic_KeyType_ChaCha20Poly1305 &&
         ((size_t)nonce.m_size != 12 || tagSize != 16)))
        return empty;
    outputSize = (size_t)plainText.m_size + tagSize;
    if (!buffer || bufferSize < outputSize) return empty;
    tag = (uint8_t*)buffer + plainText.m_size;
    if (key.type == XCryptographic_KeyType_AesGcm ||
        key.type == XCryptographic_KeyType_AriaGcm ||
        key.type == XCryptographic_KeyType_CamelliaGcm) {
        if (!xcryptographic_aes_gcm_crypt(key, nonce.m_data, (size_t)nonce.m_size,
                                          associatedData.m_data, (size_t)associatedData.m_size,
                                          plainText.m_data, (size_t)plainText.m_size,
                                          (uint8_t*)buffer, (size_t)plainText.m_size,
                                          tag, tagSize))
            return empty;
    } else if (key.type == XCryptographic_KeyType_AesCcm ||
               key.type == XCryptographic_KeyType_AriaCcm ||
               key.type == XCryptographic_KeyType_CamelliaCcm) {
        if (!xcryptographic_aes_ccm_mac(key, nonce.m_data, (size_t)nonce.m_size,
                                        associatedData.m_data, (size_t)associatedData.m_size,
                                        plainText.m_data, (size_t)plainText.m_size,
                                        tagSize, tag) ||
            !xcryptographic_aes_ccm_crypt(key, nonce.m_data, (size_t)nonce.m_size,
                                          plainText.m_data, (size_t)plainText.m_size,
                                          (uint8_t*)buffer, (size_t)plainText.m_size))
            return empty;
    } else if (key.type == XCryptographic_KeyType_ChaCha20Poly1305) {
        if (!xcryptographic_chacha20_crypt(key, nonce.m_data, plainText.m_data,
                                           (size_t)plainText.m_size,
                                           (uint8_t*)buffer, (size_t)plainText.m_size) ||
            !xcryptographic_chacha20_poly1305_tag(
                key, nonce.m_data, associatedData.m_data, (size_t)associatedData.m_size,
                (const uint8_t*)buffer, (size_t)plainText.m_size, tag))
            return empty;
    } else {
        return empty;
    }
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)outputSize };
        return result;
    }
}

XByteArray* XCryptographic_aeadEncrypt(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView plainText,
    size_t tagSize)
{
    XByteArray* result;
    XByteArrayView view;
    if (plainText.m_size < 0 || tagSize > SIZE_MAX - (size_t)plainText.m_size) return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base((XVector*)result, (size_t)plainText.m_size + tagSize)) {
        if (result) XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    view = XCryptographic_aeadEncryptInto(key, nonce, associatedData, plainText, tagSize,
                                          (char*)XByteArray_data(result),
                                          (size_t)plainText.m_size + tagSize);
    if (!view.m_data) {
        XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

XByteArrayView XCryptographic_aeadDecryptInto(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView encryptedData,
    size_t tagSize, char* buffer, size_t bufferSize)
{
    XByteArrayView empty = { NULL, 0 };
    size_t cipherTextLen;
    if (nonce.m_size <= 0 || associatedData.m_size < 0 || encryptedData.m_size < 0 ||
        !nonce.m_data || (!associatedData.m_data && associatedData.m_size != 0) ||
        (!encryptedData.m_data && encryptedData.m_size != 0) || tagSize < 4 ||
        (size_t)encryptedData.m_size < tagSize ||
        (key.type == XCryptographic_KeyType_ChaCha20Poly1305 &&
         ((size_t)nonce.m_size != 12 || tagSize != 16)))
        return empty;
    cipherTextLen = (size_t)encryptedData.m_size - tagSize;
    if (!buffer || bufferSize < cipherTextLen) return empty;
    if (key.type == XCryptographic_KeyType_AesGcm ||
        key.type == XCryptographic_KeyType_AriaGcm ||
        key.type == XCryptographic_KeyType_CamelliaGcm) {
        if (!xcryptographic_aes_gcm_authenticate(key, nonce.m_data, (size_t)nonce.m_size,
                                                 associatedData.m_data, (size_t)associatedData.m_size,
                                                 encryptedData.m_data, cipherTextLen,
                                                 encryptedData.m_data + cipherTextLen, tagSize) ||
            !xcryptographic_aes_gcm_decrypt(key, nonce.m_data, (size_t)nonce.m_size,
                                            encryptedData.m_data, cipherTextLen,
                                            (uint8_t*)buffer, bufferSize))
            return empty;
    } else if (key.type == XCryptographic_KeyType_AesCcm ||
               key.type == XCryptographic_KeyType_AriaCcm ||
               key.type == XCryptographic_KeyType_CamelliaCcm) {
        if (!xcryptographic_aes_ccm_decrypt_authenticated(
                key, nonce.m_data, (size_t)nonce.m_size,
                associatedData.m_data, (size_t)associatedData.m_size,
                encryptedData.m_data, cipherTextLen,
                encryptedData.m_data + cipherTextLen, tagSize,
                (uint8_t*)buffer, bufferSize))
            return empty;
    } else if (key.type == XCryptographic_KeyType_ChaCha20Poly1305) {
        uint8_t calculatedTag[16];
        uint8_t difference = 0;
        size_t i;
        if (!xcryptographic_chacha20_poly1305_tag(
                key, nonce.m_data, associatedData.m_data, (size_t)associatedData.m_size,
                encryptedData.m_data, cipherTextLen, calculatedTag))
            return empty;
        for (i = 0; i < sizeof(calculatedTag); ++i)
            difference |= (uint8_t)(calculatedTag[i] ^ encryptedData.m_data[cipherTextLen + i]);
        memset(calculatedTag, 0, sizeof(calculatedTag));
        if (difference != 0 || !xcryptographic_chacha20_crypt(
                key, nonce.m_data, encryptedData.m_data, cipherTextLen,
                (uint8_t*)buffer, bufferSize))
            return empty;
    } else {
        return empty;
    }
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)cipherTextLen };
        return result;
    }
}

XByteArray* XCryptographic_aeadDecrypt(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView encryptedData,
    size_t tagSize)
{
    XByteArray* result;
    XByteArrayView view;
    size_t plainTextLen;
    if (encryptedData.m_size < 0 || (size_t)encryptedData.m_size < tagSize) return NULL;
    plainTextLen = (size_t)encryptedData.m_size - tagSize;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base((XVector*)result, plainTextLen)) {
        if (result) XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    view = XCryptographic_aeadDecryptInto(key, nonce, associatedData, encryptedData, tagSize,
                                          (char*)XByteArray_data(result), plainTextLen);
    if (!view.m_data && plainTextLen != 0) {
        XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    if (!view.m_data && plainTextLen == 0) {
        char emptyOutput = 0;
        view = XCryptographic_aeadDecryptInto(key, nonce, associatedData, encryptedData, tagSize,
                                              &emptyOutput, sizeof(emptyOutput));
        if (!view.m_data) {
            XByteArray_delete_base((XClass*)result);
            return NULL;
        }
    }
    return result;
}

XByteArrayView XCryptographic_hkdfExtractInto(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    XCryptographicHash_Algorithm hashAlgorithm, char* buffer, size_t bufferSize)
{
    uint8_t zeroSalt[64] = { 0 };
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result;
    int hashLength = XCryptographicHash_hashLength(hashAlgorithm);
    if (!XCRYPTOGRAPHIC_HKDF_HMAC_ON ||
        (hashAlgorithm == XCryptographicHash_Sha256 &&
         !XCRYPTOGRAPHIC_HKDF_EXTRACT_SHA256_ON) ||
        hashLength <= 0 ||
        (size_t)hashLength > sizeof(zeroSalt) || salt.m_size < 0 ||
        inputKeyMaterial.m_size < 0 || (!salt.m_data && salt.m_size != 0) ||
        (!inputKeyMaterial.m_data && inputKeyMaterial.m_size != 0) || !buffer ||
        bufferSize < (size_t)hashLength) return empty;
    result = XCryptographicHash_hmacInto(
        buffer, bufferSize,
        (const char*)(salt.m_size == 0 ? zeroSalt : salt.m_data),
        (size_t)(salt.m_size == 0 ? hashLength : salt.m_size),
        (const char*)inputKeyMaterial.m_data, (size_t)inputKeyMaterial.m_size,
        hashAlgorithm);
    memset(zeroSalt, 0, sizeof(zeroSalt));
    if (!result.m_data || result.m_size != hashLength) return empty;
    return result;
}

XByteArrayView XCryptographic_hkdfExpandInto(
    XByteArrayView prk, XByteArrayView info,
    XCryptographicHash_Algorithm hashAlgorithm, size_t outputSize,
    char* buffer, size_t bufferSize)
{
    uint8_t previous[64] = { 0 };
    uint8_t* message = NULL;
    size_t previousSize = 0;
    size_t outputOffset = 0;
    uint8_t counter = 1;
    int hashLength = XCryptographicHash_hashLength(hashAlgorithm);
    XByteArrayView empty = { NULL, 0 };
    if (!XCRYPTOGRAPHIC_HKDF_HMAC_ON ||
        (hashAlgorithm == XCryptographicHash_Sha256 &&
         !XCRYPTOGRAPHIC_HKDF_EXPAND_SHA256_ON) ||
        hashLength <= 0 ||
        (size_t)hashLength > sizeof(previous) || prk.m_size != hashLength ||
        !prk.m_data || info.m_size < 0 || (!info.m_data && info.m_size != 0) ||
        !buffer || outputSize == 0 || outputSize > 255u * (size_t)hashLength ||
        bufferSize < outputSize || (size_t)info.m_size > SIZE_MAX - (size_t)hashLength - 1u)
        return empty;
    message = (uint8_t*)xcryptographic_hash_malloc(
        (size_t)hashLength + (size_t)info.m_size + 1u);
    if (!message) goto cleanup;
    while (outputOffset < outputSize) {
        size_t messageSize = 0;
        size_t copySize;
        if (previousSize != 0) {
            memcpy(message, previous, previousSize);
            messageSize = previousSize;
        }
        if (info.m_size != 0) {
            memcpy(message + messageSize, info.m_data, (size_t)info.m_size);
            messageSize += (size_t)info.m_size;
        }
        message[messageSize++] = counter++;
        if (!XCryptographicHash_hmacInto((char*)previous, sizeof(previous),
                                         (const char*)prk.m_data, (size_t)prk.m_size,
                                         (const char*)message, messageSize,
                                         hashAlgorithm).m_data)
            goto cleanup;
        previousSize = (size_t)hashLength;
        copySize = outputSize - outputOffset;
        if (copySize > previousSize) copySize = previousSize;
        memcpy(buffer + outputOffset, previous, copySize);
        outputOffset += copySize;
    }
    memset(previous, 0, sizeof(previous));
    memset(message, 0, (size_t)hashLength + (size_t)info.m_size + 1u);
    xcryptographic_hash_free(message);
    return XByteArrayView_create_data((const uint8_t*)buffer, (int64_t)outputSize);
cleanup:
    memset(previous, 0, sizeof(previous));
    if (message) {
        memset(message, 0, (size_t)hashLength + (size_t)info.m_size + 1u);
        xcryptographic_hash_free(message);
    }
    return empty;
}

XByteArrayView XCryptographic_hkdfInto(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    XByteArrayView info, XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize)
{
    uint8_t prk[64] = { 0 };
    int hashLength = XCryptographicHash_hashLength(hashAlgorithm);
    XByteArrayView result;
    XByteArrayView empty = { NULL, 0 };
    if (hashLength <= 0 || (size_t)hashLength > sizeof(prk)) return empty;
    result = XCryptographic_hkdfExtractInto(salt, inputKeyMaterial, hashAlgorithm,
                                            (char*)prk, sizeof(prk));
    if (!result.m_data) return empty;
    result = XCryptographic_hkdfExpandInto(
        XByteArrayView_create_data(prk, hashLength), info, hashAlgorithm,
        outputSize, buffer, bufferSize);
    memset(prk, 0, sizeof(prk));
    return result;
}


XByteArrayView XCryptographic_pbkdf2HmacInto(
    XByteArrayView password, XByteArrayView salt, uint32_t iterations,
    XCryptographicHash_Algorithm hashAlgorithm, size_t outputSize,
    char* buffer, size_t bufferSize)
{
    uint8_t saltCounter[4];
    uint8_t first[64];
    uint8_t next[64];
    uint8_t* message = NULL;
    size_t outputOffset = 0;
    uint32_t blockIndex = 1;
    int hashLength;
    XByteArrayView empty = { NULL, 0 };
    hashLength = XCryptographicHash_hashLength(hashAlgorithm);
    if (!XCRYPTOGRAPHIC_PBKDF2_HMAC_ON ||
        (hashAlgorithm == XCryptographicHash_Sha256 &&
         !XCRYPTOGRAPHIC_PBKDF2_HMAC_SHA256_ON) ||
        hashLength <= 0 ||
        (size_t)hashLength > sizeof(first) || password.m_size < 0 || salt.m_size < 0 ||
        (!password.m_data && password.m_size != 0) || (!salt.m_data && salt.m_size != 0) ||
        iterations == 0 || outputSize == 0 || !buffer || bufferSize < outputSize ||
        (size_t)salt.m_size > SIZE_MAX - sizeof(saltCounter) ||
        ((outputSize - 1u) / (size_t)hashLength) >= UINT32_MAX)
        return empty;
    message = (uint8_t*)xcryptographic_hash_malloc(
        (size_t)salt.m_size + sizeof(saltCounter));
    if (!message) goto cleanup;
    if (salt.m_size != 0) memcpy(message, salt.m_data, (size_t)salt.m_size);
    while (outputOffset < outputSize) {
        uint32_t iteration;
        size_t copySize;

        saltCounter[0] = (uint8_t)(blockIndex >> 24);
        saltCounter[1] = (uint8_t)(blockIndex >> 16);
        saltCounter[2] = (uint8_t)(blockIndex >> 8);
        saltCounter[3] = (uint8_t)blockIndex;
        memcpy(message + salt.m_size, saltCounter, sizeof(saltCounter));
        if (!XCryptographicHash_hmacInto((char*)first, (size_t)hashLength,
                                         (const char*)password.m_data, (size_t)password.m_size,
                                         (const char*)message, (size_t)salt.m_size + sizeof(saltCounter),
                                         hashAlgorithm).m_data)
            goto cleanup;
        for (iteration = 1; iteration < iterations; ++iteration) {
            size_t i;
            if (!XCryptographicHash_hmacInto((char*)next, (size_t)hashLength,
                                             (const char*)password.m_data, (size_t)password.m_size,
                                             (const char*)first, (size_t)hashLength,
                                             hashAlgorithm).m_data)
                goto cleanup;
            for (i = 0; i < (size_t)hashLength; ++i) first[i] ^= next[i];
        }
        copySize = outputSize - outputOffset;
        if (copySize > (size_t)hashLength) copySize = (size_t)hashLength;
        memcpy(buffer + outputOffset, first, copySize);
        outputOffset += copySize;
        if (blockIndex == UINT32_MAX && outputOffset < outputSize) goto cleanup;
        blockIndex++;
    }
    memset(first, 0, sizeof(first));
    memset(next, 0, sizeof(next));
    memset(message, 0, (size_t)salt.m_size + sizeof(saltCounter));
    xcryptographic_hash_free(message);
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)outputSize };
        return result;
    }
cleanup:
    memset(first, 0, sizeof(first));
    memset(next, 0, sizeof(next));
    if (message) {
        memset(message, 0, (size_t)salt.m_size + sizeof(saltCounter));
        xcryptographic_hash_free(message);
    }
    return empty;
}



XByteArrayView XCryptographic_tls12PrfInto(
    XByteArrayView secret, XByteArrayView label, XByteArrayView seed,
    XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize)
{
    uint8_t a[64] = { 0 };
    uint8_t nextA[64] = { 0 };
    uint8_t outputBlock[64] = { 0 };
    uint8_t* message = NULL;
    size_t seedSize;
    size_t messageSize;
    size_t outputOffset = 0;
    int hashLength = XCryptographicHash_hashLength(hashAlgorithm);
    XByteArrayView empty = { NULL, 0 };
    if (!XCRYPTOGRAPHIC_TLS12_PRF_HMAC_ON ||
        (hashAlgorithm == XCryptographicHash_Sha256 &&
         !XCRYPTOGRAPHIC_TLS12_PRF_SHA256_ON) ||
        hashLength <= 0 ||
        (size_t)hashLength > sizeof(a) || secret.m_size < 0 ||
        label.m_size < 0 || seed.m_size < 0 ||
        (!secret.m_data && secret.m_size != 0) ||
        (!label.m_data && label.m_size != 0) || (!seed.m_data && seed.m_size != 0) ||
        !buffer || outputSize == 0 || bufferSize < outputSize ||
        (size_t)label.m_size > SIZE_MAX - (size_t)seed.m_size)
        return empty;
    seedSize = (size_t)label.m_size + (size_t)seed.m_size;
    if (seedSize > SIZE_MAX - (size_t)hashLength) return empty;
    message = (uint8_t*)xcryptographic_hash_malloc(seedSize + (size_t)hashLength);
    if (!message) goto cleanup;
    if (label.m_size != 0) memcpy(message, label.m_data, (size_t)label.m_size);
    if (seed.m_size != 0) memcpy(message + (size_t)label.m_size, seed.m_data, (size_t)seed.m_size);
    if (!XCryptographicHash_hmacInto((char*)a, sizeof(a),
                                     (const char*)secret.m_data, (size_t)secret.m_size,
                                     (const char*)message, seedSize, hashAlgorithm).m_data)
        goto cleanup;
    while (outputOffset < outputSize) {
        size_t copySize;
        memcpy(message, a, (size_t)hashLength);
        if (label.m_size != 0) memcpy(message + (size_t)hashLength, label.m_data, (size_t)label.m_size);
        if (seed.m_size != 0) memcpy(message + (size_t)hashLength + (size_t)label.m_size,
                                     seed.m_data, (size_t)seed.m_size);
        messageSize = (size_t)hashLength + seedSize;
        if (!XCryptographicHash_hmacInto((char*)outputBlock, sizeof(outputBlock),
                                         (const char*)secret.m_data, (size_t)secret.m_size,
                                         (const char*)message, messageSize, hashAlgorithm).m_data)
            goto cleanup;
        copySize = outputSize - outputOffset;
        if (copySize > (size_t)hashLength) copySize = (size_t)hashLength;
        memcpy(buffer + outputOffset, outputBlock, copySize);
        outputOffset += copySize;
        if (outputOffset < outputSize) {
            if (!XCryptographicHash_hmacInto((char*)nextA, sizeof(nextA),
                                             (const char*)secret.m_data, (size_t)secret.m_size,
                                             (const char*)a, (size_t)hashLength,
                                             hashAlgorithm).m_data)
                goto cleanup;
            memcpy(a, nextA, (size_t)hashLength);
        }
    }
    memset(a, 0, sizeof(a));
    memset(nextA, 0, sizeof(nextA));
    memset(outputBlock, 0, sizeof(outputBlock));
    memset(message, 0, seedSize + (size_t)hashLength);
    xcryptographic_hash_free(message);
    return XByteArrayView_create_data((const uint8_t*)buffer, (int64_t)outputSize);
cleanup:
    memset(a, 0, sizeof(a));
    memset(nextA, 0, sizeof(nextA));
    memset(outputBlock, 0, sizeof(outputBlock));
    if (message) {
        memset(message, 0, seedSize + (size_t)hashLength);
        xcryptographic_hash_free(message);
    }
    return empty;
}


XByteArrayView XCryptographic_tls12PskToMsInto(
    XByteArrayView psk, XByteArrayView otherSecret,
    XByteArrayView label, XByteArrayView seed,
    XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize)
{
    uint8_t* pms = NULL;
    size_t pmsSize;
    size_t offset = 0;
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result;
    if (!XCRYPTOGRAPHIC_TLS12_PSK_TO_MS_HMAC_ON ||
        (hashAlgorithm == XCryptographicHash_Sha256 &&
         !XCRYPTOGRAPHIC_TLS12_PSK_TO_MS_SHA256_ON) ||
        psk.m_size < 0 ||
        otherSecret.m_size < 0 || label.m_size < 0 || seed.m_size < 0 ||
        (!psk.m_data && psk.m_size != 0) || (!otherSecret.m_data && otherSecret.m_size != 0) ||
        (!label.m_data && label.m_size != 0) || (!seed.m_data && seed.m_size != 0) ||
        !buffer || outputSize == 0 || bufferSize < outputSize ||
        (size_t)psk.m_size > UINT16_MAX || (size_t)otherSecret.m_size > UINT16_MAX)
        return empty;
    if (otherSecret.m_size > 0) {
        if ((size_t)otherSecret.m_size > SIZE_MAX - 4u - (size_t)psk.m_size) return empty;
        pmsSize = 4u + (size_t)otherSecret.m_size + (size_t)psk.m_size;
    } else {
        if ((size_t)psk.m_size > (SIZE_MAX - 4u) / 2u) return empty;
        pmsSize = 4u + 2u * (size_t)psk.m_size;
    }
    pms = (uint8_t*)xcryptographic_hash_malloc(pmsSize == 0 ? 1 : pmsSize);
    if (!pms) return empty;
    if (otherSecret.m_size > 0) {
        pms[offset++] = (uint8_t)((size_t)otherSecret.m_size >> 8);
        pms[offset++] = (uint8_t)(size_t)otherSecret.m_size;
        memcpy(pms + offset, otherSecret.m_data, (size_t)otherSecret.m_size);
        offset += (size_t)otherSecret.m_size;
    } else {
        pms[offset++] = (uint8_t)((size_t)psk.m_size >> 8);
        pms[offset++] = (uint8_t)(size_t)psk.m_size;
        memset(pms + offset, 0, (size_t)psk.m_size);
        offset += (size_t)psk.m_size;
    }
    pms[offset++] = (uint8_t)((size_t)psk.m_size >> 8);
    pms[offset++] = (uint8_t)(size_t)psk.m_size;
    memcpy(pms + offset, psk.m_data, (size_t)psk.m_size);
    offset += (size_t)psk.m_size;
    result = XCryptographic_tls12PrfInto(
        XByteArrayView_create_data(pms, (int64_t)offset), label, seed, hashAlgorithm,
        outputSize, buffer, bufferSize);
    memset(pms, 0, pmsSize);
    xcryptographic_hash_free(pms);
    return result;
}


XByteArrayView XCryptographic_pbkdf2AesCmacPrf128Into(
    XByteArrayView password, XByteArrayView salt, uint32_t iterations,
    size_t outputSize, char* buffer, size_t bufferSize)
{
    uint8_t saltCounter[4];
    uint8_t blockU[16];
    uint8_t nextU[16];
    uint8_t acc[16];
    uint8_t* message = NULL;
    size_t outputOffset = 0;
    uint32_t blockIndex = 1;
    XByteArrayView empty = { NULL, 0 };
    if (!XCRYPTOGRAPHIC_PBKDF2_AES_CMAC_PRF_128_ON || password.m_size < 0 ||
        salt.m_size < 0 || (!password.m_data && password.m_size != 0) ||
        (!salt.m_data && salt.m_size != 0) || iterations == 0 ||
        outputSize == 0 || !buffer || bufferSize < outputSize ||
        (size_t)salt.m_size > SIZE_MAX - sizeof(saltCounter))
        return empty;
    message = (uint8_t*)xcryptographic_hash_malloc(
        (size_t)salt.m_size + sizeof(saltCounter));
    if (!message) goto cleanup;
    if (salt.m_size != 0) memcpy(message, salt.m_data, (size_t)salt.m_size);
    while (outputOffset < outputSize) {
        uint32_t iteration;
        size_t copySize;
        size_t i;
        saltCounter[0] = (uint8_t)(blockIndex >> 24);
        saltCounter[1] = (uint8_t)(blockIndex >> 16);
        saltCounter[2] = (uint8_t)(blockIndex >> 8);
        saltCounter[3] = (uint8_t)blockIndex;
        memcpy(message + (size_t)salt.m_size, saltCounter, sizeof(saltCounter));
        if (!XCryptographic_aesCmacInto(password,
                                        XByteArrayView_create_data(message, (int64_t)((size_t)salt.m_size + sizeof(saltCounter))),
                                        (char*)blockU, sizeof(blockU)).m_data)
            goto cleanup;
        memcpy(acc, blockU, sizeof(acc));
        for (iteration = 1; iteration < iterations; ++iteration) {
            if (!XCryptographic_aesCmacInto(password,
                                            XByteArrayView_create_data(blockU, sizeof(blockU)),
                                            (char*)nextU, sizeof(nextU)).m_data)
                goto cleanup;
            for (i = 0; i < sizeof(blockU); ++i) {
                blockU[i] = nextU[i];
                acc[i] ^= nextU[i];
            }
        }
        copySize = outputSize - outputOffset;
        if (copySize > sizeof(acc)) copySize = sizeof(acc);
        memcpy(buffer + outputOffset, acc, copySize);
        memset(acc, 0, sizeof(acc));
        outputOffset += copySize;
        if (blockIndex++ == UINT32_MAX) goto cleanup;
    }
    memset(blockU, 0, sizeof(blockU));
    memset(nextU, 0, sizeof(nextU));
    memset(message, 0, (size_t)salt.m_size + sizeof(saltCounter));
    xcryptographic_hash_free(message);
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)outputSize };
        return result;
    }
cleanup:
    memset(blockU, 0, sizeof(blockU));
    memset(nextU, 0, sizeof(nextU));
    memset(acc, 0, sizeof(acc));
    if (message) {
        memset(message, 0, (size_t)salt.m_size + sizeof(saltCounter));
        xcryptographic_hash_free(message);
    }
    return empty;
}

typedef struct XCryptographic_Bn {
    uint32_t d[8];
} XCryptographic_Bn;

static void xcryptographic_bn_zero(XCryptographic_Bn* a)
{
    memset(a, 0, sizeof(*a));
}

static void xcryptographic_bn_copy(XCryptographic_Bn* dst,
                                        const XCryptographic_Bn* src)
{
    memcpy(dst, src, sizeof(*dst));
}

static int xcryptographic_bn_cmp(const XCryptographic_Bn* a,
                                      const XCryptographic_Bn* b)
{
    int i;
    for (i = 7; i >= 0; --i) {
        if (a->d[i] > b->d[i]) return 1;
        if (a->d[i] < b->d[i]) return -1;
    }
    return 0;
}

static bool xcryptographic_bn_is_zero(const XCryptographic_Bn* a)
{
    uint32_t v = 0;
    size_t i;
    for (i = 0; i < 8; ++i) v |= a->d[i];
    return v == 0;
}

static bool xcryptographic_bn_bit(const XCryptographic_Bn* a, size_t bit)
{
    return bit < 256 && ((a->d[bit / 32] >> (bit % 32)) & 1u) != 0;
}

static void xcryptographic_bn_from_be(XCryptographic_Bn* out,
                                           const uint8_t bytes[32])
{
    int i;
    for (i = 0; i < 8; ++i) {
        size_t off = (size_t)(7 - i) * 4;
        out->d[i] = ((uint32_t)bytes[off] << 24) |
                    ((uint32_t)bytes[off + 1] << 16) |
                    ((uint32_t)bytes[off + 2] << 8) | bytes[off + 3];
    }
}

static void xcryptographic_bn_to_be(const XCryptographic_Bn* in,
                                         uint8_t bytes[32])
{
    int i;
    for (i = 0; i < 8; ++i) {
        size_t off = (size_t)(7 - i) * 4;
        bytes[off] = (uint8_t)(in->d[i] >> 24);
        bytes[off + 1] = (uint8_t)(in->d[i] >> 16);
        bytes[off + 2] = (uint8_t)(in->d[i] >> 8);
        bytes[off + 3] = (uint8_t)in->d[i];
    }
}

static void xcryptographic_bn_from_le(XCryptographic_Bn* out,
                                           const uint8_t bytes[32])
{
    size_t i;
    for (i = 0; i < 8; ++i)
        out->d[i] = (uint32_t)bytes[i * 4] |
                    ((uint32_t)bytes[i * 4 + 1] << 8) |
                    ((uint32_t)bytes[i * 4 + 2] << 16) |
                    ((uint32_t)bytes[i * 4 + 3] << 24);
}

static void xcryptographic_bn_to_le(const XCryptographic_Bn* in,
                                         uint8_t bytes[32])
{
    size_t i;
    for (i = 0; i < 8; ++i) {
        bytes[i * 4] = (uint8_t)in->d[i];
        bytes[i * 4 + 1] = (uint8_t)(in->d[i] >> 8);
        bytes[i * 4 + 2] = (uint8_t)(in->d[i] >> 16);
        bytes[i * 4 + 3] = (uint8_t)(in->d[i] >> 24);
    }
}

static uint32_t xcryptographic_bn_add_raw(XCryptographic_Bn* out,
                                                const XCryptographic_Bn* a,
                                                const XCryptographic_Bn* b)
{
    uint64_t carry = 0;
    size_t i;
    for (i = 0; i < 8; ++i) {
        uint64_t sum = (uint64_t)a->d[i] + b->d[i] + carry;
        out->d[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

static void xcryptographic_bn_sub_raw(XCryptographic_Bn* out,
                                           const XCryptographic_Bn* a,
                                           const XCryptographic_Bn* b)
{
    uint64_t borrow = 0;
    size_t i;
    for (i = 0; i < 8; ++i) {
        uint32_t ai = a->d[i];
        uint64_t bi = (uint64_t)b->d[i] + borrow;
        out->d[i] = (uint32_t)((uint64_t)ai - bi);
        borrow = ((uint64_t)ai < bi) ? 1u : 0u;
    }
}

static void xcryptographic_bn_add_mod(XCryptographic_Bn* out,
                                           const XCryptographic_Bn* a,
                                           const XCryptographic_Bn* b,
                                           const XCryptographic_Bn* mod)
{
    XCryptographic_Bn temp;
    uint32_t carry = xcryptographic_bn_add_raw(&temp, a, b);
    if (carry) {
        /* 当第 257 位进位时，改为加上 2^256 - mod 来规约回绕值，
         * 避免再次减去 mod。 */
        XCryptographic_Bn correction;
        XCryptographic_Bn zero;
        xcryptographic_bn_zero(&zero);
        xcryptographic_bn_sub_raw(&correction, &zero, mod);
        (void)xcryptographic_bn_add_raw(&temp, &temp, &correction);
    } else if (xcryptographic_bn_cmp(&temp, mod) >= 0) {
        xcryptographic_bn_sub_raw(&temp, &temp, mod);
    }
    *out = temp;
}

static void xcryptographic_bn_sub_mod(XCryptographic_Bn* out,
                                           const XCryptographic_Bn* a,
                                           const XCryptographic_Bn* b,
                                           const XCryptographic_Bn* mod)
{
    if (xcryptographic_bn_cmp(a, b) >= 0) {
        xcryptographic_bn_sub_raw(out, a, b);
    } else {
        XCryptographic_Bn temp;
        xcryptographic_bn_sub_raw(&temp, mod, b);
        xcryptographic_bn_add_raw(out, a, &temp);
    }
}

static void xcryptographic_bn_mul_mod(XCryptographic_Bn* out,
                                           const XCryptographic_Bn* a,
                                           const XCryptographic_Bn* b,
                                           const XCryptographic_Bn* mod)
{
    XCryptographic_Bn result;
    XCryptographic_Bn base;
    size_t i;
    xcryptographic_bn_zero(&result);
    xcryptographic_bn_copy(&base, a);
    for (i = 0; i < 256; ++i) {
        if (xcryptographic_bn_bit(b, i)) {
            XCryptographic_Bn temp;
            xcryptographic_bn_add_mod(&temp, &result, &base, mod);
            result = temp;
        }
        {
            XCryptographic_Bn temp;
            xcryptographic_bn_add_mod(&temp, &base, &base, mod);
            base = temp;
        }
    }
    *out = result;
}

static void xcryptographic_bn_pow_mod(XCryptographic_Bn* out,
                                           const XCryptographic_Bn* base,
                                           const XCryptographic_Bn* exponent,
                                           const XCryptographic_Bn* mod)
{
    XCryptographic_Bn result;
    XCryptographic_Bn value;
    XCryptographic_Bn one;
    size_t i;
    xcryptographic_bn_zero(&one);
    one.d[0] = 1;
    result = one;
    value = *base;
    for (i = 0; i < 256; ++i) {
        if (xcryptographic_bn_bit(exponent, i)) {
            XCryptographic_Bn temp;
            xcryptographic_bn_mul_mod(&temp, &result, &value, mod);
            result = temp;
        }
        {
            XCryptographic_Bn temp;
            xcryptographic_bn_mul_mod(&temp, &value, &value, mod);
            value = temp;
        }
    }
    *out = result;
}

static void xcryptographic_bn_inverse_mod(XCryptographic_Bn* out,
                                               const XCryptographic_Bn* value,
                                               const XCryptographic_Bn* mod)
{
    XCryptographic_Bn exponent = *mod;
    XCryptographic_Bn two;
    xcryptographic_bn_zero(&two);
    two.d[0] = 2;
    xcryptographic_bn_sub_raw(&exponent, &exponent, &two);
    xcryptographic_bn_pow_mod(out, value, &exponent, mod);
}

/* P-384 使用独立的固定 12-limb 表示，避免将 256 位实现的边界扩展到
 * 不相关的算法路径。所有值均为小端 32 位 limb。 */
typedef struct XCryptographic_Bn384 {
    uint32_t d[12];
} XCryptographic_Bn384;

static void xcryptographic_bn384_zero(XCryptographic_Bn384* a)
{
    memset(a, 0, sizeof(*a));
}

static int xcryptographic_bn384_cmp(const XCryptographic_Bn384* a,
                                    const XCryptographic_Bn384* b)
{
    int i;
    for (i = 11; i >= 0; --i) {
        if (a->d[i] > b->d[i]) return 1;
        if (a->d[i] < b->d[i]) return -1;
    }
    return 0;
}

static bool xcryptographic_bn384_is_zero(const XCryptographic_Bn384* a)
{
    uint32_t value = 0;
    size_t i;
    for (i = 0; i < 12; ++i) value |= a->d[i];
    return value == 0;
}

static bool xcryptographic_bn384_bit(const XCryptographic_Bn384* a, size_t bit)
{
    return bit < 384 && ((a->d[bit / 32] >> (bit % 32)) & 1u) != 0;
}

static void xcryptographic_bn384_from_be(XCryptographic_Bn384* out,
                                         const uint8_t bytes[48])
{
    int i;
    for (i = 0; i < 12; ++i) {
        size_t off = (size_t)(11 - i) * 4;
        out->d[i] = ((uint32_t)bytes[off] << 24) |
                    ((uint32_t)bytes[off + 1] << 16) |
                    ((uint32_t)bytes[off + 2] << 8) | bytes[off + 3];
    }
}

static void xcryptographic_bn384_to_be(const XCryptographic_Bn384* in,
                                       uint8_t bytes[48])
{
    int i;
    for (i = 0; i < 12; ++i) {
        size_t off = (size_t)(11 - i) * 4;
        bytes[off] = (uint8_t)(in->d[i] >> 24);
        bytes[off + 1] = (uint8_t)(in->d[i] >> 16);
        bytes[off + 2] = (uint8_t)(in->d[i] >> 8);
        bytes[off + 3] = (uint8_t)in->d[i];
    }
}

static uint32_t xcryptographic_bn384_add_raw(XCryptographic_Bn384* out,
                                              const XCryptographic_Bn384* a,
                                              const XCryptographic_Bn384* b)
{
    uint64_t carry = 0;
    size_t i;
    for (i = 0; i < 12; ++i) {
        uint64_t sum = (uint64_t)a->d[i] + b->d[i] + carry;
        out->d[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

static void xcryptographic_bn384_sub_raw(XCryptographic_Bn384* out,
                                         const XCryptographic_Bn384* a,
                                         const XCryptographic_Bn384* b)
{
    uint64_t borrow = 0;
    size_t i;
    for (i = 0; i < 12; ++i) {
        uint32_t ai = a->d[i];
        uint64_t bi = (uint64_t)b->d[i] + borrow;
        out->d[i] = (uint32_t)((uint64_t)ai - bi);
        borrow = ((uint64_t)ai < bi) ? 1u : 0u;
    }
}

static void xcryptographic_bn384_add_mod(XCryptographic_Bn384* out,
                                         const XCryptographic_Bn384* a,
                                         const XCryptographic_Bn384* b,
                                         const XCryptographic_Bn384* mod)
{
    XCryptographic_Bn384 temp;
    if (xcryptographic_bn384_add_raw(&temp, a, b)) {
        /* a+b 落在 [2^384, 2p)，因此回绕值加上 2^384-mod 恰好等于
         * a+b-mod，且已经小于 p。 */
        XCryptographic_Bn384 correction, zero;
        xcryptographic_bn384_zero(&zero);
        xcryptographic_bn384_sub_raw(&correction, &zero, mod);
        (void)xcryptographic_bn384_add_raw(&temp, &temp, &correction);
    } else if (xcryptographic_bn384_cmp(&temp, mod) >= 0) {
        xcryptographic_bn384_sub_raw(&temp, &temp, mod);
    }
    *out = temp;
}

static void xcryptographic_bn384_sub_mod(XCryptographic_Bn384* out,
                                         const XCryptographic_Bn384* a,
                                         const XCryptographic_Bn384* b,
                                         const XCryptographic_Bn384* mod)
{
    if (xcryptographic_bn384_cmp(a, b) >= 0) {
        xcryptographic_bn384_sub_raw(out, a, b);
    } else {
        XCryptographic_Bn384 temp;
        xcryptographic_bn384_sub_raw(&temp, mod, b);
        (void)xcryptographic_bn384_add_raw(out, a, &temp);
    }
}

static void xcryptographic_bn384_mul_mod(XCryptographic_Bn384* out,
                                         const XCryptographic_Bn384* a,
                                         const XCryptographic_Bn384* b,
                                         const XCryptographic_Bn384* mod)
{
    XCryptographic_Bn384 result;
    XCryptographic_Bn384 base = *a;
    size_t i;
    xcryptographic_bn384_zero(&result);
    for (i = 0; i < 384; ++i) {
        if (xcryptographic_bn384_bit(b, i)) {
            XCryptographic_Bn384 temp;
            xcryptographic_bn384_add_mod(&temp, &result, &base, mod);
            result = temp;
        }
        {
            XCryptographic_Bn384 temp;
            xcryptographic_bn384_add_mod(&temp, &base, &base, mod);
            base = temp;
        }
    }
    *out = result;
}

static void xcryptographic_bn384_pow_mod(XCryptographic_Bn384* out,
                                         const XCryptographic_Bn384* base,
                                         const XCryptographic_Bn384* exponent,
                                         const XCryptographic_Bn384* mod)
{
    XCryptographic_Bn384 result;
    XCryptographic_Bn384 value = *base;
    size_t i;
    xcryptographic_bn384_zero(&result);
    result.d[0] = 1;
    for (i = 0; i < 384; ++i) {
        if (xcryptographic_bn384_bit(exponent, i)) {
            XCryptographic_Bn384 temp;
            xcryptographic_bn384_mul_mod(&temp, &result, &value, mod);
            result = temp;
        }
        {
            XCryptographic_Bn384 temp;
            xcryptographic_bn384_mul_mod(&temp, &value, &value, mod);
            value = temp;
        }
    }
    *out = result;
}

static void xcryptographic_bn384_inverse_mod(XCryptographic_Bn384* out,
                                             const XCryptographic_Bn384* value,
                                             const XCryptographic_Bn384* mod)
{
    XCryptographic_Bn384 exponent = *mod;
    XCryptographic_Bn384 two;
    xcryptographic_bn384_zero(&two);
    two.d[0] = 2;
    xcryptographic_bn384_sub_raw(&exponent, &exponent, &two);
    xcryptographic_bn384_pow_mod(out, value, &exponent, mod);
}

/* P-521 使用 17 个 limb；其 66 字节外部格式在首字节只有一个有效位，
 * 因此不能复用按字对齐的 P-256/P-384 编解码器。 */
typedef struct XCryptographic_Bn521 {
    uint32_t d[17];
} XCryptographic_Bn521;

static void xcryptographic_bn521_zero(XCryptographic_Bn521* a)
{
    memset(a, 0, sizeof(*a));
}

static int xcryptographic_bn521_cmp(const XCryptographic_Bn521* a,
                                    const XCryptographic_Bn521* b)
{
    int i;
    for (i = 16; i >= 0; --i) {
        if (a->d[i] > b->d[i]) return 1;
        if (a->d[i] < b->d[i]) return -1;
    }
    return 0;
}

static bool xcryptographic_bn521_is_zero(const XCryptographic_Bn521* a)
{
    uint32_t value = 0;
    size_t i;
    for (i = 0; i < 17; ++i) value |= a->d[i];
    return value == 0;
}

static bool xcryptographic_bn521_bit(const XCryptographic_Bn521* a, size_t bit)
{
    return bit < 521 && ((a->d[bit / 32] >> (bit % 32)) & 1u) != 0;
}

static void xcryptographic_bn521_from_be(XCryptographic_Bn521* out,
                                         const uint8_t bytes[66])
{
    size_t i;
    xcryptographic_bn521_zero(out);
    for (i = 0; i < 66; ++i) {
        size_t position = 65 - i;
        out->d[position / 4] |= (uint32_t)bytes[i] << ((position % 4) * 8);
    }
}

static void xcryptographic_bn521_to_be(const XCryptographic_Bn521* in,
                                       uint8_t bytes[66])
{
    size_t i;
    for (i = 0; i < 66; ++i) {
        size_t position = 65 - i;
        bytes[i] = (uint8_t)(in->d[position / 4] >> ((position % 4) * 8));
    }
}

static void xcryptographic_bn521_from_be64(XCryptographic_Bn521* out,
                                           const uint8_t bytes[64])
{
    size_t i;
    xcryptographic_bn521_zero(out);
    for (i = 0; i < 64; ++i) {
        size_t position = 63 - i;
        out->d[position / 4] |= (uint32_t)bytes[i] << ((position % 4) * 8);
    }
}

static void xcryptographic_bn521_to_be64(const XCryptographic_Bn521* in,
                                         uint8_t bytes[64])
{
    size_t i;
    for (i = 0; i < 64; ++i) {
        size_t position = 63 - i;
        bytes[i] = (uint8_t)(in->d[position / 4] >> ((position % 4) * 8));
    }
}

static uint32_t xcryptographic_bn521_add_raw(XCryptographic_Bn521* out,
                                              const XCryptographic_Bn521* a,
                                              const XCryptographic_Bn521* b)
{
    uint64_t carry = 0;
    size_t i;
    for (i = 0; i < 17; ++i) {
        uint64_t sum = (uint64_t)a->d[i] + b->d[i] + carry;
        out->d[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

static void xcryptographic_bn521_sub_raw(XCryptographic_Bn521* out,
                                         const XCryptographic_Bn521* a,
                                         const XCryptographic_Bn521* b)
{
    uint64_t borrow = 0;
    size_t i;
    for (i = 0; i < 17; ++i) {
        uint32_t ai = a->d[i];
        uint64_t bi = (uint64_t)b->d[i] + borrow;
        out->d[i] = (uint32_t)((uint64_t)ai - bi);
        borrow = ((uint64_t)ai < bi) ? 1u : 0u;
    }
}

static void xcryptographic_bn521_add_mod(XCryptographic_Bn521* out,
                                         const XCryptographic_Bn521* a,
                                         const XCryptographic_Bn521* b,
                                         const XCryptographic_Bn521* mod)
{
    XCryptographic_Bn521 temp;
    (void)xcryptographic_bn521_add_raw(&temp, a, b);
    if (xcryptographic_bn521_cmp(&temp, mod) >= 0)
        xcryptographic_bn521_sub_raw(&temp, &temp, mod);
    *out = temp;
}

static void xcryptographic_bn521_sub_mod(XCryptographic_Bn521* out,
                                         const XCryptographic_Bn521* a,
                                         const XCryptographic_Bn521* b,
                                         const XCryptographic_Bn521* mod)
{
    if (xcryptographic_bn521_cmp(a, b) >= 0) {
        xcryptographic_bn521_sub_raw(out, a, b);
    } else {
        XCryptographic_Bn521 temp;
        xcryptographic_bn521_sub_raw(&temp, mod, b);
        (void)xcryptographic_bn521_add_raw(out, a, &temp);
    }
}

static void xcryptographic_bn521_mul_mod(XCryptographic_Bn521* out,
                                         const XCryptographic_Bn521* a,
                                         const XCryptographic_Bn521* b,
                                         const XCryptographic_Bn521* mod)
{
    XCryptographic_Bn521 result;
    XCryptographic_Bn521 base = *a;
    size_t i;
    xcryptographic_bn521_zero(&result);
    for (i = 0; i < 521; ++i) {
        if (xcryptographic_bn521_bit(b, i)) {
            XCryptographic_Bn521 temp;
            xcryptographic_bn521_add_mod(&temp, &result, &base, mod);
            result = temp;
        }
        {
            XCryptographic_Bn521 temp;
            xcryptographic_bn521_add_mod(&temp, &base, &base, mod);
            base = temp;
        }
    }
    *out = result;
}

static void xcryptographic_bn521_pow_mod(XCryptographic_Bn521* out,
                                         const XCryptographic_Bn521* base,
                                         const XCryptographic_Bn521* exponent,
                                         const XCryptographic_Bn521* mod)
{
    XCryptographic_Bn521 result;
    XCryptographic_Bn521 value = *base;
    size_t i;
    xcryptographic_bn521_zero(&result);
    result.d[0] = 1;
    for (i = 0; i < 521; ++i) {
        if (xcryptographic_bn521_bit(exponent, i)) {
            XCryptographic_Bn521 temp;
            xcryptographic_bn521_mul_mod(&temp, &result, &value, mod);
            result = temp;
        }
        {
            XCryptographic_Bn521 temp;
            xcryptographic_bn521_mul_mod(&temp, &value, &value, mod);
            value = temp;
        }
    }
    *out = result;
}

static void xcryptographic_bn521_inverse_mod(XCryptographic_Bn521* out,
                                             const XCryptographic_Bn521* value,
                                             const XCryptographic_Bn521* mod)
{
    XCryptographic_Bn521 exponent = *mod;
    XCryptographic_Bn521 two;
    xcryptographic_bn521_zero(&two);
    two.d[0] = 2;
    xcryptographic_bn521_sub_raw(&exponent, &exponent, &two);
    xcryptographic_bn521_pow_mod(out, value, &exponent, mod);
}

static const uint8_t xcryptographic_p256_prime_bytes[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};
static const uint8_t xcryptographic_p256_order_bytes[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51
};
static const uint8_t xcryptographic_p256_b_bytes[32] = {
    0x5a,0xc6,0x35,0xd8,0xaa,0x3a,0x93,0xe7,0xb3,0xeb,0xbd,0x55,0x76,0x98,0x86,0xbc,
    0x65,0x1d,0x06,0xb0,0xcc,0x53,0xb0,0xf6,0x3b,0xce,0x3c,0x3e,0x27,0xd2,0x60,0x4b
};
static const uint8_t xcryptographic_p256_gx_bytes[32] = {
    0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
    0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96
};
static const uint8_t xcryptographic_p256_gy_bytes[32] = {
    0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
    0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5
};
static const uint8_t xcryptographic_secp256k1_prime_bytes[32] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,0xff,0xff,0xfc,0x2f
};
static const uint8_t xcryptographic_secp256k1_order_bytes[32] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,
    0xba,0xae,0xdc,0xe6,0xaf,0x48,0xa0,0x3b,0xbf,0xd2,0x5e,0x8c,0xd0,0x36,0x41,0x41
};
static const uint8_t xcryptographic_secp256k1_gx_bytes[32] = {
    0x79,0xbe,0x66,0x7e,0xf9,0xdc,0xbb,0xac,0x55,0xa0,0x62,0x95,0xce,0x87,0x0b,0x07,
    0x02,0x9b,0xfc,0xdb,0x2d,0xce,0x28,0xd9,0x59,0xf2,0x81,0x5b,0x16,0xf8,0x17,0x98
};
static const uint8_t xcryptographic_secp256k1_gy_bytes[32] = {
    0x48,0x3a,0xda,0x77,0x26,0xa3,0xc4,0x65,0x5d,0xa4,0xfb,0xfc,0x0e,0x11,0x08,0xa8,
    0xfd,0x17,0xb4,0x48,0xa6,0x85,0x54,0x19,0x9c,0x47,0xd0,0x8f,0xfb,0x10,0xd4,0xb8
};
static const uint8_t xcryptographic_brainpool_p256r1_prime_bytes[32] = {
    0xa9,0xfb,0x57,0xdb,0xa1,0xee,0xa9,0xbc,0x3e,0x66,0x0a,0x90,0x9d,0x83,0x8d,0x72,
    0x6e,0x3b,0xf6,0x23,0xd5,0x26,0x20,0x28,0x20,0x13,0x48,0x1d,0x1f,0x6e,0x53,0x77
};
static const uint8_t xcryptographic_brainpool_p256r1_a_bytes[32] = {
    0x7d,0x5a,0x09,0x75,0xfc,0x2c,0x30,0x57,0xee,0xf6,0x75,0x30,0x41,0x7a,0xff,0xe7,
    0xfb,0x80,0x55,0xc1,0x26,0xdc,0x5c,0x6c,0xe9,0x4a,0x4b,0x44,0xf3,0x30,0xb5,0xd9
};
static const uint8_t xcryptographic_brainpool_p256r1_b_bytes[32] = {
    0x26,0xdc,0x5c,0x6c,0xe9,0x4a,0x4b,0x44,0xf3,0x30,0xb5,0xd9,0xbb,0xd7,0x7c,0xbf,
    0x95,0x84,0x16,0x29,0x5c,0xf7,0xe1,0xce,0x6b,0xcc,0xdc,0x18,0xff,0x8c,0x07,0xb6
};
static const uint8_t xcryptographic_brainpool_p256r1_order_bytes[32] = {
    0xa9,0xfb,0x57,0xdb,0xa1,0xee,0xa9,0xbc,0x3e,0x66,0x0a,0x90,0x9d,0x83,0x8d,0x71,
    0x8c,0x39,0x7a,0xa3,0xb5,0x61,0xa6,0xf7,0x90,0x1e,0x0e,0x82,0x97,0x48,0x56,0xa7
};
static const uint8_t xcryptographic_brainpool_p256r1_gx_bytes[32] = {
    0x8b,0xd2,0xae,0xb9,0xcb,0x7e,0x57,0xcb,0x2c,0x4b,0x48,0x2f,0xfc,0x81,0xb7,0xaf,
    0xb9,0xde,0x27,0xe1,0xe3,0xbd,0x23,0xc2,0x3a,0x44,0x53,0xbd,0x9a,0xce,0x32,0x62
};
static const uint8_t xcryptographic_brainpool_p256r1_gy_bytes[32] = {
    0x54,0x7e,0xf8,0x35,0xc3,0xda,0xc4,0xfd,0x97,0xf8,0x46,0x1a,0x14,0x61,0x1d,0xc9,
    0xc2,0x77,0x45,0x13,0x2d,0xed,0x8e,0x54,0x5c,0x1d,0x54,0xc7,0x2f,0x04,0x69,0x97
};
static const uint8_t xcryptographic_p384_prime_bytes[48] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xfe,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff
};
static const uint8_t xcryptographic_p384_order_bytes[48] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xc7,0x63,0x4d,0x81,0xf4,0x37,0x2d,0xdf,
    0x58,0x1a,0x0d,0xb2,0x48,0xb0,0xa7,0x7a,0xec,0xec,0x19,0x6a,0xcc,0xc5,0x29,0x73
};
static const uint8_t xcryptographic_p384_b_bytes[48] = {
    0xb3,0x31,0x2f,0xa7,0xe2,0x3e,0xe7,0xe4,0x98,0x8e,0x05,0x6b,0xe3,0xf8,0x2d,0x19,
    0x18,0x1d,0x9c,0x6e,0xfe,0x81,0x41,0x12,0x03,0x14,0x08,0x8f,0x50,0x13,0x87,0x5a,
    0xc6,0x56,0x39,0x8d,0x8a,0x2e,0xd1,0x9d,0x2a,0x85,0xc8,0xed,0xd3,0xec,0x2a,0xef
};
static const uint8_t xcryptographic_p384_gx_bytes[48] = {
    0xaa,0x87,0xca,0x22,0xbe,0x8b,0x05,0x37,0x8e,0xb1,0xc7,0x1e,0xf3,0x20,0xad,0x74,
    0x6e,0x1d,0x3b,0x62,0x8b,0xa7,0x9b,0x98,0x59,0xf7,0x41,0xe0,0x82,0x54,0x2a,0x38,
    0x55,0x02,0xf2,0x5d,0xbf,0x55,0x29,0x6c,0x3a,0x54,0x5e,0x38,0x72,0x76,0x0a,0xb7
};
static const uint8_t xcryptographic_p384_gy_bytes[48] = {
    0x36,0x17,0xde,0x4a,0x96,0x26,0x2c,0x6f,0x5d,0x9e,0x98,0xbf,0x92,0x92,0xdc,0x29,
    0xf8,0xf4,0x1d,0xbd,0x28,0x9a,0x14,0x7c,0xe9,0xda,0x31,0x13,0xb5,0xf0,0xb8,0xc0,
    0x0a,0x60,0xb1,0xce,0x1d,0x7e,0x81,0x9d,0x7a,0x43,0x1d,0x7c,0x90,0xea,0x0e,0x5f
};
static const uint8_t xcryptographic_brainpool_p384r1_prime_bytes[48] = {
    0x8c,0xb9,0x1e,0x82,0xa3,0x38,0x6d,0x28,0x0f,0x5d,0x6f,0x7e,0x50,0xe6,0x41,0xdf,
    0x15,0x2f,0x71,0x09,0xed,0x54,0x56,0xb4,0x12,0xb1,0xda,0x19,0x7f,0xb7,0x11,0x23,
    0xac,0xd3,0xa7,0x29,0x90,0x1d,0x1a,0x71,0x87,0x47,0x00,0x13,0x31,0x07,0xec,0x53
};
static const uint8_t xcryptographic_brainpool_p384r1_a_bytes[48] = {
    0x7b,0xc3,0x82,0xc6,0x3d,0x8c,0x15,0x0c,0x3c,0x72,0x08,0x0a,0xce,0x05,0xaf,0xa0,
    0xc2,0xbe,0xa2,0x8e,0x4f,0xb2,0x27,0x87,0x13,0x91,0x65,0xef,0xba,0x91,0xf9,0x0f,
    0x8a,0xa5,0x81,0x4a,0x50,0x3a,0xd4,0xeb,0x04,0xa8,0xc7,0xdd,0x22,0xce,0x28,0x26
};
static const uint8_t xcryptographic_brainpool_p384r1_b_bytes[48] = {
    0x04,0xa8,0xc7,0xdd,0x22,0xce,0x28,0x26,0x8b,0x39,0xb5,0x54,0x16,0xf0,0x44,0x7c,
    0x2f,0xb7,0x7d,0xe1,0x07,0xdc,0xd2,0xa6,0x2e,0x88,0x0e,0xa5,0x3e,0xeb,0x62,0xd5,
    0x7c,0xb4,0x39,0x02,0x95,0xdb,0xc9,0x94,0x3a,0xb7,0x86,0x96,0xfa,0x50,0x4c,0x11
};
static const uint8_t xcryptographic_brainpool_p384r1_order_bytes[48] = {
    0x8c,0xb9,0x1e,0x82,0xa3,0x38,0x6d,0x28,0x0f,0x5d,0x6f,0x7e,0x50,0xe6,0x41,0xdf,
    0x15,0x2f,0x71,0x09,0xed,0x54,0x56,0xb3,0x1f,0x16,0x6e,0x6c,0xac,0x04,0x25,0xa7,
    0xcf,0x3a,0xb6,0xaf,0x6b,0x7f,0xc3,0x10,0x3b,0x88,0x32,0x02,0xe9,0x04,0x65,0x65
};
static const uint8_t xcryptographic_brainpool_p384r1_gx_bytes[48] = {
    0x1d,0x1c,0x64,0xf0,0x68,0xcf,0x45,0xff,0xa2,0xa6,0x3a,0x81,0xb7,0xc1,0x3f,0x6b,
    0x88,0x47,0xa3,0xe7,0x7e,0xf1,0x4f,0xe3,0xdb,0x7f,0xca,0xfe,0x0c,0xbd,0x10,0xe8,
    0xe8,0x26,0xe0,0x34,0x36,0xd6,0x46,0xaa,0xef,0x87,0xb2,0xe2,0x47,0xd4,0xaf,0x1e
};
static const uint8_t xcryptographic_brainpool_p384r1_gy_bytes[48] = {
    0x8a,0xbe,0x1d,0x75,0x20,0xf9,0xc2,0xa4,0x5c,0xb1,0xeb,0x8e,0x95,0xcf,0xd5,0x52,
    0x62,0xb7,0x0b,0x29,0xfe,0xec,0x58,0x64,0xe1,0x9c,0x05,0x4f,0xf9,0x91,0x29,0x28,
    0x0e,0x46,0x46,0x21,0x77,0x91,0x81,0x11,0x42,0x82,0x03,0x41,0x26,0x3c,0x53,0x15
};
static const uint8_t xcryptographic_brainpool_p512r1_prime_bytes[64] = {
    0xaa,0xdd,0x9d,0xb8,0xdb,0xe9,0xc4,0x8b,0x3f,0xd4,0xe6,0xae,0x33,0xc9,0xfc,0x07,
    0xcb,0x30,0x8d,0xb3,0xb3,0xc9,0xd2,0x0e,0xd6,0x63,0x9c,0xca,0x70,0x33,0x08,0x71,
    0x7d,0x4d,0x9b,0x00,0x9b,0xc6,0x68,0x42,0xae,0xcd,0xa1,0x2a,0xe6,0xa3,0x80,0xe6,
    0x28,0x81,0xff,0x2f,0x2d,0x82,0xc6,0x85,0x28,0xaa,0x60,0x56,0x58,0x3a,0x48,0xf3
};
static const uint8_t xcryptographic_brainpool_p512r1_a_bytes[64] = {
    0x78,0x30,0xa3,0x31,0x8b,0x60,0x3b,0x89,0xe2,0x32,0x71,0x45,0xac,0x23,0x4c,0xc5,
    0x94,0xcb,0xdd,0x8d,0x3d,0xf9,0x16,0x10,0xa8,0x34,0x41,0xca,0xea,0x98,0x63,0xbc,
    0x2d,0xed,0x5d,0x5a,0xa8,0x25,0x3a,0xa1,0x0a,0x2e,0xf1,0xc9,0x8b,0x9a,0xc8,0xb5,
    0x7f,0x11,0x17,0xa7,0x2b,0xf2,0xc7,0xb9,0xe7,0xc1,0xac,0x4d,0x77,0xfc,0x94,0xca
};
static const uint8_t xcryptographic_brainpool_p512r1_b_bytes[64] = {
    0x3d,0xf9,0x16,0x10,0xa8,0x34,0x41,0xca,0xea,0x98,0x63,0xbc,0x2d,0xed,0x5d,0x5a,
    0xa8,0x25,0x3a,0xa1,0x0a,0x2e,0xf1,0xc9,0x8b,0x9a,0xc8,0xb5,0x7f,0x11,0x17,0xa7,
    0x2b,0xf2,0xc7,0xb9,0xe7,0xc1,0xac,0x4d,0x77,0xfc,0x94,0xca,0xdc,0x08,0x3e,0x67,
    0x98,0x40,0x50,0xb7,0x5e,0xba,0xe5,0xdd,0x28,0x09,0xbd,0x63,0x80,0x16,0xf7,0x23
};
static const uint8_t xcryptographic_brainpool_p512r1_order_bytes[64] = {
    0xaa,0xdd,0x9d,0xb8,0xdb,0xe9,0xc4,0x8b,0x3f,0xd4,0xe6,0xae,0x33,0xc9,0xfc,0x07,
    0xcb,0x30,0x8d,0xb3,0xb3,0xc9,0xd2,0x0e,0xd6,0x63,0x9c,0xca,0x70,0x33,0x08,0x70,
    0x55,0x3e,0x5c,0x41,0x4c,0xa9,0x26,0x19,0x41,0x86,0x61,0x19,0x7f,0xac,0x10,0x47,
    0x1d,0xb1,0xd3,0x81,0x08,0x5d,0xda,0xdd,0xb5,0x87,0x96,0x82,0x9c,0xa9,0x00,0x69
};
static const uint8_t xcryptographic_brainpool_p512r1_gx_bytes[64] = {
    0x81,0xae,0xe4,0xbd,0xd8,0x2e,0xd9,0x64,0x5a,0x21,0x32,0x2e,0x9c,0x4c,0x6a,0x93,
    0x85,0xed,0x9f,0x70,0xb5,0xd9,0x16,0xc1,0xb4,0x3b,0x62,0xee,0xf4,0xd0,0x09,0x8e,
    0xff,0x3b,0x1f,0x78,0xe2,0xd0,0xd4,0x8d,0x50,0xd1,0x68,0x7b,0x93,0xb9,0x7d,0x5f,
    0x7c,0x6d,0x50,0x47,0x40,0x6a,0x5e,0x68,0x8b,0x35,0x22,0x09,0xbc,0xb9,0xf8,0x22
};
static const uint8_t xcryptographic_brainpool_p512r1_gy_bytes[64] = {
    0x7d,0xde,0x38,0x5d,0x56,0x63,0x32,0xec,0xc0,0xea,0xbf,0xa9,0xcf,0x78,0x22,0xfd,
    0xf2,0x09,0xf7,0x00,0x24,0xa5,0x7b,0x1a,0xa0,0x00,0xc5,0x5b,0x88,0x1f,0x81,0x11,
    0xb2,0xdc,0xde,0x49,0x4a,0x5f,0x48,0x5e,0x5b,0xca,0x4b,0xd8,0x8a,0x27,0x63,0xae,
    0xd1,0xca,0x2b,0x2f,0xa8,0xf0,0x54,0x06,0x78,0xcd,0x1e,0x0f,0x3a,0xd8,0x08,0x92
};
static const uint8_t xcryptographic_p521_p_bytes[66] = {
    0x01,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff
};
static const uint8_t xcryptographic_p521_n_bytes[66] = {
    0x01,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xfa,0x51,0x86,0x87,0x83,0xbf,0x2f,0x96,0x6b,0x7f,0xcc,0x01,0x48,0xf7,0x09,
    0xa5,0xd0,0x3b,0xb5,0xc9,0xb8,0x89,0x9c,0x47,0xae,0xbb,0x6f,0xb7,0x1e,0x91,0x38,
    0x64,0x09
};
static const uint8_t xcryptographic_p521_b_bytes[66] = {
    0x00,0x51,0x95,0x3e,0xb9,0x61,0x8e,0x1c,0x9a,0x1f,0x92,0x9a,0x21,0xa0,0xb6,0x85,
    0x40,0xee,0xa2,0xda,0x72,0x5b,0x99,0xb3,0x15,0xf3,0xb8,0xb4,0x89,0x91,0x8e,0xf1,
    0x09,0xe1,0x56,0x19,0x39,0x51,0xec,0x7e,0x93,0x7b,0x16,0x52,0xc0,0xbd,0x3b,0xb1,
    0xbf,0x07,0x35,0x73,0xdf,0x88,0x3d,0x2c,0x34,0xf1,0xef,0x45,0x1f,0xd4,0x6b,0x50,
    0x3f,0x00
};
static const uint8_t xcryptographic_p521_gx_bytes[66] = {
    0x00,0xc6,0x85,0x8e,0x06,0xb7,0x04,0x04,0xe9,0xcd,0x9e,0x3e,0xcb,0x66,0x23,0x95,
    0xb4,0x42,0x9c,0x64,0x81,0x39,0x05,0x3f,0xb5,0x21,0xf8,0x28,0xaf,0x60,0x6b,0x4d,
    0x3d,0xba,0xa1,0x4b,0x5e,0x77,0xef,0xe7,0x59,0x28,0xfe,0x1d,0xc1,0x27,0xa2,0xff,
    0xa8,0xde,0x33,0x48,0xb3,0xc1,0x85,0x6a,0x42,0x9b,0xf9,0x7e,0x7e,0x31,0xc2,0xe5,
    0xbd,0x66
};
static const uint8_t xcryptographic_p521_gy_bytes[66] = {
    0x01,0x18,0x39,0x29,0x6a,0x78,0x9a,0x3b,0xc0,0x04,0x5c,0x8a,0x5f,0xb4,0x2c,0x7d,
    0x1b,0xd9,0x98,0xf5,0x44,0x49,0x57,0x9b,0x44,0x68,0x17,0xaf,0xbd,0x17,0x27,0x3e,
    0x66,0x2c,0x97,0xee,0x72,0x99,0x5e,0xf4,0x26,0x40,0xc5,0x50,0xb9,0x01,0x3f,0xad,
    0x07,0x61,0x35,0x3c,0x70,0x86,0xa2,0x72,0xc2,0x40,0x88,0xbe,0x94,0x76,0x9f,0xd1,
    0x66,0x50
};

static void xcryptographic_bn_const(XCryptographic_Bn* out,
                                         const uint8_t bytes[32])
{
    xcryptographic_bn_from_be(out, bytes);
}

static void xcryptographic_bn384_const(XCryptographic_Bn384* out,
                                       const uint8_t bytes[48])
{
    xcryptographic_bn384_from_be(out, bytes);
}

static void xcryptographic_bn521_const(XCryptographic_Bn521* out,
                                       const uint8_t bytes[66])
{
    xcryptographic_bn521_from_be(out, bytes);
}

static void xcryptographic_bn521_const64(XCryptographic_Bn521* out,
                                         const uint8_t bytes[64])
{
    xcryptographic_bn521_from_be64(out, bytes);
}

typedef struct XCryptographic_EcPoint {
    XCryptographic_Bn x;
    XCryptographic_Bn y;
    XCryptographic_Bn z;
    bool infinity;
} XCryptographic_EcPoint;

typedef struct XCryptographic_EcPoint384 {
    XCryptographic_Bn384 x;
    XCryptographic_Bn384 y;
    XCryptographic_Bn384 z;
    bool infinity;
} XCryptographic_EcPoint384;

typedef struct XCryptographic_EcPointBrainpool384 {
    XCryptographic_Bn384 x;
    XCryptographic_Bn384 y;
    XCryptographic_Bn384 z;
    bool infinity;
} XCryptographic_EcPointBrainpool384;

typedef struct XCryptographic_EcPointBrainpool512 {
    XCryptographic_Bn521 x;
    XCryptographic_Bn521 y;
    XCryptographic_Bn521 z;
    bool infinity;
} XCryptographic_EcPointBrainpool512;

typedef struct XCryptographic_EcPoint521 {
    XCryptographic_Bn521 x;
    XCryptographic_Bn521 y;
    XCryptographic_Bn521 z;
    bool infinity;
} XCryptographic_EcPoint521;

static void xcryptographic_p256_modulus(XCryptographic_Bn* out)
{
    xcryptographic_bn_const(out, xcryptographic_p256_prime_bytes);
}

static void xcryptographic_p256_order(XCryptographic_Bn* out)
{
    xcryptographic_bn_const(out, xcryptographic_p256_order_bytes);
}

static void xcryptographic_secp256k1_modulus(XCryptographic_Bn* out)
{
    xcryptographic_bn_const(out, xcryptographic_secp256k1_prime_bytes);
}

static void xcryptographic_secp256k1_order(XCryptographic_Bn* out)
{
    xcryptographic_bn_const(out, xcryptographic_secp256k1_order_bytes);
}

static void xcryptographic_brainpool_p256r1_modulus(XCryptographic_Bn* out)
{
    xcryptographic_bn_const(out, xcryptographic_brainpool_p256r1_prime_bytes);
}

static void xcryptographic_brainpool_p256r1_order(XCryptographic_Bn* out)
{
    xcryptographic_bn_const(out, xcryptographic_brainpool_p256r1_order_bytes);
}

static void xcryptographic_p384_modulus(XCryptographic_Bn384* out)
{
    xcryptographic_bn384_const(out, xcryptographic_p384_prime_bytes);
}

static void xcryptographic_p384_order(XCryptographic_Bn384* out)
{
    xcryptographic_bn384_const(out, xcryptographic_p384_order_bytes);
}

static void xcryptographic_brainpool_p384r1_modulus(XCryptographic_Bn384* out)
{ xcryptographic_bn384_const(out, xcryptographic_brainpool_p384r1_prime_bytes); }
static void xcryptographic_brainpool_p384r1_order(XCryptographic_Bn384* out)
{ xcryptographic_bn384_const(out, xcryptographic_brainpool_p384r1_order_bytes); }

static void xcryptographic_brainpool_p512r1_modulus(XCryptographic_Bn521* out)
{ xcryptographic_bn521_const64(out, xcryptographic_brainpool_p512r1_prime_bytes); }

static void xcryptographic_brainpool_p512r1_order(XCryptographic_Bn521* out)
{ xcryptographic_bn521_const64(out, xcryptographic_brainpool_p512r1_order_bytes); }

static void xcryptographic_p521_modulus(XCryptographic_Bn521* out)
{
    xcryptographic_bn521_const(out, xcryptographic_p521_p_bytes);
}

static void xcryptographic_p521_order(XCryptographic_Bn521* out)
{
    xcryptographic_bn521_const(out, xcryptographic_p521_n_bytes);
}

static void xcryptographic_p256_point_infinity(XCryptographic_EcPoint* point)
{
    memset(point, 0, sizeof(*point));
    point->infinity = true;
}

static void xcryptographic_p256_point_base(XCryptographic_EcPoint* point)
{
    XCryptographic_Bn prime;
    xcryptographic_p256_modulus(&prime);
    xcryptographic_bn_const(&point->x, xcryptographic_p256_gx_bytes);
    xcryptographic_bn_const(&point->y, xcryptographic_p256_gy_bytes);
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
    (void)prime;
}

static void xcryptographic_secp256k1_point_base(XCryptographic_EcPoint* point)
{
    xcryptographic_bn_const(&point->x, xcryptographic_secp256k1_gx_bytes);
    xcryptographic_bn_const(&point->y, xcryptographic_secp256k1_gy_bytes);
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
}

static void xcryptographic_brainpool_p256r1_point_base(XCryptographic_EcPoint* point)
{
    xcryptographic_bn_const(&point->x, xcryptographic_brainpool_p256r1_gx_bytes);
    xcryptographic_bn_const(&point->y, xcryptographic_brainpool_p256r1_gy_bytes);
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
}

static void xcryptographic_p384_point_infinity(XCryptographic_EcPoint384* point)
{
    memset(point, 0, sizeof(*point));
    point->infinity = true;
}

static void xcryptographic_p384_point_base(XCryptographic_EcPoint384* point)
{
    xcryptographic_bn384_const(&point->x, xcryptographic_p384_gx_bytes);
    xcryptographic_bn384_const(&point->y, xcryptographic_p384_gy_bytes);
    xcryptographic_bn384_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
}

static void xcryptographic_brainpool_p384r1_point_infinity(XCryptographic_EcPointBrainpool384* point)
{ memset(point, 0, sizeof(*point)); point->infinity = true; }
static void xcryptographic_brainpool_p384r1_point_base(XCryptographic_EcPointBrainpool384* point)
{
    xcryptographic_bn384_const(&point->x, xcryptographic_brainpool_p384r1_gx_bytes);
    xcryptographic_bn384_const(&point->y, xcryptographic_brainpool_p384r1_gy_bytes);
    xcryptographic_bn384_zero(&point->z); point->z.d[0] = 1; point->infinity = false;
}

static void xcryptographic_brainpool_p512r1_point_infinity(XCryptographic_EcPointBrainpool512* point)
{ memset(point, 0, sizeof(*point)); point->infinity = true; }

static void xcryptographic_brainpool_p512r1_point_base(XCryptographic_EcPointBrainpool512* point)
{
    xcryptographic_bn521_const64(&point->x, xcryptographic_brainpool_p512r1_gx_bytes);
    xcryptographic_bn521_const64(&point->y, xcryptographic_brainpool_p512r1_gy_bytes);
    xcryptographic_bn521_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
}

static void xcryptographic_p521_point_infinity(XCryptographic_EcPoint521* point)
{
    memset(point, 0, sizeof(*point));
    point->infinity = true;
}

static void xcryptographic_p521_point_base(XCryptographic_EcPoint521* point)
{
    xcryptographic_bn521_const(&point->x, xcryptographic_p521_gx_bytes);
    xcryptographic_bn521_const(&point->y, xcryptographic_p521_gy_bytes);
    xcryptographic_bn521_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
}

static void xcryptographic_p256_point_copy(XCryptographic_EcPoint* dst,
                                                const XCryptographic_EcPoint* src)
{
    *dst = *src;
}

static void xcryptographic_p256_double(XCryptographic_EcPoint* out,
                                            const XCryptographic_EcPoint* in,
                                            const XCryptographic_Bn* prime)
{
    XCryptographic_Bn alpha, beta, gamma, delta, x3, y3, z3, t1, t2;
    if (in->infinity || xcryptographic_bn_is_zero(&in->y)) {
        xcryptographic_p256_point_infinity(out);
        return;
    }

    /* y^2 = x^3 - 3x + b 的 Jacobian 二倍点运算。 */
    xcryptographic_bn_mul_mod(&alpha, &in->x, &in->x, prime); /* A = X^2 */
    xcryptographic_bn_mul_mod(&beta, &in->y, &in->y, prime);  /* B = Y^2 */
    xcryptographic_bn_mul_mod(&gamma, &beta, &beta, prime);   /* C = B^2 */

    xcryptographic_bn_add_mod(&t1, &in->x, &beta, prime);
    xcryptographic_bn_mul_mod(&t1, &t1, &t1, prime);
    xcryptographic_bn_sub_mod(&t1, &t1, &alpha, prime);
    xcryptographic_bn_sub_mod(&t1, &t1, &gamma, prime);
    xcryptographic_bn_add_mod(&delta, &t1, &t1, prime);       /* D */

    xcryptographic_bn_mul_mod(&t1, &in->z, &in->z, prime);
    xcryptographic_bn_sub_mod(&t2, &in->x, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &in->x, &t1, prime);
    xcryptographic_bn_mul_mod(&alpha, &t2, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn_add_mod(&alpha, &t1, &alpha, prime);   /* E = 3(X-Z^2)(X+Z^2) */

    xcryptographic_bn_mul_mod(&t1, &alpha, &alpha, prime);   /* F = E^2 */
    xcryptographic_bn_add_mod(&x3, &delta, &delta, prime);
    xcryptographic_bn_sub_mod(&x3, &t1, &x3, prime);         /* X3 = F - 2D */

    xcryptographic_bn_sub_mod(&t1, &delta, &x3, prime);
    xcryptographic_bn_mul_mod(&y3, &alpha, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &gamma, &gamma, prime);
    xcryptographic_bn_add_mod(&t2, &t1, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &t2, &t2, prime);          /* 8C */
    xcryptographic_bn_sub_mod(&y3, &y3, &t1, prime);         /* Y3 = E(D-X3)-8C */

    xcryptographic_bn_mul_mod(&t1, &in->z, &in->z, prime);
    xcryptographic_bn_add_mod(&t2, &in->y, &in->z, prime);
    xcryptographic_bn_mul_mod(&z3, &t2, &t2, prime);
    xcryptographic_bn_sub_mod(&z3, &z3, &beta, prime);
    xcryptographic_bn_sub_mod(&z3, &z3, &t1, prime);         /* Z3 = 2YZ */
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_secp256k1_double(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* in,
    const XCryptographic_Bn* prime)
{
    XCryptographic_Bn alpha, beta, gamma, delta, x3, y3, z3, t1, t2;
    if (in->infinity || xcryptographic_bn_is_zero(&in->y)) {
        xcryptographic_p256_point_infinity(out);
        return;
    }

    /* y^2 = x^3 + 7 的 Jacobian 二倍点运算。 */
    xcryptographic_bn_mul_mod(&alpha, &in->x, &in->x, prime); /* A = X^2 */
    xcryptographic_bn_mul_mod(&beta, &in->y, &in->y, prime);  /* B = Y^2 */
    xcryptographic_bn_mul_mod(&gamma, &beta, &beta, prime);   /* C = B^2 */

    xcryptographic_bn_add_mod(&t1, &in->x, &beta, prime);
    xcryptographic_bn_mul_mod(&t1, &t1, &t1, prime);
    xcryptographic_bn_sub_mod(&t1, &t1, &alpha, prime);
    xcryptographic_bn_sub_mod(&t1, &t1, &gamma, prime);
    xcryptographic_bn_add_mod(&delta, &t1, &t1, prime);       /* D */

    xcryptographic_bn_add_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn_add_mod(&alpha, &t1, &alpha, prime);   /* E = 3A */

    xcryptographic_bn_mul_mod(&t1, &alpha, &alpha, prime);   /* F = E^2 */
    xcryptographic_bn_add_mod(&x3, &delta, &delta, prime);
    xcryptographic_bn_sub_mod(&x3, &t1, &x3, prime);         /* X3 = F - 2D */

    xcryptographic_bn_sub_mod(&t1, &delta, &x3, prime);
    xcryptographic_bn_mul_mod(&y3, &alpha, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &gamma, &gamma, prime);
    xcryptographic_bn_add_mod(&t2, &t1, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &t2, &t2, prime);          /* 8C */
    xcryptographic_bn_sub_mod(&y3, &y3, &t1, prime);         /* Y3 = E(D-X3)-8C */

    xcryptographic_bn_add_mod(&t1, &in->y, &in->y, prime);
    xcryptographic_bn_mul_mod(&z3, &in->z, &t1, prime);      /* Z3 = 2YZ */
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_brainpool_p256r1_double(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* in,
    const XCryptographic_Bn* prime)
{
    XCryptographic_Bn a, x2, y2, y4, delta, z2, z4, alpha;
    XCryptographic_Bn x3, y3, z3, t1, t2;
    if (in->infinity || xcryptographic_bn_is_zero(&in->y)) {
        xcryptographic_p256_point_infinity(out);
        return;
    }

    /* 一般 a 时 y^2 = x^3 + ax + b 的 Jacobian 二倍点运算。 */
    xcryptographic_bn_mul_mod(&x2, &in->x, &in->x, prime);  /* X^2 */
    xcryptographic_bn_mul_mod(&y2, &in->y, &in->y, prime);  /* Y^2 */
    xcryptographic_bn_mul_mod(&y4, &y2, &y2, prime);        /* Y^4 */
    xcryptographic_bn_add_mod(&t1, &in->x, &y2, prime);
    xcryptographic_bn_mul_mod(&t1, &t1, &t1, prime);
    xcryptographic_bn_sub_mod(&t1, &t1, &x2, prime);
    xcryptographic_bn_sub_mod(&t1, &t1, &y4, prime);
    xcryptographic_bn_add_mod(&delta, &t1, &t1, prime);     /* D = 2((X+Y²)²-X²-Y⁴) */

    xcryptographic_bn_mul_mod(&z2, &in->z, &in->z, prime);
    xcryptographic_bn_mul_mod(&z4, &z2, &z2, prime);
    xcryptographic_bn_const(&a, xcryptographic_brainpool_p256r1_a_bytes);
    xcryptographic_bn_mul_mod(&t1, &a, &z4, prime);
    xcryptographic_bn_add_mod(&t2, &x2, &x2, prime);
    xcryptographic_bn_add_mod(&t2, &t2, &x2, prime);
    xcryptographic_bn_add_mod(&alpha, &t2, &t1, prime);     /* E = 3X² + aZ⁴ */

    xcryptographic_bn_mul_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn_add_mod(&x3, &delta, &delta, prime);
    xcryptographic_bn_sub_mod(&x3, &t1, &x3, prime);        /* X3 = E² - 2D */
    xcryptographic_bn_sub_mod(&t1, &delta, &x3, prime);
    xcryptographic_bn_mul_mod(&y3, &alpha, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &y4, &y4, prime);
    xcryptographic_bn_add_mod(&t2, &t1, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &t2, &t2, prime);        /* 8Y⁴ */
    xcryptographic_bn_sub_mod(&y3, &y3, &t1, prime);
    xcryptographic_bn_add_mod(&t1, &in->y, &in->y, prime);
    xcryptographic_bn_mul_mod(&z3, &in->z, &t1, prime);     /* Z3 = 2YZ */
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_p256_add(XCryptographic_EcPoint* out,
                                         const XCryptographic_EcPoint* a,
                                         const XCryptographic_EcPoint* b,
                                         const XCryptographic_Bn* prime)
{
    XCryptographic_Bn z1z1, z2z2, u1, u2, s1, s2, h, r, hh, hhh, v;
    XCryptographic_Bn t1, t2, x3, y3, z3;
    if (a->infinity) { *out = *b; return; }
    if (b->infinity) { *out = *a; return; }
    xcryptographic_bn_mul_mod(&z1z1, &a->z, &a->z, prime);
    xcryptographic_bn_mul_mod(&z2z2, &b->z, &b->z, prime);
    xcryptographic_bn_mul_mod(&u1, &a->x, &z2z2, prime);
    xcryptographic_bn_mul_mod(&u2, &b->x, &z1z1, prime);
    xcryptographic_bn_mul_mod(&t1, &b->z, &z2z2, prime);
    xcryptographic_bn_mul_mod(&s1, &a->y, &t1, prime);
    xcryptographic_bn_mul_mod(&t1, &a->z, &z1z1, prime);
    xcryptographic_bn_mul_mod(&s2, &b->y, &t1, prime);
    if (xcryptographic_bn_cmp(&u1, &u2) == 0) {
        if (xcryptographic_bn_cmp(&s1, &s2) == 0)
            xcryptographic_p256_double(out, a, prime);
        else
            xcryptographic_p256_point_infinity(out);
        return;
    }
    xcryptographic_bn_sub_mod(&h, &u2, &u1, prime);
    xcryptographic_bn_sub_mod(&r, &s2, &s1, prime);
    xcryptographic_bn_mul_mod(&hh, &h, &h, prime);
    xcryptographic_bn_mul_mod(&hhh, &h, &hh, prime);
    xcryptographic_bn_mul_mod(&v, &u1, &hh, prime);
    xcryptographic_bn_mul_mod(&x3, &r, &r, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &hhh, prime);
    xcryptographic_bn_add_mod(&t1, &v, &v, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &t1, prime);
    xcryptographic_bn_sub_mod(&t1, &v, &x3, prime);
    xcryptographic_bn_mul_mod(&y3, &r, &t1, prime);
    xcryptographic_bn_mul_mod(&t2, &s1, &hhh, prime);
    xcryptographic_bn_sub_mod(&y3, &y3, &t2, prime);
    xcryptographic_bn_mul_mod(&z3, &a->z, &b->z, prime);
    xcryptographic_bn_mul_mod(&z3, &z3, &h, prime);
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_secp256k1_add(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* a,
    const XCryptographic_EcPoint* b, const XCryptographic_Bn* prime)
{
    XCryptographic_Bn z1z1, z2z2, u1, u2, s1, s2, h, r, hh, hhh, v;
    XCryptographic_Bn t1, t2, x3, y3, z3;
    if (a->infinity) { *out = *b; return; }
    if (b->infinity) { *out = *a; return; }
    xcryptographic_bn_mul_mod(&z1z1, &a->z, &a->z, prime);
    xcryptographic_bn_mul_mod(&z2z2, &b->z, &b->z, prime);
    xcryptographic_bn_mul_mod(&u1, &a->x, &z2z2, prime);
    xcryptographic_bn_mul_mod(&u2, &b->x, &z1z1, prime);
    xcryptographic_bn_mul_mod(&t1, &b->z, &z2z2, prime);
    xcryptographic_bn_mul_mod(&s1, &a->y, &t1, prime);
    xcryptographic_bn_mul_mod(&t1, &a->z, &z1z1, prime);
    xcryptographic_bn_mul_mod(&s2, &b->y, &t1, prime);
    if (xcryptographic_bn_cmp(&u1, &u2) == 0) {
        if (xcryptographic_bn_cmp(&s1, &s2) == 0)
            xcryptographic_secp256k1_double(out, a, prime);
        else
            xcryptographic_p256_point_infinity(out);
        return;
    }
    xcryptographic_bn_sub_mod(&h, &u2, &u1, prime);
    xcryptographic_bn_sub_mod(&r, &s2, &s1, prime);
    xcryptographic_bn_mul_mod(&hh, &h, &h, prime);
    xcryptographic_bn_mul_mod(&hhh, &h, &hh, prime);
    xcryptographic_bn_mul_mod(&v, &u1, &hh, prime);
    xcryptographic_bn_mul_mod(&x3, &r, &r, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &hhh, prime);
    xcryptographic_bn_add_mod(&t1, &v, &v, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &t1, prime);
    xcryptographic_bn_sub_mod(&t1, &v, &x3, prime);
    xcryptographic_bn_mul_mod(&y3, &r, &t1, prime);
    xcryptographic_bn_mul_mod(&t2, &s1, &hhh, prime);
    xcryptographic_bn_sub_mod(&y3, &y3, &t2, prime);
    xcryptographic_bn_mul_mod(&z3, &a->z, &b->z, prime);
    xcryptographic_bn_mul_mod(&z3, &z3, &h, prime);
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_brainpool_p256r1_add(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* a,
    const XCryptographic_EcPoint* b, const XCryptographic_Bn* prime)
{
    XCryptographic_Bn z1z1, z2z2, u1, u2, s1, s2, h, r, hh, hhh, v;
    XCryptographic_Bn t1, t2, x3, y3, z3;
    if (a->infinity) { *out = *b; return; }
    if (b->infinity) { *out = *a; return; }
    xcryptographic_bn_mul_mod(&z1z1, &a->z, &a->z, prime);
    xcryptographic_bn_mul_mod(&z2z2, &b->z, &b->z, prime);
    xcryptographic_bn_mul_mod(&u1, &a->x, &z2z2, prime);
    xcryptographic_bn_mul_mod(&u2, &b->x, &z1z1, prime);
    xcryptographic_bn_mul_mod(&t1, &b->z, &z2z2, prime);
    xcryptographic_bn_mul_mod(&s1, &a->y, &t1, prime);
    xcryptographic_bn_mul_mod(&t1, &a->z, &z1z1, prime);
    xcryptographic_bn_mul_mod(&s2, &b->y, &t1, prime);
    if (xcryptographic_bn_cmp(&u1, &u2) == 0) {
        if (xcryptographic_bn_cmp(&s1, &s2) == 0)
            xcryptographic_brainpool_p256r1_double(out, a, prime);
        else
            xcryptographic_p256_point_infinity(out);
        return;
    }
    xcryptographic_bn_sub_mod(&h, &u2, &u1, prime);
    xcryptographic_bn_sub_mod(&r, &s2, &s1, prime);
    xcryptographic_bn_mul_mod(&hh, &h, &h, prime);
    xcryptographic_bn_mul_mod(&hhh, &h, &hh, prime);
    xcryptographic_bn_mul_mod(&v, &u1, &hh, prime);
    xcryptographic_bn_mul_mod(&x3, &r, &r, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &hhh, prime);
    xcryptographic_bn_add_mod(&t1, &v, &v, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &t1, prime);
    xcryptographic_bn_sub_mod(&t1, &v, &x3, prime);
    xcryptographic_bn_mul_mod(&y3, &r, &t1, prime);
    xcryptographic_bn_mul_mod(&t2, &s1, &hhh, prime);
    xcryptographic_bn_sub_mod(&y3, &y3, &t2, prime);
    xcryptographic_bn_mul_mod(&z3, &a->z, &b->z, prime);
    xcryptographic_bn_mul_mod(&z3, &z3, &h, prime);
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_p256_affine_double(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* in,
    const XCryptographic_Bn* prime)
{
    XCryptographic_Bn x2, numerator, denominator, inverse, lambda;
    XCryptographic_Bn x3, y3, t;
    XCryptographic_Bn three;
    if (in->infinity || xcryptographic_bn_is_zero(&in->y)) {
        xcryptographic_p256_point_infinity(out);
        return;
    }
    xcryptographic_bn_mul_mod(&x2, &in->x, &in->x, prime);
    xcryptographic_bn_add_mod(&numerator, &x2, &x2, prime);
    xcryptographic_bn_add_mod(&numerator, &numerator, &x2, prime);
    xcryptographic_bn_zero(&three);
    three.d[0] = 3;
    xcryptographic_bn_sub_mod(&numerator, &numerator, &three, prime);
    xcryptographic_bn_add_mod(&denominator, &in->y, &in->y, prime);
    xcryptographic_bn_inverse_mod(&inverse, &denominator, prime);
    xcryptographic_bn_mul_mod(&lambda, &numerator, &inverse, prime);
    xcryptographic_bn_mul_mod(&x3, &lambda, &lambda, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &in->x, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &in->x, prime);
    xcryptographic_bn_sub_mod(&t, &in->x, &x3, prime);
    xcryptographic_bn_mul_mod(&y3, &lambda, &t, prime);
    xcryptographic_bn_sub_mod(&y3, &y3, &in->y, prime);
    out->x = x3;
    out->y = y3;
    xcryptographic_bn_zero(&out->z);
    out->z.d[0] = 1;
    out->infinity = false;
}

static void xcryptographic_p256_affine_add(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* a,
    const XCryptographic_EcPoint* b, const XCryptographic_Bn* prime)
{
    XCryptographic_Bn numerator, denominator, inverse, lambda;
    XCryptographic_Bn x3, y3, t;
    if (a->infinity) { *out = *b; return; }
    if (b->infinity) { *out = *a; return; }
    if (xcryptographic_bn_cmp(&a->x, &b->x) == 0) {
        if (xcryptographic_bn_cmp(&a->y, &b->y) == 0)
            xcryptographic_p256_affine_double(out, a, prime);
        else
            xcryptographic_p256_point_infinity(out);
        return;
    }
    xcryptographic_bn_sub_mod(&numerator, &b->y, &a->y, prime);
    xcryptographic_bn_sub_mod(&denominator, &b->x, &a->x, prime);
    xcryptographic_bn_inverse_mod(&inverse, &denominator, prime);
    xcryptographic_bn_mul_mod(&lambda, &numerator, &inverse, prime);
    xcryptographic_bn_mul_mod(&x3, &lambda, &lambda, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &a->x, prime);
    xcryptographic_bn_sub_mod(&x3, &x3, &b->x, prime);
    xcryptographic_bn_sub_mod(&t, &a->x, &x3, prime);
    xcryptographic_bn_mul_mod(&y3, &lambda, &t, prime);
    xcryptographic_bn_sub_mod(&y3, &y3, &a->y, prime);
    out->x = x3;
    out->y = y3;
    xcryptographic_bn_zero(&out->z);
    out->z.d[0] = 1;
    out->infinity = false;
}

static void xcryptographic_p256_scalar_mul(XCryptographic_EcPoint* out,
                                                const XCryptographic_EcPoint* point,
                                                const XCryptographic_Bn* scalar)
{
    XCryptographic_Bn prime;
    XCryptographic_EcPoint result;
    size_t i;
    xcryptographic_p256_modulus(&prime);
    xcryptographic_p256_point_infinity(&result);
    for (i = 256; i-- > 0;) {
        XCryptographic_EcPoint temp;
        xcryptographic_p256_double(&temp, &result, &prime);
        result = temp;
        if (xcryptographic_bn_bit(scalar, i)) {
            xcryptographic_p256_add(&temp, &result, point, &prime);
            result = temp;
        }
    }
    *out = result;
}

static void xcryptographic_secp256k1_scalar_mul(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* point,
    const XCryptographic_Bn* scalar)
{
    XCryptographic_Bn prime;
    XCryptographic_EcPoint result;
    size_t i;
    xcryptographic_secp256k1_modulus(&prime);
    xcryptographic_p256_point_infinity(&result);
    for (i = 256; i-- > 0;) {
        XCryptographic_EcPoint temp;
        xcryptographic_secp256k1_double(&temp, &result, &prime);
        result = temp;
        if (xcryptographic_bn_bit(scalar, i)) {
            xcryptographic_secp256k1_add(&temp, &result, point, &prime);
            result = temp;
        }
    }
    *out = result;
}

static void xcryptographic_brainpool_p256r1_scalar_mul(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* point,
    const XCryptographic_Bn* scalar)
{
    XCryptographic_Bn prime;
    XCryptographic_EcPoint result;
    size_t i;
    xcryptographic_brainpool_p256r1_modulus(&prime);
    xcryptographic_p256_point_infinity(&result);
    for (i = 256; i-- > 0;) {
        XCryptographic_EcPoint temp;
        xcryptographic_brainpool_p256r1_double(&temp, &result, &prime);
        result = temp;
        if (xcryptographic_bn_bit(scalar, i)) {
            xcryptographic_brainpool_p256r1_add(&temp, &result, point, &prime);
            result = temp;
        }
    }
    *out = result;
}

static void xcryptographic_p384_double(XCryptographic_EcPoint384* out,
                                       const XCryptographic_EcPoint384* in,
                                       const XCryptographic_Bn384* prime)
{
    XCryptographic_Bn384 alpha, beta, gamma, delta, x3, y3, z3, t1, t2;
    if (in->infinity || xcryptographic_bn384_is_zero(&in->y)) {
        xcryptographic_p384_point_infinity(out);
        return;
    }
    xcryptographic_bn384_mul_mod(&alpha, &in->x, &in->x, prime);
    xcryptographic_bn384_mul_mod(&beta, &in->y, &in->y, prime);
    xcryptographic_bn384_mul_mod(&gamma, &beta, &beta, prime);
    xcryptographic_bn384_add_mod(&t1, &in->x, &beta, prime);
    xcryptographic_bn384_mul_mod(&t1, &t1, &t1, prime);
    xcryptographic_bn384_sub_mod(&t1, &t1, &alpha, prime);
    xcryptographic_bn384_sub_mod(&t1, &t1, &gamma, prime);
    xcryptographic_bn384_add_mod(&delta, &t1, &t1, prime);

    xcryptographic_bn384_mul_mod(&t1, &in->z, &in->z, prime);
    xcryptographic_bn384_sub_mod(&t2, &in->x, &t1, prime);
    xcryptographic_bn384_add_mod(&t1, &in->x, &t1, prime);
    xcryptographic_bn384_mul_mod(&alpha, &t2, &t1, prime);
    xcryptographic_bn384_add_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn384_add_mod(&alpha, &t1, &alpha, prime);
    xcryptographic_bn384_mul_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn384_add_mod(&x3, &delta, &delta, prime);
    xcryptographic_bn384_sub_mod(&x3, &t1, &x3, prime);
    xcryptographic_bn384_sub_mod(&t1, &delta, &x3, prime);
    xcryptographic_bn384_mul_mod(&y3, &alpha, &t1, prime);
    xcryptographic_bn384_add_mod(&t1, &gamma, &gamma, prime);
    xcryptographic_bn384_add_mod(&t2, &t1, &t1, prime);
    xcryptographic_bn384_add_mod(&t1, &t2, &t2, prime);
    xcryptographic_bn384_sub_mod(&y3, &y3, &t1, prime);
    xcryptographic_bn384_add_mod(&t2, &in->y, &in->z, prime);
    xcryptographic_bn384_mul_mod(&z3, &t2, &t2, prime);
    xcryptographic_bn384_sub_mod(&z3, &z3, &beta, prime);
    xcryptographic_bn384_mul_mod(&t1, &in->z, &in->z, prime);
    xcryptographic_bn384_sub_mod(&z3, &z3, &t1, prime);
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_p384_add(XCryptographic_EcPoint384* out,
                                    const XCryptographic_EcPoint384* a,
                                    const XCryptographic_EcPoint384* b,
                                    const XCryptographic_Bn384* prime)
{
    XCryptographic_Bn384 z1z1, z2z2, u1, u2, s1, s2, h, r, hh, hhh, v;
    XCryptographic_Bn384 t1, t2, x3, y3, z3;
    if (a->infinity) { *out = *b; return; }
    if (b->infinity) { *out = *a; return; }
    xcryptographic_bn384_mul_mod(&z1z1, &a->z, &a->z, prime);
    xcryptographic_bn384_mul_mod(&z2z2, &b->z, &b->z, prime);
    xcryptographic_bn384_mul_mod(&u1, &a->x, &z2z2, prime);
    xcryptographic_bn384_mul_mod(&u2, &b->x, &z1z1, prime);
    xcryptographic_bn384_mul_mod(&t1, &b->z, &z2z2, prime);
    xcryptographic_bn384_mul_mod(&s1, &a->y, &t1, prime);
    xcryptographic_bn384_mul_mod(&t1, &a->z, &z1z1, prime);
    xcryptographic_bn384_mul_mod(&s2, &b->y, &t1, prime);
    if (xcryptographic_bn384_cmp(&u1, &u2) == 0) {
        if (xcryptographic_bn384_cmp(&s1, &s2) == 0)
            xcryptographic_p384_double(out, a, prime);
        else
            xcryptographic_p384_point_infinity(out);
        return;
    }
    xcryptographic_bn384_sub_mod(&h, &u2, &u1, prime);
    xcryptographic_bn384_sub_mod(&r, &s2, &s1, prime);
    xcryptographic_bn384_mul_mod(&hh, &h, &h, prime);
    xcryptographic_bn384_mul_mod(&hhh, &h, &hh, prime);
    xcryptographic_bn384_mul_mod(&v, &u1, &hh, prime);
    xcryptographic_bn384_mul_mod(&x3, &r, &r, prime);
    xcryptographic_bn384_sub_mod(&x3, &x3, &hhh, prime);
    xcryptographic_bn384_add_mod(&t1, &v, &v, prime);
    xcryptographic_bn384_sub_mod(&x3, &x3, &t1, prime);
    xcryptographic_bn384_sub_mod(&t1, &v, &x3, prime);
    xcryptographic_bn384_mul_mod(&y3, &r, &t1, prime);
    xcryptographic_bn384_mul_mod(&t2, &s1, &hhh, prime);
    xcryptographic_bn384_sub_mod(&y3, &y3, &t2, prime);
    xcryptographic_bn384_mul_mod(&z3, &a->z, &b->z, prime);
    xcryptographic_bn384_mul_mod(&z3, &z3, &h, prime);
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_p384_scalar_mul(XCryptographic_EcPoint384* out,
                                           const XCryptographic_EcPoint384* point,
                                           const XCryptographic_Bn384* scalar)
{
    XCryptographic_Bn384 prime;
    XCryptographic_EcPoint384 result;
    size_t i;
    xcryptographic_p384_modulus(&prime);
    xcryptographic_p384_point_infinity(&result);
    for (i = 384; i-- > 0;) {
        XCryptographic_EcPoint384 temp;
        xcryptographic_p384_double(&temp, &result, &prime);
        result = temp;
        if (xcryptographic_bn384_bit(scalar, i)) {
            xcryptographic_p384_add(&temp, &result, point, &prime);
            result = temp;
        }
    }
    *out = result;
}

static void xcryptographic_brainpool_p384r1_double(
    XCryptographic_EcPointBrainpool384* out,
    const XCryptographic_EcPointBrainpool384* in,
    const XCryptographic_Bn384* prime)
{
    XCryptographic_Bn384 a, x2, y2, y4, delta, z2, z4, alpha;
    XCryptographic_Bn384 x3, y3, z3, t1, t2;
    if (in->infinity || xcryptographic_bn384_is_zero(&in->y)) {
        xcryptographic_brainpool_p384r1_point_infinity(out); return;
    }
    xcryptographic_bn384_mul_mod(&x2, &in->x, &in->x, prime);
    xcryptographic_bn384_mul_mod(&y2, &in->y, &in->y, prime);
    xcryptographic_bn384_mul_mod(&y4, &y2, &y2, prime);
    xcryptographic_bn384_add_mod(&t1, &in->x, &y2, prime);
    xcryptographic_bn384_mul_mod(&t1, &t1, &t1, prime);
    xcryptographic_bn384_sub_mod(&t1, &t1, &x2, prime);
    xcryptographic_bn384_sub_mod(&t1, &t1, &y4, prime);
    xcryptographic_bn384_add_mod(&delta, &t1, &t1, prime);
    xcryptographic_bn384_mul_mod(&z2, &in->z, &in->z, prime);
    xcryptographic_bn384_mul_mod(&z4, &z2, &z2, prime);
    xcryptographic_bn384_const(&a, xcryptographic_brainpool_p384r1_a_bytes);
    xcryptographic_bn384_mul_mod(&t1, &a, &z4, prime);
    xcryptographic_bn384_add_mod(&t2, &x2, &x2, prime);
    xcryptographic_bn384_add_mod(&t2, &t2, &x2, prime);
    xcryptographic_bn384_add_mod(&alpha, &t2, &t1, prime);
    xcryptographic_bn384_mul_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn384_add_mod(&x3, &delta, &delta, prime);
    xcryptographic_bn384_sub_mod(&x3, &t1, &x3, prime);
    xcryptographic_bn384_sub_mod(&t1, &delta, &x3, prime);
    xcryptographic_bn384_mul_mod(&y3, &alpha, &t1, prime);
    xcryptographic_bn384_add_mod(&t1, &y4, &y4, prime);
    xcryptographic_bn384_add_mod(&t2, &t1, &t1, prime);
    xcryptographic_bn384_add_mod(&t1, &t2, &t2, prime);
    xcryptographic_bn384_sub_mod(&y3, &y3, &t1, prime);
    xcryptographic_bn384_add_mod(&t1, &in->y, &in->y, prime);
    xcryptographic_bn384_mul_mod(&z3, &in->z, &t1, prime);
    out->x=x3; out->y=y3; out->z=z3; out->infinity=false;
}

static void xcryptographic_brainpool_p384r1_add(
    XCryptographic_EcPointBrainpool384* out,
    const XCryptographic_EcPointBrainpool384* a,
    const XCryptographic_EcPointBrainpool384* b,
    const XCryptographic_Bn384* prime)
{
    XCryptographic_Bn384 z1z1,z2z2,u1,u2,s1,s2,h,r,hh,hhh,v,t1,t2,x3,y3,z3;
    if (a->infinity) { *out=*b; return; }
    if (b->infinity) { *out=*a; return; }
    xcryptographic_bn384_mul_mod(&z1z1,&a->z,&a->z,prime);
    xcryptographic_bn384_mul_mod(&z2z2,&b->z,&b->z,prime);
    xcryptographic_bn384_mul_mod(&u1,&a->x,&z2z2,prime);
    xcryptographic_bn384_mul_mod(&u2,&b->x,&z1z1,prime);
    xcryptographic_bn384_mul_mod(&t1,&b->z,&z2z2,prime);
    xcryptographic_bn384_mul_mod(&s1,&a->y,&t1,prime);
    xcryptographic_bn384_mul_mod(&t1,&a->z,&z1z1,prime);
    xcryptographic_bn384_mul_mod(&s2,&b->y,&t1,prime);
    if (xcryptographic_bn384_cmp(&u1,&u2)==0) {
        if (xcryptographic_bn384_cmp(&s1,&s2)==0) xcryptographic_brainpool_p384r1_double(out,a,prime);
        else xcryptographic_brainpool_p384r1_point_infinity(out);
        return;
    }
    xcryptographic_bn384_sub_mod(&h,&u2,&u1,prime);
    xcryptographic_bn384_sub_mod(&r,&s2,&s1,prime);
    xcryptographic_bn384_mul_mod(&hh,&h,&h,prime);
    xcryptographic_bn384_mul_mod(&hhh,&h,&hh,prime);
    xcryptographic_bn384_mul_mod(&v,&u1,&hh,prime);
    xcryptographic_bn384_mul_mod(&x3,&r,&r,prime);
    xcryptographic_bn384_sub_mod(&x3,&x3,&hhh,prime);
    xcryptographic_bn384_add_mod(&t1,&v,&v,prime);
    xcryptographic_bn384_sub_mod(&x3,&x3,&t1,prime);
    xcryptographic_bn384_sub_mod(&t1,&v,&x3,prime);
    xcryptographic_bn384_mul_mod(&y3,&r,&t1,prime);
    xcryptographic_bn384_mul_mod(&t2,&s1,&hhh,prime);
    xcryptographic_bn384_sub_mod(&y3,&y3,&t2,prime);
    xcryptographic_bn384_mul_mod(&z3,&a->z,&b->z,prime);
    xcryptographic_bn384_mul_mod(&z3,&z3,&h,prime);
    out->x=x3; out->y=y3; out->z=z3; out->infinity=false;
}

static void xcryptographic_brainpool_p384r1_scalar_mul(
    XCryptographic_EcPointBrainpool384* out,
    const XCryptographic_EcPointBrainpool384* point,
    const XCryptographic_Bn384* scalar)
{
    XCryptographic_Bn384 prime; XCryptographic_EcPointBrainpool384 result;
    size_t i; xcryptographic_brainpool_p384r1_modulus(&prime);
    xcryptographic_brainpool_p384r1_point_infinity(&result);
    for (i=384; i-- > 0;) {
        XCryptographic_EcPointBrainpool384 temp;
        xcryptographic_brainpool_p384r1_double(&temp,&result,&prime); result=temp;
        if (xcryptographic_bn384_bit(scalar,i)) {
            xcryptographic_brainpool_p384r1_add(&temp,&result,point,&prime); result=temp;
        }
    }
    *out=result;
}

static bool xcryptographic_brainpool_p384r1_to_affine(XCryptographic_EcPointBrainpool384* point)
{
    XCryptographic_Bn384 prime,inv,z2,z3;
    if (point->infinity) return false;
    xcryptographic_brainpool_p384r1_modulus(&prime);
    xcryptographic_bn384_inverse_mod(&inv,&point->z,&prime);
    xcryptographic_bn384_mul_mod(&z2,&inv,&inv,&prime);
    xcryptographic_bn384_mul_mod(&z3,&z2,&inv,&prime);
    xcryptographic_bn384_mul_mod(&point->x,&point->x,&z2,&prime);
    xcryptographic_bn384_mul_mod(&point->y,&point->y,&z3,&prime);
    xcryptographic_bn384_zero(&point->z); point->z.d[0]=1; return true;
}

static bool xcryptographic_brainpool_p384r1_parse(const uint8_t* input,size_t inputLen,
                                                  XCryptographic_EcPointBrainpool384* point)
{
    XCryptographic_Bn384 prime,a,b,lhs,rhs,x2,x3;
    if (!input || inputLen != 97 || input[0] != 4 || !point) return false;
    xcryptographic_bn384_from_be(&point->x,input+1); xcryptographic_bn384_from_be(&point->y,input+49);
    xcryptographic_brainpool_p384r1_modulus(&prime);
    if (xcryptographic_bn384_cmp(&point->x,&prime)>=0 || xcryptographic_bn384_cmp(&point->y,&prime)>=0) return false;
    xcryptographic_bn384_mul_mod(&lhs,&point->y,&point->y,&prime);
    xcryptographic_bn384_mul_mod(&x2,&point->x,&point->x,&prime);
    xcryptographic_bn384_mul_mod(&x3,&x2,&point->x,&prime);
    xcryptographic_bn384_const(&a,xcryptographic_brainpool_p384r1_a_bytes);
    xcryptographic_bn384_const(&b,xcryptographic_brainpool_p384r1_b_bytes);
    xcryptographic_bn384_mul_mod(&rhs,&a,&point->x,&prime);
    xcryptographic_bn384_add_mod(&rhs,&rhs,&x3,&prime);
    xcryptographic_bn384_add_mod(&rhs,&rhs,&b,&prime);
    if (xcryptographic_bn384_cmp(&lhs,&rhs)!=0) return false;
    xcryptographic_bn384_zero(&point->z); point->z.d[0]=1; point->infinity=false; return true;
}

static void xcryptographic_brainpool_p384r1_serialize(const XCryptographic_EcPointBrainpool384* point,uint8_t output[97])
{ output[0]=4; xcryptographic_bn384_to_be(&point->x,output+1); xcryptographic_bn384_to_be(&point->y,output+49); }

static void xcryptographic_brainpool_p512r1_double(
    XCryptographic_EcPointBrainpool512* out,
    const XCryptographic_EcPointBrainpool512* in,
    const XCryptographic_Bn521* prime)
{
    XCryptographic_Bn521 a, x2, y2, y4, delta, z2, z4, alpha;
    XCryptographic_Bn521 x3, y3, z3, t1, t2;
    if (in->infinity || xcryptographic_bn521_is_zero(&in->y)) {
        xcryptographic_brainpool_p512r1_point_infinity(out);
        return;
    }
    xcryptographic_bn521_mul_mod(&x2, &in->x, &in->x, prime);
    xcryptographic_bn521_mul_mod(&y2, &in->y, &in->y, prime);
    xcryptographic_bn521_mul_mod(&y4, &y2, &y2, prime);
    xcryptographic_bn521_add_mod(&t1, &in->x, &y2, prime);
    xcryptographic_bn521_mul_mod(&t1, &t1, &t1, prime);
    xcryptographic_bn521_sub_mod(&t1, &t1, &x2, prime);
    xcryptographic_bn521_sub_mod(&t1, &t1, &y4, prime);
    xcryptographic_bn521_add_mod(&delta, &t1, &t1, prime);
    xcryptographic_bn521_mul_mod(&z2, &in->z, &in->z, prime);
    xcryptographic_bn521_mul_mod(&z4, &z2, &z2, prime);
    xcryptographic_bn521_const64(&a, xcryptographic_brainpool_p512r1_a_bytes);
    xcryptographic_bn521_mul_mod(&t1, &a, &z4, prime);
    xcryptographic_bn521_add_mod(&t2, &x2, &x2, prime);
    xcryptographic_bn521_add_mod(&t2, &t2, &x2, prime);
    xcryptographic_bn521_add_mod(&alpha, &t2, &t1, prime);
    xcryptographic_bn521_mul_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn521_add_mod(&x3, &delta, &delta, prime);
    xcryptographic_bn521_sub_mod(&x3, &t1, &x3, prime);
    xcryptographic_bn521_sub_mod(&t1, &delta, &x3, prime);
    xcryptographic_bn521_mul_mod(&y3, &alpha, &t1, prime);
    xcryptographic_bn521_add_mod(&t1, &y4, &y4, prime);
    xcryptographic_bn521_add_mod(&t2, &t1, &t1, prime);
    xcryptographic_bn521_add_mod(&t1, &t2, &t2, prime);
    xcryptographic_bn521_sub_mod(&y3, &y3, &t1, prime);
    xcryptographic_bn521_add_mod(&t1, &in->y, &in->y, prime);
    xcryptographic_bn521_mul_mod(&z3, &in->z, &t1, prime);
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_brainpool_p512r1_add(
    XCryptographic_EcPointBrainpool512* out,
    const XCryptographic_EcPointBrainpool512* a,
    const XCryptographic_EcPointBrainpool512* b,
    const XCryptographic_Bn521* prime)
{
    XCryptographic_Bn521 z1z1, z2z2, u1, u2, s1, s2, h, r, hh, hhh, v;
    XCryptographic_Bn521 t1, t2, x3, y3, z3;
    if (a->infinity) { *out = *b; return; }
    if (b->infinity) { *out = *a; return; }
    xcryptographic_bn521_mul_mod(&z1z1, &a->z, &a->z, prime);
    xcryptographic_bn521_mul_mod(&z2z2, &b->z, &b->z, prime);
    xcryptographic_bn521_mul_mod(&u1, &a->x, &z2z2, prime);
    xcryptographic_bn521_mul_mod(&u2, &b->x, &z1z1, prime);
    xcryptographic_bn521_mul_mod(&t1, &b->z, &z2z2, prime);
    xcryptographic_bn521_mul_mod(&s1, &a->y, &t1, prime);
    xcryptographic_bn521_mul_mod(&t1, &a->z, &z1z1, prime);
    xcryptographic_bn521_mul_mod(&s2, &b->y, &t1, prime);
    if (xcryptographic_bn521_cmp(&u1, &u2) == 0) {
        if (xcryptographic_bn521_cmp(&s1, &s2) == 0)
            xcryptographic_brainpool_p512r1_double(out, a, prime);
        else
            xcryptographic_brainpool_p512r1_point_infinity(out);
        return;
    }
    xcryptographic_bn521_sub_mod(&h, &u2, &u1, prime);
    xcryptographic_bn521_sub_mod(&r, &s2, &s1, prime);
    xcryptographic_bn521_mul_mod(&hh, &h, &h, prime);
    xcryptographic_bn521_mul_mod(&hhh, &h, &hh, prime);
    xcryptographic_bn521_mul_mod(&v, &u1, &hh, prime);
    xcryptographic_bn521_mul_mod(&x3, &r, &r, prime);
    xcryptographic_bn521_sub_mod(&x3, &x3, &hhh, prime);
    xcryptographic_bn521_add_mod(&t1, &v, &v, prime);
    xcryptographic_bn521_sub_mod(&x3, &x3, &t1, prime);
    xcryptographic_bn521_sub_mod(&t1, &v, &x3, prime);
    xcryptographic_bn521_mul_mod(&y3, &r, &t1, prime);
    xcryptographic_bn521_mul_mod(&t2, &s1, &hhh, prime);
    xcryptographic_bn521_sub_mod(&y3, &y3, &t2, prime);
    xcryptographic_bn521_mul_mod(&z3, &a->z, &b->z, prime);
    xcryptographic_bn521_mul_mod(&z3, &z3, &h, prime);
    out->x = x3;
    out->y = y3;
    out->z = z3;
    out->infinity = false;
}

static void xcryptographic_brainpool_p512r1_scalar_mul(
    XCryptographic_EcPointBrainpool512* out,
    const XCryptographic_EcPointBrainpool512* point,
    const XCryptographic_Bn521* scalar)
{
    XCryptographic_Bn521 prime;
    XCryptographic_EcPointBrainpool512 result;
    size_t i;
    xcryptographic_brainpool_p512r1_modulus(&prime);
    xcryptographic_brainpool_p512r1_point_infinity(&result);
    for (i = 512; i-- > 0;) {
        XCryptographic_EcPointBrainpool512 temp;
        xcryptographic_brainpool_p512r1_double(&temp, &result, &prime);
        result = temp;
        if (xcryptographic_bn521_bit(scalar, i)) {
            xcryptographic_brainpool_p512r1_add(&temp, &result, point, &prime);
            result = temp;
        }
    }
    *out = result;
}

static bool xcryptographic_brainpool_p512r1_to_affine(
    XCryptographic_EcPointBrainpool512* point)
{
    XCryptographic_Bn521 prime, inv, z2, z3;
    if (point->infinity) return false;
    xcryptographic_brainpool_p512r1_modulus(&prime);
    xcryptographic_bn521_inverse_mod(&inv, &point->z, &prime);
    xcryptographic_bn521_mul_mod(&z2, &inv, &inv, &prime);
    xcryptographic_bn521_mul_mod(&z3, &z2, &inv, &prime);
    xcryptographic_bn521_mul_mod(&point->x, &point->x, &z2, &prime);
    xcryptographic_bn521_mul_mod(&point->y, &point->y, &z3, &prime);
    xcryptographic_bn521_zero(&point->z);
    point->z.d[0] = 1;
    return true;
}

static bool xcryptographic_brainpool_p512r1_parse(
    const uint8_t* input, size_t inputLen, XCryptographic_EcPointBrainpool512* point)
{
    XCryptographic_Bn521 prime, a, b, lhs, rhs, x2, x3;
    if (!input || inputLen != 129 || input[0] != 4 || !point) return false;
    xcryptographic_bn521_from_be64(&point->x, input + 1);
    xcryptographic_bn521_from_be64(&point->y, input + 65);
    xcryptographic_brainpool_p512r1_modulus(&prime);
    if (xcryptographic_bn521_cmp(&point->x, &prime) >= 0 ||
        xcryptographic_bn521_cmp(&point->y, &prime) >= 0) return false;
    xcryptographic_bn521_mul_mod(&lhs, &point->y, &point->y, &prime);
    xcryptographic_bn521_mul_mod(&x2, &point->x, &point->x, &prime);
    xcryptographic_bn521_mul_mod(&x3, &x2, &point->x, &prime);
    xcryptographic_bn521_const64(&a, xcryptographic_brainpool_p512r1_a_bytes);
    xcryptographic_bn521_const64(&b, xcryptographic_brainpool_p512r1_b_bytes);
    xcryptographic_bn521_mul_mod(&rhs, &a, &point->x, &prime);
    xcryptographic_bn521_add_mod(&rhs, &rhs, &x3, &prime);
    xcryptographic_bn521_add_mod(&rhs, &rhs, &b, &prime);
    if (xcryptographic_bn521_cmp(&lhs, &rhs) != 0) return false;
    xcryptographic_bn521_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
    return true;
}

static void xcryptographic_brainpool_p512r1_serialize(
    const XCryptographic_EcPointBrainpool512* point, uint8_t output[129])
{
    output[0] = 4;
    xcryptographic_bn521_to_be64(&point->x, output + 1);
    xcryptographic_bn521_to_be64(&point->y, output + 65);
}

static bool xcryptographic_p256_to_affine(XCryptographic_EcPoint* point)
{
    XCryptographic_Bn prime, inv, z2, z3;
    if (point->infinity) return false;
    xcryptographic_p256_modulus(&prime);
    xcryptographic_bn_inverse_mod(&inv, &point->z, &prime);
    xcryptographic_bn_mul_mod(&z2, &inv, &inv, &prime);
    xcryptographic_bn_mul_mod(&z3, &z2, &inv, &prime);
    xcryptographic_bn_mul_mod(&point->x, &point->x, &z2, &prime);
    xcryptographic_bn_mul_mod(&point->y, &point->y, &z3, &prime);
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    return true;
}

static bool xcryptographic_secp256k1_to_affine(XCryptographic_EcPoint* point)
{
    XCryptographic_Bn prime, inv, z2, z3;
    if (point->infinity) return false;
    xcryptographic_secp256k1_modulus(&prime);
    xcryptographic_bn_inverse_mod(&inv, &point->z, &prime);
    xcryptographic_bn_mul_mod(&z2, &inv, &inv, &prime);
    xcryptographic_bn_mul_mod(&z3, &z2, &inv, &prime);
    xcryptographic_bn_mul_mod(&point->x, &point->x, &z2, &prime);
    xcryptographic_bn_mul_mod(&point->y, &point->y, &z3, &prime);
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    return true;
}

static bool xcryptographic_brainpool_p256r1_to_affine(XCryptographic_EcPoint* point)
{
    XCryptographic_Bn prime, inv, z2, z3;
    if (point->infinity) return false;
    xcryptographic_brainpool_p256r1_modulus(&prime);
    xcryptographic_bn_inverse_mod(&inv, &point->z, &prime);
    xcryptographic_bn_mul_mod(&z2, &inv, &inv, &prime);
    xcryptographic_bn_mul_mod(&z3, &z2, &inv, &prime);
    xcryptographic_bn_mul_mod(&point->x, &point->x, &z2, &prime);
    xcryptographic_bn_mul_mod(&point->y, &point->y, &z3, &prime);
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    return true;
}

static bool xcryptographic_p384_to_affine(XCryptographic_EcPoint384* point)
{
    XCryptographic_Bn384 prime, inv, z2, z3;
    if (point->infinity) return false;
    xcryptographic_p384_modulus(&prime);
    xcryptographic_bn384_inverse_mod(&inv, &point->z, &prime);
    xcryptographic_bn384_mul_mod(&z2, &inv, &inv, &prime);
    xcryptographic_bn384_mul_mod(&z3, &z2, &inv, &prime);
    xcryptographic_bn384_mul_mod(&point->x, &point->x, &z2, &prime);
    xcryptographic_bn384_mul_mod(&point->y, &point->y, &z3, &prime);
    xcryptographic_bn384_zero(&point->z);
    point->z.d[0] = 1;
    return true;
}

static void xcryptographic_p521_double(XCryptographic_EcPoint521* out,
                                       const XCryptographic_EcPoint521* in,
                                       const XCryptographic_Bn521* prime)
{
    XCryptographic_Bn521 alpha, beta, gamma, delta, x3, y3, z3, t1, t2;
    if (in->infinity || xcryptographic_bn521_is_zero(&in->y)) {
        xcryptographic_p521_point_infinity(out);
        return;
    }
    xcryptographic_bn521_mul_mod(&alpha, &in->x, &in->x, prime);
    xcryptographic_bn521_mul_mod(&beta, &in->y, &in->y, prime);
    xcryptographic_bn521_mul_mod(&gamma, &beta, &beta, prime);
    xcryptographic_bn521_add_mod(&t1, &in->x, &beta, prime);
    xcryptographic_bn521_mul_mod(&t1, &t1, &t1, prime);
    xcryptographic_bn521_sub_mod(&t1, &t1, &alpha, prime);
    xcryptographic_bn521_sub_mod(&t1, &t1, &gamma, prime);
    xcryptographic_bn521_add_mod(&delta, &t1, &t1, prime);
    xcryptographic_bn521_mul_mod(&t1, &in->z, &in->z, prime);
    xcryptographic_bn521_sub_mod(&t2, &in->x, &t1, prime);
    xcryptographic_bn521_add_mod(&t1, &in->x, &t1, prime);
    xcryptographic_bn521_mul_mod(&alpha, &t2, &t1, prime);
    xcryptographic_bn521_add_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn521_add_mod(&alpha, &t1, &alpha, prime);
    xcryptographic_bn521_mul_mod(&t1, &alpha, &alpha, prime);
    xcryptographic_bn521_add_mod(&x3, &delta, &delta, prime);
    xcryptographic_bn521_sub_mod(&x3, &t1, &x3, prime);
    xcryptographic_bn521_sub_mod(&t1, &delta, &x3, prime);
    xcryptographic_bn521_mul_mod(&y3, &alpha, &t1, prime);
    xcryptographic_bn521_add_mod(&t1, &gamma, &gamma, prime);
    xcryptographic_bn521_add_mod(&t2, &t1, &t1, prime);
    xcryptographic_bn521_add_mod(&t1, &t2, &t2, prime);
    xcryptographic_bn521_sub_mod(&y3, &y3, &t1, prime);
    xcryptographic_bn521_add_mod(&t2, &in->y, &in->z, prime);
    xcryptographic_bn521_mul_mod(&z3, &t2, &t2, prime);
    xcryptographic_bn521_sub_mod(&z3, &z3, &beta, prime);
    xcryptographic_bn521_mul_mod(&t1, &in->z, &in->z, prime);
    xcryptographic_bn521_sub_mod(&z3, &z3, &t1, prime);
    out->x = x3; out->y = y3; out->z = z3; out->infinity = false;
}

static void xcryptographic_p521_add(XCryptographic_EcPoint521* out,
                                    const XCryptographic_EcPoint521* a,
                                    const XCryptographic_EcPoint521* b,
                                    const XCryptographic_Bn521* prime)
{
    XCryptographic_Bn521 z1z1, z2z2, u1, u2, s1, s2, h, r, hh, hhh, v;
    XCryptographic_Bn521 t1, t2, x3, y3, z3;
    if (a->infinity) { *out = *b; return; }
    if (b->infinity) { *out = *a; return; }
    xcryptographic_bn521_mul_mod(&z1z1, &a->z, &a->z, prime);
    xcryptographic_bn521_mul_mod(&z2z2, &b->z, &b->z, prime);
    xcryptographic_bn521_mul_mod(&u1, &a->x, &z2z2, prime);
    xcryptographic_bn521_mul_mod(&u2, &b->x, &z1z1, prime);
    xcryptographic_bn521_mul_mod(&t1, &b->z, &z2z2, prime);
    xcryptographic_bn521_mul_mod(&s1, &a->y, &t1, prime);
    xcryptographic_bn521_mul_mod(&t1, &a->z, &z1z1, prime);
    xcryptographic_bn521_mul_mod(&s2, &b->y, &t1, prime);
    if (xcryptographic_bn521_cmp(&u1, &u2) == 0) {
        if (xcryptographic_bn521_cmp(&s1, &s2) == 0)
            xcryptographic_p521_double(out, a, prime);
        else
            xcryptographic_p521_point_infinity(out);
        return;
    }
    xcryptographic_bn521_sub_mod(&h, &u2, &u1, prime);
    xcryptographic_bn521_sub_mod(&r, &s2, &s1, prime);
    xcryptographic_bn521_mul_mod(&hh, &h, &h, prime);
    xcryptographic_bn521_mul_mod(&hhh, &h, &hh, prime);
    xcryptographic_bn521_mul_mod(&v, &u1, &hh, prime);
    xcryptographic_bn521_mul_mod(&x3, &r, &r, prime);
    xcryptographic_bn521_sub_mod(&x3, &x3, &hhh, prime);
    xcryptographic_bn521_add_mod(&t1, &v, &v, prime);
    xcryptographic_bn521_sub_mod(&x3, &x3, &t1, prime);
    xcryptographic_bn521_sub_mod(&t1, &v, &x3, prime);
    xcryptographic_bn521_mul_mod(&y3, &r, &t1, prime);
    xcryptographic_bn521_mul_mod(&t2, &s1, &hhh, prime);
    xcryptographic_bn521_sub_mod(&y3, &y3, &t2, prime);
    xcryptographic_bn521_mul_mod(&z3, &a->z, &b->z, prime);
    xcryptographic_bn521_mul_mod(&z3, &z3, &h, prime);
    out->x = x3; out->y = y3; out->z = z3; out->infinity = false;
}

static void xcryptographic_p521_scalar_mul(XCryptographic_EcPoint521* out,
                                           const XCryptographic_EcPoint521* point,
                                           const XCryptographic_Bn521* scalar)
{
    XCryptographic_Bn521 prime;
    XCryptographic_EcPoint521 result;
    size_t i;
    xcryptographic_p521_modulus(&prime);
    xcryptographic_p521_point_infinity(&result);
    for (i = 521; i-- > 0;) {
        XCryptographic_EcPoint521 temp;
        xcryptographic_p521_double(&temp, &result, &prime);
        result = temp;
        if (xcryptographic_bn521_bit(scalar, i)) {
            xcryptographic_p521_add(&temp, &result, point, &prime);
            result = temp;
        }
    }
    *out = result;
}

static bool xcryptographic_p521_to_affine(XCryptographic_EcPoint521* point)
{
    XCryptographic_Bn521 prime, inv, z2, z3;
    if (point->infinity) return false;
    xcryptographic_p521_modulus(&prime);
    xcryptographic_bn521_inverse_mod(&inv, &point->z, &prime);
    xcryptographic_bn521_mul_mod(&z2, &inv, &inv, &prime);
    xcryptographic_bn521_mul_mod(&z3, &z2, &inv, &prime);
    xcryptographic_bn521_mul_mod(&point->x, &point->x, &z2, &prime);
    xcryptographic_bn521_mul_mod(&point->y, &point->y, &z3, &prime);
    xcryptographic_bn521_zero(&point->z);
    point->z.d[0] = 1;
    return true;
}

static bool xcryptographic_p256_parse(const uint8_t* input, size_t inputLen,
                                           XCryptographic_EcPoint* point)
{
    XCryptographic_Bn prime, b, lhs, rhs, x2, x3;
    if (!input || inputLen != 65 || input[0] != 4 || !point) return false;
    xcryptographic_bn_from_be(&point->x, input + 1);
    xcryptographic_bn_from_be(&point->y, input + 33);
    xcryptographic_p256_modulus(&prime);
    if (xcryptographic_bn_cmp(&point->x, &prime) >= 0 ||
        xcryptographic_bn_cmp(&point->y, &prime) >= 0) return false;
    xcryptographic_bn_mul_mod(&lhs, &point->y, &point->y, &prime);
    xcryptographic_bn_mul_mod(&x2, &point->x, &point->x, &prime);
    xcryptographic_bn_mul_mod(&x3, &x2, &point->x, &prime);
    xcryptographic_bn_const(&b, xcryptographic_p256_b_bytes);
    xcryptographic_bn_sub_mod(&rhs, &x3, &point->x, &prime);
    xcryptographic_bn_sub_mod(&rhs, &rhs, &point->x, &prime);
    xcryptographic_bn_sub_mod(&rhs, &rhs, &point->x, &prime);
    xcryptographic_bn_add_mod(&rhs, &rhs, &b, &prime);
    if (xcryptographic_bn_cmp(&lhs, &rhs) != 0) return false;
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
    return true;
}

static bool xcryptographic_secp256k1_parse(
    const uint8_t* input, size_t inputLen, XCryptographic_EcPoint* point)
{
    XCryptographic_Bn prime, lhs, rhs, x2, x3;
    XCryptographic_Bn seven;
    if (!input || inputLen != 65 || input[0] != 4 || !point) return false;
    xcryptographic_bn_from_be(&point->x, input + 1);
    xcryptographic_bn_from_be(&point->y, input + 33);
    xcryptographic_secp256k1_modulus(&prime);
    if (xcryptographic_bn_cmp(&point->x, &prime) >= 0 ||
        xcryptographic_bn_cmp(&point->y, &prime) >= 0) return false;
    xcryptographic_bn_mul_mod(&lhs, &point->y, &point->y, &prime);
    xcryptographic_bn_mul_mod(&x2, &point->x, &point->x, &prime);
    xcryptographic_bn_mul_mod(&x3, &x2, &point->x, &prime);
    xcryptographic_bn_zero(&seven);
    seven.d[0] = 7;
    xcryptographic_bn_add_mod(&rhs, &x3,
                               &seven,
                               &prime);
    if (xcryptographic_bn_cmp(&lhs, &rhs) != 0) return false;
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
    return true;
}

static bool xcryptographic_brainpool_p256r1_parse(
    const uint8_t* input, size_t inputLen, XCryptographic_EcPoint* point)
{
    XCryptographic_Bn prime, b, lhs, rhs, x2, x3, ax;
    if (!input || inputLen != 65 || input[0] != 4 || !point) return false;
    xcryptographic_bn_from_be(&point->x, input + 1);
    xcryptographic_bn_from_be(&point->y, input + 33);
    xcryptographic_brainpool_p256r1_modulus(&prime);
    if (xcryptographic_bn_cmp(&point->x, &prime) >= 0 ||
        xcryptographic_bn_cmp(&point->y, &prime) >= 0) return false;
    xcryptographic_bn_mul_mod(&lhs, &point->y, &point->y, &prime);
    xcryptographic_bn_mul_mod(&x2, &point->x, &point->x, &prime);
    xcryptographic_bn_mul_mod(&x3, &x2, &point->x, &prime);
    xcryptographic_bn_const(&b, xcryptographic_brainpool_p256r1_b_bytes);
    xcryptographic_bn_const(&ax, xcryptographic_brainpool_p256r1_a_bytes);
    xcryptographic_bn_mul_mod(&ax, &ax, &point->x, &prime);
    xcryptographic_bn_add_mod(&rhs, &x3, &ax, &prime);
    xcryptographic_bn_add_mod(&rhs, &rhs, &b, &prime);
    if (xcryptographic_bn_cmp(&lhs, &rhs) != 0) return false;
    xcryptographic_bn_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
    return true;
}

static bool xcryptographic_p384_parse(const uint8_t* input, size_t inputLen,
                                      XCryptographic_EcPoint384* point)
{
    XCryptographic_Bn384 prime, b, lhs, rhs, x2, x3;
    if (!input || inputLen != 97 || input[0] != 4 || !point) return false;
    xcryptographic_bn384_from_be(&point->x, input + 1);
    xcryptographic_bn384_from_be(&point->y, input + 49);
    xcryptographic_p384_modulus(&prime);
    if (xcryptographic_bn384_cmp(&point->x, &prime) >= 0 ||
        xcryptographic_bn384_cmp(&point->y, &prime) >= 0) return false;
    xcryptographic_bn384_mul_mod(&lhs, &point->y, &point->y, &prime);
    xcryptographic_bn384_mul_mod(&x2, &point->x, &point->x, &prime);
    xcryptographic_bn384_mul_mod(&x3, &x2, &point->x, &prime);
    xcryptographic_bn384_sub_mod(&rhs, &x3, &point->x, &prime);
    xcryptographic_bn384_sub_mod(&rhs, &rhs, &point->x, &prime);
    xcryptographic_bn384_sub_mod(&rhs, &rhs, &point->x, &prime);
    xcryptographic_bn384_const(&b, xcryptographic_p384_b_bytes);
    xcryptographic_bn384_add_mod(&rhs, &rhs, &b, &prime);
    if (xcryptographic_bn384_cmp(&lhs, &rhs) != 0) return false;
    xcryptographic_bn384_zero(&point->z);
    point->z.d[0] = 1;
    point->infinity = false;
    return true;
}

static bool xcryptographic_p521_parse(const uint8_t* input, size_t inputLen,
                                      XCryptographic_EcPoint521* point)
{
    XCryptographic_Bn521 prime, b, lhs, rhs, x2, x3;
    if (!input || inputLen != 133 || input[0] != 4 || !point) return false;
    xcryptographic_bn521_from_be(&point->x, input + 1);
    xcryptographic_bn521_from_be(&point->y, input + 67);
    xcryptographic_p521_modulus(&prime);
    if (xcryptographic_bn521_cmp(&point->x, &prime) >= 0 ||
        xcryptographic_bn521_cmp(&point->y, &prime) >= 0) return false;
    xcryptographic_bn521_mul_mod(&lhs, &point->y, &point->y, &prime);
    xcryptographic_bn521_mul_mod(&x2, &point->x, &point->x, &prime);
    xcryptographic_bn521_mul_mod(&x3, &x2, &point->x, &prime);
    xcryptographic_bn521_sub_mod(&rhs, &x3, &point->x, &prime);
    xcryptographic_bn521_sub_mod(&rhs, &rhs, &point->x, &prime);
    xcryptographic_bn521_sub_mod(&rhs, &rhs, &point->x, &prime);
    xcryptographic_bn521_const(&b, xcryptographic_p521_b_bytes);
    xcryptographic_bn521_add_mod(&rhs, &rhs, &b, &prime);
    if (xcryptographic_bn521_cmp(&lhs, &rhs) != 0) return false;
    xcryptographic_bn521_zero(&point->z); point->z.d[0] = 1; point->infinity = false;
    return true;
}

static void xcryptographic_p256_serialize(const XCryptographic_EcPoint* point,
                                               uint8_t output[65])
{
    output[0] = 4;
    xcryptographic_bn_to_be(&point->x, output + 1);
    xcryptographic_bn_to_be(&point->y, output + 33);
}

static void xcryptographic_p384_serialize(const XCryptographic_EcPoint384* point,
                                          uint8_t output[97])
{
    output[0] = 4;
    xcryptographic_bn384_to_be(&point->x, output + 1);
    xcryptographic_bn384_to_be(&point->y, output + 49);
}

static void xcryptographic_p521_serialize(const XCryptographic_EcPoint521* point,
                                          uint8_t output[133])
{
    output[0] = 4;
    xcryptographic_bn521_to_be(&point->x, output + 1);
    xcryptographic_bn521_to_be(&point->y, output + 67);
}

static void xcryptographic_x25519_prime(XCryptographic_Bn* out)
{
    xcryptographic_bn_zero(out);
    out->d[0] = 0xffffffedU;
    out->d[1] = 0xffffffffU;
    out->d[2] = 0xffffffffU;
    out->d[3] = 0xffffffffU;
    out->d[4] = 0xffffffffU;
    out->d[5] = 0xffffffffU;
    out->d[6] = 0xffffffffU;
    out->d[7] = 0x7fffffffU;
}

static void xcryptographic_x25519_cswap(XCryptographic_Bn* a,
                                              XCryptographic_Bn* b,
                                              uint32_t swap)
{
    uint32_t mask = 0u - swap;
    size_t i;
    for (i = 0; i < 8; ++i) {
        uint32_t t = mask & (a->d[i] ^ b->d[i]);
        a->d[i] ^= t;
        b->d[i] ^= t;
    }
}

static bool xcryptographic_x25519(const uint8_t scalarBytes[32],
                                       const uint8_t publicBytes[32],
                                       uint8_t output[32])
{
    XCryptographic_Bn prime, a24, x1, x2, z2, x3, z3;
    XCryptographic_Bn a, aa, b, bb, e, c, d, da, db, t0, t1, inv;
    uint8_t scalar[32], uBytes[32];
    size_t i;
    uint32_t swap = 0;
    xcryptographic_x25519_prime(&prime);
    memcpy(scalar, scalarBytes, 32);
    scalar[0] &= 248;
    scalar[31] &= 127;
    scalar[31] |= 64;
    memcpy(uBytes, publicBytes, 32);
    uBytes[31] &= 127;
    xcryptographic_bn_from_le(&x1, uBytes);
    if (xcryptographic_bn_cmp(&x1, &prime) >= 0) {
        xcryptographic_bn_sub_raw(&x1, &x1, &prime);
    }
    xcryptographic_bn_zero(&x2);
    x2.d[0] = 1;
    xcryptographic_bn_zero(&z2);
    xcryptographic_bn_copy(&x3, &x1);
    xcryptographic_bn_zero(&z3);
    z3.d[0] = 1;
    xcryptographic_bn_zero(&a24);
    a24.d[0] = 121665;
    for (i = 255; i-- > 0;) {
        uint32_t bit = (scalar[i / 8] >> (i & 7u)) & 1u;
        swap ^= bit;
        xcryptographic_x25519_cswap(&x2, &x3, swap);
        xcryptographic_x25519_cswap(&z2, &z3, swap);
        swap = bit;
        xcryptographic_bn_add_mod(&a, &x2, &z2, &prime);
        xcryptographic_bn_mul_mod(&aa, &a, &a, &prime);
        xcryptographic_bn_sub_mod(&b, &x2, &z2, &prime);
        xcryptographic_bn_mul_mod(&bb, &b, &b, &prime);
        xcryptographic_bn_sub_mod(&e, &aa, &bb, &prime);
        xcryptographic_bn_add_mod(&c, &x3, &z3, &prime);
        xcryptographic_bn_sub_mod(&d, &x3, &z3, &prime);
        xcryptographic_bn_mul_mod(&da, &d, &a, &prime);
        xcryptographic_bn_mul_mod(&db, &b, &c, &prime);
        xcryptographic_bn_add_mod(&t0, &da, &db, &prime);
        xcryptographic_bn_mul_mod(&x3, &t0, &t0, &prime);
        xcryptographic_bn_sub_mod(&t0, &da, &db, &prime);
        xcryptographic_bn_mul_mod(&t0, &t0, &t0, &prime);
        xcryptographic_bn_mul_mod(&z3, &x1, &t0, &prime);
        xcryptographic_bn_mul_mod(&x2, &aa, &bb, &prime);
        xcryptographic_bn_mul_mod(&t0, &a24, &e, &prime);
        xcryptographic_bn_add_mod(&t0, &aa, &t0, &prime);
        xcryptographic_bn_mul_mod(&z2, &e, &t0, &prime);
    }
    xcryptographic_x25519_cswap(&x2, &x3, swap);
    xcryptographic_x25519_cswap(&z2, &z3, swap);
    xcryptographic_x25519_prime(&prime);
    xcryptographic_bn_inverse_mod(&inv, &z2, &prime);
    xcryptographic_bn_mul_mod(&x2, &x2, &inv, &prime);
    xcryptographic_bn_to_le(&x2, output);
    return true;
}

static void xcryptographic_x448_prime(XCryptographic_Bn521* out)
{
    xcryptographic_bn521_zero(out);
    out->d[0] = 0xffffffffU;
    out->d[1] = 0xffffffffU;
    out->d[2] = 0xffffffffU;
    out->d[3] = 0xffffffffU;
    out->d[4] = 0xffffffffU;
    out->d[5] = 0xffffffffU;
    out->d[6] = 0xffffffffU;
    out->d[7] = 0xfffffffeU;
    out->d[8] = 0xffffffffU;
    out->d[9] = 0xffffffffU;
    out->d[10] = 0xffffffffU;
    out->d[11] = 0xffffffffU;
    out->d[12] = 0xffffffffU;
    out->d[13] = 0xffffffffU;
}

static void xcryptographic_bn521_from_le56(XCryptographic_Bn521* out, const uint8_t bytes[56])
{
    size_t i; xcryptographic_bn521_zero(out);
    for (i=0;i<56;++i) out->d[i/4] |= (uint32_t)bytes[i] << ((i%4)*8);
}
static void xcryptographic_bn521_to_le56(const XCryptographic_Bn521* in,uint8_t bytes[56])
{
    size_t i; for (i=0;i<56;++i) bytes[i]=(uint8_t)(in->d[i/4]>>((i%4)*8));
}
static void xcryptographic_x448_cswap(XCryptographic_Bn521* a,XCryptographic_Bn521* b,uint32_t swap)
{
    uint32_t mask=0u-swap; size_t i; for(i=0;i<14;++i){uint32_t t=mask&(a->d[i]^b->d[i]);a->d[i]^=t;b->d[i]^=t;}
}
static bool xcryptographic_x448(const uint8_t scalarBytes[56],const uint8_t publicBytes[56],uint8_t output[56])
{
    XCryptographic_Bn521 prime,a24,x1,x2,z2,x3,z3,a,aa,b,bb,e,c,d,da,db,t0,t1,inv;
    uint8_t scalar[56],uBytes[56]; size_t i; uint32_t swap=0;
    xcryptographic_x448_prime(&prime); memcpy(scalar,scalarBytes,56); scalar[0]&=252; scalar[55]|=128;
    memcpy(uBytes,publicBytes,56); xcryptographic_bn521_from_le56(&x1,uBytes);
    if(xcryptographic_bn521_cmp(&x1,&prime)>=0) xcryptographic_bn521_sub_raw(&x1,&x1,&prime);
    xcryptographic_bn521_zero(&x2);x2.d[0]=1; xcryptographic_bn521_zero(&z2); x3=x1; xcryptographic_bn521_zero(&z3);z3.d[0]=1;
    xcryptographic_bn521_zero(&a24);a24.d[0]=39081;
    for(i=448;i-- > 0;){
        uint32_t bit=(scalar[i/8]>>(i&7u))&1u; swap^=bit; xcryptographic_x448_cswap(&x2,&x3,swap);xcryptographic_x448_cswap(&z2,&z3,swap);swap=bit;
        xcryptographic_bn521_add_mod(&a,&x2,&z2,&prime);xcryptographic_bn521_mul_mod(&aa,&a,&a,&prime);xcryptographic_bn521_sub_mod(&b,&x2,&z2,&prime);xcryptographic_bn521_mul_mod(&bb,&b,&b,&prime);xcryptographic_bn521_sub_mod(&e,&aa,&bb,&prime);
        xcryptographic_bn521_add_mod(&c,&x3,&z3,&prime);xcryptographic_bn521_sub_mod(&d,&x3,&z3,&prime);xcryptographic_bn521_mul_mod(&da,&d,&a,&prime);xcryptographic_bn521_mul_mod(&db,&b,&c,&prime);xcryptographic_bn521_add_mod(&t0,&da,&db,&prime);xcryptographic_bn521_mul_mod(&x3,&t0,&t0,&prime);xcryptographic_bn521_sub_mod(&t0,&da,&db,&prime);xcryptographic_bn521_mul_mod(&t0,&t0,&t0,&prime);xcryptographic_bn521_mul_mod(&z3,&x1,&t0,&prime);xcryptographic_bn521_mul_mod(&x2,&aa,&bb,&prime);xcryptographic_bn521_mul_mod(&t0,&a24,&e,&prime);xcryptographic_bn521_add_mod(&t0,&aa,&t0,&prime);xcryptographic_bn521_mul_mod(&z2,&e,&t0,&prime);
    }
    xcryptographic_x448_cswap(&x2,&x3,swap);xcryptographic_x448_cswap(&z2,&z3,swap);xcryptographic_bn521_inverse_mod(&inv,&z2,&prime);xcryptographic_bn521_mul_mod(&x2,&x2,&inv,&prime);xcryptographic_bn521_to_le56(&x2,output);return true;
}

static bool xcryptographic_scalar_valid(const uint8_t privateKey[32])
{
    XCryptographic_Bn scalar, order;
    xcryptographic_bn_from_be(&scalar, privateKey);
    xcryptographic_p256_order(&order);
    return !xcryptographic_bn_is_zero(&scalar) &&
           xcryptographic_bn_cmp(&scalar, &order) < 0;
}

static bool xcryptographic_random_scalar(uint8_t output[32])
{
    XCryptographic_Bn scalar, order;
    size_t attempts;
    xcryptographic_p256_order(&order);
    for (attempts = 0; attempts < 128; ++attempts) {
        if (!xcryptographic_random(output, 32)) return false;
        xcryptographic_bn_from_be(&scalar, output);
        if (!xcryptographic_bn_is_zero(&scalar) &&
            xcryptographic_bn_cmp(&scalar, &order) < 0) return true;
    }
    return false;
}

static bool xcryptographic_secp256k1_scalar_valid(const uint8_t privateKey[32])
{
    XCryptographic_Bn scalar, order;
    xcryptographic_bn_from_be(&scalar, privateKey);
    xcryptographic_secp256k1_order(&order);
    return !xcryptographic_bn_is_zero(&scalar) &&
           xcryptographic_bn_cmp(&scalar, &order) < 0;
}

static bool xcryptographic_secp256k1_random_scalar(uint8_t output[32])
{
    XCryptographic_Bn scalar, order;
    size_t attempts;
    xcryptographic_secp256k1_order(&order);
    for (attempts = 0; attempts < 128; ++attempts) {
        if (!xcryptographic_random(output, 32)) return false;
        xcryptographic_bn_from_be(&scalar, output);
        if (!xcryptographic_bn_is_zero(&scalar) &&
            xcryptographic_bn_cmp(&scalar, &order) < 0) return true;
    }
    return false;
}

static bool xcryptographic_brainpool_p256r1_scalar_valid(const uint8_t privateKey[32])
{
    XCryptographic_Bn scalar, order;
    xcryptographic_bn_from_be(&scalar, privateKey);
    xcryptographic_brainpool_p256r1_order(&order);
    return !xcryptographic_bn_is_zero(&scalar) &&
           xcryptographic_bn_cmp(&scalar, &order) < 0;
}

static bool xcryptographic_brainpool_p256r1_random_scalar(uint8_t output[32])
{
    XCryptographic_Bn scalar, order;
    size_t attempts;
    xcryptographic_brainpool_p256r1_order(&order);
    for (attempts = 0; attempts < 128; ++attempts) {
        if (!xcryptographic_random(output, 32)) return false;
        xcryptographic_bn_from_be(&scalar, output);
        if (!xcryptographic_bn_is_zero(&scalar) &&
            xcryptographic_bn_cmp(&scalar, &order) < 0) return true;
    }
    return false;
}

static bool xcryptographic_p384_scalar_valid(const uint8_t privateKey[48])
{
    XCryptographic_Bn384 scalar, order;
    xcryptographic_bn384_from_be(&scalar, privateKey);
    xcryptographic_p384_order(&order);
    return !xcryptographic_bn384_is_zero(&scalar) &&
           xcryptographic_bn384_cmp(&scalar, &order) < 0;
}

static bool xcryptographic_p384_random_scalar(uint8_t output[48])
{
    XCryptographic_Bn384 scalar, order;
    size_t attempts;
    xcryptographic_p384_order(&order);
    for (attempts = 0; attempts < 128; ++attempts) {
        if (!xcryptographic_random(output, 48)) return false;
        xcryptographic_bn384_from_be(&scalar, output);
        if (!xcryptographic_bn384_is_zero(&scalar) &&
            xcryptographic_bn384_cmp(&scalar, &order) < 0) return true;
    }
    return false;
}

static bool xcryptographic_brainpool_p384r1_scalar_valid(const uint8_t privateKey[48])
{
    XCryptographic_Bn384 scalar, order;
    xcryptographic_bn384_from_be(&scalar, privateKey);
    xcryptographic_brainpool_p384r1_order(&order);
    return !xcryptographic_bn384_is_zero(&scalar) && xcryptographic_bn384_cmp(&scalar,&order)<0;
}

static bool xcryptographic_brainpool_p384r1_random_scalar(uint8_t output[48])
{
    XCryptographic_Bn384 scalar, order; size_t attempts;
    xcryptographic_brainpool_p384r1_order(&order);
    for (attempts=0; attempts<128; ++attempts) {
        if (!xcryptographic_random(output,48)) return false;
        xcryptographic_bn384_from_be(&scalar,output);
        if (!xcryptographic_bn384_is_zero(&scalar) && xcryptographic_bn384_cmp(&scalar,&order)<0) return true;
    }
    return false;
}

static bool xcryptographic_brainpool_p512r1_scalar_valid(const uint8_t privateKey[64])
{
    XCryptographic_Bn521 scalar, order;
    xcryptographic_bn521_from_be64(&scalar, privateKey);
    xcryptographic_brainpool_p512r1_order(&order);
    return !xcryptographic_bn521_is_zero(&scalar) &&
           xcryptographic_bn521_cmp(&scalar, &order) < 0;
}

static bool xcryptographic_brainpool_p512r1_random_scalar(uint8_t output[64])
{
    XCryptographic_Bn521 scalar, order;
    size_t attempts;
    xcryptographic_brainpool_p512r1_order(&order);
    for (attempts = 0; attempts < 128; ++attempts) {
        if (!xcryptographic_random(output, 64)) return false;
        xcryptographic_bn521_from_be64(&scalar, output);
        if (!xcryptographic_bn521_is_zero(&scalar) &&
            xcryptographic_bn521_cmp(&scalar, &order) < 0) return true;
    }
    return false;
}

static bool xcryptographic_p521_scalar_valid(const uint8_t privateKey[66])
{
    XCryptographic_Bn521 scalar, order;
    xcryptographic_bn521_from_be(&scalar, privateKey);
    xcryptographic_p521_order(&order);
    return !xcryptographic_bn521_is_zero(&scalar) &&
           xcryptographic_bn521_cmp(&scalar, &order) < 0;
}

static bool xcryptographic_p521_random_scalar(uint8_t output[66])
{
    XCryptographic_Bn521 scalar, order;
    size_t attempts;
    xcryptographic_p521_order(&order);
    for (attempts = 0; attempts < 128; ++attempts) {
        if (!xcryptographic_random(output, 66)) return false;
        output[0] &= 1u;
        xcryptographic_bn521_from_be(&scalar, output);
        if (!xcryptographic_bn521_is_zero(&scalar) &&
            xcryptographic_bn521_cmp(&scalar, &order) < 0) return true;
    }
    return false;
}

/* =============== ECJPAKE (secp256r1 / SHA-256) =============== */

typedef struct XCryptographic_EcjPakeContext {
    XCryptographic_EcjPakeRole role;
    XCryptographic_Bn secret;
    XCryptographic_Bn private1;
    XCryptographic_Bn private2;
    XCryptographic_EcPoint public1;
    XCryptographic_EcPoint public2;
    XCryptographic_EcPoint peer1;
    XCryptographic_EcPoint peer2;
    XCryptographic_EcPoint peer_round2;
    bool wrote_round_one;
    bool read_round_one;
    bool wrote_round_two;
    bool read_round_two;
} XCryptographic_EcjPakeContext;

static void xcryptographic_ecjpake_mod_from_bytes(XCryptographic_Bn *out,
                                                   const uint8_t *data, size_t size)
{
    XCryptographic_Bn order;
    size_t i;
    xcryptographic_bn_zero(out);
    xcryptographic_p256_order(&order);
    for (i = 0; i < size; ++i) {
        unsigned bit;
        for (bit = 0; bit < 8; ++bit) {
            XCryptographic_Bn doubled;
            xcryptographic_bn_add_mod(&doubled, out, out, &order);
            *out = doubled;
            if ((data[i] & (uint8_t)(0x80u >> bit)) != 0) {
                XCryptographic_Bn incremented;
                XCryptographic_Bn one;
                xcryptographic_bn_zero(&one);
                one.d[0] = 1;
                xcryptographic_bn_add_mod(&incremented, out, &one, &order);
                *out = incremented;
            }
        }
    }
}

static bool xcryptographic_ecjpake_point_equal(const XCryptographic_EcPoint *a,
                                                const XCryptographic_EcPoint *b)
{
    XCryptographic_EcPoint left = *a;
    XCryptographic_EcPoint right = *b;
    if (!xcryptographic_p256_to_affine(&left) || !xcryptographic_p256_to_affine(&right))
        return false;
    return xcryptographic_bn_cmp(&left.x, &right.x) == 0 &&
           xcryptographic_bn_cmp(&left.y, &right.y) == 0;
}

static bool xcryptographic_ecjpake_write_tls_point(const XCryptographic_EcPoint *point,
                                                    uint8_t **position,
                                                    const uint8_t *end)
{
    XCryptographic_EcPoint affine = *point;
    if (!position || !*position || end < *position ||
        (size_t)(end - *position) < 66 ||
        !xcryptographic_p256_to_affine(&affine))
        return false;
    *(*position)++ = 65;
    xcryptographic_p256_serialize(&affine, *position);
    *position += 65;
    return true;
}

static bool xcryptographic_ecjpake_read_tls_point(XCryptographic_EcPoint *point,
                                                   const uint8_t **position,
                                                   const uint8_t *end)
{
    if (!point || !position || !*position || end < *position ||
        (size_t)(end - *position) < 66 || **position != 65)
        return false;
    ++*position;
    if (!xcryptographic_p256_parse(*position, 65, point)) return false;
    *position += 65;
    return true;
}

static bool xcryptographic_ecjpake_hash(const XCryptographic_EcPoint *base,
                                        const XCryptographic_EcPoint *v,
                                        const XCryptographic_EcPoint *x,
                                        const char *identifier,
                                        XCryptographic_Bn *result)
{
    uint8_t encoded[3 * (4 + 65) + 4 + 6];
    uint8_t digest[32];
    uint8_t *position = encoded;
    const uint8_t *end = encoded + sizeof(encoded);
    XCryptographic_EcPoint affine;
    const size_t id_size = 6;
    const XCryptographic_EcPoint *points[3] = { base, v, x };
    size_t i;
    if (!base || !v || !x || !identifier || !result) return false;
    for (i = 0; i < 3; ++i) {
        affine = *points[i];
        if (!xcryptographic_p256_to_affine(&affine) ||
            (size_t)(end - position) < 69) {
            memset(encoded, 0, sizeof(encoded));
            return false;
        }
        position[0] = 0;
        position[1] = 0;
        position[2] = 0;
        position[3] = 65;
        position += 4;
        xcryptographic_p256_serialize(&affine, position);
        position += 65;
    }
    position[0] = 0;
    position[1] = 0;
    position[2] = 0;
    position[3] = (uint8_t)id_size;
    position += 4;
    memcpy(position, identifier, id_size);
    position += id_size;
    if (!XCryptographicHash_hashInto((char *)digest, sizeof(digest),
                                     (const char *)encoded,
                                     (size_t)(position - encoded),
                                     XCryptographicHash_Sha256).m_data) {
        memset(encoded, 0, sizeof(encoded));
        return false;
    }
    xcryptographic_ecjpake_mod_from_bytes(result, digest, sizeof(digest));
    memset(digest, 0, sizeof(digest));
    memset(encoded, 0, sizeof(encoded));
    return true;
}

static bool xcryptographic_ecjpake_write_zkp(const XCryptographic_EcPoint *base,
                                             const XCryptographic_Bn *private_key,
                                             const XCryptographic_EcPoint *public_key,
                                             const char *identifier,
                                             uint8_t **position,
                                             const uint8_t *end)
{
    XCryptographic_Bn order, v, h, product, r;
    XCryptographic_EcPoint proof_public;
    uint8_t random[32];
    uint8_t scalar[32];
    size_t scalar_size = 32;
    if (!base || !private_key || !public_key || !identifier || !position || !*position)
        return false;
    xcryptographic_p256_order(&order);
    if (!xcryptographic_random_scalar(random)) return false;
    xcryptographic_bn_from_be(&v, random);
    xcryptographic_p256_scalar_mul(&proof_public, base, &v);
    if (!xcryptographic_ecjpake_hash(base, &proof_public, public_key, identifier, &h))
        goto cleanup;
    xcryptographic_bn_mul_mod(&product, &h, private_key, &order);
    xcryptographic_bn_sub_mod(&r, &v, &product, &order);
    if (!xcryptographic_ecjpake_write_tls_point(&proof_public, position, end))
        goto cleanup;
    xcryptographic_bn_to_be(&r, scalar);
    while (scalar_size > 1 && scalar[32 - scalar_size] == 0) --scalar_size;
    if (end < *position || (size_t)(end - *position) < scalar_size + 1) goto cleanup;
    *(*position)++ = (uint8_t)scalar_size;
    memcpy(*position, scalar + 32 - scalar_size, scalar_size);
    *position += scalar_size;
    memset(random, 0, sizeof(random));
    memset(scalar, 0, sizeof(scalar));
    return true;
cleanup:
    memset(random, 0, sizeof(random));
    memset(scalar, 0, sizeof(scalar));
    return false;
}

static bool xcryptographic_ecjpake_read_zkp(const XCryptographic_EcPoint *base,
                                            const XCryptographic_EcPoint *public_key,
                                            const char *identifier,
                                            const uint8_t **position,
                                            const uint8_t *end)
{
    XCryptographic_Bn order, h, r;
    XCryptographic_EcPoint proof_public, h_public, r_base, expected;
    uint8_t scalar[32] = { 0 };
    size_t scalar_size;
    XCryptographic_Bn prime;
    if (!base || !public_key || !identifier || !position || !*position) return false;
    if (!xcryptographic_ecjpake_read_tls_point(&proof_public, position, end) ||
        end < *position || (size_t)(end - *position) < 1)
        return false;
    scalar_size = *(*position)++;
    if (scalar_size == 0 || scalar_size > 32 || (size_t)(end - *position) < scalar_size)
        return false;
    memcpy(scalar + 32 - scalar_size, *position, scalar_size);
    *position += scalar_size;
    xcryptographic_p256_order(&order);
    xcryptographic_bn_from_be(&r, scalar);
    memset(scalar, 0, sizeof(scalar));
    if (xcryptographic_bn_cmp(&r, &order) >= 0 ||
        !xcryptographic_ecjpake_hash(base, &proof_public, public_key, identifier, &h))
        return false;
    xcryptographic_p256_scalar_mul(&h_public, public_key, &h);
    xcryptographic_p256_scalar_mul(&r_base, base, &r);
    xcryptographic_p256_modulus(&prime);
    xcryptographic_p256_add(&expected, &h_public, &r_base, &prime);
    return xcryptographic_ecjpake_point_equal(&expected, &proof_public);
}

static bool xcryptographic_ecjpake_write_key_pair(const XCryptographic_EcPoint *base,
                                                  XCryptographic_Bn *private_key,
                                                  XCryptographic_EcPoint *public_key,
                                                  const char *identifier,
                                                  uint8_t **position,
                                                  const uint8_t *end)
{
    uint8_t random[32];
    if (!xcryptographic_random_scalar(random)) return false;
    xcryptographic_bn_from_be(private_key, random);
    memset(random, 0, sizeof(random));
    xcryptographic_p256_scalar_mul(public_key, base, private_key);
    if (!xcryptographic_ecjpake_write_tls_point(public_key, position, end) ||
        !xcryptographic_ecjpake_write_zkp(base, private_key, public_key,
                                          identifier, position, end))
        return false;
    return true;
}

XCryptographic_EcjPakeContext* XCryptographic_ecjPakeCreate(
    XCryptographic_EcjPakeRole role, XByteArrayView password)
{
    XCryptographic_EcjPakeContext *context;
    if (!XCRYPTOGRAPHIC_ECJPAKE_ON || password.m_size <= 0 || !password.m_data ||
        (role != XCryptographic_EcjPakeRole_Client &&
         role != XCryptographic_EcjPakeRole_Server))
        return NULL;
    context = (XCryptographic_EcjPakeContext *)XMalloc_System(sizeof(*context));
    if (!context) return NULL;
    memset(context, 0, sizeof(*context));
    context->role = role;
    xcryptographic_ecjpake_mod_from_bytes(&context->secret, password.m_data,
                                          (size_t)password.m_size);
    if (xcryptographic_bn_is_zero(&context->secret)) {
        memset(context, 0, sizeof(*context));
        XFree_System(context);
        return NULL;
    }
    return context;
}

void XCryptographic_ecjPakeDestroy(XCryptographic_EcjPakeContext *context)
{
    if (context) {
        memset(context, 0, sizeof(*context));
        XFree_System(context);
    }
}

XByteArrayView XCryptographic_ecjPakeWriteRoundOneInto(
    XCryptographic_EcjPakeContext *context, char *buffer, size_t bufferSize)
{
    XByteArrayView empty = { NULL, 0 };
    XCryptographic_EcPoint base;
    uint8_t *position = (uint8_t *)buffer;
    const uint8_t *end = (const uint8_t *)buffer + bufferSize;
    const char *identifier;
    if (!context || !buffer || context->wrote_round_one || !XCRYPTOGRAPHIC_ECJPAKE_ON)
        return empty;
    identifier = context->role == XCryptographic_EcjPakeRole_Client ? "client" : "server";
    xcryptographic_p256_point_base(&base);
    if (!xcryptographic_ecjpake_write_key_pair(&base, &context->private1,
                                               &context->public1, identifier,
                                               &position, end) ||
        !xcryptographic_ecjpake_write_key_pair(&base, &context->private2,
                                               &context->public2, identifier,
                                               &position, end))
        return empty;
    context->wrote_round_one = true;
    return XByteArrayView_create_data(buffer, (int64_t)(position - (uint8_t *)buffer));
}

bool XCryptographic_ecjPakeReadRoundOne(XCryptographic_EcjPakeContext *context,
                                        XByteArrayView message)
{
    XCryptographic_EcPoint base;
    const uint8_t *position;
    const uint8_t *end;
    const char *identifier;
    if (!context || message.m_size <= 0 || !message.m_data || context->read_round_one ||
        !XCRYPTOGRAPHIC_ECJPAKE_ON)
        return false;
    position = message.m_data;
    end = message.m_data + (size_t)message.m_size;
    identifier = context->role == XCryptographic_EcjPakeRole_Client ? "server" : "client";
    xcryptographic_p256_point_base(&base);
    if (!xcryptographic_ecjpake_read_tls_point(&context->peer1, &position, end) ||
        !xcryptographic_ecjpake_read_zkp(&base, &context->peer1, identifier, &position, end) ||
        !xcryptographic_ecjpake_read_tls_point(&context->peer2, &position, end) ||
        !xcryptographic_ecjpake_read_zkp(&base, &context->peer2, identifier, &position, end) ||
        position != end)
        return false;
    context->read_round_one = true;
    return true;
}

XByteArrayView XCryptographic_ecjPakeWriteRoundTwoInto(
    XCryptographic_EcjPakeContext *context, char *buffer, size_t bufferSize)
{
    XByteArrayView empty = { NULL, 0 };
    XCryptographic_EcPoint base, sum, own_public;
    XCryptographic_Bn order, product, prime;
    uint8_t *position = (uint8_t *)buffer;
    const uint8_t *end = (const uint8_t *)buffer + bufferSize;
    const char *identifier;
    if (!context || !buffer || !context->wrote_round_one || !context->read_round_one ||
        context->wrote_round_two || !XCRYPTOGRAPHIC_ECJPAKE_ON)
        return empty;
    xcryptographic_p256_point_base(&base);
    xcryptographic_p256_modulus(&prime);
    xcryptographic_p256_add(&sum, &context->peer1, &context->peer2, &prime);
    xcryptographic_p256_add(&sum, &sum, &context->public1, &prime);
    if (sum.infinity) return empty;
    xcryptographic_p256_order(&order);
    xcryptographic_bn_mul_mod(&product, &context->private2, &context->secret, &order);
    xcryptographic_p256_scalar_mul(&own_public, &sum, &product);
    identifier = context->role == XCryptographic_EcjPakeRole_Client ? "client" : "server";
    if (context->role == XCryptographic_EcjPakeRole_Server) {
        if ((size_t)(end - position) < 3) return empty;
        *position++ = 3;
        *position++ = 0;
        *position++ = 23;
    }
    if (!xcryptographic_ecjpake_write_tls_point(&own_public, &position, end) ||
        !xcryptographic_ecjpake_write_zkp(&sum, &product, &own_public,
                                          identifier, &position, end))
        return empty;
    context->wrote_round_two = true;
    return XByteArrayView_create_data(buffer, (int64_t)(position - (uint8_t *)buffer));
}

bool XCryptographic_ecjPakeReadRoundTwo(XCryptographic_EcjPakeContext *context,
                                        XByteArrayView message)
{
    XCryptographic_EcPoint sum;
    XCryptographic_Bn prime;
    const uint8_t *position;
    const uint8_t *end;
    const char *identifier;
    if (!context || message.m_size <= 0 || !message.m_data || !context->wrote_round_one ||
        !context->read_round_one || context->read_round_two || !XCRYPTOGRAPHIC_ECJPAKE_ON)
        return false;
    position = message.m_data;
    end = message.m_data + (size_t)message.m_size;
    if (context->role == XCryptographic_EcjPakeRole_Client) {
        if ((size_t)(end - position) < 3 || position[0] != 3 || position[1] != 0 || position[2] != 23)
            return false;
        position += 3;
    }
    xcryptographic_p256_modulus(&prime);
    xcryptographic_p256_add(&sum, &context->public1, &context->public2, &prime);
    xcryptographic_p256_add(&sum, &sum, &context->peer1, &prime);
    if (sum.infinity ||
        !xcryptographic_ecjpake_read_tls_point(&context->peer_round2, &position, end))
        return false;
    identifier = context->role == XCryptographic_EcjPakeRole_Client ? "server" : "client";
    if (!xcryptographic_ecjpake_read_zkp(&sum, &context->peer_round2, identifier,
                                         &position, end) || position != end)
        return false;
    context->read_round_two = true;
    return true;
}

XByteArrayView XCryptographic_ecjPakeWriteSharedKeyInto(
    XCryptographic_EcjPakeContext *context, char *buffer, size_t bufferSize)
{
    XByteArrayView empty = { NULL, 0 };
    XCryptographic_Bn order, product, negative_product, prime;
    XCryptographic_EcPoint term, shared;
    if (!context || !buffer || bufferSize < 65 || !context->read_round_two ||
        !XCRYPTOGRAPHIC_ECJPAKE_ON)
        return empty;
    xcryptographic_p256_order(&order);
    xcryptographic_bn_mul_mod(&product, &context->private2, &context->secret, &order);
    xcryptographic_bn_zero(&negative_product);
    xcryptographic_bn_sub_mod(&negative_product, &negative_product, &product, &order);
    xcryptographic_p256_scalar_mul(&term, &context->peer2, &negative_product);
    xcryptographic_p256_modulus(&prime);
    xcryptographic_p256_add(&shared, &context->peer_round2, &term, &prime);
    if (shared.infinity) return empty;
    xcryptographic_p256_scalar_mul(&shared, &shared, &context->private2);
    if (!xcryptographic_p256_to_affine(&shared)) return empty;
    xcryptographic_p256_serialize(&shared, (uint8_t *)buffer);
    return XByteArrayView_create_data(buffer, 65);
}

bool XCryptographic_ecdhGenerateKey(XCryptographic_EcdhAlgorithm algorithm,
                                        XCryptographic_Key* result)
{
    XCryptographic_EcPoint base, publicPoint;
    uint8_t baseU[32] = { 9 };
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    if (algorithm == XCryptographic_EcdhAlgorithm_X25519) {
        if (!XCRYPTOGRAPHIC_X25519_ON ||
            !xcryptographic_random(result->privateKey, 32) ||
            !xcryptographic_x25519(result->privateKey, baseU, result->publicKey))
            return false;
        result->type = XCryptographic_KeyType_X25519;
        result->publicKeyLen = 32;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_X448) {
        uint8_t baseU[56] = { 5 };
        if (!XCRYPTOGRAPHIC_X448_ON || !xcryptographic_random(result->privateKey,56) ||
            !xcryptographic_x448(result->privateKey,baseU,result->publicKey)) return false;
        result->type=XCryptographic_KeyType_X448; result->publicKeyLen=56; return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_NistP384) {
        XCryptographic_Bn384 scalar;
        XCryptographic_EcPoint384 base384, publicPoint384;
        if (!XCRYPTOGRAPHIC_ECC_SECP384R1_ON ||
            !xcryptographic_p384_random_scalar(result->privateKey)) return false;
        xcryptographic_p384_point_base(&base384);
        xcryptographic_bn384_from_be(&scalar, result->privateKey);
        xcryptographic_p384_scalar_mul(&publicPoint384, &base384, &scalar);
        if (!xcryptographic_p384_to_affine(&publicPoint384)) return false;
        xcryptographic_p384_serialize(&publicPoint384, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhNistP384;
        result->publicKeyLen = 97;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_NistP521) {
        XCryptographic_Bn521 scalar;
        XCryptographic_EcPoint521 base521, publicPoint521;
        if (!XCRYPTOGRAPHIC_ECC_SECP521R1_ON ||
            !xcryptographic_p521_random_scalar(result->privateKey)) return false;
        xcryptographic_p521_point_base(&base521);
        xcryptographic_bn521_from_be(&scalar, result->privateKey);
        xcryptographic_p521_scalar_mul(&publicPoint521, &base521, &scalar);
        if (!xcryptographic_p521_to_affine(&publicPoint521)) return false;
        xcryptographic_p521_serialize(&publicPoint521, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhNistP521;
        result->publicKeyLen = 133;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP384r1) {
        XCryptographic_Bn384 scalar; XCryptographic_EcPointBrainpool384 base, publicPoint;
        if (!XCRYPTOGRAPHIC_ECC_BRAINPOOL_P384R1_ON || !xcryptographic_brainpool_p384r1_random_scalar(result->privateKey)) return false;
        xcryptographic_brainpool_p384r1_point_base(&base); xcryptographic_bn384_from_be(&scalar,result->privateKey);
        xcryptographic_brainpool_p384r1_scalar_mul(&publicPoint,&base,&scalar);
        if (!xcryptographic_brainpool_p384r1_to_affine(&publicPoint)) return false;
        xcryptographic_brainpool_p384r1_serialize(&publicPoint,result->publicKey);
        result->type=XCryptographic_KeyType_EcdhBrainpoolP384r1; result->publicKeyLen=97; return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP512r1) {
        XCryptographic_Bn521 scalar;
        XCryptographic_EcPointBrainpool512 base, publicPoint;
        if (!XCRYPTOGRAPHIC_ECC_BRAINPOOL_P512R1_ON ||
            !xcryptographic_brainpool_p512r1_random_scalar(result->privateKey)) return false;
        xcryptographic_brainpool_p512r1_point_base(&base);
        xcryptographic_bn521_from_be64(&scalar, result->privateKey);
        xcryptographic_brainpool_p512r1_scalar_mul(&publicPoint, &base, &scalar);
        if (!xcryptographic_brainpool_p512r1_to_affine(&publicPoint)) return false;
        xcryptographic_brainpool_p512r1_serialize(&publicPoint, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhBrainpoolP512r1;
        result->publicKeyLen = 129;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_Secp256k1) {
        XCryptographic_Bn scalar;
        if (!XCRYPTOGRAPHIC_ECC_SECP256K1_ON ||
            !xcryptographic_secp256k1_random_scalar(result->privateKey)) return false;
        xcryptographic_secp256k1_point_base(&base);
        xcryptographic_bn_from_be(&scalar, result->privateKey);
        xcryptographic_secp256k1_scalar_mul(&publicPoint, &base, &scalar);
        if (!xcryptographic_secp256k1_to_affine(&publicPoint)) return false;
        xcryptographic_p256_serialize(&publicPoint, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhSecp256k1;
        result->publicKeyLen = 65;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP256r1) {
        XCryptographic_Bn scalar;
        if (!XCRYPTOGRAPHIC_ECC_BRAINPOOL_P256R1_ON ||
            !xcryptographic_brainpool_p256r1_random_scalar(result->privateKey)) return false;
        xcryptographic_brainpool_p256r1_point_base(&base);
        xcryptographic_bn_from_be(&scalar, result->privateKey);
        xcryptographic_brainpool_p256r1_scalar_mul(&publicPoint, &base, &scalar);
        if (!xcryptographic_brainpool_p256r1_to_affine(&publicPoint)) return false;
        xcryptographic_p256_serialize(&publicPoint, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhBrainpoolP256r1;
        result->publicKeyLen = 65;
        return true;
    }
    if (algorithm != XCryptographic_EcdhAlgorithm_NistP256 ||
        !XCRYPTOGRAPHIC_ECDH_NISTP256_ON ||
        !xcryptographic_random_scalar(result->privateKey)) return false;
    xcryptographic_p256_point_base(&base);
    {
        XCryptographic_Bn scalar;
        xcryptographic_bn_from_be(&scalar, result->privateKey);
        xcryptographic_p256_scalar_mul(&publicPoint, &base, &scalar);
    }
    if (!xcryptographic_p256_to_affine(&publicPoint)) return false;
    xcryptographic_p256_serialize(&publicPoint, result->publicKey);
    result->type = XCryptographic_KeyType_EcdhNistP256;
    result->publicKeyLen = 65;
    return true;
}

bool XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm algorithm,
                                         XByteArrayView privateKey,
                                         XCryptographic_Key* result)
{
    XCryptographic_Bn scalar;
    XCryptographic_EcPoint base, publicPoint;

    if (!result || privateKey.m_size < 0 || !privateKey.m_data) return false;
    memset(result, 0, sizeof(*result));
    if (algorithm == XCryptographic_EcdhAlgorithm_X25519) {
        uint8_t baseU[32] = { 9 };
        if (privateKey.m_size != 32 || !XCRYPTOGRAPHIC_X25519_ON) return false;
        memcpy(result->privateKey, privateKey.m_data, 32);
        if (!xcryptographic_x25519(result->privateKey, baseU, result->publicKey)) return false;
        result->type = XCryptographic_KeyType_X25519;
        result->publicKeyLen = 32;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_X448) {
        uint8_t baseU[56] = { 5 };
        if (privateKey.m_size!=56 || !XCRYPTOGRAPHIC_X448_ON) return false;
        memcpy(result->privateKey,privateKey.m_data,56);
        if (!xcryptographic_x448(result->privateKey,baseU,result->publicKey)) return false;
        result->type=XCryptographic_KeyType_X448; result->publicKeyLen=56; return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_NistP384) {
        XCryptographic_Bn384 scalar;
        XCryptographic_EcPoint384 base384, publicPoint384;
        if (privateKey.m_size != 48 || !XCRYPTOGRAPHIC_ECC_SECP384R1_ON) return false;
        memcpy(result->privateKey, privateKey.m_data, 48);
        if (!xcryptographic_p384_scalar_valid(result->privateKey)) return false;
        xcryptographic_p384_point_base(&base384);
        xcryptographic_bn384_from_be(&scalar, result->privateKey);
        xcryptographic_p384_scalar_mul(&publicPoint384, &base384, &scalar);
        if (!xcryptographic_p384_to_affine(&publicPoint384)) return false;
        xcryptographic_p384_serialize(&publicPoint384, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhNistP384;
        result->publicKeyLen = 97;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_NistP521) {
        XCryptographic_Bn521 scalar;
        XCryptographic_EcPoint521 base521, publicPoint521;
        if (privateKey.m_size != 66 || !XCRYPTOGRAPHIC_ECC_SECP521R1_ON) return false;
        memcpy(result->privateKey, privateKey.m_data, 66);
        if (!xcryptographic_p521_scalar_valid(result->privateKey)) return false;
        xcryptographic_p521_point_base(&base521);
        xcryptographic_bn521_from_be(&scalar, result->privateKey);
        xcryptographic_p521_scalar_mul(&publicPoint521, &base521, &scalar);
        if (!xcryptographic_p521_to_affine(&publicPoint521)) return false;
        xcryptographic_p521_serialize(&publicPoint521, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhNistP521;
        result->publicKeyLen = 133;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP384r1) {
        XCryptographic_Bn384 scalar; XCryptographic_EcPointBrainpool384 base, publicPoint;
        if (privateKey.m_size != 48 || !XCRYPTOGRAPHIC_ECC_BRAINPOOL_P384R1_ON) return false;
        memcpy(result->privateKey,privateKey.m_data,48);
        if (!xcryptographic_brainpool_p384r1_scalar_valid(result->privateKey)) return false;
        xcryptographic_brainpool_p384r1_point_base(&base); xcryptographic_bn384_from_be(&scalar,result->privateKey);
        xcryptographic_brainpool_p384r1_scalar_mul(&publicPoint,&base,&scalar);
        if (!xcryptographic_brainpool_p384r1_to_affine(&publicPoint)) return false;
        xcryptographic_brainpool_p384r1_serialize(&publicPoint,result->publicKey);
        result->type=XCryptographic_KeyType_EcdhBrainpoolP384r1; result->publicKeyLen=97; return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP512r1) {
        XCryptographic_Bn521 scalar;
        XCryptographic_EcPointBrainpool512 base, publicPoint;
        if (privateKey.m_size != 64 || !XCRYPTOGRAPHIC_ECC_BRAINPOOL_P512R1_ON) return false;
        memcpy(result->privateKey, privateKey.m_data, 64);
        if (!xcryptographic_brainpool_p512r1_scalar_valid(result->privateKey)) return false;
        xcryptographic_brainpool_p512r1_point_base(&base);
        xcryptographic_bn521_from_be64(&scalar, result->privateKey);
        xcryptographic_brainpool_p512r1_scalar_mul(&publicPoint, &base, &scalar);
        if (!xcryptographic_brainpool_p512r1_to_affine(&publicPoint)) return false;
        xcryptographic_brainpool_p512r1_serialize(&publicPoint, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhBrainpoolP512r1;
        result->publicKeyLen = 129;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_Secp256k1) {
        if (privateKey.m_size != 32 || !XCRYPTOGRAPHIC_ECC_SECP256K1_ON) return false;
        memcpy(result->privateKey, privateKey.m_data, 32);
        if (!xcryptographic_secp256k1_scalar_valid(result->privateKey)) return false;
        xcryptographic_bn_from_be(&scalar, result->privateKey);
        xcryptographic_secp256k1_point_base(&base);
        xcryptographic_secp256k1_scalar_mul(&publicPoint, &base, &scalar);
        if (!xcryptographic_secp256k1_to_affine(&publicPoint)) return false;
        xcryptographic_p256_serialize(&publicPoint, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhSecp256k1;
        result->publicKeyLen = 65;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP256r1) {
        if (privateKey.m_size != 32 || !XCRYPTOGRAPHIC_ECC_BRAINPOOL_P256R1_ON)
            return false;
        memcpy(result->privateKey, privateKey.m_data, 32);
        if (!xcryptographic_brainpool_p256r1_scalar_valid(result->privateKey)) return false;
        xcryptographic_bn_from_be(&scalar, result->privateKey);
        xcryptographic_brainpool_p256r1_point_base(&base);
        xcryptographic_brainpool_p256r1_scalar_mul(&publicPoint, &base, &scalar);
        if (!xcryptographic_brainpool_p256r1_to_affine(&publicPoint)) return false;
        xcryptographic_p256_serialize(&publicPoint, result->publicKey);
        result->type = XCryptographic_KeyType_EcdhBrainpoolP256r1;
        result->publicKeyLen = 65;
        return true;
    }
    if (algorithm != XCryptographic_EcdhAlgorithm_NistP256 ||
        privateKey.m_size != 32 || !XCRYPTOGRAPHIC_ECDH_NISTP256_ON) return false;
    memcpy(result->privateKey, privateKey.m_data, 32);
    xcryptographic_bn_from_be(&scalar, result->privateKey);
    if (!xcryptographic_scalar_valid(result->privateKey)) return false;
    xcryptographic_p256_point_base(&base);
    xcryptographic_p256_scalar_mul(&publicPoint, &base, &scalar);
    if (!xcryptographic_p256_to_affine(&publicPoint)) return false;
    xcryptographic_p256_serialize(&publicPoint, result->publicKey);
    result->type = XCryptographic_KeyType_EcdhNistP256;
    result->publicKeyLen = 65;
    return true;
}

bool XCryptographic_ecdhImportPublicKey(XCryptographic_EcdhAlgorithm algorithm,
                                        XByteArrayView publicKey,
                                        XCryptographic_Key* result)
{
    if (!result || publicKey.m_size < 0 || !publicKey.m_data) return false;
    if (algorithm == XCryptographic_EcdhAlgorithm_X25519) {
        if (publicKey.m_size != 32 || !XCRYPTOGRAPHIC_X25519_ON) return false;
        memset(result, 0, sizeof(*result));
        memcpy(result->publicKey, publicKey.m_data, 32);
        result->publicKeyLen = 32;
        result->type = XCryptographic_KeyType_X25519;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_X448) {
        if (publicKey.m_size!=56 || !XCRYPTOGRAPHIC_X448_ON) return false;
        memset(result,0,sizeof(*result));memcpy(result->publicKey,publicKey.m_data,56);
        result->publicKeyLen=56;result->type=XCryptographic_KeyType_X448;return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_NistP384 &&
        publicKey.m_size == 97 && XCRYPTOGRAPHIC_ECC_SECP384R1_ON) {
        XCryptographic_EcPoint384 point;
        if (!xcryptographic_p384_parse(publicKey.m_data, (size_t)publicKey.m_size, &point)) {
            return false;
        }
        memset(result, 0, sizeof(*result));
        memcpy(result->publicKey, publicKey.m_data, 97);
        result->publicKeyLen = 97;
        result->type = XCryptographic_KeyType_EcdhNistP384;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_NistP521 &&
        publicKey.m_size == 133 && XCRYPTOGRAPHIC_ECC_SECP521R1_ON) {
        XCryptographic_EcPoint521 point;
        if (!xcryptographic_p521_parse(publicKey.m_data, (size_t)publicKey.m_size, &point))
            return false;
        memset(result, 0, sizeof(*result));
        memcpy(result->publicKey, publicKey.m_data, 133);
        result->publicKeyLen = 133;
        result->type = XCryptographic_KeyType_EcdhNistP521;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP384r1 &&
        publicKey.m_size == 97 && XCRYPTOGRAPHIC_ECC_BRAINPOOL_P384R1_ON) {
        XCryptographic_EcPointBrainpool384 point;
        if (!xcryptographic_brainpool_p384r1_parse(publicKey.m_data,(size_t)publicKey.m_size,&point)) return false;
        memset(result,0,sizeof(*result)); memcpy(result->publicKey,publicKey.m_data,97);
        result->publicKeyLen=97; result->type=XCryptographic_KeyType_EcdhBrainpoolP384r1; return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP512r1 &&
        publicKey.m_size == 129 && XCRYPTOGRAPHIC_ECC_BRAINPOOL_P512R1_ON) {
        XCryptographic_EcPointBrainpool512 point;
        if (!xcryptographic_brainpool_p512r1_parse(
                publicKey.m_data, (size_t)publicKey.m_size, &point)) return false;
        memset(result, 0, sizeof(*result));
        memcpy(result->publicKey, publicKey.m_data, 129);
        result->publicKeyLen = 129;
        result->type = XCryptographic_KeyType_EcdhBrainpoolP512r1;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_NistP256 &&
        publicKey.m_size == 65 && XCRYPTOGRAPHIC_ECDH_NISTP256_ON) {
        XCryptographic_EcPoint point;
        if (!xcryptographic_p256_parse(publicKey.m_data, (size_t)publicKey.m_size, &point)) {
            return false;
        }
        memset(result, 0, sizeof(*result));
        memcpy(result->publicKey, publicKey.m_data, 65);
        result->publicKeyLen = 65;
        result->type = XCryptographic_KeyType_EcdhNistP256;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_Secp256k1 &&
        publicKey.m_size == 65 && XCRYPTOGRAPHIC_ECC_SECP256K1_ON) {
        XCryptographic_EcPoint point;
        if (!xcryptographic_secp256k1_parse(publicKey.m_data,
                                             (size_t)publicKey.m_size, &point)) return false;
        memset(result, 0, sizeof(*result));
        memcpy(result->publicKey, publicKey.m_data, 65);
        result->publicKeyLen = 65;
        result->type = XCryptographic_KeyType_EcdhSecp256k1;
        return true;
    }
    if (algorithm == XCryptographic_EcdhAlgorithm_BrainpoolP256r1 &&
        publicKey.m_size == 65 && XCRYPTOGRAPHIC_ECC_BRAINPOOL_P256R1_ON) {
        XCryptographic_EcPoint point;
        if (!xcryptographic_brainpool_p256r1_parse(publicKey.m_data,
                                                    (size_t)publicKey.m_size, &point)) return false;
        memset(result, 0, sizeof(*result));
        memcpy(result->publicKey, publicKey.m_data, 65);
        result->publicKeyLen = 65;
        result->type = XCryptographic_KeyType_EcdhBrainpoolP256r1;
        return true;
    }
    return false;
}

static bool xcryptographic_export_public_key(XCryptographic_Key key,
                                                   void* output, size_t outputCap,
                                                   size_t* outputLen)
{
    if (!output || !outputLen || key.publicKeyLen == 0 ||
        key.publicKeyLen > outputCap) return false;
    memcpy(output, key.publicKey, key.publicKeyLen);
    *outputLen = key.publicKeyLen;
    return true;
}

static bool xcryptographic_ecdh_agree(XCryptographic_Key privateKey,
                                            const void* peerPublicKey, size_t peerPublicKeyLen,
                                            void* output, size_t outputCap, size_t* outputLen)
{
    if (!peerPublicKey || !output || !outputLen) return false;
    if (privateKey.type == XCryptographic_KeyType_X25519) {
        if (outputCap < 32 || peerPublicKeyLen != 32 ||
            !xcryptographic_x25519(privateKey.privateKey,
                                         (const uint8_t*)peerPublicKey,
                                         (uint8_t*)output)) return false;
        *outputLen = 32;
        return true;
    }
    if (privateKey.type == XCryptographic_KeyType_X448) {
        if (outputCap<56 || peerPublicKeyLen!=56 || !xcryptographic_x448(privateKey.privateKey,(const uint8_t*)peerPublicKey,(uint8_t*)output)) return false;
        *outputLen=56;return true;
    }
    if (privateKey.type == XCryptographic_KeyType_EcdhNistP256) {
        XCryptographic_EcPoint peer, shared;
        XCryptographic_Bn scalar;
        if (outputCap < 32 || !xcryptographic_p256_parse((const uint8_t*)peerPublicKey,
                                            peerPublicKeyLen, &peer)) return false;
        xcryptographic_bn_from_be(&scalar, privateKey.privateKey);
        xcryptographic_p256_scalar_mul(&shared, &peer, &scalar);
        if (!xcryptographic_p256_to_affine(&shared)) return false;
        xcryptographic_bn_to_be(&shared.x, (uint8_t*)output);
        *outputLen = 32;
        return true;
    }
    if (privateKey.type == XCryptographic_KeyType_EcdhSecp256k1) {
        XCryptographic_EcPoint peer, shared;
        XCryptographic_Bn scalar;
        if (outputCap < 32 || !xcryptographic_secp256k1_parse((const uint8_t*)peerPublicKey,
                                             peerPublicKeyLen, &peer)) return false;
        xcryptographic_bn_from_be(&scalar, privateKey.privateKey);
        xcryptographic_secp256k1_scalar_mul(&shared, &peer, &scalar);
        if (!xcryptographic_secp256k1_to_affine(&shared)) return false;
        xcryptographic_bn_to_be(&shared.x, (uint8_t*)output);
        *outputLen = 32;
        return true;
    }
    if (privateKey.type == XCryptographic_KeyType_EcdhBrainpoolP256r1) {
        XCryptographic_EcPoint peer, shared;
        XCryptographic_Bn scalar;
        if (outputCap < 32 || !xcryptographic_brainpool_p256r1_parse((const uint8_t*)peerPublicKey,
                                                    peerPublicKeyLen, &peer)) return false;
        xcryptographic_bn_from_be(&scalar, privateKey.privateKey);
        xcryptographic_brainpool_p256r1_scalar_mul(&shared, &peer, &scalar);
        if (!xcryptographic_brainpool_p256r1_to_affine(&shared)) return false;
        xcryptographic_bn_to_be(&shared.x, (uint8_t*)output);
        *outputLen = 32;
        return true;
    }
    if (privateKey.type == XCryptographic_KeyType_EcdhNistP384) {
        XCryptographic_EcPoint384 peer, shared;
        XCryptographic_Bn384 scalar;
        if (outputCap < 48 || !xcryptographic_p384_parse((const uint8_t*)peerPublicKey,
                                                           peerPublicKeyLen, &peer))
            return false;
        xcryptographic_bn384_from_be(&scalar, privateKey.privateKey);
        xcryptographic_p384_scalar_mul(&shared, &peer, &scalar);
        if (!xcryptographic_p384_to_affine(&shared)) return false;
        xcryptographic_bn384_to_be(&shared.x, (uint8_t*)output);
        *outputLen = 48;
        return true;
    }
    if (privateKey.type == XCryptographic_KeyType_EcdhNistP521) {
        XCryptographic_EcPoint521 peer, shared;
        XCryptographic_Bn521 scalar;
        if (outputCap < 66 || !xcryptographic_p521_parse((const uint8_t*)peerPublicKey,
                                                          peerPublicKeyLen, &peer)) return false;
        xcryptographic_bn521_from_be(&scalar, privateKey.privateKey);
        xcryptographic_p521_scalar_mul(&shared, &peer, &scalar);
        if (!xcryptographic_p521_to_affine(&shared)) return false;
        xcryptographic_bn521_to_be(&shared.x, (uint8_t*)output);
        *outputLen = 66;
        return true;
    }
    if (privateKey.type == XCryptographic_KeyType_EcdhBrainpoolP384r1) {
        XCryptographic_EcPointBrainpool384 peer, shared; XCryptographic_Bn384 scalar;
        if (outputCap < 48 || !xcryptographic_brainpool_p384r1_parse((const uint8_t*)peerPublicKey,peerPublicKeyLen,&peer)) return false;
        xcryptographic_bn384_from_be(&scalar,privateKey.privateKey);
        xcryptographic_brainpool_p384r1_scalar_mul(&shared,&peer,&scalar);
        if (!xcryptographic_brainpool_p384r1_to_affine(&shared)) return false;
        xcryptographic_bn384_to_be(&shared.x,(uint8_t*)output); *outputLen=48; return true;
    }
    if (privateKey.type == XCryptographic_KeyType_EcdhBrainpoolP512r1) {
        XCryptographic_EcPointBrainpool512 peer, shared;
        XCryptographic_Bn521 scalar;
        if (outputCap < 64 || !xcryptographic_brainpool_p512r1_parse(
                (const uint8_t*)peerPublicKey, peerPublicKeyLen, &peer)) return false;
        xcryptographic_bn521_from_be64(&scalar, privateKey.privateKey);
        xcryptographic_brainpool_p512r1_scalar_mul(&shared, &peer, &scalar);
        if (!xcryptographic_brainpool_p512r1_to_affine(&shared)) return false;
        xcryptographic_bn521_to_be64(&shared.x, (uint8_t*)output);
        *outputLen = 64;
        return true;
    }
    return false;
}

static bool xcryptographic_ecdsa_p256_import_private_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPoint base, point;
    XCryptographic_Bn scalar;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP256_ON || !key || !result || keyLen != 32 ||
        !xcryptographic_scalar_valid((const uint8_t*)key)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->privateKey, key, 32);
    xcryptographic_p256_point_base(&base);
    xcryptographic_bn_from_be(&scalar, result->privateKey);
    xcryptographic_p256_scalar_mul(&point, &base, &scalar);
    if (!xcryptographic_p256_to_affine(&point)) return false;
    xcryptographic_p256_serialize(&point, result->publicKey);
    result->publicKeyLen = 65;
    result->type = XCryptographic_KeyType_EcdsaNistP256Private;
    return true;
}

static bool xcryptographic_ecdsa_p256_import_public_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPoint point;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP256_ON || !result ||
        !xcryptographic_p256_parse((const uint8_t*)key, keyLen, &point)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->publicKey, key, keyLen);
    result->publicKeyLen = keyLen;
    result->type = XCryptographic_KeyType_EcdsaNistP256Public;
    return true;
}

static void xcryptographic_scalar_from_hash(XCryptographic_Bn* out,
                                                 const void* hash, size_t hashLen,
                                                 const XCryptographic_Bn* order)
{
    uint8_t bytes[32] = { 0 };
    XCryptographic_Bn temp;
    if (hashLen > 32) hashLen = 32;
    memcpy(bytes + 32 - hashLen, hash, hashLen);
    xcryptographic_bn_from_be(&temp, bytes);
    if (xcryptographic_bn_cmp(&temp, order) >= 0)
        xcryptographic_bn_sub_raw(&temp, &temp, order);
    *out = temp;
}

static bool xcryptographic_rfc6979_k(
    uint8_t kOut[32], const XCryptographic_Key* key, const void* hash, size_t hashLen,
    const XCryptographic_Bn* order, XCryptographic_KeyType privateType)
{
    uint8_t V[32], K[32], x[32], h[32], buf[32 + 1 + 32 + 32];
    uint32_t i;
    XCryptographic_Bn kbn, hbn;
    if (!kOut || !key || !hash || !order) return false;
    if (key->type != privateType) return false;
    memset(V, 0x01, sizeof(V));
    memset(K, 0x00, sizeof(K));
    memcpy(x, key->privateKey, 32);
    /* bits2octets(h1)：先 bits2int 再取模 n，输出 32 字节。 */
    memset(h, 0, sizeof(h));
    if (hashLen > 32) hashLen = 32;
    memcpy(h + 32 - hashLen, hash, hashLen);
    xcryptographic_bn_from_be(&hbn, h);
    if (xcryptographic_bn_cmp(&hbn, order) >= 0)
        xcryptographic_bn_sub_raw(&hbn, &hbn, order);
    xcryptographic_bn_to_be(&hbn, h);
    /* K = HMAC_K(V || 0x00 || int2octets(x) || bits2octets(h1)) */
    memcpy(buf, V, 32); buf[32] = 0x00;
    memcpy(buf + 33, x, 32); memcpy(buf + 65, h, 32);
    if (!XCryptographicHash_hmacInto((char*)K, sizeof(K), (const char*)K, 32,
                                     (const char*)buf, 97, XCryptographicHash_Sha256).m_data)
        return false;
    /* V = HMAC_K(V) */
    if (!XCryptographicHash_hmacInto((char*)V, sizeof(V), (const char*)K, 32,
                                     (const char*)V, 32, XCryptographicHash_Sha256).m_data)
        return false;
    /* K = HMAC_K(V || 0x01 || int2octets(x) || bits2octets(h1)) */
    memcpy(buf, V, 32); buf[32] = 0x01;
    memcpy(buf + 33, x, 32); memcpy(buf + 65, h, 32);
    if (!XCryptographicHash_hmacInto((char*)K, sizeof(K), (const char*)K, 32,
                                     (const char*)buf, 97, XCryptographicHash_Sha256).m_data)
        return false;
    /* V = HMAC_K(V) */
    if (!XCryptographicHash_hmacInto((char*)V, sizeof(V), (const char*)K, 32,
                                     (const char*)V, 32, XCryptographicHash_Sha256).m_data)
        return false;
    for (i = 0; i < 1024; ++i) {
        /* V = HMAC_K(V)；T = V */
        if (!XCryptographicHash_hmacInto((char*)V, sizeof(V), (const char*)K, 32,
                                         (const char*)V, 32, XCryptographicHash_Sha256).m_data)
            return false;
        memcpy(kOut, V, 32);
        xcryptographic_bn_from_be(&kbn, kOut);
        if (!xcryptographic_bn_is_zero(&kbn) && xcryptographic_bn_cmp(&kbn, order) < 0) {
            memset(V, 0, sizeof(V)); memset(K, 0, sizeof(K));
            memset(x, 0, sizeof(x)); memset(h, 0, sizeof(h)); memset(buf, 0, sizeof(buf));
            return true;
        }
        /* K = HMAC_K(V || 0x00) */
        memcpy(buf, V, 32); buf[32] = 0x00;
        if (!XCryptographicHash_hmacInto((char*)K, sizeof(K), (const char*)K, 32,
                                         (const char*)buf, 33, XCryptographicHash_Sha256).m_data)
            return false;
        /* V = HMAC_K(V) */
        if (!XCryptographicHash_hmacInto((char*)V, sizeof(V), (const char*)K, 32,
                                         (const char*)V, 32, XCryptographicHash_Sha256).m_data)
            return false;
    }
    memset(V, 0, sizeof(V)); memset(K, 0, sizeof(K));
    memset(x, 0, sizeof(x)); memset(h, 0, sizeof(h)); memset(buf, 0, sizeof(buf));
    return false;
}

static bool xcryptographic_ecdsa_p256_sign_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    void* signature, size_t signatureCap, size_t* signatureLen)
{
    XCryptographic_Bn order, d, z, k, kinv, r, s, rd, sum;
    XCryptographic_EcPoint base, point;
    uint8_t kBytes[32];
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP256_ON ||
        key.type != XCryptographic_KeyType_EcdsaNistP256Private ||
        !hash || !signature || !signatureLen || signatureCap < 64) return false;
    xcryptographic_p256_order(&order);
    xcryptographic_bn_from_be(&d, key.privateKey);
    xcryptographic_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_p256_point_base(&base);
    do {
        if (!xcryptographic_random_scalar(kBytes)) return false;
        xcryptographic_bn_from_be(&k, kBytes);
        xcryptographic_p256_scalar_mul(&point, &base, &k);
        if (!xcryptographic_p256_to_affine(&point)) continue;
        xcryptographic_bn_to_be(&point.x, kBytes);
        xcryptographic_bn_from_be(&r, kBytes);
        if (xcryptographic_bn_cmp(&r, &order) >= 0)
            xcryptographic_bn_sub_raw(&r, &r, &order);
    } while (xcryptographic_bn_is_zero(&r));
    xcryptographic_bn_mul_mod(&rd, &r, &d, &order);
    xcryptographic_bn_add_mod(&sum, &z, &rd, &order);
    xcryptographic_bn_inverse_mod(&kinv, &k, &order);
    xcryptographic_bn_mul_mod(&s, &kinv, &sum, &order);
    if (xcryptographic_bn_is_zero(&s)) return false;
    xcryptographic_bn_to_be(&r, (uint8_t*)signature);
    xcryptographic_bn_to_be(&s, (uint8_t*)signature + 32);
    *signatureLen = 64;
    return true;
}

static bool xcryptographic_ecdsa_p256_sign_hash_deterministic(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    void* signature, size_t signatureCap, size_t* signatureLen)
{
    XCryptographic_Bn order, d, z, k, kinv, r, s, rd, sum;
    XCryptographic_EcPoint base, point;
    uint8_t kBytes[32];
    if (!XCRYPTOGRAPHIC_ECDSA_DETERMINISTIC_NISTP256_ON ||
        key.type != XCryptographic_KeyType_EcdsaNistP256Private ||
        !hash || !signature || !signatureLen || signatureCap < 64) return false;
    xcryptographic_p256_order(&order);
    xcryptographic_bn_from_be(&d, key.privateKey);
    xcryptographic_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_p256_point_base(&base);
    do {
        if (!xcryptographic_rfc6979_k(kBytes, &key, hash, hashLen, &order,
                                      XCryptographic_KeyType_EcdsaNistP256Private))
            return false;
        xcryptographic_bn_from_be(&k, kBytes);
        xcryptographic_p256_scalar_mul(&point, &base, &k);
        if (!xcryptographic_p256_to_affine(&point)) continue;
        xcryptographic_bn_to_be(&point.x, kBytes);
        xcryptographic_bn_from_be(&r, kBytes);
        if (xcryptographic_bn_cmp(&r, &order) >= 0)
            xcryptographic_bn_sub_raw(&r, &r, &order);
    } while (xcryptographic_bn_is_zero(&r));
    xcryptographic_bn_mul_mod(&rd, &r, &d, &order);
    xcryptographic_bn_add_mod(&sum, &z, &rd, &order);
    xcryptographic_bn_inverse_mod(&kinv, &k, &order);
    xcryptographic_bn_mul_mod(&s, &kinv, &sum, &order);
    if (xcryptographic_bn_is_zero(&s)) return false;
    xcryptographic_bn_to_be(&r, (uint8_t*)signature);
    xcryptographic_bn_to_be(&s, (uint8_t*)signature + 32);
    *signatureLen = 64;
    return true;
}

static bool xcryptographic_ecdsa_p256_verify_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    const void* signature, size_t signatureLen)
{
    XCryptographic_Bn order, prime, r, s, z, w, u1, u2;
    XCryptographic_EcPoint base, publicPoint, p1, p2, sum;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP256_ON ||
        key.type != XCryptographic_KeyType_EcdsaNistP256Public ||
        !hash || !signature || signatureLen != 64) return false;
    xcryptographic_p256_order(&order);
    xcryptographic_bn_from_be(&r, (const uint8_t*)signature);
    xcryptographic_bn_from_be(&s, (const uint8_t*)signature + 32);
    if (xcryptographic_bn_is_zero(&r) || xcryptographic_bn_is_zero(&s) ||
        xcryptographic_bn_cmp(&r, &order) >= 0 ||
        xcryptographic_bn_cmp(&s, &order) >= 0) return false;
    if (!xcryptographic_p256_parse(key.publicKey, key.publicKeyLen, &publicPoint))
        return false;
    xcryptographic_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_bn_inverse_mod(&w, &s, &order);
    xcryptographic_bn_mul_mod(&u1, &z, &w, &order);
    xcryptographic_bn_mul_mod(&u2, &r, &w, &order);
    xcryptographic_p256_point_base(&base);
    xcryptographic_p256_scalar_mul(&p1, &base, &u1);
    xcryptographic_p256_scalar_mul(&p2, &publicPoint, &u2);
    if (!xcryptographic_p256_to_affine(&p1) ||
        !xcryptographic_p256_to_affine(&p2)) return false;
    xcryptographic_p256_modulus(&prime);
    xcryptographic_p256_affine_add(&sum, &p1, &p2, &prime);
    if (sum.infinity) return false;
    if (xcryptographic_bn_cmp(&sum.x, &order) >= 0)
        xcryptographic_bn_sub_raw(&sum.x, &sum.x, &order);
    return xcryptographic_bn_cmp(&sum.x, &r) == 0;
}

static bool xcryptographic_ecdsa_secp256k1_import_private_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPoint base, point;
    XCryptographic_Bn scalar;
    if (!XCRYPTOGRAPHIC_ECDSA_SECP256K1_ON || !key || !result || keyLen != 32 ||
        !xcryptographic_secp256k1_scalar_valid((const uint8_t*)key)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->privateKey, key, 32);
    xcryptographic_secp256k1_point_base(&base);
    xcryptographic_bn_from_be(&scalar, result->privateKey);
    xcryptographic_secp256k1_scalar_mul(&point, &base, &scalar);
    if (!xcryptographic_secp256k1_to_affine(&point)) return false;
    xcryptographic_p256_serialize(&point, result->publicKey);
    result->publicKeyLen = 65;
    result->type = XCryptographic_KeyType_EcdsaSecp256k1Private;
    return true;
}

static bool xcryptographic_ecdsa_secp256k1_import_public_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPoint point;
    if (!XCRYPTOGRAPHIC_ECDSA_SECP256K1_ON || !result ||
        !xcryptographic_secp256k1_parse((const uint8_t*)key, keyLen, &point)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->publicKey, key, keyLen);
    result->publicKeyLen = keyLen;
    result->type = XCryptographic_KeyType_EcdsaSecp256k1Public;
    return true;
}

static bool xcryptographic_ecdsa_secp256k1_sign_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    void* signature, size_t signatureCap, size_t* signatureLen, bool deterministic)
{
    XCryptographic_Bn order, d, z, k, kinv, r, s, rd, sum;
    XCryptographic_EcPoint base, point;
    uint8_t kBytes[32];
    if ((!deterministic && !XCRYPTOGRAPHIC_ECDSA_SECP256K1_ON) ||
        (deterministic && !XCRYPTOGRAPHIC_ECDSA_DETERMINISTIC_SECP256K1_ON) ||
        key.type != XCryptographic_KeyType_EcdsaSecp256k1Private || !hash ||
        !signature || !signatureLen || signatureCap < 64) return false;
    xcryptographic_secp256k1_order(&order);
    xcryptographic_bn_from_be(&d, key.privateKey);
    xcryptographic_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_secp256k1_point_base(&base);
    do {
        if (deterministic) {
            if (!xcryptographic_rfc6979_k(
                    kBytes, &key, hash, hashLen, &order,
                    XCryptographic_KeyType_EcdsaSecp256k1Private)) return false;
        } else if (!xcryptographic_secp256k1_random_scalar(kBytes)) {
            return false;
        }
        xcryptographic_bn_from_be(&k, kBytes);
        xcryptographic_secp256k1_scalar_mul(&point, &base, &k);
        if (!xcryptographic_secp256k1_to_affine(&point)) continue;
        xcryptographic_bn_to_be(&point.x, kBytes);
        xcryptographic_bn_from_be(&r, kBytes);
        if (xcryptographic_bn_cmp(&r, &order) >= 0)
            xcryptographic_bn_sub_raw(&r, &r, &order);
    } while (xcryptographic_bn_is_zero(&r));
    xcryptographic_bn_mul_mod(&rd, &r, &d, &order);
    xcryptographic_bn_add_mod(&sum, &z, &rd, &order);
    xcryptographic_bn_inverse_mod(&kinv, &k, &order);
    xcryptographic_bn_mul_mod(&s, &kinv, &sum, &order);
    if (xcryptographic_bn_is_zero(&s)) return false;
    xcryptographic_bn_to_be(&r, (uint8_t*)signature);
    xcryptographic_bn_to_be(&s, (uint8_t*)signature + 32);
    *signatureLen = 64;
    return true;
}

static bool xcryptographic_ecdsa_secp256k1_verify_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    const void* signature, size_t signatureLen)
{
    XCryptographic_Bn order, prime, r, s, z, w, u1, u2;
    XCryptographic_EcPoint base, publicPoint, p1, p2, sum;
    if (!XCRYPTOGRAPHIC_ECDSA_SECP256K1_ON ||
        key.type != XCryptographic_KeyType_EcdsaSecp256k1Public || !hash ||
        !signature || signatureLen != 64) return false;
    xcryptographic_secp256k1_order(&order);
    xcryptographic_bn_from_be(&r, (const uint8_t*)signature);
    xcryptographic_bn_from_be(&s, (const uint8_t*)signature + 32);
    if (xcryptographic_bn_is_zero(&r) || xcryptographic_bn_is_zero(&s) ||
        xcryptographic_bn_cmp(&r, &order) >= 0 ||
        xcryptographic_bn_cmp(&s, &order) >= 0) return false;
    if (!xcryptographic_secp256k1_parse(key.publicKey, key.publicKeyLen, &publicPoint))
        return false;
    xcryptographic_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_bn_inverse_mod(&w, &s, &order);
    xcryptographic_bn_mul_mod(&u1, &z, &w, &order);
    xcryptographic_bn_mul_mod(&u2, &r, &w, &order);
    xcryptographic_secp256k1_point_base(&base);
    xcryptographic_secp256k1_scalar_mul(&p1, &base, &u1);
    xcryptographic_secp256k1_scalar_mul(&p2, &publicPoint, &u2);
    if (!xcryptographic_secp256k1_to_affine(&p1) ||
        !xcryptographic_secp256k1_to_affine(&p2)) return false;
    xcryptographic_secp256k1_modulus(&prime);
    xcryptographic_secp256k1_add(&sum, &p1, &p2, &prime);
    if (sum.infinity || !xcryptographic_secp256k1_to_affine(&sum)) return false;
    if (xcryptographic_bn_cmp(&sum.x, &order) >= 0)
        xcryptographic_bn_sub_raw(&sum.x, &sum.x, &order);
    return xcryptographic_bn_cmp(&sum.x, &r) == 0;
}

static bool xcryptographic_ecdsa_brainpool_p256r1_import_private_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPoint base, point;
    XCryptographic_Bn scalar;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P256R1_ON || !key || !result || keyLen != 32 ||
        !xcryptographic_brainpool_p256r1_scalar_valid((const uint8_t*)key)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->privateKey, key, 32);
    xcryptographic_brainpool_p256r1_point_base(&base);
    xcryptographic_bn_from_be(&scalar, result->privateKey);
    xcryptographic_brainpool_p256r1_scalar_mul(&point, &base, &scalar);
    if (!xcryptographic_brainpool_p256r1_to_affine(&point)) return false;
    xcryptographic_p256_serialize(&point, result->publicKey);
    result->publicKeyLen = 65;
    result->type = XCryptographic_KeyType_EcdsaBrainpoolP256r1Private;
    return true;
}

static bool xcryptographic_ecdsa_brainpool_p256r1_import_public_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPoint point;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P256R1_ON || !result ||
        !xcryptographic_brainpool_p256r1_parse((const uint8_t*)key, keyLen, &point)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->publicKey, key, keyLen);
    result->publicKeyLen = keyLen;
    result->type = XCryptographic_KeyType_EcdsaBrainpoolP256r1Public;
    return true;
}

static bool xcryptographic_ecdsa_brainpool_p256r1_sign_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    void* signature, size_t signatureCap, size_t* signatureLen, bool deterministic)
{
    XCryptographic_Bn order, d, z, k, kinv, r, s, rd, sum;
    XCryptographic_EcPoint base, point;
    uint8_t kBytes[32];
    if ((!deterministic && !XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P256R1_ON) ||
        (deterministic && !XCRYPTOGRAPHIC_ECDSA_DETERMINISTIC_BRAINPOOL_P256R1_ON) ||
        key.type != XCryptographic_KeyType_EcdsaBrainpoolP256r1Private || !hash ||
        !signature || !signatureLen || signatureCap < 64) return false;
    xcryptographic_brainpool_p256r1_order(&order);
    xcryptographic_bn_from_be(&d, key.privateKey);
    xcryptographic_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_brainpool_p256r1_point_base(&base);
    do {
        if (deterministic) {
            if (!xcryptographic_rfc6979_k(
                    kBytes, &key, hash, hashLen, &order,
                    XCryptographic_KeyType_EcdsaBrainpoolP256r1Private)) return false;
        } else if (!xcryptographic_brainpool_p256r1_random_scalar(kBytes)) {
            return false;
        }
        xcryptographic_bn_from_be(&k, kBytes);
        xcryptographic_brainpool_p256r1_scalar_mul(&point, &base, &k);
        if (!xcryptographic_brainpool_p256r1_to_affine(&point)) continue;
        xcryptographic_bn_to_be(&point.x, kBytes);
        xcryptographic_bn_from_be(&r, kBytes);
        if (xcryptographic_bn_cmp(&r, &order) >= 0)
            xcryptographic_bn_sub_raw(&r, &r, &order);
    } while (xcryptographic_bn_is_zero(&r));
    xcryptographic_bn_mul_mod(&rd, &r, &d, &order);
    xcryptographic_bn_add_mod(&sum, &z, &rd, &order);
    xcryptographic_bn_inverse_mod(&kinv, &k, &order);
    xcryptographic_bn_mul_mod(&s, &kinv, &sum, &order);
    if (xcryptographic_bn_is_zero(&s)) return false;
    xcryptographic_bn_to_be(&r, (uint8_t*)signature);
    xcryptographic_bn_to_be(&s, (uint8_t*)signature + 32);
    *signatureLen = 64;
    return true;
}

static bool xcryptographic_ecdsa_brainpool_p256r1_verify_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    const void* signature, size_t signatureLen)
{
    XCryptographic_Bn order, prime, r, s, z, w, u1, u2;
    XCryptographic_EcPoint base, publicPoint, p1, p2, sum;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P256R1_ON ||
        key.type != XCryptographic_KeyType_EcdsaBrainpoolP256r1Public || !hash ||
        !signature || signatureLen != 64) return false;
    xcryptographic_brainpool_p256r1_order(&order);
    xcryptographic_bn_from_be(&r, (const uint8_t*)signature);
    xcryptographic_bn_from_be(&s, (const uint8_t*)signature + 32);
    if (xcryptographic_bn_is_zero(&r) || xcryptographic_bn_is_zero(&s) ||
        xcryptographic_bn_cmp(&r, &order) >= 0 || xcryptographic_bn_cmp(&s, &order) >= 0)
        return false;
    if (!xcryptographic_brainpool_p256r1_parse(key.publicKey, key.publicKeyLen, &publicPoint))
        return false;
    xcryptographic_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_bn_inverse_mod(&w, &s, &order);
    xcryptographic_bn_mul_mod(&u1, &z, &w, &order);
    xcryptographic_bn_mul_mod(&u2, &r, &w, &order);
    xcryptographic_brainpool_p256r1_point_base(&base);
    xcryptographic_brainpool_p256r1_scalar_mul(&p1, &base, &u1);
    xcryptographic_brainpool_p256r1_scalar_mul(&p2, &publicPoint, &u2);
    if (!xcryptographic_brainpool_p256r1_to_affine(&p1) ||
        !xcryptographic_brainpool_p256r1_to_affine(&p2)) return false;
    xcryptographic_brainpool_p256r1_modulus(&prime);
    xcryptographic_brainpool_p256r1_add(&sum, &p1, &p2, &prime);
    if (sum.infinity || !xcryptographic_brainpool_p256r1_to_affine(&sum)) return false;
    if (xcryptographic_bn_cmp(&sum.x, &order) >= 0)
        xcryptographic_bn_sub_raw(&sum.x, &sum.x, &order);
    return xcryptographic_bn_cmp(&sum.x, &r) == 0;
}

static void xcryptographic_p384_scalar_from_hash(XCryptographic_Bn384* out,
                                                 const void* hash, size_t hashLen,
                                                 const XCryptographic_Bn384* order)
{
    uint8_t bytes[48] = { 0 };
    XCryptographic_Bn384 temp;
    if (hashLen > 48) hashLen = 48;
    if (hashLen != 0) memcpy(bytes + 48 - hashLen, hash, hashLen);
    xcryptographic_bn384_from_be(&temp, bytes);
    if (xcryptographic_bn384_cmp(&temp, order) >= 0)
        xcryptographic_bn384_sub_raw(&temp, &temp, order);
    *out = temp;
}

static bool xcryptographic_p384_rfc6979_k(uint8_t output[48],
                                          const XCryptographic_Key* key,
                                          const void* hash, size_t hashLen,
                                          const XCryptographic_Bn384* order)
{
    uint8_t v[48], k[48], x[48], h[48], buffer[48 + 1 + 48 + 48];
    XCryptographic_Bn384 candidate, hbn;
    uint32_t attempts;
    if (!output || !key || !hash || !order ||
        (key->type != XCryptographic_KeyType_EcdsaNistP384Private &&
         key->type != XCryptographic_KeyType_EcdsaBrainpoolP384r1Private)) return false;
    memset(v, 0x01, sizeof(v));
    memset(k, 0x00, sizeof(k));
    memcpy(x, key->privateKey, sizeof(x));
    memset(h, 0, sizeof(h));
    if (hashLen > sizeof(h)) hashLen = sizeof(h);
    memcpy(h + sizeof(h) - hashLen, hash, hashLen);
    xcryptographic_bn384_from_be(&hbn, h);
    if (xcryptographic_bn384_cmp(&hbn, order) >= 0)
        xcryptographic_bn384_sub_raw(&hbn, &hbn, order);
    xcryptographic_bn384_to_be(&hbn, h);
    memcpy(buffer, v, 48); buffer[48] = 0;
    memcpy(buffer + 49, x, 48); memcpy(buffer + 97, h, 48);
    if (!XCryptographicHash_hmacInto((char*)k, sizeof(k), (const char*)k, sizeof(k),
                                     (const char*)buffer, 145,
                                     XCryptographicHash_Sha384).m_data) return false;
    if (!XCryptographicHash_hmacInto((char*)v, sizeof(v), (const char*)k, sizeof(k),
                                     (const char*)v, sizeof(v),
                                     XCryptographicHash_Sha384).m_data) return false;
    memcpy(buffer, v, 48); buffer[48] = 1;
    memcpy(buffer + 49, x, 48); memcpy(buffer + 97, h, 48);
    if (!XCryptographicHash_hmacInto((char*)k, sizeof(k), (const char*)k, sizeof(k),
                                     (const char*)buffer, 145,
                                     XCryptographicHash_Sha384).m_data) return false;
    if (!XCryptographicHash_hmacInto((char*)v, sizeof(v), (const char*)k, sizeof(k),
                                     (const char*)v, sizeof(v),
                                     XCryptographicHash_Sha384).m_data) return false;
    for (attempts = 0; attempts < 1024; ++attempts) {
        if (!XCryptographicHash_hmacInto((char*)v, sizeof(v), (const char*)k, sizeof(k),
                                         (const char*)v, sizeof(v),
                                         XCryptographicHash_Sha384).m_data) return false;
        memcpy(output, v, 48);
        xcryptographic_bn384_from_be(&candidate, output);
        if (!xcryptographic_bn384_is_zero(&candidate) &&
            xcryptographic_bn384_cmp(&candidate, order) < 0) {
            memset(v, 0, sizeof(v)); memset(k, 0, sizeof(k));
            memset(x, 0, sizeof(x)); memset(h, 0, sizeof(h)); memset(buffer, 0, sizeof(buffer));
            return true;
        }
        memcpy(buffer, v, 48); buffer[48] = 0;
        if (!XCryptographicHash_hmacInto((char*)k, sizeof(k), (const char*)k, sizeof(k),
                                         (const char*)buffer, 49,
                                         XCryptographicHash_Sha384).m_data) return false;
        if (!XCryptographicHash_hmacInto((char*)v, sizeof(v), (const char*)k, sizeof(k),
                                         (const char*)v, sizeof(v),
                                         XCryptographicHash_Sha384).m_data) return false;
    }
    memset(v, 0, sizeof(v)); memset(k, 0, sizeof(k));
    memset(x, 0, sizeof(x)); memset(h, 0, sizeof(h)); memset(buffer, 0, sizeof(buffer));
    return false;
}

static bool xcryptographic_ecdsa_p384_import_private_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_Bn384 scalar;
    XCryptographic_EcPoint384 base, point;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP384_ON || !key || !result || keyLen != 48 ||
        !xcryptographic_p384_scalar_valid((const uint8_t*)key)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->privateKey, key, 48);
    xcryptographic_bn384_from_be(&scalar, result->privateKey);
    xcryptographic_p384_point_base(&base);
    xcryptographic_p384_scalar_mul(&point, &base, &scalar);
    if (!xcryptographic_p384_to_affine(&point)) return false;
    xcryptographic_p384_serialize(&point, result->publicKey);
    result->publicKeyLen = 97;
    result->type = XCryptographic_KeyType_EcdsaNistP384Private;
    return true;
}

static bool xcryptographic_ecdsa_p384_import_public_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPoint384 point;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP384_ON || !result ||
        !xcryptographic_p384_parse((const uint8_t*)key, keyLen, &point)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->publicKey, key, keyLen);
    result->publicKeyLen = keyLen;
    result->type = XCryptographic_KeyType_EcdsaNistP384Public;
    return true;
}

static bool xcryptographic_ecdsa_p384_sign_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    void* signature, size_t signatureCap, size_t* signatureLen, bool deterministic)
{
    XCryptographic_Bn384 order, d, z, k, kinv, r, s, rd, sum;
    XCryptographic_EcPoint384 base, point;
    uint8_t kBytes[48];
    if ((!deterministic && !XCRYPTOGRAPHIC_ECDSA_NISTP384_ON) ||
        (deterministic && !XCRYPTOGRAPHIC_ECDSA_DETERMINISTIC_NISTP384_ON) ||
        key.type != XCryptographic_KeyType_EcdsaNistP384Private || !hash ||
        !signature || !signatureLen || signatureCap < 96) return false;
    xcryptographic_p384_order(&order);
    xcryptographic_bn384_from_be(&d, key.privateKey);
    xcryptographic_p384_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_p384_point_base(&base);
    do {
        if (deterministic) {
            if (!xcryptographic_p384_rfc6979_k(kBytes, &key, hash, hashLen, &order)) return false;
        } else if (!xcryptographic_p384_random_scalar(kBytes)) return false;
        xcryptographic_bn384_from_be(&k, kBytes);
        xcryptographic_p384_scalar_mul(&point, &base, &k);
        if (!xcryptographic_p384_to_affine(&point)) continue;
        xcryptographic_bn384_to_be(&point.x, kBytes);
        xcryptographic_bn384_from_be(&r, kBytes);
        if (xcryptographic_bn384_cmp(&r, &order) >= 0)
            xcryptographic_bn384_sub_raw(&r, &r, &order);
    } while (xcryptographic_bn384_is_zero(&r));
    xcryptographic_bn384_mul_mod(&rd, &r, &d, &order);
    xcryptographic_bn384_add_mod(&sum, &z, &rd, &order);
    xcryptographic_bn384_inverse_mod(&kinv, &k, &order);
    xcryptographic_bn384_mul_mod(&s, &kinv, &sum, &order);
    if (xcryptographic_bn384_is_zero(&s)) return false;
    xcryptographic_bn384_to_be(&r, (uint8_t*)signature);
    xcryptographic_bn384_to_be(&s, (uint8_t*)signature + 48);
    *signatureLen = 96;
    return true;
}

static bool xcryptographic_ecdsa_p384_verify_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    const void* signature, size_t signatureLen)
{
    XCryptographic_Bn384 order, z, r, s, w, u1, u2, prime;
    XCryptographic_EcPoint384 base, publicPoint, p1, p2, sum;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP384_ON ||
        key.type != XCryptographic_KeyType_EcdsaNistP384Public || !hash ||
        !signature || signatureLen != 96) return false;
    xcryptographic_p384_order(&order);
    xcryptographic_bn384_from_be(&r, (const uint8_t*)signature);
    xcryptographic_bn384_from_be(&s, (const uint8_t*)signature + 48);
    if (xcryptographic_bn384_is_zero(&r) || xcryptographic_bn384_is_zero(&s) ||
        xcryptographic_bn384_cmp(&r, &order) >= 0 ||
        xcryptographic_bn384_cmp(&s, &order) >= 0) return false;
    if (!xcryptographic_p384_parse(key.publicKey, key.publicKeyLen, &publicPoint)) return false;
    xcryptographic_p384_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_bn384_inverse_mod(&w, &s, &order);
    xcryptographic_bn384_mul_mod(&u1, &z, &w, &order);
    xcryptographic_bn384_mul_mod(&u2, &r, &w, &order);
    xcryptographic_p384_point_base(&base);
    xcryptographic_p384_scalar_mul(&p1, &base, &u1);
    xcryptographic_p384_scalar_mul(&p2, &publicPoint, &u2);
    if (p1.infinity || p2.infinity) return false;
    xcryptographic_p384_modulus(&prime);
    xcryptographic_p384_add(&sum, &p1, &p2, &prime);
    if (sum.infinity || !xcryptographic_p384_to_affine(&sum)) return false;
    if (xcryptographic_bn384_cmp(&sum.x, &order) >= 0)
        xcryptographic_bn384_sub_raw(&sum.x, &sum.x, &order);
    return xcryptographic_bn384_cmp(&sum.x, &r) == 0;
}

static bool xcryptographic_ecdsa_brainpool_p384r1_import_private_key(const void* key,size_t keyLen,XCryptographic_Key* result)
{
    XCryptographic_Bn384 scalar; XCryptographic_EcPointBrainpool384 base, point;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P384R1_ON || !key || !result || keyLen!=48 || !xcryptographic_brainpool_p384r1_scalar_valid((const uint8_t*)key)) return false;
    memset(result,0,sizeof(*result)); memcpy(result->privateKey,key,48);
    xcryptographic_bn384_from_be(&scalar,result->privateKey); xcryptographic_brainpool_p384r1_point_base(&base);
    xcryptographic_brainpool_p384r1_scalar_mul(&point,&base,&scalar); if (!xcryptographic_brainpool_p384r1_to_affine(&point)) return false;
    xcryptographic_brainpool_p384r1_serialize(&point,result->publicKey); result->publicKeyLen=97; result->type=XCryptographic_KeyType_EcdsaBrainpoolP384r1Private; return true;
}
static bool xcryptographic_ecdsa_brainpool_p384r1_import_public_key(const void* key,size_t keyLen,XCryptographic_Key* result)
{
    XCryptographic_EcPointBrainpool384 point;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P384R1_ON || !result || !xcryptographic_brainpool_p384r1_parse((const uint8_t*)key,keyLen,&point)) return false;
    memset(result,0,sizeof(*result)); memcpy(result->publicKey,key,keyLen); result->publicKeyLen=keyLen; result->type=XCryptographic_KeyType_EcdsaBrainpoolP384r1Public; return true;
}
static bool xcryptographic_ecdsa_brainpool_p384r1_sign_hash(XCryptographic_Key key,const void* hash,size_t hashLen,void* signature,size_t signatureCap,size_t* signatureLen,bool deterministic)
{
    XCryptographic_Bn384 order,d,z,k,kinv,r,s,rd,sum; XCryptographic_EcPointBrainpool384 base,point; uint8_t kBytes[48];
    if ((!deterministic && !XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P384R1_ON) ||
        (deterministic && !XCRYPTOGRAPHIC_ECDSA_DETERMINISTIC_BRAINPOOL_P384R1_ON) ||
        key.type!=XCryptographic_KeyType_EcdsaBrainpoolP384r1Private||!hash||!signature||!signatureLen||signatureCap<96) return false;
    xcryptographic_brainpool_p384r1_order(&order); xcryptographic_bn384_from_be(&d,key.privateKey); xcryptographic_p384_scalar_from_hash(&z,hash,hashLen,&order); xcryptographic_brainpool_p384r1_point_base(&base);
    do { if (deterministic) { if (!xcryptographic_p384_rfc6979_k(kBytes,&key,hash,hashLen,&order)) return false; } else if (!xcryptographic_brainpool_p384r1_random_scalar(kBytes)) return false; xcryptographic_bn384_from_be(&k,kBytes); xcryptographic_brainpool_p384r1_scalar_mul(&point,&base,&k); if (!xcryptographic_brainpool_p384r1_to_affine(&point)) continue; xcryptographic_bn384_to_be(&point.x,kBytes); xcryptographic_bn384_from_be(&r,kBytes); if (xcryptographic_bn384_cmp(&r,&order)>=0) xcryptographic_bn384_sub_raw(&r,&r,&order); } while (xcryptographic_bn384_is_zero(&r));
    xcryptographic_bn384_mul_mod(&rd,&r,&d,&order); xcryptographic_bn384_add_mod(&sum,&z,&rd,&order); xcryptographic_bn384_inverse_mod(&kinv,&k,&order); xcryptographic_bn384_mul_mod(&s,&kinv,&sum,&order); if (xcryptographic_bn384_is_zero(&s)) return false;
    xcryptographic_bn384_to_be(&r,(uint8_t*)signature); xcryptographic_bn384_to_be(&s,(uint8_t*)signature+48); *signatureLen=96; return true;
}
static bool xcryptographic_ecdsa_brainpool_p384r1_verify_hash(XCryptographic_Key key,const void* hash,size_t hashLen,const void* signature,size_t signatureLen)
{
    XCryptographic_Bn384 order,z,r,s,w,u1,u2,prime; XCryptographic_EcPointBrainpool384 base,pub,p1,p2,sum;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P384R1_ON||key.type!=XCryptographic_KeyType_EcdsaBrainpoolP384r1Public||!hash||!signature||signatureLen!=96) return false;
    xcryptographic_brainpool_p384r1_order(&order); xcryptographic_bn384_from_be(&r,signature); xcryptographic_bn384_from_be(&s,(const uint8_t*)signature+48);
    if (xcryptographic_bn384_is_zero(&r)||xcryptographic_bn384_is_zero(&s)||xcryptographic_bn384_cmp(&r,&order)>=0||xcryptographic_bn384_cmp(&s,&order)>=0||!xcryptographic_brainpool_p384r1_parse(key.publicKey,key.publicKeyLen,&pub)) return false;
    xcryptographic_p384_scalar_from_hash(&z,hash,hashLen,&order); xcryptographic_bn384_inverse_mod(&w,&s,&order); xcryptographic_bn384_mul_mod(&u1,&z,&w,&order); xcryptographic_bn384_mul_mod(&u2,&r,&w,&order); xcryptographic_brainpool_p384r1_point_base(&base);
    xcryptographic_brainpool_p384r1_scalar_mul(&p1,&base,&u1); xcryptographic_brainpool_p384r1_scalar_mul(&p2,&pub,&u2); if (p1.infinity||p2.infinity) return false; xcryptographic_brainpool_p384r1_modulus(&prime); xcryptographic_brainpool_p384r1_add(&sum,&p1,&p2,&prime); if (sum.infinity||!xcryptographic_brainpool_p384r1_to_affine(&sum)) return false;
    if (xcryptographic_bn384_cmp(&sum.x,&order)>=0) xcryptographic_bn384_sub_raw(&sum.x,&sum.x,&order); return xcryptographic_bn384_cmp(&sum.x,&r)==0;
}

static void xcryptographic_brainpool_p512r1_scalar_from_hash(
    XCryptographic_Bn521* out, const void* hash, size_t hashLen,
    const XCryptographic_Bn521* order)
{
    uint8_t bytes[64] = { 0 };
    XCryptographic_Bn521 temp;
    if (hashLen > sizeof(bytes)) hashLen = sizeof(bytes);
    if (hashLen != 0) memcpy(bytes + sizeof(bytes) - hashLen, hash, hashLen);
    xcryptographic_bn521_from_be64(&temp, bytes);
    if (xcryptographic_bn521_cmp(&temp, order) >= 0)
        xcryptographic_bn521_sub_raw(&temp, &temp, order);
    *out = temp;
}

static bool xcryptographic_brainpool_p512r1_rfc6979_k(
    uint8_t output[64], const XCryptographic_Key* key,
    const void* hash, size_t hashLen, const XCryptographic_Bn521* order)
{
    uint8_t v[64], k[64], x[64], h[64], buffer[64 + 1 + 64 + 64];
    XCryptographic_Bn521 candidate, hbn;
    uint32_t attempts;
    if (!output || !key || !hash || !order ||
        key->type != XCryptographic_KeyType_EcdsaBrainpoolP512r1Private) return false;
    memset(v, 0x01, sizeof(v));
    memset(k, 0x00, sizeof(k));
    memcpy(x, key->privateKey, sizeof(x));
    memset(h, 0, sizeof(h));
    if (hashLen > sizeof(h)) hashLen = sizeof(h);
    memcpy(h + sizeof(h) - hashLen, hash, hashLen);
    xcryptographic_bn521_from_be64(&hbn, h);
    if (xcryptographic_bn521_cmp(&hbn, order) >= 0)
        xcryptographic_bn521_sub_raw(&hbn, &hbn, order);
    xcryptographic_bn521_to_be64(&hbn, h);
    memcpy(buffer, v, 64); buffer[64] = 0;
    memcpy(buffer + 65, x, 64); memcpy(buffer + 129, h, 64);
    if (!XCryptographicHash_hmacInto((char*)k, sizeof(k), (const char*)k, sizeof(k),
                                     (const char*)buffer, sizeof(buffer),
                                     XCryptographicHash_Sha512).m_data) return false;
    if (!XCryptographicHash_hmacInto((char*)v, sizeof(v), (const char*)k, sizeof(k),
                                     (const char*)v, sizeof(v),
                                     XCryptographicHash_Sha512).m_data) return false;
    memcpy(buffer, v, 64); buffer[64] = 1;
    memcpy(buffer + 65, x, 64); memcpy(buffer + 129, h, 64);
    if (!XCryptographicHash_hmacInto((char*)k, sizeof(k), (const char*)k, sizeof(k),
                                     (const char*)buffer, sizeof(buffer),
                                     XCryptographicHash_Sha512).m_data) return false;
    if (!XCryptographicHash_hmacInto((char*)v, sizeof(v), (const char*)k, sizeof(k),
                                     (const char*)v, sizeof(v),
                                     XCryptographicHash_Sha512).m_data) return false;
    for (attempts = 0; attempts < 1024; ++attempts) {
        if (!XCryptographicHash_hmacInto((char*)v, sizeof(v), (const char*)k, sizeof(k),
                                         (const char*)v, sizeof(v),
                                         XCryptographicHash_Sha512).m_data) return false;
        memcpy(output, v, sizeof(v));
        xcryptographic_bn521_from_be64(&candidate, output);
        if (!xcryptographic_bn521_is_zero(&candidate) &&
            xcryptographic_bn521_cmp(&candidate, order) < 0) {
            memset(v, 0, sizeof(v)); memset(k, 0, sizeof(k));
            memset(x, 0, sizeof(x)); memset(h, 0, sizeof(h)); memset(buffer, 0, sizeof(buffer));
            return true;
        }
        memcpy(buffer, v, 64); buffer[64] = 0;
        if (!XCryptographicHash_hmacInto((char*)k, sizeof(k), (const char*)k, sizeof(k),
                                         (const char*)buffer, 65,
                                         XCryptographicHash_Sha512).m_data) return false;
        if (!XCryptographicHash_hmacInto((char*)v, sizeof(v), (const char*)k, sizeof(k),
                                         (const char*)v, sizeof(v),
                                         XCryptographicHash_Sha512).m_data) return false;
    }
    memset(v, 0, sizeof(v)); memset(k, 0, sizeof(k));
    memset(x, 0, sizeof(x)); memset(h, 0, sizeof(h)); memset(buffer, 0, sizeof(buffer));
    return false;
}

static bool xcryptographic_ecdsa_brainpool_p512r1_import_private_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_Bn521 scalar;
    XCryptographic_EcPointBrainpool512 base, point;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P512R1_ON || !key || !result || keyLen != 64 ||
        !xcryptographic_brainpool_p512r1_scalar_valid((const uint8_t*)key)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->privateKey, key, 64);
    xcryptographic_bn521_from_be64(&scalar, result->privateKey);
    xcryptographic_brainpool_p512r1_point_base(&base);
    xcryptographic_brainpool_p512r1_scalar_mul(&point, &base, &scalar);
    if (!xcryptographic_brainpool_p512r1_to_affine(&point)) return false;
    xcryptographic_brainpool_p512r1_serialize(&point, result->publicKey);
    result->publicKeyLen = 129;
    result->type = XCryptographic_KeyType_EcdsaBrainpoolP512r1Private;
    return true;
}

static bool xcryptographic_ecdsa_brainpool_p512r1_import_public_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPointBrainpool512 point;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P512R1_ON || !result ||
        !xcryptographic_brainpool_p512r1_parse((const uint8_t*)key, keyLen, &point)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->publicKey, key, keyLen);
    result->publicKeyLen = keyLen;
    result->type = XCryptographic_KeyType_EcdsaBrainpoolP512r1Public;
    return true;
}

static bool xcryptographic_ecdsa_brainpool_p512r1_sign_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    void* signature, size_t signatureCap, size_t* signatureLen, bool deterministic)
{
    XCryptographic_Bn521 order, d, z, k, kinv, r, s, rd, sum;
    XCryptographic_EcPointBrainpool512 base, point;
    uint8_t kBytes[64];
    if ((!deterministic && !XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P512R1_ON) ||
        (deterministic && !XCRYPTOGRAPHIC_ECDSA_DETERMINISTIC_BRAINPOOL_P512R1_ON) ||
        key.type != XCryptographic_KeyType_EcdsaBrainpoolP512r1Private || !hash ||
        !signature || !signatureLen || signatureCap < 128) return false;
    xcryptographic_brainpool_p512r1_order(&order);
    xcryptographic_bn521_from_be64(&d, key.privateKey);
    xcryptographic_brainpool_p512r1_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_brainpool_p512r1_point_base(&base);
    do {
        if (deterministic) {
            if (!xcryptographic_brainpool_p512r1_rfc6979_k(kBytes, &key, hash, hashLen, &order)) return false;
        } else if (!xcryptographic_brainpool_p512r1_random_scalar(kBytes)) {
            return false;
        }
        xcryptographic_bn521_from_be64(&k, kBytes);
        xcryptographic_brainpool_p512r1_scalar_mul(&point, &base, &k);
        if (!xcryptographic_brainpool_p512r1_to_affine(&point)) continue;
        xcryptographic_bn521_to_be64(&point.x, kBytes);
        xcryptographic_bn521_from_be64(&r, kBytes);
        if (xcryptographic_bn521_cmp(&r, &order) >= 0)
            xcryptographic_bn521_sub_raw(&r, &r, &order);
    } while (xcryptographic_bn521_is_zero(&r));
    xcryptographic_bn521_mul_mod(&rd, &r, &d, &order);
    xcryptographic_bn521_add_mod(&sum, &z, &rd, &order);
    xcryptographic_bn521_inverse_mod(&kinv, &k, &order);
    xcryptographic_bn521_mul_mod(&s, &kinv, &sum, &order);
    if (xcryptographic_bn521_is_zero(&s)) return false;
    xcryptographic_bn521_to_be64(&r, (uint8_t*)signature);
    xcryptographic_bn521_to_be64(&s, (uint8_t*)signature + 64);
    *signatureLen = 128;
    return true;
}

static bool xcryptographic_ecdsa_brainpool_p512r1_verify_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    const void* signature, size_t signatureLen)
{
    XCryptographic_Bn521 order, z, r, s, w, u1, u2, prime;
    XCryptographic_EcPointBrainpool512 base, publicPoint, p1, p2, sum;
    if (!XCRYPTOGRAPHIC_ECDSA_BRAINPOOL_P512R1_ON ||
        key.type != XCryptographic_KeyType_EcdsaBrainpoolP512r1Public || !hash ||
        !signature || signatureLen != 128) return false;
    xcryptographic_brainpool_p512r1_order(&order);
    xcryptographic_bn521_from_be64(&r, (const uint8_t*)signature);
    xcryptographic_bn521_from_be64(&s, (const uint8_t*)signature + 64);
    if (xcryptographic_bn521_is_zero(&r) || xcryptographic_bn521_is_zero(&s) ||
        xcryptographic_bn521_cmp(&r, &order) >= 0 ||
        xcryptographic_bn521_cmp(&s, &order) >= 0 ||
        !xcryptographic_brainpool_p512r1_parse(key.publicKey, key.publicKeyLen, &publicPoint)) return false;
    xcryptographic_brainpool_p512r1_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_bn521_inverse_mod(&w, &s, &order);
    xcryptographic_bn521_mul_mod(&u1, &z, &w, &order);
    xcryptographic_bn521_mul_mod(&u2, &r, &w, &order);
    xcryptographic_brainpool_p512r1_point_base(&base);
    xcryptographic_brainpool_p512r1_scalar_mul(&p1, &base, &u1);
    xcryptographic_brainpool_p512r1_scalar_mul(&p2, &publicPoint, &u2);
    if (p1.infinity || p2.infinity) return false;
    xcryptographic_brainpool_p512r1_modulus(&prime);
    xcryptographic_brainpool_p512r1_add(&sum, &p1, &p2, &prime);
    if (sum.infinity || !xcryptographic_brainpool_p512r1_to_affine(&sum)) return false;
    if (xcryptographic_bn521_cmp(&sum.x, &order) >= 0)
        xcryptographic_bn521_sub_raw(&sum.x, &sum.x, &order);
    return xcryptographic_bn521_cmp(&sum.x, &r) == 0;
}

static void xcryptographic_p521_scalar_from_hash(XCryptographic_Bn521* out,
                                                 const void* hash, size_t hashLen,
                                                 const XCryptographic_Bn521* order)
{
    uint8_t bytes[66] = { 0 };
    XCryptographic_Bn521 temp;
    if (hashLen > sizeof(bytes)) hashLen = sizeof(bytes);
    if (hashLen != 0) memcpy(bytes + sizeof(bytes) - hashLen, hash, hashLen);
    bytes[0] &= 1u;
    xcryptographic_bn521_from_be(&temp, bytes);
    if (xcryptographic_bn521_cmp(&temp, order) >= 0)
        xcryptographic_bn521_sub_raw(&temp, &temp, order);
    *out = temp;
}

static bool xcryptographic_p521_rfc6979_k(uint8_t output[66],
                                          const XCryptographic_Key* key,
                                          const void* hash, size_t hashLen,
                                          const XCryptographic_Bn521* order)
{
    uint8_t v[64], k[64], x[66], h[66], t[132], buffer[64 + 1 + 66 + 66];
    uint8_t digest[64];
    XCryptographic_Bn521 candidate, hbn;
    uint32_t attempts;
    if (!output || !key || !hash || !order ||
        key->type != XCryptographic_KeyType_EcdsaNistP521Private) return false;
    memset(v, 0x01, sizeof(v));
    memset(k, 0x00, sizeof(k));
    memcpy(x, key->privateKey, sizeof(x));
    memset(h, 0, sizeof(h));
    if (hashLen > sizeof(h)) hashLen = sizeof(h);
    memcpy(h + sizeof(h) - hashLen, hash, hashLen);
    h[0] &= 1u;
    xcryptographic_bn521_from_be(&hbn, h);
    if (xcryptographic_bn521_cmp(&hbn, order) >= 0)
        xcryptographic_bn521_sub_raw(&hbn, &hbn, order);
    xcryptographic_bn521_to_be(&hbn, h);
    memcpy(buffer, v, sizeof(v)); buffer[64] = 0;
    memcpy(buffer + 65, x, 66); memcpy(buffer + 131, h, 66);
    if (!XCryptographicHash_hmacInto((char*)digest, sizeof(digest), (const char*)k, sizeof(k),
                                     (const char*)buffer, 197,
                                     XCryptographicHash_Sha512).m_data) return false;
    memcpy(k, digest, sizeof(k));
    if (!XCryptographicHash_hmacInto((char*)digest, sizeof(digest), (const char*)k, sizeof(k),
                                     (const char*)v, sizeof(v),
                                     XCryptographicHash_Sha512).m_data) return false;
    memcpy(v, digest, sizeof(v));
    memcpy(buffer, v, sizeof(v)); buffer[64] = 1;
    memcpy(buffer + 65, x, 66); memcpy(buffer + 131, h, 66);
    if (!XCryptographicHash_hmacInto((char*)digest, sizeof(digest), (const char*)k, sizeof(k),
                                     (const char*)buffer, 197,
                                     XCryptographicHash_Sha512).m_data) return false;
    memcpy(k, digest, sizeof(k));
    if (!XCryptographicHash_hmacInto((char*)digest, sizeof(digest), (const char*)k, sizeof(k),
                                     (const char*)v, sizeof(v),
                                     XCryptographicHash_Sha512).m_data) return false;
    memcpy(v, digest, sizeof(v));
    for (attempts = 0; attempts < 1024; ++attempts) {
        if (!XCryptographicHash_hmacInto((char*)digest, sizeof(digest), (const char*)k, sizeof(k),
                                         (const char*)v, sizeof(v),
                                         XCryptographicHash_Sha512).m_data) return false;
        memcpy(v, digest, sizeof(v));
        memcpy(t, v, sizeof(v));
        if (!XCryptographicHash_hmacInto((char*)digest, sizeof(digest), (const char*)k, sizeof(k),
                                         (const char*)v, sizeof(v),
                                         XCryptographicHash_Sha512).m_data) return false;
        memcpy(v, digest, sizeof(v));
        memcpy(t + sizeof(v), v, sizeof(v));
        memcpy(output, t, 66);
        output[0] &= 1u;
        xcryptographic_bn521_from_be(&candidate, output);
        if (!xcryptographic_bn521_is_zero(&candidate) &&
            xcryptographic_bn521_cmp(&candidate, order) < 0) {
            memset(v, 0, sizeof(v)); memset(k, 0, sizeof(k));
            memset(x, 0, sizeof(x)); memset(h, 0, sizeof(h));
            memset(t, 0, sizeof(t)); memset(buffer, 0, sizeof(buffer)); memset(digest, 0, sizeof(digest));
            return true;
        }
        memcpy(buffer, v, sizeof(v)); buffer[64] = 0;
        if (!XCryptographicHash_hmacInto((char*)digest, sizeof(digest), (const char*)k, sizeof(k),
                                         (const char*)buffer, 65,
                                         XCryptographicHash_Sha512).m_data) return false;
        memcpy(k, digest, sizeof(k));
        if (!XCryptographicHash_hmacInto((char*)digest, sizeof(digest), (const char*)k, sizeof(k),
                                         (const char*)v, sizeof(v),
                                         XCryptographicHash_Sha512).m_data) return false;
        memcpy(v, digest, sizeof(v));
    }
    memset(v, 0, sizeof(v)); memset(k, 0, sizeof(k));
    memset(x, 0, sizeof(x)); memset(h, 0, sizeof(h));
    memset(t, 0, sizeof(t)); memset(buffer, 0, sizeof(buffer)); memset(digest, 0, sizeof(digest));
    return false;
}

static bool xcryptographic_ecdsa_p521_import_private_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_Bn521 scalar;
    XCryptographic_EcPoint521 base, point;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP521_ON || !key || !result || keyLen != 66 ||
        !xcryptographic_p521_scalar_valid((const uint8_t*)key)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->privateKey, key, 66);
    xcryptographic_bn521_from_be(&scalar, result->privateKey);
    xcryptographic_p521_point_base(&base);
    xcryptographic_p521_scalar_mul(&point, &base, &scalar);
    if (!xcryptographic_p521_to_affine(&point)) return false;
    xcryptographic_p521_serialize(&point, result->publicKey);
    result->publicKeyLen = 133;
    result->type = XCryptographic_KeyType_EcdsaNistP521Private;
    return true;
}

static bool xcryptographic_ecdsa_p521_import_public_key(
    const void* key, size_t keyLen, XCryptographic_Key* result)
{
    XCryptographic_EcPoint521 point;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP521_ON || !result ||
        !xcryptographic_p521_parse((const uint8_t*)key, keyLen, &point)) return false;
    memset(result, 0, sizeof(*result));
    memcpy(result->publicKey, key, keyLen);
    result->publicKeyLen = keyLen;
    result->type = XCryptographic_KeyType_EcdsaNistP521Public;
    return true;
}

static bool xcryptographic_ecdsa_p521_sign_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    void* signature, size_t signatureCap, size_t* signatureLen, bool deterministic)
{
    XCryptographic_Bn521 order, d, z, k, kinv, r, s, rd, sum;
    XCryptographic_EcPoint521 base, point;
    uint8_t kBytes[66];
    if ((!deterministic && !XCRYPTOGRAPHIC_ECDSA_NISTP521_ON) ||
        (deterministic && !XCRYPTOGRAPHIC_ECDSA_DETERMINISTIC_NISTP521_ON) ||
        key.type != XCryptographic_KeyType_EcdsaNistP521Private || !hash ||
        !signature || !signatureLen || signatureCap < 132) return false;
    xcryptographic_p521_order(&order);
    xcryptographic_bn521_from_be(&d, key.privateKey);
    xcryptographic_p521_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_p521_point_base(&base);
    do {
        if (deterministic) {
            if (!xcryptographic_p521_rfc6979_k(kBytes, &key, hash, hashLen, &order)) return false;
        } else if (!xcryptographic_p521_random_scalar(kBytes)) return false;
        xcryptographic_bn521_from_be(&k, kBytes);
        xcryptographic_p521_scalar_mul(&point, &base, &k);
        if (!xcryptographic_p521_to_affine(&point)) continue;
        xcryptographic_bn521_to_be(&point.x, kBytes);
        xcryptographic_bn521_from_be(&r, kBytes);
        if (xcryptographic_bn521_cmp(&r, &order) >= 0)
            xcryptographic_bn521_sub_raw(&r, &r, &order);
    } while (xcryptographic_bn521_is_zero(&r));
    xcryptographic_bn521_mul_mod(&rd, &r, &d, &order);
    xcryptographic_bn521_add_mod(&sum, &z, &rd, &order);
    xcryptographic_bn521_inverse_mod(&kinv, &k, &order);
    xcryptographic_bn521_mul_mod(&s, &kinv, &sum, &order);
    if (xcryptographic_bn521_is_zero(&s)) return false;
    xcryptographic_bn521_to_be(&r, (uint8_t*)signature);
    xcryptographic_bn521_to_be(&s, (uint8_t*)signature + 66);
    *signatureLen = 132;
    return true;
}

static bool xcryptographic_ecdsa_p521_verify_hash(
    XCryptographic_Key key, const void* hash, size_t hashLen,
    const void* signature, size_t signatureLen)
{
    XCryptographic_Bn521 order, z, r, s, w, u1, u2, prime;
    XCryptographic_EcPoint521 base, publicPoint, p1, p2, sum;
    if (!XCRYPTOGRAPHIC_ECDSA_NISTP521_ON ||
        key.type != XCryptographic_KeyType_EcdsaNistP521Public || !hash ||
        !signature || signatureLen != 132) return false;
    xcryptographic_p521_order(&order);
    xcryptographic_bn521_from_be(&r, (const uint8_t*)signature);
    xcryptographic_bn521_from_be(&s, (const uint8_t*)signature + 66);
    if (xcryptographic_bn521_is_zero(&r) || xcryptographic_bn521_is_zero(&s) ||
        xcryptographic_bn521_cmp(&r, &order) >= 0 ||
        xcryptographic_bn521_cmp(&s, &order) >= 0) return false;
    if (!xcryptographic_p521_parse(key.publicKey, key.publicKeyLen, &publicPoint)) return false;
    xcryptographic_p521_scalar_from_hash(&z, hash, hashLen, &order);
    xcryptographic_bn521_inverse_mod(&w, &s, &order);
    xcryptographic_bn521_mul_mod(&u1, &z, &w, &order);
    xcryptographic_bn521_mul_mod(&u2, &r, &w, &order);
    xcryptographic_p521_point_base(&base);
    xcryptographic_p521_scalar_mul(&p1, &base, &u1);
    xcryptographic_p521_scalar_mul(&p2, &publicPoint, &u2);
    if (p1.infinity || p2.infinity) return false;
    xcryptographic_p521_modulus(&prime);
    xcryptographic_p521_add(&sum, &p1, &p2, &prime);
    if (sum.infinity || !xcryptographic_p521_to_affine(&sum)) return false;
    if (xcryptographic_bn521_cmp(&sum.x, &order) >= 0)
        xcryptographic_bn521_sub_raw(&sum.x, &sum.x, &order);
    return xcryptographic_bn521_cmp(&sum.x, &r) == 0;
}

void XCryptographic_destroyKey(XCryptographic_Key* key)
{
    if (key) memset(key, 0, sizeof(*key));
}

/* RFC 7919 有限域 Diffie-Hellman 素数（大端字节序）。 */
static const uint8_t xcryptographic_ffdhe2048_p[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAD, 0xF8, 0x54, 0x58,
    0xA2, 0xBB, 0x4A, 0x9A, 0xAF, 0xDC, 0x56, 0x20, 0x27, 0x3D, 0x3C, 0xF1,
    0xD8, 0xB9, 0xC5, 0x83, 0xCE, 0x2D, 0x36, 0x95, 0xA9, 0xE1, 0x36, 0x41,
    0x14, 0x64, 0x33, 0xFB, 0xCC, 0x93, 0x9D, 0xCE, 0x24, 0x9B, 0x3E, 0xF9,
    0x7D, 0x2F, 0xE3, 0x63, 0x63, 0x0C, 0x75, 0xD8, 0xF6, 0x81, 0xB2, 0x02,
    0xAE, 0xC4, 0x61, 0x7A, 0xD3, 0xDF, 0x1E, 0xD5, 0xD5, 0xFD, 0x65, 0x61,
    0x24, 0x33, 0xF5, 0x1F, 0x5F, 0x06, 0x6E, 0xD0, 0x85, 0x63, 0x65, 0x55,
    0x3D, 0xED, 0x1A, 0xF3, 0xB5, 0x57, 0x13, 0x5E, 0x7F, 0x57, 0xC9, 0x35,
    0x98, 0x4F, 0x0C, 0x70, 0xE0, 0xE6, 0x8B, 0x77, 0xE2, 0xA6, 0x89, 0xDA,
    0xF3, 0xEF, 0xE8, 0x72, 0x1D, 0xF1, 0x58, 0xA1, 0x36, 0xAD, 0xE7, 0x35,
    0x30, 0xAC, 0xCA, 0x4F, 0x48, 0x3A, 0x79, 0x7A, 0xBC, 0x0A, 0xB1, 0x82,
    0xB3, 0x24, 0xFB, 0x61, 0xD1, 0x08, 0xA9, 0x4B, 0xB2, 0xC8, 0xE3, 0xFB,
    0xB9, 0x6A, 0xDA, 0xB7, 0x60, 0xD7, 0xF4, 0x68, 0x1D, 0x4F, 0x42, 0xA3,
    0xDE, 0x39, 0x4D, 0xF4, 0xAE, 0x56, 0xED, 0xE7, 0x63, 0x72, 0xBB, 0x19,
    0x0B, 0x07, 0xA7, 0xC8, 0xEE, 0x0A, 0x6D, 0x70, 0x9E, 0x02, 0xFC, 0xE1,
    0xCD, 0xF7, 0xE2, 0xEC, 0xC0, 0x34, 0x04, 0xCD, 0x28, 0x34, 0x2F, 0x61,
    0x91, 0x72, 0xFE, 0x9C, 0xE9, 0x85, 0x83, 0xFF, 0x8E, 0x4F, 0x12, 0x32,
    0xEE, 0xF2, 0x81, 0x83, 0xC3, 0xFE, 0x3B, 0x1B, 0x4C, 0x6F, 0xAD, 0x73,
    0x3B, 0xB5, 0xFC, 0xBC, 0x2E, 0xC2, 0x20, 0x05, 0xC5, 0x8E, 0xF1, 0x83,
    0x7D, 0x16, 0x83, 0xB2, 0xC6, 0xF3, 0x4A, 0x26, 0xC1, 0xB2, 0xEF, 0xFA,
    0x88, 0x6B, 0x42, 0x38, 0x61, 0x28, 0x5C, 0x97, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t xcryptographic_ffdhe3072_p[384] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAD, 0xF8, 0x54, 0x58,
    0xA2, 0xBB, 0x4A, 0x9A, 0xAF, 0xDC, 0x56, 0x20, 0x27, 0x3D, 0x3C, 0xF1,
    0xD8, 0xB9, 0xC5, 0x83, 0xCE, 0x2D, 0x36, 0x95, 0xA9, 0xE1, 0x36, 0x41,
    0x14, 0x64, 0x33, 0xFB, 0xCC, 0x93, 0x9D, 0xCE, 0x24, 0x9B, 0x3E, 0xF9,
    0x7D, 0x2F, 0xE3, 0x63, 0x63, 0x0C, 0x75, 0xD8, 0xF6, 0x81, 0xB2, 0x02,
    0xAE, 0xC4, 0x61, 0x7A, 0xD3, 0xDF, 0x1E, 0xD5, 0xD5, 0xFD, 0x65, 0x61,
    0x24, 0x33, 0xF5, 0x1F, 0x5F, 0x06, 0x6E, 0xD0, 0x85, 0x63, 0x65, 0x55,
    0x3D, 0xED, 0x1A, 0xF3, 0xB5, 0x57, 0x13, 0x5E, 0x7F, 0x57, 0xC9, 0x35,
    0x98, 0x4F, 0x0C, 0x70, 0xE0, 0xE6, 0x8B, 0x77, 0xE2, 0xA6, 0x89, 0xDA,
    0xF3, 0xEF, 0xE8, 0x72, 0x1D, 0xF1, 0x58, 0xA1, 0x36, 0xAD, 0xE7, 0x35,
    0x30, 0xAC, 0xCA, 0x4F, 0x48, 0x3A, 0x79, 0x7A, 0xBC, 0x0A, 0xB1, 0x82,
    0xB3, 0x24, 0xFB, 0x61, 0xD1, 0x08, 0xA9, 0x4B, 0xB2, 0xC8, 0xE3, 0xFB,
    0xB9, 0x6A, 0xDA, 0xB7, 0x60, 0xD7, 0xF4, 0x68, 0x1D, 0x4F, 0x42, 0xA3,
    0xDE, 0x39, 0x4D, 0xF4, 0xAE, 0x56, 0xED, 0xE7, 0x63, 0x72, 0xBB, 0x19,
    0x0B, 0x07, 0xA7, 0xC8, 0xEE, 0x0A, 0x6D, 0x70, 0x9E, 0x02, 0xFC, 0xE1,
    0xCD, 0xF7, 0xE2, 0xEC, 0xC0, 0x34, 0x04, 0xCD, 0x28, 0x34, 0x2F, 0x61,
    0x91, 0x72, 0xFE, 0x9C, 0xE9, 0x85, 0x83, 0xFF, 0x8E, 0x4F, 0x12, 0x32,
    0xEE, 0xF2, 0x81, 0x83, 0xC3, 0xFE, 0x3B, 0x1B, 0x4C, 0x6F, 0xAD, 0x73,
    0x3B, 0xB5, 0xFC, 0xBC, 0x2E, 0xC2, 0x20, 0x05, 0xC5, 0x8E, 0xF1, 0x83,
    0x7D, 0x16, 0x83, 0xB2, 0xC6, 0xF3, 0x4A, 0x26, 0xC1, 0xB2, 0xEF, 0xFA,
    0x88, 0x6B, 0x42, 0x38, 0x61, 0x1F, 0xCF, 0xDC, 0xDE, 0x35, 0x5B, 0x3B,
    0x65, 0x19, 0x03, 0x5B, 0xBC, 0x34, 0xF4, 0xDE, 0xF9, 0x9C, 0x02, 0x38,
    0x61, 0xB4, 0x6F, 0xC9, 0xD6, 0xE6, 0xC9, 0x07, 0x7A, 0xD9, 0x1D, 0x26,
    0x91, 0xF7, 0xF7, 0xEE, 0x59, 0x8C, 0xB0, 0xFA, 0xC1, 0x86, 0xD9, 0x1C,
    0xAE, 0xFE, 0x13, 0x09, 0x85, 0x13, 0x92, 0x70, 0xB4, 0x13, 0x0C, 0x93,
    0xBC, 0x43, 0x79, 0x44, 0xF4, 0xFD, 0x44, 0x52, 0xE2, 0xD7, 0x4D, 0xD3,
    0x64, 0xF2, 0xE2, 0x1E, 0x71, 0xF5, 0x4B, 0xFF, 0x5C, 0xAE, 0x82, 0xAB,
    0x9C, 0x9D, 0xF6, 0x9E, 0xE8, 0x6D, 0x2B, 0xC5, 0x22, 0x36, 0x3A, 0x0D,
    0xAB, 0xC5, 0x21, 0x97, 0x9B, 0x0D, 0xEA, 0xDA, 0x1D, 0xBF, 0x9A, 0x42,
    0xD5, 0xC4, 0x48, 0x4E, 0x0A, 0xBC, 0xD0, 0x6B, 0xFA, 0x53, 0xDD, 0xEF,
    0x3C, 0x1B, 0x20, 0xEE, 0x3F, 0xD5, 0x9D, 0x7C, 0x25, 0xE4, 0x1D, 0x2B,
    0x66, 0xC6, 0x2E, 0x37, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t xcryptographic_ffdhe4096_p[512] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAD, 0xF8, 0x54, 0x58,
    0xA2, 0xBB, 0x4A, 0x9A, 0xAF, 0xDC, 0x56, 0x20, 0x27, 0x3D, 0x3C, 0xF1,
    0xD8, 0xB9, 0xC5, 0x83, 0xCE, 0x2D, 0x36, 0x95, 0xA9, 0xE1, 0x36, 0x41,
    0x14, 0x64, 0x33, 0xFB, 0xCC, 0x93, 0x9D, 0xCE, 0x24, 0x9B, 0x3E, 0xF9,
    0x7D, 0x2F, 0xE3, 0x63, 0x63, 0x0C, 0x75, 0xD8, 0xF6, 0x81, 0xB2, 0x02,
    0xAE, 0xC4, 0x61, 0x7A, 0xD3, 0xDF, 0x1E, 0xD5, 0xD5, 0xFD, 0x65, 0x61,
    0x24, 0x33, 0xF5, 0x1F, 0x5F, 0x06, 0x6E, 0xD0, 0x85, 0x63, 0x65, 0x55,
    0x3D, 0xED, 0x1A, 0xF3, 0xB5, 0x57, 0x13, 0x5E, 0x7F, 0x57, 0xC9, 0x35,
    0x98, 0x4F, 0x0C, 0x70, 0xE0, 0xE6, 0x8B, 0x77, 0xE2, 0xA6, 0x89, 0xDA,
    0xF3, 0xEF, 0xE8, 0x72, 0x1D, 0xF1, 0x58, 0xA1, 0x36, 0xAD, 0xE7, 0x35,
    0x30, 0xAC, 0xCA, 0x4F, 0x48, 0x3A, 0x79, 0x7A, 0xBC, 0x0A, 0xB1, 0x82,
    0xB3, 0x24, 0xFB, 0x61, 0xD1, 0x08, 0xA9, 0x4B, 0xB2, 0xC8, 0xE3, 0xFB,
    0xB9, 0x6A, 0xDA, 0xB7, 0x60, 0xD7, 0xF4, 0x68, 0x1D, 0x4F, 0x42, 0xA3,
    0xDE, 0x39, 0x4D, 0xF4, 0xAE, 0x56, 0xED, 0xE7, 0x63, 0x72, 0xBB, 0x19,
    0x0B, 0x07, 0xA7, 0xC8, 0xEE, 0x0A, 0x6D, 0x70, 0x9E, 0x02, 0xFC, 0xE1,
    0xCD, 0xF7, 0xE2, 0xEC, 0xC0, 0x34, 0x04, 0xCD, 0x28, 0x34, 0x2F, 0x61,
    0x91, 0x72, 0xFE, 0x9C, 0xE9, 0x85, 0x83, 0xFF, 0x8E, 0x4F, 0x12, 0x32,
    0xEE, 0xF2, 0x81, 0x83, 0xC3, 0xFE, 0x3B, 0x1B, 0x4C, 0x6F, 0xAD, 0x73,
    0x3B, 0xB5, 0xFC, 0xBC, 0x2E, 0xC2, 0x20, 0x05, 0xC5, 0x8E, 0xF1, 0x83,
    0x7D, 0x16, 0x83, 0xB2, 0xC6, 0xF3, 0x4A, 0x26, 0xC1, 0xB2, 0xEF, 0xFA,
    0x88, 0x6B, 0x42, 0x38, 0x61, 0x1F, 0xCF, 0xDC, 0xDE, 0x35, 0x5B, 0x3B,
    0x65, 0x19, 0x03, 0x5B, 0xBC, 0x34, 0xF4, 0xDE, 0xF9, 0x9C, 0x02, 0x38,
    0x61, 0xB4, 0x6F, 0xC9, 0xD6, 0xE6, 0xC9, 0x07, 0x7A, 0xD9, 0x1D, 0x26,
    0x91, 0xF7, 0xF7, 0xEE, 0x59, 0x8C, 0xB0, 0xFA, 0xC1, 0x86, 0xD9, 0x1C,
    0xAE, 0xFE, 0x13, 0x09, 0x85, 0x13, 0x92, 0x70, 0xB4, 0x13, 0x0C, 0x93,
    0xBC, 0x43, 0x79, 0x44, 0xF4, 0xFD, 0x44, 0x52, 0xE2, 0xD7, 0x4D, 0xD3,
    0x64, 0xF2, 0xE2, 0x1E, 0x71, 0xF5, 0x4B, 0xFF, 0x5C, 0xAE, 0x82, 0xAB,
    0x9C, 0x9D, 0xF6, 0x9E, 0xE8, 0x6D, 0x2B, 0xC5, 0x22, 0x36, 0x3A, 0x0D,
    0xAB, 0xC5, 0x21, 0x97, 0x9B, 0x0D, 0xEA, 0xDA, 0x1D, 0xBF, 0x9A, 0x42,
    0xD5, 0xC4, 0x48, 0x4E, 0x0A, 0xBC, 0xD0, 0x6B, 0xFA, 0x53, 0xDD, 0xEF,
    0x3C, 0x1B, 0x20, 0xEE, 0x3F, 0xD5, 0x9D, 0x7C, 0x25, 0xE4, 0x1D, 0x2B,
    0x66, 0x9E, 0x1E, 0xF1, 0x6E, 0x6F, 0x52, 0xC3, 0x16, 0x4D, 0xF4, 0xFB,
    0x79, 0x30, 0xE9, 0xE4, 0xE5, 0x88, 0x57, 0xB6, 0xAC, 0x7D, 0x5F, 0x42,
    0xD6, 0x9F, 0x6D, 0x18, 0x77, 0x63, 0xCF, 0x1D, 0x55, 0x03, 0x40, 0x04,
    0x87, 0xF5, 0x5B, 0xA5, 0x7E, 0x31, 0xCC, 0x7A, 0x71, 0x35, 0xC8, 0x86,
    0xEF, 0xB4, 0x31, 0x8A, 0xED, 0x6A, 0x1E, 0x01, 0x2D, 0x9E, 0x68, 0x32,
    0xA9, 0x07, 0x60, 0x0A, 0x91, 0x81, 0x30, 0xC4, 0x6D, 0xC7, 0x78, 0xF9,
    0x71, 0xAD, 0x00, 0x38, 0x09, 0x29, 0x99, 0xA3, 0x33, 0xCB, 0x8B, 0x7A,
    0x1A, 0x1D, 0xB9, 0x3D, 0x71, 0x40, 0x00, 0x3C, 0x2A, 0x4E, 0xCE, 0xA9,
    0xF9, 0x8D, 0x0A, 0xCC, 0x0A, 0x82, 0x91, 0xCD, 0xCE, 0xC9, 0x7D, 0xCF,
    0x8E, 0xC9, 0xB5, 0x5A, 0x7F, 0x88, 0xA4, 0x6B, 0x4D, 0xB5, 0xA8, 0x51,
    0xF4, 0x41, 0x82, 0xE1, 0xC6, 0x8A, 0x00, 0x7E, 0x5E, 0x65, 0x5F, 0x6A,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t xcryptographic_ffdhe6144_p[768] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAD, 0xF8, 0x54, 0x58,
    0xA2, 0xBB, 0x4A, 0x9A, 0xAF, 0xDC, 0x56, 0x20, 0x27, 0x3D, 0x3C, 0xF1,
    0xD8, 0xB9, 0xC5, 0x83, 0xCE, 0x2D, 0x36, 0x95, 0xA9, 0xE1, 0x36, 0x41,
    0x14, 0x64, 0x33, 0xFB, 0xCC, 0x93, 0x9D, 0xCE, 0x24, 0x9B, 0x3E, 0xF9,
    0x7D, 0x2F, 0xE3, 0x63, 0x63, 0x0C, 0x75, 0xD8, 0xF6, 0x81, 0xB2, 0x02,
    0xAE, 0xC4, 0x61, 0x7A, 0xD3, 0xDF, 0x1E, 0xD5, 0xD5, 0xFD, 0x65, 0x61,
    0x24, 0x33, 0xF5, 0x1F, 0x5F, 0x06, 0x6E, 0xD0, 0x85, 0x63, 0x65, 0x55,
    0x3D, 0xED, 0x1A, 0xF3, 0xB5, 0x57, 0x13, 0x5E, 0x7F, 0x57, 0xC9, 0x35,
    0x98, 0x4F, 0x0C, 0x70, 0xE0, 0xE6, 0x8B, 0x77, 0xE2, 0xA6, 0x89, 0xDA,
    0xF3, 0xEF, 0xE8, 0x72, 0x1D, 0xF1, 0x58, 0xA1, 0x36, 0xAD, 0xE7, 0x35,
    0x30, 0xAC, 0xCA, 0x4F, 0x48, 0x3A, 0x79, 0x7A, 0xBC, 0x0A, 0xB1, 0x82,
    0xB3, 0x24, 0xFB, 0x61, 0xD1, 0x08, 0xA9, 0x4B, 0xB2, 0xC8, 0xE3, 0xFB,
    0xB9, 0x6A, 0xDA, 0xB7, 0x60, 0xD7, 0xF4, 0x68, 0x1D, 0x4F, 0x42, 0xA3,
    0xDE, 0x39, 0x4D, 0xF4, 0xAE, 0x56, 0xED, 0xE7, 0x63, 0x72, 0xBB, 0x19,
    0x0B, 0x07, 0xA7, 0xC8, 0xEE, 0x0A, 0x6D, 0x70, 0x9E, 0x02, 0xFC, 0xE1,
    0xCD, 0xF7, 0xE2, 0xEC, 0xC0, 0x34, 0x04, 0xCD, 0x28, 0x34, 0x2F, 0x61,
    0x91, 0x72, 0xFE, 0x9C, 0xE9, 0x85, 0x83, 0xFF, 0x8E, 0x4F, 0x12, 0x32,
    0xEE, 0xF2, 0x81, 0x83, 0xC3, 0xFE, 0x3B, 0x1B, 0x4C, 0x6F, 0xAD, 0x73,
    0x3B, 0xB5, 0xFC, 0xBC, 0x2E, 0xC2, 0x20, 0x05, 0xC5, 0x8E, 0xF1, 0x83,
    0x7D, 0x16, 0x83, 0xB2, 0xC6, 0xF3, 0x4A, 0x26, 0xC1, 0xB2, 0xEF, 0xFA,
    0x88, 0x6B, 0x42, 0x38, 0x61, 0x1F, 0xCF, 0xDC, 0xDE, 0x35, 0x5B, 0x3B,
    0x65, 0x19, 0x03, 0x5B, 0xBC, 0x34, 0xF4, 0xDE, 0xF9, 0x9C, 0x02, 0x38,
    0x61, 0xB4, 0x6F, 0xC9, 0xD6, 0xE6, 0xC9, 0x07, 0x7A, 0xD9, 0x1D, 0x26,
    0x91, 0xF7, 0xF7, 0xEE, 0x59, 0x8C, 0xB0, 0xFA, 0xC1, 0x86, 0xD9, 0x1C,
    0xAE, 0xFE, 0x13, 0x09, 0x85, 0x13, 0x92, 0x70, 0xB4, 0x13, 0x0C, 0x93,
    0xBC, 0x43, 0x79, 0x44, 0xF4, 0xFD, 0x44, 0x52, 0xE2, 0xD7, 0x4D, 0xD3,
    0x64, 0xF2, 0xE2, 0x1E, 0x71, 0xF5, 0x4B, 0xFF, 0x5C, 0xAE, 0x82, 0xAB,
    0x9C, 0x9D, 0xF6, 0x9E, 0xE8, 0x6D, 0x2B, 0xC5, 0x22, 0x36, 0x3A, 0x0D,
    0xAB, 0xC5, 0x21, 0x97, 0x9B, 0x0D, 0xEA, 0xDA, 0x1D, 0xBF, 0x9A, 0x42,
    0xD5, 0xC4, 0x48, 0x4E, 0x0A, 0xBC, 0xD0, 0x6B, 0xFA, 0x53, 0xDD, 0xEF,
    0x3C, 0x1B, 0x20, 0xEE, 0x3F, 0xD5, 0x9D, 0x7C, 0x25, 0xE4, 0x1D, 0x2B,
    0x66, 0x9E, 0x1E, 0xF1, 0x6E, 0x6F, 0x52, 0xC3, 0x16, 0x4D, 0xF4, 0xFB,
    0x79, 0x30, 0xE9, 0xE4, 0xE5, 0x88, 0x57, 0xB6, 0xAC, 0x7D, 0x5F, 0x42,
    0xD6, 0x9F, 0x6D, 0x18, 0x77, 0x63, 0xCF, 0x1D, 0x55, 0x03, 0x40, 0x04,
    0x87, 0xF5, 0x5B, 0xA5, 0x7E, 0x31, 0xCC, 0x7A, 0x71, 0x35, 0xC8, 0x86,
    0xEF, 0xB4, 0x31, 0x8A, 0xED, 0x6A, 0x1E, 0x01, 0x2D, 0x9E, 0x68, 0x32,
    0xA9, 0x07, 0x60, 0x0A, 0x91, 0x81, 0x30, 0xC4, 0x6D, 0xC7, 0x78, 0xF9,
    0x71, 0xAD, 0x00, 0x38, 0x09, 0x29, 0x99, 0xA3, 0x33, 0xCB, 0x8B, 0x7A,
    0x1A, 0x1D, 0xB9, 0x3D, 0x71, 0x40, 0x00, 0x3C, 0x2A, 0x4E, 0xCE, 0xA9,
    0xF9, 0x8D, 0x0A, 0xCC, 0x0A, 0x82, 0x91, 0xCD, 0xCE, 0xC9, 0x7D, 0xCF,
    0x8E, 0xC9, 0xB5, 0x5A, 0x7F, 0x88, 0xA4, 0x6B, 0x4D, 0xB5, 0xA8, 0x51,
    0xF4, 0x41, 0x82, 0xE1, 0xC6, 0x8A, 0x00, 0x7E, 0x5E, 0x0D, 0xD9, 0x02,
    0x0B, 0xFD, 0x64, 0xB6, 0x45, 0x03, 0x6C, 0x7A, 0x4E, 0x67, 0x7D, 0x2C,
    0x38, 0x53, 0x2A, 0x3A, 0x23, 0xBA, 0x44, 0x42, 0xCA, 0xF5, 0x3E, 0xA6,
    0x3B, 0xB4, 0x54, 0x32, 0x9B, 0x76, 0x24, 0xC8, 0x91, 0x7B, 0xDD, 0x64,
    0xB1, 0xC0, 0xFD, 0x4C, 0xB3, 0x8E, 0x8C, 0x33, 0x4C, 0x70, 0x1C, 0x3A,
    0xCD, 0xAD, 0x06, 0x57, 0xFC, 0xCF, 0xEC, 0x71, 0x9B, 0x1F, 0x5C, 0x3E,
    0x4E, 0x46, 0x04, 0x1F, 0x38, 0x81, 0x47, 0xFB, 0x4C, 0xFD, 0xB4, 0x77,
    0xA5, 0x24, 0x71, 0xF7, 0xA9, 0xA9, 0x69, 0x10, 0xB8, 0x55, 0x32, 0x2E,
    0xDB, 0x63, 0x40, 0xD8, 0xA0, 0x0E, 0xF0, 0x92, 0x35, 0x05, 0x11, 0xE3,
    0x0A, 0xBE, 0xC1, 0xFF, 0xF9, 0xE3, 0xA2, 0x6E, 0x7F, 0xB2, 0x9F, 0x8C,
    0x18, 0x30, 0x23, 0xC3, 0x58, 0x7E, 0x38, 0xDA, 0x00, 0x77, 0xD9, 0xB4,
    0x76, 0x3E, 0x4E, 0x4B, 0x94, 0xB2, 0xBB, 0xC1, 0x94, 0xC6, 0x65, 0x1E,
    0x77, 0xCA, 0xF9, 0x92, 0xEE, 0xAA, 0xC0, 0x23, 0x2A, 0x28, 0x1B, 0xF6,
    0xB3, 0xA7, 0x39, 0xC1, 0x22, 0x61, 0x16, 0x82, 0x0A, 0xE8, 0xDB, 0x58,
    0x47, 0xA6, 0x7C, 0xBE, 0xF9, 0xC9, 0x09, 0x1B, 0x46, 0x2D, 0x53, 0x8C,
    0xD7, 0x2B, 0x03, 0x74, 0x6A, 0xE7, 0x7F, 0x5E, 0x62, 0x29, 0x2C, 0x31,
    0x15, 0x62, 0xA8, 0x46, 0x50, 0x5D, 0xC8, 0x2D, 0xB8, 0x54, 0x33, 0x8A,
    0xE4, 0x9F, 0x52, 0x35, 0xC9, 0x5B, 0x91, 0x17, 0x8C, 0xCF, 0x2D, 0xD5,
    0xCA, 0xCE, 0xF4, 0x03, 0xEC, 0x9D, 0x18, 0x10, 0xC6, 0x27, 0x2B, 0x04,
    0x5B, 0x3B, 0x71, 0xF9, 0xDC, 0x6B, 0x80, 0xD6, 0x3F, 0xDD, 0x4A, 0x8E,
    0x9A, 0xDB, 0x1E, 0x69, 0x62, 0xA6, 0x95, 0x26, 0xD4, 0x31, 0x61, 0xC1,
    0xA4, 0x1D, 0x57, 0x0D, 0x79, 0x38, 0xDA, 0xD4, 0xA4, 0x0E, 0x32, 0x9C,
    0xD0, 0xE4, 0x0E, 0x65, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
static const uint8_t xcryptographic_ffdhe8192_p[1024] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAD, 0xF8, 0x54, 0x58,
    0xA2, 0xBB, 0x4A, 0x9A, 0xAF, 0xDC, 0x56, 0x20, 0x27, 0x3D, 0x3C, 0xF1,
    0xD8, 0xB9, 0xC5, 0x83, 0xCE, 0x2D, 0x36, 0x95, 0xA9, 0xE1, 0x36, 0x41,
    0x14, 0x64, 0x33, 0xFB, 0xCC, 0x93, 0x9D, 0xCE, 0x24, 0x9B, 0x3E, 0xF9,
    0x7D, 0x2F, 0xE3, 0x63, 0x63, 0x0C, 0x75, 0xD8, 0xF6, 0x81, 0xB2, 0x02,
    0xAE, 0xC4, 0x61, 0x7A, 0xD3, 0xDF, 0x1E, 0xD5, 0xD5, 0xFD, 0x65, 0x61,
    0x24, 0x33, 0xF5, 0x1F, 0x5F, 0x06, 0x6E, 0xD0, 0x85, 0x63, 0x65, 0x55,
    0x3D, 0xED, 0x1A, 0xF3, 0xB5, 0x57, 0x13, 0x5E, 0x7F, 0x57, 0xC9, 0x35,
    0x98, 0x4F, 0x0C, 0x70, 0xE0, 0xE6, 0x8B, 0x77, 0xE2, 0xA6, 0x89, 0xDA,
    0xF3, 0xEF, 0xE8, 0x72, 0x1D, 0xF1, 0x58, 0xA1, 0x36, 0xAD, 0xE7, 0x35,
    0x30, 0xAC, 0xCA, 0x4F, 0x48, 0x3A, 0x79, 0x7A, 0xBC, 0x0A, 0xB1, 0x82,
    0xB3, 0x24, 0xFB, 0x61, 0xD1, 0x08, 0xA9, 0x4B, 0xB2, 0xC8, 0xE3, 0xFB,
    0xB9, 0x6A, 0xDA, 0xB7, 0x60, 0xD7, 0xF4, 0x68, 0x1D, 0x4F, 0x42, 0xA3,
    0xDE, 0x39, 0x4D, 0xF4, 0xAE, 0x56, 0xED, 0xE7, 0x63, 0x72, 0xBB, 0x19,
    0x0B, 0x07, 0xA7, 0xC8, 0xEE, 0x0A, 0x6D, 0x70, 0x9E, 0x02, 0xFC, 0xE1,
    0xCD, 0xF7, 0xE2, 0xEC, 0xC0, 0x34, 0x04, 0xCD, 0x28, 0x34, 0x2F, 0x61,
    0x91, 0x72, 0xFE, 0x9C, 0xE9, 0x85, 0x83, 0xFF, 0x8E, 0x4F, 0x12, 0x32,
    0xEE, 0xF2, 0x81, 0x83, 0xC3, 0xFE, 0x3B, 0x1B, 0x4C, 0x6F, 0xAD, 0x73,
    0x3B, 0xB5, 0xFC, 0xBC, 0x2E, 0xC2, 0x20, 0x05, 0xC5, 0x8E, 0xF1, 0x83,
    0x7D, 0x16, 0x83, 0xB2, 0xC6, 0xF3, 0x4A, 0x26, 0xC1, 0xB2, 0xEF, 0xFA,
    0x88, 0x6B, 0x42, 0x38, 0x61, 0x1F, 0xCF, 0xDC, 0xDE, 0x35, 0x5B, 0x3B,
    0x65, 0x19, 0x03, 0x5B, 0xBC, 0x34, 0xF4, 0xDE, 0xF9, 0x9C, 0x02, 0x38,
    0x61, 0xB4, 0x6F, 0xC9, 0xD6, 0xE6, 0xC9, 0x07, 0x7A, 0xD9, 0x1D, 0x26,
    0x91, 0xF7, 0xF7, 0xEE, 0x59, 0x8C, 0xB0, 0xFA, 0xC1, 0x86, 0xD9, 0x1C,
    0xAE, 0xFE, 0x13, 0x09, 0x85, 0x13, 0x92, 0x70, 0xB4, 0x13, 0x0C, 0x93,
    0xBC, 0x43, 0x79, 0x44, 0xF4, 0xFD, 0x44, 0x52, 0xE2, 0xD7, 0x4D, 0xD3,
    0x64, 0xF2, 0xE2, 0x1E, 0x71, 0xF5, 0x4B, 0xFF, 0x5C, 0xAE, 0x82, 0xAB,
    0x9C, 0x9D, 0xF6, 0x9E, 0xE8, 0x6D, 0x2B, 0xC5, 0x22, 0x36, 0x3A, 0x0D,
    0xAB, 0xC5, 0x21, 0x97, 0x9B, 0x0D, 0xEA, 0xDA, 0x1D, 0xBF, 0x9A, 0x42,
    0xD5, 0xC4, 0x48, 0x4E, 0x0A, 0xBC, 0xD0, 0x6B, 0xFA, 0x53, 0xDD, 0xEF,
    0x3C, 0x1B, 0x20, 0xEE, 0x3F, 0xD5, 0x9D, 0x7C, 0x25, 0xE4, 0x1D, 0x2B,
    0x66, 0x9E, 0x1E, 0xF1, 0x6E, 0x6F, 0x52, 0xC3, 0x16, 0x4D, 0xF4, 0xFB,
    0x79, 0x30, 0xE9, 0xE4, 0xE5, 0x88, 0x57, 0xB6, 0xAC, 0x7D, 0x5F, 0x42,
    0xD6, 0x9F, 0x6D, 0x18, 0x77, 0x63, 0xCF, 0x1D, 0x55, 0x03, 0x40, 0x04,
    0x87, 0xF5, 0x5B, 0xA5, 0x7E, 0x31, 0xCC, 0x7A, 0x71, 0x35, 0xC8, 0x86,
    0xEF, 0xB4, 0x31, 0x8A, 0xED, 0x6A, 0x1E, 0x01, 0x2D, 0x9E, 0x68, 0x32,
    0xA9, 0x07, 0x60, 0x0A, 0x91, 0x81, 0x30, 0xC4, 0x6D, 0xC7, 0x78, 0xF9,
    0x71, 0xAD, 0x00, 0x38, 0x09, 0x29, 0x99, 0xA3, 0x33, 0xCB, 0x8B, 0x7A,
    0x1A, 0x1D, 0xB9, 0x3D, 0x71, 0x40, 0x00, 0x3C, 0x2A, 0x4E, 0xCE, 0xA9,
    0xF9, 0x8D, 0x0A, 0xCC, 0x0A, 0x82, 0x91, 0xCD, 0xCE, 0xC9, 0x7D, 0xCF,
    0x8E, 0xC9, 0xB5, 0x5A, 0x7F, 0x88, 0xA4, 0x6B, 0x4D, 0xB5, 0xA8, 0x51,
    0xF4, 0x41, 0x82, 0xE1, 0xC6, 0x8A, 0x00, 0x7E, 0x5E, 0x0D, 0xD9, 0x02,
    0x0B, 0xFD, 0x64, 0xB6, 0x45, 0x03, 0x6C, 0x7A, 0x4E, 0x67, 0x7D, 0x2C,
    0x38, 0x53, 0x2A, 0x3A, 0x23, 0xBA, 0x44, 0x42, 0xCA, 0xF5, 0x3E, 0xA6,
    0x3B, 0xB4, 0x54, 0x32, 0x9B, 0x76, 0x24, 0xC8, 0x91, 0x7B, 0xDD, 0x64,
    0xB1, 0xC0, 0xFD, 0x4C, 0xB3, 0x8E, 0x8C, 0x33, 0x4C, 0x70, 0x1C, 0x3A,
    0xCD, 0xAD, 0x06, 0x57, 0xFC, 0xCF, 0xEC, 0x71, 0x9B, 0x1F, 0x5C, 0x3E,
    0x4E, 0x46, 0x04, 0x1F, 0x38, 0x81, 0x47, 0xFB, 0x4C, 0xFD, 0xB4, 0x77,
    0xA5, 0x24, 0x71, 0xF7, 0xA9, 0xA9, 0x69, 0x10, 0xB8, 0x55, 0x32, 0x2E,
    0xDB, 0x63, 0x40, 0xD8, 0xA0, 0x0E, 0xF0, 0x92, 0x35, 0x05, 0x11, 0xE3,
    0x0A, 0xBE, 0xC1, 0xFF, 0xF9, 0xE3, 0xA2, 0x6E, 0x7F, 0xB2, 0x9F, 0x8C,
    0x18, 0x30, 0x23, 0xC3, 0x58, 0x7E, 0x38, 0xDA, 0x00, 0x77, 0xD9, 0xB4,
    0x76, 0x3E, 0x4E, 0x4B, 0x94, 0xB2, 0xBB, 0xC1, 0x94, 0xC6, 0x65, 0x1E,
    0x77, 0xCA, 0xF9, 0x92, 0xEE, 0xAA, 0xC0, 0x23, 0x2A, 0x28, 0x1B, 0xF6,
    0xB3, 0xA7, 0x39, 0xC1, 0x22, 0x61, 0x16, 0x82, 0x0A, 0xE8, 0xDB, 0x58,
    0x47, 0xA6, 0x7C, 0xBE, 0xF9, 0xC9, 0x09, 0x1B, 0x46, 0x2D, 0x53, 0x8C,
    0xD7, 0x2B, 0x03, 0x74, 0x6A, 0xE7, 0x7F, 0x5E, 0x62, 0x29, 0x2C, 0x31,
    0x15, 0x62, 0xA8, 0x46, 0x50, 0x5D, 0xC8, 0x2D, 0xB8, 0x54, 0x33, 0x8A,
    0xE4, 0x9F, 0x52, 0x35, 0xC9, 0x5B, 0x91, 0x17, 0x8C, 0xCF, 0x2D, 0xD5,
    0xCA, 0xCE, 0xF4, 0x03, 0xEC, 0x9D, 0x18, 0x10, 0xC6, 0x27, 0x2B, 0x04,
    0x5B, 0x3B, 0x71, 0xF9, 0xDC, 0x6B, 0x80, 0xD6, 0x3F, 0xDD, 0x4A, 0x8E,
    0x9A, 0xDB, 0x1E, 0x69, 0x62, 0xA6, 0x95, 0x26, 0xD4, 0x31, 0x61, 0xC1,
    0xA4, 0x1D, 0x57, 0x0D, 0x79, 0x38, 0xDA, 0xD4, 0xA4, 0x0E, 0x32, 0x9C,
    0xCF, 0xF4, 0x6A, 0xAA, 0x36, 0xAD, 0x00, 0x4C, 0xF6, 0x00, 0xC8, 0x38,
    0x1E, 0x42, 0x5A, 0x31, 0xD9, 0x51, 0xAE, 0x64, 0xFD, 0xB2, 0x3F, 0xCE,
    0xC9, 0x50, 0x9D, 0x43, 0x68, 0x7F, 0xEB, 0x69, 0xED, 0xD1, 0xCC, 0x5E,
    0x0B, 0x8C, 0xC3, 0xBD, 0xF6, 0x4B, 0x10, 0xEF, 0x86, 0xB6, 0x31, 0x42,
    0xA3, 0xAB, 0x88, 0x29, 0x55, 0x5B, 0x2F, 0x74, 0x7C, 0x93, 0x26, 0x65,
    0xCB, 0x2C, 0x0F, 0x1C, 0xC0, 0x1B, 0xD7, 0x02, 0x29, 0x38, 0x88, 0x39,
    0xD2, 0xAF, 0x05, 0xE4, 0x54, 0x50, 0x4A, 0xC7, 0x8B, 0x75, 0x82, 0x82,
    0x28, 0x46, 0xC0, 0xBA, 0x35, 0xC3, 0x5F, 0x5C, 0x59, 0x16, 0x0C, 0xC0,
    0x46, 0xFD, 0x82, 0x51, 0x54, 0x1F, 0xC6, 0x8C, 0x9C, 0x86, 0xB0, 0x22,
    0xBB, 0x70, 0x99, 0x87, 0x6A, 0x46, 0x0E, 0x74, 0x51, 0xA8, 0xA9, 0x31,
    0x09, 0x70, 0x3F, 0xEE, 0x1C, 0x21, 0x7E, 0x6C, 0x38, 0x26, 0xE5, 0x2C,
    0x51, 0xAA, 0x69, 0x1E, 0x0E, 0x42, 0x3C, 0xFC, 0x99, 0xE9, 0xE3, 0x16,
    0x50, 0xC1, 0x21, 0x7B, 0x62, 0x48, 0x16, 0xCD, 0xAD, 0x9A, 0x95, 0xF9,
    0xD5, 0xB8, 0x01, 0x94, 0x88, 0xD9, 0xC0, 0xA0, 0xA1, 0xFE, 0x30, 0x75,
    0xA5, 0x77, 0xE2, 0x31, 0x83, 0xF8, 0x1D, 0x4A, 0x3F, 0x2F, 0xA4, 0x57,
    0x1E, 0xFC, 0x8C, 0xE0, 0xBA, 0x8A, 0x4F, 0xE8, 0xB6, 0x85, 0x5D, 0xFE,
    0x72, 0xB0, 0xA6, 0x6E, 0xDE, 0xD2, 0xFB, 0xAB, 0xFB, 0xE5, 0x8A, 0x30,
    0xFA, 0xFA, 0xBE, 0x1C, 0x5D, 0x71, 0xA8, 0x7E, 0x2F, 0x74, 0x1E, 0xF8,
    0xC1, 0xFE, 0x86, 0xFE, 0xA6, 0xBB, 0xFD, 0xE5, 0x30, 0x67, 0x7F, 0x0D,
    0x97, 0xD1, 0x1D, 0x49, 0xF7, 0xA8, 0x44, 0x3D, 0x08, 0x22, 0xE5, 0x06,
    0xA9, 0xF4, 0x61, 0x4E, 0x01, 0x1E, 0x2A, 0x94, 0x83, 0x8F, 0xF8, 0x8C,
    0xD6, 0x8C, 0x8B, 0xB7, 0xC5, 0xC6, 0x42, 0x4C, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
};

static void xcbig_free(XCryptographic_BigInt *a)
{
    if (!a) return;
    if (a->d) {
        memset(a->d, 0, a->cap * sizeof(uint32_t));
        XFree_System(a->d);
    }
    a->d = NULL;
    a->n = 0;
    a->cap = 0;
}

static bool xcbig_alloc(XCryptographic_BigInt *a, size_t cap)
{
    if (!a) return false;
    a->d = (uint32_t *)XMalloc_System(cap * sizeof(uint32_t));
    if (!a->d) return false;
    memset(a->d, 0, cap * sizeof(uint32_t));
    a->n = 0;
    a->cap = cap;
    return true;
}

static void xcbig_zero(XCryptographic_BigInt *a)
{
    size_t i;
    if (!a) return;
    for (i = 0; i < a->cap; ++i) a->d[i] = 0;
    a->n = 0;
}

static void xcbig_trim(XCryptographic_BigInt *a)
{
    while (a->n > 0 && a->d[a->n - 1] == 0) --a->n;
}

static int xcbig_cmp(const XCryptographic_BigInt *a, const XCryptographic_BigInt *b)
{
    size_t i;
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (i = a->n; i > 0; --i) {
        if (a->d[i - 1] > b->d[i - 1]) return 1;
        if (a->d[i - 1] < b->d[i - 1]) return -1;
    }
    return 0;
}

static bool xcbig_from_be(XCryptographic_BigInt *out, const uint8_t *data, size_t len)
{
    size_t nlimbs = (len + 3) / 4;
    size_t i;
    if (!out || (!data && len > 0)) return false;
    if (nlimbs > out->cap) {
        uint32_t *nd = (uint32_t *)XMalloc_System(nlimbs * sizeof(uint32_t));
        if (!nd) return false;
        if (out->d) {
            memset(out->d, 0, out->cap * sizeof(uint32_t));
            XFree_System(out->d);
        }
        out->d = nd;
        out->cap = nlimbs;
    }
    xcbig_zero(out);
    for (i = 0; i < len; ++i) {
        size_t limb = (len - 1 - i) / 4;
        size_t shift = (size_t)((len - 1 - i) % 4) * 8;
        out->d[limb] |= (uint32_t)data[i] << shift;
    }
    out->n = nlimbs;
    xcbig_trim(out);
    return true;
}

static void xcbig_to_be(const XCryptographic_BigInt *in, uint8_t *data, size_t len)
{
    size_t i;
    if (!in || !data) return;
    memset(data, 0, len);
    for (i = 0; i < len; ++i) {
        size_t limb = (len - 1 - i) / 4;
        size_t shift = (size_t)((len - 1 - i) % 4) * 8;
        if (limb < in->n)
            data[i] = (uint8_t)(in->d[limb] >> shift);
    }
}

static void xcbig_add(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                      const XCryptographic_BigInt *b)
{
    size_t i, n = a->n > b->n ? a->n : b->n;
    uint64_t carry = 0;
    if (!out || !a || !b) return;
    if (n + 1 > out->cap) {
        uint32_t *nd = (uint32_t *)XMalloc_System((n + 1) * sizeof(uint32_t));
        if (!nd) return;
        if (out->d) {
            memset(out->d, 0, out->cap * sizeof(uint32_t));
            XFree_System(out->d);
        }
        out->d = nd;
        out->cap = n + 1;
    }
    for (i = 0; i < n; ++i) {
        uint32_t ai = i < a->n ? a->d[i] : 0;
        uint32_t bi = i < b->n ? b->d[i] : 0;
        uint64_t sum = (uint64_t)ai + bi + carry;
        out->d[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    out->d[n] = (uint32_t)carry;
    out->n = n + 1;
    xcbig_trim(out);
}

static void xcbig_sub(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                      const XCryptographic_BigInt *b)
{
    size_t i;
    uint64_t borrow = 0;
    size_t n = a->n > b->n ? a->n : b->n;
    if (!out || !a || !b) return;
    if (n > out->cap) {
        uint32_t *nd = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
        if (!nd) return;
        if (out->d) {
            memset(out->d, 0, out->cap * sizeof(uint32_t));
            XFree_System(out->d);
        }
        out->d = nd;
        out->cap = n;
    }
    for (i = 0; i < n; ++i) {
        uint32_t ai = i < a->n ? a->d[i] : 0;
        uint64_t bi = (uint64_t)(i < b->n ? b->d[i] : 0) + borrow;
        out->d[i] = (uint32_t)((uint64_t)ai - bi);
        borrow = ((uint64_t)ai < bi) ? 1u : 0u;
    }
    out->n = n;
    xcbig_trim(out);
}

static void xcbig_copy_into(XCryptographic_BigInt *dst, const XCryptographic_BigInt *src)
{
    if (!dst || !src) return;
    if (dst->cap < src->n) return;
    memset(dst->d, 0, dst->cap * sizeof(uint32_t));
    memcpy(dst->d, src->d, src->n * sizeof(uint32_t));
    dst->n = src->n;
}

static bool xcbig_double_mod(XCryptographic_BigInt *a, const XCryptographic_BigInt *mod)
{
    XCryptographic_BigInt t, r;
    if (!a || !mod || !a->d) return false;
    if (!xcbig_alloc(&t, a->cap + 1)) return false;
    xcbig_add(&t, a, a);
    if (xcbig_cmp(&t, mod) >= 0) {
        if (!xcbig_alloc(&r, t.cap)) { xcbig_free(&t); return false; }
        xcbig_sub(&r, &t, mod);
        if (xcbig_cmp(&r, mod) >= 0) {
            XCryptographic_BigInt r2;
            if (!xcbig_alloc(&r2, r.cap)) { xcbig_free(&r); xcbig_free(&t); return false; }
            xcbig_sub(&r2, &r, mod);
            xcbig_copy_into(a, &r2);
            xcbig_free(&r2);
        } else {
            xcbig_copy_into(a, &r);
        }
        xcbig_free(&r);
    } else {
        xcbig_copy_into(a, &t);
    }
    xcbig_free(&t);
    return true;
}

/* 固定 n-limb 数组：比较 */
static int xcbig_arr_cmp(const uint32_t *a, const uint32_t *b, size_t n)
{
    size_t i;
    for (i = n; i > 0; --i) {
        if (a[i - 1] > b[i - 1]) return 1;
        if (a[i - 1] < b[i - 1]) return -1;
    }
    return 0;
}

/* 固定 n-limb 数组：out = a - b（要求 a >= b，a/out 可指向同一数组） */
static void xcbig_arr_sub(uint32_t *out, const uint32_t *a, const uint32_t *b, size_t n)
{
    size_t i;
    uint64_t borrow = 0;
    for (i = 0; i < n; ++i) {
        uint32_t ai = a[i];
        uint64_t bi = (uint64_t)b[i] + borrow;
        out[i] = (uint32_t)((uint64_t)ai - bi);
        borrow = ((uint64_t)ai < bi) ? 1u : 0u;
    }
}

/* Montgomery 常量：-m[0]^{-1} mod 2^32（Newton 迭代）。 */
static uint32_t xcbig_n0inv(uint32_t m0)
{
    uint32_t x = 1u;
    int i;
    for (i = 0; i < 5; ++i)
        x = (uint32_t)((uint64_t)x * (2u - (uint64_t)m0 * x));
    return 0u - x;
}

/* Montgomery 乘法：out = a*b*R^{-1} mod m. 所有数组均为 n-limb 小端。
 * 采用大整数 Montgomery 归约：T = a*b；循环 n 次：
 *   u = T[0] * n0inv (mod 2^32)
 *   T += u*m，carry 传播到高位
 *   T >>= 32（丢弃低 limb）
 * 结束后低 n 个 limb 为 0，结果位于 T[0..n-1]，T[n] 为进位。
 */
static void xcbig_mont_mul(uint32_t *out, const uint32_t *a, const uint32_t *b,
                           const uint32_t *m, uint32_t n0inv, size_t n)
{
    size_t i, j, k;
    uint32_t *T = (uint32_t *)XMalloc_System((2 * n + 1) * sizeof(uint32_t));
    if (!T) return;
    memset(T, 0, (2 * n + 1) * sizeof(uint32_t));
    /* T = a*b（2n limb，carry 逐位传播） */
    for (i = 0; i < n; ++i) {
        uint64_t carry = 0;
        for (j = 0; j < n; ++j) {
            uint64_t prod = (uint64_t)a[i] * b[j] + T[i + j] + carry;
            T[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        k = i + n;
        while (carry) {
            uint64_t v = (uint64_t)T[k] + carry;
            T[k] = (uint32_t)v;
            carry = v >> 32;
            ++k;
        }
    }
    /* Montgomery 归约 */
    for (i = 0; i < n; ++i) {
        uint32_t u = (uint32_t)((uint64_t)T[0] * n0inv);
        uint64_t carry = 0;
        for (j = 0; j <= n; ++j) {
            uint64_t prod = (uint64_t)u * (j < n ? m[j] : 0) + T[j] + carry;
            T[j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        j = n + 1;
        while (carry && j <= 2 * n) {
            uint64_t v = (uint64_t)T[j] + carry;
            T[j] = (uint32_t)v;
            carry = v >> 32;
            ++j;
        }
        /* 右移一个 limb */
        for (j = 0; j < 2 * n; ++j)
            T[j] = T[j + 1];
        T[2 * n] = 0;
    }
    memcpy(out, T, n * sizeof(uint32_t));
    if (T[n] != 0)
        xcbig_arr_sub(out, out, m, n);
    if (xcbig_arr_cmp(out, m, n) >= 0)
        xcbig_arr_sub(out, out, m, n);
    memset(T, 0, (2 * n + 1) * sizeof(uint32_t));
    XFree_System(T);
}

/* 计算 R2 = 2^(64*n) mod m（n 为 limb 数）。 */
static bool xcbig_mont_r2(XCryptographic_BigInt *r2, const XCryptographic_BigInt *m)
{
    size_t i, bits = m->n * 64;
    if (!xcbig_alloc(r2, m->n + 1)) return false;
    xcbig_zero(r2);
    r2->d[0] = 1;
    r2->n = 1;
    for (i = 0; i < bits; ++i) {
        if (!xcbig_double_mod(r2, m)) return false;
    }
    return true;
}

/* Montgomery 指数：out = base^exp mod m. */
static bool xcbig_mod(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                      const XCryptographic_BigInt *m);
static bool xcbig_mont_exp(XCryptographic_BigInt *out,
                           const XCryptographic_BigInt *base,
                           const XCryptographic_BigInt *exp,
                           const XCryptographic_BigInt *m)
{
    size_t n = m->n;
    size_t i, total_bits;
    uint32_t n0inv;
    XCryptographic_BigInt r2;
    XCryptographic_BigInt red = {0};
    uint32_t *M = NULL, *R = NULL, *B = NULL, *T = NULL;
    bool ok = false;
    if (!out || !base || !exp || !m || !m->d || m->n == 0 || (m->d[0] & 1u) == 0)
        return false;
    /* base 必须先约减到 [0, m) 再转 Montgomery 形式。 */
    if (xcbig_cmp(base, m) >= 0) {
        if (!xcbig_alloc(&red, base->n + 1) || !xcbig_mod(&red, base, m))
            goto cleanup;
        base = &red;
    }
    n0inv = xcbig_n0inv(m->d[0]);
    total_bits = exp->n * 32;
    M = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
    R = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
    B = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
    T = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
    if (!M || !R || !B || !T) goto cleanup;
    memcpy(M, m->d, n * sizeof(uint32_t));
    if (!xcbig_mont_r2(&r2, m)) goto cleanup;
    /* R = toMont(1) = 1*R mod m = MontMul(1, R2) */
    memset(R, 0, n * sizeof(uint32_t));
    R[0] = 1;
    {
        uint32_t *R2 = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
        if (!R2) goto cleanup;
        memset(R2, 0, n * sizeof(uint32_t));
        memcpy(R2, r2.d, (r2.n < n ? r2.n : n) * sizeof(uint32_t));
        xcbig_mont_mul(R, R, R2, M, n0inv, n);
        XFree_System(R2);
    }
    /* B = toMont(base) */
    memset(B, 0, n * sizeof(uint32_t));
    memcpy(B, base->d, (base->n < n ? base->n : n) * sizeof(uint32_t));
    {
        uint32_t *R2b = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
        if (!R2b) goto cleanup;
        memset(R2b, 0, n * sizeof(uint32_t));
        memcpy(R2b, r2.d, (r2.n < n ? r2.n : n) * sizeof(uint32_t));
        xcbig_mont_mul(B, B, R2b, M, n0inv, n);
        XFree_System(R2b);
    }
    /* 从低位到高位二进指数（右到左平方乘）。 */
    for (i = 0; i < total_bits; ++i) {
        size_t limb = i / 32;
        size_t bit = i % 32;
        if ((exp->d[limb] >> bit) & 1u) {
            memcpy(T, R, n * sizeof(uint32_t));
            xcbig_mont_mul(R, T, B, M, n0inv, n);
        }
        memcpy(T, B, n * sizeof(uint32_t));
        xcbig_mont_mul(B, T, T, M, n0inv, n);
    }
    /* out = fromMont(R) = MontMul(R, 1) */
    {
        uint32_t *One = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
        if (!One) goto cleanup;
        memset(One, 0, n * sizeof(uint32_t));
        One[0] = 1;
        memcpy(T, R, n * sizeof(uint32_t));
        xcbig_mont_mul(R, T, One, M, n0inv, n);
        XFree_System(One);
    }
    xcbig_zero(out);
    if (out->cap < n) {
        uint32_t *nd = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
        if (!nd) goto cleanup;
        if (out->d) {
            memset(out->d, 0, out->cap * sizeof(uint32_t));
            XFree_System(out->d);
        }
        out->d = nd;
        out->cap = n;
    }
    memcpy(out->d, R, n * sizeof(uint32_t));
    out->n = n;
    xcbig_trim(out);
    ok = true;
cleanup:
    XFree_System(M); XFree_System(R); XFree_System(B); XFree_System(T);
    xcbig_free(&red);
    xcbig_free(&r2);
    return ok;
}

/* 获取 RFC 7919 素数（大端字节）。 */
static const uint8_t *xcffdh_prime(size_t keySize, size_t *primeLen)
{
    switch (keySize) {
    case 256: *primeLen = sizeof(xcryptographic_ffdhe2048_p); return xcryptographic_ffdhe2048_p;
    case 384: *primeLen = sizeof(xcryptographic_ffdhe3072_p); return xcryptographic_ffdhe3072_p;
    case 512: *primeLen = sizeof(xcryptographic_ffdhe4096_p); return xcryptographic_ffdhe4096_p;
    case 768: *primeLen = sizeof(xcryptographic_ffdhe6144_p); return xcryptographic_ffdhe6144_p;
    case 1024: *primeLen = sizeof(xcryptographic_ffdhe8192_p); return xcryptographic_ffdhe8192_p;
    default: *primeLen = 0; return NULL;
    }
}

bool XCryptographic_ffdhGeneratePrivateKeyInto(char* buffer, size_t bufferSize)
{
    const uint8_t *prime;
    size_t primeLen;
    XCryptographic_BigInt P, X, one;
    size_t attempts;
    uint8_t *tmp = NULL;
    bool ok = false;
    if (!buffer || !XCRYPTOGRAPHIC_FFDH_ON) return false;
    prime = xcffdh_prime(bufferSize, &primeLen);
    if (!prime || primeLen != bufferSize) return false;
    if (!xcbig_alloc(&P, (primeLen + 3) / 4) ||
        !xcbig_alloc(&X, (primeLen + 3) / 4) ||
        !xcbig_alloc(&one, 1)) goto cleanup;
    if (!xcbig_from_be(&P, prime, primeLen)) goto cleanup;
    tmp = (uint8_t *)XMalloc_System(bufferSize);
    if (!tmp) goto cleanup;
    one.d[0] = 1; one.n = 1;
    /* RFC 7919: 私钥取值 [2, P-2]；生成 [3, P-1] 再减 1。 */
    for (attempts = 0; attempts < 128; ++attempts) {
        if (!xcryptographic_random(tmp, bufferSize)) goto cleanup;
        if (!xcbig_from_be(&X, tmp, bufferSize)) goto cleanup;
        if (xcbig_cmp(&X, &P) >= 0) continue;
        if (X.n == 0 || (X.n == 1 && X.d[0] < 3)) continue;
        xcbig_sub(&X, &X, &one);
        if (xcbig_cmp(&X, &P) >= 0) continue;
        xcbig_to_be(&X, (uint8_t *)buffer, bufferSize);
        ok = true;
        break;
    }
cleanup:
    if (tmp) { memset(tmp, 0, bufferSize); XFree_System(tmp); }
    xcbig_free(&P); xcbig_free(&X); xcbig_free(&one);
    return ok;
}

XByteArrayView XCryptographic_ffdhExportPublicKeyInto(
    char* buffer, size_t bufferSize,
    const char* privateKey, size_t privateKeyLen)
{
    const uint8_t *prime;
    size_t primeLen;
    XCryptographic_BigInt P, G, X, GX;
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = empty;
    if (!buffer || !privateKey || !XCRYPTOGRAPHIC_FFDH_ON ||
        privateKeyLen != bufferSize) return empty;
    prime = xcffdh_prime(bufferSize, &primeLen);
    if (!prime || primeLen != bufferSize) return empty;
    if (!xcbig_alloc(&P, (primeLen + 3) / 4) ||
        !xcbig_alloc(&G, (primeLen + 3) / 4) ||
        !xcbig_alloc(&X, (primeLen + 3) / 4) ||
        !xcbig_alloc(&GX, (primeLen + 3) / 4)) goto cleanup;
    if (!xcbig_from_be(&P, prime, primeLen) ||
        !xcbig_from_be(&X, (const uint8_t *)privateKey, privateKeyLen)) goto cleanup;
    G.d[0] = 2; G.n = 1;
    if (!xcbig_mont_exp(&GX, &G, &X, &P)) goto cleanup;
    xcbig_to_be(&GX, (uint8_t *)buffer, bufferSize);
    result.m_data = (const uint8_t *)buffer;
    result.m_size = (int64_t)bufferSize;
cleanup:
    xcbig_free(&P); xcbig_free(&G); xcbig_free(&X); xcbig_free(&GX);
    return result;
}

XByteArrayView XCryptographic_ffdhAgreeInto(
    char* buffer, size_t bufferSize,
    const char* privateKey, size_t privateKeyLen,
    const char* peerPublicKey, size_t peerPublicKeyLen)
{
    const uint8_t *prime;
    size_t primeLen;
    XCryptographic_BigInt P, X, GY, K, Pminus1, one;
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = empty;
    if (!buffer || !privateKey || !peerPublicKey || !XCRYPTOGRAPHIC_FFDH_ON ||
        privateKeyLen != bufferSize || peerPublicKeyLen != bufferSize) return empty;
    prime = xcffdh_prime(bufferSize, &primeLen);
    if (!prime || primeLen != bufferSize) return empty;
    if (!xcbig_alloc(&P, (primeLen + 3) / 4) ||
        !xcbig_alloc(&X, (primeLen + 3) / 4) ||
        !xcbig_alloc(&GY, (primeLen + 3) / 4) ||
        !xcbig_alloc(&K, (primeLen + 3) / 4) ||
        !xcbig_alloc(&Pminus1, (primeLen + 3) / 4) ||
        !xcbig_alloc(&one, 1)) goto cleanup;
    if (!xcbig_from_be(&P, prime, primeLen) ||
        !xcbig_from_be(&X, (const uint8_t *)privateKey, privateKeyLen) ||
        !xcbig_from_be(&GY, (const uint8_t *)peerPublicKey, peerPublicKeyLen)) goto cleanup;
    one.d[0] = 1; one.n = 1;
    xcbig_sub(&Pminus1, &P, &one);
    if (xcbig_cmp(&GY, &one) <= 0 || xcbig_cmp(&GY, &Pminus1) >= 0) goto cleanup;
    if (!xcbig_mont_exp(&K, &GY, &X, &P)) goto cleanup;
    xcbig_to_be(&K, (uint8_t *)buffer, bufferSize);
    result.m_data = (const uint8_t *)buffer;
    result.m_size = (int64_t)bufferSize;
cleanup:
    xcbig_free(&P); xcbig_free(&X); xcbig_free(&GY);
    xcbig_free(&K); xcbig_free(&Pminus1); xcbig_free(&one);
    return result;
}

/* =============== FFDH 结束 =============== */

bool XCryptographic_aesCtrImportKey(XByteArrayView key,
                                        XCryptographic_Key* result)
{
    return key.m_size >= 0 && xcryptographic_aes_ctr_import_key(
        key.m_data, (size_t)key.m_size, result);
}

bool XCryptographic_aesCtrSetup(XCryptographic_CipherOperation* operation,
                                    XCryptographic_Key key, bool encrypt,
                                    XByteArrayView iv)
{
    return iv.m_size >= 0 && xcryptographic_aes_ctr_setup(
        operation, key, encrypt, iv.m_data, (size_t)iv.m_size);
}

XByteArrayView XCryptographic_aesCtrUpdateInto(
    XCryptographic_CipherOperation* operation,
    char* buffer, size_t bufferSize, XByteArrayView data)
{
    size_t outputLen = 0;
    XByteArrayView empty = { NULL, 0 };
    if (data.m_size < 0 || !xcryptographic_aes_ctr_update(
            operation, data.m_data, (size_t)data.m_size, buffer, bufferSize, &outputLen) ||
        outputLen != (size_t)data.m_size)
        return empty;
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)outputLen };
        return result;
    }
}

XByteArray* XCryptographic_aesCtrUpdate(
    XCryptographic_CipherOperation* operation, XByteArrayView data)
{
    XByteArray* result;
    XByteArrayView view;
    if (data.m_size < 0) return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base(result, (size_t)data.m_size)) {
        if (result) XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    view = XCryptographic_aesCtrUpdateInto(operation,
        (char*)XByteArray_data(result), (size_t)data.m_size, data);
    if (!view.m_data) {
        XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

XByteArrayView XCryptographic_exportPublicKeyInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key)
{
    size_t outputLen = 0;
    XByteArrayView empty = { NULL, 0 };
    if (!xcryptographic_export_public_key(key, buffer, bufferSize, &outputLen))
        return empty;
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)outputLen };
        return result;
    }
}

XByteArray* XCryptographic_exportPublicKey(XCryptographic_Key key)
{
    XByteArray* result;
    XByteArrayView view;
    if (key.publicKeyLen == 0) return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base(result, key.publicKeyLen)) {
        if (result) XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    view = XCryptographic_exportPublicKeyInto((char*)XByteArray_data(result),
                                                   key.publicKeyLen, key);
    if (!view.m_data) {
        XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

XByteArrayView XCryptographic_ecdhAgreeInto(
    char* buffer, size_t bufferSize, XCryptographic_Key privateKey,
    XByteArrayView peerPublicKey)
{
    size_t outputLen = 0;
    XByteArrayView empty = { NULL, 0 };
    if (peerPublicKey.m_size < 0 || !xcryptographic_ecdh_agree(
            privateKey, peerPublicKey.m_data, (size_t)peerPublicKey.m_size,
            buffer, bufferSize, &outputLen)) return empty;
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)outputLen };
        return result;
    }
}

XByteArray* XCryptographic_ecdhAgree(XCryptographic_Key privateKey,
                                         XByteArrayView peerPublicKey)
{
    size_t outputSize = privateKey.type == XCryptographic_KeyType_EcdhNistP521 ? 66 :
                        ((privateKey.type == XCryptographic_KeyType_EcdhNistP384 ||
                          privateKey.type == XCryptographic_KeyType_EcdhBrainpoolP384r1) ? 48 :
                         ((privateKey.type == XCryptographic_KeyType_X448 ? 56 :
                           (privateKey.type == XCryptographic_KeyType_EcdhBrainpoolP512r1 ? 64 : 32))));
    XByteArray* result = XByteArray_create();
    XByteArrayView view;
    if (!result || !XByteArray_resize_base(result, outputSize)) {
        if (result) XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    view = XCryptographic_ecdhAgreeInto((char*)XByteArray_data(result), outputSize,
                                             privateKey, peerPublicKey);
    if (!view.m_data) {
        XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}


bool XCryptographic_ecdsaImportPrivateKey(
    XCryptographic_EcdsaAlgorithm algorithm, XByteArrayView key,
    XCryptographic_Key* result)
{
    if (key.m_size < 0) return false;
    switch (algorithm) {
        case XCryptographic_EcdsaAlgorithm_NistP256:
            return xcryptographic_ecdsa_p256_import_private_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_NistP384:
            return xcryptographic_ecdsa_p384_import_private_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_NistP521:
            return xcryptographic_ecdsa_p521_import_private_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP256r1:
            return xcryptographic_ecdsa_brainpool_p256r1_import_private_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP384r1:
            return xcryptographic_ecdsa_brainpool_p384r1_import_private_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP512r1:
            return xcryptographic_ecdsa_brainpool_p512r1_import_private_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_Secp256k1:
            return xcryptographic_ecdsa_secp256k1_import_private_key(
                key.m_data, (size_t)key.m_size, result);
        default:
            return false;
    }
}

bool XCryptographic_ecdsaGenerateKey(
    XCryptographic_EcdsaAlgorithm algorithm, XCryptographic_Key* result)
{
    uint8_t privateKey[66];
    if (!result) return false;
    switch (algorithm) {
        case XCryptographic_EcdsaAlgorithm_NistP256:
            return xcryptographic_random_scalar(privateKey) &&
                   xcryptographic_ecdsa_p256_import_private_key(privateKey, 32, result);
        case XCryptographic_EcdsaAlgorithm_NistP384:
            return xcryptographic_p384_random_scalar(privateKey) &&
                   xcryptographic_ecdsa_p384_import_private_key(privateKey, 48, result);
        case XCryptographic_EcdsaAlgorithm_NistP521:
            return xcryptographic_p521_random_scalar(privateKey) &&
                   xcryptographic_ecdsa_p521_import_private_key(privateKey, 66, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP256r1:
            return xcryptographic_brainpool_p256r1_random_scalar(privateKey) &&
                   xcryptographic_ecdsa_brainpool_p256r1_import_private_key(privateKey, 32, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP384r1:
            return xcryptographic_brainpool_p384r1_random_scalar(privateKey) &&
                   xcryptographic_ecdsa_brainpool_p384r1_import_private_key(privateKey, 48, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP512r1:
            return xcryptographic_brainpool_p512r1_random_scalar(privateKey) &&
                   xcryptographic_ecdsa_brainpool_p512r1_import_private_key(privateKey, 64, result);
        case XCryptographic_EcdsaAlgorithm_Secp256k1:
            return xcryptographic_secp256k1_random_scalar(privateKey) &&
                   xcryptographic_ecdsa_secp256k1_import_private_key(privateKey, 32, result);
        default:
            return false;
    }
}

bool XCryptographic_ecdsaImportPublicKey(
    XCryptographic_EcdsaAlgorithm algorithm, XByteArrayView key,
    XCryptographic_Key* result)
{
    if (key.m_size < 0) return false;
    switch (algorithm) {
        case XCryptographic_EcdsaAlgorithm_NistP256:
            return xcryptographic_ecdsa_p256_import_public_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_NistP384:
            return xcryptographic_ecdsa_p384_import_public_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_NistP521:
            return xcryptographic_ecdsa_p521_import_public_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP256r1:
            return xcryptographic_ecdsa_brainpool_p256r1_import_public_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP384r1:
            return xcryptographic_ecdsa_brainpool_p384r1_import_public_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_BrainpoolP512r1:
            return xcryptographic_ecdsa_brainpool_p512r1_import_public_key(
                key.m_data, (size_t)key.m_size, result);
        case XCryptographic_EcdsaAlgorithm_Secp256k1:
            return xcryptographic_ecdsa_secp256k1_import_public_key(
                key.m_data, (size_t)key.m_size, result);
        default:
            return false;
    }
}

static size_t xcryptographic_ecdsa_private_key_size(XCryptographic_KeyType type)
{
    switch (type) {
        case XCryptographic_KeyType_EcdsaNistP256Private:
        case XCryptographic_KeyType_EcdsaSecp256k1Private:
        case XCryptographic_KeyType_EcdsaBrainpoolP256r1Private:
            return 32;
        case XCryptographic_KeyType_EcdsaNistP384Private:
        case XCryptographic_KeyType_EcdsaBrainpoolP384r1Private:
            return 48;
        case XCryptographic_KeyType_EcdsaNistP521Private:
            return 66;
        case XCryptographic_KeyType_EcdsaBrainpoolP512r1Private:
            return 64;
        default:
            return 0;
    }
}

static size_t xcryptographic_ecdsa_signature_size(XCryptographic_KeyType type)
{
    switch (type) {
        case XCryptographic_KeyType_EcdsaNistP256Private:
        case XCryptographic_KeyType_EcdsaNistP256Public:
        case XCryptographic_KeyType_EcdsaSecp256k1Private:
        case XCryptographic_KeyType_EcdsaSecp256k1Public:
        case XCryptographic_KeyType_EcdsaBrainpoolP256r1Private:
        case XCryptographic_KeyType_EcdsaBrainpoolP256r1Public:
            return 64;
        case XCryptographic_KeyType_EcdsaNistP384Private:
        case XCryptographic_KeyType_EcdsaNistP384Public:
        case XCryptographic_KeyType_EcdsaBrainpoolP384r1Private:
        case XCryptographic_KeyType_EcdsaBrainpoolP384r1Public:
            return 96;
        case XCryptographic_KeyType_EcdsaNistP521Private:
        case XCryptographic_KeyType_EcdsaNistP521Public:
            return 132;
        case XCryptographic_KeyType_EcdsaBrainpoolP512r1Private:
        case XCryptographic_KeyType_EcdsaBrainpoolP512r1Public:
            return 128;
        default:
            return 0;
    }
}

XByteArrayView XCryptographic_ecdsaExportPrivateKeyInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key)
{
    XByteArrayView empty = { NULL, 0 };
    size_t keySize = xcryptographic_ecdsa_private_key_size(key.type);
    if (!buffer || keySize == 0 || bufferSize < keySize) return empty;
    memcpy(buffer, key.privateKey, keySize);
    return XByteArrayView_create_data(buffer, (int64_t)keySize);
}

XByteArray* XCryptographic_ecdsaExportPrivateKey(XCryptographic_Key key)
{
    size_t keySize = xcryptographic_ecdsa_private_key_size(key.type);
    XByteArray* result;
    XByteArrayView view;
    if (keySize == 0) return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base(result, keySize)) {
        if (result) XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    view = XCryptographic_ecdsaExportPrivateKeyInto(
        (char*)XByteArray_data(result), keySize, key);
    if (!view.m_data) {
        XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

XByteArrayView XCryptographic_ecdsaSignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash, bool deterministic)
{
    XByteArrayView empty = { NULL, 0 };
    if (!buffer || hash.m_size < 0) return empty;
    switch (key.type) {
        case XCryptographic_KeyType_EcdsaNistP256Private:
            if (deterministic) {
                size_t signatureLen = 0;
                if (!xcryptographic_ecdsa_p256_sign_hash_deterministic(
                        key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
                        &signatureLen)) return empty;
                return XByteArrayView_create_data(buffer, (int64_t)signatureLen);
            } else {
                size_t signatureLen = 0;
                if (!xcryptographic_ecdsa_p256_sign_hash(
                        key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
                        &signatureLen)) return empty;
                return XByteArrayView_create_data(buffer, (int64_t)signatureLen);
            }
        case XCryptographic_KeyType_EcdsaNistP256Public:
            return empty;
        case XCryptographic_KeyType_EcdsaNistP384Private:
            {
                size_t signatureLen = 0;
                if (!xcryptographic_ecdsa_p384_sign_hash(
                        key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
                        &signatureLen, deterministic)) return empty;
                return XByteArrayView_create_data(buffer, (int64_t)signatureLen);
            }
        case XCryptographic_KeyType_EcdsaNistP384Public:
            return empty;
        case XCryptographic_KeyType_EcdsaNistP521Private:
            {
                size_t signatureLen = 0;
                if (!xcryptographic_ecdsa_p521_sign_hash(
                        key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
                        &signatureLen, deterministic)) return empty;
                return XByteArrayView_create_data(buffer, (int64_t)signatureLen);
            }
        case XCryptographic_KeyType_EcdsaNistP521Public:
            return empty;
        case XCryptographic_KeyType_EcdsaBrainpoolP256r1Private:
            {
                size_t signatureLen = 0;
                if (!xcryptographic_ecdsa_brainpool_p256r1_sign_hash(
                        key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
                        &signatureLen, deterministic)) return empty;
                return XByteArrayView_create_data(buffer, (int64_t)signatureLen);
            }
        case XCryptographic_KeyType_EcdsaBrainpoolP256r1Public:
            return empty;
        case XCryptographic_KeyType_EcdsaBrainpoolP384r1Private:
            {
                size_t signatureLen = 0;
                if (!xcryptographic_ecdsa_brainpool_p384r1_sign_hash(
                        key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
                        &signatureLen, deterministic)) return empty;
                return XByteArrayView_create_data(buffer, (int64_t)signatureLen);
            }
        case XCryptographic_KeyType_EcdsaBrainpoolP384r1Public:
            return empty;
        case XCryptographic_KeyType_EcdsaBrainpoolP512r1Private:
            {
                size_t signatureLen = 0;
                if (!xcryptographic_ecdsa_brainpool_p512r1_sign_hash(
                        key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
                        &signatureLen, deterministic)) return empty;
                return XByteArrayView_create_data(buffer, (int64_t)signatureLen);
            }
        case XCryptographic_KeyType_EcdsaBrainpoolP512r1Public:
            return empty;
        case XCryptographic_KeyType_EcdsaSecp256k1Private:
            {
                size_t signatureLen = 0;
                if (!xcryptographic_ecdsa_secp256k1_sign_hash(
                        key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
                        &signatureLen, deterministic)) return empty;
                return XByteArrayView_create_data(buffer, (int64_t)signatureLen);
            }
        case XCryptographic_KeyType_EcdsaSecp256k1Public:
            return empty;
        default:
            return empty;
    }
}

XByteArray* XCryptographic_ecdsaSignHash(
    XCryptographic_Key key, XByteArrayView hash, bool deterministic)
{
    size_t signatureSize = xcryptographic_ecdsa_signature_size(key.type);
    XByteArray* result;
    XByteArrayView view;
    if (signatureSize == 0) return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base(result, signatureSize)) {
        if (result) XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    view = XCryptographic_ecdsaSignHashInto(
        (char*)XByteArray_data(result), signatureSize, key, hash, deterministic);
    if (!view.m_data) {
        XByteArray_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

bool XCryptographic_ecdsaVerifyHash(
    XCryptographic_Key key, XByteArrayView hash, XByteArrayView signature)
{
    if (hash.m_size < 0 || signature.m_size < 0) return false;
    switch (key.type) {
        case XCryptographic_KeyType_EcdsaNistP256Private:
        case XCryptographic_KeyType_EcdsaNistP256Public:
            return xcryptographic_ecdsa_p256_verify_hash(
                key, hash.m_data, (size_t)hash.m_size,
                signature.m_data, (size_t)signature.m_size);
        case XCryptographic_KeyType_EcdsaNistP384Private:
        case XCryptographic_KeyType_EcdsaNistP384Public:
            return xcryptographic_ecdsa_p384_verify_hash(
                key, hash.m_data, (size_t)hash.m_size,
                signature.m_data, (size_t)signature.m_size);
        case XCryptographic_KeyType_EcdsaNistP521Private:
        case XCryptographic_KeyType_EcdsaNistP521Public:
            return xcryptographic_ecdsa_p521_verify_hash(
                key, hash.m_data, (size_t)hash.m_size,
                signature.m_data, (size_t)signature.m_size);
        case XCryptographic_KeyType_EcdsaBrainpoolP256r1Private:
        case XCryptographic_KeyType_EcdsaBrainpoolP256r1Public:
            return xcryptographic_ecdsa_brainpool_p256r1_verify_hash(
                key, hash.m_data, (size_t)hash.m_size,
                signature.m_data, (size_t)signature.m_size);
        case XCryptographic_KeyType_EcdsaBrainpoolP384r1Private:
        case XCryptographic_KeyType_EcdsaBrainpoolP384r1Public:
            return xcryptographic_ecdsa_brainpool_p384r1_verify_hash(
                key, hash.m_data, (size_t)hash.m_size,
                signature.m_data, (size_t)signature.m_size);
        case XCryptographic_KeyType_EcdsaBrainpoolP512r1Private:
        case XCryptographic_KeyType_EcdsaBrainpoolP512r1Public:
            return xcryptographic_ecdsa_brainpool_p512r1_verify_hash(
                key, hash.m_data, (size_t)hash.m_size,
                signature.m_data, (size_t)signature.m_size);
        case XCryptographic_KeyType_EcdsaSecp256k1Private:
        case XCryptographic_KeyType_EcdsaSecp256k1Public:
            return xcryptographic_ecdsa_secp256k1_verify_hash(
                key, hash.m_data, (size_t)hash.m_size,
                signature.m_data, (size_t)signature.m_size);
        default:
            return false;
    }
}

/* =============== RSA / 大整数扩展（XCRYPTOGRAPHIC_RSA_ON） =============== */
#if XCRYPTOGRAPHIC_RSA_ON

/* 大整数辅助：确保 dst 容量足够并赋值。 */
static bool xcbig_assign(XCryptographic_BigInt *dst, const XCryptographic_BigInt *src)
{
    if (!dst || !src) return false;
    if (dst->cap < src->n) {
        uint32_t *nd = (uint32_t *)XMalloc_System(src->n * sizeof(uint32_t));
        if (!nd) return false;
        if (dst->d) {
            memset(dst->d, 0, dst->cap * sizeof(uint32_t));
            XFree_System(dst->d);
        }
        dst->d = nd;
        dst->cap = src->n;
    }
    memset(dst->d, 0, dst->cap * sizeof(uint32_t));
    memcpy(dst->d, src->d, src->n * sizeof(uint32_t));
    dst->n = src->n;
    return true;
}

static bool xcbig_add_bool(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                           const XCryptographic_BigInt *b)
{
    size_t i, n = a->n > b->n ? a->n : b->n;
    uint64_t carry = 0;
    if (!out || !a || !b) return false;
    if (n + 1 > out->cap) {
        uint32_t *nd = (uint32_t *)XMalloc_System((n + 1) * sizeof(uint32_t));
        if (!nd) return false;
        if (out->d) {
            memset(out->d, 0, out->cap * sizeof(uint32_t));
            XFree_System(out->d);
        }
        out->d = nd;
        out->cap = n + 1;
    }
    for (i = 0; i < n; ++i) {
        uint32_t ai = i < a->n ? a->d[i] : 0;
        uint32_t bi = i < b->n ? b->d[i] : 0;
        uint64_t sum = (uint64_t)ai + bi + carry;
        out->d[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    out->d[n] = (uint32_t)carry;
    out->n = n + 1;
    xcbig_trim(out);
    return true;
}

static bool xcbig_sub_bool(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                           const XCryptographic_BigInt *b)
{
    size_t i, n = a->n > b->n ? a->n : b->n;
    uint64_t borrow = 0;
    if (!out || !a || !b) return false;
    if (n > out->cap) {
        uint32_t *nd = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
        if (!nd) return false;
        if (out->d) {
            memset(out->d, 0, out->cap * sizeof(uint32_t));
            XFree_System(out->d);
        }
        out->d = nd;
        out->cap = n;
    }
    for (i = 0; i < n; ++i) {
        uint32_t ai = i < a->n ? a->d[i] : 0;
        uint64_t bi = (uint64_t)(i < b->n ? b->d[i] : 0) + borrow;
        out->d[i] = (uint32_t)((uint64_t)ai - bi);
        borrow = ((uint64_t)ai < bi) ? 1u : 0u;
    }
    out->n = n;
    xcbig_trim(out);
    return true;
}

/* 普通乘法：out = a * b. */
static bool xcbig_mul(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                      const XCryptographic_BigInt *b)
{
    size_t i, j, n;
    uint64_t carry;
    uint32_t *tmp;
    if (!out || !a || !b || a->n == 0 || b->n == 0) return false;
    n = a->n + b->n;
    tmp = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
    if (!tmp) return false;
    memset(tmp, 0, n * sizeof(uint32_t));
    for (i = 0; i < a->n; ++i) {
        carry = 0;
        for (j = 0; j < b->n; ++j) {
            uint64_t prod = (uint64_t)a->d[i] * b->d[j] + tmp[i + j] + carry;
            tmp[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        j = i + b->n;
        while (carry) {
            uint64_t v = (uint64_t)tmp[j] + carry;
            tmp[j] = (uint32_t)v;
            carry = v >> 32;
            ++j;
        }
    }
    if (out->cap < n) {
        uint32_t *nd = (uint32_t *)XMalloc_System(n * sizeof(uint32_t));
        if (!nd) {
            memset(tmp, 0, n * sizeof(uint32_t));
            XFree_System(tmp);
            return false;
        }
        if (out->d) {
            memset(out->d, 0, out->cap * sizeof(uint32_t));
            XFree_System(out->d);
        }
        out->d = nd;
        out->cap = n;
    }
    memset(out->d, 0, out->cap * sizeof(uint32_t));
    memcpy(out->d, tmp, n * sizeof(uint32_t));
    out->n = n;
    xcbig_trim(out);
    memset(tmp, 0, n * sizeof(uint32_t));
    XFree_System(tmp);
    return true;
}

/* 大整数除法：quot = a / b, rem = a % b（b != 0）。 */
static bool xcbig_divmod(XCryptographic_BigInt *quot, XCryptographic_BigInt *rem,
                         const XCryptographic_BigInt *a, const XCryptographic_BigInt *b)
{
    size_t abits = a->n * 32;
    size_t i;
    XCryptographic_BigInt r, q;
    bool ok = false;
    if (!quot || !rem || !a || !b || b->n == 0 ||
        (b->n == 1 && b->d[0] == 0)) return false;
    if (!xcbig_alloc(&r, a->n + 1) || !xcbig_alloc(&q, a->n + 1)) goto cleanup;
    xcbig_zero(&r);
    xcbig_zero(&q);
    for (i = abits; i > 0; --i) {
        size_t limb = (i - 1) / 32;
        size_t bit = (i - 1) % 32;
        size_t j;
        uint32_t carry = 0;
        for (j = 0; j < r.cap; ++j) {
            uint32_t old = r.d[j];
            r.d[j] = (old << 1) | carry;
            carry = old >> 31;
        }
        r.n = r.cap;
        xcbig_trim(&r);
        r.d[0] |= (a->d[limb] >> bit) & 1u;
        if (r.n == 0) r.n = 1;
        if (xcbig_cmp(&r, b) >= 0) {
            xcbig_sub(&r, &r, b);
            q.d[limb] |= (1u << bit);
        }
    }
    q.n = q.cap;
    xcbig_trim(&q);
    r.n = r.cap;
    xcbig_trim(&r);
    if (!xcbig_assign(quot, &q) || !xcbig_assign(rem, &r)) goto cleanup;
    ok = true;
cleanup:
    xcbig_free(&r);
    xcbig_free(&q);
    return ok;
}

static bool xcbig_mod(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                      const XCryptographic_BigInt *m)
{
    XCryptographic_BigInt q, r;
    bool ok = false;
    if (!out || !a || !m || m->n == 0) return false;
    if (!xcbig_alloc(&q, a->n + 1) || !xcbig_alloc(&r, a->n + 1)) goto cleanup;
    if (!xcbig_divmod(&q, &r, a, m)) goto cleanup;
    if (!xcbig_assign(out, &r)) goto cleanup;
    ok = true;
cleanup:
    xcbig_free(&q);
    xcbig_free(&r);
    return ok;
}

static bool xcbig_gcd(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                      const XCryptographic_BigInt *b)
{
    XCryptographic_BigInt x, y, t, r;
    bool ok = false;
    if (!out || !a || !b) return false;
    if (!xcbig_alloc(&x, a->n + 1) || !xcbig_alloc(&y, b->n + 1) ||
        !xcbig_alloc(&t, a->n + b->n + 1) || !xcbig_alloc(&r, b->n + 1))
        goto cleanup;
    if (!xcbig_assign(&x, a) || !xcbig_assign(&y, b)) goto cleanup;
    while (y.n != 0) {
        if (!xcbig_divmod(&t, &r, &x, &y)) goto cleanup;
        if (!xcbig_assign(&x, &y) || !xcbig_assign(&y, &r)) goto cleanup;
    }
    if (!xcbig_assign(out, &x)) goto cleanup;
    ok = true;
cleanup:
    xcbig_free(&x);
    xcbig_free(&y);
    xcbig_free(&t);
    xcbig_free(&r);
    return ok;
}

/* 带符号大整数（扩展欧几里得/模逆用）。 */
typedef struct {
    XCryptographic_BigInt mag;
    bool neg;
} xcbig_signed_t;

static void xcbig_signed_free(xcbig_signed_t *s) { if (s) xcbig_free(&s->mag); }

static bool xcbig_signed_alloc(xcbig_signed_t *s, size_t cap)
{
    if (!s) return false;
    if (!xcbig_alloc(&s->mag, cap)) return false;
    s->neg = false;
    return true;
}

static bool xcbig_signed_assign(xcbig_signed_t *dst, const xcbig_signed_t *src)
{
    if (!dst || !src) return false;
    if (!xcbig_assign(&dst->mag, &src->mag)) return false;
    dst->neg = src->neg;
    return true;
}

static void xcbig_signed_zero(xcbig_signed_t *s)
{
    if (s) {
        xcbig_zero(&s->mag);
        s->neg = false;
    }
}

static bool xcbig_signed_from_big(xcbig_signed_t *s, const XCryptographic_BigInt *v)
{
    if (!s || !v) return false;
    if (!xcbig_assign(&s->mag, v)) return false;
    s->neg = false;
    return true;
}

static bool xcbig_signed_is_zero(const xcbig_signed_t *s) { return s && s->mag.n == 0; }

static bool xcbig_signed_add(xcbig_signed_t *out, const xcbig_signed_t *a,
                             const xcbig_signed_t *b)
{
    bool neg;
    if (!out || !a || !b) return false;
    if (a->neg == b->neg) {
        if (!xcbig_add_bool(&out->mag, &a->mag, &b->mag)) return false;
        neg = a->neg;
    } else {
        int c = xcbig_cmp(&a->mag, &b->mag);
        if (c >= 0) {
            if (!xcbig_sub_bool(&out->mag, &a->mag, &b->mag)) return false;
            neg = a->neg;
        } else {
            if (!xcbig_sub_bool(&out->mag, &b->mag, &a->mag)) return false;
            neg = b->neg;
        }
    }
    /* 零值不携带符号，避免后续符号判定错误。 */
    out->neg = (out->mag.n != 0) && neg;
    return true;
}

static bool xcbig_signed_sub(xcbig_signed_t *out, const xcbig_signed_t *a,
                             const xcbig_signed_t *b)
{
    xcbig_signed_t nb;
    bool ok;
    if (!out || !a || !b) return false;
    if (!xcbig_signed_alloc(&nb, b->mag.cap)) return false;
    if (!xcbig_signed_assign(&nb, b)) {
        xcbig_signed_free(&nb);
        return false;
    }
    nb.neg = !nb.neg;
    ok = xcbig_signed_add(out, a, &nb);
    xcbig_signed_free(&nb);
    return ok;
}

/* 模逆：out = a^{-1} mod m（要求 gcd(a,m)=1）。 */
static bool xcbig_mod_inv(XCryptographic_BigInt *out, const XCryptographic_BigInt *a,
                          const XCryptographic_BigInt *m)
{
    xcbig_signed_t t, newt, r, newr, tmp, tmp2;
    bool ok = false;
    if (!out || !a || !m || m->n == 0) return false;
    if (!xcbig_signed_alloc(&t, m->n + 1) || !xcbig_signed_alloc(&newt, a->n + 1) ||
        !xcbig_signed_alloc(&r, m->n + 1) || !xcbig_signed_alloc(&newr, a->n + 1) ||
        !xcbig_signed_alloc(&tmp, a->n + m->n + 1) ||
        !xcbig_signed_alloc(&tmp2, a->n + m->n + 1)) goto cleanup;
    xcbig_signed_zero(&t);
    newt.mag.d[0] = 1;
    newt.mag.n = 1;
    newt.neg = false;
    if (!xcbig_signed_from_big(&r, m) || !xcbig_signed_from_big(&newr, a)) goto cleanup;
    while (!xcbig_signed_is_zero(&newr)) {
        XCryptographic_BigInt quot, rrem;
        bool loop_ok;
        if (!xcbig_alloc(&quot, r.mag.n + 1) || !xcbig_alloc(&rrem, r.mag.n + 1))
            goto cleanup;
        loop_ok = xcbig_divmod(&quot, &rrem, &r.mag, &newr.mag);
        if (!loop_ok) {
            xcbig_free(&quot);
            xcbig_free(&rrem);
            goto cleanup;
        }
        /* (r, newr) = (newr, rrem) */
        if (!xcbig_assign(&r.mag, &newr.mag) || !xcbig_assign(&newr.mag, &rrem)) {
            xcbig_free(&quot);
            xcbig_free(&rrem);
            goto cleanup;
        }
        r.neg = newr.neg;
        newr.neg = false;
        /* tmp = quot * newt */
        if (!xcbig_mul(&tmp.mag, &quot, &newt.mag)) {
            xcbig_free(&quot);
            xcbig_free(&rrem);
            goto cleanup;
        }
        tmp.neg = newt.neg;
        /* tmp2 = t - tmp */
        if (!xcbig_signed_sub(&tmp2, &t, &tmp)) {
            xcbig_free(&quot);
            xcbig_free(&rrem);
            goto cleanup;
        }
        /* (t, newt) = (newt, tmp2) */
        if (!xcbig_signed_assign(&t, &newt) || !xcbig_signed_assign(&newt, &tmp2)) {
            xcbig_free(&quot);
            xcbig_free(&rrem);
            goto cleanup;
        }
        xcbig_free(&quot);
        xcbig_free(&rrem);
    }
    if (r.mag.n == 1 && r.mag.d[0] == 1) {
        if (t.neg) {
            /* t 为负：out = m - |t|（再取模），等价于 t + m。 */
            if (!xcbig_sub_bool(&t.mag, m, &t.mag)) goto cleanup;
            t.neg = false;
        }
        if (xcbig_cmp(&t.mag, m) >= 0) {
            if (!xcbig_mod(&t.mag, &t.mag, m)) goto cleanup;
        }
        if (!xcbig_assign(out, &t.mag)) goto cleanup;
        ok = true;
    }
cleanup:
    xcbig_signed_free(&t);
    xcbig_signed_free(&newt);
    xcbig_signed_free(&r);
    xcbig_signed_free(&newr);
    xcbig_signed_free(&tmp);
    xcbig_signed_free(&tmp2);
    return ok;
}

static size_t xcbig_bitlen(const XCryptographic_BigInt *a)
{
    size_t bits;
    uint32_t top;
    if (!a || a->n == 0) return 0;
    bits = (a->n - 1) * 32;
    top = a->d[a->n - 1];
    while (top) {
        ++bits;
        top >>= 1;
    }
    return bits;
}

static void xcbig_shr1(XCryptographic_BigInt *a)
{
    size_t i;
    uint32_t carry = 0;
    if (!a) return;
    for (i = a->n; i > 0; --i) {
        uint32_t v = a->d[i - 1];
        a->d[i - 1] = (v >> 1) | (carry << 31);
        carry = v & 1u;
    }
    xcbig_trim(a);
}

static void xcbig_from_uint32(XCryptographic_BigInt *out, uint32_t v)
{
    if (!out) return;
    xcbig_zero(out);
    out->d[0] = v;
    out->n = v ? 1 : 0;
    xcbig_trim(out);
}

/* Miller-Rabin 素性测试。 */
static bool xcbig_is_prime(const XCryptographic_BigInt *n, unsigned rounds)
{
    XCryptographic_BigInt one, nm1, d, a, x, y, g, two;
    unsigned s = 0;
    unsigned round;
    bool ok = false;
    if (!n || n->n == 0) return false;
    if (n->n == 1) {
        uint32_t v = n->d[0];
        unsigned i;
        static const uint32_t small_primes[] = {
            2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47,
            53, 59, 61, 67, 71, 73, 79, 83, 89, 97
        };
        if (v < 2) return false;
        for (i = 0; i < sizeof(small_primes) / sizeof(small_primes[0]); ++i) {
            if (v == small_primes[i]) return true;
            if (v % small_primes[i] == 0) return false;
        }
    }
    if ((n->d[0] & 1u) == 0) return false;
    if (!xcbig_alloc(&one, 1) || !xcbig_alloc(&two, 1) ||
        !xcbig_alloc(&nm1, n->n + 1) || !xcbig_alloc(&d, n->n + 1) ||
        !xcbig_alloc(&a, n->n + 1) || !xcbig_alloc(&x, n->n + 1) ||
        !xcbig_alloc(&y, n->n + 1) || !xcbig_alloc(&g, n->n + 1))
        goto cleanup;
    one.d[0] = 1; one.n = 1;
    two.d[0] = 2; two.n = 1;
    if (!xcbig_sub_bool(&nm1, n, &one) || !xcbig_assign(&d, &nm1)) goto cleanup;
    while ((d.d[0] & 1u) == 0) {
        xcbig_shr1(&d);
        ++s;
    }
    for (round = 0; round < rounds; ++round) {
        size_t bytes = (n->n * 4);
        uint8_t *buf = (uint8_t *)XMalloc_System(bytes);
        unsigned r;
        if (!buf) goto cleanup;
        if (!xcryptographic_random(buf, bytes)) {
            memset(buf, 0, bytes);
            XFree_System(buf);
            goto cleanup;
        }
        if (!xcbig_from_be(&a, buf, bytes)) {
            memset(buf, 0, bytes);
            XFree_System(buf);
            goto cleanup;
        }
        memset(buf, 0, bytes);
        XFree_System(buf);
        /* a = 2 + (a mod (n-3))，保证 [2, n-2] */
        if (!xcbig_mod(&a, &a, &nm1)) goto cleanup;
        if (a.n == 0 || (a.n == 1 && a.d[0] < 2)) {
            a.d[0] = 2;
            a.n = 1;
        }
        if (xcbig_cmp(&a, &nm1) >= 0) {
            if (!xcbig_sub_bool(&a, &a, &one)) goto cleanup;
        }
        if (!xcbig_gcd(&g, &a, n)) goto cleanup;
        if (!(g.n == 1 && g.d[0] == 1)) goto cleanup;
        if (!xcbig_mont_exp(&x, &a, &d, n)) goto cleanup;
        if ((x.n == 1 && x.d[0] == 1) || xcbig_cmp(&x, &nm1) == 0) continue;
        for (r = 1; r < s; ++r) {
            if (!xcbig_mont_exp(&y, &x, &two, n)) goto cleanup;
            if (!xcbig_assign(&x, &y)) goto cleanup;
            if (xcbig_cmp(&x, &nm1) == 0) break;
        }
        if (xcbig_cmp(&x, &nm1) != 0) goto cleanup;
    }
    ok = true;
cleanup:
    xcbig_free(&one);
    xcbig_free(&two);
    xcbig_free(&nm1);
    xcbig_free(&d);
    xcbig_free(&a);
    xcbig_free(&x);
    xcbig_free(&y);
    xcbig_free(&g);
    return ok;
}

/* 生成 bits 位素数（最高两位为 1，最低位为 1）。 */
static bool xcbig_gen_prime(XCryptographic_BigInt *out, unsigned bits)
{
    size_t bytes = (bits + 7) / 8;
    unsigned attempt;
    if (!out || bits < 8) return false;
    for (attempt = 0; attempt < 2000; ++attempt) {
        uint8_t *buf = (uint8_t *)XMalloc_System(bytes);
        XCryptographic_BigInt p;
        bool prime;
        if (!buf) return false;
        if (!xcryptographic_random(buf, bytes)) {
            XFree_System(buf);
            return false;
        }
        buf[0] |= (uint8_t)(1u << ((bits - 1) % 8));
        buf[0] |= (uint8_t)(1u << ((bits - 2) % 8));
        buf[bytes - 1] |= 1u;
        if (!xcbig_alloc(&p, (bytes + 3) / 4)) {
            memset(buf, 0, bytes);
            XFree_System(buf);
            return false;
        }
        if (!xcbig_from_be(&p, buf, bytes)) {
            memset(buf, 0, bytes);
            XFree_System(buf);
            xcbig_free(&p);
            return false;
        }
        memset(buf, 0, bytes);
        XFree_System(buf);
        prime = xcbig_is_prime(&p, 40);
        if (prime && xcbig_bitlen(&p) == bits) {
            bool assign_ok = xcbig_assign(out, &p);
            xcbig_free(&p);
            return assign_ok;
        }
        xcbig_free(&p);
    }
    return false;
}

/* =============== DER 编解码（RSA 键） =============== */

static bool xcder_read_tlv(const uint8_t *p, size_t len, size_t *pos, uint8_t *tag,
                           const uint8_t **data, size_t *dlen)
{
    size_t i, n;
    if (!p || !pos || !tag || !data || !dlen) return false;
    if (*pos + 2 > len) return false;
    *tag = p[*pos];
    if ((p[*pos + 1] & 0x80) == 0) {
        *dlen = p[*pos + 1];
        i = *pos + 2;
    } else {
        n = p[*pos + 1] & 0x7f;
        size_t j;
        size_t v = 0;
        if (n == 0 || n > 4) return false;
        if (*pos + 2 + n > len) return false;
        for (j = 0; j < n; ++j) {
            v = (v << 8) | p[*pos + 2 + j];
        }
        *dlen = v;
        i = *pos + 2 + n;
    }
    if (i + *dlen > len) return false;
    *data = p + i;
    *pos = i + *dlen;
    return true;
}

static bool xcder_read_integer(XCryptographic_BigInt *out, const uint8_t *p,
                               size_t len, size_t *pos)
{
    uint8_t tag;
    const uint8_t *data;
    size_t dlen;
    if (!xcder_read_tlv(p, len, pos, &tag, &data, &dlen)) return false;
    if (tag != 0x02 || dlen == 0) return false;
    if (data[0] == 0) {
        ++data;
        --dlen;
    }
    if (dlen == 0) {
        /* 值为 0 的合法 INTEGER */
        if (!out) return false;
        xcbig_zero(out);
        return true;
    }
    return xcbig_from_be(out, data, dlen);
}

static size_t xcbig_byte_len(const XCryptographic_BigInt *a)
{
    size_t bits = xcbig_bitlen(a);
    return (bits + 7) / 8;
}

static size_t xcder_write_uint32(uint8_t *p, size_t pos, uint32_t v)
{
    size_t n = 0;
    uint8_t tmp[4];
    size_t i;
    while (v) {
        tmp[n++] = (uint8_t)(v & 0xff);
        v >>= 8;
    }
    if (n == 0) tmp[n++] = 0;
    for (i = 0; i < n; ++i) p[pos + i] = tmp[n - 1 - i];
    return pos + n;
}

static size_t xcder_length_size(size_t len)
{
    size_t size = 1;
    if (len >= 0x80) {
        size = 1;
        do {
            ++size;
            len >>= 8;
        } while (len != 0);
    }
    return size;
}

static size_t xcder_write_length(uint8_t *p, size_t pos, size_t len)
{
    if (len < 0x80) {
        p[pos++] = (uint8_t)len;
    } else {
        uint8_t tmp[8];
        size_t n = 0;
        size_t v = len;
        while (v) {
            tmp[n++] = (uint8_t)(v & 0xff);
            v >>= 8;
        }
        p[pos++] = (uint8_t)(0x80 | n);
        while (n > 0) {
            p[pos++] = tmp[--n];
        }
    }
    return pos;
}

static size_t xcder_write_integer(uint8_t *p, size_t pos, const XCryptographic_BigInt *v)
{
    size_t blen = xcbig_byte_len(v);
    bool needs_sign_byte;
    if (blen == 0) blen = 1;
    needs_sign_byte = v && v->n != 0 && (xcbig_bitlen(v) % 8u) == 0;
    p[pos++] = 0x02;
    pos = xcder_write_length(p, pos, blen + (needs_sign_byte ? 1 : 0));
    if (needs_sign_byte) p[pos++] = 0x00;
    xcbig_to_be(v, p + pos, blen);
    pos += blen;
    return pos;
}

static size_t xcder_write_sequence_header(uint8_t *p, size_t pos, size_t content_len)
{
    p[pos++] = 0x30;
    return xcder_write_length(p, pos, content_len);
}

static size_t xcrsa_der_integer_preview(const XCryptographic_BigInt *v)
{
    size_t blen = xcbig_byte_len(v);
    size_t content;
    if (blen == 0) blen = 1;
    content = blen + (v && v->n != 0 && (xcbig_bitlen(v) % 8u) == 0 ? 1 : 0);
    return 1 + xcder_length_size(content) + content;
}

/* =============== RSA 键结构 =============== */

typedef struct XCryptographic_RsaKey {
    XCryptographic_BigInt n, e, d, p, q, dp, dq, qinv;
    bool isPrivate;
    size_t keyBytes;
} XCryptographic_RsaKey;

static void xcrsa_key_free(XCryptographic_RsaKey *key)
{
    if (!key) return;
    xcbig_free(&key->n);
    xcbig_free(&key->e);
    xcbig_free(&key->d);
    xcbig_free(&key->p);
    xcbig_free(&key->q);
    xcbig_free(&key->dp);
    xcbig_free(&key->dq);
    xcbig_free(&key->qinv);
    memset(key, 0, sizeof(*key));
}

static bool xcrsa_key_init(XCryptographic_RsaKey *key)
{
    if (!key) return false;
    memset(key, 0, sizeof(*key));
    if (!xcbig_alloc(&key->n, 1) || !xcbig_alloc(&key->e, 1) ||
        !xcbig_alloc(&key->d, 1) || !xcbig_alloc(&key->p, 1) ||
        !xcbig_alloc(&key->q, 1) || !xcbig_alloc(&key->dp, 1) ||
        !xcbig_alloc(&key->dq, 1) || !xcbig_alloc(&key->qinv, 1)) {
        xcrsa_key_free(key);
        return false;
    }
    return true;
}

static bool xcrsa_parse_private_key(XCryptographic_RsaKey *key,
                                    const uint8_t *data, size_t len)
{
    size_t pos = 0;
    size_t i;
    XCryptographic_BigInt *fields[8];
    uint8_t tag;
    const uint8_t *seq;
    size_t seqlen;
    if (!key || !data || len == 0) return false;
    if (!xcder_read_tlv(data, len, &pos, &tag, &seq, &seqlen)) return false;
    if (tag != 0x30) return false;
    pos = 0;
    /* 重新在 seq 内解析，seq 起始偏移为 (seq - data) */
    {
        size_t inner = (size_t)(seq - data);
        if (inner + seqlen > len) return false;
        pos = inner;
        len = inner + seqlen;
    }
    fields[0] = &key->n;
    fields[1] = &key->e;
    fields[2] = &key->d;
    fields[3] = &key->p;
    fields[4] = &key->q;
    fields[5] = &key->dp;
    fields[6] = &key->dq;
    fields[7] = &key->qinv;
    /* version INTEGER */
    {
        XCryptographic_BigInt version = {0};
        if (!xcbig_alloc(&version, 1)) return false;
        if (!xcder_read_integer(&version, data, len, &pos)) {

            xcbig_free(&version);
            return false;
        }
        xcbig_free(&version);
    }
    for (i = 0; i < 8; ++i) {
        size_t before = pos;
        if (!xcder_read_integer(fields[i], data, len, &pos)) {

            return false;
        }
        /* 跳过 otherPrimeInfos 之后的字段？至少前 8 个整数够用 */
        (void) before;
    }
    key->isPrivate = true;
    key->keyBytes = xcbig_byte_len(&key->n);
    return key->n.n != 0 && key->e.n != 0;
}

static bool xcrsa_parse_public_key(XCryptographic_RsaKey *key,
                                   const uint8_t *data, size_t len)
{
    size_t pos = 0;
    uint8_t tag;
    const uint8_t *seq;
    size_t seqlen;
    if (!key || !data || len == 0) return false;
    if (!xcder_read_tlv(data, len, &pos, &tag, &seq, &seqlen)) return false;
    if (tag != 0x30) return false;
    pos = (size_t)(seq - data);
    len = pos + seqlen;
    if (!xcder_read_integer(&key->n, data, len, &pos)) return false;
    if (!xcder_read_integer(&key->e, data, len, &pos)) return false;
    key->isPrivate = false;
    key->keyBytes = xcbig_byte_len(&key->n);
    return key->n.n != 0 && key->e.n != 0;
}

/* =============== RSA 原始运算 =============== */

static bool xcrsa_modexp(XCryptographic_BigInt *out, const XCryptographic_BigInt *base,
                         const XCryptographic_BigInt *exp,
                         const XCryptographic_BigInt *mod)
{
    return xcbig_mont_exp(out, base, exp, mod);
}

/* CRT 私钥运算：c^d mod n。 */
static bool xcrsa_crt(XCryptographic_BigInt *out, const XCryptographic_BigInt *c,
                      const XCryptographic_RsaKey *key)
{
    XCryptographic_BigInt m1, m2, h, p, q, dp, dq, qinv;
    bool ok = false;
    if (!out || !c || !key || !key->isPrivate) return false;
    if (!xcbig_alloc(&m1, key->p.cap) || !xcbig_alloc(&m2, key->q.cap) ||
        !xcbig_alloc(&h, key->p.cap) || !xcbig_alloc(&p, key->p.cap) ||
        !xcbig_alloc(&q, key->q.cap) || !xcbig_alloc(&dp, key->dp.cap) ||
        !xcbig_alloc(&dq, key->dq.cap) || !xcbig_alloc(&qinv, key->qinv.cap))
        goto cleanup;
    if (!xcbig_assign(&p, &key->p) || !xcbig_assign(&q, &key->q) ||
        !xcbig_assign(&dp, &key->dp) || !xcbig_assign(&dq, &key->dq) ||
        !xcbig_assign(&qinv, &key->qinv))
        goto cleanup;
    if (!xcrsa_modexp(&m1, c, &dp, &p)) goto cleanup;
    if (!xcrsa_modexp(&m2, c, &dq, &q)) goto cleanup;
    /* h = qinv * (m1 - m2) mod p */
    {
        XCryptographic_BigInt diff = {0}, tmp = {0};
        if (!xcbig_alloc(&diff, p.cap + 1) || !xcbig_alloc(&tmp, p.cap + qinv.cap + 1))
            goto cleanup;
        if (xcbig_cmp(&m1, &m2) >= 0) {
            if (!xcbig_sub_bool(&diff, &m1, &m2)) {
                xcbig_free(&diff);
                xcbig_free(&tmp);
                goto cleanup;
            }
        } else {
            /* diff = m1 - m2 + p = p - (m2 - m1) */
            if (!xcbig_sub_bool(&diff, &m2, &m1)) {
                xcbig_free(&diff);
                xcbig_free(&tmp);
                goto cleanup;
            }
            if (!xcbig_sub_bool(&diff, &p, &diff)) {
                xcbig_free(&diff);
                xcbig_free(&tmp);
                goto cleanup;
            }
        }
        if (!xcbig_mod(&diff, &diff, &p)) goto cleanup;
        if (!xcbig_mul(&tmp, &qinv, &diff)) goto cleanup;
        if (!xcbig_mod(&h, &tmp, &p)) goto cleanup;
        xcbig_free(&diff);
        xcbig_free(&tmp);
    }
    /* out = m2 + q*h */
    {
        XCryptographic_BigInt qh = {0};
        if (!xcbig_alloc(&qh, q.cap + h.cap + 1)) goto cleanup;
        if (!xcbig_mul(&qh, &q, &h)) goto cleanup;
        if (!xcbig_add_bool(out, &m2, &qh)) goto cleanup;
        if (!xcbig_mod(out, out, &key->n)) goto cleanup;
        xcbig_free(&qh);
    }
    ok = true;
cleanup:
    xcbig_free(&m1);
    xcbig_free(&m2);
    xcbig_free(&h);
    xcbig_free(&p);
    xcbig_free(&q);
    xcbig_free(&dp);
    xcbig_free(&dq);
    xcbig_free(&qinv);
    return ok;
}

/* =============== 摘要信息前缀与 MGF1 =============== */

static const uint8_t xcrsa_digest_info_md5[] = {
    0x30, 0x20, 0x30, 0x0c, 0x06, 0x08, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05,
    0x05, 0x00, 0x04, 0x10
};
static const uint8_t xcrsa_digest_info_sha1[] = {
    0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14
};
static const uint8_t xcrsa_digest_info_sha224[] = {
    0x30, 0x2d, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
    0x04, 0x05, 0x00, 0x04, 0x1c
};
static const uint8_t xcrsa_digest_info_sha256[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
    0x01, 0x05, 0x00, 0x04, 0x20
};
static const uint8_t xcrsa_digest_info_sha384[] = {
    0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
    0x02, 0x05, 0x00, 0x04, 0x30
};
static const uint8_t xcrsa_digest_info_sha512[] = {
    0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
    0x03, 0x05, 0x00, 0x04, 0x40
};
static const uint8_t xcrsa_digest_info_ripemd160[] = {
    0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x24, 0x03, 0x02, 0x01, 0x05, 0x00, 0x04, 0x14
};

static const uint8_t *xcrsa_digest_info(XCryptographicHash_Algorithm alg, size_t *len)
{
    switch (alg) {
    case XCryptographicHash_Md5:
        *len = sizeof(xcrsa_digest_info_md5);
        return xcrsa_digest_info_md5;
    case XCryptographicHash_Sha1:
        *len = sizeof(xcrsa_digest_info_sha1);
        return xcrsa_digest_info_sha1;
    case XCryptographicHash_Sha224:
        *len = sizeof(xcrsa_digest_info_sha224);
        return xcrsa_digest_info_sha224;
    case XCryptographicHash_Sha256:
        *len = sizeof(xcrsa_digest_info_sha256);
        return xcrsa_digest_info_sha256;
    case XCryptographicHash_Sha384:
        *len = sizeof(xcrsa_digest_info_sha384);
        return xcrsa_digest_info_sha384;
    case XCryptographicHash_Sha512:
        *len = sizeof(xcrsa_digest_info_sha512);
        return xcrsa_digest_info_sha512;
    case XCryptographicHash_Ripemd160:
        *len = sizeof(xcrsa_digest_info_ripemd160);
        return xcrsa_digest_info_ripemd160;
    default:
        *len = 0;
        return NULL;
    }
}

static bool xcmgf1(uint8_t *out, size_t out_len, const uint8_t *seed, size_t seed_len,
                   XCryptographicHash_Algorithm hash_alg)
{
    size_t h_len = (size_t)XCryptographicHash_hashLength(hash_alg);
    size_t done = 0;
    uint32_t counter = 0;
    if (!out || h_len == 0) return false;
    while (done < out_len) {
        uint8_t counter_bytes[4];
        uint8_t *tmp = (uint8_t *)XMalloc_System(h_len);
        XByteArrayView views[2];
        XByteArrayView hv;
        if (!tmp) return false;
        counter_bytes[0] = (uint8_t)(counter >> 24);
        counter_bytes[1] = (uint8_t)(counter >> 16);
        counter_bytes[2] = (uint8_t)(counter >> 8);
        counter_bytes[3] = (uint8_t)(counter & 0xff);
        views[0].m_data = seed;
        views[0].m_size = (int64_t)seed_len;
        views[1].m_data = counter_bytes;
        views[1].m_size = 4;
        hv = XCryptographicHash_hashInto_1((char *)tmp, h_len, views, 2, hash_alg);
        if (!hv.m_data || (size_t)hv.m_size != h_len) {
            memset(tmp, 0, h_len);
            XFree_System(tmp);
            return false;
        }
        if (done + h_len <= out_len) {
            memcpy(out + done, tmp, h_len);
        } else {
            memcpy(out + done, tmp, out_len - done);
        }
        done += h_len;
        memset(tmp, 0, h_len);
        XFree_System(tmp);
        ++counter;
        if (counter == 0) return false; /* 溢出 */
    }
    return true;
}

/* =============== RSA 填充 =============== */

static bool xcrsa_pkcs1_v15_encrypt_pad(uint8_t *em, size_t k, const uint8_t *input,
                                        size_t input_len)
{
    size_t ps_len = k - 3 - input_len;
    size_t i;
    if (!em || k < 11 || input_len > k - 11) return false;
    em[0] = 0x00;
    em[1] = 0x02;
    for (i = 0; i < ps_len; ++i) {
        uint8_t b;
        do {
            if (!xcryptographic_random(&b, 1)) return false;
        } while (b == 0);
        em[2 + i] = b;
    }
    em[2 + ps_len] = 0x00;
    memcpy(em + 3 + ps_len, input, input_len);
    return true;
}

static bool xcrsa_pkcs1_v15_decrypt_unpad(const uint8_t *em, size_t k,
                                          uint8_t *output, size_t output_size,
                                          size_t *output_len)
{
    size_t i;
    size_t msg_start;
    if (!em || k < 11 || em[0] != 0x00 || em[1] != 0x02) return false;
    for (i = 2; i < k; ++i) {
        if (em[i] == 0x00) break;
    }
    if (i >= k - 1) return false; /* 至少一个数据字节 */
    msg_start = i + 1;
    if (k - msg_start > output_size) return false;
    memcpy(output, em + msg_start, k - msg_start);
    *output_len = k - msg_start;
    return true;
}

static bool xcrsa_pkcs1_v15_sign_pad(uint8_t *em, size_t k, XCryptographicHash_Algorithm alg,
                                     const uint8_t *hash, size_t hash_len)
{
    const uint8_t *di;
    size_t di_len;
    size_t ps_len;
    if (!em || k < 11) return false;
    di = xcrsa_digest_info(alg, &di_len);
    if (!di) return false;
    if (di_len + hash_len > k - 11) return false;
    ps_len = k - 3 - di_len - hash_len;
    em[0] = 0x00;
    em[1] = 0x01;
    memset(em + 2, 0xff, ps_len);
    em[2 + ps_len] = 0x00;
    memcpy(em + 3 + ps_len, di, di_len);
    memcpy(em + 3 + ps_len + di_len, hash, hash_len);
    return true;
}

static bool xcrsa_pkcs1_v15_verify_unpad(const uint8_t *em, size_t k,
                                         XCryptographicHash_Algorithm alg,
                                         const uint8_t *hash, size_t hash_len)
{
    const uint8_t *di;
    size_t di_len;
    size_t i;
    size_t di_pos, hash_pos;
    if (!em || k < 11 || em[0] != 0x00 || em[1] != 0x01) return false;
    di = xcrsa_digest_info(alg, &di_len);
    if (!di) return false;
    for (i = 2; i < k; ++i) {
        if (em[i] != 0xff) break;
    }
    if (i < 8 || i >= k || em[i] != 0x00) return false;
    di_pos = i + 1;
    if (di_pos + di_len > k) return false;
    if (memcmp(em + di_pos, di, di_len) != 0) return false;
    hash_pos = di_pos + di_len;
    if (hash_pos + hash_len != k) return false;
    return memcmp(em + hash_pos, hash, hash_len) == 0;
}

static bool xcrsa_oaep_pad(uint8_t *em, size_t k, XCryptographicHash_Algorithm alg,
                           const uint8_t *label, size_t label_len,
                           const uint8_t *input, size_t input_len)
{
    size_t h_len = (size_t)XCryptographicHash_hashLength(alg);
    size_t db_len = k - h_len - 1;
    size_t ps_len = db_len - h_len - 1 - input_len;
    uint8_t *seed;
    uint8_t *db;
    uint8_t *db_mask;
    uint8_t *seed_mask;
    bool ok = false;
    if (!em || k < 2 * h_len + 2 || input_len > db_len - h_len - 1) {  return false; }
    seed = (uint8_t *)XMalloc_System(h_len);
    db = (uint8_t *)XMalloc_System(db_len);
    db_mask = (uint8_t *)XMalloc_System(db_len);
    seed_mask = (uint8_t *)XMalloc_System(h_len);
    if (!seed || !db || !db_mask || !seed_mask) {  goto cleanup; }
    if (!xcryptographic_random(seed, h_len)) {  goto cleanup; }

    memset(em, 0, k);
    /* lHash */
    {
        uint8_t *lh = (uint8_t *)XMalloc_System(h_len);
        if (!lh) {  goto cleanup; }
        if (!XCryptographicHash_hashInto((char *)lh, h_len,
                                          label ? (const char *)label : (const char *)"",
                                          label_len, alg).m_data) {

            memset(lh, 0, h_len);
            XFree_System(lh);
            goto cleanup;
        }
        memcpy(db, lh, h_len);
        memset(lh, 0, h_len);
        XFree_System(lh);
    }

    memset(db + h_len, 0, ps_len);
    db[h_len + ps_len] = 0x01;
    memcpy(db + h_len + ps_len + 1, input, input_len);
    /* dbMask = MGF1(seed, db_len); maskedDB = db XOR dbMask */
    if (!xcmgf1(db_mask, db_len, seed, h_len, alg)) goto cleanup;
    {
        size_t i;
        for (i = 0; i < db_len; ++i) db[i] ^= db_mask[i];
    }
    /* seedMask = MGF1(maskedDB, h_len); maskedSeed = seed XOR seedMask */
    if (!xcmgf1(seed_mask, h_len, db, db_len, alg)) goto cleanup;
    {
        size_t i2;
        for (i2 = 0; i2 < h_len; ++i2) seed_mask[i2] ^= seed[i2];
    }
    /* 0x00 || maskedSeed || maskedDB */
    em[0] = 0x00;
    memcpy(em + 1, seed_mask, h_len);
    memcpy(em + 1 + h_len, db, db_len);
    ok = true;
cleanup:
    if (seed) { memset(seed, 0, h_len); XFree_System(seed); }
    if (db) { memset(db, 0, db_len); XFree_System(db); }
    if (db_mask) { memset(db_mask, 0, db_len); XFree_System(db_mask); }
    if (seed_mask) { memset(seed_mask, 0, h_len); XFree_System(seed_mask); }
    return ok;
}

static bool xcrsa_oaep_unpad(const uint8_t *em, size_t k, XCryptographicHash_Algorithm alg,
                             const uint8_t *label, size_t label_len,
                             uint8_t *output, size_t output_size, size_t *output_len)
{
    size_t h_len = (size_t)XCryptographicHash_hashLength(alg);
    size_t db_len = k - h_len - 1;
    uint8_t *seed_mask;
    uint8_t *db;
    size_t i;
    bool ok = false;
    if (!em || k < 2 * h_len + 2 || em[0] != 0x00) {  return false; }
    seed_mask = (uint8_t *)XMalloc_System(h_len);
    db = (uint8_t *)XMalloc_System(db_len);
    if (!seed_mask || !db) {  goto cleanup; }
    /* seedMask = MGF1(maskedDB, h_len); seed = maskedSeed XOR seedMask */
    if (!xcmgf1(seed_mask, h_len, em + 1 + h_len, db_len, alg)) {  goto cleanup; }
    for (i = 0; i < h_len; ++i) seed_mask[i] ^= em[1 + i];
    /* dbMask = MGF1(seed, db_len); DB = maskedDB XOR dbMask */
    if (!xcmgf1(db, db_len, seed_mask, h_len, alg)) {  goto cleanup; }
    for (i = 0; i < db_len; ++i) db[i] ^= em[1 + h_len + i];
    /* 校验 lHash */
    {
        uint8_t *lh = (uint8_t *)XMalloc_System(h_len);
        if (!lh) {  goto cleanup; }
        if (!XCryptographicHash_hashInto((char *)lh, h_len,
                                          label ? (const char *)label : (const char *)"",
                                          label_len, alg).m_data) {

            memset(lh, 0, h_len);
            XFree_System(lh);
            goto cleanup;
        }
        if (memcmp(lh, db, h_len) != 0) {
            memset(lh, 0, h_len);
            XFree_System(lh);
            goto cleanup;
        }
        memset(lh, 0, h_len);
        XFree_System(lh);
    }

    /* 找 0x01 */
    i = h_len;
    while (i < db_len && db[i] == 0) ++i;
    if (i >= db_len || db[i] != 0x01) {  goto cleanup; }
    ++i;
    if (db_len - i > output_size) goto cleanup;
    memcpy(output, db + i, db_len - i);
    *output_len = db_len - i;
    ok = true;
cleanup:
    if (seed_mask) { memset(seed_mask, 0, h_len); XFree_System(seed_mask); }
    if (db) { memset(db, 0, db_len); XFree_System(db); }
    return ok;
}

static bool xcrsa_pss_encode(uint8_t *em, size_t k, XCryptographicHash_Algorithm alg,
                             const uint8_t *salt, size_t salt_len,
                             const uint8_t *hash, size_t hash_len)
{
    size_t h_len = (size_t)XCryptographicHash_hashLength(alg);
    size_t db_len = k - h_len - 1;
    size_t ps_len = db_len - salt_len - h_len - 1;
    uint8_t *db = NULL;
    uint8_t *db_mask = NULL;
    uint8_t *h2 = NULL;
    bool ok = false;
    if (!em || k < h_len + salt_len + 2 || hash_len != h_len) return false;
    db = (uint8_t *)XMalloc_System(db_len);
    db_mask = (uint8_t *)XMalloc_System(db_len);
    h2 = (uint8_t *)XMalloc_System(h_len);
    if (!db || !db_mask || !h2) goto cleanup;
    memset(db, 0, ps_len);
    db[ps_len] = 0x01;
    memcpy(db + ps_len + 1, salt, salt_len);
    /* H = Hash(0x00 00 00 00 00 00 00 00 || hash || salt) */
    {
        uint8_t *buf = (uint8_t *)XMalloc_System(8 + h_len + salt_len);
        XByteArrayView views[3];
        if (!buf) goto cleanup;
        memset(buf, 0, 8);
        memcpy(buf + 8, hash, h_len);
        memcpy(buf + 8 + h_len, salt, salt_len);
        views[0].m_data = buf;
        views[0].m_size = 8;
        views[1].m_data = hash;
        views[1].m_size = (int64_t)hash_len;
        views[2].m_data = salt;
        views[2].m_size = (int64_t)salt_len;
        if (!XCryptographicHash_hashInto_1((char *)h2, h_len, views, 3, alg).m_data) {
            memset(buf, 0, 8 + h_len + salt_len);
            XFree_System(buf);
            goto cleanup;
        }
        memset(buf, 0, 8 + h_len + salt_len);
        XFree_System(buf);
    }
    /* dbMask = MGF1(H, db_len); maskedDB = DB XOR dbMask */
    if (!xcmgf1(db_mask, db_len, h2, h_len, alg)) goto cleanup;
    {
        size_t i;
        for (i = 0; i < db_len; ++i) db[i] ^= db_mask[i];
    }
    /* 清掉 maskedDB 最高位（用于非整字节模数，模数高位为 0）。 */
    db[0] &= 0x7f;
    /* EM = maskedDB || H || 0xbc */
    memcpy(em, db, db_len);
    memcpy(em + db_len, h2, h_len);
    em[k - 1] = 0xbc;
    ok = true;
cleanup:
    if (db) { memset(db, 0, db_len); XFree_System(db); }
    if (db_mask) { memset(db_mask, 0, db_len); XFree_System(db_mask); }
    if (h2) { memset(h2, 0, h_len); XFree_System(h2); }
    return ok;
}

static bool xcrsa_pss_verify(uint8_t *em, size_t k, XCryptographicHash_Algorithm alg,
                             const uint8_t *salt, size_t salt_len,
                             const uint8_t *hash, size_t hash_len)
{
    size_t h_len = (size_t)XCryptographicHash_hashLength(alg);
    size_t db_len = k - h_len - 1;
    uint8_t *db = NULL;
    uint8_t *db_mask = NULL;
    uint8_t *h2 = NULL;
    bool ok = false;
    if (!em || k < h_len + salt_len + 2 || hash_len != h_len) {  return false; }
    if ((em[0] & 0x80u) != 0 || em[k - 1] != 0xbc) {  return false; }
    db = (uint8_t *)XMalloc_System(db_len);
    db_mask = (uint8_t *)XMalloc_System(db_len);
    h2 = (uint8_t *)XMalloc_System(h_len);
    if (!db || !db_mask || !h2) goto cleanup;
    memcpy(db, em, db_len);
    /* dbMask = MGF1(H, db_len); H 在 em 中位于 db 之后 */
    if (!xcmgf1(db_mask, db_len, em + db_len, h_len, alg)) goto cleanup;
    {
        size_t i;
        for (i = 0; i < db_len; ++i) db[i] ^= db_mask[i];
    }
    db[0] &= 0x7f;
    /* 校验 DB = PS || 0x01 || salt，并从 DB 中提取 salt */
    {
        size_t ps_len = db_len - salt_len - h_len - 1;
        size_t i;
        const uint8_t *salt_in_db = NULL;
        uint8_t *buf = NULL;
        XByteArrayView views[3];
        for (i = 0; i < ps_len; ++i) {
            if (db[i] != 0) {  goto cleanup; }
        }
        if (db[ps_len] != 0x01) {  goto cleanup; }
        salt_in_db = db + ps_len + 1;
        /* H = Hash(8 zero bytes || hash || salt) */
        buf = (uint8_t *)XMalloc_System(8 + h_len + salt_len);
        if (!buf) goto cleanup;
        memset(buf, 0, 8);
        memcpy(buf + 8, hash, h_len);
        memcpy(buf + 8 + h_len, salt_in_db, salt_len);
        views[0].m_data = buf;
        views[0].m_size = 8;
        views[1].m_data = hash;
        views[1].m_size = (int64_t)hash_len;
        views[2].m_data = salt_in_db;
        views[2].m_size = (int64_t)salt_len;
        if (!XCryptographicHash_hashInto_1((char *)h2, h_len, views, 3, alg).m_data) {
            memset(buf, 0, 8 + h_len + salt_len);
            XFree_System(buf);
            goto cleanup;
        }
        memset(buf, 0, 8 + h_len + salt_len);
        XFree_System(buf);
    }
    if (memcmp(h2, em + db_len, h_len) != 0) goto cleanup;
    ok = true;
cleanup:
    if (db) { memset(db, 0, db_len); XFree_System(db); }
    if (db_mask) { memset(db_mask, 0, db_len); XFree_System(db_mask); }
    if (h2) { memset(h2, 0, h_len); XFree_System(h2); }
    return ok;
}

/* =============== 密钥生成 =============== */

static bool XCryptographic_rsaGenerateKeyInternal(XCryptographic_RsaKey *key,
                                                  unsigned bits, uint32_t exponent)
{
    XCryptographic_BigInt p = {0}, q = {0}, n = {0}, phi = {0}, e = {0};
    XCryptographic_BigInt d = {0}, pm1 = {0}, qm1 = {0}, t = {0}, gcd_t = {0}, one = {0};
    XCryptographic_BigInt dp = {0}, dq = {0}, qinv = {0};
    bool ok = false;
    unsigned half = bits / 2;
    unsigned attempt;
    if (!key || bits < 512 || (bits % 8) != 0 || exponent < 3 || (exponent & 1u) == 0)
        return false;
    if (!xcbig_alloc(&p, (half + 31) / 32 + 1) ||
        !xcbig_alloc(&q, (half + 31) / 32 + 1) ||
        !xcbig_alloc(&n, (bits + 31) / 32 + 1) ||
        !xcbig_alloc(&phi, (bits + 31) / 32 + 1) ||
        !xcbig_alloc(&e, 1) || !xcbig_alloc(&d, (bits + 31) / 32 + 1) ||
        !xcbig_alloc(&pm1, (half + 31) / 32 + 1) ||
        !xcbig_alloc(&qm1, (half + 31) / 32 + 1) ||
        !xcbig_alloc(&t, (bits + 31) / 32 + 1) ||
        !xcbig_alloc(&gcd_t, (bits + 31) / 32 + 1) ||
        !xcbig_alloc(&one, 1) || !xcbig_alloc(&dp, (half + 31) / 32 + 1) ||
        !xcbig_alloc(&dq, (half + 31) / 32 + 1) ||
        !xcbig_alloc(&qinv, (half + 31) / 32 + 1))
        goto cleanup;
    one.d[0] = 1;
    one.n = 1;
    xcbig_from_uint32(&e, exponent);
    for (attempt = 0; attempt < 64; ++attempt) {
        if (!xcbig_gen_prime(&p, half) || !xcbig_gen_prime(&q, half)) continue;
        /* p != q */
        if (xcbig_cmp(&p, &q) == 0) continue;
        if (!xcbig_mul(&n, &p, &q)) goto cleanup;
        if (xcbig_bitlen(&n) != bits) continue;
        if (!xcbig_sub_bool(&pm1, &p, &one) || !xcbig_sub_bool(&qm1, &q, &one)) goto cleanup;
        if (!xcbig_mul(&phi, &pm1, &qm1)) goto cleanup;
        if (!xcbig_gcd(&gcd_t, &e, &phi)) goto cleanup;
        if (!(gcd_t.n == 1 && gcd_t.d[0] == 1)) continue;
        if (!xcbig_mod_inv(&d, &e, &phi)) goto cleanup;
        /* dp = d mod (p-1), dq = d mod (q-1), qinv = q^{-1} mod p */
        if (!xcbig_mod(&dp, &d, &pm1) || !xcbig_mod(&dq, &d, &qm1)) goto cleanup;
        if (!xcbig_mod_inv(&qinv, &q, &p)) continue;
        if (!xcbig_assign(&key->n, &n) || !xcbig_assign(&key->e, &e) ||
            !xcbig_assign(&key->d, &d) || !xcbig_assign(&key->p, &p) ||
            !xcbig_assign(&key->q, &q) || !xcbig_assign(&key->dp, &dp) ||
            !xcbig_assign(&key->dq, &dq) || !xcbig_assign(&key->qinv, &qinv))
            goto cleanup;
        key->isPrivate = true;
        key->keyBytes = xcbig_byte_len(&key->n);
        ok = true;
        break;
    }
cleanup:
    xcbig_free(&p); xcbig_free(&q); xcbig_free(&n); xcbig_free(&phi);
    xcbig_free(&e); xcbig_free(&d); xcbig_free(&pm1); xcbig_free(&qm1);
    xcbig_free(&t); xcbig_free(&gcd_t); xcbig_free(&one);
    xcbig_free(&dp); xcbig_free(&dq); xcbig_free(&qinv);
    return ok;
}

/* =============== RSA 公共 API =============== */

XCryptographic_RsaKey *XCryptographic_rsaImportPrivateKey(XByteArrayView der)
{
    XCryptographic_RsaKey *result = NULL;
    if (der.m_size < 0 || !XCRYPTOGRAPHIC_RSA_ON) return NULL;
    result = (XCryptographic_RsaKey *)XMalloc_System(sizeof(*result));
    if (!result) return NULL;
    memset(result, 0, sizeof(*result));
    if (!xcrsa_key_init(result)) {
        XFree_System(result);
        return NULL;
    }
    if (!xcrsa_parse_private_key(result, der.m_data, (size_t)der.m_size)) {
        xcrsa_key_free(result);
        XFree_System(result);
        return NULL;
    }
    return result;
}

XCryptographic_RsaKey *XCryptographic_rsaImportPublicKey(XByteArrayView der)
{
    XCryptographic_RsaKey *result = NULL;
    if (der.m_size < 0 || !XCRYPTOGRAPHIC_RSA_ON) return NULL;
    result = (XCryptographic_RsaKey *)XMalloc_System(sizeof(*result));
    if (!result) return NULL;
    memset(result, 0, sizeof(*result));
    if (!xcrsa_key_init(result)) {
        XFree_System(result);
        return NULL;
    }
    if (!xcrsa_parse_public_key(result, der.m_data, (size_t)der.m_size)) {
        xcrsa_key_free(result);
        XFree_System(result);
        return NULL;
    }
    return result;
}

void XCryptographic_rsaDestroyKey(XCryptographic_RsaKey *key)
{
    if (key) xcrsa_key_free(key);
}

size_t XCryptographic_rsaKeyBytes(const XCryptographic_RsaKey *key)
{
    return key ? key->keyBytes : 0;
}

bool XCryptographic_rsaIsPrivateKey(const XCryptographic_RsaKey *key)
{
    return key && key->isPrivate;
}

XByteArrayView XCryptographic_rsaExportPublicKeyInto(char *buffer, size_t bufferSize,
                                                     const XCryptographic_RsaKey *key)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = empty;
    size_t content_len, total_len, pos;
    size_t n_encoded, e_encoded;
    uint8_t *tmp;
    if (!buffer || !key || !XCRYPTOGRAPHIC_RSA_ON || key->n.n == 0) return empty;
    n_encoded = xcrsa_der_integer_preview(&key->n);
    e_encoded = xcrsa_der_integer_preview(&key->e);
    content_len = n_encoded + e_encoded;
    total_len = 1 + xcder_length_size(content_len) + content_len;
    if (bufferSize < total_len) return empty;
    tmp = (uint8_t *)XMalloc_System(total_len);
    if (!tmp) return empty;
    pos = xcder_write_sequence_header(tmp, 0, content_len);
    pos = xcder_write_integer(tmp, pos, &key->n);
    pos = xcder_write_integer(tmp, pos, &key->e);
    if (pos != total_len) {
        memset(tmp, 0, total_len);
        XFree_System(tmp);
        return empty;
    }
    memcpy(buffer, tmp, total_len);
    memset(tmp, 0, total_len);
    XFree_System(tmp);
    result.m_data = (const uint8_t *)buffer;
    result.m_size = (int64_t)total_len;
    return result;
}

XByteArrayView XCryptographic_rsaExportPrivateKeyInto(
    char *buffer, size_t bufferSize, const XCryptographic_RsaKey *key)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = empty;
    size_t content_len = 3; /* version INTEGER: 02 01 00 */
    size_t total_len, pos;
    const XCryptographic_BigInt *fields[8];
    uint8_t *tmp;
    int i;
    if (!buffer || !key || !XCRYPTOGRAPHIC_RSA_ON || !key->isPrivate ||
        key->n.n == 0) return empty;
    fields[0] = &key->n;
    fields[1] = &key->e;
    fields[2] = &key->d;
    fields[3] = &key->p;
    fields[4] = &key->q;
    fields[5] = &key->dp;
    fields[6] = &key->dq;
    fields[7] = &key->qinv;
    for (i = 0; i < 8; ++i) {
        size_t fl = xcrsa_der_integer_preview(fields[i]);
        if (fl == 0) return empty;
        content_len += fl;
    }
    total_len = 1 + xcder_length_size(content_len) + content_len;
    if (bufferSize < total_len) return empty;
    tmp = (uint8_t *)XMalloc_System(total_len);
    if (!tmp) return empty;
    pos = xcder_write_sequence_header(tmp, 0, content_len);
    tmp[pos++] = 0x02;
    pos = xcder_write_length(tmp, pos, 1);
    tmp[pos++] = 0x00;
    for (i = 0; i < 8; ++i) pos = xcder_write_integer(tmp, pos, fields[i]);
    if (pos != total_len) {
        memset(tmp, 0, total_len);
        XFree_System(tmp);
        return empty;
    }
    memcpy(buffer, tmp, total_len);
    memset(tmp, 0, total_len);
    XFree_System(tmp);
    result.m_data = (const uint8_t *)buffer;
    result.m_size = (int64_t)total_len;
    return result;
}

XByteArray *XCryptographic_rsaExportPrivateKey(const XCryptographic_RsaKey *key)
{
    XByteArray *result = XByteArray_create();
    XByteArrayView view;
    size_t content_len = 3;
    size_t total_len;
    const XCryptographic_BigInt *fields[8];
    int i;
    if (!result || !key || !XCRYPTOGRAPHIC_RSA_ON || !key->isPrivate ||
        key->n.n == 0) {
        if (result) XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    fields[0] = &key->n;
    fields[1] = &key->e;
    fields[2] = &key->d;
    fields[3] = &key->p;
    fields[4] = &key->q;
    fields[5] = &key->dp;
    fields[6] = &key->dq;
    fields[7] = &key->qinv;
    for (i = 0; i < 8; ++i) {
        size_t fl = xcrsa_der_integer_preview(fields[i]);
        if (fl == 0) {
            XByteArray_delete_base((XClass *)result);
            return NULL;
        }
        content_len += fl;
    }
    total_len = 1 + xcder_length_size(content_len) + content_len;
    if (!XByteArray_resize_base((XVector *)result, total_len)) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    view = XCryptographic_rsaExportPrivateKeyInto((char *)XByteArray_data(result),
                                                  total_len, key);
    if (!view.m_data) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    return result;
}

XCryptographic_RsaKey *XCryptographic_rsaGenerateKey(unsigned bits, uint32_t exponent)
{
    XCryptographic_RsaKey *result = NULL;
    if (!XCRYPTOGRAPHIC_RSA_ON) return NULL;
    result = (XCryptographic_RsaKey *)XMalloc_System(sizeof(*result));
    if (!result) return NULL;
    memset(result, 0, sizeof(*result));
    if (!xcrsa_key_init(result)) {
        XFree_System(result);
        return NULL;
    }
    if (!XCryptographic_rsaGenerateKeyInternal(result, bits, exponent)) {
        xcrsa_key_free(result);
        XFree_System(result);
        return NULL;
    }
    return result;
}

XByteArrayView XCryptographic_rsaEncryptInto(char *buffer, size_t bufferSize,
                                             const XCryptographic_RsaKey *key,
                                             XCryptographic_RsaPadding padding,
                                             XCryptographicHash_Algorithm hash_alg,
                                             XByteArrayView label,
                                             XByteArrayView input)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = empty;
    uint8_t *em = NULL;
    XCryptographic_BigInt m = {0}, c = {0};
    size_t k;
    if (!buffer || !key || !XCRYPTOGRAPHIC_RSA_ON || input.m_size < 0 ||
        key->n.n == 0 || key->e.n == 0) return empty;
    k = key->keyBytes;
    if (k == 0) return empty;
    if (bufferSize < k) return empty;
    em = (uint8_t *)XMalloc_System(k);
    if (!em) return empty;
    if (padding == XCryptographic_RsaPadding_Pkcs1) {
        if (!xcrsa_pkcs1_v15_encrypt_pad(em, k, input.m_data, (size_t)input.m_size))
            goto cleanup;
    } else if (padding == XCryptographic_RsaPadding_Oaep) {

        if (!xcrsa_oaep_pad(em, k, hash_alg,
                            label.m_size > 0 ? label.m_data : NULL,
                            label.m_size > 0 ? (size_t)label.m_size : 0,
                            input.m_data, (size_t)input.m_size))
            goto cleanup;
    } else {
        goto cleanup;
    }
    if (!xcbig_alloc(&m, (k + 3) / 4) || !xcbig_alloc(&c, (k + 3) / 4)) {  goto cleanup; }
    if (!xcbig_from_be(&m, em, k)) {  goto cleanup; }

    if (!xcrsa_modexp(&c, &m, &key->e, &key->n)) {  goto cleanup; }

    xcbig_to_be(&c, (uint8_t *)buffer, k);
    result.m_data = (const uint8_t *)buffer;
    result.m_size = (int64_t)k;
cleanup:
    if (em) { memset(em, 0, k); XFree_System(em); }
    xcbig_free(&m);
    xcbig_free(&c);
    return result;
}

XByteArray *XCryptographic_rsaEncrypt(const XCryptographic_RsaKey *key,
                                      XCryptographic_RsaPadding padding,
                                      XCryptographicHash_Algorithm hash_alg,
                                      XByteArrayView label, XByteArrayView input)
{
    XByteArray *result = XByteArray_create();
    XByteArrayView view;
    size_t k = XCryptographic_rsaKeyBytes(key);
    if (!result || k == 0) {
        if (result) XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    if (!XByteArray_resize_base((XVector *)result, k)) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    view = XCryptographic_rsaEncryptInto((char *)XByteArray_data(result), k, key,
                                         padding, hash_alg, label, input);
    if (!view.m_data) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    return result;
}

XByteArrayView XCryptographic_rsaDecryptInto(char *buffer, size_t bufferSize,
                                             const XCryptographic_RsaKey *key,
                                             XCryptographic_RsaPadding padding,
                                             XCryptographicHash_Algorithm hash_alg,
                                             XByteArrayView label,
                                             XByteArrayView input,
                                             size_t *output_len)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = empty;
    uint8_t *em = NULL;
    XCryptographic_BigInt c = {0}, m = {0};
    size_t k;
    size_t plain_len = 0;
    if (!buffer || !key || !input.m_size || !XCRYPTOGRAPHIC_RSA_ON ||
        !key->isPrivate || input.m_size < 0) return empty;
    k = key->keyBytes;
    if (k == 0 || (size_t)input.m_size != k || bufferSize < k) return empty;
    em = (uint8_t *)XMalloc_System(k);
    if (!em) return empty;
    if (!xcbig_alloc(&c, (k + 3) / 4) || !xcbig_alloc(&m, (k + 3) / 4)) goto cleanup;
    if (!xcbig_from_be(&c, input.m_data, k)) goto cleanup;
    if (!xcrsa_crt(&m, &c, key)) {  goto cleanup; }
    xcbig_to_be(&m, em, k);
    if (padding == XCryptographic_RsaPadding_Pkcs1) {
        if (!xcrsa_pkcs1_v15_decrypt_unpad(em, k, (uint8_t *)buffer, bufferSize,
                                           &plain_len)) {
            goto cleanup;
        }
    } else if (padding == XCryptographic_RsaPadding_Oaep) {

        if (!xcrsa_oaep_unpad(em, k, hash_alg,
                              label.m_size > 0 ? label.m_data : NULL,
                              label.m_size > 0 ? (size_t)label.m_size : 0,
                              (uint8_t *)buffer, bufferSize, &plain_len)) {

            goto cleanup;
        }

    } else {
        goto cleanup;
    }
    if (output_len) *output_len = plain_len;
    result.m_data = (const uint8_t *)buffer;
    result.m_size = (int64_t)plain_len;
cleanup:
    if (em) { memset(em, 0, k); XFree_System(em); }
    xcbig_free(&c);
    xcbig_free(&m);
    return result;
}

XByteArray *XCryptographic_rsaDecrypt(const XCryptographic_RsaKey *key,
                                      XCryptographic_RsaPadding padding,
                                      XCryptographicHash_Algorithm hash_alg,
                                      XByteArrayView label, XByteArrayView input)
{
    XByteArray *result = XByteArray_create();
    XByteArrayView view;
    size_t k = XCryptographic_rsaKeyBytes(key);
    size_t out_len = 0;
    if (!result || k == 0) {
        if (result) XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    if (!XByteArray_resize_base((XVector *)result, k)) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    view = XCryptographic_rsaDecryptInto((char *)XByteArray_data(result), k, key,
                                         padding, hash_alg, label, input, &out_len);
    if (!view.m_data) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    if (!XByteArray_resize_base((XVector *)result, out_len)) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    return result;
}

XByteArrayView XCryptographic_rsaSignHashInto(char *buffer, size_t bufferSize,
                                              const XCryptographic_RsaKey *key,
                                              XCryptographic_RsaPadding padding,
                                              XCryptographicHash_Algorithm hash_alg,
                                              XByteArrayView hash)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView result = empty;
    uint8_t *em = NULL;
    XCryptographic_BigInt m = {0}, s = {0};
    size_t k;
    size_t h_len;
    if (!buffer || !key || !XCRYPTOGRAPHIC_RSA_ON || !key->isPrivate ||
        hash.m_size < 0) return empty;
    k = key->keyBytes;
    h_len = (size_t)XCryptographicHash_hashLength(hash_alg);
    if (k == 0 || (size_t)hash.m_size != h_len || bufferSize < k) return empty;
    em = (uint8_t *)XMalloc_System(k);
    if (!em) return empty;
    if (padding == XCryptographic_RsaPadding_Pkcs1) {
        if (!xcrsa_pkcs1_v15_sign_pad(em, k, hash_alg, hash.m_data, h_len))
            goto cleanup;
    } else if (padding == XCryptographic_RsaPadding_Pss) {
        size_t salt_len = h_len;
        uint8_t *salt = (uint8_t *)XMalloc_System(salt_len);
        if (!salt) goto cleanup;
        if (!xcryptographic_random(salt, salt_len)) {
            memset(salt, 0, salt_len);
            XFree_System(salt);
            goto cleanup;
        }
        if (!xcrsa_pss_encode(em, k, hash_alg, salt, salt_len, hash.m_data, h_len)) {
            memset(salt, 0, salt_len);
            XFree_System(salt);
            goto cleanup;
        }
        memset(salt, 0, salt_len);
        XFree_System(salt);
    } else {
        goto cleanup;
    }
    if (!xcbig_alloc(&m, (k + 3) / 4) || !xcbig_alloc(&s, (k + 3) / 4)) goto cleanup;
    if (!xcbig_from_be(&m, em, k)) goto cleanup;
    if (!xcrsa_crt(&s, &m, key)) goto cleanup;
    xcbig_to_be(&s, (uint8_t *)buffer, k);
    result.m_data = (const uint8_t *)buffer;
    result.m_size = (int64_t)k;
cleanup:
    if (em) { memset(em, 0, k); XFree_System(em); }
    xcbig_free(&m);
    xcbig_free(&s);
    return result;
}

XByteArray *XCryptographic_rsaSignHash(const XCryptographic_RsaKey *key,
                                       XCryptographic_RsaPadding padding,
                                       XCryptographicHash_Algorithm hash_alg,
                                       XByteArrayView hash)
{
    XByteArray *result = XByteArray_create();
    XByteArrayView view;
    size_t k = XCryptographic_rsaKeyBytes(key);
    if (!result || k == 0) {
        if (result) XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    if (!XByteArray_resize_base((XVector *)result, k)) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    view = XCryptographic_rsaSignHashInto((char *)XByteArray_data(result), k, key,
                                          padding, hash_alg, hash);
    if (!view.m_data) {
        XByteArray_delete_base((XClass *)result);
        return NULL;
    }
    return result;
}

bool XCryptographic_rsaVerifyHash(const XCryptographic_RsaKey *key,
                                  XCryptographic_RsaPadding padding,
                                  XCryptographicHash_Algorithm hash_alg,
                                  XByteArrayView hash, XByteArrayView signature)
{

    uint8_t *em = NULL;
    XCryptographic_BigInt s = {0}, m = {0};
    size_t k;
    size_t h_len;
    bool ok = false;
    if (!key || !XCRYPTOGRAPHIC_RSA_ON || hash.m_size < 0 || signature.m_size < 0) {

        return false;
    }
    k = key->keyBytes;
    h_len = (size_t)XCryptographicHash_hashLength(hash_alg);
    if (k == 0 || (size_t)hash.m_size != h_len ||
        (size_t)signature.m_size != k || key->n.n == 0) {

        return false;
    }
    em = (uint8_t *)XMalloc_System(k);
    if (!em) return false;
    if (!xcbig_alloc(&s, (k + 3) / 4) || !xcbig_alloc(&m, (k + 3) / 4)) goto cleanup;
    if (!xcbig_from_be(&s, signature.m_data, k)) {  goto cleanup; }
    if (!xcrsa_modexp(&m, &s, &key->e, &key->n)) {  goto cleanup; }
    xcbig_to_be(&m, em, k);
    if (padding == XCryptographic_RsaPadding_Pkcs1) {
        ok = xcrsa_pkcs1_v15_verify_unpad(em, k, hash_alg, hash.m_data, h_len);
    } else if (padding == XCryptographic_RsaPadding_Pss) {
        size_t salt_len = h_len;
        ok = xcrsa_pss_verify(em, k, hash_alg, NULL, salt_len, hash.m_data, h_len);
    }
cleanup:
    if (em) { memset(em, 0, k); XFree_System(em); }
    xcbig_free(&s);
    xcbig_free(&m);
    return ok;
}

#endif /* XCRYPTOGRAPHIC_RSA_ON */
