#include "XHashFunc.h"

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

size_t XHashMap_murmur3_32(const void* key, size_t len)
{
    return murmur3_32(key, len, 0);
}

/* ========================= FNV-1a ========================= */

size_t XHashMap_fnv1a_64(const void* key, size_t len) {
    const uint8_t* data = (const uint8_t*)key;
    uint64_t hash = 0xcbf29ce484222325ULL; // FNV偏移基数
    const uint64_t prime = 0x100000001b3ULL; // FNV素数

    for (size_t i = 0; i < len; i++) {
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
size_t XHashMap_cityhash64(const void* key, size_t len) 
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