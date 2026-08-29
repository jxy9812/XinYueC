/******************************************************************************
 * @file       XIconThemeInternal.c
 * @brief      XIcon 主题图标资源解析实现（对标 Qt 6.8 QIconLoader 简化版）。
 * @note       本文件提供内部主题文件发现、index.theme 目录元数据解析与
 *             Inherits 父主题回退；无 index.theme 时仍按常用 XDG 目录名匹配，
 *             再按“当前主题、父主题、后备主题”的顺序回退，供主题引擎和
 *             hasThemeIcon 等入口共用。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XIconThemeInternal.h"
#include "XMemory.h"
#include "XString.h"
#include "XStringList.h"
#include "XContainer.h"
#include "XIcon.h"
#include "XImageCodec_config.h"
#include "XGeometry.h"
#include "XClass.h"
#include "XFile.h"
#include "XFileInfo.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XDir.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if XIMAGECODEC_ON && XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON
#define XICON_THEME_SVG_AVAILABLE 1
#else
#define XICON_THEME_SVG_AVAILABLE 0
#endif

static const char* theme_size_dirs[] = {
    "scalable", "512x512", "256x256", "128x128", "96x96", "64x64",
    "48x48", "32x32", "24x24", "22x22", "16x16"
};

static const char* theme_context_dirs[] = {
    "actions", "apps", "devices", "emblems", "mimetypes",
    "places", "status", "ui"
};

static const char* theme_import_exts[] = {
    ".png", ".svg", ".xpm", ".bmp", ""
};

/* Qt QIconLoader::lookupFallbackIcon() 只把这三种后缀作为主题外的
 * 独立回退文件；主题目录内部仍由 theme_import_exts 按编解码器裁剪。 */
static const char* theme_fallback_exts[] = {
    ".png", ".xpm", ".svg"
};

/* XDG 目录名中的尺寸为十进制，例如 48x48。 */
static int theme_parseSize(const char* s)
{
    int value = 0;
    if (!s) return 0;
    s = strchr(s, 'x');
    if (!s || !s[1]) return 0;
    ++s;
    while (*s >= '0' && *s <= '9') {
        int d = *s - '0';
        if (value > 65535) return 0;
        value = value * 10 + d;
        ++s;
    }
    return value > 0 ? value : 0;
}

static void theme_buildDirPath(char* out, size_t outSize, const char* root,
                               const char* theme, const char* sizeDir,
                               const char* context, const char* name,
                               const char* ext)
{
    snprintf(out, outSize, "%s/%s/%s/%s/%s%s", root ? root : "",
             theme ? theme : "", sizeDir ? sizeDir : "",
             context ? context : "", name ? name : "", ext ? ext : "");
}

static void theme_buildThemePath(char* out, size_t outSize, const char* root,
                                 const char* theme, const char* name,
                                 const char* ext)
{
    snprintf(out, outSize, "%s/%s/%s%s", root ? root : "",
             theme ? theme : "", name ? name : "", ext ? ext : "");
}

static void theme_buildRootPath(char* out, size_t outSize, const char* root,
                                const char* name, const char* ext)
{
    snprintf(out, outSize, "%s/%s%s", root ? root : "", name ? name : "",
             ext ? ext : "");
}

static bool theme_loadFile(const char* path, XPixmap* out)
{
    XPixmap candidate;
    bool ok;
    if (!path || !path[0]) return false;
    XPixmap_init(&candidate);
    ok = XPixmap_load_2(&candidate, path, NULL, 0) &&
         !XPixmap_isNull(&candidate);
    if (ok && out) {
        XPixmap_move_base(out, &candidate);
        XPixmap_deinit_base(&candidate);
    } else {
        XPixmap_deinit_base(&candidate);
    }
    return ok;
}

/* Qt lookupFallbackIcon() 先检查条目是否存在，再决定是否尝试解码；
 * 因而“文件存在但内容损坏”也必须阻止后续扩展名和搜索目录抢占。 */
static bool theme_fileExists(const char* path)
{
    XString* fileName;
    bool exists;
    if (!path || !path[0]) return false;
    fileName = XString_create_utf8(path);
    if (!fileName) return false;
    exists = XFileInfo_exists_static(fileName);
    XString_delete_base((XClass*)fileName);
    return exists;
}

static bool theme_extAllowed(const char* ext)
{
#if XICON_THEME_SVG_AVAILABLE
    return true;
#else
    return !(ext[0] == '.' && ext[1] == 's' && ext[2] == 'v' && ext[3] == 'g');
#endif
}

/*
 * Qt QIconLoader::findIconHelper() 将同一尺寸的 PNG 条目排在 SVG、XPM
 * 等可替代格式之前。主题文件的扩展名探测顺序也必须保留这个优先级，
 * 否则当一个目录同时提供 PNG 和 SVG 时，逐目录立即返回会让 SVG 抢先。
 * 数值越小表示优先级越高；未知扩展名放到最后，空扩展名仍可作为项目
 * 的裁剪格式，但不应覆盖标准图像格式。
 */
static int theme_extPriority(const char* ext)
{
    if (!ext) return INT_MAX;
    if (strcmp(ext, ".png") == 0) return 0;
    if (strcmp(ext, ".svg") == 0) return 1;
    if (strcmp(ext, ".xpm") == 0) return 2;
    if (strcmp(ext, ".bmp") == 0) return 3;
    if (ext[0] == '\0') return 4;
    return 5;
}

/*
 * Qt 6.8 读取 gtk-update-icon-cache 生成的 icon-theme.cache 时，所有
 * 整数均按网络字节序读取。返回值约定：1 表示缓存确认目录中存在图标，
 * 0 表示缓存确认不存在，-1 表示缓存缺失、过期或损坏，调用方必须完整扫描。
 */
static bool theme_cacheRead16(const uint8_t* data, size_t length,
                              uint32_t offset, uint16_t* out)
{
    if (!data || !out || (uint64_t)offset > (uint64_t)length ||
        (offset & 1u) != 0u || length - (size_t)offset < 2u)
        return false;
    *out = (uint16_t)(((uint16_t)data[offset] << 8) |
                      (uint16_t)data[offset + 1u]);
    return true;
}

/*
 * 缓存中的偏移和条目下标均为 32 位值。必须在相加前检查回绕，不能
 * 依赖相加后的偏移再由 theme_cacheRead32() 检查，否则大偏移可能回到
 * 文件头而被误判为合法记录。
 */
static bool theme_cacheAddOffset(uint32_t base, uint64_t delta,
                                 uint32_t* out)
{
    if (!out || delta > (uint64_t)UINT32_MAX - (uint64_t)base)
        return false;
    *out = (uint32_t)((uint64_t)base + delta);
    return true;
}

/* 检查缓存文件中从 offset 开始的完整字节区间。 */
static bool theme_cacheRange(const uint8_t* data, size_t length,
                             uint32_t offset, uint64_t bytes)
{
    if (!data || (uint64_t)offset > (uint64_t)length)
        return false;
    return bytes <= (uint64_t)length - (uint64_t)offset;
}

static bool theme_cacheRead32(const uint8_t* data, size_t length,
                              uint32_t offset, uint32_t* out)
{
    if (!data || !out || (uint64_t)offset > (uint64_t)length ||
        (offset & 3u) != 0u || length - (size_t)offset < 4u)
        return false;
    *out = ((uint32_t)data[offset] << 24) |
           ((uint32_t)data[offset + 1u] << 16) |
           ((uint32_t)data[offset + 2u] << 8) |
           (uint32_t)data[offset + 3u];
    return true;
}

static const char* theme_cacheString(const uint8_t* data, size_t length,
                                     uint32_t offset)
{
    const uint8_t* end;
    if (!data || (uint64_t)offset >= (uint64_t)length) return NULL;
    end = (const uint8_t*)memchr(data + offset, '\0', length - offset);
    return end ? (const char*)(data + offset) : NULL;
}

static uint32_t theme_cacheHash(const char* name)
{
    const char* p = name;
    uint32_t hash;
    if (!p || !*p) return 0;
    hash = (uint32_t)(int32_t)(int8_t)*p;
    ++p;
    while (*p) {
        hash = (hash << 5) - hash +
               (uint32_t)(int32_t)(int8_t)*p;
        ++p;
    }
    return hash;
}

static bool theme_cachePath(const char* root, const char* theme,
                            char* out, size_t outSize)
{
    int written;
    if (!root || !root[0] || !theme || !theme[0] || !out || outSize == 0)
        return false;
    written = snprintf(out, outSize, "%s/%s/icon-theme.cache", root, theme);
    return written >= 0 && (size_t)written < outSize;
}

static int64_t theme_fileModified(const char* path, bool* exists)
{
    XString* fileName;
    XFileInfo* info;
    XDateTime modified;
    bool present;
    int64_t stamp;
    if (exists) *exists = false;
    if (!path || !path[0]) return 0;
    fileName = XString_create_utf8(path);
    info = fileName ? XFileInfo_create_2(fileName) : NULL;
    present = info && XFileInfo_exists(info);
    modified = present ? XFileInfo_lastModified(info) : XDateTime_create();
    stamp = present ? XDateTime_toSecsSinceEpoch(&modified) : 0;
    if (exists) *exists = present;
    if (info) XFileInfo_delete_base((XClass*)info);
    if (fileName) XString_delete_base((XClass*)fileName);
    return stamp;
}

static bool theme_cacheFresh(const uint8_t* data, size_t length,
                             uint32_t dirListOffset, uint32_t dirListLength,
                             const char* root, const char* theme,
                             int64_t cacheStamp)
{
    uint32_t i;
    uint32_t entryOffset;
    char themePath[1024];
    int written;
    bool themePresent;
    int64_t themeStamp;
    if (!data || !root || !theme || cacheStamp <= 0 ||
        !theme_cacheRange(data, length, dirListOffset,
                          4u + 4u * (uint64_t)dirListLength))
        return false;
    written = snprintf(themePath, sizeof(themePath), "%s/%s", root, theme);
    if (written < 0 || (size_t)written >= sizeof(themePath)) return false;
    themeStamp = theme_fileModified(themePath, &themePresent);
    /* Qt 先比较主题根目录时间，再检查缓存列出的每个内容目录。 */
    if (!themePresent || themeStamp > cacheStamp) return false;
    for (i = 0; i < dirListLength; ++i) {
        uint32_t dirOffset;
        char dirPath[1024];
        bool present;
        int64_t dirStamp;
        if (!theme_cacheAddOffset(dirListOffset,
                                  4u + 4u * (uint64_t)i, &entryOffset) ||
            !theme_cacheRead32(data, length, entryOffset, &dirOffset) ||
            !theme_cacheString(data, length, dirOffset))
            return false;
        {
            int written = snprintf(dirPath, sizeof(dirPath), "%s/%s/%s",
                                   root, theme,
                                   theme_cacheString(data, length, dirOffset));
            if (written < 0 || (size_t)written >= sizeof(dirPath))
                return false;
        }
        dirStamp = theme_fileModified(dirPath, &present);
        if (!present || dirStamp > cacheStamp) return false;
    }
    return true;
}

static int theme_cacheDirState(const char* root, const char* theme,
                               const char* name, const char* directory)
{
    char cachePath[1024];
    XString* cacheName = NULL;
    XFile* cacheFile = NULL;
    XByteArray* bytes = NULL;
    const uint8_t* data;
    size_t length;
    uint16_t version;
    uint32_t hashOffset;
    uint32_t dirListOffset;
    uint32_t hashBucketCount;
    uint32_t bucketOffset;
    uint32_t dirListLength;
    uint32_t bucketIndex;
    uint32_t nodeCount = 0;
    int result = -1;
    bool cacheExists = false;
    int64_t cacheStamp;

    if (!root || !theme || !name || !directory ||
        !theme_cachePath(root, theme, cachePath, sizeof(cachePath)))
        return -1;
    cacheStamp = theme_fileModified(cachePath, &cacheExists);
    if (!cacheExists || cacheStamp <= 0) return -1;
    cacheName = XString_create_utf8(cachePath);
    cacheFile = cacheName ? XFile_create_2(cacheName) : NULL;
    if (!cacheFile || !XIODevice_open_base((XIODevice*)cacheFile,
                                           XIODevice_ReadOnly))
        goto done;
    bytes = XIODevice_readAll_3((XIODevice*)cacheFile);
    XIODevice_close_base((XIODevice*)cacheFile);
    if (!bytes) goto done;
    data = (const uint8_t*)XByteArray_data(bytes);
    length = (size_t)XByteArray_size_base((const XContainer*)bytes);
    if (!theme_cacheRead16(data, length, 0, &version) || version != 1 ||
        !theme_cacheRead32(data, length, 4, &hashOffset) ||
        !theme_cacheRead32(data, length, 8, &dirListOffset) ||
        !theme_cacheRead32(data, length, hashOffset, &hashBucketCount) ||
        !theme_cacheRead32(data, length, dirListOffset, &dirListLength) ||
        hashBucketCount == 0 ||
        !theme_cacheRange(data, length, hashOffset,
                          4u + 4u * (uint64_t)hashBucketCount) ||
        !theme_cacheRange(data, length, dirListOffset,
                          4u + 4u * (uint64_t)dirListLength) ||
        !theme_cacheFresh(data, length, dirListOffset, dirListLength, root,
                          theme, cacheStamp))
        goto done;

    bucketIndex = theme_cacheHash(name) % hashBucketCount;
    {
        uint32_t bucketEntryOffset;
        if (!theme_cacheAddOffset(hashOffset,
                                  4u + 4u * (uint64_t)bucketIndex,
                                  &bucketEntryOffset) ||
            !theme_cacheRead32(data, length, bucketEntryOffset,
                               &bucketOffset))
            goto done;
    }
    while (bucketOffset != 0u) {
        uint32_t nextOffset;
        uint32_t nameOffset;
        uint32_t listOffset;
        uint32_t listLength;
        uint32_t listIndex;
        uint32_t nameFieldOffset;
        uint32_t listFieldOffset;
        if (!theme_cacheRange(data, length, bucketOffset, 12u) ||
            (bucketOffset & 3u) != 0u ||
            ++nodeCount > (uint32_t)(length / 12u + 1u) ||
            !theme_cacheAddOffset(bucketOffset, 4u, &nameFieldOffset) ||
            !theme_cacheAddOffset(bucketOffset, 8u, &listFieldOffset) ||
            !theme_cacheRead32(data, length, bucketOffset, &nextOffset) ||
            !theme_cacheRead32(data, length, nameFieldOffset, &nameOffset) ||
            !theme_cacheRead32(data, length, listFieldOffset, &listOffset) ||
            !theme_cacheString(data, length, nameOffset) ||
            !theme_cacheRead32(data, length, listOffset, &listLength) ||
            !theme_cacheRange(data, length, listOffset,
                              4u + 8u * (uint64_t)listLength))
            goto done;
        if (strcmp(theme_cacheString(data, length, nameOffset), name) == 0) {
            for (listIndex = 0; listIndex < listLength; ++listIndex) {
                uint16_t directoryIndex;
                uint32_t directoryOffset;
                uint32_t listEntryOffset;
                uint32_t directoryEntryOffset;
                const char* cachedDirectory;
                if (!theme_cacheAddOffset(listOffset,
                                          4u + 8u * (uint64_t)listIndex,
                                          &listEntryOffset) ||
                    !theme_cacheRead16(data, length, listEntryOffset,
                                       &directoryIndex) ||
                    directoryIndex >= dirListLength ||
                    !theme_cacheAddOffset(dirListOffset,
                                          4u + 4u * (uint64_t)directoryIndex,
                                          &directoryEntryOffset) ||
                    !theme_cacheRead32(data, length, directoryEntryOffset,
                                       &directoryOffset) ||
                    !(cachedDirectory = theme_cacheString(data, length,
                                                          directoryOffset)))
                    goto done;
                if (strcmp(cachedDirectory, directory) == 0) {
                    result = 1;
                    goto done;
                }
            }
            result = 0;
            goto done;
        }
        bucketOffset = nextOffset;
    }
    result = 0;
done:
    if (bytes) XByteArray_delete_base((XClass*)bytes);
    if (cacheFile) XClass_delete_base((XClass*)cacheFile);
    if (cacheName) XString_delete_base((XClass*)cacheName);
    return result;
}

static int theme_sizeDistance(int requested, int dirSize)
{
    if (requested <= 0 || dirSize <= 0) return INT_MAX;
    if (requested == dirSize) return 0;
    if (requested < dirSize) return dirSize - requested;
    return (requested - dirSize) * 4;
}

static bool theme_tryDir(const char* root, const char* theme, const char* name,
                         int target, const char* sizeDir, const char* context,
                         XPixmap* out, int* distance)
{
    char path[1024];
    size_t extIndex;
    int dirSize;
    bool loaded = false;
    if (!root || !root[0] || !theme || !theme[0] || !name || !name[0])
        return false;
    dirSize = theme_parseSize(sizeDir);
    for (extIndex = 0; extIndex < sizeof(theme_import_exts) /
             sizeof(theme_import_exts[0]); ++extIndex) {
        const char* ext = theme_import_exts[extIndex];
        if (!theme_extAllowed(ext)) continue;
        theme_buildDirPath(path, sizeof(path), root, theme, sizeDir, context,
                           name, ext);
        if (theme_loadFile(path, out)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) return false;
    if (distance)
        *distance = (dirSize > 0) ? (sizeDir[0] == 's' ? 0 :
                                     theme_sizeDistance(target, dirSize)) : 0;
    return true;
}

static bool theme_tryTheme(const char* root, const char* theme,
                           const char* name, int target, XPixmap* out,
                           int* distance)
{
    size_t di;
    size_t ci;
    for (di = 0; di < sizeof(theme_size_dirs) / sizeof(theme_size_dirs[0]); ++di) {
        for (ci = 0; ci < sizeof(theme_context_dirs) / sizeof(theme_context_dirs[0]); ++ci) {
            if (theme_tryDir(root, theme, name, target, theme_size_dirs[di],
                             theme_context_dirs[ci], out, distance))
                return true;
        }
    }
    return false;
}

typedef enum ThemeDirType
{
    ThemeDir_Threshold = 0, /**< 阈值型：允许在 Size +/- Threshold 范围内匹配 */
    ThemeDir_Scalable,      /**< 可缩放型：MinSize/MaxSize 区间内均匹配 */
    ThemeDir_Fixed,         /**< 固定型：仅精确 Size 匹配 */
    ThemeDir_Fallback,      /**< 回退型：任意尺寸均可匹配 */
    ThemeDir_Invalid = INT_MAX
} ThemeDirType;

typedef enum ThemeDirContext
{
    ThemeDir_UnknownContext = 0, /**< 未声明或未知 Context */
    ThemeDir_Applications,       /**< Applications 上下文 */
    ThemeDir_MimeTypes           /**< MimeTypes 上下文 */
} ThemeDirContext;

typedef struct ThemeDirInfo
{
    size_t       m_nameIndex; /**< 对应 ThemeContext::m_dirs 中的目录名索引 */
    int          m_size;      /**< 目录名义尺寸 */
    int          m_minSize;   /**< 可缩放最小尺寸；未提供时为 Size */
    int          m_maxSize;   /**< 可缩放最大尺寸；未提供时为 Size */
    int          m_threshold; /**< 阈值型允许误差；默认 2 */
    int          m_scale;     /**< 高分辨率倍率；默认 1 */
    ThemeDirType m_type;      /**< 目录类型 */
    ThemeDirContext m_context; /**< 目录上下文；通用回退时跳过应用/MIME 目录 */
} ThemeDirInfo;

typedef struct ThemeContext
{
    XStringList*   m_dirs;      /**< Directories 列出的目录名 */
    XStringList*   m_parents;   /**< Inherits 列出的父主题名 */
    ThemeDirInfo*  m_meta;      /**< 每个目录对应的元数据 */
    size_t         m_metaCount; /**< m_meta 元素个数 */
    bool           m_valid;     /**< 成功读取过 index.theme */
} ThemeContext;

typedef struct ThemeVisitStack
{
    const char* m_names[64]; /**< 已访问主题名，防循环继承 */
    size_t      m_count;     /**< 当前访问深度 */
} ThemeVisitStack;

static bool theme_keyEquals(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

static char* theme_trimLine(char* s)
{
    char* end;
    if (!s) return NULL;
    while (*s == ' ' || *s == '\t' || *s == '\r') ++s;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r')) --end;
    *end = '\0';
    return s;
}

static void theme_trimSpan(const char** start, const char** end)
{
    if (!start || !end || *start >= *end) return;
    while (*start < *end && (**start == ' ' || **start == '\t' ||
                             **start == '\r')) ++*start;
    while (*end > *start && ((*end)[-1] == ' ' || (*end)[-1] == '\t' ||
                             (*end)[-1] == '\r')) --*end;
}

static int theme_parseInt(const char* value, int fallback)
{
    const char* p;
    int sign = 1;
    int result = 0;
    bool any = false;
    if (!value) return fallback;
    p = value;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '-') { sign = -1; ++p; }
    else if (*p == '+') ++p;
    while (*p >= '0' && *p <= '9') {
        if (result > (INT_MAX - (*p - '0')) / 10) return fallback;
        result = result * 10 + (*p - '0');
        any = true;
        ++p;
    }
    return any ? result * sign : fallback;
}

static void theme_splitComma(const char* value, XStringList* out)
{
    const char* p;
    const char* start;
    if (!value || !out) return;
    p = value;
    start = value;
    while (1) {
        const char* end;
        XString* item;
        if (*p == ',' || *p == '\0') {
            end = p;
            theme_trimSpan(&start, &end);
            if (end > start) {
                item = XString_create_with_length_utf8(
                    start, (size_t)(end - start));
                if (item) {
                    if (!XString_isEmpty_base((const XContainer*)item))
                        XStringList_push_back_move_base((XVector*)out, item);
                    XString_delete_base((XClass*)item);
                }
            }
            if (*p == '\0') break;
            start = p + 1;
        }
        ++p;
    }
}

/**
 * @brief 释放由主题解析器拥有的字符串列表。
 * @param list 待释放的字符串列表；允许传入 NULL。
 * @note XStringList 的基础反初始化不会替列表元素调用析构，因此这里
 *       先逐项释放嵌入 XString，再释放列表容器本身。
 */
static void theme_stringListDelete(XStringList* list)
{
    size_t count;
    size_t i;
    if (!list)
        return;
    /* 主题路径可能与全局路径列表共享，先分离再析构嵌入字符串。 */
    XVector_detach((XVector*)list);
    count = XStringList_size_base((const XContainer*)list);
    for (i = 0; i < count; ++i)
    {
        XString* item = (XString*)XStringList_at_base(
            (XVector*)list, (int64_t)i);
        if (item)
            XString_deinit_base((XClass*)item);
    }
    XContainerSize((XContainer*)list) = 0;
    XStringList_delete_base((XClass*)list);
}

static void themeContext_init(ThemeContext* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    self->m_dirs = XStringList_create();
    self->m_parents = XStringList_create();
}

static void themeContext_deinit(ThemeContext* self)
{
    if (!self) return;
    theme_stringListDelete(self->m_dirs);
    theme_stringListDelete(self->m_parents);
    if (self->m_meta) XFree_System(self->m_meta);
    memset(self, 0, sizeof(*self));
}

static bool themeContext_prepareDirs(ThemeContext* self)
{
    size_t count;
    size_t i;
    if (!self || !self->m_dirs) return false;
    count = (size_t)XStringList_size_base((XContainer*)self->m_dirs);
    self->m_meta = count ? (ThemeDirInfo*)XMalloc_System(
        count * sizeof(ThemeDirInfo)) : NULL;
    if (count && !self->m_meta) return false;
    self->m_metaCount = count;
    memset(self->m_meta, 0, count * sizeof(ThemeDirInfo));
    for (i = 0; i < count; ++i) {
        const XString* nameStr = (const XString*)XStringList_at_base(
            (const XVector*)self->m_dirs, (int64_t)i);
        const char* name = nameStr ? XString_toUtf8(nameStr) : NULL;
        self->m_meta[i].m_nameIndex = i;
        self->m_meta[i].m_size = theme_parseSize(name);
        self->m_meta[i].m_minSize = -1;
        self->m_meta[i].m_maxSize = -1;
        self->m_meta[i].m_threshold = 2;
        self->m_meta[i].m_scale = 1;
        self->m_meta[i].m_type = ThemeDir_Threshold;
        self->m_meta[i].m_context = ThemeDir_UnknownContext;
    }
    return true;
}

static ThemeDirInfo* themeContext_findDir(ThemeContext* self,
                                          const char* section)
{
    size_t i;
    if (!self || !section || !section[0]) return NULL;
    for (i = 0; i < self->m_metaCount; ++i) {
        const XString* nameStr = (const XString*)XStringList_at_base(
            (const XVector*)self->m_dirs, (int64_t)i);
        const char* name = nameStr ? XString_toUtf8(nameStr) : NULL;
        if (name && strcmp(name, section) == 0)
            return &self->m_meta[i];
    }
    return NULL;
}

static void themeContext_finalizeDirs(ThemeContext* self)
{
    size_t i;
    if (!self) return;
    for (i = 0; i < self->m_metaCount; ++i) {
        ThemeDirInfo* info = &self->m_meta[i];
        if (info->m_size > 0) {
            if (info->m_minSize < 0) info->m_minSize = info->m_size;
            if (info->m_maxSize < 0) info->m_maxSize = info->m_size;
        } else {
            /* Qt QIconTheme 仅从 index.theme 中带正 Size 的组建立
               QIconDirInfo；缺少 Size 的组不是主题内的 Fallback 目录。
               Fallback 只用于主题外的独立文件条目。 */
            info->m_type = ThemeDir_Invalid;
        }
    }
}

static bool theme_parseIndexFile(const char* root, const char* theme,
                                 ThemeContext* out)
{
    char filePath[1024];
    XString* fileName = NULL;
    XFile* file = NULL;
    XByteArray* bytes = NULL;
    char* text = NULL;
    char section[256];
    size_t len;
    bool ok = false;

    if (!root || !root[0] || !theme || !theme[0] || !out) return false;
    themeContext_deinit(out);
    themeContext_init(out);
    snprintf(filePath, sizeof(filePath), "%s/%s/index.theme", root, theme);
    fileName = XString_create_utf8(filePath);
    if (!fileName) return false;
    file = XFile_create_2(fileName);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        XString_delete_base((XClass*)fileName);
        return false;
    }
    bytes = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    file = NULL;
    if (!bytes) goto done;

    len = (size_t)XByteArray_size_base((const XContainer*)bytes);
    text = (char*)XMalloc_System(len + 1);
    if (!text) goto done;
    if (len) memcpy(text, XByteArray_data(bytes), len);
    text[len] = '\0';

    {
        const char* cursor = text;
        section[0] = '\0';
        while (cursor && *cursor) {
            char line[1024];
            const char* newline;
            size_t n;
            char* lp;
            newline = strchr(cursor, '\n');
            n = newline ? (size_t)(newline - cursor) : strlen(cursor);
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, cursor, n);
            line[n] = '\0';
            lp = theme_trimLine(line);
            if (lp[0]) {
                if (lp[0] == '[') {
                    char* closing = strchr(lp, ']');
                    const char* start;
                    const char* end;
                    size_t sLen;
                    if (closing) {
                        start = lp + 1;
                        end = closing;
                        theme_trimSpan(&start, &end);
                        sLen = (size_t)(end - start);
                        if (sLen >= sizeof(section)) sLen = sizeof(section) - 1;
                        memcpy(section, start, sLen);
                        section[sLen] = '\0';
                    } else {
                        section[0] = '\0';
                    }
                } else {
                    char* eq = strchr(lp, '=');
                    if (eq && section[0]) {
                        char* key;
                        char* value;
                        *eq = '\0';
                        key = theme_trimLine(lp);
                        value = theme_trimLine(eq + 1);
                        if (theme_keyEquals(section, "Icon Theme")) {
                            if (theme_keyEquals(key, "Directories"))
                                theme_splitComma(value, out->m_dirs);
                            else if (theme_keyEquals(key, "Inherits"))
                                theme_splitComma(value, out->m_parents);
                        }
                    }
                }
            }
            cursor = newline ? newline + 1 : cursor + n;
        }
    }

    out->m_valid = true;
    if (!themeContext_prepareDirs(out)) {
        out->m_valid = false;
        goto done;
    }
    {
        const char* cursor = text;
        section[0] = '\0';
        while (cursor && *cursor) {
            char line[1024];
            const char* newline;
            size_t n;
            char* lp;
            newline = strchr(cursor, '\n');
            n = newline ? (size_t)(newline - cursor) : strlen(cursor);
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, cursor, n);
            line[n] = '\0';
            lp = theme_trimLine(line);
            if (lp[0]) {
                if (lp[0] == '[') {
                    char* closing = strchr(lp, ']');
                    const char* start;
                    const char* end;
                    size_t sLen;
                    if (closing) {
                        start = lp + 1;
                        end = closing;
                        theme_trimSpan(&start, &end);
                        sLen = (size_t)(end - start);
                        if (sLen >= sizeof(section)) sLen = sizeof(section) - 1;
                        memcpy(section, start, sLen);
                        section[sLen] = '\0';
                    } else {
                        section[0] = '\0';
                    }
                } else {
                    char* eq = strchr(lp, '=');
                    if (eq && section[0]) {
                        char* key;
                        char* value;
                        ThemeDirInfo* info;
                        *eq = '\0';
                        key = theme_trimLine(lp);
                        value = theme_trimLine(eq + 1);
                        info = themeContext_findDir(out, section);
                        if (info) {
                            if (theme_keyEquals(key, "Size"))
                                info->m_size = theme_parseInt(value, info->m_size);
                            else if (theme_keyEquals(key, "MinSize"))
                                info->m_minSize = theme_parseInt(value,
                                                                 info->m_minSize);
                            else if (theme_keyEquals(key, "MaxSize"))
                                info->m_maxSize = theme_parseInt(value,
                                                                 info->m_maxSize);
                            else if (theme_keyEquals(key, "Threshold"))
                                info->m_threshold = theme_parseInt(value, 2);
                            else if (theme_keyEquals(key, "Scale"))
                                info->m_scale = theme_parseInt(value, 1);
                            else if (theme_keyEquals(key, "Context")) {
                                if (theme_keyEquals(value, "Applications"))
                                    info->m_context = ThemeDir_Applications;
                                else if (theme_keyEquals(value, "MimeTypes"))
                                    info->m_context = ThemeDir_MimeTypes;
                                else
                                    info->m_context = ThemeDir_UnknownContext;
                            }
                            else if (theme_keyEquals(key, "Type")) {
                                if (theme_keyEquals(value, "Fixed"))
                                    info->m_type = ThemeDir_Fixed;
                                else if (theme_keyEquals(value, "Scalable"))
                                    info->m_type = ThemeDir_Scalable;
                                else if (theme_keyEquals(value, "Threshold"))
                                    info->m_type = ThemeDir_Threshold;
                                else
                                    info->m_type = ThemeDir_Threshold;
                            }
                        }
                    }
                }
            }
            cursor = newline ? newline + 1 : cursor + n;
        }
    }

    themeContext_finalizeDirs(out);
    ok = true;
done:
    if (text) XFree_System(text);
    if (bytes) XByteArray_delete_base((XClass*)bytes);
    if (fileName) XString_delete_base((XClass*)fileName);
    return ok;
}

static bool theme_directoryMatches(const ThemeDirInfo* info, int iconsize,
                                   int iconscale)
{
    int scale;
    if (!info || iconsize <= 0 || iconscale <= 0) return false;
    /* Qt directoryMatchesSize() 直接比较元数据 Scale；显式 Scale=0
       不得被当成默认的 1 倍目录。 */
    scale = info->m_scale;
    if (scale != iconscale) return false;
    if (info->m_type == ThemeDir_Fixed)
        return info->m_size == iconsize;
    if (info->m_type == ThemeDir_Scalable)
        return iconsize >= info->m_minSize && iconsize <= info->m_maxSize;
    if (info->m_type == ThemeDir_Threshold)
        return iconsize >= info->m_size - info->m_threshold &&
               iconsize <= info->m_size + info->m_threshold;
    if (info->m_type == ThemeDir_Fallback) return true;
    return false;
}

static int theme_dirDistance(const ThemeDirInfo* info, int iconsize,
                             int iconscale)
{
    int scale;
    int scaledIconSize;
    int size;
    int minSize;
    int maxSize;
    int scaledMin;
    int scaledMax;
    int scaledLow;
    int scaledHigh;
    if (!info || iconsize <= 0 || iconscale <= 0) return INT_MAX;
    if (info->m_type == ThemeDir_Fallback) return 0;
    /* Qt directorySizeDistance() 同样使用原始 Scale 值。 */
    scale = info->m_scale;
    scaledIconSize = iconsize * iconscale;
    size = info->m_size > 0 ? info->m_size * scale : 0;
    minSize = info->m_minSize > 0 ? info->m_minSize * scale : size;
    maxSize = info->m_maxSize > 0 ? info->m_maxSize * scale : size;
    if (info->m_type == ThemeDir_Fixed) {
        return size > 0 ? (scaledIconSize > size ? scaledIconSize - size :
                           size - scaledIconSize)
                        : INT_MAX;
    }
    if (info->m_type == ThemeDir_Scalable) {
        if (minSize <= 0 || maxSize <= 0) return INT_MAX;
        if (scaledIconSize < minSize) return minSize - scaledIconSize;
        if (scaledIconSize > maxSize) return scaledIconSize - maxSize;
        return 0;
    }
    if (info->m_type == ThemeDir_Invalid) return INT_MAX;
    if (size <= 0) return INT_MAX;
    /* Threshold 的负值也按 Qt 的原始算式保留，不要静默改成零。 */
    scaledLow = size - info->m_threshold * scale;
    scaledHigh = size + info->m_threshold * scale;
    scaledMin = info->m_minSize > 0 ? info->m_minSize * scale : size;
    scaledMax = info->m_maxSize > 0 ? info->m_maxSize * scale : size;
    if (scaledIconSize < scaledLow) return scaledMin - scaledIconSize;
    if (scaledIconSize > scaledHigh) return scaledIconSize - scaledMax;
    return 0;
}

static bool theme_tryParsedDir(const ThemeContext* ctx, size_t dirIndex,
                               const char* root, const char* theme,
                               const char* name, int target, int iconScale,
                               XPixmap* out, int* distance,
                               int* formatPriority)
{
    const XString* dirStr;
    const char* dir;
    const ThemeDirInfo* info;
    size_t extIndex;
    char path[1024];
    bool loaded = false;
    if (!ctx || !root || !theme || !name) return false;
    if (dirIndex >= ctx->m_metaCount || !ctx->m_dirs) return false;
    if (formatPriority) *formatPriority = INT_MAX;
    dirStr = (const XString*)XStringList_at_base(
        (const XVector*)ctx->m_dirs, (int64_t)dirIndex);
    dir = dirStr ? XString_toUtf8(dirStr) : NULL;
    info = &ctx->m_meta[dirIndex];
    if (!dir || !dir[0]) return false;
    /* A valid GTK cache can reject this directory without probing each
       extension.  Unknown/invalid caches deliberately fall through. */
    if (theme_cacheDirState(root, theme, name, dir) == 0)
        return false;
    for (extIndex = 0; extIndex < sizeof(theme_import_exts) /
             sizeof(theme_import_exts[0]); ++extIndex) {
        const char* ext = theme_import_exts[extIndex];
        if (!theme_extAllowed(ext)) continue;
        snprintf(path, sizeof(path), "%s/%s/%s/%s%s", root, theme, dir,
                 name, ext);
        if (theme_loadFile(path, out)) {
            loaded = true;
            if (formatPriority) *formatPriority = theme_extPriority(ext);
            break;
        }
    }
    if (!loaded) return false;
    if (distance) *distance = theme_dirDistance(info, target, iconScale);
    return true;
}

/*
 * 与 theme_tryParsedDir() 配对的登记查询。Qt QIconLoader 先用
 * QFile::exists() 建立 PixmapEntry/ScalableEntry，实际文件解码延迟到
 * pixmap() 调用；因此这里故意不调用 theme_loadFile()，损坏文件也应
 * 作为已登记条目参与 isNull()/hasThemeIcon() 判定。
 */
static bool theme_tryParsedDirExists(const ThemeContext* ctx, size_t dirIndex,
                                     const char* root, const char* theme,
                                     const char* name)
{
    const XString* dirStr;
    const char* dir;
    size_t extIndex;
    char path[1024];
    if (!ctx || !ctx->m_dirs || !root || !root[0] || !theme || !theme[0] ||
        !name || !name[0] || dirIndex >= ctx->m_metaCount)
        return false;
    dirStr = (const XString*)XStringList_at_base(
        (const XVector*)ctx->m_dirs, (int64_t)dirIndex);
    dir = dirStr ? XString_toUtf8(dirStr) : NULL;
    if (!dir || !dir[0]) return false;
    if (theme_cacheDirState(root, theme, name, dir) == 0)
        return false;
    for (extIndex = 0; extIndex < sizeof(theme_import_exts) /
             sizeof(theme_import_exts[0]); ++extIndex) {
        const char* ext = theme_import_exts[extIndex];
        if (!theme_extAllowed(ext)) continue;
        if (snprintf(path, sizeof(path), "%s/%s/%s/%s%s", root, theme,
                     dir, name, ext) < 0 ||
            !theme_fileExists(path))
            continue;
        return true;
    }
    return false;
}

static bool theme_tryParsedTheme(const ThemeContext* ctx,
                                 const char* root, const char* theme,
                                 const char* name, int target, int iconScale,
                                 bool skipGenericContext,
                                 XPixmap* best, int* bestDistance)
{
    size_t di;
    bool found = false;
    bool foundExact = false;
    int bestFormatPriority = INT_MAX;
    if (!ctx) return false;

    /* QIconLoaderEngine::entryForSize() gives exact Scale/size matches a
       separate first pass, independent of directory order or distance. */
    for (di = 0; di < ctx->m_metaCount; ++di) {
        XPixmap candidate;
        int distance = INT_MAX;
        int formatPriority = INT_MAX;
        if (skipGenericContext &&
            (ctx->m_meta[di].m_context == ThemeDir_Applications ||
             ctx->m_meta[di].m_context == ThemeDir_MimeTypes))
            continue;
        if (!theme_directoryMatches(&ctx->m_meta[di], target, iconScale))
            continue;
        XPixmap_init(&candidate);
        if (best && theme_tryParsedDir(ctx, di, root, theme, name, target,
                                       iconScale, &candidate, &distance,
                                       &formatPriority)) {
            if (distance == 0 &&
                (!foundExact || formatPriority < bestFormatPriority)) {
                if (!XPixmap_isNull(best)) XPixmap_deinit_base(best);
                XPixmap_move_base(best, &candidate);
                *bestDistance = 0;
                bestFormatPriority = formatPriority;
                foundExact = true;
            }
        }
        XPixmap_deinit_base(&candidate);
    }
    if (foundExact) return true;

    /* No exact match: choose the first minimum-distance entry, as Qt does. */
    for (di = 0; di < ctx->m_metaCount; ++di) {
        XPixmap candidate;
        int distance = INT_MAX;
        int formatPriority = INT_MAX;
        if (skipGenericContext &&
            (ctx->m_meta[di].m_context == ThemeDir_Applications ||
             ctx->m_meta[di].m_context == ThemeDir_MimeTypes))
            continue;
        XPixmap_init(&candidate);
        if (best && theme_tryParsedDir(ctx, di, root, theme, name, target,
                                       iconScale, &candidate, &distance,
                                       &formatPriority) &&
            (distance < *bestDistance ||
             (distance == *bestDistance &&
              formatPriority < bestFormatPriority))) {
            if (!XPixmap_isNull(best)) XPixmap_deinit_base(best);
            XPixmap_move_base(best, &candidate);
            XPixmap_deinit_base(&candidate);
            *bestDistance = distance;
            bestFormatPriority = formatPriority;
            found = true;
        } else {
            XPixmap_deinit_base(&candidate);
        }
    }
    return found;
}

static bool theme_visitContains(const ThemeVisitStack* stack, const char* name)
{
    size_t i;
    if (!stack || !name) return false;
    for (i = 0; i < stack->m_count; ++i) {
        if (stack->m_names[i] && strcmp(stack->m_names[i], name) == 0)
            return true;
    }
    return false;
}

static void theme_visitPush(ThemeVisitStack* stack, const char* name)
{
    if (stack && name &&
        stack->m_count < sizeof(stack->m_names) / sizeof(stack->m_names[0]))
        stack->m_names[stack->m_count++] = name;
}

static void theme_visitPop(ThemeVisitStack* stack)
{
    if (stack && stack->m_count)
        stack->m_names[--stack->m_count] = NULL;
}

static void theme_appendUnique(XStringList* list, const char* value)
{
    size_t i;
    if (!list || !value || !value[0]) return;
    for (i = 0; i < (size_t)XStringList_size_base((XContainer*)list); ++i) {
        const XString* item = (const XString*)XStringList_at_base(
            (const XVector*)list, (int64_t)i);
        const char* n = item ? XString_toUtf8(item) : NULL;
        if (n && strcmp(n, value) == 0) return;
    }
    XStringList_push_back_utf8(list, value);
}

static XStringList* theme_parentsFor(const ThemeContext* ctx,
                                     const char* fallbackTheme)
{
    XStringList* result = XStringList_create();
    size_t i;
    if (!result) return NULL;
    if (ctx && ctx->m_parents) {
        for (i = 0; i < (size_t)XStringList_size_base(
                 (XContainer*)ctx->m_parents); ++i) {
            const XString* src = (const XString*)XStringList_at_base(
                (const XVector*)ctx->m_parents, (int64_t)i);
            XString* trimmed;
            const char* name;
            if (!src) continue;
            trimmed = XString_trimmed(src);
            if (!trimmed) continue;
            name = XString_toUtf8(trimmed);
            if (name && name[0] && strcmp(name, "hicolor") != 0)
                theme_appendUnique(result, name);
            XString_delete_base((XClass*)trimmed);
        }
    }
    if (fallbackTheme && fallbackTheme[0] &&
        strcmp(fallbackTheme, "hicolor") != 0)
        theme_appendUnique(result, fallbackTheme);
    theme_appendUnique(result, "hicolor");
    return result;
}

static size_t theme_lastDash(const char* name, size_t len)
{
    while (len > 0) {
        --len;
        if (name[len] == '-')
            return len;
    }
    return (size_t)-1;
}

static bool theme_searchTheme(const XStringList* paths, const char* theme,
                              const char* name, int target, int iconScale,
                              const char* fallbackTheme,
                              ThemeVisitStack* visited,
                              bool genericFallback, bool allowDashFallback,
                              XPixmap* out)
{
    ThemeContext ctx;
    XPixmap best;
    int bestDistance = INT_MAX;
    XStringList* parents = NULL;
    size_t pathIndex;
    size_t parentIndex;
    bool parsed = false;
    bool local = false;
    bool foundParent = false;
    bool dashFound = false;

    if (!paths || !theme || !theme[0] || !name || !name[0]) return false;
    if (!visited || theme_visitContains(visited, theme)) return false;
    theme_visitPush(visited, theme);
    themeContext_init(&ctx);

    for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
             (XContainer*)paths); ++pathIndex) {
        const XString* rootStr = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
        if (!root || !root[0]) continue;
        if (!parsed && theme_parseIndexFile(root, theme, &ctx))
            parsed = true;
    }

    XPixmap_init(&best);
    if (parsed) {
        /* Qt QIconTheme treats an existing index.theme as authoritative.  An
           empty Directories list has no local entries and must proceed to
           Inherits, rather than probing legacy size/context directories. */
        for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
                 (XContainer*)paths); ++pathIndex) {
            const XString* rootStr = (const XString*)XStringList_at_base(
                (const XVector*)paths, (int64_t)pathIndex);
            const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
            if (!root || !root[0]) continue;
            if (theme_tryParsedTheme(&ctx, root, theme, name, target, iconScale,
                                     genericFallback,
                                     &best, &bestDistance)) {
                local = true;
            }
        }
    } else {
        /* Only themes without index.theme use the embedded legacy directory
           convention; a malformed/empty indexed theme is not legacy. */
        for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
                 (XContainer*)paths); ++pathIndex) {
            const XString* rootStr = (const XString*)XStringList_at_base(
                (const XVector*)paths, (int64_t)pathIndex);
            const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
            XPixmap candidate;
            int distance = INT_MAX;
            if (!root || !root[0]) continue;
            XPixmap_init(&candidate);
            if (theme_tryTheme(root, theme, name, target, &candidate,
                               &distance) && distance < bestDistance) {
                if (!XPixmap_isNull(&best)) XPixmap_deinit_base(&best);
                XPixmap_move_base(&best, &candidate);
                XPixmap_deinit_base(&candidate);
                bestDistance = distance;
                local = true;
                if (distance == 0) break;
            } else {
                XPixmap_deinit_base(&candidate);
            }
        }
    }

    if (local) {
        if (out) {
            XPixmap_move_base(out, &best);
            XPixmap_deinit_base(&best);
        }
        theme_visitPop(visited);
        themeContext_deinit(&ctx);
        return true;
    }

    parents = theme_parentsFor(&ctx, fallbackTheme);
    if (parents) {
        for (parentIndex = 0; parentIndex < (size_t)XStringList_size_base(
                 (XContainer*)parents); ++parentIndex) {
            const XString* parentStr = (const XString*)XStringList_at_base(
                (const XVector*)parents, (int64_t)parentIndex);
            const char* parent = parentStr ? XString_toUtf8(parentStr) : NULL;
            if (!parent || !parent[0]) continue;
            if (theme_searchTheme(paths, parent, name, target, iconScale,
                                  fallbackTheme,
                                  visited, genericFallback, false, out)) {
                foundParent = true;
                break;
            }
        }
    }

    if (allowDashFallback && !foundParent) {
        size_t nameLen = strlen(name);
        while (nameLen > 2) {
            char dashName[1024];
            size_t dashPos;
            ThemeVisitStack fresh;
            dashPos = theme_lastDash(name, nameLen);
            if (dashPos == (size_t)-1 || dashPos == 0) break;
            nameLen = dashPos;
            if (nameLen >= sizeof(dashName)) break;
            memcpy(dashName, name, nameLen);
            dashName[nameLen] = '\0';
            memset(&fresh, 0, sizeof(fresh));
            if (theme_searchTheme(paths, theme, dashName, target, iconScale,
                                  fallbackTheme, &fresh, true, true, out)) {
                dashFound = true;
                break;
            }
        }
    }

    XPixmap_deinit_base(&best);
    theme_stringListDelete(parents);
    theme_visitPop(visited);
    themeContext_deinit(&ctx);
    return foundParent || dashFound;
}

/*
 * 只按主题目录登记关系查找图标。该路径复用与 theme_searchTheme() 相同
 * 的 Inherits、hicolor 和短横线回退顺序，但将“文件存在”作为命中条件，
 * 不触发图像解码，从而符合 QIconLoaderEngine::isNull() 的条目语义。
 */
static bool theme_searchThemeExists(const XStringList* paths,
                                    const char* theme, const char* name,
                                    int target, int iconScale,
                                    const char* fallbackTheme,
                                    ThemeVisitStack* visited,
                                    bool genericFallback,
                                    bool allowDashFallback)
{
    ThemeContext ctx;
    XStringList* parents = NULL;
    size_t pathIndex;
    size_t dirIndex;
    size_t parentIndex;
    bool parsed = false;
    bool local = false;
    if (!paths || !theme || !theme[0] || !name || !name[0] || !visited ||
        theme_visitContains(visited, theme))
        return false;
    theme_visitPush(visited, theme);
    themeContext_init(&ctx);
    for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
             (const XContainer*)paths); ++pathIndex) {
        const XString* rootStr = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
        if (!root || !root[0]) continue;
        if (!parsed && theme_parseIndexFile(root, theme, &ctx))
            parsed = true;
    }
    if (parsed) {
        for (dirIndex = 0; dirIndex < ctx.m_metaCount && !local; ++dirIndex) {
            const ThemeDirInfo* info = &ctx.m_meta[dirIndex];
            if (info->m_type == ThemeDir_Invalid ||
                (genericFallback &&
                 (info->m_context == ThemeDir_Applications ||
                  info->m_context == ThemeDir_MimeTypes)) ||
                !theme_directoryMatches(info, target, iconScale))
                continue;
            for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
                     (const XContainer*)paths); ++pathIndex) {
                const XString* rootStr = (const XString*)XStringList_at_base(
                    (const XVector*)paths, (int64_t)pathIndex);
                const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
                if (theme_tryParsedDirExists(&ctx, dirIndex, root, theme,
                                             name)) {
                    local = true;
                    break;
                }
            }
        }
    } else {
        /* 无 index.theme 时仅保留项目已有的传统目录约定。 */
        for (dirIndex = 0; dirIndex < sizeof(theme_size_dirs) /
                 sizeof(theme_size_dirs[0]) && !local; ++dirIndex) {
            int logicalSize = theme_parseSize(theme_size_dirs[dirIndex]);
            size_t contextIndex;
            (void)logicalSize;
            for (contextIndex = 0; contextIndex < sizeof(theme_context_dirs) /
                     sizeof(theme_context_dirs[0]) && !local; ++contextIndex) {
                for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
                         (const XContainer*)paths); ++pathIndex) {
                    const XString* rootStr = (const XString*)XStringList_at_base(
                        (const XVector*)paths, (int64_t)pathIndex);
                    const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
                    char path[1024];
                    size_t extIndex;
                    if (!root || !root[0]) continue;
                    for (extIndex = 0; extIndex < sizeof(theme_import_exts) /
                             sizeof(theme_import_exts[0]); ++extIndex) {
                        const char* ext = theme_import_exts[extIndex];
                        if (!theme_extAllowed(ext)) continue;
                        if (snprintf(path, sizeof(path), "%s/%s/%s/%s%s",
                                     root, theme, theme_size_dirs[dirIndex],
                                     theme_context_dirs[contextIndex], name,
                                     ext) >= 0 && theme_fileExists(path)) {
                            local = true;
                            break;
                        }
                    }
                    if (local) break;
                }
            }
        }
    }
    if (local) {
        theme_visitPop(visited);
        themeContext_deinit(&ctx);
        return true;
    }
    parents = theme_parentsFor(&ctx, fallbackTheme);
    if (parents) {
        for (parentIndex = 0; parentIndex < (size_t)XStringList_size_base(
                 (const XContainer*)parents); ++parentIndex) {
            const XString* parentStr = (const XString*)XStringList_at_base(
                (const XVector*)parents, (int64_t)parentIndex);
            const char* parent = parentStr ? XString_toUtf8(parentStr) : NULL;
            if (parent && theme_searchThemeExists(paths, parent, name, target,
                                                  iconScale, fallbackTheme,
                                                  visited, genericFallback,
                                                  false)) {
                local = true;
                break;
            }
        }
    }
    if (!local && allowDashFallback) {
        size_t nameLen = strlen(name);
        while (nameLen > 2) {
            char dashName[1024];
            size_t dashPos = theme_lastDash(name, nameLen);
            ThemeVisitStack fresh;
            if (dashPos == (size_t)-1 || dashPos == 0 ||
                dashPos >= sizeof(dashName)) break;
            nameLen = dashPos;
            memcpy(dashName, name, nameLen);
            dashName[nameLen] = '\0';
            memset(&fresh, 0, sizeof(fresh));
            if (theme_searchThemeExists(paths, theme, dashName, target,
                                        iconScale, fallbackTheme, &fresh,
                                        true, true)) {
                local = true;
                break;
            }
        }
    }
    theme_stringListDelete(parents);
    theme_visitPop(visited);
    themeContext_deinit(&ctx);
    return local;
}

static bool theme_scaledToSize(XPixmap* pixmap, int target)
{
    int w;
    int h;
    int targetWidth;
    int targetHeight;
    XPixmap scaled;
    if (!pixmap || XPixmap_isNull(pixmap) || target <= 1) return false;
    w = XPixmap_width(pixmap);
    h = XPixmap_height(pixmap);
    if (w <= 0 || h <= 0) return false;
    /* 对齐 Qt QPixmapIconEngine::adjustSize(expected, source)：小于请求
       尺寸的回退文件保持原尺寸，只有超出请求边界时才按比例缩小。 */
    targetWidth = target;
    targetHeight = target;
    if (w <= targetWidth && h <= targetHeight) return true;
    XPixmap_init(&scaled);
    /* aspectMode=1 是 KeepAspectRatio；这样 7x5 文件在 18x18 请求下
       保持为 18x13，而不是被拉伸成 18x18。 */
    XPixmap_scaled(pixmap, targetWidth, targetHeight, 1, 0, &scaled);
    if (!XPixmap_isNull(&scaled)) {
        XPixmap_deinit_base(pixmap);
        XPixmap_move_base(pixmap, &scaled);
        XPixmap_deinit_base(&scaled);
        return true;
    }
    XPixmap_deinit_base(&scaled);
    return false;
}

static bool theme_dirHasIcon(const char* root, const char* theme,
                             const char* dir, const char* name)
{
    size_t extIndex;
    char path[1024];
    XPixmap candidate;
    if (!root || !root[0] || !theme || !theme[0] || !dir || !dir[0] ||
        !name || !name[0])
        return false;
    if (theme_cacheDirState(root, theme, name, dir) == 0)
        return false;
    for (extIndex = 0; extIndex < sizeof(theme_import_exts) /
             sizeof(theme_import_exts[0]); ++extIndex) {
        const char* ext = theme_import_exts[extIndex];
        if (!theme_extAllowed(ext)) continue;
        snprintf(path, sizeof(path), "%s/%s/%s/%s%s", root, theme, dir,
                 name, ext);
        XPixmap_init(&candidate);
        if (theme_loadFile(path, &candidate)) {
            XPixmap_deinit_base(&candidate);
            return true;
        }
        XPixmap_deinit_base(&candidate);
    }
    return false;
}

static void theme_appendSizeUnique(XVector* out, int size)
{
    size_t i;
    XSize value;
    if (!out || size <= 0) return;
    for (i = 0; i < XVector_size_base((const XContainer*)out); ++i) {
        const XSize* existing = (const XSize*)XVector_at_base(out,
                                                               (int64_t)i);
        if (existing && existing->width == size && existing->height == size)
            return;
    }
    value.width = size;
    value.height = size;
    XVector_push_back_1_base(out, &value);
}

static bool theme_collectSizes(const XStringList* paths, const char* theme,
                               const char* name, const char* fallbackTheme,
                               ThemeVisitStack* visited, XVector* out)
{
    ThemeContext ctx;
    XStringList* parents = NULL;
    bool parsed = false;
    bool found = false;
    size_t pathIndex;
    size_t dirIndex;
    size_t parentIndex;
    if (!paths || !theme || !theme[0] || !name || !name[0] || !visited ||
        !out || theme_visitContains(visited, theme))
        return false;
    theme_visitPush(visited, theme);
    themeContext_init(&ctx);
    for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
             (const XContainer*)paths); ++pathIndex) {
        const XString* rootStr = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
        if (!root || !root[0]) continue;
        if (!parsed && theme_parseIndexFile(root, theme, &ctx))
            parsed = true;
    }
    if (parsed) {
        /* As in QIconTheme, an existing index.theme suppresses legacy
           directory probing even when it declares no content directories. */
        for (dirIndex = 0; dirIndex < ctx.m_metaCount; ++dirIndex) {
            const XString* dirStr = (const XString*)XStringList_at_base(
                (const XVector*)ctx.m_dirs, (int64_t)dirIndex);
            const char* dir = dirStr ? XString_toUtf8(dirStr) : NULL;
            int logicalSize = ctx.m_meta[dirIndex].m_size;
            if (!dir || !dir[0] || logicalSize <= 0) continue;
            for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
                     (const XContainer*)paths); ++pathIndex) {
                const XString* rootStr = (const XString*)XStringList_at_base(
                    (const XVector*)paths, (int64_t)pathIndex);
                const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
                if (theme_dirHasIcon(root, theme, dir, name)) {
                    theme_appendSizeUnique(out, logicalSize);
                    found = true;
                    break;
                }
            }
        }
    } else {
        for (dirIndex = 0; dirIndex < sizeof(theme_size_dirs) /
                 sizeof(theme_size_dirs[0]); ++dirIndex) {
            int logicalSize = theme_parseSize(theme_size_dirs[dirIndex]);
            size_t contextIndex;
            bool sizeFound = false;
            for (contextIndex = 0; contextIndex < sizeof(theme_context_dirs) /
                     sizeof(theme_context_dirs[0]); ++contextIndex) {
                for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
                         (const XContainer*)paths); ++pathIndex) {
                    const XString* rootStr = (const XString*)XStringList_at_base(
                        (const XVector*)paths, (int64_t)pathIndex);
                    const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
                    char dir[64];
                    snprintf(dir, sizeof(dir), "%s", theme_size_dirs[dirIndex]);
                    if (theme_dirHasIcon(root, theme, dir, name)) {
                        theme_appendSizeUnique(out, logicalSize);
                        found = true;
                        sizeFound = true;
                        break;
                    }
                }
                if (sizeFound) break;
            }
        }
    }
    parents = theme_parentsFor(&ctx, fallbackTheme);
    if (parents) {
        for (parentIndex = 0; parentIndex < (size_t)XStringList_size_base(
                 (const XContainer*)parents); ++parentIndex) {
            const XString* parentStr = (const XString*)XStringList_at_base(
                (const XVector*)parents, (int64_t)parentIndex);
            const char* parent = parentStr ? XString_toUtf8(parentStr) : NULL;
            if (parent && theme_collectSizes(paths, parent, name, fallbackTheme,
                                             visited, out))
                found = true;
        }
    }
    theme_stringListDelete(parents);
    theme_visitPop(visited);
    themeContext_deinit(&ctx);
    return found;
}

static bool theme_collectScalable(const XStringList* paths, const char* theme,
                                  const char* name, const char* fallbackTheme,
                                  ThemeVisitStack* visited)
{
    ThemeContext ctx;
    XStringList* parents = NULL;
    bool parsed = false;
    bool found = false;
    size_t pathIndex;
    size_t dirIndex;
    size_t parentIndex;
    if (!paths || !theme || !theme[0] || !name || !name[0] || !visited ||
        theme_visitContains(visited, theme))
        return false;
    theme_visitPush(visited, theme);
    themeContext_init(&ctx);
    for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
             (const XContainer*)paths); ++pathIndex) {
        const XString* rootStr = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
        if (!root || !root[0]) continue;
        if (!parsed && theme_parseIndexFile(root, theme, &ctx))
            parsed = true;
    }
    if (parsed) {
        for (dirIndex = 0; dirIndex < ctx.m_metaCount && !found; ++dirIndex) {
            const XString* dirStr = (const XString*)XStringList_at_base(
                (const XVector*)ctx.m_dirs, (int64_t)dirIndex);
            const char* dir = dirStr ? XString_toUtf8(dirStr) : NULL;
            if (ctx.m_meta[dirIndex].m_type != ThemeDir_Scalable ||
                !dir || !dir[0])
                continue;
            for (pathIndex = 0; pathIndex < (size_t)XStringList_size_base(
                     (const XContainer*)paths); ++pathIndex) {
                const XString* rootStr = (const XString*)XStringList_at_base(
                    (const XVector*)paths, (int64_t)pathIndex);
                const char* root = rootStr ? XString_toUtf8(rootStr) : NULL;
                if (theme_dirHasIcon(root, theme, dir, name)) {
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        parents = theme_parentsFor(&ctx, fallbackTheme);
        if (parents) {
            for (parentIndex = 0; parentIndex < (size_t)XStringList_size_base(
                     (const XContainer*)parents); ++parentIndex) {
                const XString* parentStr = (const XString*)XStringList_at_base(
                    (const XVector*)parents, (int64_t)parentIndex);
                const char* parent = parentStr ? XString_toUtf8(parentStr) : NULL;
                if (parent && theme_collectScalable(paths, parent, name,
                                                    fallbackTheme, visited)) {
                    found = true;
                    break;
                }
            }
        }
    }
    theme_stringListDelete(parents);
    theme_visitPop(visited);
    themeContext_deinit(&ctx);
    return found;
}

static bool theme_loadLegacy(const XStringList* paths, const char* theme,
                             const char* name, XPixmap* out)
{
    size_t pathIndex;
    bool indexedTheme = false;
    if (!paths) return false;
    if (theme && theme[0]) {
        /* QIconTheme 遍历全部 themeSearchPaths() 后，只要其中任一路径
           存在 index.theme，整个主题就按索引格式处理；不能因为另一
           个搜索根没有索引，就让其根目录旧式文件绕过 Directories。 */
        for (pathIndex = 0; pathIndex < XStringList_size_base(
                 (XContainer*)paths); ++pathIndex) {
            const XString* path = (const XString*)XStringList_at_base(
                (const XVector*)paths, (int64_t)pathIndex);
            const char* root = path ? XString_toUtf8(path) : NULL;
            char indexPath[1024];
            if (!root || !root[0]) continue;
            snprintf(indexPath, sizeof(indexPath), "%s/%s/index.theme",
                     root, theme);
            if (theme_fileExists(indexPath)) {
                indexedTheme = true;
                break;
            }
        }
        if (indexedTheme) return false;
    }
    for (pathIndex = 0; pathIndex < XStringList_size_base(
             (XContainer*)paths); ++pathIndex) {
        const XString* path = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = path ? XString_toUtf8(path) : NULL;
        size_t extIndex;
        char filePath[1024];
        if (!root || !root[0]) continue;
        for (extIndex = 0; extIndex < sizeof(theme_import_exts) /
                 sizeof(theme_import_exts[0]); ++extIndex) {
            const char* ext = theme_import_exts[extIndex];
            if (!theme_extAllowed(ext)) continue;
            if (theme && theme[0]) {
                theme_buildThemePath(filePath, sizeof(filePath), root, theme,
                                     name, ext);
            } else {
                theme_buildRootPath(filePath, sizeof(filePath), root, name, ext);
            }
            if (theme_loadFile(filePath, out)) return true;
        }
    }
    return false;
}

/* 对齐 Qt QIconLoader::lookupFallbackIcon：每个目录只选第一个命中的
 * png/xpm/svg 文件，目录顺序优先于后续目录。 */
static bool theme_loadFallback(const XStringList* paths, const char* name,
                               XPixmap* out)
{
    size_t pathIndex;
    if (!paths || !name || !name[0]) return false;
    for (pathIndex = 0; pathIndex < XStringList_size_base(
             (XContainer*)paths); ++pathIndex) {
        const XString* path = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = path ? XString_toUtf8(path) : NULL;
        size_t extIndex;
        char filePath[1024];
        if (!root || !root[0]) continue;
        for (extIndex = 0; extIndex < sizeof(theme_fallback_exts) /
                 sizeof(theme_fallback_exts[0]); ++extIndex) {
            const char* ext = theme_fallback_exts[extIndex];
            if (!theme_extAllowed(ext)) continue;
            theme_buildRootPath(filePath, sizeof(filePath), root, name, ext);
            if (!theme_fileExists(filePath)) continue;
            return theme_loadFile(filePath, out);
        }
    }
    return false;
}

/* 与 theme_loadFallback() 对应的登记查询，只检查文件目录项是否存在。 */
static bool theme_fallbackEntryExists(const XStringList* paths,
                                      const char* name)
{
    size_t pathIndex;
    if (!paths || !name || !name[0]) return false;
    for (pathIndex = 0; pathIndex < XStringList_size_base(
             (XContainer*)paths); ++pathIndex) {
        const XString* pathString = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = pathString ? XString_toUtf8(pathString) : NULL;
        size_t extIndex;
        char filePath[1024];
        if (!root || !root[0]) continue;
        for (extIndex = 0; extIndex < sizeof(theme_fallback_exts) /
                 sizeof(theme_fallback_exts[0]); ++extIndex) {
            const char* ext = theme_fallback_exts[extIndex];
            if (!theme_extAllowed(ext)) continue;
            if (snprintf(filePath, sizeof(filePath), "%s/%s%s", root,
                         name, ext) >= 0 && theme_fileExists(filePath))
                return true;
        }
    }
    return false;
}

static bool theme_collectFallbackSizes(const XStringList* paths,
                                       const char* name, XVector* out)
{
    size_t pathIndex;
    if (!paths || !name || !name[0] || !out) return false;
    for (pathIndex = 0; pathIndex < XStringList_size_base(
             (XContainer*)paths); ++pathIndex) {
        const XString* path = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = path ? XString_toUtf8(path) : NULL;
        size_t extIndex;
        char filePath[1024];
        if (!root || !root[0]) continue;
        for (extIndex = 0; extIndex < sizeof(theme_fallback_exts) /
                 sizeof(theme_fallback_exts[0]); ++extIndex) {
            const char* ext = theme_fallback_exts[extIndex];
            XPixmap pixmap;
            XSize size;
            size_t sizeIndex;
            bool duplicate = false;
            if (!theme_extAllowed(ext)) continue;
            theme_buildRootPath(filePath, sizeof(filePath), root, name, ext);
            if (!theme_fileExists(filePath)) continue;
            XPixmap_init(&pixmap);
            if (!theme_loadFile(filePath, &pixmap)) {
                XPixmap_deinit_base(&pixmap);
                return false;
            }
            size.width = XPixmap_width(&pixmap);
            size.height = XPixmap_height(&pixmap);
            if (size.width > 0 && size.height > 0) {
                for (sizeIndex = 0; sizeIndex < XVector_size_base(
                         (const XContainer*)out); ++sizeIndex) {
                    const XSize* existing = (const XSize*)XVector_at_base(
                        out, (int64_t)sizeIndex);
                    if (existing && existing->width == size.width &&
                        existing->height == size.height) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate) XVector_push_back_1_base(out, &size);
            }
            XPixmap_deinit_base(&pixmap);
            /* lookupFallbackIcon() stops at the first existing file. */
            return size.width > 0 && size.height > 0;
        }
    }
    return false;
}

static bool theme_resolveThemePixmapSizeInternal(const char* name, int size,
                                                 int iconScale, int outputSize,
                                                 XPixmap* out,
                                                 bool scaleToSize)
{
    XStringList* themePaths = NULL;
    XStringList* fallbackPaths = NULL;
    XString* themeName = NULL;
    XString* fallbackThemeName = NULL;
    const char* themeUtf8 = NULL;
    const char* fallbackUtf8 = NULL;
    XPixmap best;
    ThemeVisitStack visited;
    bool any = false;
    bool found;
    if (!name || !name[0] || !out) return false;
    if (size <= 0) size = 48;
    if (iconScale <= 0) iconScale = 1;
    if (outputSize <= 0) outputSize = size;
    XPixmap_init(out);

    /* QIcon::fromTheme() only treats an absolute path as a file icon.  This
       includes Unix paths and Qt resource paths beginning with ":/".  A
       relative name such as "./foo" or "dir/foo" remains a theme name;
       resolving it from the process working directory would also make
       hasThemeIcon() report standalone files as theme entries. */
    if (name[0] == '/' || name[0] == ':') {
        if (theme_loadFile(name, out)) {
            if (scaleToSize)
                theme_scaledToSize(out, outputSize);
            return true;
        }
        return false;
    }

    themePaths = XIcon_themeSearchPaths_2();
    fallbackPaths = XIcon_fallbackSearchPaths_2();
    themeName = XIcon_themeName();
    fallbackThemeName = XIcon_fallbackThemeName();
    if (themeName && !XString_isEmpty_base((const XContainer*)themeName))
        themeUtf8 = XString_toUtf8(themeName);
    if (fallbackThemeName && !XString_isEmpty_base(
            (const XContainer*)fallbackThemeName))
        fallbackUtf8 = XString_toUtf8(fallbackThemeName);

    memset(&visited, 0, sizeof(visited));
    XPixmap_init(&best);

    if (themeUtf8) {
        any = theme_searchTheme(themePaths, themeUtf8, name, size, iconScale,
                                fallbackUtf8, &visited, false, true,
                                &best) || any;
    }
    if (!any && fallbackUtf8) {
        /* Qt QIconLoader 用 themeSearchPaths() 查找 fallbackThemeName；
           fallbackSearchPaths() 仅用于最终的独立图标文件回退。 */
        any = theme_searchTheme(themePaths, fallbackUtf8, name, size,
                                iconScale,
                                NULL, &visited, false, true, &best) || any;
    }
    if (!any && themeUtf8) {
        any = theme_loadLegacy(themePaths, themeUtf8, name, &best);
    }
    if (!any && fallbackUtf8) {
        /* Qt QIconLoader 对 fallbackThemeName 仍在 themeSearchPaths()
           中查找；fallbackSearchPaths() 只用于无主题名的独立文件回退。 */
        any = theme_loadLegacy(themePaths, fallbackUtf8, name, &best);
    }
    if (!any) {
        /* Qt QIconLoader::lookupFallbackIcon() 只扫描
           fallbackSearchPaths()，不把 themeSearchPaths() 当作独立文件
           目录；这样主题路径中的散落文件不会错误地抢占回退主题。 */
        any = theme_loadFallback(fallbackPaths, name, &best);
    }

    found = any && !XPixmap_isNull(&best);
    if (found) {
        if (scaleToSize)
            theme_scaledToSize(&best, outputSize);
        if (out) {
            XPixmap_move_base(out, &best);
            XPixmap_deinit_base(&best);
        }
    } else {
        XPixmap_deinit_base(&best);
    }
    if (themeName) XString_delete_base((XClass*)themeName);
    if (fallbackThemeName) XString_delete_base((XClass*)fallbackThemeName);
    theme_stringListDelete(themePaths);
    theme_stringListDelete(fallbackPaths);
    return found;
}

bool XIconInternal_resolveThemePixmapSize(const char* name, int size,
                                          XPixmap* out)
{
    return theme_resolveThemePixmapSizeInternal(name, size, 1, size, out, true);
}

bool XIconInternal_resolveThemePixmapSizeScale(const char* name, int size,
                                               int iconScale, int outputSize,
                                               XPixmap* out)
{
    return theme_resolveThemePixmapSizeInternal(name, size, iconScale,
                                                outputSize, out, true);
}

bool XIconInternal_resolveThemePixmapSourceSize(const char* name, int size,
                                                XPixmap* out)
{
    return theme_resolveThemePixmapSizeInternal(name, size, 1, size, out,
                                                false);
}

bool XIconInternal_resolveThemePixmap(const char* name, XPixmap* out)
{
    return XIconInternal_resolveThemePixmapSize(name, 48, out);
}

bool XIconInternal_availableThemeSizes(const char* name, XVector* out)
{
    XStringList* themePaths = NULL;
    XStringList* fallbackPaths = NULL;
    XString* themeName = NULL;
    XString* fallbackThemeName = NULL;
    const char* themeUtf8 = NULL;
    const char* fallbackUtf8 = NULL;
    ThemeVisitStack visited;
    bool found = false;
    if (!name || !name[0] || !out) return false;

    themePaths = XIcon_themeSearchPaths_2();
    fallbackPaths = XIcon_fallbackSearchPaths_2();
    themeName = XIcon_themeName();
    fallbackThemeName = XIcon_fallbackThemeName();
    if (themeName && !XString_isEmpty_base((const XContainer*)themeName))
        themeUtf8 = XString_toUtf8(themeName);
    if (fallbackThemeName && !XString_isEmpty_base(
            (const XContainer*)fallbackThemeName))
        fallbackUtf8 = XString_toUtf8(fallbackThemeName);

    memset(&visited, 0, sizeof(visited));
    if (themeUtf8)
        found = theme_collectSizes(themePaths, themeUtf8, name, fallbackUtf8,
                                   &visited, out) || found;
    memset(&visited, 0, sizeof(visited));
    if (fallbackUtf8)
        found = theme_collectSizes(themePaths, fallbackUtf8, name, NULL,
                                   &visited, out) || found;
    if (!found) {
        /* Qt QIconLoaderEngine::availableSizes() 对 Fallback 条目委托给
           QIcon(file).availableSizes()；独立 PNG/XPM/SVG 的原始矩形尺寸
           因而必须保留，不能按主题目录一律返回 size x size。 */
        found = theme_collectFallbackSizes(fallbackPaths, name, out);
    }

    if (themeName) XString_delete_base((XClass*)themeName);
    if (fallbackThemeName) XString_delete_base((XClass*)fallbackThemeName);
    theme_stringListDelete(themePaths);
    theme_stringListDelete(fallbackPaths);
    return found;
}

bool XIconInternal_themeHasScalable(const char* name)
{
    XStringList* themePaths = NULL;
    XStringList* fallbackPaths = NULL;
    XString* themeName = NULL;
    XString* fallbackThemeName = NULL;
    const char* themeUtf8 = NULL;
    const char* fallbackUtf8 = NULL;
    ThemeVisitStack visited;
    bool found = false;
    if (!name || !name[0]) return false;

    themePaths = XIcon_themeSearchPaths_2();
    fallbackPaths = XIcon_fallbackSearchPaths_2();
    themeName = XIcon_themeName();
    fallbackThemeName = XIcon_fallbackThemeName();
    if (themeName && !XString_isEmpty_base((const XContainer*)themeName))
        themeUtf8 = XString_toUtf8(themeName);
    if (fallbackThemeName && !XString_isEmpty_base(
            (const XContainer*)fallbackThemeName))
        fallbackUtf8 = XString_toUtf8(fallbackThemeName);

    memset(&visited, 0, sizeof(visited));
    if (themeUtf8)
        found = theme_collectScalable(themePaths, themeUtf8, name,
                                      fallbackUtf8, &visited);
    if (!found && fallbackUtf8) {
        memset(&visited, 0, sizeof(visited));
        found = theme_collectScalable(themePaths, fallbackUtf8, name,
                                      NULL, &visited);
    }

    if (themeName) XString_delete_base((XClass*)themeName);
    if (fallbackThemeName) XString_delete_base((XClass*)fallbackThemeName);
    theme_stringListDelete(themePaths);
    theme_stringListDelete(fallbackPaths);
    return found;
}

bool XIconInternal_themeHasIcon(const char* name)
{
    XStringList* themePaths = NULL;
    XStringList* fallbackPaths = NULL;
    XString* themeName = NULL;
    XString* fallbackThemeName = NULL;
    const char* themeUtf8 = NULL;
    const char* fallbackUtf8 = NULL;
    ThemeVisitStack visited;
    bool found = false;
    if (!name || !name[0]) return false;

    themePaths = XIcon_themeSearchPaths_2();
    fallbackPaths = XIcon_fallbackSearchPaths_2();
    themeName = XIcon_themeName();
    fallbackThemeName = XIcon_fallbackThemeName();
    if (themeName && !XString_isEmpty_base((const XContainer*)themeName))
        themeUtf8 = XString_toUtf8(themeName);
    if (fallbackThemeName && !XString_isEmpty_base(
            (const XContainer*)fallbackThemeName))
        fallbackUtf8 = XString_toUtf8(fallbackThemeName);

    memset(&visited, 0, sizeof(visited));
    if (themeUtf8)
        found = theme_searchThemeExists(themePaths, themeUtf8, name, 48, 1,
                                        fallbackUtf8, &visited, false, true);
    if (!found && fallbackUtf8) {
        memset(&visited, 0, sizeof(visited));
        found = theme_searchThemeExists(themePaths, fallbackUtf8, name, 48, 1,
                                        NULL, &visited, false, true);
    }
    if (!found)
        found = theme_fallbackEntryExists(fallbackPaths, name);

    if (themeName) XString_delete_base((XClass*)themeName);
    if (fallbackThemeName) XString_delete_base((XClass*)fallbackThemeName);
    theme_stringListDelete(themePaths);
    theme_stringListDelete(fallbackPaths);
    return found;
}
