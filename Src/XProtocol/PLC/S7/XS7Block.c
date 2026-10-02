#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_BLOCK_ON
#include "XS7Block.h"
#include "XS7Pdu.h"
#include "XS7Tpkt.h"
#include <string.h>

/**
 * @file XS7Block.c
 * @brief S7 运维：块列表/上传/下载多包序列的组包与应答解析实现
 * @details 功能码来源（Wireshark packet-s7comm.c 枚举与 snap7
 *          s7_micro_client.cpp 双源一致，可冻结）：
 *   0x1A RequestDownload / 0x1B Download / 0x1C DownloadEnded /
 *   0x1D StartUpload / 0x1E Upload / 0x1F EndUpload。
 *
 *   以下布局取自 snap7 通行实现（请求参数逐字节模板），Wireshark 解码器
 *   同构可解析；未经过真实 PLC 抓包二次核验的常量在行内标注"待实测"：
 * - ListBlocks：UserData(0x07) funcgroup4(0x44)/subfunc1 读 SZL，
 *   SZL-ID 0x0100（snap7 ListBlocks 通行值，待实测）；
 *   应答记录 = [2B 块类型 ASCII 十六进制][2B 数量 BE]，前缀 SZL 头 4 字节。
 * - StartUpload/EndUpload 参数：func | 00 | 00 00 | UploadID(4B) | 09 |
 *   "_0<类型字符><5位十进制>A"（snap7 Filename 结构：Prefix 0x5F、
 *   BlkPrfx 0x30、类型字符、5 位数字、FileSystem 0x41）。
 * - Upload 参数：func | 00 | 00 00 | UploadID(4B)（snap7 同构；
 *   UploadID 由 StartUpload 应答给出，本助手层恒 0，由会话层回填）。
 * - RequestDownload 参数：func | 00 01 00 00 00 00（Uk6[1]=0x01）|
 *   DwnldID 00 | 09 | 文件名 9B | 'P'(0x50) | 0D | '1'(0x31) |
 *   6 位装载内存大小 | 6 位 MC7 大小（snap7 RequestDownload 同构，待实测）。
 * - Download 参数：func | EoS(0x01=还有后续包,0x00=最后一包) | 00 00 |
 *   本包长度(2B)；数据区 = FB 00 | 本包长度(2B) | 数据
 *   （snap7 下行分片 FB_00=0xFB00 数据头，待实测）。
 * - Upload 应答：Ack_Data；参数 FuncStatus bit0=0x01 表示还有后续包；
 *   数据区 = FB 00 | 本包长度(2B) | 分片数据（snap7 上行分片同构，待实测）。
 */

/** @brief 块服务功能码（Wireshark packet-s7comm.c 枚举，双源一致） */
#define XS7_BLOCK_FUNC_REQUEST_DOWNLOAD 0x1A
#define XS7_BLOCK_FUNC_DOWNLOAD         0x1B
#define XS7_BLOCK_FUNC_START_UPLOAD     0x1D
#define XS7_BLOCK_FUNC_UPLOAD           0x1E
#define XS7_BLOCK_FUNC_END_UPLOAD       0x1F

/** @brief 上传/下载文件名长度（'_' + '0' + 类型字符 + 5 位数字 + 'A'） */
#define XS7_BLOCK_FILENAME_LEN 9

// =============== 内部辅助 ===============

/*
 * XByteArray 首成员即 XVector（XByteArray.h 结构定义），库内自带内联包装
 * 同样做 (XVector*) 显式转换；此处沿用该仓库习惯用法调用向量接口，
 * 规避宏转发接口的 C4133 形参类型告警。
 */

/** @brief 扩容字节数组到 n 字节 */
static inline bool xs7BlockResize(XByteArray* array, size_t n)
{
    return XVector_resize_base((XVector*)array, n);
}

/** @brief 取当前字节数 */
static inline size_t xs7BlockSize(const XByteArray* array)
{
    return XContainer_size_base((const XContainer*)array);
}

/** @brief 写 2 字节大端整数 */
static void xs7BlockWriteBe16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/** @brief 读 2 字节大端整数 */
static uint16_t xs7BlockReadBe16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/** @brief 组 S7 Job 头（10 字节）：32 | rosctr | 0000 | pduRef | paramLen | dataLen */
static void xs7BlockBuildJobHeader(uint8_t* out, uint8_t rosctr, uint16_t pduRef,
                                   uint16_t paramLen, uint16_t dataLen)
{
    out[0] = 0x32;
    out[1] = rosctr;
    out[2] = 0x00;
    out[3] = 0x00;
    xs7BlockWriteBe16(out + 4, pduRef);
    xs7BlockWriteBe16(out + 6, paramLen);
    xs7BlockWriteBe16(out + 8, dataLen);
}

/**
 * @brief 解析块名为 snap7 类型字符 + 块号
 * @param name 块名（NUL 结尾，如 "DB1"/"OB35"/"SFB52"）
 * @param typeChar 输出：snap7 类型字符（'8'=OB 'A'=DB 'B'=SDB 'C'=FB 'D'=FC 'E'=SFB 'F'=SFC）
 * @param number 输出：块号（0..99999，5 位 ASCII 可容纳）
 * @return 合法返回true
 */
static bool xs7BlockParseName(const char* name, uint8_t* typeChar, uint32_t* number)
{
    static const struct {
        const char* prefix;
        uint8_t     ch;
    } table[] = {
        { "SDB", 0x42 }, { "SFB", 0x45 }, { "SFC", 0x46 },   /* 3 字符前缀优先匹配 */
        { "OB",  0x38 }, { "DB",  0x41 }, { "FB",  0x43 }, { "FC", 0x44 }
    };
    const size_t tableCount = sizeof(table) / sizeof(table[0]);
    size_t i;
    size_t pos = 0;
    uint32_t value = 0;

    if (!name || !typeChar || !number) return false;

    for (i = 0; i < tableCount; ++i) {
        const char* p = table[i].prefix;
        size_t n = 0;
        while (p[n] != '\0') ++n;
        {
            size_t k;
            bool match = true;
            for (k = 0; k < n; ++k) {
                if (name[k] != p[k]) { match = false; break; }
            }
            if (match) { pos = n; *typeChar = table[i].ch; break; }
        }
    }
    if (pos == 0) return false;               /* 前缀不识别 */

    while (name[pos] >= '0' && name[pos] <= '9') {
        value = value * 10u + (uint32_t)(name[pos] - '0');
        if (value > 99999u) return false;     /* 超出 5 位 ASCII 数字域 */
        ++pos;
    }
    if (name[pos] != '\0') return false;      /* 尾部残留非法字符 */
    if (pos < 2) return false;                /* 至少 1 位数字 */

    *number = value;
    return true;
}

/** @brief 在 out 的 off 偏移写 snap7 上传/下载文件名（9 字节），返回新偏移 */
static size_t xs7BlockAppendFilename(uint8_t* out, size_t off, uint8_t typeChar, uint32_t number)
{
    out[off + 0] = 0x5F;                      /* Prefix '_' */
    out[off + 1] = 0x30;                      /* BlkPrfx '0' */
    out[off + 2] = typeChar;
    out[off + 3] = (uint8_t)('0' + (number / 10000u) % 10u);
    out[off + 4] = (uint8_t)('0' + (number / 1000u) % 10u);
    out[off + 5] = (uint8_t)('0' + (number / 100u) % 10u);
    out[off + 6] = (uint8_t)('0' + (number / 10u) % 10u);
    out[off + 7] = (uint8_t)('0' + number % 10u);
    out[off + 8] = 0x41;                      /* FileSystem 'A'（工作存储区，snap7 同值） */
    return off + XS7_BLOCK_FILENAME_LEN;
}

/** @brief 写 N 位十进制 ASCII（右侧对齐补 '0'），返回新偏移 */
static size_t xs7BlockAppendDigits(uint8_t* out, size_t off, uint32_t value, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; --i) {
        out[off + (size_t)i] = (uint8_t)('0' + value % 10u);
        value /= 10u;
    }
    return off + (size_t)digits;
}

/** @brief 应答头公共校验：Ack_Data/UserData 且头级无错误，失败返回 false */
static bool xs7BlockParseAckHeader(const uint8_t* s7, size_t len, uint16_t* paramLen,
                                   uint16_t* dataLen)
{
    XS7PduHeader hdr;
    if (!XS7Pdu_parseHeader(s7, len, &hdr)) return false;
    /* 块服务应答两种形态：经典 Ack_Data（0x03）与 UserData 应答（0x07，如块列表） */
    if (hdr.rosctr != XS7_ROSCTR_ACK_DATA && hdr.rosctr != XS7_ROSCTR_USERDATA) return false;
    if (hdr.errorClass != 0x00) return false;
    if ((size_t)12 + (size_t)hdr.paramLen + (size_t)hdr.dataLen > len) return false;
    if (paramLen) *paramLen = hdr.paramLen;
    if (dataLen) *dataLen = hdr.dataLen;
    return true;
}

// =============== 块列表 ===============

size_t XS7Block_buildListBlocks(uint8_t* out, uint16_t pduRef)
{
    if (!out) return 0;

    /* UserData 头（12 字节）：32 07 0000 ref paramLen(8) dataLen(8) 00 00 */
    xs7BlockBuildJobHeader(out, XS7_ROSCTR_USERDATA, pduRef, 0x0008, 0x0008);
    out[10] = 0x00;                           /* errorClass（UD 头含错误两字节） */
    out[11] = 0x00;                           /* errorCode */

    /* UD 参数（8 字节）：func=00 项数=01 | 12 04 11 | 44 01 | 序号 00
     * 0x44 = (req<<6)|funcgroup4（CPU 功能），subfunc 0x01 = 读 SZL */
    out[12] = 0x00;
    out[13] = 0x01;
    out[14] = 0x12;                           /* 变规范标识 */
    out[15] = 0x04;                           /* 规范长度 */
    out[16] = 0x11;                           /* 语法 ID */
    out[17] = 0x44;
    out[18] = 0x01;
    out[19] = 0x00;

    /* UD 数据（8 字节）：FF 09 0004 | SZL-ID 0x0D91 | index 0x0000
     * 0x0D91 = 块列表（snap7 ListBlocks 同源读取；记录体 [2B 块类型]
     * [2B 块数量] 与 parseListBlocksAck 布局一致。曾误用 0x0100——那是
     * 模块标识（snap7 GetCpuInfo 用），对 ListBlocks 语义不成立） */
    out[20] = 0xFF;
    out[21] = XS7_TRANSPORT_DATA_OCTET;       /* 0x09 八位组串 */
    out[22] = 0x00;
    out[23] = 0x04;
    out[24] = 0x0D;                           /* SZL-ID 0x0D91 高字节 */
    out[25] = 0x91;                           /* SZL-ID 0x0D91 低字节 */
    out[26] = 0x00;                           /* index 0 高字节 */
    out[27] = 0x00;
    return 28;
}

bool XS7Block_parseListBlocksAck(const uint8_t* s7, size_t len, XVector* outBlocks)
{
    uint16_t paramLen = 0;
    uint16_t dataLen = 0;
    size_t off;
    uint8_t ret;
    uint8_t transport;
    uint16_t szlLen;

    if (!outBlocks) return false;
    if (!xs7BlockParseAckHeader(s7, len, &paramLen, &dataLen)) return false;
    if (s7[12] != 0x00) return false;         /* UD 应答参数 func 恒 0x00 */

    /* 数据区首个数据项：FF 09 | 长度 | SZL 载荷 */
    off = 12 + (size_t)paramLen;
    if (dataLen < 4) return false;
    ret = s7[off];
    transport = s7[off + 1];
    szlLen = xs7BlockReadBe16(s7 + off + 2);
    if (ret != XS7_RETURN_OK) return false;
    if (transport != XS7_TRANSPORT_DATA_OCTET) return false;
    if (off + 4 + (size_t)szlLen > 12 + (size_t)paramLen + (size_t)dataLen) return false;

    /* SZL 载荷：[ID(2)][index(2)] 前缀 + 逐条 4 字节记录 [2B 类型 ASCII][2B 数量 BE] */
    if (szlLen < 4) return false;
    {
        size_t rec = off + 4 + 4;             /* 跳过 SZL 头 4 字节 */
        size_t recEnd = off + 4 + (size_t)szlLen;
        while (rec + 4 <= recEnd) {
            XS7BlockInfo info;
            int hi = s7[rec];
            int lo = s7[rec + 1];
            int type;

            /* 类型字段为块类型码的两位 ASCII 十六进制（如 "08"=OB、"0A"=DB） */
            if (hi >= '0' && hi <= '9') hi -= '0';
            else if (hi >= 'A' && hi <= 'F') hi -= 'A' - 10;
            else return false;
            if (lo >= '0' && lo <= '9') lo -= '0';
            else if (lo >= 'A' && lo <= 'F') lo -= 'A' - 10;
            else return false;
            type = (hi << 4) | lo;

            memset(&info, 0, sizeof(info));
            switch (type) {
            case 0x08: info.name[0]='O'; info.name[1]='B'; break;   /* OB */
            case 0x0A: info.name[0]='D'; info.name[1]='B'; break;   /* DB */
            case 0x0B: info.name[0]='F'; info.name[1]='B'; break;   /* FB */
            case 0x0C: info.name[0]='F'; info.name[1]='C'; break;   /* FC */
            case 0x05: info.name[0]='S'; info.name[1]='D'; info.name[2]='B'; break;  /* SDB */
            case 0x0D: info.name[0]='S'; info.name[1]='F'; info.name[2]='B'; break;  /* SFB */
            case 0x0E: info.name[0]='S'; info.name[1]='F'; info.name[2]='C'; break;  /* SFC */
            default: break;                   /* 未知类型：名称留空，仅保留编码 */
            }
            info.blockType = (uint8_t)type;
            info.size = ((uint32_t)s7[rec + 2] << 8) | (uint32_t)s7[rec + 3];
            if (!XVector_push_back_1_base(outBlocks, &info)) return false;
            rec += 4;
        }
    }
    return true;
}

// =============== 上传序列 ===============

size_t XS7Block_buildStartUpload(uint8_t* out, uint16_t pduRef, const char* blockName)
{
    uint8_t typeChar = 0;
    uint32_t number = 0;

    if (!out || !blockName) return 0;
    if (!xs7BlockParseName(blockName, &typeChar, &number)) return 0;

    /* 参数（18 字节）：1D 00 0000 00000000 | 09 | 文件名 9B */
    xs7BlockBuildJobHeader(out, XS7_ROSCTR_JOB, pduRef, 18, 0);
    out[10] = XS7_BLOCK_FUNC_START_UPLOAD;    /* 0x1D */
    out[11] = 0x00;                           /* FuncStatus（请求恒 0） */
    out[12] = 0x00;                           /* Uk（2 字节） */
    out[13] = 0x00;
    out[14] = 0x00;                           /* UploadID（由 StartUpload 应答回填，此处 0） */
    out[15] = 0x00;
    out[16] = 0x00;
    out[17] = 0x00;
    out[18] = XS7_BLOCK_FILENAME_LEN;
    (void)xs7BlockAppendFilename(out, 19, typeChar, number);
    return 28;
}

size_t XS7Block_buildUpload(uint8_t* out, uint16_t pduRef)
{
    if (!out) return 0;

    /* 参数（8 字节）：1E 00 0000 00000000（UploadID 恒 0，会话层回填） */
    xs7BlockBuildJobHeader(out, XS7_ROSCTR_JOB, pduRef, 8, 0);
    out[10] = XS7_BLOCK_FUNC_UPLOAD;          /* 0x1E */
    out[11] = 0x00;
    out[12] = 0x00;
    out[13] = 0x00;
    out[14] = 0x00;
    out[15] = 0x00;
    out[16] = 0x00;
    out[17] = 0x00;
    return 18;
}

size_t XS7Block_buildEndUpload(uint8_t* out, uint16_t pduRef, const char* blockName)
{
    uint8_t typeChar = 0;
    uint32_t number = 0;

    if (!out || !blockName) return 0;
    if (!xs7BlockParseName(blockName, &typeChar, &number)) return 0;

    /* 参数（18 字节）：1F 00 0000 00000000 | 09 | 文件名 9B（与 StartUpload 同构） */
    xs7BlockBuildJobHeader(out, XS7_ROSCTR_JOB, pduRef, 18, 0);
    out[10] = XS7_BLOCK_FUNC_END_UPLOAD;      /* 0x1F */
    out[11] = 0x00;
    out[12] = 0x00;
    out[13] = 0x00;
    out[14] = 0x00;
    out[15] = 0x00;
    out[16] = 0x00;
    out[17] = 0x00;
    out[18] = XS7_BLOCK_FILENAME_LEN;
    (void)xs7BlockAppendFilename(out, 19, typeChar, number);
    return 28;
}

bool XS7Block_parseUploadAck(const uint8_t* s7, size_t len, XByteArray* outData, bool* outFinished)
{
    uint16_t paramLen = 0;
    uint16_t dataLen = 0;
    size_t off;
    uint8_t func;

    if (!xs7BlockParseAckHeader(s7, len, &paramLen, &dataLen)) return false;
    if (paramLen < 2) return false;
    func = s7[12];
    if (func != XS7_BLOCK_FUNC_START_UPLOAD && func != XS7_BLOCK_FUNC_UPLOAD &&
        func != XS7_BLOCK_FUNC_END_UPLOAD) {
        return false;                          /* 功能码回显不符 */
    }

    /* FuncStatus bit0=0x01：还有后续分片（snap7 EoS 同语义） */
    if (outFinished) {
        *outFinished = (s7[13] & 0x01) ? false : true;
    }

    /* 数据区：FB 00 | 本包长度(2B) | 分片数据（StartUpload 应答可为空数据段） */
    if (outData && dataLen >= 4) {
        size_t sliceLen;
        off = 12 + (size_t)paramLen;
        if (s7[off] != 0xFB) return false;    /* 数据响应头固定 0xFB */
        sliceLen = (size_t)xs7BlockReadBe16(s7 + off + 2);
        if (off + 4 + sliceLen > 12 + (size_t)paramLen + (size_t)dataLen) return false;
        if (sliceLen > 0) {
            /* 扩容 + 直写缓冲 */
            size_t old = xs7BlockSize(outData);
            if (!xs7BlockResize(outData, old + sliceLen)) return false;
            memcpy(XByteArray_data(outData) + old, s7 + off + 4, sliceLen);
        }
    }
    return true;
}

// =============== 下载序列 ===============

size_t XS7Block_buildRequestDownload(uint8_t* out, uint16_t pduRef, const char* blockName, uint32_t blockSize)
{
    uint8_t typeChar = 0;
    uint32_t number = 0;

    if (!out || !blockName) return 0;
    if (!xs7BlockParseName(blockName, &typeChar, &number)) return 0;
    if (blockSize > 999999u) return 0;        /* 6 位 ASCII 数字域 */

    /* 参数（33 字节）：1A | 00 01 00 00 00 00 | 00 | 09 | 文件名 9B |
     * 'P' | 0D | '1' | 6 位装载内存大小 | 6 位 MC7 大小 */
    xs7BlockBuildJobHeader(out, XS7_ROSCTR_JOB, pduRef, 33, 0);
    out[10] = XS7_BLOCK_FUNC_REQUEST_DOWNLOAD;    /* 0x1A */
    out[11] = 0x00;                               /* Uk6[0] */
    out[12] = 0x01;                               /* Uk6[1]=0x01（snap7 同值） */
    out[13] = 0x00;                               /* Uk6[2] */
    out[14] = 0x00;                               /* Uk6[3] */
    out[15] = 0x00;                               /* Uk6[4] */
    out[16] = 0x00;                               /* Uk6[5] */
    out[17] = 0x00;                               /* DwnldID */
    out[18] = XS7_BLOCK_FILENAME_LEN;
    {
        size_t off = xs7BlockAppendFilename(out, 19, typeChar, number);
        out[off] = 0x50;                          /* 'P' */
        out[off + 1] = 0x0D;                      /* 尾部长度字段 = 13（'1'+两组 6 位数字） */
        out[off + 2] = 0x31;                      /* '1'（snap7 Uk1 同值） */
        off = xs7BlockAppendDigits(out, off + 3, blockSize, 6);   /* 装载内存大小 */
        (void)xs7BlockAppendDigits(out, off, blockSize, 6);       /* MC7 大小 */
    }
    return 10 + 33;
}

size_t XS7Block_buildDownload(uint8_t* out, uint16_t pduRef, uint32_t offset,
                              const uint8_t* data, size_t dataLen, bool last)
{
    if (!out || !data) return 0;
    if (dataLen == 0 || dataLen > 0xFFFF) return 0;
    (void)offset;                             /* 分片偏移由序号隐含；S7 无显式偏移字段 */

    /* 参数（6 字节）：1B | EoS | 00 00 | 本包长度(2B) */
    xs7BlockBuildJobHeader(out, XS7_ROSCTR_JOB, pduRef, 6, (uint16_t)(4 + dataLen));
    out[10] = XS7_BLOCK_FUNC_DOWNLOAD;        /* 0x1B */
    out[11] = last ? 0x00 : 0x01;             /* EoS：0x01=还有后续包 */
    out[12] = 0x00;
    out[13] = 0x00;
    xs7BlockWriteBe16(out + 14, (uint16_t)dataLen);

    /* 数据区：FB 00 | 本包长度(2B) | 数据 */
    out[16] = 0xFB;
    out[17] = 0x00;
    xs7BlockWriteBe16(out + 18, (uint16_t)dataLen);
    {
        size_t i;
        for (i = 0; i < dataLen; ++i) {
            out[20 + i] = data[i];
        }
    }
    return 20 + dataLen;
}

bool XS7Block_parseDownloadAck(const uint8_t* s7, size_t len)
{
    uint16_t paramLen = 0;
    uint16_t dataLen = 0;

    if (!xs7BlockParseAckHeader(s7, len, &paramLen, &dataLen)) return false;
    if (paramLen < 1) return false;
    return (s7[12] == XS7_BLOCK_FUNC_DOWNLOAD);   /* 功能码回显 0x1B */
}

#endif /* XS7_BLOCK_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
