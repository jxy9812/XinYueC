#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON
#include "XS7Address.h"

/**
 * @file XS7Address.c
 * @brief S7 地址语法解析实现（纯函数，无IO无状态）
 * @details 语法全集（与 s-pms siemens_s7_comm.c s7_analysis_address 对齐，
 *          其原文已抓取核验；该实现使用 ctype.h/malloc，此处按仓库规范以
 *          手写 ASCII 大写化与栈上定长缓冲重写）：
 * - `DB1.DBW10` / `DB1.DBX0.1` / `DB1.DBB0` / `DB1.DBD4`（DB 块号 + 可选
 *   DBX/DBB/DBW/DBD 后缀；点后也可省略后缀直接写 `byte` 或 `byte.bit`）；
 * - `D10`（= DB10：块选择，字节偏移 0）；`D10.4`、`D10.DBW2` 同样合法；
 * - `M100` / `MX0.1` / `MB0` / `MW2` / `MD4`（M 区，X/B/W/D 后缀可选）；
 * - `I/IB/IW/ID`、`Q/QB/QW/QD`、`V/VB/VW/VD`（同 M 区规则）；
 * - `T100` / `C100`（计时/计费器序号，字访问，拒绝点分形式）。
 * - 大小写不敏感：解析前对副本做手写 ASCII 大写化（禁 ctype.h toupper）。
 *
 * 解析语义约定（实现决策，供 W4/W5 对照）：
 * - 位偏移：普通区 = byte*8+bit；T/C = 序号本身（字访问，不乘 8）。
 * - 类型推导：有 X/B/W/D（或 DBX/DBB/DBW/DBD）后缀 → Bool/Byte/Word/DWord；
 *   无后缀但为 `byte.bit` 点分形式 → Bool（位语法）；其余裸地址 → Word
 *   （16 位默认，T/C 本就是字访问）。
 * - V 区映射：area = XS7Area_V（线码 0x84，同 DB），dbNumber 固定为 1
 *   （S7-200/SMART 的 V 区即 DB1，Snap7 通行约定；头文件契约
 *   "dbNumber 仅 DB/V 有效" 且 isValid 拒绝 DB/V 缺块号，故不能填 0）。
 * - 块号边界：DB0 视为缺块号直接拒绝（S7 数据块自 DB1 起，isValid 同口径）。
 * - 越界防护：位偏移上限 0x00FFFFFF（S7 ANY 指针 3 字节位址容量）；
 *   块号上限 65535；地址文本长度上限 32 字符（超长拒绝）。
 * - 非法输入拒绝：空串、MX0.8（bit>7）、MX0.A（非数字）、多点号、
 *   裸前缀无序号（如 `M`/`MB`/`DB`）、块号 0、非数字、超长。
 *
 * @note 与 W1 的 XS7Pdu.c 无依赖关系，仅依赖 XS7Types.h 与本文件头契约。
 */

/******************************************************************************************
 * 内部常量
 ******************************************************************************************/

/** @brief 地址文本最大长度（字符数，不含结束符）。
 *  @details 最长合法形式 `DB65535.DBX8388607.7` 仅 19 字符，32 留足余量；
 *           超过上限直接判非法，防超长输入拖垮栈上定长副本。 */
#define XS7ADDRESS_MAX_TEXT_LEN     32u

/** @brief 位偏移上限：S7 ANY 指针 3 字节位址字段容量 0xFFFFFF（字节地址 0x1FFFFF）。 */
#define XS7ADDRESS_MAX_BIT_OFFSET   0x00FFFFFFu

/** @brief 字节段解析上限：byte*8+bit 不溢出 uint32 且受 MAX_BIT_OFFSET 约束。 */
#define XS7ADDRESS_MAX_BYTE_INDEX   0x1FFFFFFFu

/******************************************************************************************
 * 内部辅助（纯字符处理，禁 ctype.h）
 ******************************************************************************************/

/**
 * @brief 手写 ASCII 大写化（替代 ctype.h toupper）
 * @param c 输入字符
 * @return 'a'..'z' 转为 'A'..'Z'，其余原样返回
 */
static char s7_addr_to_upper_ascii(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/**
 * @brief 判定 ASCII 十进制数字
 * @param c 输入字符
 * @return '0'..'9' 返回 true
 */
static bool s7_addr_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/**
 * @brief 求以 '\0' 结尾的 C 串长度（替代 strlen，避免 string.h 依赖争议）
 * @param s 字符串（可为 NULL）
 * @return 字符数；s 为 NULL 返回 0
 */
static size_t s7_addr_text_len(const char* s)
{
    size_t n = 0;
    if (s) {
        while (s[n] != '\0') {
            ++n;
        }
    }
    return n;
}

/**
 * @brief 解析十进制无符号数字段（全数字、非空、带溢出与上限防护）
 * @param s 段起始指针
 * @param len 段长度
 * @param limit 允许的最大值
 * @param outVal 输出数值（成功时有效）
 * @return 合法且不超上限返回 true；空段/非数字/溢出/超上限返回 false
 */
static bool s7_addr_parse_uint(const char* s, size_t len, uint32_t limit, uint32_t* outVal)
{
    if (!s || len == 0 || !outVal) {
        return false;
    }
    uint32_t val = 0;
    for (size_t i = 0; i < len; ++i) {
        if (!s7_addr_is_digit(s[i])) {
            return false;
        }
        const uint32_t digit = (uint32_t)(s[i] - '0');
        if (val > (0xFFFFFFFFu - digit) / 10u) {
            return false;   /* 乘加将溢出 uint32 */
        }
        val = val * 10u + digit;
    }
    if (val > limit) {
        return false;
    }
    *outVal = val;
    return true;
}

/**
 * @brief 解析偏移段：`byte`、`byte.bit` 或 T/C 序号（对齐 s-pms calculate_address_started）
 * @param s 段起始指针
 * @param len 段长度
 * @param isCT true 表示 T/C 区（仅接受纯序号，拒绝点分形式）
 * @param outStart 输出位偏移（普通区=byte*8+bit；T/C=序号本身）
 * @param outBitForm 输出是否为 `byte.bit` 点分形式（供类型推导，可为 NULL）
 * @return 合法返回 true；多点号/bit>7/非数字/空段返回 false
 */
static bool s7_addr_parse_start(const char* s, size_t len, bool isCT,
                                uint32_t* outStart, bool* outBitForm)
{
    if (!s || !outStart) {
        return false;
    }
    if (outBitForm) {
        *outBitForm = false;
    }

    /* 定位第一个 '.'（无点则 dot == len） */
    size_t dot = len;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] == '.') {
            dot = i;
            break;
        }
    }

    if (dot == len) {
        /* 纯数字：普通区为字节地址（结果乘 8），T/C 为序号（不乘 8） */
        uint32_t num = 0;
        if (!s7_addr_parse_uint(s, len, XS7ADDRESS_MAX_BYTE_INDEX, &num)) {
            return false;
        }
        if (isCT) {
            if (num > XS7ADDRESS_MAX_BIT_OFFSET) {
                return false;
            }
            *outStart = num;
            return true;
        }
        const uint32_t start = num * 8u;
        if (start > XS7ADDRESS_MAX_BIT_OFFSET) {
            return false;
        }
        *outStart = start;
        return true;
    }

    /* byte.bit 形式：T/C 拒绝；bit 限 0..7；多点号拒绝 */
    if (isCT) {
        return false;
    }
    for (size_t i = dot + 1; i < len; ++i) {
        if (s[i] == '.') {
            return false;   /* 多点号 */
        }
    }
    uint32_t byteIndex = 0;
    uint32_t bitIndex = 0;
    if (!s7_addr_parse_uint(s, dot, XS7ADDRESS_MAX_BYTE_INDEX, &byteIndex)) {
        return false;
    }
    if (!s7_addr_parse_uint(s + dot + 1, len - dot - 1, 7u, &bitIndex)) {
        return false;   /* 含空位段 / 非数字 / bit>7（如 MX0.8、MX0.A） */
    }
    const uint32_t start = byteIndex * 8u + bitIndex;
    if (start > XS7ADDRESS_MAX_BIT_OFFSET) {
        return false;
    }
    if (outBitForm) {
        *outBitForm = true;
    }
    *outStart = start;
    return true;
}

/******************************************************************************************
 * 公开 API：解析
 ******************************************************************************************/

bool XS7Address_parse(XS7Address* out, const XString* addr)
{
    if (!out || !addr) {
        return false;
    }
    /* XString_toUtf8 返回串内缓存的 UTF-8 只读视图；
       空串/转换失败返回 NULL，恰好在 _2 中作为非法输入拒绝 */
    return XS7Address_parse_2(out, XString_toUtf8(addr));
}

bool XS7Address_parse_2(XS7Address* out, const char* addrUtf8)
{
    if (!out || !addrUtf8) {
        return false;
    }

    /* 空串与超长预检（超长拒绝） */
    const size_t len = s7_addr_text_len(addrUtf8);
    if (len == 0 || len > XS7ADDRESS_MAX_TEXT_LEN) {
        return false;
    }

    /* 手写 ASCII 大写化副本（禁 ctype.h toupper；栈上定长缓冲，无堆分配） */
    char buf[XS7ADDRESS_MAX_TEXT_LEN + 1];
    for (size_t i = 0; i < len; ++i) {
        buf[i] = s7_addr_to_upper_ascii(addrUtf8[i]);
    }
    buf[len] = '\0';

    /* ---- 前缀判定（顺序对齐 s-pms：DB 先于 D，防前缀吞噬） ---- */
    uint8_t area = 0;
    size_t prefixLen = 0;
    bool isDbLike = false;  /* DB/D 分支：带块号 + 可选 DBX/DBB/DBW/DBD 子后缀 */
    bool isCT = false;      /* T/C：序号字访问 */
    bool matched = false;

    if (buf[0] == 'D' && len >= 2 && buf[1] == 'B') {
        area = (uint8_t)XS7Area_DB;
        prefixLen = 2;
        isDbLike = true;
        matched = true;
    }
    else {
        switch (buf[0]) {
        case 'I': area = (uint8_t)XS7Area_I; matched = true; break;
        case 'Q': area = (uint8_t)XS7Area_Q; matched = true; break;
        case 'M': area = (uint8_t)XS7Area_M; matched = true; break;
        case 'V': area = (uint8_t)XS7Area_V; matched = true; break;
        case 'T': area = (uint8_t)XS7Area_T; matched = true; isCT = true; break;
        case 'C': area = (uint8_t)XS7Area_C; matched = true; isCT = true; break;
        case 'D': area = (uint8_t)XS7Area_DB; matched = true; isDbLike = true; break;
        default: matched = false; break;
        }
        prefixLen = 1;
    }
    if (!matched) {
        return false;   /* 未知前缀（非数字开头亦在此拒绝） */
    }

    /* ---- 公共解析状态 ---- */
    uint16_t dbNumber = 0;
    uint32_t start = 0;
    int suffixType = -1;    /* -1=无后缀；否则 XS7ValueType */

    if (isDbLike) {
        /* 块号段：前缀之后到第一个 '.'（纯数字、1..65535；DB0 视为缺块号拒绝） */
        size_t numEnd = prefixLen;
        while (numEnd < len && buf[numEnd] != '.') {
            ++numEnd;
        }
        uint32_t block = 0;
        if (!s7_addr_parse_uint(buf + prefixLen, numEnd - prefixLen, 0xFFFFu, &block)) {
            return false;
        }
        if (block == 0) {
            return false;   /* DB0/V0：数据块自 DB1 起，与 isValid "缺块号" 同口径 */
        }
        dbNumber = (uint16_t)block;

        if (numEnd < len) {
            /* 点后有子地址段：可带 DBX/DBB/DBW/DBD 后缀（对齐 s-pms：
               仅完整三字母后缀才剥离，剥离后仍可为裸 byte[.bit]） */
            size_t pos = numEnd + 1;
            if (pos < len && buf[pos] == 'D' && (pos + 1) < len && buf[pos + 1] == 'B'
                && (pos + 2) < len) {
                switch (buf[pos + 2]) {
                case 'X': suffixType = (int)XS7Value_Bool;  pos += 3; break;
                case 'B': suffixType = (int)XS7Value_Byte;  pos += 3; break;
                case 'W': suffixType = (int)XS7Value_Word;  pos += 3; break;
                case 'D': suffixType = (int)XS7Value_DWord; pos += 3; break;
                default: break; /* "DB" 后无类型字母 → 不剥离（如 DB1.DB10 非法） */
                }
            }
            bool bitForm = false;
            if (!s7_addr_parse_start(buf + pos, len - pos, false, &start, &bitForm)) {
                return false;
            }
            if (suffixType < 0) {
                /* 无后缀类型推导：点分位形式 → Bool；其余裸地址 → Word */
                suffixType = bitForm ? (int)XS7Value_Bool : (int)XS7Value_Word;
            }
        }
        else {
            /* 纯块号（如 D10 / DB10）：块选择，字节偏移 0，默认 Word */
            start = 0;
            suffixType = (int)XS7Value_Word;
        }
    }
    else {
        /* I/Q/M/V/T/C 单字母区：可选 X/B/W/D 后缀（T/C 无后缀语法，对齐 s-pms） */
        size_t pos = prefixLen;
        if (!isCT && pos < len) {
            switch (buf[pos]) {
            case 'X': suffixType = (int)XS7Value_Bool;  ++pos; break;
            case 'B': suffixType = (int)XS7Value_Byte;  ++pos; break;
            case 'W': suffixType = (int)XS7Value_Word;  ++pos; break;
            case 'D': suffixType = (int)XS7Value_DWord; ++pos; break;
            default: break; /* 后随数字 → 无后缀 */
            }
        }
        bool bitForm = false;
        if (!s7_addr_parse_start(buf + pos, len - pos, isCT, &start, &bitForm)) {
            return false;
        }
        if (suffixType < 0) {
            suffixType = bitForm ? (int)XS7Value_Bool : (int)XS7Value_Word;
        }
    }

    /* ---- V 区块号映射：V 语法地址（未走 DB/D 块号分支）固定映射 DB1
       （S7-200/SMART 通行约定；isValid 拒绝 DB/V 缺块号，故不可为 0）。
       注意 XS7Area_V 与 XS7Area_DB 线码同为 0x84，必须以 isDbLike 区分，
       否则会把普通 DB 地址的块号一并覆写。 ---- */
    if (!isDbLike && area == (uint8_t)XS7Area_V) {
        dbNumber = 1;
    }

    /* ---- 回填结果 ---- */
    out->area = area;
    out->dbNumber = (area == (uint8_t)XS7Area_DB) ? dbNumber : 0; /* V 同线码 0x84，一并落入 */
    out->bitOffset = start;
    out->type = (XS7ValueType)suffixType;
    out->count = 1;
    return true;
}

/******************************************************************************************
 * 公开 API：有效性判定
 ******************************************************************************************/

bool XS7Address_isValid(const XS7Address* addr)
{
    if (!addr) {
        return false;
    }

    /* 区域线码必须落在枚举全集内 */
    switch (addr->area) {
    case XS7Area_C:
    case XS7Area_T:
    case XS7Area_I:
    case XS7Area_Q:
    case XS7Area_M:
        break;
    case XS7Area_DB:    /* 含 V 区（线码同 0x84）：必须带块号（缺块号判非法） */
        if (addr->dbNumber == 0) {
            return false;
        }
        break;
    default:
        return false;
    }

    /* 类型枚举越界防护 */
    if ((int)addr->type < (int)XS7Value_Bool || (int)addr->type > (int)XS7Value_String) {
        return false;
    }

    /* 位偏移越界（ANY 指针 3 字节容量） */
    if (addr->bitOffset > XS7ADDRESS_MAX_BIT_OFFSET) {
        return false;
    }

    /* 元素个数至少 1（count 默认 1，0 无意义） */
    if (addr->count == 0) {
        return false;
    }

    return true;
}

/******************************************************************************************
 * 公开 API：序列化（诊断用）
 ******************************************************************************************/

/**
 * @brief 地址转回字符串（诊断用，头契约见 XS7Address.h）
 * @param addr 地址结构（非NULL，须通过 isValid 校验）
 * @return 新建的规范形式 XString（调用者负责 XClassDelete）；
 *         addr 为 NULL / 校验失败 / 类型组合非法时返回 NULL
 * @note 规范形式与解析语法可往返：如 D10(Word) → "DB10.DBW0"、
 *       VD20 → "DB1.DBD20"（V 线码同 DB，输出 DB 形式）、
 *       T100 → "T100"、MX0.1 → "MX0.1"。
 * @note 用 XString_create_fmt_utf8 整体生成（XString_setNum_* 语义为
 *       整串替换，不能用于增量拼装，故不走 append+setNum 路线）
 */
XString* XS7Address_toString(const XS7Address* addr)
{
    if (!addr || !XS7Address_isValid(addr)) {
        return NULL;
    }

    const uint32_t byteIndex = addr->bitOffset / 8u;
    const uint32_t bitIndex = addr->bitOffset % 8u;
    XString* str = NULL;

    switch (addr->area) {
    case XS7Area_DB:    /* 含 V 区：线码同 0x84，规范输出 DB 形式（二者不可区分） */
        switch (addr->type) {
        case XS7Value_Bool:
            str = XString_create_fmt_utf8("DB%u.DBX%u.%u",
                                          (unsigned int)addr->dbNumber,
                                          (unsigned int)byteIndex, (unsigned int)bitIndex);
            break;
        case XS7Value_Byte:
            str = XString_create_fmt_utf8("DB%u.DBB%u",
                                          (unsigned int)addr->dbNumber,
                                          (unsigned int)byteIndex);
            break;
        case XS7Value_Word:
            str = XString_create_fmt_utf8("DB%u.DBW%u",
                                          (unsigned int)addr->dbNumber,
                                          (unsigned int)byteIndex);
            break;
        case XS7Value_DWord:
            str = XString_create_fmt_utf8("DB%u.DBD%u",
                                          (unsigned int)addr->dbNumber,
                                          (unsigned int)byteIndex);
            break;
        case XS7Value_String:
            /* STRING(n) 按字节址；语法无专用后缀，输出字节址形式 */
            str = XString_create_fmt_utf8("DB%u.DBB%u",
                                          (unsigned int)addr->dbNumber,
                                          (unsigned int)byteIndex);
            break;
        default:
            str = NULL;
            break;
        }
        break;

    case XS7Area_M:
    case XS7Area_I:
    case XS7Area_Q: {
        const char* areaChar = (addr->area == XS7Area_M) ? "M"
                               : ((addr->area == XS7Area_I) ? "I" : "Q");
        switch (addr->type) {
        case XS7Value_Bool:
            str = XString_create_fmt_utf8("%sX%u.%u", areaChar,
                                          (unsigned int)byteIndex, (unsigned int)bitIndex);
            break;
        case XS7Value_Byte:
            str = XString_create_fmt_utf8("%sB%u", areaChar, (unsigned int)byteIndex);
            break;
        case XS7Value_Word:
            str = XString_create_fmt_utf8("%sW%u", areaChar, (unsigned int)byteIndex);
            break;
        case XS7Value_DWord:
            str = XString_create_fmt_utf8("%sD%u", areaChar, (unsigned int)byteIndex);
            break;
        case XS7Value_String:
            str = XString_create_fmt_utf8("%sB%u", areaChar, (unsigned int)byteIndex);
            break;
        default:
            str = NULL;
            break;
        }
        break;
    }

    case XS7Area_T:     /* T/C：bitOffset 即序号（字访问），规范输出无后缀形式 */
        str = XString_create_fmt_utf8("T%u", (unsigned int)addr->bitOffset);
        break;
    case XS7Area_C:
        str = XString_create_fmt_utf8("C%u", (unsigned int)addr->bitOffset);
        break;
    default:
        str = NULL;
        break;
    }

    return str;   /* 失败（含非法类型组合）时为 NULL */
}

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
