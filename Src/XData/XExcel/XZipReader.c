#include "XZipReader.h"
#include "XMemory.h"
#include "XFile.h"
#include "XClass.h"
#include "zlib.h"
#include <limits.h>
#include <string.h>

#define ZIP_LOCAL_FILE_HEADER_SIG 0x04034b50u
#define ZIP_CENTRAL_DIR_SIG       0x02014b50u
#define ZIP_END_CENTRAL_DIR_SIG   0x06054b50u

static uint16_t read_le16(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t* data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool range_is_valid(size_t offset, size_t length, size_t total)
{
    return offset <= total && length <= total - offset;
}

static bool find_central_directory(const uint8_t* data, size_t size,
                                   size_t* centralOffset, uint16_t* entryCount)
{
    if (!data || size < 22 || !centralOffset || !entryCount) return false;

    size_t searchStart = size > 65557 ? size - 65557 : 0;
    size_t pos = size - 22;
    for (;;) {
        if (read_le32(data + pos) == ZIP_END_CENTRAL_DIR_SIG) {
            uint16_t diskNumber = read_le16(data + pos + 4);
            uint16_t centralDisk = read_le16(data + pos + 6);
            uint16_t diskEntries = read_le16(data + pos + 8);
            uint16_t totalEntries = read_le16(data + pos + 10);
            uint32_t centralSize32 = read_le32(data + pos + 12);
            uint32_t centralOffset32 = read_le32(data + pos + 16);
            uint16_t commentLength = read_le16(data + pos + 20);

            if (diskNumber != 0 || centralDisk != 0 || diskEntries != totalEntries) return false;
            if (!range_is_valid(pos + 22, commentLength, size)) return false;
            if (!range_is_valid((size_t)centralOffset32, (size_t)centralSize32, size)) return false;
            if ((size_t)centralOffset32 + (size_t)centralSize32 > pos) return false;

            *centralOffset = (size_t)centralOffset32;
            *entryCount = totalEntries;
            return true;
        }
        if (pos == searchStart) break;
        --pos;
    }
    return false;
}

static XByteArray* load_archive_data(const XZipReader* self)
{
    if (!self) return NULL;
    if (self->m_archiveData) return self->m_archiveData;
    if (!self->m_fileName) return NULL;

    XFile* file = XFile_create_2(self->m_fileName);
    if (!file) return NULL;
    if (!XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        XClass_delete_base((XClass*)file);
        return NULL;
    }

    XByteArray* data = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    if (!data) return NULL;

    ((XZipReader*)self)->m_archiveData = data;
    return data;
}

static uint8_t* inflate_data(const uint8_t* compressed, size_t compSize,
                             size_t expectedSize, size_t* outSize)
{
    if (!compressed || !outSize || compSize > UINT_MAX || expectedSize > UINT_MAX) return NULL;
    *outSize = 0;

    size_t allocationSize = expectedSize ? expectedSize : 1;
    uint8_t* out = (uint8_t*)XMalloc_System(allocationSize);
    if (!out) return NULL;

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -15) != Z_OK) {
        XFree_System(out);
        return NULL;
    }

    stream.next_in = (Bytef*)compressed;
    stream.avail_in = (uInt)compSize;
    stream.next_out = out;
    stream.avail_out = (uInt)allocationSize;

    int result = inflate(&stream, Z_FINISH);
    size_t actualSize = (size_t)stream.total_out;
    inflateEnd(&stream);
    if (result != Z_STREAM_END || actualSize != expectedSize) {
        XFree_System(out);
        return NULL;
    }

    *outSize = actualSize;
    return out;
}

XZipReader* XZipReader_create(const XString* fileName)
{
    XZipReader* self = (XZipReader*)XMalloc_System(sizeof(XZipReader));
    if (!self) return NULL;
    memset(self, 0, sizeof(XZipReader));
    if (fileName) self->m_fileName = XString_create_copy(fileName);
    self->m_filePaths = XStringList_create();
    if (!self->m_filePaths || (fileName && !self->m_fileName)) {
        XZipReader_delete(self);
        return NULL;
    }
    return self;
}

XZipReader* XZipReader_createFromData(const uint8_t* data, size_t size)
{
    if (!data || size == 0) return NULL;
    XZipReader* self = XZipReader_create(NULL);
    if (!self) return NULL;
    self->m_archiveData = XByteArray_create_with_data((const char*)data, size);
    if (!self->m_archiveData) {
        XZipReader_delete(self);
        return NULL;
    }
    return self;
}

void XZipReader_delete(XZipReader* self)
{
    if (!self) return;
    if (self->m_fileName) XString_delete_base(self->m_fileName);
    if (self->m_filePaths) XStringList_delete_base(self->m_filePaths);
    if (self->m_archiveData) XByteArray_delete_base(self->m_archiveData);
    XFree_System(self);
}

bool XZipReader_exists(const XZipReader* self)
{
    XByteArray* archive = load_archive_data(self);
    if (!archive) return false;
    size_t centralOffset = 0;
    uint16_t entryCount = 0;
    return find_central_directory(XByteArray_data(archive),
                                  XByteArray_size_base(archive),
                                  &centralOffset, &entryCount);
}

XStringList* XZipReader_filePaths(const XZipReader* self)
{
    if (!self || !self->m_filePaths) return NULL;
    if (XStringList_size_base(self->m_filePaths) > 0) return self->m_filePaths;

    XByteArray* archive = load_archive_data(self);
    if (!archive) return self->m_filePaths;
    const uint8_t* data = XByteArray_data(archive);
    size_t size = XByteArray_size_base(archive);
    size_t offset = 0;
    uint16_t entryCount = 0;
    if (!find_central_directory(data, size, &offset, &entryCount)) return self->m_filePaths;

    for (uint16_t i = 0; i < entryCount; ++i) {
        if (!range_is_valid(offset, 46, size) || read_le32(data + offset) != ZIP_CENTRAL_DIR_SIG) break;
        uint16_t nameLength = read_le16(data + offset + 28);
        uint16_t extraLength = read_le16(data + offset + 30);
        uint16_t commentLength = read_le16(data + offset + 32);
        size_t entryLength = 46u + nameLength + extraLength + commentLength;
        if (!range_is_valid(offset, entryLength, size)) break;

        const uint8_t* name = data + offset + 46;
        if (nameLength > 0 && name[nameLength - 1] != '/') {
            char* path = (char*)XMalloc_System((size_t)nameLength + 1);
            if (!path) break;
            memcpy(path, name, nameLength);
            path[nameLength] = '\0';
            XStringList_push_back_utf8(self->m_filePaths, path);
            XFree_System(path);
        }
        offset += entryLength;
    }
    return self->m_filePaths;
}

XByteArray* XZipReader_fileData(const XZipReader* self, const XString* fileName)
{
    if (!self || !fileName) return NULL;
    const char* wantedName = XString_toUtf8(fileName);
    if (!wantedName) return NULL;

    XByteArray* archive = load_archive_data(self);
    if (!archive) return NULL;
    const uint8_t* data = XByteArray_data(archive);
    size_t size = XByteArray_size_base(archive);
    size_t offset = 0;
    uint16_t entryCount = 0;
    if (!find_central_directory(data, size, &offset, &entryCount)) return NULL;

    for (uint16_t i = 0; i < entryCount; ++i) {
        if (!range_is_valid(offset, 46, size) || read_le32(data + offset) != ZIP_CENTRAL_DIR_SIG) return NULL;

        uint16_t flags = read_le16(data + offset + 8);
        uint16_t method = read_le16(data + offset + 10);
        uint32_t expectedCrc = read_le32(data + offset + 16);
        uint32_t compressedSize = read_le32(data + offset + 20);
        uint32_t uncompressedSize = read_le32(data + offset + 24);
        uint16_t nameLength = read_le16(data + offset + 28);
        uint16_t extraLength = read_le16(data + offset + 30);
        uint16_t commentLength = read_le16(data + offset + 32);
        uint32_t localOffset = read_le32(data + offset + 42);
        size_t entryLength = 46u + nameLength + extraLength + commentLength;
        if (!range_is_valid(offset, entryLength, size)) return NULL;

        const uint8_t* name = data + offset + 46;
        size_t wantedLength = strlen(wantedName);
        bool matches = wantedLength == nameLength && memcmp(name, wantedName, nameLength) == 0;
        if (!matches) {
            offset += entryLength;
            continue;
        }

        if ((flags & 1u) != 0 || (method != 0 && method != 8)) return NULL;
        if (!range_is_valid(localOffset, 30, size) || read_le32(data + localOffset) != ZIP_LOCAL_FILE_HEADER_SIG) return NULL;
        uint16_t localNameLength = read_le16(data + localOffset + 26);
        uint16_t localExtraLength = read_le16(data + localOffset + 28);
        size_t payloadOffset = (size_t)localOffset + 30u + localNameLength + localExtraLength;
        if (!range_is_valid(payloadOffset, compressedSize, size)) return NULL;

        const uint8_t* output = data + payloadOffset;
        uint8_t* inflated = NULL;
        size_t outputSize = uncompressedSize;
        if (method == 0) {
            if (compressedSize != uncompressedSize) return NULL;
        } else {
            inflated = inflate_data(data + payloadOffset, compressedSize, uncompressedSize, &outputSize);
            if (!inflated) return NULL;
            output = inflated;
        }

        uLong actualCrc = crc32(0L, Z_NULL, 0);
        actualCrc = crc32(actualCrc, output, (uInt)outputSize);
        if ((uint32_t)actualCrc != expectedCrc) {
            if (inflated) XFree_System(inflated);
            return NULL;
        }

        XByteArray* result = XByteArray_create_with_data((const char*)output, outputSize);
        if (inflated) XFree_System(inflated);
        return result;
    }
    return NULL;
}
