#include "XPrintf.h"
#include "XRcode.h"
#include "XMemory.h"
#include <string.h>
#include <stdio.h>

/* =========================== RS 编码相关表格 =========================== */
/* GF(2^8) 指数到系数表 */
static const unsigned char m_exp_to_int[] =
{
  1,   2,   4,   8,  16,  32,  64, 128,  29,  58, 116, 232, 205, 135,  19,  38,
 76, 152,  45,  90, 180, 117, 234, 201, 143,   3,   6,  12,  24,  48,  96, 192,
157,  39,  78, 156,  37,  74, 148,  53, 106, 212, 181, 119, 238, 193, 159,  35,
 70, 140,   5,  10,  20,  40,  80, 160,  93, 186, 105, 210, 185, 111, 222, 161,
 95, 190,  97, 194, 153,  47,  94, 188, 101, 202, 137,  15,  30,  60, 120, 240,
253, 231, 211, 187, 107, 214, 177, 127, 254, 225, 223, 163,  91, 182, 113, 226,
217, 175,  67, 134,  17,  34,  68, 136,  13,  26,  52, 104, 208, 189, 103, 206,
129,  31,  62, 124, 248, 237, 199, 147,  59, 118, 236, 197, 151,  51, 102, 204,
133,  23,  46,  92, 184, 109, 218, 169,  79, 158,  33,  66, 132,  21,  42,  84,
168,  77, 154,  41,  82, 164,  85, 170,  73, 146,  57, 114, 228, 213, 183, 115,
230, 209, 191,  99, 198, 145,  63, 126, 252, 229, 215, 179, 123, 246, 241, 255,
227, 219, 171,  75, 150,  49,  98, 196, 149,  55, 110, 220, 165,  87, 174,  65,
130,  25,  50, 100, 200, 141,   7,  14,  28,  56, 112, 224, 221, 167,  83, 166,
 81, 162,  89, 178, 121, 242, 249, 239, 195, 155,  43,  86, 172,  69, 138,   9,
 18,  36,  72, 144,  61, 122, 244, 245, 247, 243, 251, 235, 203, 139,  11,  22,
 44,  88, 176, 125, 250, 233, 207, 131,  27,  54, 108, 216, 173,  71, 142,   1
};

/* GF(2^8) 系数到指数表 */
static const unsigned char m_int_to_exp[] =
{
  0,   0,   1,  25,   2,  50,  26, 198,   3, 223,  51, 238,  27, 104, 199,  75,
  4, 100, 224,  14,  52, 141, 239, 129,  28, 193, 105, 248, 200,   8,  76, 113,
  5, 138, 101,  47, 225,  36,  15,  33,  53, 147, 142, 218, 240,  18, 130,  69,
 29, 181, 194, 125, 106,  39, 249, 185, 201, 154,   9, 120,  77, 228, 114, 166,
  6, 191, 139,  98, 102, 221,  48, 253, 226, 152,  37, 179,  16, 145,  34, 136,
 54, 208, 148, 206, 143, 150, 219, 189, 241, 210,  19,  92, 131,  56,  70,  64,
 30,  66, 182, 163, 195,  72, 126, 110, 107,  58,  40,  84, 250, 133, 186,  61,
202,  94, 155, 159,  10,  21, 121,  43,  78, 212, 229, 172, 115, 243, 167,  87,
  7, 112, 192, 247, 140, 128,  99,  13, 103,  74, 222, 237,  49, 197, 254,  24,
227, 165, 153, 119,  38, 184, 180, 124,  17,  68, 146, 217,  35,  32, 137,  46,
 55,  63, 209,  91, 149, 188, 207, 205, 144, 135, 151, 178, 220, 252, 190,  97,
242,  86, 211, 171,  20,  42,  93, 158, 132,  60,  57,  83,  71, 109,  65, 162,
 31,  45,  67, 216, 183, 123, 164, 118, 196,  23,  73, 236, 127,  12, 111, 246,
108, 161,  59,  82,  41, 157,  85, 170, 251,  96, 134, 177, 187, 204,  62,  90,
203,  89,  95, 176, 156, 169, 160,  81,  11, 245,  22, 235, 122, 117,  44, 215,
 79, 174, 213, 233, 230, 231, 173, 232, 116, 214, 244, 234, 168,  80,  88, 175
};

/* 生成多项式系数表（按纠错码字数索引） */
static const unsigned char m_rs_exp_7[] = { 87,229,146,149,238,102,21 };
static const unsigned char m_rs_exp_10[] = { 251,67,46,61,118,70,64,94,32,45 };
static const unsigned char m_rs_exp_13[] = { 74,152,176,100,86,100,106,104,130,218,206,140,78 };
static const unsigned char m_rs_exp_15[] = { 8,183,61,91,202,37,51,58,58,237,140,124,5,99,105 };
static const unsigned char m_rs_exp_16[] = { 120,104,107,109,102,161,76,3,91,191,147,169,182,194,225,120 };
static const unsigned char m_rs_exp_17[] = { 43,139,206,78,43,239,123,206,214,147,24,99,150,39,243,163,136 };
static const unsigned char m_rs_exp_18[] = { 215,234,158,94,184,97,118,170,79,187,152,148,252,179,5,98,96,153 };
static const unsigned char m_rs_exp_20[] = { 17,60,79,50,61,163,26,187,202,180,221,225,83,239,156,164,212,212,188,190 };
static const unsigned char m_rs_exp_22[] = { 210,171,247,242,93,230,14,109,221,53,200,74,8,172,98,80,219,134,160,105,165,231 };
static const unsigned char m_rs_exp_24[] = { 229,121,135,48,211,117,251,126,159,180,169,152,192,226,228,218,111,0,117,232,87,96,227,21 };
static const unsigned char m_rs_exp_26[] = { 173,125,158,2,103,182,118,17,145,201,111,28,165,53,161,21,245,142,13,102,48,227,153,145,218,70 };
static const unsigned char m_rs_exp_28[] = { 168,223,200,104,224,234,108,180,110,190,195,147,205,27,232,201,21,43,245,87,42,195,212,119,242,37,9,123 };
static const unsigned char m_rs_exp_30[] = { 41,173,145,152,216,31,179,182,50,48,110,86,239,96,222,125,42,173,226,193,224,130,156,37,251,216,238,40,192,180 };
static const unsigned char m_rs_exp_32[] = { 10,6,106,190,249,167,4,67,209,138,138,32,242,123,89,27,120,185,80,156,38,69,171,60,28,222,80,52,254,185,220,241 };
static const unsigned char m_rs_exp_34[] = { 111,77,146,94,26,21,108,19,105,94,113,193,86,140,163,125,58,158,229,239,218,103,56,70,114,61,183,129,167,13,98,62,129,51 };
static const unsigned char m_rs_exp_36[] = { 200,183,98,16,172,31,246,234,60,152,115,0,167,152,113,248,238,107,18,63,218,37,87,210,105,177,120,74,121,196,117,251,113,233,30,120 };
static const unsigned char m_rs_exp_38[] = { 159,34,38,228,230,59,243,95,49,218,176,164,20,65,45,111,39,81,49,118,113,222,193,250,242,168,217,41,164,247,177,30,238,18,120,153,60,193 };
static const unsigned char m_rs_exp_40[] = { 59,116,79,161,252,98,128,205,128,161,247,57,163,56,235,106,53,26,187,174,226,104,170,7,175,35,181,114,88,41,47,163,125,134,72,20,232,53,35,15 };
static const unsigned char m_rs_exp_42[] = { 250,103,221,230,25,18,137,231,0,3,58,242,221,191,110,84,230,8,188,106,96,147,15,131,139,34,101,223,39,101,213,199,237,254,201,123,171,162,194,117,50,96 };
static const unsigned char m_rs_exp_44[] = { 190,7,61,121,71,246,69,55,168,188,89,243,191,25,72,123,9,145,14,247,1,238,44,78,143,62,224,126,118,114,68,163,52,194,217,147,204,169,37,130,113,102,73,181 };
static const unsigned char m_rs_exp_46[] = { 112,94,88,112,253,224,202,115,187,99,89,5,54,113,129,44,58,16,135,216,169,211,36,1,4,96,60,241,73,104,234,8,249,245,119,174,52,25,157,224,43,202,223,19,82,15 };
static const unsigned char m_rs_exp_48[] = { 228,25,196,130,211,146,60,24,251,90,39,102,240,61,178,63,46,123,115,18,221,111,135,160,182,205,107,206,95,150,120,184,91,21,247,156,140,238,191,11,94,227,84,50,163,39,34,108 };
static const unsigned char m_rs_exp_50[] = { 232,125,157,161,164,9,118,46,209,99,203,193,35,3,209,111,195,242,203,225,46,13,32,160,126,209,130,160,242,215,242,75,77,42,189,32,113,65,124,69,228,114,235,175,124,170,215,232,133,205 };
static const unsigned char m_rs_exp_52[] = { 116,50,86,186,50,220,251,89,192,46,86,127,124,19,184,233,151,215,22,14,59,145,37,242,203,134,254,89,190,94,59,65,124,113,100,233,235,121,22,76,86,97,39,242,200,220,101,33,239,254,116,51 };
static const unsigned char m_rs_exp_54[] = { 183,26,201,87,210,221,113,21,46,65,45,50,238,184,249,225,102,58,209,218,109,165,26,95,184,192,52,245,35,254,238,175,172,79,123,25,122,43,120,108,215,80,128,201,235,8,153,59,101,31,198,76,31,156 };
static const unsigned char m_rs_exp_56[] = { 106,120,107,157,164,216,112,116,2,91,248,163,36,201,202,229,6,144,254,155,135,208,170,209,12,139,127,142,182,249,177,174,190,28,10,85,239,184,101,124,152,206,96,23,163,61,27,196,247,151,154,202,207,20,61,10 };
static const unsigned char m_rs_exp_58[] = { 82,116,26,247,66,27,62,107,252,182,200,185,235,55,251,242,210,144,154,237,176,141,192,248,152,249,206,85,253,142,65,165,125,23,24,30,122,240,214,6,129,218,29,145,127,134,206,245,117,29,41,63,159,142,233,125,148,123 };
static const unsigned char m_rs_exp_60[] = { 107,140,26,12,9,141,243,197,226,197,219,45,211,101,219,120,28,181,127,6,100,247,2,205,198,57,115,219,101,109,160,82,37,38,238,49,160,209,121,86,11,124,30,181,84,25,194,87,65,102,190,220,70,27,209,16,89,7,33,240 };
static const unsigned char m_rs_exp_62[] = { 65,202,113,98,71,223,248,118,214,94,0,122,37,23,2,228,58,121,7,105,135,78,243,118,70,76,223,89,72,50,70,111,194,17,212,126,181,35,221,117,235,11,229,149,147,123,213,40,115,6,200,100,26,246,182,218,127,215,36,186,110,106 };
static const unsigned char m_rs_exp_64[] = { 45,51,175,9,7,158,159,49,68,119,92,123,177,204,187,254,200,78,141,149,119,26,127,53,160,93,199,212,29,24,145,156,208,150,218,209,4,216,91,47,184,146,47,140,195,195,125,242,238,63,99,108,140,230,242,31,204,11,178,243,217,156,213,231 };
static const unsigned char m_rs_exp_66[] = { 5,118,222,180,136,136,162,51,46,117,13,215,81,17,139,247,197,171,95,173,65,137,178,68,111,95,101,41,72,214,169,197,95,7,44,154,77,111,236,40,121,143,63,87,80,253,240,126,217,77,34,232,106,50,168,82,76,146,67,106,171,25,132,93,45,105 };
static const unsigned char m_rs_exp_68[] = { 247,159,223,33,224,93,77,70,90,160,32,254,43,150,84,101,190,205,133,52,60,202,165,220,203,151,93,84,15,84,253,173,160,89,227,52,199,97,95,231,52,177,41,125,137,241,166,225,118,2,54,32,82,215,175,198,43,238,235,27,101,184,127,3,5,8,163,238 };

static const unsigned char* m_rs_exp[] =
{
    NULL,        NULL,      NULL,        NULL,        NULL,        NULL,        NULL,        m_rs_exp_7,  NULL,        NULL,
    m_rs_exp_10, NULL,      NULL,        m_rs_exp_13, NULL,        m_rs_exp_15, m_rs_exp_16, m_rs_exp_17, m_rs_exp_18, NULL,
    m_rs_exp_20, NULL,      m_rs_exp_22, NULL,        m_rs_exp_24, NULL,        m_rs_exp_26, NULL,        m_rs_exp_28, NULL,
    m_rs_exp_30, NULL,      m_rs_exp_32, NULL,        m_rs_exp_34, NULL,        m_rs_exp_36, NULL,        m_rs_exp_38, NULL,
    m_rs_exp_40, NULL,      m_rs_exp_42, NULL,        m_rs_exp_44, NULL,        m_rs_exp_46, NULL,        m_rs_exp_48, NULL,
    m_rs_exp_50, NULL,      m_rs_exp_52, NULL,        m_rs_exp_54, NULL,        m_rs_exp_56, NULL,        m_rs_exp_58, NULL,
    m_rs_exp_60, NULL,      m_rs_exp_62, NULL,        m_rs_exp_64, NULL,        m_rs_exp_66, NULL,        m_rs_exp_68, NULL,
};

/* 内部 RS 编码函数 */
static void rs_encode_block(XByteArray* block, int data_count, int ecc_count)
{
    unsigned char* data = XByteArray_data(block);
    int i, j;
    unsigned char exp_first;
    unsigned char exp_element;

    for (i = 0; i < data_count; i++) {
        if (data[0] != 0) {
            exp_first = m_int_to_exp[data[0]];
            for (j = 0; j < ecc_count; j++) {
                exp_element = (m_rs_exp[ecc_count][j] + exp_first) % 255;
                data[j] = (data[j + 1] ^ m_exp_to_int[exp_element]);
            }
            for (j = ecc_count; j < data_count + ecc_count - 1; j++) {
                data[j] = data[j + 1];
            }
        }
        else {
            for (j = 0; j < data_count + ecc_count - 1; j++) {
                data[j] = data[j + 1];
            }
        }
    }
}

/* =========================== QR 编码实现 =========================== */
#define MAX_CODE_WORD   292
#define MAX_DATA_CODE   232
#define MAX_RSEC_CODE   146
#define MAX_QRCODE_SIZE 53

typedef struct {
    unsigned short version;
    unsigned short code_word_count;
    unsigned short data_code_count;
    unsigned short align_point_count;
    unsigned short align_point[6];
    unsigned short rsec_block;             /* RS 块数 */
    unsigned short rsec_ec_count;          /* 每块纠错码字数（块内等长） */
    unsigned short rsec_block_data_count;  /* 长块数据码字数 */
    unsigned short rsec_block_short;       /* 短块数（0=全部等长；标准要求短块排在块序前列） */
    unsigned short rsec_block_short_data;  /* 短块数据码字数 */
} qrcode_info_t;

/* 纠错等级参数表（ISO 18004 分块结构；0=L 1=M 2=Q 3=H，M/Q 暂未内置）。
 * L：约 7% 可恢复冗余；H：约 30%——中心内嵌图（logo）场景必须用 H，
 * 否则预留空白吃掉的模块超出 L 的纠错能力，真机不可识（2026-10-08
 * 用户手机实测）。H 级 v5/v7/v8/v9 为不等长分块（短块在前）。 */
#define XRCODE_LEVEL_L 0
#define XRCODE_LEVEL_M 1
#define XRCODE_LEVEL_Q 2
#define XRCODE_LEVEL_H 3

static const qrcode_info_t m_qrcode_info[4][10] = {
    /* [0] = L 级（历史缺省，全部等长分块） */
    {
        {0},
        {1,  26,  19, 0, {0},     1,  7, 19, 0, 0},
        {2,  44,  34, 1, {18},    1, 10, 34, 0, 0},
        {3,  70,  55, 1, {22},    1, 15, 55, 0, 0},
        {4, 100,  80, 1, {26},    1, 20, 80, 0, 0},
        {5, 134, 108, 1, {30},    1, 26, 108, 0, 0},
        {6, 172, 136, 1, {34},    2, 18, 68, 0, 0},
        {7, 196, 156, 2, {22,38}, 2, 20, 78, 0, 0},
        {8, 242, 194, 2, {24,42}, 2, 24, 97, 0, 0},
        {9, 292, 232, 2, {26,46}, 2, 30, 116, 0, 0}
    },
    /* [1] = M 级（未内置） */
    { {0},{0},{0},{0},{0},{0},{0},{0},{0},{0} },
    /* [2] = Q 级（未内置） */
    { {0},{0},{0},{0},{0},{0},{0},{0},{0},{0} },
    /* [3] = H 级（内嵌图场景；v5+ 不等长分块，短块在前） */
    {
        {0},
        {1,  26,   9, 0, {0},     1, 17,  9, 0, 0},
        {2,  44,  16, 1, {18},    1, 28, 16, 0, 0},
        {3,  70,  26, 1, {22},    2, 22, 13, 0, 0},
        {4, 100,  36, 1, {26},    4, 16,  9, 0, 0},
        {5, 134,  46, 1, {30},    4, 22, 12, 2, 11},
        {6, 172,  60, 1, {34},    4, 28, 15, 0, 0},
        {7, 196,  66, 2, {22,38}, 6, 26, 14, 4, 13},
        {8, 242,  86, 2, {24,42}, 6, 26, 15, 2, 14},
        {9, 292, 100, 2, {26,46}, 8, 24, 13, 4, 12}
    }
};

struct XRcode {
    XByteArray data_code;
    XByteArray rsec_code;
    XByteArray code_word;
    XByteArray matrix;
    int size;
    int version;
    int level;                    /* 本次编码的纠错等级（0=L 3=H） */
    const qrcode_info_t* m_info;  /* 本次编码使用的等级/版本参数行 */
};

static int  set_encode_data(XRcode* qr, const XByteArray* data);
static int  set_data_code(XRcode* qr, int index, int data, int size);
static int  check_version(int version, int bits_count, int reserved_blank, int level);
static int  set_padding_byte(XRcode* qr, int data_bits_count);
static int  set_code_word(XRcode* qr);
static void format_qrcode_data(XRcode* qr, int version, int code_word_count, int reserved_blank);
static void set_function_patterns(XRcode* qr, int version);
static void set_postion_pattern(XRcode* qr, int x, int y);
static void set_separator_pattern(XRcode* qr);
static void set_alignment_pattern(XRcode* qr, int x, int y);
static void set_timing_pattern(XRcode* qr);
static void set_version_info(XRcode* qr, int version);
static void set_code_word_pattern(XRcode* qr, int code_word_count);
static void get_masking_pattern(XRcode* qr, int* mask);
static void set_masking_pattern(XRcode* qr, int masking);
static void set_format_info(XRcode* qr, int masking);
static int  get_penalty_count(XRcode* qr);

static inline unsigned char matrix_get(const XRcode* qr, int x, int y) {
    return XByteArray_at_base(&qr->matrix, y * qr->size + x);
}
static inline void matrix_set(XRcode* qr, int x, int y, unsigned char val) {
    XByteArray_at_base(&qr->matrix, y * qr->size + x) = val;
}

XRcode* XRcode_create(void) {
    XRcode* qr = (XRcode*)XMalloc_System(sizeof(XRcode));
    if (!qr) return NULL;
    memset(qr, 0, sizeof(XRcode));
    XByteArray_init(&qr->data_code, true);
    XByteArray_init(&qr->rsec_code, true);
    XByteArray_init(&qr->code_word, true);
    XByteArray_init(&qr->matrix, true);
    XByteArray_resize_base(&qr->data_code, MAX_DATA_CODE);
    XByteArray_resize_base(&qr->rsec_code, MAX_RSEC_CODE);
    XByteArray_resize_base(&qr->code_word, MAX_CODE_WORD);
    return qr;
}

void XRcode_delete(XRcode* qr) {
    if (!qr) return;
    XClassDeinit(&qr->data_code);
    XClassDeinit(&qr->rsec_code);
    XClassDeinit(&qr->code_word);
    XClassDeinit(&qr->matrix);
    XFree_System(qr);
}

/* 检查指定版本是否满足数据容量和预留空白限制（按纠错等级取参数行）。
 * 预留空白占版面上限：L 级 15%（约 7% 冗余的一半），H 级 30%——
 * H 的 30% 冗余足以吃下中心留白，这正是内嵌图必须走 H 的原因。 */
static int check_version(int version, int bits_count, int reserved_blank,
                         int level) {
    const qrcode_info_t* info;
    int data_capacity;
    int total_modules;
    double blank_ratio;
    if (version < 1 || version > 9 || level < 0 || level > 3) return 0;
    info = &m_qrcode_info[level][version];
    if (info->version == 0) return 0; /* 该等级未内置 */
    data_capacity = info->data_code_count * 8;
    if (bits_count > data_capacity) return 0;
    if (reserved_blank == 0) return 1;
    total_modules = (version * 4 + 17) * (version * 4 + 17);
    blank_ratio = (level == XRCODE_LEVEL_H) ? 0.30 : 0.15;
    return (reserved_blank * reserved_blank <=
            (int)(total_modules * blank_ratio));
}

/* 自动选择最小合适版本 */
static int get_auto_version(int bits_count, int reserved_blank, int level) {
    for (int v = 1; v <= 9; ++v) {
        if (check_version(v, bits_count, reserved_blank, level))
            return v;
    }
    return 0;
}

bool XRcode_encode_ex(XRcode* qr, const XByteArray* data, int reserved_blank,
                      int version, int level) {
    int data_bits_count;
    int final_version;
    if (!qr || !data || XByteArray_isEmpty_base(data)) return false;
    if (reserved_blank < 0) reserved_blank = 0;
    if (version < 0 || version > 9) return false;
    if (level < 0 || level > 3) return false;
    if (m_qrcode_info[level][version > 0 ? version : 1].version == 0)
        return false; /* M/Q 暂未内置 */
    qr->level = level;

    data_bits_count = set_encode_data(qr, data);
    if (data_bits_count == 0) return false;

    final_version = version;
    if (final_version == 0) {
        final_version = get_auto_version(data_bits_count, reserved_blank, level);
    }
    else {
        if (!check_version(final_version, data_bits_count, reserved_blank, level))
            return false;
    }
    if (final_version == 0) return false;

    qr->m_info = &m_qrcode_info[level][final_version];
    set_padding_byte(qr, data_bits_count);
    set_code_word(qr);
    format_qrcode_data(qr, final_version, qr->m_info->code_word_count,
                       reserved_blank);
    return true;
}

bool XRcode_encode(XRcode* qr, const XByteArray* data, int reserved_blank, int version) {
    return XRcode_encode_ex(qr, data, reserved_blank, version, XRCODE_LEVEL_L);
}

int XRcode_size(const XRcode* qr) { return qr ? qr->size : 0; }
const XByteArray* XRcode_matrix(const XRcode* qr) { return qr ? &qr->matrix : NULL; }
const XByteArray* XRcode_codeWord(const XRcode* qr)
{ return qr ? &qr->code_word : NULL; }
int XRcode_codeWordCount(const XRcode* qr)
{ return (qr && qr->m_info) ? qr->m_info->code_word_count : 0; }

void XRcode_print_matrix(const XRcode* qr) {
    if (!qr || qr->size == 0) return;
    int sz = qr->size;
    for (int y = 0; y < sz; ++y) {
        for (int x = 0; x < sz; ++x)
            XPrintf("%c", matrix_get(qr, x, y) ? '#' : ' ');
        XPrintf("%c", '\n');
    }
    XPrintf("%c", '\n');
}

/* ---------- 内部 QR 编码函数实现 ---------- */
static int set_encode_data(XRcode* qr, const XByteArray* data) {
    size_t size = XByteArray_size_base(data);
    if ((size + 2) > MAX_DATA_CODE) return 0;
    memset(XByteArray_data(&qr->data_code), 0, MAX_DATA_CODE);
    int count = 0;
    count = set_data_code(qr, count, 0x04, 4);
    count = set_data_code(qr, count, (int)size, 8);
    for (size_t i = 0; i < size; ++i)
        count = set_data_code(qr, count, XByteArray_at_base(data, i), 8);
    count = set_data_code(qr, count, 0x00, 4);
    return count;
}

static int set_data_code(XRcode* qr, int index, int data, int size) {
    unsigned char* buf = XByteArray_data(&qr->data_code);
    for (int i = 0; i < size; ++i) {
        if (data & (1 << (size - i - 1)))
            buf[(index + i) / 8] |= 1 << (7 - ((index + i) % 8));
    }
    return index + size;
}

static int set_padding_byte(XRcode* qr, int data_bits_count) {
    int data_code_count = qr->m_info->data_code_count;
    unsigned char padding = 0xEC;
    unsigned char* buf = XByteArray_data(&qr->data_code);
    for (int i = (data_bits_count + 7) / 8; i < data_code_count; ++i) {
        buf[i] = padding;
        padding = (padding == 0xEC) ? 0x11 : 0xEC;
    }
    return data_code_count;
}

/* 码字交织：数据列优先取（短块先耗尽，标准要求短块排在块序前列），
 * 纠错码字接在全部数据码字之后同样列优先。此前只支持等长分块，
 * H 级 v5/v7/v8/v9 的不等长分块（如 v5-H=2×11+2×12）依赖本泛化。 */
static int set_code_word(XRcode* qr) {
    const qrcode_info_t* info = qr->m_info;
    int code_word_count = info->code_word_count;
    int cw_block_count = info->rsec_block;
    int cw_ec_count = info->rsec_ec_count;
    int cw_data_long = info->rsec_block_data_count;
    int cw_data_short = info->rsec_block_short ? info->rsec_block_short_data
                                               : cw_data_long;
    int cw_short_blocks = info->rsec_block_short;
    unsigned char* code_buf = XByteArray_data(&qr->code_word);
    unsigned char* data_buf = XByteArray_data(&qr->data_code);
    int data_total = info->data_code_count;
    int cw_data_index = 0;
    int cw_out_index = 0;
    int off[9];
    int i;
    int j;
    int b;
    memset(code_buf, 0, code_word_count);
    /* 各块数据在 data_buf 中的源偏移（块间顺序拼接） */
    off[0] = 0;
    for (b = 1; b < cw_block_count; ++b)
        off[b] = off[b - 1] + ((b - 1 < cw_short_blocks) ? cw_data_short
                                                         : cw_data_long);
    /* 数据交织：列优先取码字，短块先耗尽（此前误写成顺序拼接，
       多块版本数据全部错位——2026-10-08 修）。 */
    for (j = 0; j < cw_data_long; ++j) {
        for (b = 0; b < cw_block_count; ++b) {
            int len = (b < cw_short_blocks) ? cw_data_short : cw_data_long;
            if (j < len)
                code_buf[cw_out_index++] = data_buf[off[b] + j];
        }
    }
    cw_data_index = 0;
    for (i = 0; i < cw_block_count; ++i) {
        int len = (i < cw_short_blocks) ? cw_data_short : cw_data_long;
        unsigned char* rsec_buf;
        memcpy(XByteArray_data(&qr->rsec_code), &data_buf[cw_data_index], len);
        rs_encode_block(&qr->rsec_code, len, cw_ec_count);
        rsec_buf = XByteArray_data(&qr->rsec_code);
        for (j = 0; j < cw_ec_count; ++j)
            code_buf[data_total + (j * cw_block_count) + i] = rsec_buf[j];
        cw_data_index += len;
    }
    (void)cw_out_index;
    return code_word_count;
}

static void format_qrcode_data(XRcode* qr, int version, int code_word_count, int reserved_blank) {
    int size = version * 4 + 17;
    qr->size = size;
    qr->version = version;
    XByteArray_resize_base(&qr->matrix, size * size);
    memset(XByteArray_data(&qr->matrix), 0, size * size);
    set_function_patterns(qr, version);
    set_code_word_pattern(qr, code_word_count);
    int masking;
    get_masking_pattern(qr, &masking);
    set_masking_pattern(qr, masking);
    set_format_info(qr, masking);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            unsigned char val = matrix_get(qr, x, y);
            matrix_set(qr, x, y, (val & 0x11) ? 1 : 0);
        }
    }
    /* 清空预留空白区域 */
    if (reserved_blank > 0 && reserved_blank <= size) {
        int cx = size / 2;
        int half = reserved_blank / 2;
        int start_x = cx - half;
        int start_y = cx - half;
        int end_x = start_x + reserved_blank;
        int end_y = start_y + reserved_blank;
        if (start_x < 0) start_x = 0;
        if (start_y < 0) start_y = 0;
        if (end_x > size) end_x = size;
        if (end_y > size) end_y = size;
        for (int y = start_y; y < end_y; ++y) {
            for (int x = start_x; x < end_x; ++x) {
                matrix_set(qr, x, y, 0);
            }
        }
    }
}

static void set_function_patterns(XRcode* qr, int version) {
    int size = qr->size;
    /* 表已升级为 [等级][版本] 二维——版本几何（校正点等）与等级无关，
       统一走本次编码的参数行（2026-10-08 修：残留一维索引会取到错误
       等级的表行，校正图案丢失且保留区错乱）。 */
    const qrcode_info_t* info = qr->m_info;
    (void)version;
    set_postion_pattern(qr, 0, 0);
    set_postion_pattern(qr, size - 7, 0);
    set_postion_pattern(qr, 0, size - 7);
    set_separator_pattern(qr);
    /* 定时图案先画：校正图案中心 (c,6)/(6,c) 正落在定时行/列上，若定时
       图案后画，会把 5x5 校正图案的中间一行/列改写成交替模块，校正图案
       被划坏——扫码器据此拒识（2026-10-09 实测 v5+ 每版必少两处）。 */
    set_timing_pattern(qr);
    /* 校正图案后画即可完整覆盖定时行/列上那两处（中心不在定位图案
       占位内的都会画，见 set_alignment_pattern 的跳过条件）。 */
    for (int i = 0; i < info->align_point_count; ++i) {
        int center = info->align_point[i];
        set_alignment_pattern(qr, center, 6);
        set_alignment_pattern(qr, 6, center);
        for (int j = 0; j < info->align_point_count; ++j)
            set_alignment_pattern(qr, center, info->align_point[j]);
    }
    set_version_info(qr, version);
}

static void set_postion_pattern(XRcode* qr, int x, int y) {
    const unsigned char pattern[] = { 0x7F,0x41,0x5D,0x5D,0x5D,0x41,0x7F };
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j)
            matrix_set(qr, x + j, y + i, (pattern[i] & (1 << (6 - j))) ? 0x30 : 0x20);
}

static void set_separator_pattern(XRcode* qr) {
    int size = qr->size;
    for (int i = 0; i < 8; ++i) {
        matrix_set(qr, i, 7, 0x20);
        matrix_set(qr, 7, i, 0x20);
        matrix_set(qr, size - 8, i, 0x20);
        matrix_set(qr, i, size - 8, 0x20);
        matrix_set(qr, size - 8 + i, 7, 0x20);
        matrix_set(qr, 7, size - 8 + i, 0x20);
    }
    for (int i = 0; i < 9; ++i) {
        matrix_set(qr, i, 8, 0x20);
        matrix_set(qr, 8, i, 0x20);
    }
    for (int i = 0; i < 8; ++i) {
        matrix_set(qr, size - 8 + i, 8, 0x20);
        matrix_set(qr, 8, size - 8 + i, 0x20);
    }
}

static void set_alignment_pattern(XRcode* qr, int x, int y) {
    const unsigned char pattern[] = { 0x1F,0x11,0x15,0x11,0x1F };
    /* 仅当中心落在三个定位图案（含分隔符）的 8x8 占位内才跳过——那三处
     * 本就不该画校正图案（ISO 18004 表 E.1：与定位图案重叠的中心省略）。
     * 2026-10-09 修：旧守卫是「中心格已被标记保留就跳过」，而校正图案
     * 中心 (c,6)/(6,c) 正压在定时图案上，定时图案先画就把它标了保留，
     * 于是 (c,6)、(6,c) 两处校正图案被整体漏画——v5+ 每版必少两处，
     * 扫码器找不到完整校正图案即拒识（2026-10-09 实测 v5-H 不可识）。
     * 跳过条件按 ISO 18004 表 E.1：只有与三个定位图案**中心**重合的
     * 中心才省略，即 (6,6)、(6,size-7)、(size-7,6) 三处——注意判定
     * 用的是中心坐标本身，不能用「落在 8x8 占位块内」，否则 (c,6)
     * 这类 y=6、x 较大的合法中心会被误判成在右上定位图案内而漏画。 */
    int size = qr->size;
    if ((x == 6 && y == 6) ||
        (x == 6 && y == size - 7) ||
        (x == size - 7 && y == 6))
        return;
    x -= 2; y -= 2;
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
            matrix_set(qr, x + j, y + i, (pattern[i] & (1 << (4 - j))) ? 0x30 : 0x20);
}

static void set_timing_pattern(XRcode* qr) {
    int size = qr->size;
    for (int i = 8; i <= size - 9; ++i) {
        unsigned char val = (i % 2 == 0) ? 0x30 : 0x20;
        matrix_set(qr, i, 6, val);
        matrix_set(qr, 6, i, val);
    }
}

/* 版本信息块（仅 v7~v9，18 位 = 6 位版本号 + 12 位 BCH(0x1F25)）：
 * 两份拷贝分列右上/左下定位图案内侧。此前为空桩——v7+ 的版本信息区
 * 未绘制也未标记保留，数据之字形直接把码字写进这些位置，解码器版本
 * BCH 校验失败且数据流错位，v7+ 全部不可识（2026-10-08 cv2 实测根因）。 */
static void set_version_info(XRcode* qr, int version) {
    int size = qr->size;
    int rem = version;
    int bits;
    int i;
    if (version < 7) return;
    for (i = 0; i < 12; ++i)
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    bits = (version << 12) | (rem & 0xFFF);
    for (i = 0; i < 18; ++i) {
        int bit = (bits >> i) & 1;
        int val = bit ? 0x30 : 0x20;
        matrix_set(qr, size - 11 + i % 3, i / 3, val); /* 右上块 */
        matrix_set(qr, i / 3, size - 11 + i % 3, val); /* 左下块 */
    }
}

/* 数据码字之字形摆放（ISO 18004 8.7.3）。
 * 2026-10-09 根修：此前「两列一组、组内同向」的成对走位是错的——它把
 * 同一组两列都按同一方向扫，与规范的蛇形次序不一致，导致码字写到了
 * 错误模块上。表现是矩阵结构（定位/定时/校正/格式位）全部正确、
 * 码本经自检也自洽，但任何扫码器都读不出内容（cv2 能定位四角却解不出
 * 文本，手机同样扫不出）。根因用一条已被 cv2 正确解码的历史 PNG 作
 * oracle 反查确认：按下面的对角走位 + 该 PNG 自称的 mask3，能逐位
 * 还原出 byte 模式的正确码字（42 76 87 47…，即 'https://...' 开头）；
 * 换成成对走位则还原为乱码。
 * 走位用 x/kx 与 y/ky 双游标：每次 x 前进一格并反向，当 x 反向时 y 沿
 * ky 走一格；y 越界即翻转 ky 并令 x 退回两格（跳过时序列 x==6 再退一格），
 * 这正是规范 8.7.3 的 2×2 模块蛇形步进。 */
static void set_code_word_pattern(XRcode* qr, int code_word_count) {
    int size = qr->size;
    unsigned char* code_buf = XByteArray_data(&qr->code_word);
    int x = size;
    int y = size - 1;
    int kx = 1;
    int ky = 1;
    for (int i = 0; i < code_word_count; ++i) {
        for (int j = 0; j < 8; ++j) {
            /* 沿蛇形步进，直到落在一个非保留（数据）模块上。 */
            while (1) {
                x += kx;
                kx = -kx;
                if (kx < 0) {
                    y += ky;
                    if (y < 0 || y == size) {
                        y = (y < 0) ? 0 : (size - 1);
                        ky = -ky;
                        x -= 2;
                        if (x == 6) x--;
                    }
                }
                if (!(matrix_get(qr, x, y) & 0x20))
                    break;
            }
            matrix_set(qr, x, y,
                (code_buf[i] & (1 << (7 - j))) ? 0x02 : 0x00);
        }
    }
}

static void get_masking_pattern(XRcode* qr, int* mask) {
    int best = 0, best_penalty = 0x7FFFFFFF;
    for (int m = 0; m < 8; ++m) {
        set_masking_pattern(qr, m);
        set_format_info(qr, m);
        int penalty = get_penalty_count(qr);
        if (penalty < best_penalty) {
            best_penalty = penalty;
            best = m;
        }
    }
    *mask = best;
}

static void set_masking_pattern(XRcode* qr, int masking) {
    int size = qr->size;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!(matrix_get(qr, x, y) & 0x20)) {
                int mask = 0;
                switch (masking) {
                case 0: mask = ((x + y) % 2 == 0); break;
                case 1: mask = (y % 2 == 0); break;
                case 2: mask = (x % 3 == 0); break;
                case 3: mask = ((x + y) % 3 == 0); break;
                case 4: mask = ((y / 2) + (x / 3)) % 2 == 0; break;
                case 5: mask = (((x * y) % 2) + ((x * y) % 3) == 0); break;
                case 6: mask = ((((x * y) % 2) + ((x * y) % 3)) % 2 == 0); break;
                default:mask = ((((x * y) % 3) + ((x + y) % 2)) % 2 == 0); break;
                }
                unsigned char val = matrix_get(qr, x, y);
                val = (val & 0xFE) | (((val & 0x02) > 1) ^ mask);
                matrix_set(qr, x, y, val);
            }
        }
    }
}

static void set_format_info(XRcode* qr, int masking) {
    int size = qr->size;
    /* 格式信息 5 位 = 纠错等级(2) << 3 | 掩码(3)。等级编码按 ISO 18004：
     * M=00, L=01, H=10, Q=11。此处此前把等级写死成 0x08（即 L），H 级
     * 内嵌图码对外谎报 L——解码器按 L 的分块/容量去解，必然失败，
     * 表现为「库内自检全过但所有扫码器都读不出」（2026-10-09 根因）。
     * m_level_bits 按「库内 level 序号」索引（XRCODE_LEVEL_L/M/Q/H =
     * 0/1/2/3），值即该等级在格式位里的 2 位编码。 */
    static const unsigned char m_level_bits[4] = { 1, 0, 3, 2 }; /* L M Q H */
    int lvl = qr->level;
    if (lvl < 0 || lvl > 3) lvl = 0;
    int bits = ((m_level_bits[lvl] << 3) + masking) << 10;
    int data = bits;
    /* BCH(15,5) 逐位多项式除法（生成式 0x537）。2026-10-08 修：旧循环
     * 只消最高 5 位（i<5 且移位量 4-i），对掩码 3/7（中间位为 1 的输
     * 入）余数错误——标准表 L/3=111010010011110、L/7=110100101011101
     * 与旧输出不符。逐位除法对任意输入正确。 */
    for (int i = 14; i >= 10; --i)
        if (data & (1 << i))
            data ^= (0x0537 << (i - 10));
    bits = data + bits;
    bits ^= 0x5412;
    /* 环绕左上定位图案的一段（matrix_set(x=col, y=row) 约定）。
     * 规范（ISO 18004 图 25 / nayuki drawFormatBits）：bits0~5 竖排
     * 在 col 8 的 rows 0..5；bit6=(col8,row7)；bit7=(col8,row8)；
     * bit8=(col7,row8)；bits9~14 横排在 row 8 的 cols 0..5。
     * 2026-10-09 修：此前把 bits0~5 写成 row8/cols0..5、bits9~14
     * 写成 col8/rows5..0——两段整体转置，位序全错，BCH 校验必败，
     * cv2 与手机一律拒识。CHK2 只读数据位故一直 0 mismatch 掩盖了
     * 此错（用 cv2.QRCodeEncoder 黄金矩阵逐位比对才定位到）。
     * 已用 cv2 黄金矩阵（'A'→v1，L/mask0=0x77C4）双向验证本布局。 */
    for (int i = 0; i <= 5; ++i)
        matrix_set(qr, 8, i, (bits & (1 << i)) ? 0x30 : 0x20);
    matrix_set(qr, 8, 7, (bits & (1 << 6)) ? 0x30 : 0x20);
    matrix_set(qr, 8, 8, (bits & (1 << 7)) ? 0x30 : 0x20);
    matrix_set(qr, 7, 8, (bits & (1 << 8)) ? 0x30 : 0x20);
    for (int i = 9; i <= 14; ++i)
        matrix_set(qr, 14 - i, 8, (bits & (1 << i)) ? 0x30 : 0x20);
    /* 右上/左下复制段（原坐标正确，保留） */
    for (int i = 0; i <= 7; ++i)
        matrix_set(qr, size - 1 - i, 8, (bits & (1 << i)) ? 0x30 : 0x20);
    matrix_set(qr, 8, size - 8, 0x30);
    for (int i = 8; i <= 14; ++i)
        matrix_set(qr, 8, size - 15 + i, (bits & (1 << i)) ? 0x30 : 0x20);
}

static int get_penalty_count(XRcode* qr) {
    int size = qr->size;
    int penalty = 0, count;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size - 4; ++x) {
            count = 1;
            int cur = (matrix_get(qr, x, y) & 0x11) ? 1 : 0;
            int k;
            for (k = x + 1; k < size; ++k) {
                if (((matrix_get(qr, k, y) & 0x11) ? 1 : 0) == cur)
                    count++;
                else break;
            }
            if (count >= 5) penalty += 3 + (count - 5);
            x = k - 1;
        }
    }
    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size - 4; ++y) {
            count = 1;
            int cur = (matrix_get(qr, x, y) & 0x11) ? 1 : 0;
            int k;
            for (k = y + 1; k < size; ++k) {
                if (((matrix_get(qr, x, k) & 0x11) ? 1 : 0) == cur)
                    count++;
                else break;
            }
            if (count >= 5) penalty += 3 + (count - 5);
            y = k - 1;
        }
    }
    for (int y = 0; y < size - 1; ++y) {
        for (int x = 0; x < size - 1; ++x) {
            int v = (matrix_get(qr, x, y) & 0x11) ? 1 : 0;
            if (v == ((matrix_get(qr, x + 1, y) & 0x11) ? 1 : 0) &&
                v == ((matrix_get(qr, x, y + 1) & 0x11) ? 1 : 0) &&
                v == ((matrix_get(qr, x + 1, y + 1) & 0x11) ? 1 : 0))
                penalty += 3;
        }
    }
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size - 6; ++x) {
            if ((matrix_get(qr, x, y) & 0x11) &&
                !(matrix_get(qr, x + 1, y) & 0x11) &&
                (matrix_get(qr, x + 2, y) & 0x11) &&
                (matrix_get(qr, x + 3, y) & 0x11) &&
                (matrix_get(qr, x + 4, y) & 0x11) &&
                !(matrix_get(qr, x + 5, y) & 0x11) &&
                (matrix_get(qr, x + 6, y) & 0x11))
                penalty += 40;
        }
    }
    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size - 6; ++y) {
            if ((matrix_get(qr, x, y) & 0x11) &&
                !(matrix_get(qr, x, y + 1) & 0x11) &&
                (matrix_get(qr, x, y + 2) & 0x11) &&
                (matrix_get(qr, x, y + 3) & 0x11) &&
                (matrix_get(qr, x, y + 4) & 0x11) &&
                !(matrix_get(qr, x, y + 5) & 0x11) &&
                (matrix_get(qr, x, y + 6) & 0x11))
                penalty += 40;
        }
    }
    int black = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (!(matrix_get(qr, x, y) & 0x11)) black++;
    int ratio = (black * 100) / (size * size);
    int diff = (ratio > 50) ? ratio - 50 : 50 - ratio;
    penalty += (diff / 5) * 10;
    return penalty;
}