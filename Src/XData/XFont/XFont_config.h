/**
 * @file       XFont_config.h
 * @brief      XFont 字体模块的编译期配置。
 * @details    配置项可以通过编译器 -D 覆盖，便于嵌入式工程裁剪字库数据、
 *             文件系统支持和默认字体参数。默认配置保持桌面端现有行为。
 */
#ifndef XFONT_CONFIG_H
#define XFONT_CONFIG_H

/** @brief 采用 LVGL fmt_txt 的小型字形描述布局；1 时使用 large 布局。 */
#ifndef XFONT_LVGL_FMT_TXT_LARGE
#define XFONT_LVGL_FMT_TXT_LARGE 0
#endif

/** @brief 是否把内置 8x16（含常用中文）点阵字库编译进固件。 */
#ifndef XFONT_BUILTIN_8X16_ON
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define XFONT_BUILTIN_8X16_ON 1
#else
#define XFONT_BUILTIN_8X16_ON 0
#endif
#endif

/** @brief 是否把内置 LVGL fmt_txt 16px 2bpp 字库编译进固件。 */
#ifndef XFONT_BUILTIN_16X16_ON
#define XFONT_BUILTIN_16X16_ON 0
#endif

/** @brief 是否把内置 LVGL fmt_txt 32px 2bpp 字库编译进固件。 */
#ifndef XFONT_BUILTIN_32X32_ON
#define XFONT_BUILTIN_32X32_ON 0
#endif

/** @brief XFont 位图 provider 注册表容量；按嵌入式固件实际字库数量调整。 */
#ifndef XFONT_MAX_BITMAP_PROVIDERS
#define XFONT_MAX_BITMAP_PROVIDERS 8
#endif

/** @brief 是否启用轮廓字体 provider/API；关闭后裁剪轮廓解析和绘制支持。 */
#ifndef XFONT_OUTLINE_ON
#define XFONT_OUTLINE_ON 1
#endif

/** @brief 轮廓 provider 注册表容量；嵌入式可按实际字体数量缩小。 */
#ifndef XFONT_MAX_OUTLINE_PROVIDERS
#define XFONT_MAX_OUTLINE_PROVIDERS 4
#endif

/** @brief 单个 XFO1 字形允许的最大命令数；用于限制损坏文件的内存/CPU开销。 */
#ifndef XFONT_OUTLINE_MAX_COMMANDS
#define XFONT_OUTLINE_MAX_COMMANDS 2048
#endif

/** @brief 是否编译 XFO1 二次贝塞尔命令支持。 */
#ifndef XFONT_OUTLINE_QUADRATIC_ON
#define XFONT_OUTLINE_QUADRATIC_ON 1
#endif

/** @brief 是否编译 XFO1 三次贝塞尔命令支持。 */
#ifndef XFONT_OUTLINE_CUBIC_ON
#define XFONT_OUTLINE_CUBIC_ON 1
#endif

/** @brief 是否启用轮廓字形路径缓存；缓存按字库、码点和字号复用。 */
#ifndef XFONT_OUTLINE_CACHE_ON
#define XFONT_OUTLINE_CACHE_ON 1
#endif

/** @brief 轮廓路径缓存项数量；设为 0 或关闭开关可裁剪缓存。 */
#ifndef XFONT_OUTLINE_CACHE_ENTRIES
#define XFONT_OUTLINE_CACHE_ENTRIES 24
#endif

/**
 * @brief 是否把内置 XFontOutlineCommon 轮廓字库编译进目标。
 * @details 该开关同时控制 ASCII/标点和 GB2312 一级常用汉字两个数据块；
 *          字库数据约 1.6 MiB，默认关闭以保持基础库体积，桌面端可通过
 *          -DXFONT_BUILTIN_OUTLINE_ON=1 开启，嵌入式按需裁剪。
 */
#ifndef XFONT_BUILTIN_OUTLINE_ON
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define XFONT_BUILTIN_OUTLINE_ON 1
#else
#define XFONT_BUILTIN_OUTLINE_ON 0
#endif
#endif

#if !XFONT_OUTLINE_ON
#undef XFONT_BUILTIN_OUTLINE_ON
#define XFONT_BUILTIN_OUTLINE_ON 0
#undef XFONT_OUTLINE_FILE_ON
#define XFONT_OUTLINE_FILE_ON 0
#undef XFONT_OUTLINE_QUADRATIC_ON
#define XFONT_OUTLINE_QUADRATIC_ON 0
#undef XFONT_OUTLINE_CUBIC_ON
#define XFONT_OUTLINE_CUBIC_ON 0
#undef XFONT_OUTLINE_CACHE_ON
#define XFONT_OUTLINE_CACHE_ON 0
#endif

/**
 * @brief 是否启用外部点阵字库文件读取。
 * @details 该功能依赖 XFile；当 XFILE_ON 为 0 时自动关闭。
 */
#ifndef XFONT_FILE_ON
#if defined(XFILE_ON)
#define XFONT_FILE_ON XFILE_ON
#else
#define XFONT_FILE_ON 1
#endif
#endif

/**
 * @brief 是否兼容 LVGL 8 `lv_font_conv --format bin` 外挂字库。
 * @details 文件格式为 head/cmap/loca/glyf 四个分段，是 LVGL 8 的
 *          lv_font_loader.c 所定义的运行时二进制格式。该开关受
 *          XFONT_FILE_ON 总开关约束。
 */
#ifndef XFONT_LVGL8_FILE_ON
#define XFONT_LVGL8_FILE_ON XFONT_FILE_ON
#endif
#if !XFONT_FILE_ON
#undef XFONT_LVGL8_FILE_ON
#define XFONT_LVGL8_FILE_ON 0
#endif

/**
 * @brief 是否启用 LVGL 9 外挂字库后端。
 * @details LVGL 9 主线没有继续提供 LVGL 8 的 .bin 加载协议。其字体
 *          对象和字形描述已经重构，因此此开关默认关闭，待接入明确的
 *          V9 文件规范和专用解析器后再启用；不会回退误读为 V8 文件。
 */
#ifndef XFONT_LVGL9_FILE_ON
#define XFONT_LVGL9_FILE_ON 0
#endif

/**
 * @brief 是否启用 XFO1 外挂轮廓字库文件。
 * @details 该功能依赖 XFile，并受 XFONT_OUTLINE_ON 总开关约束。
 */
#ifndef XFONT_OUTLINE_FILE_ON
#if XFONT_OUTLINE_ON && XFONT_FILE_ON
#define XFONT_OUTLINE_FILE_ON 1
#else
#define XFONT_OUTLINE_FILE_ON 0
#endif
#endif
#if !XFONT_OUTLINE_ON || !XFONT_FILE_ON
#undef XFONT_OUTLINE_FILE_ON
#define XFONT_OUTLINE_FILE_ON 0
#endif

/**
 * @brief 外挂 LVGL 二进制字库目录。
 * @details XFont_setFamily() 对未注册的普通名称接收不含扩展名的文件名，
 *          并在该目录下查找 "<family>.bin"；包含盘符或目录分隔符的输入
 *          按完整路径读取，并兼容带或不带 ".bin" 后缀。嵌入式工程应通过
 *          编译选项覆盖为实际文件系统挂载目录，例如 "0:/font"。
 */
#ifndef XFONT_EXTERNAL_FONT_DIR
#define XFONT_EXTERNAL_FONT_DIR "../Library/XFont"
#endif

/** @brief XFO1 外挂轮廓字库目录；默认与点阵字库目录相同。 */
#ifndef XFONT_EXTERNAL_OUTLINE_FONT_DIR
#define XFONT_EXTERNAL_OUTLINE_FONT_DIR XFONT_EXTERNAL_FONT_DIR
#endif

/** @brief 外挂字库完整路径的最大长度（含结尾的 NUL）。 */
#ifndef XFONT_EXTERNAL_FONT_PATH_MAX
#define XFONT_EXTERNAL_FONT_PATH_MAX 512
#endif

/**
 * @brief 新建 XFont 未指定家族时使用的默认字体家族名称。
 * @details 可以配置为已注册的 provider 名称，例如 "XFont8x16"；外挂字库
 *          使用不含 ".bin" 后缀的文件名，并由 XFONT_EXTERNAL_FONT_DIR
 *          指定搜索目录。
 */
#ifndef XFONT_DEFAULT_FAMILY
#define XFONT_DEFAULT_FAMILY "XFont8x16"
#endif

/* 点阵 provider 的最大单元尺寸。位图按行存储，每行字节数由宽度向上
 * 取整得到；高度上限同时决定渲染器的栈上临时缓冲区大小。 */
#ifndef XFONT_BITMAP_MAX_WIDTH
#define XFONT_BITMAP_MAX_WIDTH 40
#endif
#ifndef XFONT_BITMAP_MAX_HEIGHT
#define XFONT_BITMAP_MAX_HEIGHT 40
#endif
#ifndef XFONT_BITMAP_MAX_ROW_BYTES
/* LVGL fmt_txt supports 1/2/4/8bpp; reserve the widest supported row. */
#define XFONT_BITMAP_MAX_ROW_BYTES ((XFONT_BITMAP_MAX_WIDTH * 8 + 7) / 8)
#endif

/** @brief 新建 XFont 的默认点大小；单位为 point。 */
#ifndef XFONT_DEFAULT_POINT_SIZE
#define XFONT_DEFAULT_POINT_SIZE 12.0
#endif

/** @brief 新建 XFont 的默认像素大小；小于等于 0 表示按点阵基准字号计算。 */
#ifndef XFONT_DEFAULT_PIXEL_SIZE
#define XFONT_DEFAULT_PIXEL_SIZE -1
#endif

/** @brief 新建 XFont 的默认字重，取值遵循 XFont_Weight 的数值约定。 */
#ifndef XFONT_DEFAULT_WEIGHT
#define XFONT_DEFAULT_WEIGHT 400
#endif

/** @brief 新建 XFont 是否默认为斜体；0 为否，1 为是。 */
#ifndef XFONT_DEFAULT_ITALIC
#define XFONT_DEFAULT_ITALIC 0
#endif

#endif /* XFONT_CONFIG_H */
