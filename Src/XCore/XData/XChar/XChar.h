#ifndef XCHAR_H
#define XCHAR_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @file XChar.h
 * @brief XChar - 16位Unicode字符类，对齐Qt 6.8 QChar API
 *
 * XChar是一个轻量级的16位Unicode字符表示，等价于Qt的QChar。
 * 提供完整的Unicode字符分类、转换、比较和编码转换功能。
 *
 * @note XChar是值类型（typedef uint16_t），不是XClass派生类
 */

/* ========================================================================== */
/*                              枚举类型定义                                    */
/* ========================================================================== */

/**
 * @brief 字符大小写敏感性枚举
 * 用于指定字符串比较或查找时是否区分大小写
 */
typedef enum XChar_CaseSensitivity {
    XChar_CaseInsensitive,  /**< 不区分大小写 */
    XChar_CaseSensitive     /**< 区分大小写 */
} XChar_CaseSensitivity;

/**
 * @brief Unicode字符分类枚举（对齐Qt 6.8 QChar::Category）
 *
 * 映射Unicode标准定义的字符类别。
 * 规范性类别（Normative）和信息性类别（Informative）均包含在内。
 */
typedef enum XChar_Category {
    /* 规范性类别（Normative） */
    XChar_Mark_NonSpacing        = 0,  /**< Unicode类别 Mn，非间距标记 */
    XChar_Mark_SpacingCombining  = 1,  /**< Unicode类别 Mc，间距组合标记 */
    XChar_Mark_Enclosing         = 2,  /**< Unicode类别 Me，包围标记 */
    XChar_Number_DecimalDigit    = 3,  /**< Unicode类别 Nd，十进制数字 */
    XChar_Number_Letter          = 4,  /**< Unicode类别 Nl，数字字母 */
    XChar_Number_Other           = 5,  /**< Unicode类别 No，其他数字 */
    XChar_Separator_Space        = 6,  /**< Unicode类别 Zs，空格分隔符 */
    XChar_Separator_Line         = 7,  /**< Unicode类别 Zl，行分隔符 */
    XChar_Separator_Paragraph    = 8,  /**< Unicode类别 Zp，段落分隔符 */
    XChar_Other_Control          = 9,  /**< Unicode类别 Cc，控制字符 */
    XChar_Other_Format           = 10, /**< Unicode类别 Cf，格式控制字符 */
    XChar_Other_Surrogate        = 11, /**< Unicode类别 Cs，代理对 */
    XChar_Other_PrivateUse       = 12, /**< Unicode类别 Co，私用区 */
    XChar_Other_NotAssigned      = 13, /**< Unicode类别 Cn，未分配 */

    /* 信息性类别（Informative） */
    XChar_Letter_Uppercase       = 14, /**< Unicode类别 Lu，大写字母 */
    XChar_Letter_Lowercase       = 15, /**< Unicode类别 Ll，小写字母 */
    XChar_Letter_Titlecase       = 16, /**< Unicode类别 Lt，标题字母 */
    XChar_Letter_Modifier        = 17, /**< Unicode类别 Lm，修饰字母 */
    XChar_Letter_Other           = 18, /**< Unicode类别 Lo，其他字母 */
    XChar_Punctuation_Connector  = 19, /**< Unicode类别 Pc，连接标点 */
    XChar_Punctuation_Dash       = 20, /**< Unicode类别 Pd，破折号标点 */
    XChar_Punctuation_Open       = 21, /**< Unicode类别 Ps，开始标点 */
    XChar_Punctuation_Close      = 22, /**< Unicode类别 Pe，结束标点 */
    XChar_Punctuation_InitialQuote = 23, /**< Unicode类别 Pi，开始引号 */
    XChar_Punctuation_FinalQuote = 24, /**< Unicode类别 Pf，结束引号 */
    XChar_Punctuation_Other      = 25, /**< Unicode类别 Po，其他标点 */
    XChar_Symbol_Math            = 26, /**< Unicode类别 Sm，数学符号 */
    XChar_Symbol_Currency        = 27, /**< Unicode类别 Sc，货币符号 */
    XChar_Symbol_Modifier        = 28, /**< Unicode类别 Sk，修饰符号 */
    XChar_Symbol_Other           = 29  /**< Unicode类别 So，其他符号 */
} XChar_Category;

/**
 * @brief Unicode分解类型枚举（对齐Qt 6.8 QChar::Decomposition）
 *
 * 定义Unicode字符的分解属性，参见Unicode标准了解详细说明。
 */
typedef enum XChar_Decomposition {
    XChar_NoDecomposition = 0,  /**< 无分解 */
    XChar_Canonical       = 1,  /**< 规范分解 */
    XChar_Font            = 2,  /**< 字体变体 */
    XChar_NoBreak         = 3,  /**< 不可换行 */
    XChar_Initial         = 4,  /**< 阿拉伯语词首形式 */
    XChar_Medial          = 5,  /**< 阿拉伯语词中形式 */
    XChar_Final           = 6,  /**< 阿拉伯语词尾形式 */
    XChar_Isolated        = 7,  /**< 阿拉伯语独立形式 */
    XChar_Circle          = 8,  /**< 圆圈变体 */
    XChar_Super           = 9,  /**< 上标 */
    XChar_Sub             = 10, /**< 下标 */
    XChar_Vertical        = 11, /**< 竖排变体 */
    XChar_Wide            = 12, /**< 全角变体 */
    XChar_Narrow          = 13, /**< 半角变体 */
    XChar_Small           = 14, /**< 小型变体 */
    XChar_Square          = 15, /**< 正方形变体 */
    XChar_Compat          = 16, /**< 兼容分解 */
    XChar_Fraction        = 17  /**< 分数形式 */
} XChar_Decomposition;

/**
 * @brief Unicode方向属性枚举（对齐Qt 6.8 QChar::Direction）
 *
 * 定义Unicode字符的书写方向属性。
 * 为符合C/C++命名规范，Unicode标准中的代码前加"Dir"前缀。
 */
typedef enum XChar_Direction {
    XChar_DirL    = 0,  /**< 从左到右 */
    XChar_DirR    = 1,  /**< 从右到左 */
    XChar_DirEN   = 2,  /**< 欧洲数字 */
    XChar_DirES   = 3,  /**< 欧洲数字分隔符 */
    XChar_DirET   = 4,  /**< 欧洲数字终止符 */
    XChar_DirAN   = 5,  /**< 阿拉伯数字 */
    XChar_DirCS   = 6,  /**< 通用数字分隔符 */
    XChar_DirB    = 7,  /**< 段落分隔符 */
    XChar_DirS    = 8,  /**< 段落分隔符 */
    XChar_DirWS   = 9,  /**< 空格 */
    XChar_DirON   = 10, /**< 其他中性 */
    XChar_DirLRE  = 11, /**< 从左到右嵌入 */
    XChar_DirLRO  = 12, /**< 从左到右覆盖 */
    XChar_DirAL   = 13, /**< 从右到左的阿拉伯语 */
    XChar_DirRLE  = 14, /**< 从右到左嵌入 */
    XChar_DirRLO  = 15, /**< 从右到左覆盖 */
    XChar_DirPDF  = 16, /**< 弹出方向格式 */
    XChar_DirNSM  = 17, /**< 非间距标记 */
    XChar_DirBN   = 18, /**< 边界中性 */
    XChar_DirLRI  = 19, /**< 从左到右隔离 (since Qt 5.3) */
    XChar_DirRLI  = 20, /**< 从右到左隔离 (since Qt 5.3) */
    XChar_DirFSI  = 21, /**< 首方向隔离 (since Qt 5.3) */
    XChar_DirPDI  = 22  /**< 弹出方向隔离 (since Qt 5.3) */
} XChar_Direction;

/**
 * @brief Unicode连接类型枚举（对齐Qt 6.8 QChar::JoiningType）
 *
 * 定义Unicode字符的连接类型属性（主要用于阿拉伯语或叙利亚语）。
 * 为符合C/C++命名规范，前加"Joining_"前缀。
 */
typedef enum XChar_JoiningType {
    XChar_Joining_None       = 0, /**< 无连接 */
    XChar_Joining_Causing    = 1, /**< 引起连接 */
    XChar_Joining_Dual       = 2, /**< 双向连接 */
    XChar_Joining_Right      = 3, /**< 右连接 */
    XChar_Joining_Left       = 4, /**< 左连接 */
    XChar_Joining_Transparent = 5 /**< 透明连接 */
} XChar_JoiningType;

/**
 * @brief Unicode脚本属性枚举（对齐Qt 6.8 QChar::Script）
 *
 * 定义Unicode脚本属性值，参见Unicode Standard Annex #24。
 * 为符合C/C++命名规范，前加"Script_"前缀。
 */
typedef enum XChar_Script {
    XChar_Script_Unknown    = 0,   /**< 未分配、私用区、非字符和代理码位 */
    XChar_Script_Inherited  = 1,   /**< 继承脚本（非间距标记等） */
    XChar_Script_Common     = 2,   /**< 通用脚本 */
    XChar_Script_Latin      = 3,   /**< 拉丁文 */
    XChar_Script_Greek      = 4,   /**< 希腊文 */
    XChar_Script_Cyrillic   = 5,   /**< 西里尔文 */
    XChar_Script_Armenian   = 6,   /**< 亚美尼亚文 */
    XChar_Script_Hebrew     = 7,   /**< 希伯来文 */
    XChar_Script_Arabic     = 8,   /**< 阿拉伯文 */
    XChar_Script_Syriac     = 9,   /**< 叙利亚文 */
    XChar_Script_Thaana     = 10,  /**< 塔纳文 */
    XChar_Script_Devanagari = 11,  /**< 天城文 */
    XChar_Script_Bengali    = 12,  /**< 孟加拉文 */
    XChar_Script_Gurmukhi   = 13,  /**< 古尔穆基文 */
    XChar_Script_Gujarati   = 14,  /**< 古吉拉特文 */
    XChar_Script_Oriya      = 15,  /**< 奥里亚文 */
    XChar_Script_Tamil      = 16,  /**< 泰米尔文 */
    XChar_Script_Telugu     = 17,  /**< 泰卢固文 */
    XChar_Script_Kannada    = 18,  /**< 卡纳达文 */
    XChar_Script_Malayalam  = 19,  /**< 马拉雅拉姆文 */
    XChar_Script_Sinhala    = 20,  /**< 僧伽罗文 */
    XChar_Script_Thai       = 21,  /**< 泰文 */
    XChar_Script_Lao        = 22,  /**< 老挝文 */
    XChar_Script_Tibetan    = 23,  /**< 藏文 */
    XChar_Script_Myanmar    = 24,  /**< 缅甸文 */
    XChar_Script_Georgian   = 25,  /**< 格鲁吉亚文 */
    XChar_Script_Hangul     = 26,  /**< 韩文 */
    XChar_Script_Ethiopic   = 27,  /**< 埃塞俄比亚文 */
    XChar_Script_Cherokee   = 28,  /**< 切罗基文 */
    XChar_Script_CanadianAboriginal = 29, /**< 加拿大原住民音节 */
    XChar_Script_Ogham      = 30,  /**< 欧甘文 */
    XChar_Script_Runic      = 31,  /**< 卢恩文 */
    XChar_Script_Khmer      = 32,  /**< 高棉文 */
    XChar_Script_Mongolian  = 33,  /**< 蒙古文 */
    XChar_Script_Hiragana   = 34,  /**< 平假名 */
    XChar_Script_Katakana   = 35,  /**< 片假名 */
    XChar_Script_Bopomofo   = 36,  /**< 注音符号 */
    XChar_Script_Han        = 37,  /**< 汉字 */
    XChar_Script_Yi         = 38,  /**< 彝文 */
    XChar_Script_OldItalic  = 39,  /**< 古意大利文 */
    XChar_Script_Gothic     = 40,  /**< 哥特文 */
    XChar_Script_Deseret    = 41,  /**< 德塞雷特文 */
    XChar_Script_Tagalog    = 42,  /**< 他加禄文 */
    XChar_Script_Hanunoo    = 43,  /**< 哈努努文 */
    XChar_Script_Buhid      = 44,  /**< 布希德文 */
    XChar_Script_Tagbanwa   = 45,  /**< 塔格巴努阿文 */
    XChar_Script_Coptic     = 46,  /**< 科普特文 */
    XChar_Script_Limbu      = 47,  /**< 林布文 */
    XChar_Script_TaiLe      = 48,  /**< 傣哪文 */
    XChar_Script_LinearB    = 49,  /**< 线形文字B */
    XChar_Script_Ugaritic   = 50,  /**< 乌加里特文 */
    XChar_Script_Shavian    = 51,  /**< 萧伯纳字母 */
    XChar_Script_Osmanya    = 52,  /**< 奥斯曼亚文 */
    XChar_Script_Cypriot    = 53,  /**< 塞浦路斯文 */
    XChar_Script_Braille    = 54,  /**< 布莱叶盲文 */
    XChar_Script_Buginese   = 55,  /**< 布吉文 */
    XChar_Script_NewTaiLue  = 56,  /**< 新傣仂文 */
    XChar_Script_Glagolitic = 57,  /**< 格拉哥里文 */
    XChar_Script_Tifinagh   = 58,  /**< 提非纳文 */
    XChar_Script_SylotiNagri = 59, /**< 锡尔赫特文 */
    XChar_Script_OldPersian = 60,  /**< 古波斯楔形文 */
    XChar_Script_Kharoshthi = 61,  /**< 佉卢文 */
    XChar_Script_Balinese   = 62,  /**< 巴厘文 */
    XChar_Script_Cuneiform  = 63,  /**< 楔形文字 */
    XChar_Script_Phoenician = 64,  /**< 腓尼基文 */
    XChar_Script_PhagsPa    = 65,  /**< 八思巴文 */
    XChar_Script_Nko        = 66,  /**< 西非书面语言 */
    XChar_Script_Sundanese  = 67,  /**< 巽他文 */
    XChar_Script_Lepcha     = 68,  /**< 雷布查文 */
    XChar_Script_OlChiki    = 69,  /**< 桑塔利文 */
    XChar_Script_Vai        = 70,  /**< 瓦伊文 */
    XChar_Script_Saurashtra = 71,  /**< 索拉什特拉文 */
    XChar_Script_KayahLi    = 72,  /**< 克耶文 */
    XChar_Script_Rejang     = 73,  /**< 勒姜文 */
    XChar_Script_Lycian     = 74,  /**< 吕西亚文 */
    XChar_Script_Carian     = 75,  /**< 卡里亚文 */
    XChar_Script_Lydian     = 76,  /**< 吕底亚文 */
    XChar_Script_Cham       = 77,  /**< 占文 */
    XChar_Script_TaiTham    = 78,  /**< 傣仂文 */
    XChar_Script_TaiViet    = 79,  /**< 傣越文 */
    XChar_Script_Avestan    = 80,  /**< 阿维斯陀文 */
    XChar_Script_EgyptianHieroglyphs = 81, /**< 埃及象形文字 */
    XChar_Script_Samaritan  = 82,  /**< 撒马利亚文 */
    XChar_Script_Lisu       = 83,  /**< 傈僳文 */
    XChar_Script_Bamum      = 84,  /**< 巴穆姆文 */
    XChar_Script_Javanese   = 85,  /**< 爪哇文 */
    XChar_Script_MeeteiMayek = 86, /**< 曼尼普尔文 */
    XChar_Script_ImperialAramaic = 87, /**< 帝国阿拉米文 */
    XChar_Script_OldSouthArabian = 88, /**< 古南阿拉伯文 */
    XChar_Script_InscriptionalParthian = 89, /**< 帕提亚铭文 */
    XChar_Script_InscriptionalPahlavi = 90, /**< 巴列维铭文 */
    XChar_Script_OldTurkic  = 91,  /**< 古突厥文 */
    XChar_Script_Kaithi     = 92,  /**< 凯提文 */
    XChar_Script_Batak      = 93,  /**< 巴塔克文 */
    XChar_Script_Brahmi     = 94,  /**< 婆罗米文 */
    XChar_Script_Mandaic    = 95,  /**< 曼达安文 */
    XChar_Script_Chakma     = 96,  /**< 查克马文 */
    XChar_Script_MeroiticCursive = 97, /**< 麦罗埃草书 */
    XChar_Script_MeroiticHieroglyphs = 98, /**< 麦罗埃象形文字 */
    XChar_Script_Miao       = 99,  /**< 苗文 */
    XChar_Script_Sharada    = 100, /**< 夏拉达文 */
    XChar_Script_SoraSompeng = 101, /**< 索拉僧平文 */
    XChar_Script_Takri      = 102, /**< 塔克里文 */
    XChar_Script_CaucasianAlbanian = 103, /**< 高加索阿尔巴尼亚文 */
    XChar_Script_BassaVah   = 104, /**< 巴萨文 */
    XChar_Script_Duployan   = 105, /**< 杜普洛速记 */
    XChar_Script_Elbasan    = 106, /**< 埃尔巴桑文 */
    XChar_Script_Grantha    = 107, /**< 格兰塔文 */
    XChar_Script_PahawhHmong = 108, /**< 帕豪苗文 */
    XChar_Script_Khojki     = 109, /**< 霍吉基文 */
    XChar_Script_LinearA    = 110, /**< 线形文字A */
    XChar_Script_Mahajani   = 111, /**< 马哈贾尼文 */
    XChar_Script_Manichaean = 112, /**< 摩尼文 */
    XChar_Script_MendeKikakui = 113, /**< 门德基卡奎文 */
    XChar_Script_Modi       = 114, /**< 莫迪文 */
    XChar_Script_Mro        = 115, /**< 姆罗文 */
    XChar_Script_OldNorthArabian = 116, /**< 古北阿拉伯文 */
    XChar_Script_Nabataean  = 117, /**< 纳巴泰文 */
    XChar_Script_Palmyrene  = 118, /**< 帕尔米拉文 */
    XChar_Script_PauCinHau  = 119, /**< 包钦豪文 */
    XChar_Script_OldPermic  = 120, /**< 古彼尔姆文 */
    XChar_Script_PsalterPahlavi = 121, /**< 诗篇巴列维文 */
    XChar_Script_Siddham    = 122, /**< 悉昙文 */
    XChar_Script_Khudawadi  = 123, /**< 库达瓦迪文 */
    XChar_Script_Tirhuta    = 124, /**< 蒂尔胡塔文 */
    XChar_Script_WarangCiti = 125, /**< 瓦朗奇蒂文 */
    XChar_Script_Ahom       = 126, /**< 阿洪姆文 */
    XChar_Script_AnatolianHieroglyphs = 127, /**< 安纳托利亚象形文字 */
    XChar_Script_Hatran     = 128, /**< 哈特拉文 */
    XChar_Script_Multani    = 129, /**< 穆尔塔尼文 */
    XChar_Script_OldHungarian = 130, /**< 古匈牙利文 */
    XChar_Script_SignWriting = 131, /**< 手语书写 */
    XChar_Script_Adlam      = 132, /**< 阿德拉姆文 */
    XChar_Script_Bhaiksuki  = 133, /**< 拜克苏基文 */
    XChar_Script_Marchen    = 134, /**< 象雄文 */
    XChar_Script_Newa       = 135, /**< 尼瓦尔文 */
    XChar_Script_Osage      = 136, /**< 奥塞奇文 */
    XChar_Script_Tangut     = 137, /**< 西夏文 */
    XChar_Script_MasaramGondi = 138, /**< 马萨拉姆贡德文 */
    XChar_Script_Nushu      = 139, /**< 女书 */
    XChar_Script_Soyombo    = 140, /**< 索永布文 */
    XChar_Script_ZanabazarSquare = 141, /**< 扎纳巴扎尔方块文 */
    XChar_Script_Dogra      = 142, /**< 多格拉文 */
    XChar_Script_GunjalaGondi = 143, /**< 贡贾拉贡德文 */
    XChar_Script_HanifiRohingya = 144, /**< 哈尼菲罗兴亚文 */
    XChar_Script_Makasar    = 145, /**< 望加锡文 */
    XChar_Script_Medefaidrin = 146, /**< 梅德法伊德林文 */
    XChar_Script_OldSogdian = 147, /**< 古粟特文 */
    XChar_Script_Sogdian    = 148, /**< 粟特文 */
    XChar_Script_Elymaic    = 149, /**< 埃利迈克文 */
    XChar_Script_Nandinagari = 150, /**< 南迪纳加里文 */
    XChar_Script_NyiakengPuachueHmong = 151, /**< 苗文变体 */
    XChar_Script_Wancho     = 152, /**< 万秋文 */
    XChar_Script_Chorasmian = 153, /**< 花拉子模文 */
    XChar_Script_DivesAkuru = 154, /**< 迪维斯阿库鲁文 */
    XChar_Script_KhitanSmallScript = 155, /**< 契丹小字 */
    XChar_Script_Yezidi     = 156, /**< 雅兹迪文 */
    XChar_Script_CyproMinoan = 157, /**< 塞浦路斯-米诺斯文 */
    XChar_Script_OldUyghur  = 158, /**< 古回鹘文 */
    XChar_Script_Tangsa     = 159, /**< 唐萨文 */
    XChar_Script_Toto       = 160, /**< 托托文 */
    XChar_Script_Vithkuqi   = 161, /**< 维特库奇文 */
    XChar_Script_Kawi       = 162, /**< 卡维文 */
    XChar_Script_NagMundari = 163, /**< 纳格蒙达里文 */
    XChar_Script_Garay      = 164, /**< 加拉伊文 */
    XChar_Script_GurungKhema = 165, /**< 古隆凯马文 */
    XChar_Script_KiratRai   = 166, /**< 基拉特拉伊文 */
    XChar_Script_OlOnal     = 167, /**< 奥洛纳尔文 */
    XChar_Script_Sunuwar   = 168, /**< 苏努瓦尔文 */
    XChar_Script_Todhri     = 169, /**< 托德里文 */
    XChar_Script_TuluTigalari = 170, /**< 图卢蒂格拉里文 */
    XChar_ScriptCount       = 171  /**< 脚本数量（哨兵值） */
} XChar_Script;

/**
 * @brief 特殊字符常量枚举（对齐Qt 6.8 QChar::SpecialCharacter）
 *
 * 定义常用的特殊Unicode字符常量。
 */
typedef enum XChar_SpecialCharacter {
    XChar_Null                  = 0x0000, /**< 空字符，isNull()返回true */
    XChar_Tabulation            = 0x0009, /**< 制表符 */
    XChar_LineFeed              = 0x000a, /**< 换行符 */
    XChar_FormFeed              = 0x000c, /**< 换页符 */
    XChar_CarriageReturn        = 0x000d, /**< 回车符 */
    XChar_Space                 = 0x0020, /**< 空格 */
    XChar_Nbsp                  = 0x00a0, /**< 不间断空格 */
    XChar_SoftHyphen            = 0x00ad, /**< 软连字符 */
    XChar_ReplacementCharacter  = 0xfffd, /**< 替换字符（字体无对应字形时显示） */
    XChar_ObjectReplacementCharacter = 0xfffc, /**< 对象替换字符（如图片） */
    XChar_ByteOrderMark         = 0xfeff, /**< 字节序标记 */
    XChar_ByteOrderSwapped      = 0xfffe, /**< 反转字节序标记 */
    XChar_ParagraphSeparator    = 0x2029, /**< 段落分隔符 */
    XChar_LineSeparator         = 0x2028, /**< 行分隔符 */
    XChar_VisualTabCharacter    = 0x2192, /**< 可视制表符（水平箭头，since Qt 6.2） */
    XChar_LastValidCodePoint    = 0x10ffff /**< 最后有效码点 */
} XChar_SpecialCharacter;

/**
 * @brief Unicode版本枚举（对齐Qt 6.8 QChar::UnicodeVersion）
 *
 * 指定引入特定字符的Unicode标准版本。
 */
typedef enum XChar_UnicodeVersion {
    XChar_Unicode_Unassigned = 0,  /**< 未分配（Unicode 8.0中无对应字符） */
    XChar_Unicode_1_1    = 1,  /**< Unicode 1.1 */
    XChar_Unicode_2_0    = 2,  /**< Unicode 2.0 */
    XChar_Unicode_2_1_2  = 3,  /**< Unicode 2.1.2 */
    XChar_Unicode_3_0    = 4,  /**< Unicode 3.0 */
    XChar_Unicode_3_1    = 5,  /**< Unicode 3.1 */
    XChar_Unicode_3_2    = 6,  /**< Unicode 3.2 */
    XChar_Unicode_4_0    = 7,  /**< Unicode 4.0 */
    XChar_Unicode_4_1    = 8,  /**< Unicode 4.1 */
    XChar_Unicode_5_0    = 9,  /**< Unicode 5.0 */
    XChar_Unicode_5_1    = 10, /**< Unicode 5.1 */
    XChar_Unicode_5_2    = 11, /**< Unicode 5.2 */
    XChar_Unicode_6_0    = 12, /**< Unicode 6.0 */
    XChar_Unicode_6_1    = 13, /**< Unicode 6.1 */
    XChar_Unicode_6_2    = 14, /**< Unicode 6.2 */
    XChar_Unicode_6_3    = 15, /**< Unicode 6.3 */
    XChar_Unicode_7_0    = 16, /**< Unicode 7.0 */
    XChar_Unicode_8_0    = 17, /**< Unicode 8.0 */
    XChar_Unicode_9_0    = 18, /**< Unicode 9.0 */
    XChar_Unicode_10_0   = 19, /**< Unicode 10.0 */
    XChar_Unicode_11_0   = 20, /**< Unicode 11.0 */
    XChar_Unicode_12_0   = 21, /**< Unicode 12.0 */
    XChar_Unicode_12_1   = 22, /**< Unicode 12.1 */
    XChar_Unicode_13_0   = 23, /**< Unicode 13.0 */
    XChar_Unicode_14_0   = 24, /**< Unicode 14.0 */
    XChar_Unicode_15_0   = 25, /**< Unicode 15.0 */
    XChar_Unicode_15_1   = 26, /**< Unicode 15.1 (since Qt 6.8) */
    XChar_Unicode_16_0   = 27  /**< Unicode 16.0 (since Qt 6.9) */
} XChar_UnicodeVersion;

/* ========================================================================== */
/*                              XChar类型定义                                   */
/* ========================================================================== */

/**
 * @brief XChar字符类型
 *
 * 存储UTF-16编码的字符单元（16位），可能是单个字符或代理对的一部分。
 * 轻量级值类型，可随处使用。大多数编译器将其视为unsigned short。
 */
typedef uint16_t XChar;

/* ========================================================================== */
/*                         构造与创建函数                                        */
/* ========================================================================== */

/**
 * @brief 创建空XChar（'\0'）
 * @return 空XChar（code=0）
 */
XChar XChar_null(void);

/**
 * @brief 从uint16_t创建XChar
 * @param code UTF-16字符编码
 * @return 对应的XChar
 */
XChar XChar_from(uint16_t code);

/**
 * @brief 从特殊字符常量创建XChar（对齐QChar::QChar(QChar::SpecialCharacter ch)）
 * @param ch 特殊字符枚举值（如 XChar_Tabulation, XChar_LineFeed 等）
 * @return 对应的XChar
 */
XChar XChar_fromSpecial(XChar_SpecialCharacter ch);

/**
 * @brief 从Latin-1字符创建XChar（对齐QChar::fromLatin1）
 * @param c Latin-1字符（char类型）
 * @return 对应的XChar
 */
XChar XChar_fromLatin1(char c);

/**
 * @brief 从char16_t创建XChar（对齐QChar::fromUcs2）
 * @param c UTF-16字符
 * @return 对应的XChar
 */
XChar XChar_fromUcs2(uint16_t c);

/**
 * @brief 从Unicode码点创建XChar（对齐QChar构造函数 QChar(int)）
 * @param code Unicode码点（0~0x10FFFF）
 * @return 对应的XChar（基础平面直接存储，补充平面返回高代理），无效码点返回空XChar
 */
XChar XChar_fromUnicode(uint32_t code);

/**
 * @brief 创建补充平面字符的高代理（对齐QChar::highSurrogate）
 * @param ucs4 Unicode码点（应 >= 0x10000）
 * @return 高代理XChar
 */
XChar XChar_highSurrogate(uint32_t ucs4);

/**
 * @brief 创建补充平面字符的低代理（对齐QChar::lowSurrogate）
 * @param ucs4 Unicode码点（应 >= 0x10000）
 * @return 低代理XChar
 */
XChar XChar_lowSurrogate(uint32_t ucs4);

/* ========================================================================== */
/*                         Unicode码点获取函数                                   */
/* ========================================================================== */

/**
 * @brief 获取XChar的Unicode码点（对齐QChar::unicode）
 * @param ch XChar值
 * @return 对应的Unicode码点（uint32_t）
 */
uint32_t XChar_unicode(XChar ch);

/**
 * @brief 获取XChar的Latin-1等价值（对齐QChar::toLatin1）
 * @param ch XChar值
 * @return Latin-1字符，非Latin-1字符返回0
 */
char XChar_toLatin1(XChar ch);

/**
 * @brief 获取Unicode行号（对齐QChar::row）
 * @param ch XChar值
 * @return Unicode行号（高字节）
 */
uint8_t XChar_row(XChar ch);

/**
 * @brief 获取Unicode单元号（对齐QChar::cell）
 * @param ch XChar值
 * @return Unicode单元号（低字节）
 */
uint8_t XChar_cell(XChar ch);

/* ========================================================================== */
/*                      字符分类函数（实例版本）                                  */
/* ========================================================================== */

/**
 * @brief 获取字符的Unicode分类（对齐QChar::category）
 * @param ch XChar值
 * @return XChar_Category枚举值
 */
XChar_Category XChar_category(XChar ch);

/**
 * @brief 获取字符的组合类（对齐QChar::combiningClass）
 * @param ch XChar值
 * @return Unicode标准定义的组合类
 */
uint8_t XChar_combiningClass(XChar ch);

/**
 * @brief 获取字符的分解标签（对齐QChar::decompositionTag）
 * @param ch XChar值
 * @return XChar_Decomposition枚举值，无分解返回XChar_NoDecomposition
 */
XChar_Decomposition XChar_decompositionTag(XChar ch);

/**
 * @brief 获取字符的书写方向（对齐QChar::direction）
 * @param ch XChar值
 * @return XChar_Direction枚举值
 */
XChar_Direction XChar_direction(XChar ch);

/**
 * @brief 获取字符的连接类型（对齐QChar::joiningType）
 * @param ch XChar值
 * @return XChar_JoiningType枚举值
 */
XChar_JoiningType XChar_joiningType(XChar ch);

/**
 * @brief 获取字符的Unicode脚本属性（对齐QChar::script）
 * @param ch XChar值
 * @return XChar_Script枚举值
 */
XChar_Script XChar_script(XChar ch);

/**
 * @brief 获取引入此字符的Unicode版本（对齐QChar::unicodeVersion）
 * @param ch XChar值
 * @return XChar_UnicodeVersion枚举值
 */
XChar_UnicodeVersion XChar_unicodeVersion(XChar ch);

/**
 * @brief 获取当前支持的最新Unicode版本（对齐QChar::currentUnicodeVersion）
 * @return XChar_UnicodeVersion枚举值
 */
XChar_UnicodeVersion XChar_currentUnicodeVersion(void);

/**
 * @brief 获取字符的数字值（对齐QChar::digitValue）
 * @param ch XChar值
 * @return 数字值，非数字返回-1
 */
int XChar_digitValue(XChar ch);

/**
 * @brief 判断字符是否有镜像字符（对齐QChar::hasMirrored）
 * @param ch XChar值
 * @return 有镜像字符返回true
 */
bool XChar_hasMirrored(XChar ch);

/**
 * @brief 获取镜像字符（对齐QChar::mirroredChar）
 * @param ch XChar值
 * @return 镜像字符，无镜像返回原字符
 */
XChar XChar_mirroredChar(XChar ch);

/* ========================================================================== */
/*                    字符类型判断函数（对齐QChar isXxx系列）                      */
/* ========================================================================== */

/**
 * @brief 判断是否为空字符 '\0'（对齐QChar::isNull）
 * @param ch XChar值
 * @return 是空字符返回true
 */
bool XChar_isNull(XChar ch);

/**
 * @brief 判断是否为可打印字符（对齐QChar::isPrint）
 * @param ch XChar值
 * @return 是可打印字符返回true（不包括Other_*类别）
 */
bool XChar_isPrint(XChar ch);

/**
 * @brief 判断是否为空格/分隔字符（对齐QChar::isSpace）
 * @param ch XChar值
 * @return 是空格字符返回true（Separator_*类别或特定控制字符）
 */
bool XChar_isSpace(XChar ch);

/**
 * @brief 判断是否为标点符号（对齐QChar::isPunct）
 * @param ch XChar值
 * @return 是标点符号返回true（Punctuation_*类别）
 */
bool XChar_isPunct(XChar ch);

/**
 * @brief 判断是否为字母（对齐QChar::isLetter）
 * @param ch XChar值
 * @return 是字母返回true（Letter_*类别）
 */
bool XChar_isLetter(XChar ch);

/**
 * @brief 判断是否为数字（对齐QChar::isNumber）
 * @param ch XChar值
 * @return 是数字返回true（Number_*类别，不限于0-9）
 */
bool XChar_isNumber(XChar ch);

/**
 * @brief 判断是否为字母或数字（对齐QChar::isLetterOrNumber）
 * @param ch XChar值
 * @return 是字母或数字返回true
 */
bool XChar_isLetterOrNumber(XChar ch);

/**
 * @brief 判断是否为十进制数字（对齐QChar::isDigit）
 * @param ch XChar值
 * @return 是十进制数字返回true（Number_DecimalDigit类别）
 */
bool XChar_isDigit(XChar ch);

/**
 * @brief 判断是否为标记字符（对齐QChar::isMark）
 * @param ch XChar值
 * @return 是标记字符返回true（Mark_*类别）
 */
bool XChar_isMark(XChar ch);

/**
 * @brief 判断是否为符号（对齐QChar::isSymbol）
 * @param ch XChar值
 * @return 是符号返回true（Symbol_*类别）
 */
bool XChar_isSymbol(XChar ch);

/**
 * @brief 判断是否为大写字母（对齐QChar::isUpper）
 * @param ch XChar值
 * @return 是大写字母返回true（Letter_Uppercase类别）
 */
bool XChar_isUpper(XChar ch);

/**
 * @brief 判断是否为小写字母（对齐QChar::isLower）
 * @param ch XChar值
 * @return 是小写字母返回true（Letter_Lowercase类别）
 */
bool XChar_isLower(XChar ch);

/**
 * @brief 判断是否为标题字母（对齐QChar::isTitleCase）
 * @param ch XChar值
 * @return 是标题字母返回true（Letter_Titlecase类别）
 */
bool XChar_isTitleCase(XChar ch);

/**
 * @brief 判断是否为非字符（对齐QChar::isNonCharacter）
 * @param ch XChar值
 * @return 是非字符返回true
 *
 * Unicode中某些码点被分类为"非字符"，可用于内部用途但不能用于文本交换。
 * 包括每个Unicode平面的最后两个条目（[0xfffe..0xffff]等）以及[0xfdd0..0xfdef]范围。
 */
bool XChar_isNonCharacter(XChar ch);

/**
 * @brief 判断是否为控制字符
 * @param ch XChar值
 * @return 是控制字符返回true（C0和C1控制字符）
 */
bool XChar_isControl(XChar ch);

/* ========================================================================== */
/*                        代理对相关函数                                         */
/* ========================================================================== */

/**
 * @brief 判断是否为高代理（对齐QChar::isHighSurrogate）
 * @param ch XChar值
 * @return 是高代理返回true（码点范围[0xd800..0xdbff]）
 */
bool XChar_isHighSurrogate(XChar ch);

/**
 * @brief 判断是否为低代理（对齐QChar::isLowSurrogate）
 * @param ch XChar值
 * @return 是低代理返回true（码点范围[0xdc00..0xdfff]）
 */
bool XChar_isLowSurrogate(XChar ch);

/**
 * @brief 判断是否为代理（对齐QChar::isSurrogate）
 * @param ch XChar值
 * @return 是代理返回true（码点范围[0xd800..0xdfff]）
 */
bool XChar_isSurrogate(XChar ch);

/**
 * @brief 判断码点是否需要代理对（对齐QChar::requiresSurrogates）
 * @param ucs4 Unicode码点
 * @return 需要代理对返回true（>= 0x10000）
 */
bool XChar_requiresSurrogates(uint32_t ucs4);

/**
 * @brief 代理对转UCS-4码点（对齐QChar::surrogateToUcs4(char16_t, char16_t)）
 * @param high 高代理
 * @param low 低代理
 * @return 对应的UCS-4码点
 */
uint32_t XChar_surrogateToUcs4(XChar high, XChar low);

/* ========================================================================== */
/*                   字符类型判断函数（UCS-4静态版本）                              */
/* ========================================================================== */

/**
 * @brief 获取UCS-4码点的Unicode分类（对齐QChar::category(char32_t)）
 * @param ucs4 Unicode码点
 * @return XChar_Category枚举值
 */
XChar_Category XChar_category_2(uint32_t ucs4);

/**
 * @brief 获取UCS-4码点的组合类（对齐QChar::combiningClass(char32_t)）
 * @param ucs4 Unicode码点
 * @return 组合类值
 */
uint8_t XChar_combiningClass_2(uint32_t ucs4);

/**
 * @brief 获取UCS-4码点的分解标签（对齐QChar::decompositionTag(char32_t)）
 * @param ucs4 Unicode码点
 * @return XChar_Decomposition枚举值
 */
XChar_Decomposition XChar_decompositionTag_2(uint32_t ucs4);

/**
 * @brief 获取UCS-4码点的书写方向（对齐QChar::direction(char32_t)）
 * @param ucs4 Unicode码点
 * @return XChar_Direction枚举值
 */
XChar_Direction XChar_direction_2(uint32_t ucs4);

/**
 * @brief 获取UCS-4码点的连接类型（对齐QChar::joiningType(char32_t)）
 * @param ucs4 Unicode码点
 * @return XChar_JoiningType枚举值
 */
XChar_JoiningType XChar_joiningType_2(uint32_t ucs4);

/**
 * @brief 获取UCS-4码点的脚本属性（对齐QChar::script(char32_t)）
 * @param ucs4 Unicode码点
 * @return XChar_Script枚举值
 */
XChar_Script XChar_script_2(uint32_t ucs4);

/**
 * @brief 获取UCS-4码点的Unicode版本（对齐QChar::unicodeVersion(char32_t)）
 * @param ucs4 Unicode码点
 * @return XChar_UnicodeVersion枚举值
 */
XChar_UnicodeVersion XChar_unicodeVersion_2(uint32_t ucs4);

/**
 * @brief 获取UCS-4码点的数字值（对齐QChar::digitValue(char32_t)）
 * @param ucs4 Unicode码点
 * @return 数字值，非数字返回-1
 */
int XChar_digitValue_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否有镜像字符（对齐QChar::hasMirrored(char32_t)）
 * @param ucs4 Unicode码点
 * @return 有镜像字符返回true
 */
bool XChar_hasMirrored_2(uint32_t ucs4);

/**
 * @brief 获取UCS-4码点的镜像字符（对齐QChar::mirroredChar(char32_t)）
 * @param ucs4 Unicode码点
 * @return 镜像字符，无镜像返回原码点
 */
uint32_t XChar_mirroredChar_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为空字符（对齐QChar::isNull(char32_t)）
 */
bool XChar_isNull_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为可打印字符（对齐QChar::isPrint(char32_t)）
 */
bool XChar_isPrint_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为空格（对齐QChar::isSpace(char32_t)）
 */
bool XChar_isSpace_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为标点（对齐QChar::isPunct(char32_t)）
 */
bool XChar_isPunct_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为字母（对齐QChar::isLetter(char32_t)）
 */
bool XChar_isLetter_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为数字（对齐QChar::isNumber(char32_t)）
 */
bool XChar_isNumber_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为字母或数字（对齐QChar::isLetterOrNumber(char32_t)）
 */
bool XChar_isLetterOrNumber_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为十进制数字（对齐QChar::isDigit(char32_t)）
 */
bool XChar_isDigit_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为标记（对齐QChar::isMark(char32_t)）
 */
bool XChar_isMark_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为符号（对齐QChar::isSymbol(char32_t)）
 */
bool XChar_isSymbol_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为大写（对齐QChar::isUpper(char32_t)）
 */
bool XChar_isUpper_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为小写（对齐QChar::isLower(char32_t)）
 */
bool XChar_isLower_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为标题字母（对齐QChar::isTitleCase(char32_t)）
 */
bool XChar_isTitleCase_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为非字符（对齐QChar::isNonCharacter(char32_t)）
 */
bool XChar_isNonCharacter_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为高代理（对齐QChar::isHighSurrogate(char32_t)）
 */
bool XChar_isHighSurrogate_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为低代理（对齐QChar::isLowSurrogate(char32_t)）
 */
bool XChar_isLowSurrogate_2(uint32_t ucs4);

/**
 * @brief 判断UCS-4码点是否为代理（对齐QChar::isSurrogate(char32_t)）
 */
bool XChar_isSurrogate_2(uint32_t ucs4);

/* ========================================================================== */
/*                         大小写转换函数                                        */
/* ========================================================================== */

/**
 * @brief 转换为大写（对齐QChar::toUpper）
 * @param ch XChar值
 * @return 大写形式，无对应大写返回原字符
 */
XChar XChar_toUpper(XChar ch);

/**
 * @brief 转换为小写（对齐QChar::toLower）
 * @param ch XChar值
 * @return 小写形式，无对应小写返回原字符
 */
XChar XChar_toLower(XChar ch);

/**
 * @brief 转换为大小写折叠形式（对齐QChar::toCaseFolded）
 * @param ch XChar值
 * @return 大小写折叠形式，对大多数Unicode字符等同于toLower()
 */
XChar XChar_toCaseFolded(XChar ch);

/**
 * @brief 转换为标题形式（对齐QChar::toTitleCase）
 * @param ch XChar值
 * @return 标题形式，无对应标题形式返回原字符
 */
XChar XChar_toTitleCase(XChar ch);

/**
 * @brief UCS-4码点转大写（对齐QChar::toUpper(char32_t)）
 * @param ucs4 Unicode码点
 * @return 大写形式的码点
 */
uint32_t XChar_toUpper_2(uint32_t ucs4);

/**
 * @brief UCS-4码点转小写（对齐QChar::toLower(char32_t)）
 * @param ucs4 Unicode码点
 * @return 小写形式的码点
 */
uint32_t XChar_toLower_2(uint32_t ucs4);

/**
 * @brief UCS-4码点大小写折叠（对齐QChar::toCaseFolded(char32_t)）
 * @param ucs4 Unicode码点
 * @return 大小写折叠形式的码点
 */
uint32_t XChar_toCaseFolded_2(uint32_t ucs4);

/**
 * @brief UCS-4码点转标题形式（对齐QChar::toTitleCase(char32_t)）
 * @param ucs4 Unicode码点
 * @return 标题形式的码点
 */
uint32_t XChar_toTitleCase_2(uint32_t ucs4);

/* ========================================================================== */
/*                         字符比较函数                                          */
/* ========================================================================== */

/**
 * @brief 比较两个XChar是否相等（支持大小写敏感性）
 * @param a 第一个XChar
 * @param b 第二个XChar
 * @param cs 大小写敏感性
 * @return 相等返回true
 */
bool XChar_equals(XChar a, XChar b, XChar_CaseSensitivity cs);

/**
 * @brief 比较两个XChar的大小（对齐QChar operator< 等）
 * @param a 第一个XChar
 * @param b 第二个XChar
 * @return a < b返回-1，a > b返回1，相等返回0
 */
int32_t XChar_compare(XChar a, XChar b);

/* ========================================================================== */
/*                     扩展功能（非QChar标准，实用补充）                            */
/* ========================================================================== */

/**
 * @brief 将XChar转换为UTF-8字节数（非QChar标准）
 * @param ch XChar值
 * @return UTF-8编码所需的字节数
 */
uint8_t XChar_toUtf8Size(XChar ch);

/**
 * @brief 判断是否为表情符号（非QChar标准）
 * @param ch XChar值
 * @return 是表情符号返回true
 */
bool XChar_isEmoji(XChar ch);

/**
 * @brief 判断是否为全角字符（非QChar标准）
 * @param ch XChar值
 * @return 是全角字符返回true
 */
bool XChar_isFullwidth(XChar ch);

/**
 * @brief 判断是否为半角字符（非QChar标准）
 * @param ch XChar值
 * @return 是半角字符返回true
 */
bool XChar_isHalfwidth(XChar ch);

/**
 * @brief 半角转全角（非QChar标准）
 * @param ch XChar值（半角字符）
 * @return 全角XChar，无法转换返回原字符
 */
XChar XChar_toFullwidth(XChar ch);

/**
 * @brief 全角转半角（非QChar标准）
 * @param ch XChar值（全角字符）
 * @return 半角XChar，无法转换返回原字符
 */
XChar XChar_toHalfwidth(XChar ch);

/* ========================================================================== */
/*                        编码转换函数（流式API）                                 */
/* ========================================================================== */

/**
 * @brief 从UTF-8字节流解析出XChar数组
 * @param utf8 输入的UTF-8字节流
 * @param input_size 输入数据大小（字节），0则自动检测NULL结尾
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功解析的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_fromUtf8Stream(const uint8_t* utf8, size_t input_size, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为UTF-8字节流
 * @param ch XChar数组（以code=0为终止符）
 * @param input_count 输入XChar数量，0则自动检测终止符
 * @param utf8 输出的UTF-8字节流
 * @param max_utf8 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_toUtf8Stream(const XChar* ch, size_t input_count, uint8_t* utf8, size_t max_utf8);

/**
 * @brief 从UTF-16编码字符串转换为XChar数组
 * @param utf16_str UTF-16字符串（uint16_t类型，以0为终止符）
 * @param input_size 输入数据大小（uint16_t元素数），0则自动检测
 * @param out 输出的XChar数组
 * @param max_count 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_fromUtf16Stream(const uint16_t* utf16_str, size_t input_size, XChar* out, size_t max_count);

/**
 * @brief 将XChar数组转换为UTF-16编码字符串
 * @param xchars XChar数组（以code=0为终止符）
 * @param input_count 输入XChar数量，0则自动检测
 * @param out_buf 输出的UTF-16字符串缓冲区
 * @param buf_size 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的uint16_t数量（不含终止符），失败返回-1
 */
int64_t XChar_toUtf16Stream(const XChar* xchars, size_t input_count, uint16_t* out_buf, size_t buf_size);

/**
 * @brief 从UTF-32码点数组创建XChar数组
 * @param utf32 输入的UTF-32码点数组（以0为终止符）
 * @param input_count 输入码点数量，0则自动检测
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_fromUtf32Stream(const uint32_t* utf32, size_t input_count, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为UTF-32码点数组
 * @param ch XChar数组（以code=0为终止符）
 * @param input_count 输入XChar数量，0则自动检测
 * @param utf32 输出的UTF-32码点数组
 * @param max_utf32 输出数组的最大容量（含终止符）
 * @return 成功转换的码点数量（不含终止符），失败返回-1
 */
int64_t XChar_toUtf32Stream(const XChar* ch, size_t input_count, uint32_t* utf32, size_t max_utf32);

/**
 * @brief 从Latin1编码字符串转换为XChar数组
 * @param latin1 Latin1字符串（uint8_t类型，以'\0'为终止符）
 * @param input_size 输入数据大小（字节），0则自动检测
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_fromLatin1Stream(const uint8_t* latin1, size_t input_size, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为Latin1编码字符串
 * @param ch XChar数组（以code=0为终止符）
 * @param input_count 输入XChar数量，0则自动检测
 * @param latin1 输出的Latin1字符串缓冲区
 * @param max_latin1 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的字节数（不含终止符），超出Latin1范围返回-1
 */
int64_t XChar_toLatin1Stream(const XChar* ch, size_t input_count, uint8_t* latin1, size_t max_latin1);

/**
 * @brief 从GBK编码字符串转换为XChar数组
 * @param gbk GBK编码字符串
 * @param input_size 输入数据大小（字节），0则自动检测NULL结尾
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_fromGbkStream(const char* gbk, size_t input_size, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为GBK编码字符串
 * @param ch XChar数组（以code=0为终止符）
 * @param input_count 输入XChar数量，0则自动检测
 * @param gbk 输出的GBK编码字符串缓冲区
 * @param max_gbk 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_toGbkStream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk);

/**
 * @brief 从本地编码字符串转换为XChar数组
 * @param local_str 本地编码字符串（Windows为GBK，Linux为UTF-8）
 * @param input_size 输入数据大小（字节），0则自动检测
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_fromLocalStream(const char* local_str, size_t input_size, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为本地编码字符串
 * @param ch XChar数组（以code=0为终止符）
 * @param input_count 输入XChar数量，0则自动检测
 * @param local_str 输出的本地编码字符串缓冲区
 * @param max_local 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_toLocalStream(const XChar* ch, size_t input_count, char* local_str, size_t max_local);

/**
 * @brief UTF-8转GBK编码（跨平台实现）
 * @param utf8_str 输入UTF-8字符串
 * @param input_size 输入数据大小（字节），0则自动检测
 * @param gbk_buf 输出GBK缓冲区（NULL时仅计算所需大小）
 * @param max_len 输出缓冲区大小（含终止符）
 * @return 成功返回GBK字节数（不含终止符），失败返回-1
 */
int64_t XChar_utf8ToGbkStream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len);

/**
 * @brief GBK转UTF-8编码（跨平台实现）
 * @param gbk_str 输入GBK字符串
 * @param input_size 输入数据大小（字节），0则自动检测
 * @param utf8_buf 输出UTF-8缓冲区（NULL时仅计算所需大小）
 * @param max_len 输出缓冲区大小（含终止符）
 * @return 成功返回UTF-8字节数（不含终止符），失败返回-1
 */
int64_t XChar_gbkToUtf8Stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len);

/**
 * @brief 获取XChar数组的实际长度
 * @param xchars XChar数组（以code=0为终止符）
 * @param input_count 缓冲区大小（XChar元素数量），0表示不限制范围
 * @return 有效字符数（不含终止符），xchars为NULL返回0
 */
size_t XChar_getInputLength(const XChar* xchars, size_t input_count);

/* ========================================================================== */
/*                        数值与字符串互转函数                                    */
/* ========================================================================== */

/**
 * @brief XChar数组(UTF-16)转short整数
 * @param xchars 输入XChar数组（以code=0为终止符）
 * @param input_count 输入数量，0则自动检测
 * @param base 进制（2-36）
 * @param success 输出参数，成功为true
 * @return 转换后的值，失败返回0
 */
short XChar_toShort(const XChar* xchars, size_t input_count, int base, bool* success);

/**
 * @brief XChar数组(UTF-16)转int整数
 */
int XChar_toInt(const XChar* xchars, size_t input_count, int base, bool* success);

/**
 * @brief XChar数组(UTF-16)转long整数
 */
long XChar_toLong(const XChar* xchars, size_t input_count, int base, bool* success);

/**
 * @brief XChar数组(UTF-16)转long long整数
 */
long long XChar_toLongLong(const XChar* xchars, size_t input_count, int base, bool* success);

/**
 * @brief XChar数组(UTF-16)转unsigned short整数
 */
unsigned short XChar_toUShort(const XChar* xchars, size_t input_count, int base, bool* success);

/**
 * @brief XChar数组(UTF-16)转unsigned int整数
 */
unsigned int XChar_toUInt(const XChar* xchars, size_t input_count, int base, bool* success);

/**
 * @brief XChar数组(UTF-16)转unsigned long整数
 */
unsigned long XChar_toULong(const XChar* xchars, size_t input_count, int base, bool* success);

/**
 * @brief XChar数组(UTF-16)转unsigned long long整数
 */
unsigned long long XChar_toULongLong(const XChar* xchars, size_t input_count, int base, bool* success);

/**
 * @brief XChar数组(UTF-16)转float
 */
float XChar_toFloat(const XChar* xchars, size_t input_count, bool* success);

/**
 * @brief XChar数组(UTF-16)转double
 */
double XChar_toDouble(const XChar* xchars, size_t input_count, bool* success);

/**
 * @brief short转UTF-16数组
 * @param value 待转换的值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度）
 * @param max_out 输出缓冲区最大容量
 * @param uppercase 字母是否大写
 * @return out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_fromShort(short value, int base, XChar* out, size_t max_out, bool uppercase);

/**
 * @brief int转UTF-16数组
 */
int64_t XChar_fromInt(int value, int base, XChar* out, size_t max_out, bool uppercase);

/**
 * @brief long转UTF-16数组
 */
int64_t XChar_fromLong(long value, int base, XChar* out, size_t max_out, bool uppercase);

/**
 * @brief long long转UTF-16数组
 */
int64_t XChar_fromLongLong(long long value, int base, XChar* out, size_t max_out, bool uppercase);

/**
 * @brief unsigned short转UTF-16数组
 */
int64_t XChar_fromUShort(unsigned short value, int base, XChar* out, size_t max_out, bool uppercase);

/**
 * @brief unsigned int转UTF-16数组
 */
int64_t XChar_fromUInt(unsigned int value, int base, XChar* out, size_t max_out, bool uppercase);

/**
 * @brief unsigned long转UTF-16数组
 */
int64_t XChar_fromULong(unsigned long value, int base, XChar* out, size_t max_out, bool uppercase);

/**
 * @brief unsigned long long转UTF-16数组
 */
int64_t XChar_fromULongLong(unsigned long long value, int base, XChar* out, size_t max_out, bool uppercase);

/**
 * @brief float转UTF-16数组
 * @param value 待转换的值
 * @param format 格式：'f'/'F'（定点）、'e'/'E'（科学计数）、'g'/'G'（自动）
 * @param out 输出缓冲区（NULL时返回所需长度）
 * @param max_out 输出缓冲区最大容量
 * @param precision 精度（-1=自动）
 * @return out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_fromFloat(float value, char format, XChar* out, size_t max_out, int precision);

/**
 * @brief double转UTF-16数组
 */
int64_t XChar_fromDouble(double value, char format, XChar* out, size_t max_out, int precision);

#ifdef __cplusplus
}
#endif
#endif // XCHAR_H
