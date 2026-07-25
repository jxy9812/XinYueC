#include "XZipReader.h"
#include "XMemory.h"
#include "XFile.h"
#include "zlib.h"
#include <string.h>

/* 解压缩 DEFLATE 数据 */
static uint8_t* inflate_data(const uint8_t* compressed, size_t compSize, size_t* outSize) {
    if (!compressed || compSize == 0 || !outSize) return NULL;
    *outSize = 0;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -15) != Z_OK) return NULL;

    strm.next_in = (Bytef*)compressed;
    strm.avail_in = compSize;

    size_t bufSize = compSize * 8 + 4096;
    uint8_t* out = (uint8_t*)XMalloc_System(bufSize);
    if (!out) { inflateEnd(&strm); return NULL; }

    strm.next_out = out;
    strm.avail_out = bufSize;

    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (ret == Z_STREAM_END) {
        *outSize = strm.total_out;
        return out;
    }

    XFree_System(out);
    return NULL;
}

/* 查找中央目录偏移（通过 XFile 读取） */
static size_t find_central_dir_offset(XIODevice* dev, int64_t fileSize) {
    size_t readSize = (fileSize < 65558) ? (size_t)fileSize : 65558;
    XIODevice_seek_base(dev, fileSize - (int64_t)readSize);

    uint8_t* buf = (uint8_t*)XMalloc_System(readSize);
    if (!buf) return 0;

    int64_t got = XIODevice_read_1(dev, (char*)buf, (int64_t)readSize);
    if (got < (int64_t)readSize) { XFree_System(buf); return 0; }

    for (long i = (long)readSize - 22; i >= 0; i--) {
        if (buf[i] == 0x50 && buf[i+1] == 0x4b &&
            buf[i+2] == 0x05 && buf[i+3] == 0x06) {
            uint32_t offset = *(uint32_t*)(buf + i + 16);
            XFree_System(buf);
            return offset;
        }
    }

    XFree_System(buf);
    return 0;
}

XZipReader* XZipReader_create(const XString* fileName) {
    XZipReader* self = (XZipReader*)XMalloc_System(sizeof(XZipReader));
    if (!self) return NULL;
    memset(self, 0, sizeof(XZipReader));
    if (fileName) {
        self->m_fileName = XString_create();
        if (self->m_fileName) XString_append(self->m_fileName, fileName);
    }
    self->m_filePaths = XStringList_create();
    self->m_zipHandle = NULL;
    return self;
}

XZipReader* XZipReader_createFromData(const uint8_t* data, size_t size) {
    (void)data; (void)size;
    return XZipReader_create(NULL);
}

void XZipReader_delete(XZipReader* self) {
    if (!self) return;
    if (self->m_fileName) { XString_delete_base(self->m_fileName); }
    if (self->m_filePaths) XStringList_delete_base(self->m_filePaths);
    XFree_System(self);
}

bool XZipReader_exists(const XZipReader* self) {
    if (!self || !self->m_fileName) return false;
    return XFile_exists_static(self->m_fileName);
}

XStringList* XZipReader_filePaths(const XZipReader* self) {
    if (!self || !self->m_fileName) return self ? self->m_filePaths : NULL;

    /* 如果已经有文件列表就直接返回 */
    if (XStringList_size_base(self->m_filePaths) > 0) {
        return self->m_filePaths;
    }

    XFile* file = XFile_create_2(self->m_fileName);
    if (!file) return self->m_filePaths;
    if (!XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        XFile_deleteLater(file);
        return self->m_filePaths;
    }

    int64_t fileSize = XIODevice_size_base((XIODevice*)file);

    /* 查找中央目录 */
    size_t centralOffset = find_central_dir_offset((XIODevice*)file, fileSize);
    if (centralOffset == 0) {
        XIODevice_close_base((XIODevice*)file);
        XFile_deleteLater(file);
        return self->m_filePaths;
    }

    XIODevice_seek_base((XIODevice*)file, (int64_t)centralOffset);

    /* 读取中央目录中的文件条目 */
    while (1) {
        uint8_t header[46];
        if (XIODevice_read_1((XIODevice*)file, (char*)header, 46) != 46) break;

        uint32_t sig = *(uint32_t*)header;
        if (sig != 0x02014b50) break; /* 中央目录条目签名 */

        uint16_t nameLen = *(uint16_t*)(header + 28);

        char* name = (char*)XMalloc_System(nameLen + 1);
        if (!name) break;

        if (XIODevice_read_1((XIODevice*)file, name, nameLen) != nameLen) {
            XFree_System(name);
            break;
        }
        name[nameLen] = '\0';

        /* 只添加文件，不添加目录 */
        if (nameLen > 0 && name[nameLen-1] != '/') {
            XStringList_push_back_utf8(self->m_filePaths, name);
        }

        XFree_System(name);
    }

    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    return self->m_filePaths;
}

XByteArray* XZipReader_fileData(const XZipReader* self, const XString* fileName) {
    if (!self || !self->m_fileName || !fileName) return NULL;
    const char* fileNameUtf8 = XString_toUtf8(fileName);

    XFile* file = XFile_create_2(self->m_fileName);
    if (!file) return NULL;
    if (!XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        XFile_deleteLater(file);
        return NULL;
    }

    int64_t fileSize = XIODevice_size_base((XIODevice*)file);

    /* 查找中央目录 */
    size_t centralOffset = find_central_dir_offset((XIODevice*)file, fileSize);
    if (centralOffset == 0) {
        XIODevice_close_base((XIODevice*)file);
        XFile_deleteLater(file);
        return NULL;
    }

    XIODevice_seek_base((XIODevice*)file, (int64_t)centralOffset);

    /* 在中央目录中查找目标文件 */
    uint32_t localHeaderOffset = 0;

    while (1) {
        uint8_t header[46];
        if (XIODevice_read_1((XIODevice*)file, (char*)header, 46) != 46) break;

        uint32_t sig = *(uint32_t*)header;
        if (sig != 0x02014b50) break;

        uint16_t nameLen = *(uint16_t*)(header + 28);

        char* name = (char*)XMalloc_System(nameLen + 1);
        if (!name) break;

        if (XIODevice_read_1((XIODevice*)file, name, nameLen) != nameLen) {
            XFree_System(name);
            break;
        }
        name[nameLen] = '\0';

        if (strcmp(name, fileNameUtf8) == 0) {
            localHeaderOffset = *(uint32_t*)(header + 42);
            XFree_System(name);
            break;
        }

        XFree_System(name);
    }

    if (localHeaderOffset == 0) {
        XIODevice_close_base((XIODevice*)file);
        XFile_deleteLater(file);
        return NULL;
    }

    /* 读取本地文件头 */
    XIODevice_seek_base((XIODevice*)file, (int64_t)localHeaderOffset);
    uint8_t localHeader[30];
    if (XIODevice_read_1((XIODevice*)file, (char*)localHeader, 30) != 30) {
        XIODevice_close_base((XIODevice*)file);
        XFile_deleteLater(file);
        return NULL;
    }

    uint32_t sig = *(uint32_t*)localHeader;
    if (sig != 0x04034b50) {
        XIODevice_close_base((XIODevice*)file);
        XFile_deleteLater(file);
        return NULL;
    }

    uint16_t compMethod = *(uint16_t*)(localHeader + 8);
    uint32_t compSize = *(uint32_t*)(localHeader + 18);
    uint32_t uncompSize = *(uint32_t*)(localHeader + 22);
    uint16_t nameLen = *(uint16_t*)(localHeader + 26);
    uint16_t extraLen = *(uint16_t*)(localHeader + 28);

    /* 跳过文件名和额外字段，读取数据 */
    XIODevice_seek_base((XIODevice*)file,
        XIODevice_pos_base((XIODevice*)file) + nameLen + extraLen);

    uint8_t* compData = (uint8_t*)XMalloc_System(compSize);
    if (!compData) {
        XIODevice_close_base((XIODevice*)file);
        XFile_deleteLater(file);
        return NULL;
    }

    if (XIODevice_read_1((XIODevice*)file, (char*)compData, compSize) != (int64_t)compSize) {
        XFree_System(compData);
        XIODevice_close_base((XIODevice*)file);
        XFile_deleteLater(file);
        return NULL;
    }
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);

    XByteArray* result = XByteArray_create();

    if (compMethod == 0) {
        /* 存储模式，直接复制 */
        for (uint32_t i = 0; i < uncompSize; i++) {
            XByteArray_push_back_1(result, compData[i]);
        }
    } else if (compMethod == 8) {
        /* DEFLATE 压缩 */
        size_t outSize = 0;
        uint8_t* decompData = inflate_data(compData, compSize, &outSize);
        XFree_System(compData);

        if (decompData) {
            for (size_t i = 0; i < outSize; i++) {
                XByteArray_push_back_1(result, decompData[i]);
            }
            XFree_System(decompData);
        } else {
            XByteArray_delete_base(result);
            return NULL;
        }
        return result;
    }

    XFree_System(compData);
    return result;
}
