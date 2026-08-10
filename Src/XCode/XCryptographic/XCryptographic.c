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
#include <string.h>
#include <stdlib.h>

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

static void keccak_f(uint64_t* state)
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
            keccak_f(ctx->state);
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
    keccak_f(ctx->state);

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

// ==================== 析构函数 ====================

static void VXCryptographicHash_deinit(XCryptographicHash* hash)
{
    if (!hash) return;

    if (hash->buffer) {
        XByteArray_delete_base(hash->buffer);
        hash->buffer = NULL;
    }

    if (hash->resultCache) {
        XByteArray_delete_base(hash->resultCache);
        hash->resultCache = NULL;
    }

    XClass_Deinit_Parent(XClass, hash);
}

// ==================== 拷贝/移动函数 ====================

static void VXCryptographicHash_copy(XCryptographicHash* dest, const XCryptographicHash* src)
{
    if (!dest || !src) return;

    // 先调用基类拷贝
    if (XClassIsVtableNull(dest))
        XCryptographicHash_init(dest, src->algorithm);

    XClass_Parent(XClass, EXClass_Copy, void(*)(XClass*, const XClass*))(dest, src);

    // 拷贝成员
    dest->algorithm = src->algorithm;
    dest->totalLen = src->totalLen;
    dest->finalized = src->finalized;

    // 深拷贝buffer
    if (src->buffer) {
        if (!dest->buffer)
            dest->buffer = XByteArray_create();
        if (dest->buffer)
            XByteArray_copy_base(dest->buffer, src->buffer);
    } else {
        if (dest->buffer) {
            XByteArray_delete_base(dest->buffer);
            dest->buffer = NULL;
        }
    }

    // 深拷贝resultCache
    if (src->resultCache) {
        if (!dest->resultCache)
            dest->resultCache = XByteArray_create();
        if (dest->resultCache)
            XByteArray_copy_base(dest->resultCache, src->resultCache);
    } else {
        if (dest->resultCache) {
            XByteArray_delete_base(dest->resultCache);
            dest->resultCache = NULL;
        }
    }
}

static void VXCryptographicHash_move(XCryptographicHash* dest, XCryptographicHash* src)
{
    if (!dest || !src) return;

    // 先调用基类移动
    if (XClassIsVtableNull(dest))
        XCryptographicHash_init(dest, src->algorithm);

    XClass_Parent(XClass, EXClass_Move, void(*)(XClass*, XClass*))(dest, src);

    // 移动成员
    dest->algorithm = src->algorithm;
    dest->buffer = src->buffer;
    dest->resultCache = src->resultCache;
    dest->totalLen = src->totalLen;
    dest->finalized = src->finalized;

    // 清空源对象
    src->buffer = NULL;
    src->resultCache = NULL;
    src->totalLen = 0;
    src->finalized = false;
}

// ==================== 虚函数表初始化 ====================

XVtable* XCryptographicHash_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCryptographicHash)
    XVTABLE_INHERIT_XCLASS(XClass);
    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCryptographicHash_deinit);
    // 重载拷贝函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXCryptographicHash_copy);
    // 重载移动函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXCryptographicHash_move);
    XCLASS_SHOW_SIZE_DEFAULT(XCryptographicHash);
    return XVTABLE_DEFAULT;
}

// ==================== 构造函数 ====================

void XCryptographicHash_init(XCryptographicHash* hash, XCryptographicHash_Algorithm method)
{
    if (!hash) return;

    XClass_init(&hash->base);
    XClassSetVtable(hash, XCryptographicHash);

    hash->algorithm = method;
    hash->buffer = XByteArray_create();
    hash->resultCache = NULL;
    hash->totalLen = 0;
    hash->finalized = false;
}

XCryptographicHash* XCryptographicHash_create(XCryptographicHash_Algorithm method)
{
    XCryptographicHash* hash = (XCryptographicHash*)XMalloc_System(sizeof(XCryptographicHash));
    if (!hash) return NULL;

    XCryptographicHash_init(hash, method);
    Set_Class_MemoryFree(hash, XFree_System);
    return hash;
}

// ==================== 数据输入 ====================

void XCryptographicHash_addData(XCryptographicHash* hash, const char* data, size_t len)
{
    if (!hash || !data || len == 0 || hash->finalized) return;

    XByteArray_push_back_2(hash->buffer, data, len);
    hash->totalLen += len;
}

void XCryptographicHash_addData_2(XCryptographicHash* hash, XByteArrayView bytes)
{
    if (!hash || !bytes.m_data || bytes.m_size == 0 || hash->finalized) return;

    XByteArray_push_back_2(hash->buffer, bytes.m_data, bytes.m_size);
    hash->totalLen += bytes.m_size;
}

void XCryptographicHash_addData_3(XCryptographicHash* hash, const XByteArray* data)
{
    if (!hash || !data || hash->finalized) return;

    const char* d = (const char*)XByteArray_data((XByteArray*)data);
    size_t len = XByteArray_size_base(data);
    if (d && len > 0) {
        XByteArray_push_back_2(hash->buffer, d, len);
        hash->totalLen += len;
    }
}

bool XCryptographicHash_addData_4(XCryptographicHash* hash, XIODevice* device)
{
    if (!hash || !device || hash->finalized) return false;

    XByteArray* data = XIODevice_readAll_3(device);
    if (!data) return false;

    XCryptographicHash_addData_3(hash, data);
    XByteArray_delete_base(data);
    return true;
}

void XCryptographicHash_reset(XCryptographicHash* hash)
{
    if (!hash) return;

    XByteArray_clear_base(hash->buffer);
    hash->totalLen = 0;
    hash->finalized = false;

    if (hash->resultCache) {
        XByteArray_clear_base(hash->resultCache);
    }
}

// ==================== 内部计算函数 ====================

static void computeHash(XCryptographicHash* hash, uint8_t* output)
{
    const char* data = XByteArray_data(hash->buffer);
    size_t len = XByteArray_size_base(hash->buffer);

    switch (hash->algorithm) {
        case XCryptographicHash_Md4: {
            MD4_CTX ctx;
            md4_init(&ctx);
            md4_update(&ctx, (const uint8_t*)data, len);
            md4_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Md5: {
            MD5_CTX ctx;
            md5_init(&ctx);
            md5_update(&ctx, (const uint8_t*)data, len);
            md5_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Sha1: {
            SHA1_CTX ctx;
            sha1_init(&ctx);
            sha1_update(&ctx, (const uint8_t*)data, len);
            sha1_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Sha224: {
            SHA256_CTX ctx;
            sha224_init(&ctx);
            sha256_update(&ctx, (const uint8_t*)data, len);
            sha256_final(&ctx, output, 28);
            break;
        }
        case XCryptographicHash_Sha256: {
            SHA256_CTX ctx;
            sha256_init(&ctx);
            sha256_update(&ctx, (const uint8_t*)data, len);
            sha256_final(&ctx, output, 32);
            break;
        }
        case XCryptographicHash_Sha384: {
            SHA512_CTX ctx;
            sha384_init(&ctx);
            sha512_update(&ctx, (const uint8_t*)data, len);
            sha512_final(&ctx, output, 48);
            break;
        }
        case XCryptographicHash_Sha512: {
            SHA512_CTX ctx;
            sha512_init(&ctx);
            sha512_update(&ctx, (const uint8_t*)data, len);
            sha512_final(&ctx, output, 64);
            break;
        }
        /* Keccak 算法（原始 Keccak，分隔符 0x01） */
        case XCryptographicHash_Keccak_224: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 28, false);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Keccak_256: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 32, false);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Keccak_384: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 48, false);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Keccak_512: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 64, false);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            break;
        }
        /* SHA3 算法（NIST 标准化，分隔符 0x06） */
        case XCryptographicHash_RealSha3_224: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 28, true);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            break;
        }
        case XCryptographicHash_RealSha3_256: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 32, true);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            break;
        }
        case XCryptographicHash_RealSha3_384: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 48, true);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            break;
        }
        case XCryptographicHash_RealSha3_512: {
            Keccak_CTX ctx;
            keccak_init(&ctx, 64, true);
            keccak_absorb(&ctx, (const uint8_t*)data, len);
            keccak_final(&ctx, output);
            break;
        }
        /* Blake2b 算法 */
        case XCryptographicHash_Blake2b_160: {
            Blake2b_CTX ctx;
            blake2b_init(&ctx, 20);
            blake2b_update(&ctx, (const uint8_t*)data, len);
            blake2b_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Blake2b_256: {
            Blake2b_CTX ctx;
            blake2b_init(&ctx, 32);
            blake2b_update(&ctx, (const uint8_t*)data, len);
            blake2b_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Blake2b_384: {
            Blake2b_CTX ctx;
            blake2b_init(&ctx, 48);
            blake2b_update(&ctx, (const uint8_t*)data, len);
            blake2b_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Blake2b_512: {
            Blake2b_CTX ctx;
            blake2b_init(&ctx, 64);
            blake2b_update(&ctx, (const uint8_t*)data, len);
            blake2b_final(&ctx, output);
            break;
        }
        /* Blake2s 算法 */
        case XCryptographicHash_Blake2s_128: {
            Blake2s_CTX ctx;
            blake2s_init(&ctx, 16);
            blake2s_update(&ctx, (const uint8_t*)data, len);
            blake2s_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Blake2s_160: {
            Blake2s_CTX ctx;
            blake2s_init(&ctx, 20);
            blake2s_update(&ctx, (const uint8_t*)data, len);
            blake2s_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Blake2s_224: {
            Blake2s_CTX ctx;
            blake2s_init(&ctx, 28);
            blake2s_update(&ctx, (const uint8_t*)data, len);
            blake2s_final(&ctx, output);
            break;
        }
        case XCryptographicHash_Blake2s_256: {
            Blake2s_CTX ctx;
            blake2s_init(&ctx, 32);
            blake2s_update(&ctx, (const uint8_t*)data, len);
            blake2s_final(&ctx, output);
            break;
        }
        default:
            memset(output, 0, 64);
            break;
    }
}

// ==================== 结果获取 ====================

XByteArray* XCryptographicHash_result(XCryptographicHash* hash)
{
    if (!hash) return NULL;

    int hashLen = XCryptographicHash_hashLength(hash->algorithm);
    XByteArray* result = XByteArray_create();
    if (!result) return NULL;

    XByteArray_resize_base(result, hashLen);
    char* data = (char*)XByteArray_data(result);

    computeHash(hash, (uint8_t*)data);
    hash->finalized = true;

    return result;
}

XByteArrayView XCryptographicHash_resultView(XCryptographicHash* hash)
{
    XByteArrayView empty = { NULL, 0 };
    if (!hash) return empty;

    int hashLen = XCryptographicHash_hashLength(hash->algorithm);

    if (!hash->resultCache) {
        hash->resultCache = XByteArray_create();
        if (!hash->resultCache) return empty;
    }

    XByteArray_resize_base(hash->resultCache, hashLen);
    char* data = (char*)XByteArray_data(hash->resultCache);

    computeHash(hash, (uint8_t*)data);
    hash->finalized = true;

    XByteArrayView view = { (const uint8_t*)data, (int64_t)hashLen };
    return view;
}

// ==================== 算法信息 ====================

XCryptographicHash_Algorithm XCryptographicHash_algorithm(const XCryptographicHash* hash)
{
    return hash ? hash->algorithm : XCryptographicHash_Md5;
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

    XCryptographicHash_delete_base(ctx);
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

    computeHash(ctx, (uint8_t*)buffer);

    XCryptographicHash_delete_base(ctx);

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

    computeHash(ctx, (uint8_t*)buffer);

    XCryptographicHash_delete_base(ctx);

    XByteArrayView result = { (const uint8_t*)buffer, (int64_t)hashLen };
    return result;
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
    /* 目前支持 MD4, MD5, SHA-1, SHA-224/256/384/512 */
    return method >= 0 && method < XCryptographicHash_NumAlgorithms;
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
        case XCryptographicHash_Sha224:
        case XCryptographicHash_Sha256:
        case XCryptographicHash_RealSha3_224:
        case XCryptographicHash_RealSha3_256:
        case XCryptographicHash_Keccak_224:
        case XCryptographicHash_Keccak_256:
        case XCryptographicHash_Blake2s_128:
        case XCryptographicHash_Blake2s_160:
        case XCryptographicHash_Blake2s_224:
        case XCryptographicHash_Blake2s_256:
            return 64;  // 512 bits

        case XCryptographicHash_Sha384:
        case XCryptographicHash_Sha512:
        case XCryptographicHash_RealSha3_384:
        case XCryptographicHash_RealSha3_512:
        case XCryptographicHash_Keccak_384:
        case XCryptographicHash_Keccak_512:
        case XCryptographicHash_Blake2b_160:
        case XCryptographicHash_Blake2b_256:
        case XCryptographicHash_Blake2b_384:
        case XCryptographicHash_Blake2b_512:
            return 128; // 1024 bits

        default:
            return 64;
    }
}

XByteArrayView XCryptographicHash_hmacInto(
    char* buffer, size_t bufferSize,
    const char* key, size_t keyLen,
    const char* message, size_t msgLen,
    XCryptographicHash_Algorithm method
)
{
    XByteArrayView empty = { NULL, 0 };

    int hashLen = XCryptographicHash_hashLength(method);
    int blockSize = getBlockSize(method);

    if (!buffer || bufferSize < (size_t)hashLen) {
        return empty;
    }

    // 分配内部缓冲区
    char* ipad = (char*)XMalloc_System(blockSize);
    char* opad = (char*)XMalloc_System(blockSize);
    char* kkey = (char*)XMalloc_System(blockSize);

    if (!ipad || !opad || !kkey) {
        if (ipad) XFree_System(ipad);
        if (opad) XFree_System(opad);
        if (kkey) XFree_System(kkey);
        return empty;
    }

    // 初始化kkey
    memset(kkey, 0, blockSize);

    // 如果密钥长度超过块大小，先进行哈希
    if (keyLen > (size_t)blockSize) {
        XByteArray* hashedKey = XCryptographicHash_hash(key, keyLen, method);
        if (hashedKey) {
            size_t hashedLen = XByteArray_size_base(hashedKey);
            const char* hashedData = XByteArray_data(hashedKey);
            if (hashedData && hashedLen <= (size_t)blockSize) {
                memcpy(kkey, hashedData, hashedLen);
            }
            XByteArray_delete_base(hashedKey);
        }
    } else if (keyLen > 0) {
        memcpy(kkey, key, keyLen);
    }

    // 准备ipad和opad
    for (int i = 0; i < blockSize; i++) {
        ipad[i] = kkey[i] ^ 0x36;
        opad[i] = kkey[i] ^ 0x5c;
    }

    // 计算内部哈希: H((K ^ ipad) || message)
    char* innerHash = (char*)XMalloc_System(hashLen);
    if (!innerHash) {
        XFree_System(ipad);
        XFree_System(opad);
        XFree_System(kkey);
        return empty;
    }

    {
        XCryptographicHash* ctx = XCryptographicHash_create(method);
        if (!ctx) {
            XFree_System(innerHash);
            XFree_System(ipad);
            XFree_System(opad);
            XFree_System(kkey);
            return empty;
        }

        XCryptographicHash_addData(ctx, ipad, blockSize);
        if (message && msgLen > 0) {
            XCryptographicHash_addData(ctx, message, msgLen);
        }

        XByteArray* innerResult = XCryptographicHash_result(ctx);
        if (innerResult) {
            const char* innerData = XByteArray_data(innerResult);
            if (innerData) {
                memcpy(innerHash, innerData, hashLen);
            }
            XByteArray_delete_base(innerResult);
        }

        XCryptographicHash_delete_base(ctx);
    }

    // 计算外部哈希: H((K ^ opad) || innerHash)
    {
        XCryptographicHash* ctx = XCryptographicHash_create(method);
        if (!ctx) {
            XFree_System(innerHash);
            XFree_System(ipad);
            XFree_System(opad);
            XFree_System(kkey);
            return empty;
        }

        XCryptographicHash_addData(ctx, opad, blockSize);
        XCryptographicHash_addData(ctx, innerHash, hashLen);

        XByteArray* outerResult = XCryptographicHash_result(ctx);
        if (outerResult) {
            const char* outerData = XByteArray_data(outerResult);
            if (outerData) {
                memcpy(buffer, outerData, hashLen);
            }
            XByteArray_delete_base(outerResult);
        }

        XCryptographicHash_delete_base(ctx);
    }

    // 清理
    XFree_System(innerHash);
    XFree_System(ipad);
    XFree_System(opad);
    XFree_System(kkey);

    XByteArrayView result = { (const uint8_t*)buffer, (int64_t)hashLen };
    return result;
}

XByteArray* XCryptographicHash_hmac(
    const char* key, size_t keyLen,
    const char* message, size_t msgLen,
    XCryptographicHash_Algorithm method
)
{
    int hashLen = XCryptographicHash_hashLength(method);
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
        /* When the 257th bit is set, reduce the wrapped value by adding
         * 2^256 - mod rather than subtracting mod a second time. */
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

static void xcryptographic_bn_const(XCryptographic_Bn* out,
                                         const uint8_t bytes[32])
{
    xcryptographic_bn_from_be(out, bytes);
}

typedef struct XCryptographic_EcPoint {
    XCryptographic_Bn x;
    XCryptographic_Bn y;
    XCryptographic_Bn z;
    bool infinity;
} XCryptographic_EcPoint;

static void xcryptographic_p256_modulus(XCryptographic_Bn* out)
{
    xcryptographic_bn_const(out, xcryptographic_p256_prime_bytes);
}

static void xcryptographic_p256_order(XCryptographic_Bn* out)
{
    xcryptographic_bn_const(out, xcryptographic_p256_order_bytes);
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

    /* Jacobian doubling for y^2 = x^3 - 3x + b. */
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

static void xcryptographic_p256_affine_double(
    XCryptographic_EcPoint* out, const XCryptographic_EcPoint* in,
    const XCryptographic_Bn* prime)
{
    XCryptographic_Bn x2, numerator, denominator, inverse, lambda;
    XCryptographic_Bn x3, y3, t;
    if (in->infinity || xcryptographic_bn_is_zero(&in->y)) {
        xcryptographic_p256_point_infinity(out);
        return;
    }
    xcryptographic_bn_mul_mod(&x2, &in->x, &in->x, prime);
    xcryptographic_bn_add_mod(&numerator, &x2, &x2, prime);
    xcryptographic_bn_add_mod(&numerator, &numerator, &x2, prime);
    xcryptographic_bn_sub_mod(&numerator, &numerator, &(XCryptographic_Bn){{3,0,0,0,0,0,0,0}}, prime);
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

static void xcryptographic_p256_serialize(const XCryptographic_EcPoint* point,
                                               uint8_t output[65])
{
    output[0] = 4;
    xcryptographic_bn_to_be(&point->x, output + 1);
    xcryptographic_bn_to_be(&point->y, output + 33);
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

bool XCryptographic_ecdhGenerateKey(XCryptographic_EcdhAlgorithm algorithm,
                                        XCryptographic_Key* result)
{
    XCryptographic_EcPoint base, publicPoint;
    uint8_t baseU[32] = { 9 };
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    if (algorithm == XCryptographic_EcdhAlgorithm_X25519) {
        if (!XCRYPTOGRAPHIC_X25519_ON ||
            !xcryptographic_random(result->privateKey, sizeof(result->privateKey)) ||
            !xcryptographic_x25519(result->privateKey, baseU, result->publicKey))
            return false;
        result->type = XCryptographic_KeyType_X25519;
        result->publicKeyLen = 32;
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
    if (!peerPublicKey || !output || !outputLen || outputCap < 32) return false;
    if (privateKey.type == XCryptographic_KeyType_X25519) {
        if (peerPublicKeyLen != 32 ||
            !xcryptographic_x25519(privateKey.privateKey,
                                         (const uint8_t*)peerPublicKey,
                                         (uint8_t*)output)) return false;
        *outputLen = 32;
        return true;
    }
    if (privateKey.type == XCryptographic_KeyType_EcdhNistP256) {
        XCryptographic_EcPoint peer, shared;
        XCryptographic_Bn scalar;
        if (!xcryptographic_p256_parse((const uint8_t*)peerPublicKey,
                                            peerPublicKeyLen, &peer)) return false;
        xcryptographic_bn_from_be(&scalar, privateKey.privateKey);
        xcryptographic_p256_scalar_mul(&shared, &peer, &scalar);
        if (!xcryptographic_p256_to_affine(&shared)) return false;
        xcryptographic_bn_to_be(&shared.x, (uint8_t*)output);
        *outputLen = 32;
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

bool XCryptographic_ecdsaP256GenerateKey(XCryptographic_Key* result)
{
    uint8_t privateKey[32];
    if (!result || !xcryptographic_random_scalar(privateKey)) return false;
    return xcryptographic_ecdsa_p256_import_private_key(privateKey, 32, result);
}

static bool xcryptographic_ecdsa_p256_export_private_key(
    XCryptographic_Key key, void* output, size_t outputCap, size_t* outputLen)
{
    if (!output || !outputLen || outputCap < 32 ||
        key.type != XCryptographic_KeyType_EcdsaNistP256Private) return false;
    memcpy(output, key.privateKey, 32);
    *outputLen = 32;
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

void XCryptographic_destroyKey(XCryptographic_Key* key)
{
    if (key) memset(key, 0, sizeof(*key));
}

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
        if (result) XByteArray_delete_base(result);
        return NULL;
    }
    view = XCryptographic_aesCtrUpdateInto(operation,
        (char*)XByteArray_data(result), (size_t)data.m_size, data);
    if (!view.m_data) {
        XByteArray_delete_base(result);
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
        if (result) XByteArray_delete_base(result);
        return NULL;
    }
    view = XCryptographic_exportPublicKeyInto((char*)XByteArray_data(result),
                                                   key.publicKeyLen, key);
    if (!view.m_data) {
        XByteArray_delete_base(result);
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
    XByteArray* result = XByteArray_create();
    XByteArrayView view;
    if (!result || !XByteArray_resize_base(result, 32)) {
        if (result) XByteArray_delete_base(result);
        return NULL;
    }
    view = XCryptographic_ecdhAgreeInto((char*)XByteArray_data(result), 32,
                                             privateKey, peerPublicKey);
    if (!view.m_data) {
        XByteArray_delete_base(result);
        return NULL;
    }
    return result;
}

bool XCryptographic_ecdsaP256ImportPrivateKey(XByteArrayView key,
                                                  XCryptographic_Key* result)
{
    return key.m_size >= 0 && xcryptographic_ecdsa_p256_import_private_key(
        key.m_data, (size_t)key.m_size, result);
}

XByteArrayView XCryptographic_ecdsaP256ExportPrivateKeyInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key)
{
    size_t outputLen = 0;
    XByteArrayView empty = { NULL, 0 };
    if (!xcryptographic_ecdsa_p256_export_private_key(key, buffer, bufferSize,
                                                            &outputLen))
        return empty;
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)outputLen };
        return result;
    }
}

XByteArray* XCryptographic_ecdsaP256ExportPrivateKey(XCryptographic_Key key)
{
    XByteArray* result = XByteArray_create();
    XByteArrayView view;
    if (!result || !XByteArray_resize_base(result, 32)) {
        if (result) XByteArray_delete_base(result);
        return NULL;
    }
    view = XCryptographic_ecdsaP256ExportPrivateKeyInto(
        (char*)XByteArray_data(result), 32, key);
    if (!view.m_data) {
        XByteArray_delete_base(result);
        return NULL;
    }
    return result;
}

bool XCryptographic_ecdsaP256ImportPublicKey(XByteArrayView key,
                                                 XCryptographic_Key* result)
{
    return key.m_size >= 0 && xcryptographic_ecdsa_p256_import_public_key(
        key.m_data, (size_t)key.m_size, result);
}

XByteArrayView XCryptographic_ecdsaP256SignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash)
{
    size_t signatureLen = 0;
    XByteArrayView empty = { NULL, 0 };
    if (hash.m_size < 0 || !xcryptographic_ecdsa_p256_sign_hash(
            key, hash.m_data, (size_t)hash.m_size, buffer, bufferSize,
            &signatureLen)) return empty;
    {
        XByteArrayView result = { (const uint8_t*)buffer, (int64_t)signatureLen };
        return result;
    }
}

XByteArray* XCryptographic_ecdsaP256SignHash(XCryptographic_Key key,
                                                  XByteArrayView hash)
{
    XByteArray* result = XByteArray_create();
    XByteArrayView view;
    if (!result || !XByteArray_resize_base(result, 64)) {
        if (result) XByteArray_delete_base(result);
        return NULL;
    }
    view = XCryptographic_ecdsaP256SignHashInto((char*)XByteArray_data(result),
                                                     64, key, hash);
    if (!view.m_data) {
        XByteArray_delete_base(result);
        return NULL;
    }
    return result;
}

bool XCryptographic_ecdsaP256VerifyHash(XCryptographic_Key key,
                                            XByteArrayView hash,
                                            XByteArrayView signature)
{
    return hash.m_size >= 0 && signature.m_size >= 0 &&
           xcryptographic_ecdsa_p256_verify_hash(
               key, hash.m_data, (size_t)hash.m_size,
               signature.m_data, (size_t)signature.m_size);
}
