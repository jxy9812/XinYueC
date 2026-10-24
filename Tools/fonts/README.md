# 位图字库（.bin）生成器

生成 XGui 外挂位图字库，供 `Src/XData/XFont/XFont.c` 的 LVGL8 `.bin` 解析路径使用。
字库是**纯数据**，不含 FreeType、不依赖轮廓字体，可在无文件系统的环境（安卓 APK 资源、
ESP8266 flash）中使用。

## 用法

```bat
python Tools\fonts\gen_bitmap_bin.py ^
    --otf Library\XFont\XFontOutlineCommon.otf ^
    --out Library\XFont\XFont16x16.bin ^
    --size 16 --ascent 15 --descent -4 --bpp 2 ^
    --report out\gen16_report.txt
```

依赖：`Pillow`（栅格化）+ `fontTools`（读源字库的 cmap）。

生成后把字库放到 `XFONT_EXTERNAL_FONT_DIR`（默认 `../Library/XFont`），并让字体
家族名与之对应：

| 家族名 | 文件 | 说明 |
|---|---|---|
| `XFont16x16` | `XFont16x16.bin` | 16px 正文字号 |
| `XFont32x32` | `XFont32x32.bin` | 32px 标题/大字号 |

```c
XFont_setFamily(font, "XFont16x16");   /* 走 .bin 外挂位图路径 */
```

## 字符集

源字库（Noto Sans CJK SC）里存在的以下区间全部收录：

| 区间 | 内容 | 字数 |
|---|---|---|
| U+0020–U+007E | ASCII | 95 |
| U+00A0–U+00FF | Latin-1 | 20 |
| U+0100–U+017F | Latin Extended-A | 6 |
| U+2000–U+206F | General Punctuation | 12 |
| U+3000–U+303F | CJK 标点（，。、；：？！（）「」） | 20 |
| U+4E00–U+9FFF | CJK 统一表意文字（一级+二级） | 6763 |
| U+FF00–U+FFEF | 全角/半角形式 | 98 |

合计 7014 个码点。**注意**：旧字库只有 3495 个汉字且**完全没有中文标点**，
新字库补齐了这两块。

## 体积

| 字库 | 旧 | 新 |
|---|---|---|
| XFont16x16.bin | 255 KB | 490 KB |
| XFont32x32.bin | 839 KB | 1.53 MB |
| XFontOutlineCommon.otf（轮廓字） | 3.3 MB | 3.3 MB |

位图字库省掉 FreeType 与字体文件解析，但体积大于纯轮廓字的一个尺寸；两个尺寸
合计仍小于 OTF 的两倍，**省的是运行时依赖而不是 flash**——若 flash 紧张，优先
用 16x16 一个尺寸。

## 文件格式

四段结构，每段 `[u32 length][4B label][payload]`，`length` **含 8 字节节头**：

```
head : 48 字节（40 字节有效载荷）
cmap : [u32 count][count×16B 子表][稀疏偏移表]
loca : [u32 count][count×u32 记录偏移]
glyf : [字形记录...]
```

### head 关键字段（**文件绝对偏移**）

| 偏移 | 类型 | 含义 |
|---|---|---|
| 14 | u16 | font_size（em 宽，像素） |
| 16 | u16 | ascent（基线到行顶） |
| 18 | i16 | descent（负值） |
| 34 | u8 | loca 索引位宽：0=2 字节，1=4 字节 |
| 36 | u8 | advance 单位：0 → adv 再乘 16；1 → adv 已是 1/16 px |
| 37 | u8 | bpp（1/2/3/4/8） |
| 38/39 | u8 | ofs 位宽 / box 位宽 |
| 40 | u8 | advance 位宽 |
| 41 | u8 | compression（0=裸位图，1=RLE） |

> **踩坑点**：偏移 34 是**文件绝对偏移**（落在 head 段里），不是 `locaStart+34`。
> 按后者读会把 4 字节偏移数组当成 2 字节读，所有字形静默解码失败。

### cmap 子表项（16 字节）

```
0  u32 data_offset（相对 cmap 段首）   4  u32 range_start
8  u16 range_length                    10 u16 glyph_id_start
12 u16 entries（稀疏表长度）            14 u8 format
```

format：`0`=Format0Full `1`=SparseFull `2`=Format0Tiny(连续) `3`=SparseTiny(稀疏)。

### 字形记录（位流，MSB first，逐字节对齐）

```
advance_bits(u) | xy_bits(i) ofs_x | xy_bits(i) ofs_y |
wh_bits(u) box_w | wh_bits(u) box_h | box_h 行 × box_w 像素 × bpp
```

### 定位约定（`Src/XGui/Graphics/XPainter.c` 实际用法）

```
originX = pen_x + ofs_x                    （像素）
originY = baselineY - box_h - ofs_y         （像素，符号与 LVGL 相反）
baselineY = line_top + ascent
```

生成器据此反推：`ofs_x = 墨迹左边缘 - pen`，`ofs_y = ascent - 墨迹下边缘`。

## 栅格化

4 倍超采样（`--supersample`）后 box 降采样得到抗锯齿灰度，再量化到 `bpp` 位
（2bpp → 4 级灰度）。基线固定在 `ascent` 处，`--ascent/--descent` 必须与
`XFont16x16.h` / `XFont32x32.h` 里的常量保持一致，否则行高和回归断言会变。
