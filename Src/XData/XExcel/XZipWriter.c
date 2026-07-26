#include "XZipWriter.h"
#include <stdio.h>
#include "XMemory.h"
#include "XFile.h"
#include "XClass.h"
#include "zlib.h"
#include <limits.h>
#include <string.h>

/* ZIP 文件本地文件头标记 */
#define ZIP_LOCAL_FILE_HEADER_SIG  0x04034b50
#define ZIP_CENTRAL_DIR_SIG        0x02014b50
#define ZIP_END_CENTRAL_DIR_SIG    0x06054b50

/* 辅助：写入小端 uint16 */
static void write_le16(uint8_t* buf, uint16_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
}

/* 辅助：写入小端 uint32 */
static void write_le32(uint8_t* buf, uint32_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

/* 辅助：CRC32 计算 */
static uint32_t calc_crc32(const uint8_t* data, size_t size) {
    uLong crc = crc32(0L, Z_NULL, 0);
    while (size > 0) {
        uInt chunk = size > UINT_MAX ? UINT_MAX : (uInt)size;
        crc = crc32(crc, data, chunk);
        data += chunk;
        size -= chunk;
    }
    return (uint32_t)crc;
}

/*
 * 辅助：使用 zlib deflate 压缩数据，返回 ZIP 兼容的 RAW DEFLATE 流
 * @note ZIP 规范（PKWARE APPNOTE）中 method=8 表示 "deflate"，约定是
 *       RFC 1951 原始 DEFLATE 流（不带 zlib 头尾），而 compress2()
 *       输出的是 RFC 1950 zlib 流（带 2 字节头 + 4 字节 Adler-32 尾）。
 *       直接把 zlib 流塞进 ZIP 会被读取方（zlib.inflateInit2(..., -15)）
 *       报 "invalid stored block lengths"。这里压缩后裁掉头尾并重算
 *       Adler-32 已被 ZIP 自己的 CRC32 覆盖，所以可以放心丢弃。
 */
static uint8_t* compress_data(const uint8_t* input, size_t input_size, size_t* output_size) {
    if (input_size == 0) {
        *output_size = 0;
        return NULL;
    }
    uLongf zlib_len = compressBound((uLong)input_size);
    uint8_t* zlib_buf = (uint8_t*)XMalloc_System((size_t)zlib_len);
    if (!zlib_buf) return NULL;
    if (compress2(zlib_buf, &zlib_len, input, (uLong)input_size, Z_DEFAULT_COMPRESSION) != Z_OK) {
        XFree_System(zlib_buf);
        return NULL;
    }
    /*
     * 丢弃 zlib 头（2 字节：CMF + FLG）和 zlib 校验尾（4 字节 Adler-32）。
     * ZIP 的 method=8 期望的就是中间这段原始 DEFLATE。
     * 极端情况下 zlib_len < 6 的防御处理（极短数据几乎不会被压缩）。
     */
    if (zlib_len <= 6) {
        *output_size = (size_t)zlib_len;
        return zlib_buf;
    }
    size_t raw_len = (size_t)zlib_len - 6;
    uint8_t* raw = (uint8_t*)XMalloc_System(raw_len ? raw_len : 1);
    if (!raw) {
        XFree_System(zlib_buf);
        return NULL;
    }
    memcpy(raw, zlib_buf + 2, raw_len);
    XFree_System(zlib_buf);
    *output_size = raw_len;
    return raw;
}

/* 内部结构：记录待写入的文件信息 */
typedef struct ZipFileEntry {
    char* m_path;
    uint8_t* m_data;
    size_t m_size;
    uint32_t m_crc32;
    uint8_t* m_compressed;
    size_t m_compressed_size;
    int m_is_directory;
    uint16_t m_method;
} ZipFileEntry;

XZipWriter* XZipWriter_create(const XString* fileName) {
    XZipWriter* self = (XZipWriter*)XMalloc_System(sizeof(XZipWriter));
    if (!self) return NULL;
    memset(self, 0, sizeof(XZipWriter));
    if (fileName) {
        self->m_fileName = XString_create();
        if (self->m_fileName) XString_append(self->m_fileName, fileName);
    }
    self->m_entries = XVector_Create(ZipFileEntry);
    if (!self->m_entries || (fileName && !self->m_fileName)) {
        self->m_closeAttempted = true;
        XZipWriter_delete(self);
        return NULL;
    }
    return self;
}

XZipWriter* XZipWriter_createForDevice(XIODevice* device) {
    if (!device) return NULL;
    XZipWriter* self = XZipWriter_create(NULL);
    if (self) self->m_zipHandle = device;
    return self;
}

static bool write_bytes(XIODevice* device, const void* data, size_t size) {
    const char* bytes = (const char*)data;
    size_t written = 0;
    while (written < size) {
        int64_t result = XIODevice_write_1(device, bytes + written, (int64_t)(size - written));
        if (result <= 0) return false;
        written += (size_t)result;
    }
    return true;
}

void XZipWriter_delete(XZipWriter* self) {
    if (!self) return;
    if (!self->m_closeAttempted && (self->m_fileName || self->m_zipHandle))
        XZipWriter_close(self);
    if (self->m_fileName) XString_delete_base(self->m_fileName);
    if (self->m_entries) {
        size_t count = XVector_size_base(self->m_entries);
        for (size_t i = 0; i < count; i++) {
            ZipFileEntry* entry = (ZipFileEntry*)XVector_at_base(self->m_entries, i);
            if (entry->m_path) XFree_System(entry->m_path);
            if (entry->m_data) XFree_System(entry->m_data);
            if (entry->m_compressed) XFree_System(entry->m_compressed);
        }
        XVector_delete_base(self->m_entries);
    }
    XFree_System(self);
}

bool XZipWriter_addDirectory(XZipWriter* self, const XString* path) {
    if (!self || !self->m_entries || self->m_closeAttempted || !path) return false;
    const char* pathUtf8 = XString_toUtf8(path);
    if (!pathUtf8 || !pathUtf8[0] || strlen(pathUtf8) + 1 > UINT16_MAX) return false;
    ZipFileEntry entry;
    memset(&entry, 0, sizeof(entry));
    size_t len = strlen(pathUtf8);
    entry.m_path = (char*)XMalloc_System(len + 2);
    if (!entry.m_path) return false;
    memcpy(entry.m_path, pathUtf8, len);
    /* 确保目录名以 / 结尾 */
    if (len == 0 || pathUtf8[len-1] != '/') {
        entry.m_path[len] = '/';
        entry.m_path[len+1] = '\0';
    } else {
        entry.m_path[len] = '\0';
    }
    entry.m_is_directory = 1;
    entry.m_data = NULL;
    entry.m_size = 0;
    entry.m_crc32 = 0;
    entry.m_compressed = NULL;
    entry.m_compressed_size = 0;
    entry.m_method = 0;
    if (!XVector_push_back_2((XVector*)self->m_entries, &entry, 1)) {
        XFree_System(entry.m_path);
        return false;
    }
    return true;
}

bool XZipWriter_addFile(XZipWriter* self, const XString* path, const uint8_t* data, size_t size) {
    if (!self || !self->m_entries || self->m_closeAttempted || !path ||
        (!data && size > 0) || size > UINT32_MAX) return false;
    const char* pathUtf8 = XString_toUtf8(path);
    if (!pathUtf8 || !pathUtf8[0] || strlen(pathUtf8) > UINT16_MAX) return false;
    for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_entries); ++i) {
        ZipFileEntry* existing = (ZipFileEntry*)XVector_at_base((XVector*)self->m_entries, i);
        if (existing && existing->m_path &&
            XString_equals_utf8(path, existing->m_path, XChar_CaseSensitive))
            return false;
    }
    ZipFileEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.m_path = (char*)XMalloc_System(strlen(pathUtf8) + 1);
    if (!entry.m_path) return false;
    strcpy(entry.m_path, pathUtf8);
    entry.m_is_directory = 0;
    entry.m_size = size;
    entry.m_data = (uint8_t*)XMalloc_System(size ? size : 1);
    if (!entry.m_data && size > 0) { XFree_System(entry.m_path); return false; }
    if (size > 0) memcpy(entry.m_data, data, size);
    entry.m_crc32 = calc_crc32(data, size);
    if (size > 0) {
        entry.m_compressed = compress_data(data, size, &entry.m_compressed_size);
        if (!entry.m_compressed) {
            XFree_System(entry.m_data);
            XFree_System(entry.m_path);
            return false;
        }
        entry.m_method = 8;
    } else {
        entry.m_method = 0;
    }
    if (!XVector_push_back_2((XVector*)self->m_entries, &entry, 1)) {
        if (entry.m_compressed) XFree_System(entry.m_compressed);
        if (entry.m_data) XFree_System(entry.m_data);
        XFree_System(entry.m_path);
        return false;
    }
    return true;
}

bool XZipWriter_close(XZipWriter* self) {
    if (!self || (!self->m_fileName && !self->m_zipHandle)) return false;
    if (self->m_closed) return true;
    if (self->m_closeAttempted) return false;
    self->m_closeAttempted = true;

    XFile* file = NULL;
    XIODevice* device = (XIODevice*)self->m_zipHandle;
    bool openedHere = false;
    if (!device) {
        file = XFile_create_2(self->m_fileName);
        if (!file) return false;
        device = (XIODevice*)file;
    }
    if (!XIODevice_isOpen(device)) {
        if (!XIODevice_open_base(device, XIODevice_WriteOnly | XIODevice_Truncate)) {
            if (file) XClass_delete_base((XClass*)file);
            return false;
        }
        openedHere = true;
    } else if (!XIODevice_isWritable(device)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }

    size_t entry_count = XVector_size_base((XContainer*)self->m_entries);
    if (entry_count > UINT16_MAX) goto open_failed;
    /* 用于记录每个 entry 的偏移量 */
    uint32_t* offsets = (uint32_t*)XMalloc_System(sizeof(uint32_t) * (entry_count ? entry_count : 1));
    if (!offsets) {
        if (openedHere) XIODevice_close_base(device);
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }

    /* 写入本地文件头 + 数据 */
    for (size_t i = 0; i < entry_count; i++) {
        ZipFileEntry* entry = (ZipFileEntry*)XVector_at_base((XVector*)self->m_entries, i);
        int64_t position = XIODevice_pos_base(device);
        if (position < 0 || (uint64_t)position > UINT32_MAX) goto write_failed;
        offsets[i] = (uint32_t)position;

        size_t name_len = strlen(entry->m_path);
        uint8_t header[30];
        memset(header, 0, 30);
        write_le32(header, ZIP_LOCAL_FILE_HEADER_SIG);
        write_le16(header + 4, 20);   /* 版本 2.0 */
        write_le16(header + 6, 0x0800); /* 文件名使用 UTF-8 */
        write_le16(header + 8, entry->m_method); /* 0=存储, 8=deflate */
        write_le16(header + 10, 0);   /* 最后修改时间 */
        write_le16(header + 12, 0);   /* 最后修改日期 */
        write_le32(header + 14, entry->m_crc32);
        write_le32(header + 18, entry->m_method == 8
            ? (uint32_t)entry->m_compressed_size : (uint32_t)entry->m_size);
        write_le32(header + 22, entry->m_is_directory ? 0 : (uint32_t)entry->m_size);
        write_le16(header + 26, (uint16_t)name_len);
        write_le16(header + 28, 0);   /* 额外字段长度 */

        if (!write_bytes(device, header, 30) || !write_bytes(device, entry->m_path, name_len))
            goto write_failed;

        /* 写入压缩数据 */
        if (!entry->m_is_directory && entry->m_method == 8) {
            if (!write_bytes(device, entry->m_compressed, entry->m_compressed_size)) goto write_failed;
        } else if (!entry->m_is_directory && entry->m_data && entry->m_size > 0) {
            /* 存储模式 */
            if (!write_bytes(device, entry->m_data, entry->m_size)) goto write_failed;
        }
    }

    /* 写入中央目录 */
    int64_t centralPosition = XIODevice_pos_base(device);
    if (centralPosition < 0 || (uint64_t)centralPosition > UINT32_MAX) goto write_failed;
    uint32_t central_dir_offset = (uint32_t)centralPosition;
    uint16_t central_dir_count = 0;

    for (size_t i = 0; i < entry_count; i++) {
        ZipFileEntry* entry = (ZipFileEntry*)XVector_at_base((XVector*)self->m_entries, i);
        size_t name_len = strlen(entry->m_path);
        uint8_t cd_header[46];
        memset(cd_header, 0, 46);
        write_le32(cd_header, ZIP_CENTRAL_DIR_SIG);
        write_le16(cd_header + 4, 20);    /* 创建版本 */
        write_le16(cd_header + 6, 20);    /* 提取版本 */
        write_le16(cd_header + 8, 0x0800); /* 文件名使用 UTF-8 */
        write_le16(cd_header + 10, entry->m_method);
        write_le16(cd_header + 12, 0);    /* 最后修改时间 */
        write_le16(cd_header + 14, 0);    /* 最后修改日期 */
        write_le32(cd_header + 16, entry->m_crc32);
        write_le32(cd_header + 20, entry->m_method == 8
            ? (uint32_t)entry->m_compressed_size : (uint32_t)entry->m_size);
        write_le32(cd_header + 24, entry->m_is_directory ? 0 : (uint32_t)entry->m_size);
        write_le16(cd_header + 28, (uint16_t)name_len);
        write_le16(cd_header + 30, 0);    /* 额外字段长度 */
        write_le16(cd_header + 32, 0);    /* 注释长度 */
        write_le16(cd_header + 34, 0);    /* 磁盘号 */
        write_le16(cd_header + 36, 0);    /* 内部属性 */
        write_le32(cd_header + 38, 0);    /* 外部属性 */
        write_le32(cd_header + 42, offsets[i]);

        if (!write_bytes(device, cd_header, 46) || !write_bytes(device, entry->m_path, name_len))
            goto write_failed;
        central_dir_count++;
    }

    /* 写入结束中央目录记录 */
    int64_t endPosition = XIODevice_pos_base(device);
    if (endPosition < central_dir_offset || (uint64_t)(endPosition - central_dir_offset) > UINT32_MAX)
        goto write_failed;
    uint32_t central_dir_size = (uint32_t)(endPosition - central_dir_offset);
    uint8_t eocd[22];
    memset(eocd, 0, 22);
    write_le32(eocd, ZIP_END_CENTRAL_DIR_SIG);
    write_le16(eocd + 4, 0);              /* 磁盘号 */
    write_le16(eocd + 6, 0);              /* 中央目录起始磁盘 */
    write_le16(eocd + 8, central_dir_count);  /* 当前磁盘上的条目数 */
    write_le16(eocd + 10, central_dir_count); /* 总条目数 */
    write_le32(eocd + 12, central_dir_size);
    write_le32(eocd + 16, central_dir_offset);
    write_le16(eocd + 20, 0);             /* 注释长度 */
    if (!write_bytes(device, eocd, 22)) goto write_failed;

    XFree_System(offsets);
    if (openedHere) XIODevice_close_base(device);
    if (file) XClass_delete_base((XClass*)file);
    self->m_closed = true;
    return true;

write_failed:
    XFree_System(offsets);
    if (openedHere) XIODevice_close_base(device);
    if (file) XClass_delete_base((XClass*)file);
    return false;

open_failed:
    if (openedHere) XIODevice_close_base(device);
    if (file) XClass_delete_base((XClass*)file);
    return false;
}
