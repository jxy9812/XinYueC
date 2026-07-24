#include "XZipReader.h"
#include "XMemory.h"
#include "zlib.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

/* 查找中央目录偏移 */
static size_t find_central_dir_offset(FILE* fp) {
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    
    size_t readSize = (fileSize < 65558) ? fileSize : 65558;
    fseek(fp, fileSize - readSize, SEEK_SET);
    
    uint8_t* buf = (uint8_t*)XMalloc_System(readSize);
    if (!buf) return 0;
    
    fread(buf, 1, readSize, fp);
    
    for (long i = readSize - 22; i >= 0; i--) {
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

XZipReader* XZipReader_create(const char* fileName) {
    XZipReader* self = (XZipReader*)XMalloc_System(sizeof(XZipReader));
    if (!self) return NULL;
    memset(self, 0, sizeof(XZipReader));
    if (fileName) {
        self->m_fileName = XString_create();
        if (self->m_fileName) XString_append_utf8(self->m_fileName, fileName);
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
    const char* fname = XString_toUtf8(self->m_fileName);
    if (!fname) return false;
    FILE* fp = fopen(fname, "rb");
    if (fp) { fclose(fp); return true; }
    return false;
}

XStringList* XZipReader_filePaths(const XZipReader* self) {
    if (!self || !self->m_fileName) return self ? self->m_filePaths : NULL;
    
    /* 如果已经有文件列表就直接返回 */
    if (XStringList_size_base(self->m_filePaths) > 0) {
        return self->m_filePaths;
    }
    
    const char* fname = XString_toUtf8(self->m_fileName);
    FILE* fp = fopen(fname, "rb");
    if (!fp) return self->m_filePaths;
    
    /* 查找中央目录 */
    size_t centralOffset = find_central_dir_offset(fp);
    if (centralOffset == 0) { fclose(fp); return self->m_filePaths; }
    
    fseek(fp, centralOffset, SEEK_SET);
    
    /* 读取中央目录中的文件条目 */
    while (1) {
        uint8_t header[46];
        if (fread(header, 1, 46, fp) != 46) break;
        
        uint32_t sig = *(uint32_t*)header;
        if (sig != 0x02014b50) break; /* 中央目录条目签名 */
        
        uint16_t nameLen = *(uint16_t*)(header + 28);
        
        char* name = (char*)XMalloc_System(nameLen + 1);
        if (!name) break;
        
        if (fread(name, 1, nameLen, fp) != nameLen) {
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
    
    fclose(fp);
    return self->m_filePaths;
}

XByteArray* XZipReader_fileData(const XZipReader* self, const char* fileName) {
    if (!self || !self->m_fileName || !fileName) return NULL;
    
    const char* fname = XString_toUtf8(self->m_fileName);
    FILE* fp = fopen(fname, "rb");
    if (!fp) return NULL;
    
    /* 查找中央目录 */
    size_t centralOffset = find_central_dir_offset(fp);
    if (centralOffset == 0) { fclose(fp); return NULL; }
    
    fseek(fp, centralOffset, SEEK_SET);
    
    /* 在中央目录中查找目标文件 */
    uint32_t localHeaderOffset = 0;
    uint16_t foundNameLen = 0;
    
    while (1) {
        uint8_t header[46];
        if (fread(header, 1, 46, fp) != 46) break;
        
        uint32_t sig = *(uint32_t*)header;
        if (sig != 0x02014b50) break;
        
        uint16_t nameLen = *(uint16_t*)(header + 28);
        
        char* name = (char*)XMalloc_System(nameLen + 1);
        if (!name) break;
        
        if (fread(name, 1, nameLen, fp) != nameLen) {
            XFree_System(name);
            break;
        }
        name[nameLen] = '\0';
        
        if (strcmp(name, fileName) == 0) {
            localHeaderOffset = *(uint32_t*)(header + 42);
            foundNameLen = nameLen;
            XFree_System(name);
            break;
        }
        
        XFree_System(name);
    }
    
    if (localHeaderOffset == 0) { fclose(fp); return NULL; }
    
    /* 读取本地文件头 */
    fseek(fp, localHeaderOffset, SEEK_SET);
    uint8_t localHeader[30];
    if (fread(localHeader, 1, 30, fp) != 30) { fclose(fp); return NULL; }
    
    uint32_t sig = *(uint32_t*)localHeader;
    if (sig != 0x04034b50) { fclose(fp); return NULL; }
    
    uint16_t compMethod = *(uint16_t*)(localHeader + 8);
    uint32_t compSize = *(uint32_t*)(localHeader + 18);
    uint32_t uncompSize = *(uint32_t*)(localHeader + 22);
    uint16_t nameLen = *(uint16_t*)(localHeader + 26);
    uint16_t extraLen = *(uint16_t*)(localHeader + 28);
    
    /* 跳过文件名和额外字段，读取数据 */
    fseek(fp, nameLen + extraLen, SEEK_CUR);
    
    uint8_t* compData = (uint8_t*)XMalloc_System(compSize);
    if (!compData) { fclose(fp); return NULL; }
    
    if (fread(compData, 1, compSize, fp) != compSize) {
        XFree_System(compData);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    
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
