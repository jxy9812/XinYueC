#include "XCryptographicHash.h"
#include "XCryptographicHash_xxhash.h"
#include "XByteArray.h"
#include "XIODevice.h"
#include "XMemory.h"
#include "XCryptographic_config.h"
#include <stdint.h>
#include <string.h>
/* ========================= 辅助函数 ========================= */

// 32位循环左移
static inline uint32_t rotl32(uint32_t x, int8_t r)
{
    return (x << r) | (x >> (32 - r));
}

// 32位最终混合函数
static inline uint32_t fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

// 64位循环左移
static inline uint64_t rotl64(uint64_t x, int8_t r)
{
    return (x << r) | (x >> (64 - r));
}

// 64位最终混合函数
static inline uint64_t fmix64(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

// 64位乘法混合
static inline uint64_t shift_mix(uint64_t v)
{
    return v ^ (v >> 47);
}

/* ========================= MurmurHash3 ========================= */

static size_t murmur3_32(const void* key, size_t len, uint32_t seed)
{
    const uint8_t* data = (const uint8_t*)key;
    const int nblocks = len / 4;
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    // 处理块
    const uint32_t* blocks = (const uint32_t*)(data + nblocks * 4);
    for (int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];
        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }

    // 处理剩余字节
    const uint8_t* tail = (const uint8_t*)(data + nblocks * 4);
    uint32_t k1 = 0;
    switch (len & 3) {
    case 3: k1 ^= tail[2] << 16;
    case 2: k1 ^= tail[1] << 8;
    case 1: k1 ^= tail[0];
        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
    }

    // 最终处理
    h1 ^= len;
    h1 = fmix32(h1);
    return h1;
}
/* ========================= MurmurHash3_32 ========================= */
static size_t xcryptographic_hash_murmur3_32(const void* key, size_t len)
{
    return murmur3_32(key, len, 0);
}

/* ========================= DJB2 ========================= */

static size_t xcryptographic_hash_djb2(const void* key, size_t len)
{
    const uint8_t* data = (const uint8_t*)key;
    uint64_t hash = 5381; // DJB2 initial value

    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + data[i]; // hash * 33 + data[i]
    }

    return hash;
}

/* ========================= Pearson ========================= */

// Pearson 哈希的标准256字节置换表
static const uint8_t pearson_table[256] = {
    1, 87, 49, 12, 176, 178, 102, 166, 121, 193, 6, 84, 249, 230, 44, 163,
    14, 197, 213, 181, 161, 85, 218, 80, 64, 239, 24, 226, 236, 142, 38, 200,
    110, 177, 104, 103, 141, 253, 255, 50, 77, 101, 81, 18, 45, 96, 31, 222,
    25, 107, 190, 70, 86, 237, 240, 34, 72, 242, 20, 214, 244, 227, 149, 235,
    97, 234, 57, 22, 60, 250, 82, 175, 208, 5, 127, 199, 111, 62, 135, 248,
    174, 169, 211, 58, 66, 154, 106, 195, 245, 171, 17, 187, 182, 179, 0, 243,
    132, 56, 148, 75, 128, 133, 158, 100, 130, 126, 91, 13, 153, 246, 216, 219,
    119, 68, 223, 78, 83, 88, 201, 99, 122, 11, 92, 32, 136, 114, 52, 10,
    138, 30, 48, 183, 156, 35, 61, 26, 143, 74, 251, 94, 129, 162, 63, 152,
    170, 7, 115, 167, 241, 206, 3, 150, 55, 59, 151, 220, 90, 53, 23, 131,
    125, 173, 15, 238, 79, 95, 89, 16, 105, 137, 225, 224, 217, 160, 37, 123,
    118, 73, 2, 157, 46, 116, 9, 145, 134, 228, 207, 212, 202, 215, 69, 229,
    27, 188, 67, 124, 168, 252, 42, 4, 29, 108, 21, 247, 19, 205, 39, 203,
    233, 40, 186, 147, 198, 192, 155, 33, 164, 191, 98, 204, 165, 180, 117, 76,
    140, 36, 210, 172, 41, 54, 159, 8, 185, 232, 113, 196, 231, 47, 146, 120,
    51, 65, 28, 144, 254, 221, 93, 189, 194, 139, 112, 43, 71, 109, 184, 209
};

static size_t xcryptographic_hash_pearson(const void* key, size_t len)
{
    const uint8_t* data = (const uint8_t*)key;
    uint8_t hash = 0;

    for (size_t i = 0; i < len; i++) {
        hash = pearson_table[hash ^ data[i]];
    }

    return hash;
}
/* ========================= Jenkins Lookup3 ========================= */

#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

#define mix(a,b,c) \
{ \
  a -= c;  a ^= rot(c, 4);  c += b; \
  b -= a;  b ^= rot(a, 6);  a += c; \
  c -= b;  c ^= rot(b, 8);  b += a; \
  a -= c;  a ^= rot(c,16);  c += b; \
  b -= a;  b ^= rot(a,19);  a += c; \
  c -= b;  c ^= rot(b, 4);  b += a; \
}

#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}

static size_t xcryptographic_hash_lookup3(const void* key, size_t length)
{
    uint32_t a, b, c;
    uint32_t initval = 0;
    const uint8_t* k = (const uint8_t*)key;
    const uint8_t* const end = k + length;

    a = b = c = 0xdeadbeef + ((uint32_t)length) + initval;

    while (k + 12 <= end) {
        a += (k[0] + ((uint32_t)k[1] << 8) + ((uint32_t)k[2] << 16) + ((uint32_t)k[3] << 24));
        b += (k[4] + ((uint32_t)k[5] << 8) + ((uint32_t)k[6] << 16) + ((uint32_t)k[7] << 24));
        c += (k[8] + ((uint32_t)k[9] << 8) + ((uint32_t)k[10] << 16) + ((uint32_t)k[11] << 24));
        mix(a, b, c);
        k += 12;
    }

    c += (uint32_t)length;
    switch (end - k) {
    case 11: c += ((uint32_t)k[10] << 24);
    case 10: c += ((uint32_t)k[9] << 16);
    case 9: c += ((uint32_t)k[8] << 8);
    case 8: b += ((uint32_t)k[7] << 24);
    case 7: b += ((uint32_t)k[6] << 16);
    case 6: b += ((uint32_t)k[5] << 8);
    case 5: b += k[4];
    case 4: a += ((uint32_t)k[3] << 24);
    case 3: a += ((uint32_t)k[2] << 16);
    case 2: a += ((uint32_t)k[1] << 8);
    case 1: a += k[0];
        break;
    case 0: return c;
    }

    final(a, b, c);
    return c;
}

/* ========================= FNV-1a_64 ========================= */

static uint64_t xcryptographic_hash_fnv1a_64(const void* key, size_t  len) {
    const uint8_t* data = (const uint8_t*)key;
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV偏移基数
    const uint64_t prime = 0x100000001b3ULL; // FNV素数

    for (uint64_t  i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= prime;
    }

    return hash;
}

/* ========================= SipHash-2-4 ========================= */

// SipRound 函数
static void sipround(uint64_t* v0, uint64_t* v1, uint64_t* v2, uint64_t* v3)
{
    *v0 += *v1;
    *v1 = rotl64(*v1, 13);
    *v1 ^= *v0;
    *v0 = rotl64(*v0, 32);
    *v2 += *v3;
    *v3 = rotl64(*v3, 16);
    *v3 ^= *v2;
    *v0 += *v3;
    *v3 = rotl64(*v3, 21);
    *v3 ^= *v0;
    *v2 += *v1;
    *v1 = rotl64(*v1, 17);
    *v1 ^= *v2;
    *v2 = rotl64(*v2, 32);
}

// SipHash-2-4 64位版本   使用SipHash (需要16字节密钥)uint8_t sip_key[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
size_t siphash24(const void* key, size_t len, const uint8_t* k)
{
    uint64_t v0 = 0x736f6d6570736575ULL;
    uint64_t v1 = 0x646f72616e646f6dULL;
    uint64_t v2 = 0x6c7967656e657261ULL;
    uint64_t v3 = 0x7465646279746573ULL;
    uint64_t k0 = ((uint64_t)k[0]) | ((uint64_t)k[1] << 8) |
        ((uint64_t)k[2] << 16) | ((uint64_t)k[3] << 24) |
        ((uint64_t)k[4] << 32) | ((uint64_t)k[5] << 40) |
        ((uint64_t)k[6] << 48) | ((uint64_t)k[7] << 56);
    uint64_t k1 = ((uint64_t)k[8]) | ((uint64_t)k[9] << 8) |
        ((uint64_t)k[10] << 16) | ((uint64_t)k[11] << 24) |
        ((uint64_t)k[12] << 32) | ((uint64_t)k[13] << 40) |
        ((uint64_t)k[14] << 48) | ((uint64_t)k[15] << 56);
    const uint8_t* m = (const uint8_t*)key;
    uint64_t last = (uint64_t)len << 56;

    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;

    // 处理8字节块
    for (size_t i = 0; i + 8 <= len; i += 8) {
        uint64_t mi = ((uint64_t)m[i]) | ((uint64_t)m[i + 1] << 8) |
            ((uint64_t)m[i + 2] << 16) | ((uint64_t)m[i + 3] << 24) |
            ((uint64_t)m[i + 4] << 32) | ((uint64_t)m[i + 5] << 40) |
            ((uint64_t)m[i + 6] << 48) | ((uint64_t)m[i + 7] << 56);
        v3 ^= mi;
        sipround(&v0, &v1, &v2, &v3);
        sipround(&v0, &v1, &v2, &v3);
        v0 ^= mi;
    }

    // 处理剩余字节
    for (size_t i = 0; i < len % 8; i++) {
        last |= (uint64_t)m[len - (len % 8) + i] << (8 * i);
    }

    v3 ^= last;
    sipround(&v0, &v1, &v2, &v3);
    sipround(&v0, &v1, &v2, &v3);
    v0 ^= last;
    v2 ^= 0xff;

    // 最终四轮压缩
    sipround(&v0, &v1, &v2, &v3);
    sipround(&v0, &v1, &v2, &v3);
    sipround(&v0, &v1, &v2, &v3);
    sipround(&v0, &v1, &v2, &v3);

    return v0 ^ v1 ^ v2 ^ v3;
}

/* ========================= CityHash64 ========================= */

// 处理8字节块
static uint64_t hash_len_16(uint64_t u, uint64_t v, uint64_t mul)
{
    uint64_t a = (u ^ v) * mul;
    a ^= (a >> 47);
    uint64_t b = (v ^ a) * mul;
    b ^= (b >> 47);
    b *= mul;
    return b;
}

// 处理4字节块
static uint64_t hash_len_16_2(uint64_t u, uint64_t v)
{
    return hash_len_16(u, v, 0x9ddfea08eb382d69ULL);
}

// 处理8字节块和剩余4字节
static uint64_t hash_len_24(const uint8_t* s, uint64_t mul)
{
    uint64_t a = ((uint64_t)s[0]) | (((uint64_t)s[1]) << 8) |
        (((uint64_t)s[2]) << 16) | (((uint64_t)s[3]) << 24) |
        (((uint64_t)s[4]) << 32) | (((uint64_t)s[5]) << 40) |
        (((uint64_t)s[6]) << 48) | (((uint64_t)s[7]) << 56);
    uint64_t b = ((uint64_t)s[8]) | (((uint64_t)s[9]) << 8) |
        (((uint64_t)s[10]) << 16) | (((uint64_t)s[11]) << 24) |
        (((uint64_t)s[12]) << 32) | (((uint64_t)s[13]) << 40) |
        (((uint64_t)s[14]) << 48) | (((uint64_t)s[15]) << 56);
    uint64_t c = ((uint64_t)s[16]) | (((uint64_t)s[17]) << 8) |
        (((uint64_t)s[18]) << 16) | (((uint64_t)s[19]) << 24) |
        (((uint64_t)s[20]) << 32) | (((uint64_t)s[21]) << 40) |
        (((uint64_t)s[22]) << 48) | (((uint64_t)s[23]) << 56);

    uint64_t d = b * mul;
    uint64_t e = c * mul;
    uint64_t f = d + e;

    return hash_len_16_2(a, f);
}

// 处理更长数据
static uint64_t hash_len_32(const uint8_t* s, uint64_t mul)
{
    uint64_t a = ((uint64_t)s[0]) | (((uint64_t)s[1]) << 8) |
        (((uint64_t)s[2]) << 16) | (((uint64_t)s[3]) << 24) |
        (((uint64_t)s[4]) << 32) | (((uint64_t)s[5]) << 40) |
        (((uint64_t)s[6]) << 48) | (((uint64_t)s[7]) << 56);
    uint64_t b = ((uint64_t)s[8]) | (((uint64_t)s[9]) << 8) |
        (((uint64_t)s[10]) << 16) | (((uint64_t)s[11]) << 24) |
        (((uint64_t)s[12]) << 32) | (((uint64_t)s[13]) << 40) |
        (((uint64_t)s[14]) << 48) | (((uint64_t)s[15]) << 56);
    uint64_t c = ((uint64_t)s[16]) | (((uint64_t)s[17]) << 8) |
        (((uint64_t)s[18]) << 16) | (((uint64_t)s[19]) << 24) |
        (((uint64_t)s[20]) << 32) | (((uint64_t)s[21]) << 40) |
        (((uint64_t)s[22]) << 48) | (((uint64_t)s[23]) << 56);
    uint64_t d = ((uint64_t)s[24]) | (((uint64_t)s[25]) << 8) |
        (((uint64_t)s[26]) << 16) | (((uint64_t)s[27]) << 24) |
        (((uint64_t)s[28]) << 32) | (((uint64_t)s[29]) << 40) |
        (((uint64_t)s[30]) << 48) | (((uint64_t)s[31]) << 56);

    uint64_t e = b * mul;
    uint64_t f = c * mul;
    uint64_t g = d * mul;
    uint64_t h = a * mul;

    return hash_len_16_2(h + e, g + f);
}

// CityHash64 完整版
static uint64_t xcryptographic_hash_cityhash64(const void* key, size_t  len)
{
    const uint8_t* s = (const uint8_t*)key;
    const uint64_t k0 = 0xc3a5c85c97cb3127ULL;
    const uint64_t k1 = 0xb492b66fbe98f273ULL;
    const uint64_t k2 = 0x9ae16a3b2f90404fULL;

    if (len <= 16) {
        if (len >= 8) {
            uint64_t a = ((uint64_t)s[7] << 56) | ((uint64_t)s[6] << 48) |
                ((uint64_t)s[5] << 40) | ((uint64_t)s[4] << 32) |
                ((uint64_t)s[3] << 24) | ((uint64_t)s[2] << 16) |
                ((uint64_t)s[1] << 8) | s[0];
            uint64_t b = (len >= 16) ?
                (((uint64_t)s[15] << 56) | ((uint64_t)s[14] << 48) |
                    ((uint64_t)s[13] << 40) | ((uint64_t)s[12] << 32) |
                    ((uint64_t)s[11] << 24) | ((uint64_t)s[10] << 16) |
                    ((uint64_t)s[9] << 8) | s[8]) : 0;
            return hash_len_16(a, b, k0);
        }
        else if (len >= 4) {
            uint64_t a = ((uint64_t)s[3] << 24) | ((uint64_t)s[2] << 16) |
                ((uint64_t)s[1] << 8) | s[0];
            return hash_len_16(len + (a << 3), 0, k0);
        }
        else if (len > 0) {
            uint64_t a = s[0];
            uint64_t b = s[len >> 1];
            uint64_t c = s[len - 1];
            uint64_t y = a + (b << 8);
            uint64_t z = len + (c << 2);
            return shift_mix(y * k2 ^ z * k0) * k2;
        }
        return k0;
    }
    else if (len <= 32) {
        if (len <= 24) {
            return hash_len_24(s, k0 + len * k2);
        }
        else {
            uint64_t a = ((uint64_t)s[7] << 56) | ((uint64_t)s[6] << 48) |
                ((uint64_t)s[5] << 40) | ((uint64_t)s[4] << 32) |
                ((uint64_t)s[3] << 24) | ((uint64_t)s[2] << 16) |
                ((uint64_t)s[1] << 8) | s[0];
            uint64_t b = ((uint64_t)s[15] << 56) | ((uint64_t)s[14] << 48) |
                ((uint64_t)s[13] << 40) | ((uint64_t)s[12] << 32) |
                ((uint64_t)s[11] << 24) | ((uint64_t)s[10] << 16) |
                ((uint64_t)s[9] << 8) | s[8];
            uint64_t c = ((uint64_t)s[23] << 56) | ((uint64_t)s[22] << 48) |
                ((uint64_t)s[21] << 40) | ((uint64_t)s[20] << 32) |
                ((uint64_t)s[19] << 24) | ((uint64_t)s[18] << 16) |
                ((uint64_t)s[17] << 8) | s[16];
            uint64_t d = ((uint64_t)s[31] << 56) | ((uint64_t)s[30] << 48) |
                ((uint64_t)s[29] << 40) | ((uint64_t)s[28] << 32) |
                ((uint64_t)s[27] << 24) | ((uint64_t)s[26] << 16) |
                ((uint64_t)s[25] << 8) | s[24];
            return hash_len_16(hash_len_16(a, b, k0) + len,
                hash_len_16(c, d, k1), k2);
        }
    }
    else if (len <= 64) {
        uint64_t mul = k2 + len * 2;
        uint64_t a = ((uint64_t)s[7] << 56) | ((uint64_t)s[6] << 48) |
            ((uint64_t)s[5] << 40) | ((uint64_t)s[4] << 32) |
            ((uint64_t)s[3] << 24) | ((uint64_t)s[2] << 16) |
            ((uint64_t)s[1] << 8) | s[0];
        uint64_t b = ((uint64_t)s[15] << 56) | ((uint64_t)s[14] << 48) |
            ((uint64_t)s[13] << 40) | ((uint64_t)s[12] << 32) |
            ((uint64_t)s[11] << 24) | ((uint64_t)s[10] << 16) |
            ((uint64_t)s[9] << 8) | s[8];
        uint64_t c = ((uint64_t)s[23] << 56) | ((uint64_t)s[22] << 48) |
            ((uint64_t)s[21] << 40) | ((uint64_t)s[20] << 32) |
            ((uint64_t)s[19] << 24) | ((uint64_t)s[18] << 16) |
            ((uint64_t)s[17] << 8) | s[16];
        uint64_t d = ((uint64_t)s[31] << 56) | ((uint64_t)s[30] << 48) |
            ((uint64_t)s[29] << 40) | ((uint64_t)s[28] << 32) |
            ((uint64_t)s[27] << 24) | ((uint64_t)s[26] << 16) |
            ((uint64_t)s[25] << 8) | s[24];
        uint64_t e = ((uint64_t)s[39] << 56) | ((uint64_t)s[38] << 48) |
            ((uint64_t)s[37] << 40) | ((uint64_t)s[36] << 32) |
            ((uint64_t)s[35] << 24) | ((uint64_t)s[34] << 16) |
            ((uint64_t)s[33] << 8) | s[32];
        uint64_t f = ((uint64_t)s[47] << 56) | ((uint64_t)s[46] << 48) |
            ((uint64_t)s[45] << 40) | ((uint64_t)s[44] << 32) |
            ((uint64_t)s[43] << 24) | ((uint64_t)s[42] << 16) |
            ((uint64_t)s[41] << 8) | s[40];
        uint64_t g = ((uint64_t)s[55] << 56) | ((uint64_t)s[54] << 48) |
            ((uint64_t)s[53] << 40) | ((uint64_t)s[52] << 32) |
            ((uint64_t)s[51] << 24) | ((uint64_t)s[50] << 16) |
            ((uint64_t)s[49] << 8) | s[48];
        uint64_t h = ((uint64_t)s[63] << 56) | ((uint64_t)s[62] << 48) |
            ((uint64_t)s[61] << 40) | ((uint64_t)s[60] << 32) |
            ((uint64_t)s[59] << 24) | ((uint64_t)s[58] << 16) |
            ((uint64_t)s[57] << 8) | s[56];

        a += b;
        b += c;
        c += d;
        d += e;
        e += f;
        f += g;
        g += h;
        h += a;

        return hash_len_16(hash_len_16(a, d, mul) + fmix64(len) * k0,
            hash_len_16(g, c, mul) + b, mul);
    }

    // 处理超过64字节的数据
    uint64_t x = ((uint64_t)s[7] << 56) | ((uint64_t)s[6] << 48) |
        ((uint64_t)s[5] << 40) | ((uint64_t)s[4] << 32) |
        ((uint64_t)s[3] << 24) | ((uint64_t)s[2] << 16) |
        ((uint64_t)s[1] << 8) | s[0];
    uint64_t y = ((uint64_t)s[63] << 56) | ((uint64_t)s[62] << 48) |
        ((uint64_t)s[61] << 40) | ((uint64_t)s[60] << 32) |
        ((uint64_t)s[59] << 24) | ((uint64_t)s[58] << 16) |
        ((uint64_t)s[57] << 8) | s[56];
    uint64_t z = len;

    // 处理64字节块
    const uint8_t* p = s;
    const uint8_t* end = s + ((len - 1) & ~63);
    uint64_t mul = k2 + z * 2;

    do {
        uint64_t a0 = ((uint64_t)p[7] << 56) | ((uint64_t)p[6] << 48) |
            ((uint64_t)p[5] << 40) | ((uint64_t)p[4] << 32) |
            ((uint64_t)p[3] << 24) | ((uint64_t)p[2] << 16) |
            ((uint64_t)p[1] << 8) | p[0];
        uint64_t a1 = ((uint64_t)p[15] << 56) | ((uint64_t)p[14] << 48) |
            ((uint64_t)p[13] << 40) | ((uint64_t)p[12] << 32) |
            ((uint64_t)p[11] << 24) | ((uint64_t)p[10] << 16) |
            ((uint64_t)p[9] << 8) | p[8];
        uint64_t a2 = ((uint64_t)p[23] << 56) | ((uint64_t)p[22] << 48) |
            ((uint64_t)p[21] << 40) | ((uint64_t)p[20] << 32) |
            ((uint64_t)p[19] << 24) | ((uint64_t)p[18] << 16) |
            ((uint64_t)p[17] << 8) | p[16];
        uint64_t a3 = ((uint64_t)p[31] << 56) | ((uint64_t)p[30] << 48) |
            ((uint64_t)p[29] << 40) | ((uint64_t)p[28] << 32) |
            ((uint64_t)p[27] << 24) | ((uint64_t)p[26] << 16) |
            ((uint64_t)p[25] << 8) | p[24];
        uint64_t a4 = ((uint64_t)p[39] << 56) | ((uint64_t)p[38] << 48) |
            ((uint64_t)p[37] << 40) | ((uint64_t)p[36] << 32) |
            ((uint64_t)p[35] << 24) | ((uint64_t)p[34] << 16) |
            ((uint64_t)p[33] << 8) | p[32];
        uint64_t a5 = ((uint64_t)p[47] << 56) | ((uint64_t)p[46] << 48) |
            ((uint64_t)p[45] << 40) | ((uint64_t)p[44] << 32) |
            ((uint64_t)p[43] << 24) | ((uint64_t)p[42] << 16) |
            ((uint64_t)p[41] << 8) | p[40];
        uint64_t a6 = ((uint64_t)p[55] << 56) | ((uint64_t)p[54] << 48) |
            ((uint64_t)p[53] << 40) | ((uint64_t)p[52] << 32) |
            ((uint64_t)p[51] << 24) | ((uint64_t)p[50] << 16) |
            ((uint64_t)p[49] << 8) | p[48];
        uint64_t a7 = ((uint64_t)p[63] << 56) | ((uint64_t)p[62] << 48) |
            ((uint64_t)p[61] << 40) | ((uint64_t)p[60] << 32) |
            ((uint64_t)p[59] << 24) | ((uint64_t)p[58] << 16) |
            ((uint64_t)p[57] << 8) | p[56];

        // 混合处理
        x += a0;
        y += a1;
        z += a2;
        a3 *= mul;
        a4 *= mul;
        x = rotl64(x, 26);
        x *= mul;
        y = rotl64(y, 29);
        y *= mul;
        z = rotl64(z, 33);
        z *= mul;

        x += a3;
        y += a4;
        z += a5;
        a6 *= mul;
        a7 *= mul;
        x = rotl64(x, 26);
        x *= mul;
        y = rotl64(y, 29);
        y *= mul;
        z = rotl64(z, 33);
        z *= mul;

        x += a6;
        y += a7;
        z += len;

        // 更新指针
        p += 64;
    } while (p < end);

    // 处理剩余字节
    z += len;
    x = rotl64(x, 26);
    y = rotl64(y, 29);
    x *= 9;
    y *= 9;

    // 处理最后的8字节
    if (len & 32) {
        uint64_t a = ((uint64_t)p[7] << 56) | ((uint64_t)p[6] << 48) |
            ((uint64_t)p[5] << 40) | ((uint64_t)p[4] << 32) |
            ((uint64_t)p[3] << 24) | ((uint64_t)p[2] << 16) |
            ((uint64_t)p[1] << 8) | p[0];
        uint64_t b = ((uint64_t)p[15] << 56) | ((uint64_t)p[14] << 48) |
            ((uint64_t)p[13] << 40) | ((uint64_t)p[12] << 32) |
            ((uint64_t)p[11] << 24) | ((uint64_t)p[10] << 16) |
            ((uint64_t)p[9] << 8) | p[8];
        uint64_t c = ((uint64_t)p[23] << 56) | ((uint64_t)p[22] << 48) |
            ((uint64_t)p[21] << 40) | ((uint64_t)p[20] << 32) |
            ((uint64_t)p[19] << 24) | ((uint64_t)p[18] << 16) |
            ((uint64_t)p[17] << 8) | p[16];
        uint64_t d = ((uint64_t)p[31] << 56) | ((uint64_t)p[30] << 48) |
            ((uint64_t)p[29] << 40) | ((uint64_t)p[28] << 32) |
            ((uint64_t)p[27] << 24) | ((uint64_t)p[26] << 16) |
            ((uint64_t)p[25] << 8) | p[24];

        x += a;
        y += b;
        z += c;
        x = rotl64(x, 13);
        y = rotl64(y, 15);
        x += d;
        y += c * 9;
        z = rotl64(z, 33);
        p += 32;
    }

    if (len & 16) {
        uint64_t a = ((uint64_t)p[7] << 56) | ((uint64_t)p[6] << 48) |
            ((uint64_t)p[5] << 40) | ((uint64_t)p[4] << 32) |
            ((uint64_t)p[3] << 24) | ((uint64_t)p[2] << 16) |
            ((uint64_t)p[1] << 8) | p[0];
        uint64_t b = ((uint64_t)p[15] << 56) | ((uint64_t)p[14] << 48) |
            ((uint64_t)p[13] << 40) | ((uint64_t)p[12] << 32) |
            ((uint64_t)p[11] << 24) | ((uint64_t)p[10] << 16) |
            ((uint64_t)p[9] << 8) | p[8];

        x += a;
        y += b * 9;
        x = rotl64(x, 13);
        y = rotl64(y, 15);
        x += y;
        y += z;
        z += x;
        p += 16;
    }

    if (len & 8) {
        uint64_t a = ((uint64_t)p[7] << 56) | ((uint64_t)p[6] << 48) |
            ((uint64_t)p[5] << 40) | ((uint64_t)p[4] << 32) |
            ((uint64_t)p[3] << 24) | ((uint64_t)p[2] << 16) |
            ((uint64_t)p[1] << 8) | p[0];

        x += a;
        x = rotl64(x, 33);
        x += y;
        y += z;
        z += x;
        p += 8;
    }

    if (len & 4) {
        uint32_t a = ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
            ((uint32_t)p[1] << 8) | p[0];

        x += a;
        x = rotl64(x, 13);
        y += x;
        z += y;
        p += 4;
    }

    if (len & 2) {
        uint16_t a = ((uint16_t)p[1] << 8) | p[0];

        x += a;
        y += x;
        z += y;
        p += 2;
    }

    if (len & 1) {
        x += *p;
        y += x;
        z += y;
    }

    // 最终混合
    x = fmix64(x);
    y = fmix64(y);
    z = fmix64(z);

    x += y;
    y += z;
    z += x;

    return z;
}
/* ========================= FarmHash64 完整实现 ========================= */

static inline uint64_t farmhash_fetch64(const char* p) {
    return *((const uint64_t*)p);
}

static inline uint32_t farmhash_fetch32(const char* p) {
    return *((const uint32_t*)p);
}

static inline uint64_t farmhash_shift_mix(uint64_t val) {
    return val ^ (val >> 47);
}

static inline uint64_t farmhash_rotate64(uint64_t val, int shift) {
    return (val >> shift) | (val << (64 - shift));
}

// Hash 16 bytes from p with mul.
static inline uint64_t farmhash_hash_len_16(uint64_t u, uint64_t v, uint64_t mul) {
    uint64_t a = (u ^ v) * mul;
    a ^= (a >> 47);
    uint64_t b = (v ^ a) * mul;
    b ^= (b >> 47);
    b *= mul;
    return b;
}

// Hash function for <= 16 bytes
static inline uint64_t farmhash_hash_len_0_to_16(const char* s, size_t len) {
    if (len >= 8) {
        uint64_t mul = farmhash_hash_len_16(farmhash_fetch64(s),
            farmhash_fetch64(s + len - 8), 0xc95d38c2f79ca05bULL);
        return farmhash_hash_len_16(mul, (uint64_t)len, 0x9ae16a3b2f90404fULL);
    }
    if (len >= 4) {
        uint64_t mul = farmhash_fetch32(s) + ((uint64_t)farmhash_fetch32(s + len - 4) << 32);
        return farmhash_hash_len_16(mul, (uint64_t)len, 0x9ae16a3b2f90404fULL);
    }
    if (len > 0) {
        uint8_t a = s[0];
        uint8_t b = s[len >> 1];
        uint8_t c = s[len - 1];
        uint32_t y = (uint32_t)a + ((uint32_t)b << 8);
        uint32_t z = len + ((uint32_t)c << 2);
        return farmhash_shift_mix((uint64_t)y * 0xe4aa10ce + (uint64_t)z * 0x9fb21ea6) * 0xe4aa10ce;
    }
    return 0xcbf29ce484222325ULL;
}

// Hash function for 17 to 32 bytes
static inline uint64_t farmhash_hash_len_17_to_32(const char* s, size_t len) {
    uint64_t a = farmhash_fetch64(s) * 0x880355f21e6d1965ULL;
    uint64_t b = farmhash_fetch64(s + 8);
    uint64_t c = farmhash_fetch64(s + len - 8) * 0xe6546b6400000000ULL;
    uint64_t d = farmhash_fetch64(s + len - 16) * 0x9fb21ea600000000ULL;
    return farmhash_hash_len_16(
        farmhash_rotate64(a + b, 43) + farmhash_rotate64(c, 30) + d,
        a + farmhash_rotate64(b + 0x9fb21ea600000000ULL, 18) + c,
        0x880355f21e6d1965ULL);
}

// Hash function for 33 to 64 bytes
static inline uint64_t farmhash_hash_len_33_to_64(const char* s, size_t len) {
    uint64_t mul = 0x6c07896593ed12c5ULL;
    uint64_t a = farmhash_fetch64(s) * mul;
    uint64_t b = farmhash_fetch64(s + 8);
    uint64_t c = farmhash_fetch64(s + len - 8) * mul;
    uint64_t d = farmhash_fetch64(s + len - 16);
    uint64_t u = farmhash_rotate64(a + b, 43) + farmhash_rotate64(c, 30) + d;
    uint64_t v = farmhash_rotate64(a + b + c, 43) + farmhash_rotate64(d, 30) + len;
    uint64_t w = farmhash_hash_len_16(u, v, mul);
    return w;
}

static uint64_t xcryptographic_hash_farmhash64(const void* key, size_t  len) {
    const char* s = (const char*)key;

    if (len <= 32) {
        if (len <= 16) {
            return farmhash_hash_len_0_to_16(s, len);
        }
        else {
            return farmhash_hash_len_17_to_32(s, len);
        }
    }
    else if (len <= 64) {
        return farmhash_hash_len_33_to_64(s, len);
    }

    // For strings longer than 64 bytes
    const size_t seed = 81;
    const char* end = s + len;
    const char* last64 = end - 64;
    uint64_t x = farmhash_fetch64(s);
    uint64_t y = farmhash_fetch64(s + 8);
    uint64_t z = farmhash_fetch64(s + 16);
    uint64_t v = farmhash_fetch64(s + 24);
    uint64_t w = len * seed;
    x = x * seed + y;
    y = farmhash_rotate64(y, 43) + z;
    z = farmhash_rotate64(z, 37) + v;
    v = farmhash_rotate64(v, 31) + w;
    w = farmhash_rotate64(w, 29) + x;

    x = x * seed + farmhash_fetch64(s + 32);
    y = y * seed + farmhash_fetch64(s + 40);
    z = z * seed + farmhash_fetch64(s + 48);
    v = v * seed + farmhash_fetch64(s + 56);

    w += farmhash_fetch64(last64 + 64 - 8);
    v += farmhash_fetch64(last64 + 64 - 16);
    z += farmhash_fetch64(last64 + 64 - 24);
    y += farmhash_fetch64(last64 + 64 - 32);
    x += farmhash_fetch64(last64 + 64 - 40);

    // Additional mixing for long strings
    uint64_t mul = 0x6c07896593ed12c5ULL;
    x = farmhash_hash_len_16(x, v, mul);
    y = farmhash_hash_len_16(y, w, mul);
    z = farmhash_hash_len_16(z + farmhash_fetch64(last64), x, mul);

    // Process 64-byte chunks
    const char* p = s + 64;
    while (p <= last64) {
        x += farmhash_fetch64(p) * mul;
        y += farmhash_fetch64(p + 8);
        z += farmhash_fetch64(p + 16);
        v += farmhash_fetch64(p + 24);
        w += farmhash_fetch64(p + 32);

        x = farmhash_rotate64(x, 43) + y;
        y = farmhash_rotate64(y, 37) + z;
        z = farmhash_rotate64(z, 31) + v;
        v = farmhash_rotate64(v, 29) + w;
        w = farmhash_rotate64(w, 23) + x;

        p += 64;
    }

    return farmhash_hash_len_16(farmhash_hash_len_16(x, y, mul) + z,
        farmhash_hash_len_16(v, w, mul) + len, mul);
}

/* ========================= HighwayHash64 简化完整实现 ========================= */

// Note: Full HighwayHash requires SIMD instructions (AVX2/NEON)
// This is a portable reference implementation

static inline uint64_t highwayhash_rotate64(uint64_t val, int shift) {
    return (val >> shift) | (val << (64 - shift));
}

static inline void highwayhash_permute(uint64_t* v) {
    uint64_t temp = v[1];
    v[1] = v[2];
    v[2] = temp;
}

static inline void highwayhash_update(uint64_t* state, const uint64_t* packet) {
    state[0] += packet[0];
    state[1] += packet[1];
    state[2] += packet[2];
    state[3] += packet[3];

    state[0] += state[3];
    state[3] = highwayhash_rotate64(state[3], 32);
    state[3] *= 0x9e3779b185ebca87ULL;
    state[2] += state[3];
    state[3] = highwayhash_rotate64(state[3], 32);
    state[3] *= 0x9e3779b185ebca87ULL;

    highwayhash_permute(state);
}

static uint64_t xcryptographic_hash_highwayhash64(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    const uint8_t* end = data + len;

    // Initialize state with constants
    uint64_t state[4] = {
        0x243f6a8885a308d3ULL,
        0x13198a2e03707344ULL,
        0xa4093822299f31d0ULL,
        0x082efa98ec4e6c89ULL
    };

    // Process 32-byte chunks
    const uint8_t* p = data;
    while (p + 32 <= end) {
        uint64_t packet[4];
        packet[0] = *((uint64_t*)(p));
        packet[1] = *((uint64_t*)(p + 8));
        packet[2] = *((uint64_t*)(p + 16));
        packet[3] = *((uint64_t*)(p + 24));
        highwayhash_update(state, packet);
        p += 32;
    }

    // Handle remaining bytes
    uint8_t remainder[32] = { 0 };
    size_t remaining = end - p;
    if (remaining > 0) {
        for (size_t i = 0; i < remaining; i++) {
            remainder[i] = p[i];
        }
        // Pad with length
        *((uint64_t*)(remainder + 24)) = len;

        uint64_t packet[4];
        packet[0] = *((uint64_t*)(remainder));
        packet[1] = *((uint64_t*)(remainder + 8));
        packet[2] = *((uint64_t*)(remainder + 16));
        packet[3] = *((uint64_t*)(remainder + 24));
        highwayhash_update(state, packet);
    }
    else {
        // No remaining bytes, just add length
        uint64_t packet[4] = { 0, 0, 0, len };
        highwayhash_update(state, packet);
    }

    // Final mixing
    for (int i = 0; i < 4; i++) {
        highwayhash_update(state, state);
    }

    return state[0] ^ state[1] ^ state[2] ^ state[3];
}

/* ========================= xxHash64 完整实现 ========================= */

static uint64_t xcryptographic_hash_xxhash64(const void* key, size_t len) {
    return XXH64(key, len, 0); // 使用默认seed=0
}

/* ========================= Wyhash 完整实现 ========================= */

static inline uint64_t wyrot(uint64_t x) {
    return (x >> 32) | (x << 32);
}

static inline void wyrand(uint64_t* seed) {
    *seed += 0x2b2e15ed6a7b159dULL;
    *seed = (*seed << 32) | (*seed >> 32);
}

static inline uint64_t wymum(uint64_t A, uint64_t B) {
    uint64_t C = A * B;
    return C ^ wyrot(C);
}

static inline uint64_t wymix(uint64_t A, uint64_t B) {
    return wymum(A ^ 0x3141592653589793ULL, B ^ 0x2718281828459045ULL);
}

static uint64_t xcryptographic_hash_wyhash(const void* key, size_t len) {
    const uint8_t* p = (const uint8_t*)key;
    uint64_t seed = 0; // default seed
    uint64_t a = 0x2b2e15ed6a7b159dULL ^ seed;
    uint64_t b = 0x3141592653589793ULL ^ seed;
    uint64_t c = 0x2718281828459045ULL ^ seed;
    uint64_t d = 0x9e3779b185ebca87ULL ^ seed;

    if (len <= 16) {
        if (len >= 4) {
            a ^= *(uint32_t*)p;
            c ^= *(uint32_t*)(p + len - 4);
        }
        if (len >= 8) {
            b ^= *(uint64_t*)(p + len - 8);
        }
        return wymix(a, b) ^ wymix(c, d);
    }

    if (len <= 32) {
        a ^= *(uint64_t*)p;
        b ^= *(uint64_t*)(p + 8);
        c ^= *(uint64_t*)(p + len - 16);
        d ^= *(uint64_t*)(p + len - 8);
        return wymix(a, b) ^ wymix(c, d);
    }

    if (len <= 64) {
        a ^= *(uint64_t*)p;
        b ^= *(uint64_t*)(p + 8);
        c ^= *(uint64_t*)(p + 16);
        d ^= *(uint64_t*)(p + 24);
        uint64_t aa = *(uint64_t*)(p + len - 32);
        uint64_t bb = *(uint64_t*)(p + len - 24);
        uint64_t cc = *(uint64_t*)(p + len - 16);
        uint64_t dd = *(uint64_t*)(p + len - 8);
        uint64_t h = wymix(a, b) ^ wymix(c, d);
        h = wymix(h, aa) ^ wymix(bb, cc);
        return h ^ dd;
    }

    // For longer inputs
    size_t i = len;
    while (i > 64) {
        a ^= *(uint64_t*)p;
        b ^= *(uint64_t*)(p + 8);
        c ^= *(uint64_t*)(p + 16);
        d ^= *(uint64_t*)(p + 24);
        a = wymix(a, b);
        c = wymix(c, d);
        p += 32;
        i -= 32;
    }

    uint64_t h = wymix(a, b) ^ wymix(c, d);
    h ^= *(uint64_t*)(p + i - 32);
    h = wymix(h, *(uint64_t*)(p + i - 24));
    h ^= *(uint64_t*)(p + i - 16);
    h = wymix(h, *(uint64_t*)(p + i - 8));

    return h;
}
/* ========================= t1ha2 完整实现 ========================= */

static inline uint64_t t1ha2_le_xor_fold(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t hi = 0, lo = 0;

    if (len >= 16) {
        do {
            lo ^= *(uint64_t*)p;
            hi ^= *(uint64_t*)(p + 8);
            p += 16;
            len -= 16;
        } while (len >= 16);
    }

    if (len & 8) {
        lo ^= *(uint64_t*)p;
        p += 8;
    }

    if (len & 4) {
        lo ^= *(uint32_t*)p;
        p += 4;
    }

    if (len & 2) {
        lo ^= *(uint16_t*)p;
        p += 2;
    }

    if (len & 1) {
        lo ^= *p;
    }

    return lo ^ hi;
}

static inline uint64_t t1ha2_rotl64(uint64_t value, unsigned shift) {
    return (value << shift) | (value >> (64 - shift));
}

static inline uint64_t t1ha2_mul_64(uint64_t a, uint64_t b) {
    return a * b;
}

static uint64_t xcryptographic_hash_t1ha2(const void* key, size_t len) {
    const uint8_t* p = (const uint8_t*)key;
    uint64_t a = 0x94D049BB133111EBULL;
    uint64_t b = 0xBF58476D1CE4E5B9ULL;
    uint64_t c = 0xC4CEB9FE1A85EC53ULL;
    uint64_t d = 0x27D4EB2F165667C5ULL;

    if (len >= 32) {
        const uint8_t* const end = p + len - 31;
        do {
            a += t1ha2_mul_64(*(uint64_t*)p, b);
            b += t1ha2_mul_64(*(uint64_t*)(p + 8), c);
            c += t1ha2_mul_64(*(uint64_t*)(p + 16), d);
            d += t1ha2_mul_64(*(uint64_t*)(p + 24), a);

            a = t1ha2_rotl64(a, 23) + c;
            b = t1ha2_rotl64(b, 23) + d;
            c = t1ha2_rotl64(c, 23) + a;
            d = t1ha2_rotl64(d, 23) + b;

            p += 32;
        } while (p < end);
    }

    uint64_t folded = t1ha2_le_xor_fold(p, len - (p - (const uint8_t*)key));
    a += folded;
    b += len;

    a ^= t1ha2_rotl64(b, 23);
    a *= c;
    a ^= t1ha2_rotl64(a, 23);
    a *= d;
    a ^= t1ha2_rotl64(a, 23);

    return a;
}
/* ========================= SpookyHash64 完整实现 ========================= */

static inline uint64_t spooky_rot64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static void spooky_short(const void* message, size_t length, uint64_t* hash1, uint64_t* hash2) {
    const uint8_t* data = (const uint8_t*)message;
    uint64_t hash = *hash1;

    if (length >= 4) {
        hash ^= (uint64_t) * (uint32_t*)data;
        hash *= 0x85ebca6b;
        hash ^= hash >> 16;
        hash *= 0xc2b2ae35;
        hash ^= hash >> 16;
        data += 4;
        length -= 4;
    }

    if (length >= 4) {
        hash ^= (uint64_t) * (uint32_t*)data << 32;
        hash *= 0x85ebca6b;
        hash ^= hash >> 16;
        hash *= 0xc2b2ae35;
        hash ^= hash >> 16;
        data += 4;
        length -= 4;
    }

    if (length >= 2) {
        hash ^= (uint64_t) * (uint16_t*)data << 48;
        hash *= 0x85ebca6b;
        hash ^= hash >> 16;
        hash *= 0xc2b2ae35;
        hash ^= hash >> 16;
        data += 2;
        length -= 2;
    }

    if (length >= 1) {
        hash ^= (uint64_t)*data << 56;
        hash *= 0x85ebca6b;
        hash ^= hash >> 16;
        hash *= 0xc2b2ae35;
        hash ^= hash >> 16;
    }

    *hash1 = hash;
    *hash2 = hash;
}

static void spooky_end_partial(uint64_t* state, uint64_t data, int lane, int length) {
    uint64_t temp = 0;
    switch (length) {
    case 7: temp ^= (uint64_t)((data >> 48) & 0xFF) << 56;
    case 6: temp ^= (uint64_t)((data >> 40) & 0xFF) << 48;
    case 5: temp ^= (uint64_t)((data >> 32) & 0xFF) << 40;
    case 4: temp ^= (uint64_t)((data >> 24) & 0xFF) << 32;
    case 3: temp ^= (uint64_t)((data >> 16) & 0xFF) << 24;
    case 2: temp ^= (uint64_t)((data >> 8) & 0xFF) << 16;
    case 1: temp ^= (uint64_t)(data & 0xFF) << 8;
    }
    state[lane] ^= temp;
}

static void spooky_end(const void* message, size_t length, uint64_t* state) {
    const uint8_t* data = (const uint8_t*)message;
    size_t remaining = length % 32;
    const uint8_t* end = data + length - remaining;

    if (remaining >= 16) {
        state[4] ^= *(uint64_t*)(end + 0);
        state[5] ^= *(uint64_t*)(end + 8);
        remaining -= 16;
        end += 16;
    }

    if (remaining >= 8) {
        state[6] ^= *(uint64_t*)end;
        remaining -= 8;
        end += 8;
    }

    if (remaining >= 4) {
        state[7] ^= (uint64_t) * (uint32_t*)end;
        remaining -= 4;
        end += 4;
    }

    if (remaining >= 2) {
        state[8] ^= (uint64_t) * (uint16_t*)end;
        remaining -= 2;
        end += 2;
    }

    if (remaining >= 1) {
        state[9] ^= (uint64_t)*end;
    }
}

static void spooky_mix(uint64_t* state) {
    state[0] += state[10]; state[11] ^= state[1]; state[1] = spooky_rot64(state[1], 52); state[1] ^= state[2];
    state[2] = spooky_rot64(state[2], 29); state[2] += state[3]; state[3] = spooky_rot64(state[3], 43); state[3] ^= state[4];
    state[4] = spooky_rot64(state[4], 52); state[4] += state[5]; state[5] = spooky_rot64(state[5], 29); state[5] ^= state[6];
    state[6] = spooky_rot64(state[6], 43); state[6] += state[7]; state[7] = spooky_rot64(state[7], 52); state[7] ^= state[8];
    state[8] = spooky_rot64(state[8], 29); state[8] += state[9]; state[9] = spooky_rot64(state[9], 43); state[9] ^= state[10];
    state[10] = spooky_rot64(state[10], 52); state[10] += state[11]; state[11] = spooky_rot64(state[11], 29); state[11] ^= state[0];
}

static void spooky_end_spooky(uint64_t* state, size_t length) {
    state[11] ^= length;
    spooky_mix(state);
    spooky_mix(state);
    spooky_mix(state);
    spooky_mix(state);
    spooky_mix(state);
    spooky_mix(state);
}

static uint64_t xcryptographic_hash_spookyhash64(const void* key, size_t len) {
    if (len < 16) {
        uint64_t hash1 = 0, hash2 = 0;
        spooky_short(key, len, &hash1, &hash2);
        return hash1;
    }

    uint64_t state[12] = {
        0x1ac87a4d5e8a1f1aULL, 0x2b9f3c6e4d7b2e2bULL, 0x3c6e4d7b2e2b1ac8ULL, 0x4d7b2e2b1ac87a4dULL,
        0x5e8a1f1a2b9f3c6eULL, 0x6f1a2b9f3c6e4d7bULL, 0x7a4d5e8a1f1a2b9fULL, 0x8b2e2b1ac87a4d5eULL,
        0x9c6e4d7b2e2b1ac8ULL, 0xad7b2e2b1ac87a4dULL, 0xbe8a1f1a2b9f3c6eULL, 0xcf1a2b9f3c6e4d7bULL
    };

    const uint8_t* data = (const uint8_t*)key;
    size_t blocks = len / 32;
    const uint8_t* end = data + blocks * 32;

    for (const uint8_t* p = data; p < end; p += 32) {
        state[0] ^= *(uint64_t*)(p + 0);
        state[1] ^= *(uint64_t*)(p + 8);
        state[2] ^= *(uint64_t*)(p + 16);
        state[3] ^= *(uint64_t*)(p + 24);
        spooky_mix(state);
        spooky_mix(state);
        spooky_mix(state);
    }

    spooky_end(key, len, state);
    spooky_end_spooky(state, len);

    return state[0];
}

/* ========================= SipHash-2-4 Implementation ========================= */

// 默认密钥（在实际应用中应该使用随机密钥）
//static const uint64_t siphash_k0 = 0x0706050403020100ULL;
//static const uint64_t siphash_k1 = 0x0f0e0d0c0b0a0908ULL;

static uint64_t sip_rotl(uint64_t x, int b) {
    return (x << b) | (x >> (64 - b));
}

static void sip_round(uint64_t* v0, uint64_t* v1, uint64_t* v2, uint64_t* v3) {
    *v0 += *v1; *v1 = sip_rotl(*v1, 13); *v1 ^= *v0; *v0 = sip_rotl(*v0, 32);
    *v2 += *v3; *v3 = sip_rotl(*v3, 16); *v3 ^= *v2;
    *v0 += *v3; *v3 = sip_rotl(*v3, 21); *v3 ^= *v0;
    *v2 += *v1; *v1 = sip_rotl(*v1, 17); *v1 ^= *v2; *v2 = sip_rotl(*v2, 32);
}

static uint64_t xcryptographic_hash_siphash24(const uint8_t* in, size_t inlen, uint64_t k0, uint64_t k1)
{
    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;
    uint64_t b = (uint64_t)inlen << 56;
    const uint8_t* end = in + inlen - (inlen % sizeof(uint64_t));
    const uint8_t* left = end + (inlen % sizeof(uint64_t));
    uint64_t m;

    // Process full 8-byte chunks
    while (in != end) {
        memcpy(&m, in, sizeof(m));
        in += sizeof(m);

        v3 ^= m;
        for (int i = 0; i < 2; i++) sip_round(&v0, &v1, &v2, &v3);
        v0 ^= m;
    }

    // Process remaining bytes
    switch (left - end) {
    case 7: b |= ((uint64_t)end[6]) << 48;
    case 6: b |= ((uint64_t)end[5]) << 40;
    case 5: b |= ((uint64_t)end[4]) << 32;
    case 4: b |= ((uint64_t)end[3]) << 24;
    case 3: b |= ((uint64_t)end[2]) << 16;
    case 2: b |= ((uint64_t)end[1]) << 8;
    case 1: b |= ((uint64_t)end[0]); break;
    case 0: break;
    }

    v3 ^= b;
    for (int i = 0; i < 2; i++) sip_round(&v0, &v1, &v2, &v3);
    v0 ^= b;
    v2 ^= 0xff;
    for (int i = 0; i < 4; i++) sip_round(&v0, &v1, &v2, &v3);

    return v0 ^ v1 ^ v2 ^ v3;
}
/* ========================= MetroHash64 Implementation ========================= */

/**
 * @brief MetroHash64 核心哈希函数实现
 *
 * @param key   [in] 指向待哈希数据的指针，不能为 NULL（当 len > 0 时）
 * @param len   [in] 输入数据的字节长度，类型为 uint64_t 以支持大尺寸输入
 * @param seed  [in] 哈希种子值，用于改变哈希函数的行为：
 *              - seed = 0：标准 MetroHash64 行为
 *              - seed ≠ 0：密钥化哈希模式，相同输入产生不同输出
 *              - 推荐在安全敏感场景使用随机种子防止哈希DoS攻击
 *
 * @return      64位哈希值（uint64_t），具有良好的分布性和低碰撞率
 *
 * @note        此为内部实现函数，通常通过 xcryptographic_hash_metrohash64() 公共接口调用
 * @warning     当 len = 0 时，key 参数可为任意值（包括 NULL），函数返回基于 seed 的确定性结果
 */
static inline uint64_t metrohash64(const uint8_t* key, uint64_t len, uint64_t seed) {
    const uint8_t* p = key;
    const uint8_t* const end = p + len;

    // Initialize state with constants XORed with seed
    uint64_t v0 = 0x736f6d6570736575ULL ^ seed;
    uint64_t v1 = 0x646f72616e646f6dULL ^ seed;
    uint64_t v2 = 0x6c7967656e657261ULL ^ seed;
    uint64_t v3 = 0x7465646279746573ULL ^ seed;

    // Process 32-byte blocks
    while (p + 32 <= end) {
        uint64_t t0, t1, t2, t3;

        // Load 32 bytes as four 64-bit values (little-endian)
        t0 = ((uint64_t)p[0] << 0) | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
            ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
        t1 = ((uint64_t)p[8] << 0) | ((uint64_t)p[9] << 8) | ((uint64_t)p[10] << 16) | ((uint64_t)p[11] << 24) |
            ((uint64_t)p[12] << 32) | ((uint64_t)p[13] << 40) | ((uint64_t)p[14] << 48) | ((uint64_t)p[15] << 56);
        t2 = ((uint64_t)p[16] << 0) | ((uint64_t)p[17] << 8) | ((uint64_t)p[18] << 16) | ((uint64_t)p[19] << 24) |
            ((uint64_t)p[20] << 32) | ((uint64_t)p[21] << 40) | ((uint64_t)p[22] << 48) | ((uint64_t)p[23] << 56);
        t3 = ((uint64_t)p[24] << 0) | ((uint64_t)p[25] << 8) | ((uint64_t)p[26] << 16) | ((uint64_t)p[27] << 24) |
            ((uint64_t)p[28] << 32) | ((uint64_t)p[29] << 40) | ((uint64_t)p[30] << 48) | ((uint64_t)p[31] << 56);

        v0 += t0;
        v1 += t1;
        v2 += t2;
        v3 += t3;

        // Mix
        v0 = (v0 << 23) | (v0 >> 41);
        v2 = (v2 << 23) | (v2 >> 41);
        v0 ^= v2;
        v1 = (v1 << 23) | (v1 >> 41);
        v3 = (v3 << 23) | (v3 >> 41);
        v1 ^= v3;

        v0 = (v0 << 23) | (v0 >> 41);
        v2 = (v2 << 23) | (v2 >> 41);
        v0 ^= v2;
        v1 = (v1 << 23) | (v1 >> 41);
        v3 = (v3 << 23) | (v3 >> 41);
        v1 ^= v3;

        p += 32;
    }

    // Handle remaining bytes
    if (len & 31) {
        uint64_t t[4] = { 0 };
        int i = 0;

        // Load remaining bytes
        while (p < end) {
            t[i >> 3] |= ((uint64_t)*p++) << ((i & 7) << 3);
            i++;
        }

        // Add length to the last word
        t[(i - 1) >> 3] |= len << 56;

        v0 += t[0];
        v1 += t[1];
        v2 += t[2];
        v3 += t[3];

        // Final mix
        v0 = (v0 << 23) | (v0 >> 41);
        v2 = (v2 << 23) | (v2 >> 41);
        v0 ^= v2;
        v1 = (v1 << 23) | (v1 >> 41);
        v3 = (v3 << 23) | (v3 >> 41);
        v1 ^= v3;

        v0 = (v0 << 23) | (v0 >> 41);
        v2 = (v2 << 23) | (v2 >> 41);
        v0 ^= v2;
        v1 = (v1 << 23) | (v1 >> 41);
        v3 = (v3 << 23) | (v3 >> 41);
        v1 ^= v3;

        v0 = (v0 << 23) | (v0 >> 41);
        v2 = (v2 << 23) | (v2 >> 41);
        v0 ^= v2;
        v1 = (v1 << 23) | (v1 >> 41);
        v3 = (v3 << 23) | (v3 >> 41);
        v1 ^= v3;
    }

    return v0 ^ v1 ^ v2 ^ v3;
}

static uint64_t xcryptographic_hash_metrohash64(const void* key, size_t len) {
    return metrohash64((const uint8_t*)key, len, 0);
}

/* ========================= MumHash Implementation ========================= */

// MumHash implementation based on Vsevolod Solovyov's algorithm
static inline uint64_t mum_rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static inline uint64_t mum_hash_step(uint64_t h, uint64_t k) {
    k *= 0xffdd55aa21313131ULL;
    k ^= mum_rotl64(k, 32);
    k *= 0x7722113355664499ULL;
    h ^= k;
    h = mum_rotl64(h, 29);
    return h * 7 + 0x517cc1b727220a95ULL;
}

static inline uint64_t mum_unaligned_read(const uint8_t* p) {
    uint64_t result;
    memcpy(&result, p, sizeof(result));
    return result;
}

/**
 * @brief MumHash 哈希函数。
 * Vsevolod Solovyov 开发的高质量哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 高质量哈希需求、替代 SpookyHash
 * @优点: 质量优秀，速度良好，现代设计
 * @缺点: 相对小众，文档和社区支持有限
 */
static uint64_t xcryptographic_hash_mumhash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    const uint8_t* const end = data + len;
    uint64_t h = len;

    if (len >= 8) {
        const uint8_t* const limit = end - 8;
        uint64_t v = mum_unaligned_read(data);
        h = mum_hash_step(h, v);
        data += 8;

        while (data <= limit) {
            v = mum_unaligned_read(data);
            h = mum_hash_step(h, v);
            data += 8;
        }
    }

    if (data < end) {
        uint64_t v = 0;
        switch (end - data) {
        case 7: v ^= ((uint64_t)data[6]) << 48;
        case 6: v ^= ((uint64_t)data[5]) << 40;
        case 5: v ^= ((uint64_t)data[4]) << 32;
        case 4: v ^= ((uint64_t)data[3]) << 24;
        case 3: v ^= ((uint64_t)data[2]) << 16;
        case 2: v ^= ((uint64_t)data[1]) << 8;
        case 1: v ^= ((uint64_t)data[0]);
        }
        h = mum_hash_step(h, v);
    }

    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;

    return h;
}

/* ========================= FastHash64 Implementation ========================= */

// FastHash64 by Dan Bernstein
static inline uint64_t fasthash_mix(uint64_t h) {
    h ^= h >> 23;
    h *= 0x2127599bf4325c37ULL;
    h ^= h >> 47;
    return h;
}

/**
 * @brief FastHash64 哈希函数。
 * 简单快速的64位哈希函数。
 *
 * @param key 待哈希数据的指针
 * @param len 数据的字节长度
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 简单快速的64位哈希需求、教学示例
 * @优点: 实现简单，速度较快，64位输出
 * @缺点: 质量一般，分布性不如专业算法
 */
static uint64_t xcryptographic_hash_fasthash64(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    uint64_t h = 0xcbf29ce484222325ULL;
    size_t i;

    for (i = 0; i + 8 <= len; i += 8) {
        uint64_t v;
        memcpy(&v, data + i, 8);
        h ^= v;
        h = (h * 0x100000001b3ULL) ^ (h >> 29);
    }

    if (i < len) {
        uint64_t v = 0;
        for (size_t j = 0; j < len - i; j++) {
            v |= ((uint64_t)data[i + j]) << (j * 8);
        }
        h ^= v;
        h = (h * 0x100000001b3ULL) ^ (h >> 29);
    }

    return fasthash_mix(h);
}

/* ========================= Thomas Wang 64-bit Hash Implementation ========================= */

/**
 * @brief Thomas Wang 64-bit 哈希函数。
 * 经典的整数哈希函数，优秀的位分布。
 *
 * @param key 待哈希数据的指针（应指向8字节整数）
 * @param len 数据的字节长度（应为8）
 * @return 64位哈希值 (uint64_t)
 *
 * @推荐用途: 整数键哈希、哈希表索引计算
 * @优点: 对整数输入优化极佳，位分布优秀，速度极快
 * @缺点: 主要针对整数设计，对字符串等复杂数据效果一般
 */
static uint64_t xcryptographic_hash_thomaswang64(const void* key, size_t len) {
    if (len != sizeof(uint64_t)) {
        // 如果不是8字节，使用第一个8字节或填充
        uint64_t val = 0;
        const uint8_t* data = (const uint8_t*)key;
        size_t copy_len = (len < 8) ? len : 8;
        memcpy(&val, data, copy_len);
        if (len < 8) {
            // 简单填充剩余字节
            for (size_t i = len; i < 8; i++) {
                val |= ((uint64_t)len) << (i * 8);
            }
        }
        uint64_t hash = val;
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
        return hash;
    }
    else {
        uint64_t hash;
        memcpy(&hash, key, sizeof(hash));
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
        return hash;
    }
}
/* ========================= OneAtATime Hash Implementation ========================= */
static size_t xcryptographic_hash_oneatatime(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 0;

    for (size_t i = 0; i < len; i++) {
        hash += data[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }

    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);

    return hash;
}
/* ========================= SuperFastHash Implementation ========================= */

// Helper function for SuperFastHash
static inline uint32_t get16bits(const void* p) {
    uint32_t result;
    memcpy(&result, p, sizeof(uint16_t));
    return result;
}
static size_t xcryptographic_hash_superfasthash(const void* key, size_t len) {
    if (len == 0) return 0;

    const uint8_t* data = (const uint8_t*)key;
    uint32_t hash = (uint32_t)len;
    uint32_t tmp;
    int rem;

    rem = len & 3;
    len >>= 2;

    // Main loop
    for (; len > 0; len--) {
        hash += get16bits(data);
        tmp = (get16bits(data + 2) << 11) ^ hash;
        hash = (hash << 16) ^ tmp;
        data += 2 * sizeof(uint16_t);
        hash += hash >> 11;
    }

    // Handle end cases
    switch (rem) {
    case 3:
        hash += get16bits(data);
        hash ^= hash << 16;
        hash ^= ((signed char)data[2]) << 18;
        hash += hash >> 11;
        break;
    case 2:
        hash += get16bits(data);
        hash ^= hash << 11;
        hash += hash >> 17;
        break;
    case 1:
        hash += (signed char)*data;
        hash ^= hash << 10;
        hash += hash >> 1;
        break;
    }

    // Force "avalanche" of final 127 bits
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;

    return (size_t)hash;
}

/* ========================= ELFHash Implementation ========================= */
static size_t xcryptographic_hash_elfhash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 0;
    size_t x = 0;

    for (size_t i = 0; i < len; i++) {
        hash = (hash << 4) + data[i];
        if ((x = hash & 0xF0000000L) != 0) {
            hash ^= (x >> 24);
            hash &= ~x;
        }
    }

    return hash;
}

/* ========================= APHash Implementation ========================= */

static size_t xcryptographic_hash_aphash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 0xAAAAAAAA;

    for (size_t i = 0; i < len; i++) {
        if ((i & 1) == 0) {
            hash ^= ((hash << 7) ^ data[i] ^ (hash >> 3));
        }
        else {
            hash ^= (~((hash << 11) ^ data[i] ^ (hash >> 5)));
        }
    }

    return hash;
}
/**
 * @brief JSHash (Java String Hash)
 * Java 字符串类使用的哈希算法
 */
static size_t xcryptographic_hash_jshash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 1315423911;

    for (size_t i = 0; i < len; i++) {
        hash ^= ((hash << 5) + data[i] + (hash >> 2));
    }

    return hash;
}

/**
 * @brief RSHash (Robert Sedgwick Hash)
 * 由 Robert Sedgwick 在其著作《Algorithms in C》中提出
 */
static size_t xcryptographic_hash_rshash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 0;
    size_t magic = 63689;

    for (size_t i = 0; i < len; i++) {
        hash = hash * magic + data[i];
        magic *= 378551;
    }

    return hash;
}

/**
 * @brief PJWHash (Peter J. Weinberger Hash)
 * 由 Peter J. Weinberger 提出，ELF 格式也使用类似算法
 */
static size_t xcryptographic_hash_pjwhash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 0;
    size_t high_bits = 0;

    for (size_t i = 0; i < len; i++) {
        hash = (hash << 4) + data[i];
        high_bits = hash & 0xF0000000;
        if (high_bits) {
            hash ^= (high_bits >> 24);
            hash &= ~high_bits;
        }
    }

    return hash;
}

/**
 * @brief BKDRHash (Brian Kernighan & Dennis Ritchie Hash)
 * 《The C Programming Language》书中提出的算法
 */
static size_t xcryptographic_hash_bkdrhash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 0;
    const size_t seed = 131; // 也可以使用 31、1313、13131 等

    for (size_t i = 0; i < len; i++) {
        hash = hash * seed + data[i];
    }

    return hash;
}

/**
 * @brief SDBMHash
 * 来自 SDBM 项目（公共域数据库），具有良好的分布性
 */
static size_t xcryptographic_hash_sdbmhash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 0;

    for (size_t i = 0; i < len; i++) {
        // 等价于: hash = hash * 65599 + data[i];
        hash = data[i] + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

static uint64_t xcryptographic_hash_murmur3_32_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_murmur3_32(key, len);
}

static uint64_t xcryptographic_hash_djb2_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_djb2(key, len);
}

static uint64_t xcryptographic_hash_pearson_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_pearson(key, len);
}

static uint64_t xcryptographic_hash_lookup3_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_lookup3(key, len);
}

static uint64_t xcryptographic_hash_oneatatime_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_oneatatime(key, len);
}

static uint64_t xcryptographic_hash_superfasthash_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_superfasthash(key, len);
}

static uint64_t xcryptographic_hash_elfhash_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_elfhash(key, len);
}

static uint64_t xcryptographic_hash_aphash_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_aphash(key, len);
}

static uint64_t xcryptographic_hash_jshash_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_jshash(key, len);
}

static uint64_t xcryptographic_hash_rshash_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_rshash(key, len);
}

static uint64_t xcryptographic_hash_pjwhash_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_pjwhash(key, len);
}

static uint64_t xcryptographic_hash_bkdrhash_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_bkdrhash(key, len);
}

static uint64_t xcryptographic_hash_sdbmhash_callback(const void* key, size_t len)
{
    return (uint64_t)xcryptographic_hash_sdbmhash(key, len);
}

XCryptographicHashFunction XCryptographicHash_function(
    XCryptographicHash_Algorithm method)
{
    switch (method) {
    case XCryptographicHash_Murmur3_32: return xcryptographic_hash_murmur3_32_callback;
    case XCryptographicHash_Fnv1a_64: return xcryptographic_hash_fnv1a_64;
    case XCryptographicHash_Djb2: return xcryptographic_hash_djb2_callback;
    case XCryptographicHash_Pearson: return xcryptographic_hash_pearson_callback;
    case XCryptographicHash_Lookup3: return xcryptographic_hash_lookup3_callback;
    case XCryptographicHash_CityHash64: return xcryptographic_hash_cityhash64;
    case XCryptographicHash_FarmHash64: return xcryptographic_hash_farmhash64;
    case XCryptographicHash_HighwayHash64: return xcryptographic_hash_highwayhash64;
    case XCryptographicHash_XxHash64: return xcryptographic_hash_xxhash64;
    case XCryptographicHash_WyHash: return xcryptographic_hash_wyhash;
    case XCryptographicHash_T1ha2: return xcryptographic_hash_t1ha2;
    case XCryptographicHash_SpookyHash64: return xcryptographic_hash_spookyhash64;
    case XCryptographicHash_MetroHash64: return xcryptographic_hash_metrohash64;
    case XCryptographicHash_MumHash: return xcryptographic_hash_mumhash;
    case XCryptographicHash_FastHash64: return xcryptographic_hash_fasthash64;
    case XCryptographicHash_ThomasWang64: return xcryptographic_hash_thomaswang64;
    case XCryptographicHash_OneAtATime: return xcryptographic_hash_oneatatime_callback;
    case XCryptographicHash_SuperFastHash: return xcryptographic_hash_superfasthash_callback;
    case XCryptographicHash_ElfHash: return xcryptographic_hash_elfhash_callback;
    case XCryptographicHash_ApHash: return xcryptographic_hash_aphash_callback;
    case XCryptographicHash_JsHash: return xcryptographic_hash_jshash_callback;
    case XCryptographicHash_RsHash: return xcryptographic_hash_rshash_callback;
    case XCryptographicHash_PjwHash: return xcryptographic_hash_pjwhash_callback;
    case XCryptographicHash_BkdrHash: return xcryptographic_hash_bkdrhash_callback;
    case XCryptographicHash_SdbmHash: return xcryptographic_hash_sdbmhash_callback;
    case XCryptographicHash_SipHash24:
    default:
        return NULL;
    }
}

uint64_t XCryptographicHash_value(const void* data, size_t len,
                                  XCryptographicHash_Algorithm method)
{
    static const uint8_t empty = 0;
    XCryptographicHashFunction function = XCryptographicHash_function(method);
    if (!function || (!data && len != 0)) return 0;
    return function(data ? data : &empty, len);
}

uint64_t XCryptographicHash_siphash24(const uint8_t* data, size_t len,
                                      uint64_t key0, uint64_t key1)
{
    static const uint8_t empty = 0;
    if (!data && len != 0) return 0;
    return xcryptographic_hash_siphash24(data ? data : &empty, len, key0, key1);
}
// ==================== 内部常量 ====================

/* 各算法输出长度（字节） */
static const int g_hashLengths[] = {
    16, /* Md4 */
    16, /* Md5 */
    20, /* Sha1 */
    28, /* Sha224 */
    32, /* Sha256 */
    48, /* Sha384 */
    64, /* Sha512 */
    28, /* Keccak_224 */
    32, /* Keccak_256 */
    48, /* Keccak_384 */
    64, /* Keccak_512 */
    28, /* RealSha3_224 */
    32, /* RealSha3_256 */
    48, /* RealSha3_384 */
    64, /* RealSha3_512 */
    20, /* Blake2b_160 */
    32, /* Blake2b_256 */
    48, /* Blake2b_384 */
    64, /* Blake2b_512 */
    16, /* Blake2s_128 */
    20, /* Blake2s_160 */
    28, /* Blake2s_224 */
    32, /* Blake2s_256 */
    20, /* Ripemd160 */
    4,  /* Murmur3-32 */
    8,  /* FNV-1a-64 */
    4,  /* DJB2 */
    4,  /* Pearson */
    4,  /* Lookup3 */
    8,  /* CityHash64 */
    8,  /* FarmHash64 */
    8,  /* HighwayHash64 */
    8,  /* xxHash64 */
    8,  /* wyhash */
    8,  /* t1ha2 */
    8,  /* SpookyHash64 */
    8,  /* SipHash-2-4 */
    8,  /* MetroHash64 */
    8,  /* MumHash */
    8,  /* FastHash64 */
    8,  /* Thomas Wang 64 */
    4,  /* One-at-a-Time */
    4,  /* SuperFastHash */
    4,  /* ELFHash */
    4,  /* APHash */
    4,  /* JSHash */
    4,  /* RSHash */
    4,  /* PJWHash */
    4,  /* BKDRHash */
    4,  /* SDBMHash */
};

/* 算法名称 */
static const char* g_algorithmNames[] = {
    "Md4",
    "Md5",
    "Sha1",
    "Sha224",
    "Sha256",
    "Sha384",
    "Sha512",
    "Keccak-224",
    "Keccak-256",
    "Keccak-384",
    "Keccak-512",
    "Sha3-224",
    "Sha3-256",
    "Sha3-384",
    "Sha3-512",
    "Blake2b-160",
    "Blake2b-256",
    "Blake2b-384",
    "Blake2b-512",
    "Blake2s-128",
    "Blake2s-160",
    "Blake2s-224",
    "Blake2s-256",
    "Ripemd160",
    "Murmur3-32",
    "FNV-1a-64",
    "DJB2",
    "Pearson",
    "Lookup3",
    "CityHash64",
    "FarmHash64",
    "HighwayHash64",
    "xxHash64",
    "wyhash",
    "t1ha2",
    "SpookyHash64",
    "SipHash-2-4",
    "MetroHash64",
    "MumHash",
    "FastHash64",
    "ThomasWang64",
    "OneAtATime",
    "SuperFastHash",
    "ELFHash",
    "APHash",
    "JSHash",
    "RSHash",
    "PJWHash",
    "BKDRHash",
    "SDBMHash",
};

// ==================== MD5 实现 ====================

#define MD5_BLOCK_SIZE 64
#define MD5_DIGEST_SIZE 16

typedef struct MD5_CTX {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[MD5_BLOCK_SIZE];
} MD5_CTX;

static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const uint8_t S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#define F(x,y,z) ((x & y) | (~x & z))
#define G(x,y,z) ((x & z) | (y & ~z))
#define H(x,y,z) (x ^ y ^ z)
#define I(x,y,z) (y ^ (x | ~z))

static void md5_transform(MD5_CTX* ctx, const uint8_t* block)
{
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t m[16];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = (uint32_t)block[i * 4] |
               ((uint32_t)block[i * 4 + 1] << 8) |
               ((uint32_t)block[i * 4 + 2] << 16) |
               ((uint32_t)block[i * 4 + 3] << 24);
    }

    for (i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16) {
            f = F(b, c, d);
            g = i;
        } else if (i < 32) {
            f = G(b, c, d);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = H(b, c, d);
            g = (3 * i + 5) % 16;
        } else {
            f = I(b, c, d);
            g = (7 * i) % 16;
        }

        uint32_t temp = d;
        d = c;
        c = b;
        b = b + ROTLEFT(a + f + K[i] + m[g], S[i]);
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

static void md5_init(MD5_CTX* ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
    memset(ctx->buffer, 0, MD5_BLOCK_SIZE);
}

static void md5_update(MD5_CTX* ctx, const uint8_t* data, size_t len)
{
    size_t index = (size_t)(ctx->count % MD5_BLOCK_SIZE);
    ctx->count += len;

    size_t firstPart = MD5_BLOCK_SIZE - index;
    if (len >= firstPart) {
        memcpy(&ctx->buffer[index], data, firstPart);
        md5_transform(ctx, ctx->buffer);

        data += firstPart;
        len -= firstPart;

        while (len >= MD5_BLOCK_SIZE) {
            md5_transform(ctx, data);
            data += MD5_BLOCK_SIZE;
            len -= MD5_BLOCK_SIZE;
        }
        index = 0;
    }

    if (len > 0) {
        memcpy(&ctx->buffer[index], data, len);
    }
}

static void md5_final(MD5_CTX* ctx, uint8_t* digest)
{
    uint8_t bits[8];
    size_t index = (size_t)(ctx->count % MD5_BLOCK_SIZE);
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);

    /* 保存长度 */
    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((ctx->count << 3) >> (i * 8));
    }

    /* 填充 */
    static const uint8_t padding[64] = { 0x80 };
    md5_update(ctx, padding, padLen);
    md5_update(ctx, bits, 8);

    /* 输出 */
    for (int i = 0; i < 4; i++) {
        digest[i] = (uint8_t)(ctx->state[0] >> (i * 8));
        digest[i + 4] = (uint8_t)(ctx->state[1] >> (i * 8));
        digest[i + 8] = (uint8_t)(ctx->state[2] >> (i * 8));
        digest[i + 12] = (uint8_t)(ctx->state[3] >> (i * 8));
    }
}

// ==================== SHA-256 实现 ====================

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

typedef struct SHA256_CTX {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[SHA256_BLOCK_SIZE];
} SHA256_CTX;

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))

static void sha256_transform(SHA256_CTX* ctx, const uint8_t* block)
{
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }

    for (i = 16; i < 64; i++) {
        W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K256[i] + W[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX* ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
    memset(ctx->buffer, 0, SHA256_BLOCK_SIZE);
}

static void sha224_init(SHA256_CTX* ctx)
{
    ctx->state[0] = 0xc1059ed8;
    ctx->state[1] = 0x367cd507;
    ctx->state[2] = 0x3070dd17;
    ctx->state[3] = 0xf70e5939;
    ctx->state[4] = 0xffc00b31;
    ctx->state[5] = 0x68581511;
    ctx->state[6] = 0x64f98fa7;
    ctx->state[7] = 0xbefa4fa4;
    ctx->count = 0;
    memset(ctx->buffer, 0, SHA256_BLOCK_SIZE);
}

static void sha256_update(SHA256_CTX* ctx, const uint8_t* data, size_t len)
{
    size_t index = (size_t)(ctx->count % SHA256_BLOCK_SIZE);
    ctx->count += len;

    size_t firstPart = SHA256_BLOCK_SIZE - index;
    if (len >= firstPart) {
        memcpy(&ctx->buffer[index], data, firstPart);
        sha256_transform(ctx, ctx->buffer);

        data += firstPart;
        len -= firstPart;

        while (len >= SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, data);
            data += SHA256_BLOCK_SIZE;
            len -= SHA256_BLOCK_SIZE;
        }
        index = 0;
    }

    if (len > 0) {
        memcpy(&ctx->buffer[index], data, len);
    }
}

static void sha256_final(SHA256_CTX* ctx, uint8_t* digest, int digestSize)
{
    uint8_t bits[8];
    size_t index = (size_t)(ctx->count % SHA256_BLOCK_SIZE);
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);

    /* 保存长度 */
    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((ctx->count << 3) >> (56 - i * 8));
    }

    /* 填充 */
    static const uint8_t padding[64] = { 0x80 };
    sha256_update(ctx, padding, padLen);
    sha256_update(ctx, bits, 8);

    /* 输出 */
    for (int i = 0; i < digestSize; i++) {
        digest[i] = (uint8_t)(ctx->state[i / 4] >> (24 - (i % 4) * 8));
    }
}

// ==================== SHA-512 实现 ====================

#define SHA512_BLOCK_SIZE 128
#define SHA512_DIGEST_SIZE 64

typedef struct SHA512_CTX {
    uint64_t state[8];
    uint64_t count;
    uint8_t buffer[SHA512_BLOCK_SIZE];
} SHA512_CTX;

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

#define CH64(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ64(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP064(x) (ROTRIGHT64(x,28) ^ ROTRIGHT64(x,34) ^ ROTRIGHT64(x,39))
#define EP164(x) (ROTRIGHT64(x,14) ^ ROTRIGHT64(x,18) ^ ROTRIGHT64(x,41))
#define SIG064(x) (ROTRIGHT64(x,1) ^ ROTRIGHT64(x,8) ^ ((x) >> 7))
#define SIG164(x) (ROTRIGHT64(x,19) ^ ROTRIGHT64(x,61) ^ ((x) >> 6))
#define ROTRIGHT64(a,b) (((a) >> (b)) | ((a) << (64-(b))))

static void sha512_transform(SHA512_CTX* ctx, const uint8_t* block)
{
    uint64_t W[80];
    uint64_t a, b, c, d, e, f, g, h;
    uint64_t t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        W[i] = ((uint64_t)block[i * 8] << 56) | ((uint64_t)block[i * 8 + 1] << 48) |
               ((uint64_t)block[i * 8 + 2] << 40) | ((uint64_t)block[i * 8 + 3] << 32) |
               ((uint64_t)block[i * 8 + 4] << 24) | ((uint64_t)block[i * 8 + 5] << 16) |
               ((uint64_t)block[i * 8 + 6] << 8) | (uint64_t)block[i * 8 + 7];
    }

    for (i = 16; i < 80; i++) {
        W[i] = SIG164(W[i - 2]) + W[i - 7] + SIG064(W[i - 15]) + W[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 80; i++) {
        t1 = h + EP164(e) + CH64(e, f, g) + K512[i] + W[i];
        t2 = EP064(a) + MAJ64(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha512_init(SHA512_CTX* ctx)
{
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count = 0;
    memset(ctx->buffer, 0, SHA512_BLOCK_SIZE);
}

static void sha384_init(SHA512_CTX* ctx)
{
    ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->state[1] = 0x629a292a367cd507ULL;
    ctx->state[2] = 0x9159015a3070dd17ULL;
    ctx->state[3] = 0x152fecd8f70e5939ULL;
    ctx->state[4] = 0x67332667ffc00b31ULL;
    ctx->state[5] = 0x8eb44a8768581511ULL;
    ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->state[7] = 0x47b5481dbefa4fa4ULL;
    ctx->count = 0;
    memset(ctx->buffer, 0, SHA512_BLOCK_SIZE);
}

static void sha512_update(SHA512_CTX* ctx, const uint8_t* data, size_t len)
{
    size_t index = (size_t)(ctx->count % SHA512_BLOCK_SIZE);
    ctx->count += len;

    size_t firstPart = SHA512_BLOCK_SIZE - index;
    if (len >= firstPart) {
        memcpy(&ctx->buffer[index], data, firstPart);
        sha512_transform(ctx, ctx->buffer);

        data += firstPart;
        len -= firstPart;

        while (len >= SHA512_BLOCK_SIZE) {
            sha512_transform(ctx, data);
            data += SHA512_BLOCK_SIZE;
            len -= SHA512_BLOCK_SIZE;
        }
        index = 0;
    }

    if (len > 0) {
        memcpy(&ctx->buffer[index], data, len);
    }
}

static void sha512_final(SHA512_CTX* ctx, uint8_t* digest, int digestSize)
{
    uint8_t bits[16];
    size_t index = (size_t)(ctx->count % SHA512_BLOCK_SIZE);
    size_t padLen = (index < 112) ? (112 - index) : (240 - index);

    /* 保存长度 */
    uint64_t bitLen = ctx->count << 3;
    for (int i = 0; i < 8; i++) {
        bits[i] = 0;
        bits[8 + i] = (uint8_t)(bitLen >> (56 - i * 8));
    }

    /* 填充 */
    static const uint8_t padding[128] = { 0x80 };
    sha512_update(ctx, padding, padLen);
    sha512_update(ctx, bits, 16);

    /* 输出 */
    for (int i = 0; i < digestSize; i++) {
        digest[i] = (uint8_t)(ctx->state[i / 8] >> (56 - (i % 8) * 8));
    }
}

// ==================== SHA-1 实现 ====================

#define SHA1_BLOCK_SIZE 64
#define SHA1_DIGEST_SIZE 20

typedef struct SHA1_CTX {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[SHA1_BLOCK_SIZE];
} SHA1_CTX;

static void sha1_transform(SHA1_CTX* ctx, const uint8_t* block)
{
    uint32_t W[80];
    uint32_t a, b, c, d, e;
    uint32_t t;
    int i;

    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }

    for (i = 16; i < 80; i++) {
        W[i] = ROTLEFT(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        t = ROTLEFT(a, 5) + f + e + k + W[i];
        e = d;
        d = c;
        c = ROTLEFT(b, 30);
        b = a;
        a = t;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_init(SHA1_CTX* ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
    memset(ctx->buffer, 0, SHA1_BLOCK_SIZE);
}

static void sha1_update(SHA1_CTX* ctx, const uint8_t* data, size_t len)
{
    size_t index = (size_t)(ctx->count % SHA1_BLOCK_SIZE);
    ctx->count += len;

    size_t firstPart = SHA1_BLOCK_SIZE - index;
    if (len >= firstPart) {
        memcpy(&ctx->buffer[index], data, firstPart);
        sha1_transform(ctx, ctx->buffer);

        data += firstPart;
        len -= firstPart;

        while (len >= SHA1_BLOCK_SIZE) {
            sha1_transform(ctx, data);
            data += SHA1_BLOCK_SIZE;
            len -= SHA1_BLOCK_SIZE;
        }
        index = 0;
    }

    if (len > 0) {
        memcpy(&ctx->buffer[index], data, len);
    }
}

static void sha1_final(SHA1_CTX* ctx, uint8_t* digest)
{
    uint8_t bits[8];
    size_t index = (size_t)(ctx->count % SHA1_BLOCK_SIZE);
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);

    /* 保存长度 */
    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((ctx->count << 3) >> (56 - i * 8));
    }

    /* 填充 */
    static const uint8_t padding[64] = { 0x80 };
    sha1_update(ctx, padding, padLen);
    sha1_update(ctx, bits, 8);

    /* 输出 */
    for (int i = 0; i < 20; i++) {
        digest[i] = (uint8_t)(ctx->state[i / 4] >> (24 - (i % 4) * 8));
    }
}

// ==================== RIPEMD-160 实现 ====================

#define RIPEMD160_BLOCK_SIZE 64

typedef struct RIPEMD160_CTX {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[RIPEMD160_BLOCK_SIZE];
} RIPEMD160_CTX;

static uint32_t ripemd160_load_le(const uint8_t* input)
{
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static void ripemd160_store_le(uint8_t* output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint32_t ripemd160_rotate_left(uint32_t value, uint8_t bits)
{
    return (value << bits) | (value >> (32u - bits));
}

static uint32_t ripemd160_function(size_t round, uint32_t x, uint32_t y, uint32_t z)
{
    switch (round) {
    case 0: return x ^ y ^ z;
    case 1: return (x & y) | (~x & z);
    case 2: return (x | ~y) ^ z;
    case 3: return (x & z) | (y & ~z);
    default: return x ^ (y | ~z);
    }
}

static void ripemd160_transform(RIPEMD160_CTX* context, const uint8_t block[64])
{
    static const uint8_t leftIndex[80] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8,
        3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12,
        1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2,
        4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13
    };
    static const uint8_t rightIndex[80] = {
        5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12,
        6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2,
        15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13,
        8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14,
        12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11
    };
    static const uint8_t leftShift[80] = {
        11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8,
        7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
        11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5,
        11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12,
        9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6
    };
    static const uint8_t rightShift[80] = {
        8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6,
        9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11,
        9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5,
        15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8,
        8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11
    };
    static const uint32_t leftConstant[5] = {
        UINT32_C(0x00000000), UINT32_C(0x5a827999), UINT32_C(0x6ed9eba1),
        UINT32_C(0x8f1bbcdc), UINT32_C(0xa953fd4e)
    };
    static const uint32_t rightConstant[5] = {
        UINT32_C(0x50a28be6), UINT32_C(0x5c4dd124), UINT32_C(0x6d703ef3),
        UINT32_C(0x7a6d76e9), UINT32_C(0x00000000)
    };
    uint32_t words[16];
    uint32_t al, bl, cl, dl, el, ar, br, cr, dr, er;
    size_t i;

    for (i = 0; i < 16; ++i) words[i] = ripemd160_load_le(block + i * 4u);
    al = ar = context->state[0];
    bl = br = context->state[1];
    cl = cr = context->state[2];
    dl = dr = context->state[3];
    el = er = context->state[4];
    for (i = 0; i < 80; ++i) {
        size_t round = i / 16u;
        uint32_t temp = ripemd160_rotate_left(al + ripemd160_function(round, bl, cl, dl) +
                                               words[leftIndex[i]] + leftConstant[round],
                                               leftShift[i]) + el;
        al = el;
        el = dl;
        dl = ripemd160_rotate_left(cl, 10);
        cl = bl;
        bl = temp;

        temp = ripemd160_rotate_left(ar + ripemd160_function(4u - round, br, cr, dr) +
                                     words[rightIndex[i]] + rightConstant[round],
                                     rightShift[i]) + er;
        ar = er;
        er = dr;
        dr = ripemd160_rotate_left(cr, 10);
        cr = br;
        br = temp;
    }
    {
        uint32_t temp = context->state[1] + cl + dr;
        context->state[1] = context->state[2] + dl + er;
        context->state[2] = context->state[3] + el + ar;
        context->state[3] = context->state[4] + al + br;
        context->state[4] = context->state[0] + bl + cr;
        context->state[0] = temp;
    }
    memset(words, 0, sizeof(words));
}

static void ripemd160_init(RIPEMD160_CTX* context)
{
    context->state[0] = UINT32_C(0x67452301);
    context->state[1] = UINT32_C(0xefcdab89);
    context->state[2] = UINT32_C(0x98badcfe);
    context->state[3] = UINT32_C(0x10325476);
    context->state[4] = UINT32_C(0xc3d2e1f0);
    context->count = 0;
    memset(context->buffer, 0, sizeof(context->buffer));
}

static void ripemd160_update(RIPEMD160_CTX* context, const uint8_t* data, size_t size)
{
    size_t offset = (size_t)(context->count % RIPEMD160_BLOCK_SIZE);
    context->count += size;
    if (size >= RIPEMD160_BLOCK_SIZE - offset) {
        size_t first = RIPEMD160_BLOCK_SIZE - offset;
        if (offset != 0) {
            memcpy(context->buffer + offset, data, first);
            ripemd160_transform(context, context->buffer);
            data += first;
            size -= first;
        }
        while (size >= RIPEMD160_BLOCK_SIZE) {
            ripemd160_transform(context, data);
            data += RIPEMD160_BLOCK_SIZE;
            size -= RIPEMD160_BLOCK_SIZE;
        }
        offset = 0;
    }
    if (size != 0) memcpy(context->buffer + offset, data, size);
}

static void ripemd160_final(RIPEMD160_CTX* context, uint8_t digest[20])
{
    static const uint8_t padding[64] = { 0x80 };
    uint8_t bitLength[8];
    uint64_t length = context->count << 3;
    size_t offset = (size_t)(context->count % RIPEMD160_BLOCK_SIZE);
    size_t paddingSize = offset < 56 ? 56 - offset : 120 - offset;
    size_t i;
    for (i = 0; i < sizeof(bitLength); ++i) {
        bitLength[i] = (uint8_t)length;
        length >>= 8;
    }
    ripemd160_update(context, padding, paddingSize);
    ripemd160_update(context, bitLength, sizeof(bitLength));
    for (i = 0; i < 5; ++i) ripemd160_store_le(digest + i * 4u, context->state[i]);
    memset(context, 0, sizeof(*context));
}

// ==================== MD4 实现 ====================

#define MD4_BLOCK_SIZE 64
#define MD4_DIGEST_SIZE 16

typedef struct MD4_CTX {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[MD4_BLOCK_SIZE];
} MD4_CTX;

static void md4_transform(MD4_CTX* ctx, const uint8_t* block)
{
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t m[16];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = (uint32_t)block[i * 4] |
               ((uint32_t)block[i * 4 + 1] << 8) |
               ((uint32_t)block[i * 4 + 2] << 16) |
               ((uint32_t)block[i * 4 + 3] << 24);
    }

    /* Round 1 */
#define MD4_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD4_ROUND1(a, b, c, d, x, s) { a += MD4_F(b, c, d) + x; a = ROTLEFT(a, s); }

    MD4_ROUND1(a, b, c, d, m[0], 3);
    MD4_ROUND1(d, a, b, c, m[1], 7);
    MD4_ROUND1(c, d, a, b, m[2], 11);
    MD4_ROUND1(b, c, d, a, m[3], 19);
    MD4_ROUND1(a, b, c, d, m[4], 3);
    MD4_ROUND1(d, a, b, c, m[5], 7);
    MD4_ROUND1(c, d, a, b, m[6], 11);
    MD4_ROUND1(b, c, d, a, m[7], 19);
    MD4_ROUND1(a, b, c, d, m[8], 3);
    MD4_ROUND1(d, a, b, c, m[9], 7);
    MD4_ROUND1(c, d, a, b, m[10], 11);
    MD4_ROUND1(b, c, d, a, m[11], 19);
    MD4_ROUND1(a, b, c, d, m[12], 3);
    MD4_ROUND1(d, a, b, c, m[13], 7);
    MD4_ROUND1(c, d, a, b, m[14], 11);
    MD4_ROUND1(b, c, d, a, m[15], 19);

    /* Round 2 */
#define MD4_G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define MD4_ROUND2(a, b, c, d, x, s) { a += MD4_G(b, c, d) + x + 0x5A827999; a = ROTLEFT(a, s); }

    MD4_ROUND2(a, b, c, d, m[0], 3);
    MD4_ROUND2(d, a, b, c, m[4], 5);
    MD4_ROUND2(c, d, a, b, m[8], 9);
    MD4_ROUND2(b, c, d, a, m[12], 13);
    MD4_ROUND2(a, b, c, d, m[1], 3);
    MD4_ROUND2(d, a, b, c, m[5], 5);
    MD4_ROUND2(c, d, a, b, m[9], 9);
    MD4_ROUND2(b, c, d, a, m[13], 13);
    MD4_ROUND2(a, b, c, d, m[2], 3);
    MD4_ROUND2(d, a, b, c, m[6], 5);
    MD4_ROUND2(c, d, a, b, m[10], 9);
    MD4_ROUND2(b, c, d, a, m[14], 13);
    MD4_ROUND2(a, b, c, d, m[3], 3);
    MD4_ROUND2(d, a, b, c, m[7], 5);
    MD4_ROUND2(c, d, a, b, m[11], 9);
    MD4_ROUND2(b, c, d, a, m[15], 13);

    /* Round 3 */
#define MD4_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD4_ROUND3(a, b, c, d, x, s) { a += MD4_H(b, c, d) + x + 0x6ED9EBA1; a = ROTLEFT(a, s); }

    MD4_ROUND3(a, b, c, d, m[0], 3);
    MD4_ROUND3(d, a, b, c, m[8], 9);
    MD4_ROUND3(c, d, a, b, m[4], 11);
    MD4_ROUND3(b, c, d, a, m[12], 15);
    MD4_ROUND3(a, b, c, d, m[2], 3);
    MD4_ROUND3(d, a, b, c, m[10], 9);
    MD4_ROUND3(c, d, a, b, m[6], 11);
    MD4_ROUND3(b, c, d, a, m[14], 15);
    MD4_ROUND3(a, b, c, d, m[1], 3);
    MD4_ROUND3(d, a, b, c, m[9], 9);
    MD4_ROUND3(c, d, a, b, m[5], 11);
    MD4_ROUND3(b, c, d, a, m[13], 15);
    MD4_ROUND3(a, b, c, d, m[3], 3);
    MD4_ROUND3(d, a, b, c, m[11], 9);
    MD4_ROUND3(c, d, a, b, m[7], 11);
    MD4_ROUND3(b, c, d, a, m[15], 15);

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

static void md4_init(MD4_CTX* ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
    memset(ctx->buffer, 0, MD4_BLOCK_SIZE);
}

static void md4_update(MD4_CTX* ctx, const uint8_t* data, size_t len)
{
    size_t index = (size_t)(ctx->count % MD4_BLOCK_SIZE);
    ctx->count += len;

    size_t firstPart = MD4_BLOCK_SIZE - index;
    if (len >= firstPart) {
        memcpy(&ctx->buffer[index], data, firstPart);
        md4_transform(ctx, ctx->buffer);

        data += firstPart;
        len -= firstPart;

        while (len >= MD4_BLOCK_SIZE) {
            md4_transform(ctx, data);
            data += MD4_BLOCK_SIZE;
            len -= MD4_BLOCK_SIZE;
        }
        index = 0;
    }

    if (len > 0) {
        memcpy(&ctx->buffer[index], data, len);
    }
}

static void md4_final(MD4_CTX* ctx, uint8_t* digest)
{
    uint8_t bits[8];
    size_t index = (size_t)(ctx->count % MD4_BLOCK_SIZE);
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);

    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((ctx->count << 3) >> (i * 8));
    }

    static const uint8_t padding[64] = { 0x80 };
    md4_update(ctx, padding, padLen);
    md4_update(ctx, bits, 8);

    for (int i = 0; i < 4; i++) {
        digest[i] = (uint8_t)(ctx->state[0] >> (i * 8));
        digest[i + 4] = (uint8_t)(ctx->state[1] >> (i * 8));
        digest[i + 8] = (uint8_t)(ctx->state[2] >> (i * 8));
        digest[i + 12] = (uint8_t)(ctx->state[3] >> (i * 8));
    }
}

// ==================== Keccak/SHA3 实现 ====================

#define KECCAK_ROUNDS 24
#define KECCAK_STATE_SIZE 25  /* 5x5 的 uint64_t 状态 */

/* Keccak 轮常数 */
static const uint64_t keccak_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

/* 旋转偏移量 */
static const int keccak_rotations[5][5] = {
    {0, 36, 3, 41, 18},
    {1, 44, 10, 45, 2},
    {62, 6, 43, 15, 61},
    {28, 55, 25, 21, 56},
    {27, 20, 39, 8, 14}
};

#define KECCAK_ROL64(a, n) (((a) << (n)) | ((a) >> (64 - (n))))

void XCryptographicHash_keccakPermute(uint64_t* state)
{
    int round;
    uint64_t C[5], D[5], B[5][5];

    for (round = 0; round < KECCAK_ROUNDS; round++) {
        /* θ 步骤 */
        for (int x = 0; x < 5; x++) {
            C[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }
        for (int x = 0; x < 5; x++) {
            D[x] = C[(x + 4) % 5] ^ KECCAK_ROL64(C[(x + 1) % 5], 1);
        }
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                state[x + 5 * y] ^= D[x];
            }
        }

        /* ρ 和 π 步骤 */
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                B[y][(2 * x + 3 * y) % 5] = KECCAK_ROL64(state[x + 5 * y], keccak_rotations[x][y]);
            }
        }

        /* χ 步骤 */
        for (int x = 0; x < 5; x++) {
            for (int y = 0; y < 5; y++) {
                state[x + 5 * y] = B[x][y] ^ ((~B[(x + 1) % 5][y]) & B[(x + 2) % 5][y]);
            }
        }

        /* ι 步骤 */
        state[0] ^= keccak_rc[round];
    }
}

typedef struct Keccak_CTX {
    uint64_t state[25];
    uint8_t buffer[200];  /* 最大速率 168 字节 (SHA3-224) */
    size_t rate;          /* 速率（字节） */
    size_t digestSize;    /* 输出长度 */
    size_t bufferLen;     /* 缓冲区当前长度 */
    uint8_t delimitedSuffix; /* 分隔符：SHA3=0x06, Keccak=0x01 */
} Keccak_CTX;

static void keccak_init(Keccak_CTX* ctx, int digestSize, bool isSha3)
{
    memset(ctx->state, 0, sizeof(ctx->state));
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
    ctx->digestSize = digestSize;
    ctx->bufferLen = 0;

    /* 速率 = (1600 - 2*输出长度) / 8 */
    ctx->rate = (200 - 2 * digestSize);

    /* SHA3 使用 0x06 分隔符，原始 Keccak 使用 0x01 */
    ctx->delimitedSuffix = isSha3 ? 0x06 : 0x01;
}

static void keccak_absorb(Keccak_CTX* ctx, const uint8_t* data, size_t len)
{
    size_t offset = 0;

    /* 处理缓冲区中的数据 */
    while (len > 0) {
        size_t copyLen = ctx->rate - ctx->bufferLen;
        if (copyLen > len) copyLen = len;

        /* XOR 到缓冲区 */
        for (size_t i = 0; i < copyLen; i++) {
            ctx->buffer[ctx->bufferLen + i] ^= data[offset + i];
        }

        ctx->bufferLen += copyLen;
        offset += copyLen;
        len -= copyLen;

        /* 如果缓冲区满，进行置换 */
        if (ctx->bufferLen == ctx->rate) {
            /* 将缓冲区 XOR 到状态 */
            for (size_t i = 0; i < ctx->rate / 8; i++) {
                ctx->state[i] ^= ((uint64_t*)ctx->buffer)[i];
            }
            XCryptographicHash_keccakPermute(ctx->state);
            ctx->bufferLen = 0;
            memset(ctx->buffer, 0, ctx->rate);
        }
    }
}

static void keccak_final(Keccak_CTX* ctx, uint8_t* output)
{
    /* 填充分隔符 */
    ctx->buffer[ctx->bufferLen] ^= ctx->delimitedSuffix;
    ctx->buffer[ctx->rate - 1] ^= 0x80;

    /* 最后的吸收 */
    for (size_t i = 0; i < ctx->rate / 8; i++) {
        ctx->state[i] ^= ((uint64_t*)ctx->buffer)[i];
    }
    XCryptographicHash_keccakPermute(ctx->state);

    /* 挤出输出 */
    uint8_t* stateBytes = (uint8_t*)ctx->state;
    memcpy(output, stateBytes, ctx->digestSize);
}

// ==================== Blake2b 实现 ====================

#define BLAKE2B_BLOCK_SIZE 128
#define BLAKE2B_MAX_DIGEST 64

/* Blake2b IV */
static const uint64_t blake2b_iv[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

/* Blake2b sigma 置换 */
static const uint8_t blake2b_sigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}
};

typedef struct Blake2b_CTX {
    uint64_t h[8];
    uint64_t t[2];
    uint8_t buffer[BLAKE2B_BLOCK_SIZE];
    size_t bufferLen;
    size_t digestSize;
} Blake2b_CTX;

#define BLAKE2B_G(v, a, b, c, d, x, y) \
    v[a] += v[b] + x; v[d] = ROTRIGHT64(v[d] ^ v[a], 32); \
    v[c] += v[d]; v[b] = ROTRIGHT64(v[b] ^ v[c], 24); \
    v[a] += v[b] + y; v[d] = ROTRIGHT64(v[d] ^ v[a], 16); \
    v[c] += v[d]; v[b] = ROTRIGHT64(v[b] ^ v[c], 63);

static void blake2b_compress(Blake2b_CTX* ctx, const uint8_t* block, bool isFinal)
{
    uint64_t v[16];
    uint64_t m[16];
    int i;

    for (i = 0; i < 8; i++) v[i] = ctx->h[i];
    for (i = 0; i < 8; i++) v[i + 8] = blake2b_iv[i];
    v[12] ^= ctx->t[0];
    v[13] ^= ctx->t[1];
    if (isFinal) v[14] = ~v[14];

    for (i = 0; i < 16; i++) {
        m[i] = ((uint64_t*)block)[i];
    }

    for (i = 0; i < 12; i++) {
        BLAKE2B_G(v, 0, 4, 8, 12, m[blake2b_sigma[i][0]], m[blake2b_sigma[i][1]]);
        BLAKE2B_G(v, 1, 5, 9, 13, m[blake2b_sigma[i][2]], m[blake2b_sigma[i][3]]);
        BLAKE2B_G(v, 2, 6, 10, 14, m[blake2b_sigma[i][4]], m[blake2b_sigma[i][5]]);
        BLAKE2B_G(v, 3, 7, 11, 15, m[blake2b_sigma[i][6]], m[blake2b_sigma[i][7]]);
        BLAKE2B_G(v, 0, 5, 10, 15, m[blake2b_sigma[i][8]], m[blake2b_sigma[i][9]]);
        BLAKE2B_G(v, 1, 6, 11, 12, m[blake2b_sigma[i][10]], m[blake2b_sigma[i][11]]);
        BLAKE2B_G(v, 2, 7, 8, 13, m[blake2b_sigma[i][12]], m[blake2b_sigma[i][13]]);
        BLAKE2B_G(v, 3, 4, 9, 14, m[blake2b_sigma[i][14]], m[blake2b_sigma[i][15]]);
    }

    for (i = 0; i < 8; i++) ctx->h[i] ^= v[i] ^ v[i + 8];
}

static void blake2b_init(Blake2b_CTX* ctx, int digestSize)
{
    ctx->digestSize = digestSize;
    ctx->bufferLen = 0;
    ctx->t[0] = ctx->t[1] = 0;

    for (int i = 0; i < 8; i++) ctx->h[i] = blake2b_iv[i];
    ctx->h[0] ^= 0x01010000 ^ digestSize;

    memset(ctx->buffer, 0, BLAKE2B_BLOCK_SIZE);
}

static void blake2b_update(Blake2b_CTX* ctx, const uint8_t* data, size_t len)
{
    size_t offset = 0;

    while (len > 0) {
        size_t copyLen = BLAKE2B_BLOCK_SIZE - ctx->bufferLen;
        if (copyLen > len) copyLen = len;

        memcpy(ctx->buffer + ctx->bufferLen, data + offset, copyLen);
        ctx->bufferLen += copyLen;
        offset += copyLen;
        len -= copyLen;

        if (ctx->bufferLen == BLAKE2B_BLOCK_SIZE) {
            ctx->t[0] += BLAKE2B_BLOCK_SIZE;
            if (ctx->t[0] < BLAKE2B_BLOCK_SIZE) ctx->t[1]++;
            blake2b_compress(ctx, ctx->buffer, false);
            ctx->bufferLen = 0;
        }
    }
}

static void blake2b_final(Blake2b_CTX* ctx, uint8_t* output)
{
    ctx->t[0] += ctx->bufferLen;
    if (ctx->t[0] < ctx->bufferLen) ctx->t[1]++;

    memset(ctx->buffer + ctx->bufferLen, 0, BLAKE2B_BLOCK_SIZE - ctx->bufferLen);
    blake2b_compress(ctx, ctx->buffer, true);

    memcpy(output, ctx->h, ctx->digestSize);
}

// ==================== Blake2s 实现 ====================

#define BLAKE2S_BLOCK_SIZE 64
#define BLAKE2S_MAX_DIGEST 32

/* Blake2s IV */
static const uint32_t blake2s_iv[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/* Blake2s sigma 置换 */
static const uint8_t blake2s_sigma[10][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0}
};

typedef struct Blake2s_CTX {
    uint32_t h[8];
    uint32_t t[2];
    uint8_t buffer[BLAKE2S_BLOCK_SIZE];
    size_t bufferLen;
    size_t digestSize;
} Blake2s_CTX;

#define BLAKE2S_G(v, a, b, c, d, x, y) \
    v[a] += v[b] + x; v[d] = ROTRIGHT(v[d] ^ v[a], 16); \
    v[c] += v[d]; v[b] = ROTRIGHT(v[b] ^ v[c], 12); \
    v[a] += v[b] + y; v[d] = ROTRIGHT(v[d] ^ v[a], 8); \
    v[c] += v[d]; v[b] = ROTRIGHT(v[b] ^ v[c], 7);

static void blake2s_compress(Blake2s_CTX* ctx, const uint8_t* block, bool isFinal)
{
    uint32_t v[16];
    uint32_t m[16];
    int i;

    for (i = 0; i < 8; i++) v[i] = ctx->h[i];
    for (i = 0; i < 8; i++) v[i + 8] = blake2s_iv[i];
    v[12] ^= ctx->t[0];
    v[13] ^= ctx->t[1];
    if (isFinal) v[14] = ~v[14];

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t*)block)[i];
    }

    for (i = 0; i < 10; i++) {
        BLAKE2S_G(v, 0, 4, 8, 12, m[blake2s_sigma[i][0]], m[blake2s_sigma[i][1]]);
        BLAKE2S_G(v, 1, 5, 9, 13, m[blake2s_sigma[i][2]], m[blake2s_sigma[i][3]]);
        BLAKE2S_G(v, 2, 6, 10, 14, m[blake2s_sigma[i][4]], m[blake2s_sigma[i][5]]);
        BLAKE2S_G(v, 3, 7, 11, 15, m[blake2s_sigma[i][6]], m[blake2s_sigma[i][7]]);
        BLAKE2S_G(v, 0, 5, 10, 15, m[blake2s_sigma[i][8]], m[blake2s_sigma[i][9]]);
        BLAKE2S_G(v, 1, 6, 11, 12, m[blake2s_sigma[i][10]], m[blake2s_sigma[i][11]]);
        BLAKE2S_G(v, 2, 7, 8, 13, m[blake2s_sigma[i][12]], m[blake2s_sigma[i][13]]);
        BLAKE2S_G(v, 3, 4, 9, 14, m[blake2s_sigma[i][14]], m[blake2s_sigma[i][15]]);
    }

    for (i = 0; i < 8; i++) ctx->h[i] ^= v[i] ^ v[i + 8];
}

static void blake2s_init(Blake2s_CTX* ctx, int digestSize)
{
    ctx->digestSize = digestSize;
    ctx->bufferLen = 0;
    ctx->t[0] = ctx->t[1] = 0;

    for (int i = 0; i < 8; i++) ctx->h[i] = blake2s_iv[i];
    ctx->h[0] ^= 0x01010000 ^ digestSize;

    memset(ctx->buffer, 0, BLAKE2S_BLOCK_SIZE);
}

static void blake2s_update(Blake2s_CTX* ctx, const uint8_t* data, size_t len)
{
    size_t offset = 0;

    while (len > 0) {
        size_t copyLen = BLAKE2S_BLOCK_SIZE - ctx->bufferLen;
        if (copyLen > len) copyLen = len;

        memcpy(ctx->buffer + ctx->bufferLen, data + offset, copyLen);
        ctx->bufferLen += copyLen;
        offset += copyLen;
        len -= copyLen;

        if (ctx->bufferLen == BLAKE2S_BLOCK_SIZE) {
            ctx->t[0] += BLAKE2S_BLOCK_SIZE;
            if (ctx->t[0] < BLAKE2S_BLOCK_SIZE) ctx->t[1]++;
            blake2s_compress(ctx, ctx->buffer, false);
            ctx->bufferLen = 0;
        }
    }
}

static void blake2s_final(Blake2s_CTX* ctx, uint8_t* output)
{
    ctx->t[0] += ctx->bufferLen;
    if (ctx->t[0] < ctx->bufferLen) ctx->t[1]++;

    memset(ctx->buffer + ctx->bufferLen, 0, BLAKE2S_BLOCK_SIZE - ctx->bufferLen);
    blake2s_compress(ctx, ctx->buffer, true);

    memcpy(output, ctx->h, ctx->digestSize);
}

// ==================== 统一上下文结构 ====================

typedef union HashContext {
    MD4_CTX md4;
    MD5_CTX md5;
    SHA1_CTX sha1;
    SHA256_CTX sha256;
    SHA512_CTX sha512;
    Keccak_CTX keccak;
    Blake2b_CTX blake2b;
    Blake2s_CTX blake2s;
} HashContext;

// ==================== 轻量内部缓冲区 ====================

static const XMemory* xcryptographic_hash_memory_method(void)
{
    return XMemory_method(XCRYPTOGRAPHIC_HASH_MEMORY_POOL_TYPE);
}

static void* xcryptographic_hash_malloc(size_t size)
{
    const XMemory* memory = xcryptographic_hash_memory_method();
    return memory && memory->malloc ? memory->malloc(size) : NULL;
}

static void* xcryptographic_hash_realloc(void* data, size_t size)
{
    const XMemory* memory = xcryptographic_hash_memory_method();
    return memory && memory->realloc ? memory->realloc(data, size) : NULL;
}

static void xcryptographic_hash_free(void* data)
{
    const XMemory* memory = xcryptographic_hash_memory_method();
    if (data && memory && memory->free) memory->free(data);
}

static void xcryptographic_hash_buffer_delete(void** data,
                                               size_t* size,
                                               size_t* capacity)
{
    if (!data) return;
    xcryptographic_hash_free(*data);
    *data = NULL;
    if (size) *size = 0;
    if (capacity) *capacity = 0;
}

static bool xcryptographic_hash_buffer_reserve(void** data,
                                               size_t* capacity,
                                               size_t required)
{
    size_t newCapacity;
    uint8_t* newData;
    if (!data || !capacity) return false;
    if (required <= *capacity) return true;

    newCapacity = *capacity ? *capacity : 64u;
    while (newCapacity < required) {
        if (newCapacity > SIZE_MAX / 2u) {
            newCapacity = required;
            break;
        }
        newCapacity *= 2u;
    }
    newData = *data
                  ? (uint8_t*)xcryptographic_hash_realloc(*data, newCapacity)
                  : (uint8_t*)xcryptographic_hash_malloc(newCapacity);
    if (!newData) return false;
    *data = newData;
    *capacity = newCapacity;
    return true;
}

static bool xcryptographic_hash_buffer_append(void** data,
                                               size_t* size,
                                               size_t* capacity,
                                               const void* source,
                                               size_t length)
{
    if (!data || !size || !capacity || (!source && length > 0)) return false;
    if (length == 0) return true;
    if (*size > SIZE_MAX - length) return false;
    if (!xcryptographic_hash_buffer_reserve(data, capacity, *size + length))
        return false;
    memcpy((uint8_t*)*data + *size, source, length);
    *size += length;
    return true;
}

static bool xcryptographic_hash_buffer_resize(void** data,
                                               size_t* size,
                                               size_t* capacity,
                                               size_t length)
{
    size_t oldSize;
    if (!data || !size || !capacity) return false;
    if (!xcryptographic_hash_buffer_reserve(data, capacity, length)) return false;
    oldSize = *size;
    if (length > oldSize)
        memset((uint8_t*)*data + oldSize, 0, length - oldSize);
    *size = length;
    return true;
}

static bool xcryptographic_hash_buffer_copy(void** destData,
                                             size_t* destSize,
                                             size_t* destCapacity,
                                             const void* sourceData,
                                             size_t sourceSize)
{
    if (!destData || !destSize || !destCapacity ||
        (!sourceData && sourceSize > 0)) return false;
    *destData = NULL;
    *destSize = 0;
    *destCapacity = 0;
    if (sourceSize == 0) return true;
    if (!xcryptographic_hash_buffer_reserve(destData, destCapacity, sourceSize))
        return false;
    memcpy(*destData, sourceData, sourceSize);
    *destSize = sourceSize;
    return true;
}

// ==================== 生命周期、拷贝和移动 ====================

void XCryptographicHash_deinit(XCryptographicHash* hash)
{
    if (!hash || !hash->initialized) return;
    xcryptographic_hash_buffer_delete(&hash->buffer,
                                      &hash->bufferSize,
                                      &hash->bufferCapacity);
    xcryptographic_hash_buffer_delete(&hash->resultCache,
                                      &hash->resultCacheSize,
                                      &hash->resultCacheCapacity);
    hash->algorithm = 0;
    hash->finalized = 0;
    hash->initialized = 0;
    hash->totalLen = 0;
}

void XCryptographicHash_delete(XCryptographicHash* hash)
{
    if (!hash) return;
    XCryptographicHash_deinit(hash);
    xcryptographic_hash_free(hash);
}

void XCryptographicHash_copy(XCryptographicHash* dest,
                             const XCryptographicHash* src)
{
    void* buffer = NULL;
    void* resultCache = NULL;
    size_t bufferSize = 0;
    size_t bufferCapacity = 0;
    size_t resultCacheSize = 0;
    size_t resultCacheCapacity = 0;
    if (!dest || !src || dest == src || !src->initialized) return;

    if (!xcryptographic_hash_buffer_copy(&buffer, &bufferSize, &bufferCapacity,
                                         src->buffer, src->bufferSize)) return;
    if (!xcryptographic_hash_buffer_copy(&resultCache, &resultCacheSize,
                                         &resultCacheCapacity,
                                         src->resultCache, src->resultCacheSize)) {
        xcryptographic_hash_free(buffer);
        return;
    }

    XCryptographicHash_deinit(dest);
    dest->algorithm = src->algorithm;
    dest->finalized = src->finalized;
    dest->initialized = 1;
    dest->totalLen = src->totalLen;
    dest->buffer = buffer;
    dest->bufferSize = bufferSize;
    dest->bufferCapacity = bufferCapacity;
    dest->resultCache = resultCache;
    dest->resultCacheSize = resultCacheSize;
    dest->resultCacheCapacity = resultCacheCapacity;
}

void XCryptographicHash_move(XCryptographicHash* dest, XCryptographicHash* src)
{
    if (!dest || !src || dest == src || !src->initialized) return;
    XCryptographicHash_deinit(dest);
    *dest = *src;
    memset(src, 0, sizeof(*src));
}

void XCryptographicHash_init(XCryptographicHash* hash,
                             XCryptographicHash_Algorithm method)
{
    if (!hash) return;
    memset(hash, 0, sizeof(*hash));
    hash->algorithm = (uint64_t)method;
    hash->initialized = 1;
}

XCryptographicHash* XCryptographicHash_create(XCryptographicHash_Algorithm method)
{
    if (!XCryptographicHash_supportsAlgorithm(method)) return NULL;
    XCryptographicHash* hash = (XCryptographicHash*)xcryptographic_hash_malloc(
        sizeof(*hash));
    if (!hash) return NULL;
    XCryptographicHash_init(hash, method);
    if (!hash->initialized) {
        xcryptographic_hash_free(hash);
        return NULL;
    }
    return hash;
}

// ==================== 数据输入 ====================

void XCryptographicHash_addData(XCryptographicHash* hash, const char* data, size_t len)
{
    if (!hash || !hash->initialized || !data || len == 0 || hash->finalized) return;
    if (!xcryptographic_hash_buffer_append(&hash->buffer, &hash->bufferSize,
                                           &hash->bufferCapacity, data, len)) return;
    if (len > (((UINT64_C(1) << 57) - 1u) - hash->totalLen))
        hash->totalLen = (UINT64_C(1) << 57) - 1u;
    else
        hash->totalLen += len;
}

void XCryptographicHash_addData_2(XCryptographicHash* hash, XByteArrayView bytes)
{
    if (!hash || !bytes.m_data || bytes.m_size <= 0) return;
    XCryptographicHash_addData(hash, (const char*)bytes.m_data,
                               (size_t)bytes.m_size);
}

void XCryptographicHash_addData_3(XCryptographicHash* hash, const XByteArray* data)
{
    if (!hash || !hash->initialized || !data || hash->finalized) return;

    const char* d = (const char*)XByteArray_data((XByteArray*)data);
    size_t len = XByteArray_size_base(data);
    if (d && len > 0) XCryptographicHash_addData(hash, d, len);
}

bool XCryptographicHash_addData_4(XCryptographicHash* hash, XIODevice* device)
{
    if (!hash || !hash->initialized || !device || hash->finalized) return false;

    XByteArray* data = XIODevice_readAll_3(device);
    if (!data) return false;

    XCryptographicHash_addData_3(hash, data);
    XByteArray_delete_base(data);
    return true;
}

void XCryptographicHash_reset(XCryptographicHash* hash)
{
    if (!hash || !hash->initialized) return;
    hash->bufferSize = 0;
    hash->totalLen = 0;
    hash->finalized = false;
    hash->resultCacheSize = 0;
}

// ==================== 内部计算函数 ====================

static void xcryptographic_hash_store_u32(uint8_t* output, uint32_t value)
{
    output[0] = (uint8_t)(value & 0xffu);
    output[1] = (uint8_t)((value >> 8) & 0xffu);
    output[2] = (uint8_t)((value >> 16) & 0xffu);
    output[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void xcryptographic_hash_store_u64(uint8_t* output, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) {
        output[i] = (uint8_t)(value >> (i * 8));
    }
}

/*
 * 计算一个已经收集完输入的摘要。
 *
 * 快速哈希也属于 XCryptographicHash 的算法集合，因此在这里直接按枚举
 * 写入摘要。XCryptographicHash_value() 只服务于无状态容器回调，不能作为
 * 摘要计算的中间层；尤其是需要密钥的 SipHash 不能被无密钥接口伪造。
 */
static bool computeHash(XCryptographicHash* hash, uint8_t* output)
{
    static const uint8_t empty = 0;
    const char* data;
    size_t len;

    if (!hash || !output) return false;
    len = hash->bufferSize;
    data = hash->buffer ? (const char*)hash->buffer :
           (len == 0 ? (const char*)&empty : NULL);
    if (!data && len != 0) return false;

    switch (hash->algorithm) {
        case XCryptographicHash_Md4: {
            MD4_CTX ctx;
            md4_init(&ctx);
            md4_update(&ctx, (const uint8_t*)data, len);
            md4_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Md5: {
            MD5_CTX ctx;
            md5_init(&ctx);
            md5_update(&ctx, (const uint8_t*)data, len);
            md5_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Sha1: {
            SHA1_CTX ctx;
            sha1_init(&ctx);
            sha1_update(&ctx, (const uint8_t*)data, len);
            sha1_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Ripemd160: {
            RIPEMD160_CTX ctx;
            ripemd160_init(&ctx);
            ripemd160_update(&ctx, (const uint8_t*)data, len);
            ripemd160_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Sha224: {
            SHA256_CTX ctx;
            sha224_init(&ctx);
            sha256_update(&ctx, (const uint8_t*)data, len);
            sha256_final(&ctx, output, 28);
            return true;
        }
        case XCryptographicHash_Sha256: {
            SHA256_CTX ctx;
            sha256_init(&ctx);
            sha256_update(&ctx, (const uint8_t*)data, len);
            sha256_final(&ctx, output, 32);
            return true;
        }
        case XCryptographicHash_Sha384: {
            SHA512_CTX ctx;
            sha384_init(&ctx);
            sha512_update(&ctx, (const uint8_t*)data, len);
            sha512_final(&ctx, output, 48);
            return true;
        }
        case XCryptographicHash_Sha512: {
            SHA512_CTX ctx;
            sha512_init(&ctx);
            sha512_update(&ctx, (const uint8_t*)data, len);
            sha512_final(&ctx, output, 64);
            return true;
        }
        /* Keccak 算法（原始 Keccak，分隔符 0x01） */
        case XCryptographicHash_Keccak_224: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 28, false);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Keccak_256: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 32, false);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Keccak_384: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 48, false);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Keccak_512: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 64, false);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            return true;
        }
        /* SHA3 算法（NIST 标准化，分隔符 0x06） */
        case XCryptographicHash_RealSha3_224: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 28, true);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_RealSha3_256: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 32, true);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_RealSha3_384: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 48, true);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_RealSha3_512: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 64, true);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            return true;
        }
        /* Blake2b 算法 */
        case XCryptographicHash_Blake2b_160: {
            Blake2b_CTX ctx;
            blake2b_init(&ctx, 20);
            blake2b_update(&ctx, (const uint8_t*)data, len);
            blake2b_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Blake2b_256: {
            Blake2b_CTX ctx;
            blake2b_init(&ctx, 32);
            blake2b_update(&ctx, (const uint8_t*)data, len);
            blake2b_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Blake2b_384: {
            Blake2b_CTX ctx;
            blake2b_init(&ctx, 48);
            blake2b_update(&ctx, (const uint8_t*)data, len);
            blake2b_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Blake2b_512: {
            Blake2b_CTX ctx;
            blake2b_init(&ctx, 64);
            blake2b_update(&ctx, (const uint8_t*)data, len);
            blake2b_final(&ctx, output);
            return true;
        }
        /* Blake2s 算法 */
        case XCryptographicHash_Blake2s_128: {
            Blake2s_CTX ctx;
            blake2s_init(&ctx, 16);
            blake2s_update(&ctx, (const uint8_t*)data, len);
            blake2s_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Blake2s_160: {
            Blake2s_CTX ctx;
            blake2s_init(&ctx, 20);
            blake2s_update(&ctx, (const uint8_t*)data, len);
            blake2s_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Blake2s_224: {
            Blake2s_CTX ctx;
            blake2s_init(&ctx, 28);
            blake2s_update(&ctx, (const uint8_t*)data, len);
            blake2s_final(&ctx, output);
            return true;
        }
        case XCryptographicHash_Blake2s_256: {
            Blake2s_CTX ctx;
            blake2s_init(&ctx, 32);
            blake2s_update(&ctx, (const uint8_t*)data, len);
            blake2s_final(&ctx, output);
            return true;
        }
        /* 原 XHashFunc 的非加密算法，直接并入统一摘要 API。 */
        case XCryptographicHash_Murmur3_32:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_murmur3_32(data, len));
            return true;
        case XCryptographicHash_Fnv1a_64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_fnv1a_64(data, len));
            return true;
        case XCryptographicHash_Djb2:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_djb2(data, len));
            return true;
        case XCryptographicHash_Pearson:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_pearson(data, len));
            return true;
        case XCryptographicHash_Lookup3:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_lookup3(data, len));
            return true;
        case XCryptographicHash_CityHash64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_cityhash64(data, len));
            return true;
        case XCryptographicHash_FarmHash64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_farmhash64(data, len));
            return true;
        case XCryptographicHash_HighwayHash64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_highwayhash64(data, len));
            return true;
        case XCryptographicHash_XxHash64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_xxhash64(data, len));
            return true;
        case XCryptographicHash_WyHash:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_wyhash(data, len));
            return true;
        case XCryptographicHash_T1ha2:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_t1ha2(data, len));
            return true;
        case XCryptographicHash_SpookyHash64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_spookyhash64(data, len));
            return true;
        case XCryptographicHash_MetroHash64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_metrohash64(data, len));
            return true;
        case XCryptographicHash_MumHash:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_mumhash(data, len));
            return true;
        case XCryptographicHash_FastHash64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_fasthash64(data, len));
            return true;
        case XCryptographicHash_ThomasWang64:
            xcryptographic_hash_store_u64(
                output, xcryptographic_hash_thomaswang64(data, len));
            return true;
        case XCryptographicHash_OneAtATime:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_oneatatime(data, len));
            return true;
        case XCryptographicHash_SuperFastHash:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_superfasthash(data, len));
            return true;
        case XCryptographicHash_ElfHash:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_elfhash(data, len));
            return true;
        case XCryptographicHash_ApHash:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_aphash(data, len));
            return true;
        case XCryptographicHash_JsHash:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_jshash(data, len));
            return true;
        case XCryptographicHash_RsHash:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_rshash(data, len));
            return true;
        case XCryptographicHash_PjwHash:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_pjwhash(data, len));
            return true;
        case XCryptographicHash_BkdrHash:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_bkdrhash(data, len));
            return true;
        case XCryptographicHash_SdbmHash:
            xcryptographic_hash_store_u32(
                output, (uint32_t)xcryptographic_hash_sdbmhash(data, len));
            return true;
        default:
            /* SipHash-2-4 需要显式密钥，不能从通用无密钥 API 计算。 */
            return false;
    }
}

// ==================== 结果获取 ====================

XByteArray* XCryptographicHash_result(XCryptographicHash* hash)
{
    if (!hash || !hash->initialized) return NULL;

    int hashLen = XCryptographicHash_hashLength(hash->algorithm);
    if (hashLen <= 0) return NULL;
    XByteArray* result = XByteArray_create();
    if (!result) return NULL;

    XByteArray_resize_base(result, hashLen);
    char* data = (char*)XByteArray_data(result);

    if (!computeHash(hash, (uint8_t*)data)) {
        XByteArray_delete_base(result);
        return NULL;
    }
    hash->finalized = true;

    return result;
}

XByteArrayView XCryptographicHash_resultView(XCryptographicHash* hash)
{
    XByteArrayView empty = { NULL, 0 };
    if (!hash || !hash->initialized) return empty;

    int hashLen = XCryptographicHash_hashLength(hash->algorithm);
    if (hashLen <= 0) return empty;
    if (!xcryptographic_hash_buffer_resize(&hash->resultCache,
                                           &hash->resultCacheSize,
                                           &hash->resultCacheCapacity,
                                           (size_t)hashLen))
        return empty;
    char* data = (char*)hash->resultCache;

    if (!computeHash(hash, (uint8_t*)data)) return empty;
    hash->finalized = true;

    XByteArrayView view = { (const uint8_t*)data, (int64_t)hashLen };
    return view;
}

// ==================== 算法信息 ====================

XCryptographicHash_Algorithm XCryptographicHash_algorithm(const XCryptographicHash* hash)
{
    return hash && hash->initialized
               ? (XCryptographicHash_Algorithm)hash->algorithm
                : XCryptographicHash_Md5;
}

// ==================== 静态便捷方法 ====================

XByteArray* XCryptographicHash_hash(const char* data, size_t len, XCryptographicHash_Algorithm method)
{
    /* 注意：空字符串(len=0)的哈希是有效的，只有 data 为 NULL 时才返回 NULL */
    if (!data) return NULL;

    XCryptographicHash* ctx = XCryptographicHash_create(method);
    if (!ctx) return NULL;

    if (len > 0) {
        XCryptographicHash_addData(ctx, data, len);
    }
    XByteArray* result = XCryptographicHash_result(ctx);

    XCryptographicHash_delete(ctx);
    return result;
}

XByteArray* XCryptographicHash_hash_2(XByteArrayView data, XCryptographicHash_Algorithm method)
{
    return XCryptographicHash_hash(data.m_data, data.m_size, method);
}

XByteArray* XCryptographicHash_hash_3(const XByteArray* data, XCryptographicHash_Algorithm method)
{
    if (!data) return NULL;

    const char* d = (const char*)XByteArray_data((XByteArray*)data);
    size_t len = XByteArray_size_base((XByteArray*)data);
    return XCryptographicHash_hash(d, len, method);
}

XByteArrayView XCryptographicHash_hashInto(
    char* buffer, size_t bufferSize,
    const char* data, size_t dataLen,
    XCryptographicHash_Algorithm method
)
{
    XByteArrayView empty = { NULL, 0 };

    int hashLen = XCryptographicHash_hashLength(method);
    if (!buffer || bufferSize < (size_t)hashLen || !data) {
        return empty;
    }

    XCryptographicHash* ctx = XCryptographicHash_create(method);
    if (!ctx) return empty;

    XCryptographicHash_addData(ctx, data, dataLen);

    if (!computeHash(ctx, (uint8_t*)buffer)) {
        XCryptographicHash_delete(ctx);
        return empty;
    }

    XCryptographicHash_delete(ctx);

    XByteArrayView result = { (const uint8_t*)buffer, (int64_t)hashLen };
    return result;
}

XByteArrayView XCryptographicHash_hashInto_1(
    char* buffer, size_t bufferSize,
    const XByteArrayView* dataArray, size_t count,
    XCryptographicHash_Algorithm method
)
{
    XByteArrayView empty = { NULL, 0 };

    int hashLen = XCryptographicHash_hashLength(method);
    if (!buffer || bufferSize < (size_t)hashLen || !dataArray) {
        return empty;
    }

    XCryptographicHash* ctx = XCryptographicHash_create(method);
    if (!ctx) return empty;

    for (size_t i = 0; i < count; i++) {
        XCryptographicHash_addData_2(ctx, dataArray[i]);
    }

    if (!computeHash(ctx, (uint8_t*)buffer)) {
        XCryptographicHash_delete(ctx);
        return empty;
    }

    XCryptographicHash_delete(ctx);

    XByteArrayView result = { (const uint8_t*)buffer, (int64_t)hashLen };
    return result;
}

static bool xcryptographic_lms_sha256_parts(const XByteArrayView* parts,
                                            size_t count, uint8_t output[32])
{
    XCryptographicHash* hash;
    XByteArrayView result;
    size_t index;

    if (!parts || !output || count == 0) return false;
    hash = XCryptographicHash_create(XCryptographicHash_Sha256);
    if (!hash) return false;
    for (index = 0; index < count; ++index) {
        if (parts[index].m_size < 0 ||
            (!parts[index].m_data && parts[index].m_size != 0)) {
            XCryptographicHash_delete(hash);
            return false;
        }
        XCryptographicHash_addData_2(hash, parts[index]);
    }
    result = XCryptographicHash_resultView(hash);
    if (!result.m_data || result.m_size != 32) {
        XCryptographicHash_delete(hash);
        return false;
    }
    memcpy(output, result.m_data, 32);
    XCryptographicHash_delete(hash);
    return true;
}

static void xcryptographic_lms_put_u32_be(uint32_t value, uint8_t output[4])
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

bool XCryptographic_lmotsCalculatePublicKeyCandidate(
    XByteArrayView keyIdentifier, uint32_t leafIdentifier,
    XByteArrayView message, XByteArrayView signature,
    uint8_t* output, size_t outputSize)
{
    uint8_t qBytes[4];
    uint8_t iBytes[2];
    uint8_t jByte;
    uint8_t digest[32];
    uint8_t digits[34];
    uint8_t hashedDigits[34][32];
    XByteArrayView parts[6];
    const uint8_t domain81[2] = { 0x81, 0x81 };
    const uint8_t domain80[2] = { 0x80, 0x80 };
    size_t digitIndex;
    unsigned int hashIndex;

    if (keyIdentifier.m_size != 16) return false;
    if (message.m_size < 0 || signature.m_size != 1124 || outputSize < 32 ||
        (!message.m_data && message.m_size != 0) || !output ||
        signature.m_data == NULL) return false;
    if (signature.m_data[0] != 0 || signature.m_data[1] != 0 ||
        signature.m_data[2] != 0 || signature.m_data[3] != 4) return false;

    xcryptographic_lms_put_u32_be(leafIdentifier, qBytes);
    parts[0] = keyIdentifier;
    parts[1] = XByteArrayView_create_data(qBytes, 4);
    parts[2] = XByteArrayView_create_data(domain81, 2);
    parts[3] = XByteArrayView_create_data(signature.m_data + 4, 32);
    parts[4] = message;
    if (!xcryptographic_lms_sha256_parts(parts, 5, digest)) return false;

    memcpy(digits, digest, 32);
    {
        unsigned int checksum = 0;
        for (digitIndex = 0; digitIndex < 32; ++digitIndex) {
            checksum += 255u - digits[digitIndex];
        }
        digits[32] = (uint8_t)(checksum >> 8);
        digits[33] = (uint8_t)checksum;
    }

    for (digitIndex = 0; digitIndex < 34; ++digitIndex) {
        memcpy(digest, signature.m_data + 36 + digitIndex * 32, 32);
        for (hashIndex = digits[digitIndex]; hashIndex < 255; ++hashIndex) {
            xcryptographic_lms_put_u32_be(leafIdentifier, qBytes);
            iBytes[0] = (uint8_t)(digitIndex >> 8);
            iBytes[1] = (uint8_t)digitIndex;
            jByte = (uint8_t)hashIndex;
            parts[0] = keyIdentifier;
            parts[1] = XByteArrayView_create_data(qBytes, 4);
            parts[2] = XByteArrayView_create_data(iBytes, 2);
            parts[3] = XByteArrayView_create_data(&jByte, 1);
            parts[4] = XByteArrayView_create_data(digest, 32);
            if (!xcryptographic_lms_sha256_parts(parts, 5, digest)) return false;
        }
        memcpy(hashedDigits[digitIndex], digest, 32);
    }

    parts[0] = keyIdentifier;
    parts[1] = XByteArrayView_create_data(qBytes, 4);
    parts[2] = XByteArrayView_create_data(domain80, 2);
    parts[3] = XByteArrayView_create_data((const uint8_t*)hashedDigits, sizeof(hashedDigits));
    parts[4] = XByteArrayView_create_data(NULL, 0);
    return xcryptographic_lms_sha256_parts(parts, 4, output);
}

bool XCryptographic_lmsVerify(
    XByteArrayView keyIdentifier, XByteArrayView root,
    XByteArrayView message, XByteArrayView signature)
{
    uint8_t candidate[32];
    uint8_t node[32];
    uint8_t path[32];
    uint8_t indexBytes[4];
    uint8_t qBytes[4];
    XByteArrayView parts[6];
    const uint8_t domain82[2] = { 0x82, 0x82 };
    const uint8_t domain83[2] = { 0x83, 0x83 };
    uint32_t leafIdentifier;
    uint32_t currentNode;
    unsigned int height;

    if (keyIdentifier.m_size != 16 || root.m_size != 32 || signature.m_size != 1452 ||
        (!keyIdentifier.m_data && keyIdentifier.m_size != 0) ||
        (!root.m_data && root.m_size != 0) ||
        (!message.m_data && message.m_size != 0) || !signature.m_data) return false;
    if (signature.m_data[1128] != 0 || signature.m_data[1129] != 0 ||
        signature.m_data[1130] != 0 || signature.m_data[1131] != 6) return false;
    leafIdentifier = ((uint32_t)signature.m_data[0] << 24) |
                     ((uint32_t)signature.m_data[1] << 16) |
                     ((uint32_t)signature.m_data[2] << 8) |
                     signature.m_data[3];
    if (leafIdentifier >= 1024u) return false;
    if (!XCryptographic_lmotsCalculatePublicKeyCandidate(
            keyIdentifier, leafIdentifier, message,
            XByteArrayView_create_data(signature.m_data + 4, 1124), candidate, sizeof(candidate))) {
        return false;
    }

    currentNode = 1024u + leafIdentifier;
    xcryptographic_lms_put_u32_be(currentNode, qBytes);
    parts[0] = keyIdentifier;
    parts[1] = XByteArrayView_create_data(qBytes, 4);
    parts[2] = XByteArrayView_create_data(domain82, 2);
    parts[3] = XByteArrayView_create_data(candidate, 32);
    if (!xcryptographic_lms_sha256_parts(parts, 4, node)) return false;

    for (height = 0; height < 10; ++height) {
        uint32_t parent = currentNode / 2u;
        xcryptographic_lms_put_u32_be(parent, indexBytes);
        memcpy(path, signature.m_data + 1132 + height * 32, 32);
        parts[0] = keyIdentifier;
        parts[1] = XByteArrayView_create_data(indexBytes, 4);
        parts[2] = XByteArrayView_create_data(domain83, 2);
        parts[3] = (currentNode & 1u) ? XByteArrayView_create_data(path, 32) : XByteArrayView_create_data(node, 32);
        parts[4] = (currentNode & 1u) ? XByteArrayView_create_data(node, 32) : XByteArrayView_create_data(path, 32);
        if (!xcryptographic_lms_sha256_parts(parts, 5, node)) return false;
        currentNode = parent;
    }
    return memcmp(node, root.m_data, 32) == 0;
}

int XCryptographicHash_hashLength(XCryptographicHash_Algorithm method)
{
    if (method < 0 || method >= XCryptographicHash_NumAlgorithms) {
        return 0;
    }
    return g_hashLengths[method];
}

bool XCryptographicHash_supportsAlgorithm(XCryptographicHash_Algorithm method)
{
    /* SipHash-2-4 需要调用者提供密钥，不能作为无密钥流式哈希。 */
    return method >= 0 && method < XCryptographicHash_NumAlgorithms &&
           method != XCryptographicHash_SipHash24;
}

const char* XCryptographicHash_algorithmName(XCryptographicHash_Algorithm method)
{
    if (method < 0 || method >= XCryptographicHash_NumAlgorithms) {
        return "Unknown";
    }
    return g_algorithmNames[method];
}

// ==================== HMAC 实现 ====================

/**
 * @brief HMAC内部实现（RFC 2104）
 *
 * HMAC(K, m) = H((K ^ opad) || H((K ^ ipad) || m))
 *
 * 其中：
 * - K: 密钥（如果长度超过块大小，先进行哈希）
 * - m: 消息
 * - H: 哈希函数
 * - opad: 0x5c重复块大小次
 * - ipad: 0x36重复块大小次
 * - ||: 连接操作
 */

/**
 * @brief 获取算法的块大小（字节）
 * @param method 哈希算法
 * @return 块大小（字节）
 */
static int getBlockSize(XCryptographicHash_Algorithm method)
{
    switch (method) {
        case XCryptographicHash_Md4:
        case XCryptographicHash_Md5:
        case XCryptographicHash_Sha1:
        case XCryptographicHash_Ripemd160:
        case XCryptographicHash_Sha224:
        case XCryptographicHash_Sha256:
        case XCryptographicHash_Blake2s_128:
        case XCryptographicHash_Blake2s_160:
        case XCryptographicHash_Blake2s_224:
        case XCryptographicHash_Blake2s_256:
            return 64;  // 512 bits

        case XCryptographicHash_RealSha3_224:
        case XCryptographicHash_Keccak_224:
            return 144; // SHA-3/Keccak-224 海绵速率

        case XCryptographicHash_RealSha3_256:
        case XCryptographicHash_Keccak_256:
            return 136; // SHA-3/Keccak-256 海绵速率

        case XCryptographicHash_RealSha3_384:
        case XCryptographicHash_Keccak_384:
            return 104; // SHA-3/Keccak-384 海绵速率

        case XCryptographicHash_RealSha3_512:
        case XCryptographicHash_Keccak_512:
            return 72; // SHA-3/Keccak-512 海绵速率

        case XCryptographicHash_Sha384:
        case XCryptographicHash_Sha512:
        case XCryptographicHash_Blake2b_160:
        case XCryptographicHash_Blake2b_256:
        case XCryptographicHash_Blake2b_384:
        case XCryptographicHash_Blake2b_512:
            return 128; // 1024 bits

        default:
            return 0;
    }
}

bool XCryptographic_hmacSetup(XCryptographic_HmacOperation* operation,
                              XByteArrayView key,
                              XCryptographicHash_Algorithm algorithm)
{
    uint8_t ipad[144];
    XByteArrayView hashedKey;
    const uint8_t* keyData;
    size_t keySize;
    size_t i;
    int hashLength;
    int blockSize;

    if (!operation || key.m_size < 0 || (!key.m_data && key.m_size != 0)) {
        return false;
    }
    hashLength = XCryptographicHash_hashLength(algorithm);
    blockSize = getBlockSize(algorithm);
    if (hashLength <= 0 || hashLength > (int)sizeof(ipad) ||
        blockSize <= 0 || blockSize > (int)sizeof(operation->opad)) {
        return false;
    }

    memset(operation, 0, sizeof(*operation));
    keyData = key.m_data;
    keySize = (size_t)key.m_size;
    if (keySize > (size_t)blockSize) {
        hashedKey = XCryptographicHash_hashInto(
            (char*)ipad, sizeof(ipad), (const char*)keyData, keySize, algorithm);
        if (!hashedKey.m_data || hashedKey.m_size != hashLength) {
            memset(ipad, 0, sizeof(ipad));
            return false;
        }
        keyData = ipad;
        keySize = (size_t)hashLength;
    }

    operation->inner = XCryptographicHash_create(algorithm);
    if (!operation->inner) {
        memset(ipad, 0, sizeof(ipad));
        return false;
    }
    for (i = 0; i < (size_t)blockSize; ++i) {
        uint8_t value = i < keySize ? keyData[i] : 0;
        operation->opad[i] = value ^ 0x5cu;
        ipad[i] = value ^ 0x36u;
    }
    XCryptographicHash_addData(operation->inner, (const char*)ipad,
                               (size_t)blockSize);
    memset(ipad, 0, sizeof(ipad));
    operation->algorithm = algorithm;
    operation->blockSize = (size_t)blockSize;
    operation->active = true;
    return true;
}

bool XCryptographic_hmacUpdate(XCryptographic_HmacOperation* operation,
                               XByteArrayView message)
{
    if (!operation || !operation->active || !operation->inner ||
        message.m_size < 0 || (!message.m_data && message.m_size != 0)) {
        return false;
    }
    if (message.m_size != 0) {
        XCryptographicHash_addData(operation->inner, (const char*)message.m_data,
                                   (size_t)message.m_size);
    }
    return true;
}

XByteArrayView XCryptographic_hmacFinishInto(XCryptographic_HmacOperation* operation,
                                             char* buffer, size_t bufferSize)
{
    XByteArrayView empty = { NULL, 0 };
    XByteArrayView innerResult = { NULL, 0 };
    XByteArrayView outerResult = { NULL, 0 };
    XCryptographicHash* outer = NULL;
    int hashLength;

    if (!operation || !operation->active || !operation->inner || !buffer) {
        return empty;
    }
    hashLength = XCryptographicHash_hashLength(operation->algorithm);
    if (hashLength <= 0 || bufferSize < (size_t)hashLength) {
        return empty;
    }
    innerResult = XCryptographicHash_resultView(operation->inner);
    outer = XCryptographicHash_create(operation->algorithm);
    if (!innerResult.m_data || !outer || innerResult.m_size != hashLength) {
        goto cleanup;
    }
    XCryptographicHash_addData(outer, (const char*)operation->opad,
                               operation->blockSize);
    XCryptographicHash_addData(outer, (const char*)innerResult.m_data,
                               (size_t)hashLength);
    outerResult = XCryptographicHash_resultView(outer);
    if (!outerResult.m_data || outerResult.m_size != hashLength) {
        goto cleanup;
    }
    memcpy(buffer, outerResult.m_data, (size_t)hashLength);
    XCryptographic_hmacAbort(operation);
    XCryptographicHash_delete(outer);
    return XByteArrayView_create_data((const uint8_t*)buffer, hashLength);

cleanup:
    if (outer) XCryptographicHash_delete(outer);
    XCryptographic_hmacAbort(operation);
    return empty;
}

void XCryptographic_hmacAbort(XCryptographic_HmacOperation* operation)
{
    if (!operation) return;
    if (operation->inner) {
        XCryptographicHash_reset(operation->inner);
        XCryptographicHash_delete(operation->inner);
    }
    memset(operation, 0, sizeof(*operation));
}

XByteArrayView XCryptographicHash_hmacInto(
    char* buffer, size_t bufferSize,
    const char* key, size_t keyLen,
    const char* message, size_t msgLen,
    XCryptographicHash_Algorithm method
)
{
    XByteArrayView empty = { NULL, 0 };
    XCryptographic_HmacOperation operation;
    XByteArrayView result;
    XByteArrayView keyView;
    XByteArrayView messageView;
    int hashLen = XCryptographicHash_hashLength(method);

    if (!buffer || hashLen <= 0 || bufferSize < (size_t)hashLen ||
        (!key && keyLen != 0) || (!message && msgLen != 0) ||
        keyLen > (size_t)INT64_MAX || msgLen > (size_t)INT64_MAX) {
        return empty;
    }

    memset(&operation, 0, sizeof(operation));
    keyView = XByteArrayView_create_data((const uint8_t*)key, (int64_t)keyLen);
    messageView = XByteArrayView_create_data((const uint8_t*)message,
                                             (int64_t)msgLen);
    if (!XCryptographic_hmacSetup(&operation, keyView, method) ||
        !XCryptographic_hmacUpdate(&operation, messageView)) {
        XCryptographic_hmacAbort(&operation);
        return empty;
    }

    result = XCryptographic_hmacFinishInto(&operation, buffer, bufferSize);
    if (!result.m_data) XCryptographic_hmacAbort(&operation);
    return result;
}

XByteArray* XCryptographicHash_hmac(
    const char* key, size_t keyLen,
    const char* message, size_t msgLen,
    XCryptographicHash_Algorithm method
)
{
    int hashLen = XCryptographicHash_hashLength(method);
    if (hashLen <= 0 || (!key && keyLen != 0) ||
        (!message && msgLen != 0)) return NULL;
    XByteArray* result = XByteArray_create();
    if (!result) return NULL;

    XByteArray_resize_base(result, hashLen);
    char* data = (char*)XByteArray_data(result);

    XByteArrayView view = XCryptographicHash_hmacInto(data, hashLen, key, keyLen, message, msgLen, method);

    if (!view.m_data) {
        XByteArray_delete_base(result);
        return NULL;
    }

    return result;
}

XByteArray* XCryptographicHash_hmac_2(
    XByteArrayView key,
    XByteArrayView message,
    XCryptographicHash_Algorithm method
)
{
    return XCryptographicHash_hmac(key.m_data, key.m_size, message.m_data, message.m_size, method);
}

XByteArray* XCryptographicHash_hmac_3(
    const XByteArray* key,
    const XByteArray* message,
    XCryptographicHash_Algorithm method
)
{
    if (!key || !message) return NULL;

    const char* keyData = (const char*)XByteArray_data((XByteArray*)key);
    size_t keyLen = XByteArray_size_base((XByteArray*)key);
    const char* msgData = (const char*)XByteArray_data((XByteArray*)message);
    size_t msgLen = XByteArray_size_base((XByteArray*)message);

    return XCryptographicHash_hmac(keyData, keyLen, msgData, msgLen, method);
}
