/* crc32.c -- 计算数据流的CRC-32校验值
 * 版权所有 (C) 1995-2006、2010、2011、2012、2016 Mark Adler
 * 有关发布和使用的条件，请参见 zlib.h 中的版权声明
 *
 * 感谢 Rodney Brown <rbrown64@csc.com.au> 贡献的更快CRC计算方法：
 * 1. 每次对32位数据进行异或操作；2. 预计算表格，通过3次异或操作（而非4次操作4次异或）
 * 实现移位寄存器的一步更新。在使用 gcc -O3 编译的 Power PC G4（PPC7455）处理器上，
 * 此方法可使速度提升约一倍。
 */

 /* @(#) $Id$ */

 /*
   关于 DYNAMIC_CRC_TABLE 的使用说明：用于控制首次生成CRC表格的静态变量
   未提供互斥锁或信号量保护。因此，若你定义了 DYNAMIC_CRC_TABLE，
   应在允许多个线程使用 crc32() 之前，先调用 get_crc_table() 初始化表格。

   可通过定义 DYNAMIC_CRC_TABLE 和 MAKECRCH 来生成 crc32.h 文件。
  */

#ifdef MAKECRCH
#  include <stdio.h>
#  ifndef DYNAMIC_CRC_TABLE
#    define DYNAMIC_CRC_TABLE  // 启用动态CRC表格（生成头文件时必需）
#  endif /* !DYNAMIC_CRC_TABLE */
#endif /* MAKECRCH */

#include "zutil.h"      /* 用于 STDC 和 FAR 宏定义 */

  /* 定义每次处理4个数据字节的CRC计算相关配置 */
#if !defined(NOBYFOUR) && defined(Z_U4)  // 未禁用4字节处理且支持Z_U4类型
#  define BYFOUR  // 启用4字节批量处理模式
#endif
#ifdef BYFOUR
   // 本地函数：小端模式下4字节批量计算CRC-32
local unsigned long crc32_little OF((unsigned long, const unsigned char FAR*, z_size_t));
// 本地函数：大端模式下4字节批量计算CRC-32
local unsigned long crc32_big OF((unsigned long, const unsigned char FAR*, z_size_t));
#  define TBLS 8  // 4字节模式下需8个CRC子表格
#else
#  define TBLS 1  // 单字节模式下仅需1个CRC表格
#endif /* BYFOUR */

/* 用于CRC拼接的本地函数声明 */
// 本地函数：GF(2)域（二元域）中矩阵与向量的乘法运算
local unsigned long gf2_matrix_times OF((unsigned long* mat, unsigned long vec));
// 本地函数：GF(2)域中矩阵的平方运算（用于生成CRC拼接所需的算子）
local void gf2_matrix_square OF((unsigned long* square, unsigned long* mat));
// 本地函数：CRC-32拼接的核心实现（处理64位长度）
local uLong crc32_combine_ OF((uLong crc1, uLong crc2, z_off64_t len2));


#ifdef DYNAMIC_CRC_TABLE  // 动态生成CRC表格（而非使用预编译表格）

local volatile int crc_table_empty = 1;  // CRC表格未初始化标记（1=未初始化）
local z_crc_t FAR crc_table[TBLS][256];  // 动态生成的CRC表格（TBLS个256字节子表）
local void make_crc_table OF((void));    // 生成CRC表格的本地函数声明
#ifdef MAKECRCH
// 本地函数：将CRC表格写入文件（生成crc32.h时使用）
local void write_table OF((FILE*, const z_crc_t FAR*));
#endif /* MAKECRCH */

/*
  生成用于字节级32位CRC计算的表格，所使用的多项式为：
  x³² + x²⁶ + x²³ + x²² + x¹⁶ + x¹² + x¹¹ + x¹⁰ + x⁸ + x⁷ + x⁵ + x⁴ + x² + x + 1

  GF(2)域中的多项式以二进制表示，每个比特对应一个系数，最低次幂对应最高有效位。
  多项式加法即为异或操作，多项式乘以x等价于右移1位。若将上述多项式记为p，
  字节数据记为多项式q（同样以最低次幂对应最高有效位，例如字节0xb1对应多项式x⁷+x³+x+1），
  则CRC值为 (q×x³²) mod p（mod表示多项式除法后的余数）。

  此计算通过“移位寄存器法”实现：寄存器初始化为0，对于每个输入比特，
  若比特为1，则将寄存器与x³² mod p（即p+x³² = x²⁶+...+1）异或；
  之后将寄存器乘以x（即右移1位，若移出比特为1，则再与x³² mod p异或）。
  对q的8个比特，从最高次幂（最低有效位）开始重复上述操作。

  第一个表格存储所有8位数值的CRC值，仅需此表格即可实现“逐字节”CRC计算。
  其余表格用于“逐字（4字节）”CRC计算，适配大端/小端机器。
*/
local void make_crc_table()
{
    z_crc_t c;                  // 临时存储CRC计算结果
    int n, k;                   // 循环变量
    z_crc_t poly;               // 多项式的异或模式（二进制表示）
    // 标记：确保仅一个线程执行表格生成（非线程安全，但能降低并发风险）
    static volatile int first = 1;
    // CRC多项式的非x³²项对应的比特位（多项式x³²+...+1中，系数为1的次幂）
    static const unsigned char p[] = { 0,1,2,4,5,7,8,10,11,12,16,22,23,26 };

    /* 检查是否已有其他任务正在生成表格（非线程安全，但聊胜于无）
       若忽略DYNAMIC_CRC_TABLE的使用建议，此逻辑可缩短并发风险窗口 */
    if (first) {
        first = 0;  // 标记为已有任务在生成表格

        /* 根据多项式生成异或模式（最终结果为0xedb88320UL）*/
        poly = 0;
        for (n = 0; n < (int)(sizeof(p) / sizeof(unsigned char)); n++) {
            // 将多项式中系数为1的次幂对应的比特置1（31-p[n]是因为比特位与次幂反向对应）
            poly |= (z_crc_t)1 << (31 - p[n]);
        }

        /* 生成所有8位数值（0~255）的CRC值，存入第一个子表格 */
        for (n = 0; n < 256; n++) {
            c = (z_crc_t)n;  // 初始化为当前8位数值
            for (k = 0; k < 8; k++) {
                // 移位寄存器计算：若最低位为1，异或多项式后右移；否则直接右移
                c = c & 1 ? poly ^ (c >> 1) : c >> 1;
            }
            crc_table[0][n] = c;  // 存入表格
        }

#ifdef BYFOUR  // 若启用4字节批量处理，生成剩余7个辅助表格
        /* 生成以下场景的CRC值并存入表格：
           1. 每个8位数值后接1个、2个、3个零字节的CRC；
           2. 上述CRC值的字节反转（适配大端模式） */
        for (n = 0; n < 256; n++) {
            c = crc_table[0][n];          // 取基础CRC值
            crc_table[4][n] = ZSWAP32(c); // 字节反转后存入表格4（大端基础表）

            // 生成“后接k个零字节”的CRC（k=1~3）
            for (k = 1; k < 4; k++) {
                // 计算“当前CRC后接1个零字节”的CRC（等价于CRC右移8位后异或对应表格值）
                c = crc_table[0][c & 0xff] ^ (c >> 8);
                crc_table[k][n] = c;          // 存入表格k（小端辅助表）
                crc_table[k + 4][n] = ZSWAP32(c); // 字节反转后存入表格k+4（大端辅助表）
            }
        }
#endif /* BYFOUR */

        crc_table_empty = 0;  // 标记CRC表格已初始化完成
    }
    else {  // 已有其他任务在生成表格，等待其完成
        while (crc_table_empty)
            ;  // 空循环等待（效率低，但极少触发）
    }

#ifdef MAKECRCH  // 若启用表格生成模式，将CRC表格写入crc32.h文件
    {
        FILE* out;  // 文件指针

        out = fopen("crc32.h", "w");  // 打开文件（覆盖写入）
        if (out == NULL) return;      // 打开失败则返回

        // 写入文件头部注释
        fprintf(out, "/* crc32.h -- 用于快速CRC计算的表格\n");
        fprintf(out, " * 由 crc32.c 自动生成\n */\n\n");
        // 写入表格变量声明
        fprintf(out, "local const z_crc_t FAR ");
        fprintf(out, "crc_table[TBLS][256] =\n{\n  {\n");
        write_table(out, crc_table[0]);  // 写入第一个子表格

#  ifdef BYFOUR  // 若启用4字节模式，写入剩余子表格
        fprintf(out, "#ifdef BYFOUR\n");
        for (k = 1; k < 8; k++) {
            fprintf(out, "  },\n  {\n");
            write_table(out, crc_table[k]);  // 写入第k个子表格
        }
        fprintf(out, "#endif\n");
#  endif /* BYFOUR */

        // 写入表格结束符
        fprintf(out, "  }\n};\n");
        fclose(out);  // 关闭文件
    }
#endif /* MAKECRCH */
}

#ifdef MAKECRCH
// 本地函数：将单个CRC子表格写入文件（格式化输出）
local void write_table(out, table)
FILE* out;                  // 目标文件指针
const z_crc_t FAR* table;   // 待写入的CRC子表格
{
    int n;

    for (n = 0; n < 256; n++) {
        // 格式化输出：每5个值换行，末尾加逗号（最后一个值除外）
        fprintf(out, "%s0x%08lxUL%s",
            n % 5 ? "" : "    ",  // 每5个值缩进一次
            (unsigned long)(table[n]),  // CRC值（转为无符号长整数）
            n == 255 ? "\n" : (n % 5 == 4 ? ",\n" : ", ")  // 换行/逗号控制
        );
    }
}
#endif /* MAKECRCH */

#else /* !DYNAMIC_CRC_TABLE  // 使用预编译的CRC表格（而非动态生成）*/
/* ========================================================================
 * 所有单字节值的CRC-32表格，由 make_crc_table() 生成并存储在 crc32.h 中
 */
#include "crc32.h"
#endif /* DYNAMIC_CRC_TABLE */

 /* =========================================================================
  * 此函数可被 crc32() 的汇编实现调用（用于获取CRC表格地址）
  */
const z_crc_t FAR* ZEXPORT get_crc_table()
{
#ifdef DYNAMIC_CRC_TABLE
    if (crc_table_empty)  // 若表格未初始化，先生成
        make_crc_table();
#endif /* DYNAMIC_CRC_TABLE */
    return (const z_crc_t FAR*)crc_table;  // 返回CRC表格地址
}

/* ========================================================================= */
// 宏定义：逐字节计算CRC（单次处理1字节）
#define DO1 crc = crc_table[0][((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8)
// 宏定义：批量处理8字节CRC（调用8次DO1）
#define DO8 DO1; DO1; DO1; DO1; DO1; DO1; DO1; DO1

/* =========================================================================
 * 函数：crc32_z
 * 功能：计算数据流的CRC-32校验值（支持64位长度参数 z_size_t）
 * 参数：
 *   crc  - 初始CRC值（首次调用通常为0）
 *   buf  - 待计算CRC的数据流指针（非空）
 *   len  - 数据流长度（字节数）
 * 返回值：计算后的CRC-32校验值
 */
unsigned long ZEXPORT crc32_z(crc, buf, len)
unsigned long crc;
const unsigned char FAR* buf;
z_size_t len;
{
    if (buf == Z_NULL) return 0UL;  // 空指针输入，返回0

#ifdef DYNAMIC_CRC_TABLE
    if (crc_table_empty)  // 确保CRC表格已初始化
        make_crc_table();
#endif /* DYNAMIC_CRC_TABLE */

#ifdef BYFOUR  // 若启用4字节批量处理，先判断机器字节序
    // 检查指针大小是否与 ptrdiff_t 一致（确保地址对齐判断有效）
    if (sizeof(void*) == sizeof(ptrdiff_t)) {
        z_crc_t endian;  // 用于判断字节序的临时变量

        endian = 1;
        // 小端机器：低地址存储低字节（*((unsigned char*)&endian) == 1）
        if (*((unsigned char*)(&endian)))
            return crc32_little(crc, buf, len);
        // 大端机器：低地址存储高字节
        else
            return crc32_big(crc, buf, len);
    }
#endif /* BYFOUR */

    // 标准逐字节CRC计算：先对初始CRC值取反
    crc = crc ^ 0xffffffffUL;
    // 批量处理8字节数据（提升效率）
    while (len >= 8) {
        DO8;
        len -= 8;
    }
    // 处理剩余不足8字节的数据
    if (len) do {
        DO1;
    } while (--len);
    // 对最终结果取反后返回（CRC-32标准计算步骤）
    return crc ^ 0xffffffffUL;
}

/* =========================================================================
 * 函数：crc32
 * 功能：计算数据流的CRC-32校验值（兼容旧接口，长度参数为16位 uInt）
 * 参数：
 *   crc  - 初始CRC值
 *   buf  - 待计算CRC的数据流指针
 *   len  - 数据流长度（字节数，uInt类型）
 * 返回值：计算后的CRC-32校验值
 */
unsigned long ZEXPORT crc32(crc, buf, len)
unsigned long crc;
const unsigned char FAR* buf;
uInt len;
{
    return crc32_z(crc, buf, len);  // 调用支持64位长度的 crc32_z 实现
}

#ifdef BYFOUR  // 4字节批量处理模式的实现

/*
   注意：BYFOUR 模式下，通过32位整数指针访问传入的 unsigned char* 缓冲区，
   这违反了“严格别名规则”（编译器可能假设不同类型的指针不会指向同一块内存）。
   但此代码仅读取缓冲区（不写入），只要此代码独立在当前编译单元中，就不会产生问题。
   因此，请勿将此代码复制到其他编译单元（尤其是存在缓冲区写入操作的单元）。
 */

 /* ========================================================================= */
 // 宏定义：小端模式下批量处理4字节CRC（单次处理1个32位字）
#define DOLIT4 c ^= *buf4++; \
        c = crc_table[3][c & 0xff] ^ crc_table[2][(c >> 8) & 0xff] ^ \
            crc_table[1][(c >> 16) & 0xff] ^ crc_table[0][c >> 24]
// 宏定义：小端模式下批量处理32字节CRC（调用8次DOLIT4）
#define DOLIT32 DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4; DOLIT4

/* =========================================================================
 * 本地函数：crc32_little
 * 功能：小端机器上批量计算CRC-32（每次处理4字节）
 * 参数：
 *   crc  - 初始CRC值
 *   buf  - 待计算CRC的数据流指针
 *   len  - 数据流长度（字节数）
 * 返回值：计算后的CRC-32校验值
 */
local unsigned long crc32_little(crc, buf, len)
unsigned long crc;
const unsigned char FAR* buf;
z_size_t len;
{
    register z_crc_t c;  // 寄存器变量：存储当前CRC值（提升访问速度）
    // 寄存器变量：指向4字节对齐的缓冲区（批量处理用）
    register const z_crc_t FAR* buf4;

    c = (z_crc_t)crc;
    c = ~c;  // 初始CRC值取反（CRC-32标准步骤）

    /* 处理缓冲区起始的非4字节对齐部分（逐字节处理）*/
    while (len && ((ptrdiff_t)buf & 3)) {  // (ptrdiff_t)buf & 3 检查低2位是否为0（4字节对齐）
        c = crc_table[0][(c ^ *buf++) & 0xff] ^ (c >> 8);
        len--;
    }

    // 将缓冲区指针转为4字节对齐的指针（批量处理）
    buf4 = (const z_crc_t FAR*)(const void FAR*)buf;
    // 批量处理32字节数据（8个4字节字）
    while (len >= 32) {
        DOLIT32;
        len -= 32;
    }
    // 批量处理剩余的4字节块
    while (len >= 4) {
        DOLIT4;
        len -= 4;
    }
    // 恢复缓冲区指针为原始char*类型（处理剩余字节）
    buf = (const unsigned char FAR*)buf4;

    /* 处理最后不足4字节的数据（逐字节处理）*/
    if (len) do {
        c = crc_table[0][(c ^ *buf++) & 0xff] ^ (c >> 8);
    } while (--len);

    c = ~c;  // 最终CRC值取反
    return (unsigned long)c;
}

/* ========================================================================= */
// 宏定义：大端模式下批量处理4字节CRC（单次处理1个32位字）
#define DOBIG4 c ^= *buf4++; \
        c = crc_table[4][c & 0xff] ^ crc_table[5][(c >> 8) & 0xff] ^ \
            crc_table[6][(c >> 16) & 0xff] ^ crc_table[7][c >> 24]
// 宏定义：大端模式下批量处理32字节CRC（调用8次DOBIG4）
#define DOBIG32 DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4; DOBIG4

/* =========================================================================
 * 本地函数：crc32_big
 * 功能：大端机器上批量计算CRC-32（每次处理4字节）
 * 参数：
 *   crc  - 初始CRC值
 *   buf  - 待计算CRC的数据流指针
 *   len  - 数据流长度（字节数）
 * 返回值：计算后的CRC-32校验值
 */
local unsigned long crc32_big(crc, buf, len)
unsigned long crc;
const unsigned char FAR* buf;
z_size_t len;
{
    register z_crc_t c;  // 寄存器变量：存储当前CRC值
    // 寄存器变量：指向4字节对齐的缓冲区（批量处理用）
    register const z_crc_t FAR* buf4;

    // 大端模式需先反转初始CRC值的字节顺序
    c = ZSWAP32((z_crc_t)crc);
    c = ~c;  // 初始CRC值取反

    /* 处理缓冲区起始的非4字节对齐部分（逐字节处理）*/
    while (len && ((ptrdiff_t)buf & 3)) {
        // 大端模式：使用表格4（字节反转后的基础表），CRC左移8位
        c = crc_table[4][(c >> 24) ^ *buf++] ^ (c << 8);
        len--;
    }

    // 将缓冲区指针转为4字节对齐的指针（批量处理）
    buf4 = (const z_crc_t FAR*)(const void FAR*)buf;
    // 批量处理32字节数据（8个4字节字）
    while (len >= 32) {
        DOBIG32;
        len -= 32;
    }
    // 批量处理剩余的4字节块
    while (len >= 4) {
        DOBIG4;
        len -= 4;
    }
    // 恢复缓冲区指针为原始char*类型（处理剩余字节）
    buf = (const unsigned char FAR*)buf4;

    /* 处理最后不足4字节的数据（逐字节处理）*/
    if (len) do {
        c = crc_table[4][(c >> 24) ^ *buf++] ^ (c << 8);
    } while (--len);

    c = ~c;  // 最终CRC值取反
    return (unsigned long)(ZSWAP32(c));  // 反转字节顺序后返回（适配大端模式）
}

#endif /* BYFOUR */

#define GF2_DIM 32  // GF(2)域中向量的维度（即CRC的位数，32位）

/* =========================================================================
 * 本地函数：gf2_matrix_times
 * 功能：GF(2)域中矩阵与向量的乘法运算（用于CRC拼接）
 * 参数：
 *   mat  - 指向GF(2)矩阵的指针（每行1个32位值，共32行）
 *   vec  - 待乘的GF(2)向量（32位值）
 * 返回值：矩阵与向量的乘积（32位GF(2)向量）
 */
local unsigned long gf2_matrix_times(mat, vec)
unsigned long* mat;
unsigned long vec;
{
    unsigned long sum = 0;  // 乘积结果（初始为0向量）

    while (vec) {
        if (vec & 1)  // 若向量当前比特为1，异或对应的矩阵行
            sum ^= *mat;
        vec >>= 1;    // 向量右移1位（处理下一个比特）
        mat++;        // 矩阵指针下移1行（处理下一行）
    }
    return sum;
}

/* =========================================================================
 * 本地函数：gf2_matrix_square
 * 功能：GF(2)域中矩阵的平方运算（A² = A×A，用于生成CRC拼接算子）
 * 参数：
 *   square  - 指向结果矩阵（平方矩阵）的指针
 *   mat     - 指向原始矩阵的指针
 */
local void gf2_matrix_square(square, mat)
unsigned long* square;
unsigned long* mat;
{
    int n;

    // 平方矩阵的第n行 = 原始矩阵 × 原始矩阵的第n行
    for (n = 0; n < GF2_DIM; n++)
        square[n] = gf2_matrix_times(mat, mat[n]);
}

/* =========================================================================
 * 本地函数：crc32_combine_
 * 功能：CRC-32拼接的核心实现（将两个CRC值合并为一个）
 * 原理：若数据流A的CRC为crc1，数据流B的CRC为crc2，B的长度为len2，
 *       则A+B的CRC = (crc1 × x^(8×len2) + crc2) mod 多项式
 * 参数：
 *   crc1  - 数据流A的CRC值
 *   crc2  - 数据流B的CRC值
 *   len2  - 数据流B的长度（字节数，64位）
 * 返回值：A+B的合并CRC值
 */
local uLong crc32_combine_(crc1, crc2, len2)
uLong crc1;
uLong crc2;
z_off64_t len2;
{
    int n;
    unsigned long row;  // 临时变量：生成矩阵行
    // even：对应“2^k个零字节”的CRC算子（偶数次幂）
    unsigned long even[GF2_DIM];
    // odd：对应“2^k+1个零字节”的CRC算子（奇数次幂）
    unsigned long odd[GF2_DIM];

    /* 边界情况：B的长度≤0，合并结果即为A的CRC */
    if (len2 <= 0)
        return crc1;

    /* 初始化odd矩阵：对应“1个零比特”的CRC算子 */
    odd[0] = 0xedb88320UL;  // CRC-32多项式（x³²+...+1）
    row = 1;
    for (n = 1; n < GF2_DIM; n++) {
        odd[n] = row;  // 第n行初始化为2^(n-1)（仅第n-1位为1）
        row <<= 1;
    }

    /* 计算even矩阵：对应“2个零比特”的CRC算子（odd矩阵的平方）*/
    gf2_matrix_square(even, odd);

    /* 计算odd矩阵：对应“4个零比特”的CRC算子（even矩阵的平方）*/
    gf2_matrix_square(odd, even);

    /* 对crc1应用“len2个零字节”的算子（即计算crc1 × x^(8×len2) mod 多项式）
       首次平方后，even矩阵对应“1个零字节”（8个零比特）的算子 */
    do {
        // 对当前算子求平方（对应零比特数翻倍）
        gf2_matrix_square(even, odd);
        if (len2 & 1)  // 若len2当前比特为1，应用当前算子到crc1
            crc1 = gf2_matrix_times(even, crc1);
        len2 >>= 1;    // len2右移1位（处理下一个比特）

        if (len2 == 0)  // 无更多比特，退出循环
            break;

        // 交换even和odd，再次求平方（对应零比特数翻倍）
        gf2_matrix_square(odd, even);
        if (len2 & 1)  // 若len2当前比特为1，应用当前算子到crc1
            crc1 = gf2_matrix_times(odd, crc1);
        len2 >>= 1;    // len2右移1位

    } while (len2 != 0);

    // 合并crc1（crc1 × x^(8×len2)）和crc2（异或操作）
    crc1 ^= crc2;
    return crc1;
}

/* =========================================================================
 * 函数：crc32_combine
 * 功能：CRC-32拼接（兼容32位长度参数 z_off_t）
 * 参数：
 *   crc1  - 数据流A的CRC值
 *   crc2  - 数据流B的CRC值
 *   len2  - 数据流B的长度（字节数，32位）
 * 返回值：A+B的合并CRC值
 */
uLong ZEXPORT crc32_combine(crc1, crc2, len2)
uLong crc1;
uLong crc2;
z_off_t len2;
{
    return crc32_combine_(crc1, crc2, len2);  // 调用64位长度的核心实现
}

/* =========================================================================
 * 函数：crc32_combine64
 * 功能：CRC-32拼接（支持64位长度参数 z_off64_t）
 * 参数：
 *   crc1  - 数据流A的CRC值
 *   crc2  - 数据流B的CRC值
 *   len2  - 数据流B的长度（字节数，64位）
 * 返回值：A+B的合并CRC值
 */
uLong ZEXPORT crc32_combine64(crc1, crc2, len2)
uLong crc1;
uLong crc2;
z_off64_t len2;
{
    return crc32_combine_(crc1, crc2, len2);  // 调用核心实现
}