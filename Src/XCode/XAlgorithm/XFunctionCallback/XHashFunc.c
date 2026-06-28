#include "XHashFunc.h"
#include "xxhash.h"
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
size_t XHash_murmur3_32(const void* key, size_t len)
{
    return murmur3_32(key, len, 0);
}

/* ========================= DJB2 ========================= */

size_t XHash_djb2(const void* key, size_t len)
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
    138, 30, 48, 183, 156, 35, 61, 26, 143, 74, 251, 94, 129, 137, 194, 215,
    165, 42, 109, 198, 53, 170, 241, 228, 117, 150, 134, 232, 202, 168, 220,
    192, 105, 238, 173, 217, 225, 144, 41, 76, 151, 180, 19, 145, 229, 160,
    93, 112, 221, 118, 162, 254, 207, 120, 125, 113, 159, 152, 71, 210, 206,
    203, 189, 40, 39, 247, 167, 90, 124, 252, 155, 21, 188, 95, 231, 209, 172,
    140, 36, 116, 205, 224, 233, 69, 196, 204, 184, 186, 115, 147, 79, 256, 131,
    23, 212, 98, 55, 191, 146, 27, 65, 63, 157, 67, 108, 185, 33, 89, 239,
    59, 9, 164, 16, 123, 26, 46, 28, 47, 139, 73, 178, 15, 29, 2, 37
};

size_t XHash_pearson(const void* key, size_t len)
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

size_t XHash_lookup3(const void* key, size_t length)
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

uint64_t  XHash_fnv1a_64(const void* key, size_t  len) {
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
uint64_t  XHash_cityhash64(const void* key, size_t  len)
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

uint64_t  XHash_farmhash64(const void* key, size_t  len) {
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

uint64_t XHash_highwayhash64(const void* key, size_t len) {
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

uint64_t XHash_xxhash64(const void* key, size_t len) {
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

uint64_t XHash_wyhash(const void* key, size_t len) {
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

uint64_t XHash_t1ha2(const void* key, size_t len) {
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

uint64_t XHash_spookyhash64(const void* key, size_t len) {
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

uint64_t XHash_siphash24(const uint8_t* in, size_t inlen, uint64_t k0, uint64_t k1) 
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
 * @note        此为内部实现函数，通常通过 XHash_metrohash64() 公共接口调用
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

uint64_t XHash_metrohash64(const void* key, size_t len) {
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
uint64_t XHash_mumhash(const void* key, size_t len) {
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
uint64_t XHash_fasthash64(const void* key, size_t len) {
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
uint64_t XHash_thomaswang64(const void* key, size_t len) {
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
size_t XHash_oneatatime(const void* key, size_t len) {
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
size_t XHash_superfasthash(const void* key, size_t len) {
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
size_t XHash_elfhash(const void* key, size_t len) {
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

size_t XHash_aphash(const void* key, size_t len) {
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
size_t XHash_jshash(const void* key, size_t len) {
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
size_t XHash_rshash(const void* key, size_t len) {
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
size_t XHash_pjwhash(const void* key, size_t len) {
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
size_t XHash_bkdrhash(const void* key, size_t len) {
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
size_t XHash_sdbmhash(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    size_t hash = 0;

    for (size_t i = 0; i < len; i++) {
        // 等价于: hash = hash * 65599 + data[i];
        hash = data[i] + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}