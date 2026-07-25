#include "XZipWriter.h"
#include "XMemory.h"
#include "XFile.h"
#include "XClass.h"
#include "zlib.h"
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
    crc = crc32(crc, data, (uInt)size);
    return (uint32_t)crc;
}

/* 辅助：使用 zlib deflate 压缩数据 */
static uint8_t* compress_data(const uint8_t* input, size_t input_size, size_t* output_size) {
    if (input_size == 0) {
        *output_size = 0;
        return NULL;
    }
    uLongf dest_len = compressBound((uLong)input_size);
    uint8_t* compressed = (uint8_t*)XMalloc_System((size_t)dest_len);
    if (!compressed) return NULL;
    if (compress2(compressed, &dest_len, input, (uLong)input_size, Z_DEFAULT_COMPRESSION) != Z_OK) {
        XFree_System(compressed);
        return NULL;
    }
    *output_size = (size_t)dest_len;
    return compressed;
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
    return self;
}

void XZipWriter_delete(XZipWriter* self) {
    if (!self) return;
    if (self->m_fileName) { XString_deinit_base(self->m_fileName); XFree_System(self->m_fileName); }
    if (self->m_entries) {
        size_t count = XVector_size_base(self->m_entries);
        for (size_t i = 0; i < count; i++) {
            ZipFileEntry* entry = (ZipFileEntry*)XVector_at_base(self->m_entries, i);
            if (entry->m_path) XFree_System(entry->m_path);
            if (entry->m_data) XFree_System(entry->m_data);
            if (entry->m_compressed) XFree_System(entry->m_compressed);
        }
        XVector_deinit_base(self->m_entries);
        XFree_System(self->m_entries);
    }
    XFree_System(self);
}

bool XZipWriter_addDirectory(XZipWriter* self, const XString* path) {
    if (!self || !path) return false;
    const char* pathUtf8 = XString_toUtf8(path);
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
    XVector_push_back_1_base(self->m_entries, &entry);
    return true;
}

bool XZipWriter_addFile(XZipWriter* self, const XString* path, const uint8_t* data, size_t size) {
    if (!self || !path || !data) return false;
    const char* pathUtf8 = XString_toUtf8(path);
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
    entry.m_compressed = compress_data(data, size, &entry.m_compressed_size);
    XVector_push_back_1_base(self->m_entries, &entry);
    return true;
}

bool XZipWriter_close(XZipWriter* self) {
    if (!self || !self->m_fileName) return false;

    XFile* file = XFile_create_2(self->m_fileName);
    if (!file) return false;
    if (!XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        XClass_delete_base((XClass*)file);
        return false;
    }

    size_t entry_count = XVector_size_base(self->m_entries);
    /* 用于记录每个 entry 的偏移量 */
    uint32_t* offsets = (uint32_t*)XMalloc_System(sizeof(uint32_t) * (entry_count ? entry_count : 1));
    if (!offsets) {
        XIODevice_close_base((XIODevice*)file);
        XClass_delete_base((XClass*)file);
        return false;
    }

    /* 写入本地文件头 + 数据 */
    for (size_t i = 0; i < entry_count; i++) {
        ZipFileEntry* entry = (ZipFileEntry*)XVector_at_base(self->m_entries, i);
        offsets[i] = (uint32_t)XIODevice_pos_base((XIODevice*)file);

        size_t name_len = strlen(entry->m_path);
        uint8_t header[30];
        memset(header, 0, 30);
        write_le32(header, ZIP_LOCAL_FILE_HEADER_SIG);
        write_le16(header + 4, 20);   /* 版本 2.0 */
        write_le16(header + 6, 0);    /* 通用位标志 */
        write_le16(header + 8, entry->m_is_directory ? 0 : 8); /* 压缩方式：0=存储, 8=deflate */
        write_le16(header + 10, 0);   /* 最后修改时间 */
        write_le16(header + 12, 0);   /* 最后修改日期 */
        write_le32(header + 14, entry->m_crc32);
        write_le32(header + 18, entry->m_is_directory ? 0 : (uint32_t)entry->m_compressed_size);
        write_le32(header + 22, entry->m_is_directory ? 0 : (uint32_t)entry->m_size);
        write_le16(header + 26, (uint16_t)name_len);
        write_le16(header + 28, 0);   /* 额外字段长度 */

        XIODevice_write_1((XIODevice*)file, (const char*)header, 30);
        XIODevice_write_1((XIODevice*)file, entry->m_path, (int64_t)name_len);

        /* 写入压缩数据 */
        if (!entry->m_is_directory && entry->m_compressed && entry->m_compressed_size > 0) {
            XIODevice_write_1((XIODevice*)file, (const char*)entry->m_compressed, (int64_t)entry->m_compressed_size);
        } else if (!entry->m_is_directory && entry->m_data && entry->m_size > 0) {
            /* 存储模式 */
            XIODevice_write_1((XIODevice*)file, (const char*)entry->m_data, (int64_t)entry->m_size);
        }
    }

    /* 写入中央目录 */
    uint32_t central_dir_offset = (uint32_t)XIODevice_pos_base((XIODevice*)file);
    uint16_t central_dir_count = 0;

    for (size_t i = 0; i < entry_count; i++) {
        ZipFileEntry* entry = (ZipFileEntry*)XVector_at_base(self->m_entries, i);
        size_t name_len = strlen(entry->m_path);
        uint8_t cd_header[46];
        memset(cd_header, 0, 46);
        write_le32(cd_header, ZIP_CENTRAL_DIR_SIG);
        write_le16(cd_header + 4, 20);    /* 创建版本 */
        write_le16(cd_header + 6, 20);    /* 提取版本 */
        write_le16(cd_header + 8, 0);     /* 通用位标志 */
        write_le16(cd_header + 10, entry->m_is_directory ? 0 : 8);
        write_le16(cd_header + 12, 0);    /* 最后修改时间 */
        write_le16(cd_header + 14, 0);    /* 最后修改日期 */
        write_le32(cd_header + 16, entry->m_crc32);
        write_le32(cd_header + 20, entry->m_is_directory ? 0 : (uint32_t)entry->m_compressed_size);
        write_le32(cd_header + 24, entry->m_is_directory ? 0 : (uint32_t)entry->m_size);
        write_le16(cd_header + 28, (uint16_t)name_len);
        write_le16(cd_header + 30, 0);    /* 额外字段长度 */
        write_le16(cd_header + 32, 0);    /* 注释长度 */
        write_le16(cd_header + 34, 0);    /* 磁盘号 */
        write_le16(cd_header + 36, 0);    /* 内部属性 */
        write_le32(cd_header + 38, 0);    /* 外部属性 */
        write_le32(cd_header + 42, offsets[i]);

        XIODevice_write_1((XIODevice*)file, (const char*)cd_header, 46);
        XIODevice_write_1((XIODevice*)file, entry->m_path, (int64_t)name_len);
        central_dir_count++;
    }

    /* 写入结束中央目录记录 */
    uint32_t central_dir_size = (uint32_t)(XIODevice_pos_base((XIODevice*)file) - central_dir_offset);
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
    XIODevice_write_1((XIODevice*)file, (const char*)eocd, 22);

    XFree_System(offsets);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    return true;
}
