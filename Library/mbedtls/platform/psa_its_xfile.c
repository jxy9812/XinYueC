/******************************************************************************
 * @file       psa_its_xfile.c
 * @brief      PSA Internal Trusted Storage 统一实现——XFile 桥接后端。
 * @details    替代 MBEDTLS_PSA_ITS_FILE_C（stdio 直写，桌面专用）：密钥
 *             材料/随机种子的持久化统一经库内跨平台文件 API
 *             （XDevice_open + XDevice_read/write/seek/close）完成。
 *             桌面后端等价于普通文件；裸机端 XDeviceFile 自动桥接
 *             FatFs 后端（Library/Fatfs/XDeviceFile_fatfs.c），密钥
 *             落盘即用，无需任何额外接入。
 *
 *             存储布局（每 uid 一个文件，文件名由 uid 的 16 位十六进制
 *             构成，避开目标文件系统的大小写/字符集差异）：
 *                 [4B LE create_flags][payload]
 *             写入采用「先删后建」，规避尾部残留；原子性由调用方
 *             （PSA 密钥槽管理）的事务语义兜底。
 * @note       打开失败统一映射为 PSA_ERROR_DOES_NOT_EXIST（与 ITS 文件
 *             实现按 errno 区分的口径相比收窄了 IO 故障的可见性；
 *             PSA 层面对不存在与读取失败的处理路径一致）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "psa_its_xin.h"

#include "XDevice.h"
#include "XDeviceFile.h"
#include "XString.h"
#include <stdint.h>
#include <string.h>

/* ITS 文件头：4 字节小端 create_flags。 */
#define PSA_ITS_HEADER_SIZE 4

static XString *psa_its_path_for_uid(psa_storage_uid_t uid)
{
    return XString_create_fmt_utf8("psa_its_%016llx.psaits",
                                   (unsigned long long) uid);
}

static XFd psa_its_open(const XString *path, int mode)
{
    XDeviceOpenOptions options;
    int error = 0;
    memset(&options, 0, sizeof(options));
    options.m_openMode = mode;
    options.m_target = path;
    return XDevice_open(XDeviceType_File, &options, &error);
}

static int64_t psa_its_file_size(XFd fd)
{
    int64_t saved = XDevice_seek(fd, 0, XSeekEnd);
    if (saved < 0) return -1;
    if (XDevice_seek(fd, 0, XSeekSet) < 0) return -1;
    return saved;
}

static bool psa_its_read_header(XFd fd, uint32_t *outFlags)
{
    uint8_t header[PSA_ITS_HEADER_SIZE];
    if (XDevice_seek(fd, 0, XSeekSet) < 0) return false;
    if (XDevice_read(fd, header, sizeof(header)) != (int64_t) sizeof(header)) {
        return false;
    }
    *outFlags = (uint32_t) header[0] | ((uint32_t) header[1] << 8) |
                ((uint32_t) header[2] << 16) | ((uint32_t) header[3] << 24);
    return true;
}

psa_status_t psa_its_set(psa_storage_uid_t uid,
                         uint32_t data_length,
                         const void *p_data,
                         psa_storage_create_flags_t create_flags)
{
    XString *path = psa_its_path_for_uid(uid);
    XFd fd;
    uint8_t header[PSA_ITS_HEADER_SIZE];
    psa_status_t status = PSA_SUCCESS;

    if (!path) return PSA_ERROR_INSUFFICIENT_MEMORY;
    /* 先删后建：规避旧文件更长时的尾部残留。 */
    (void) XDeviceFile_removePermanent(path);
    fd = psa_its_open(path, XIODevice_ReadWrite | XIODevice_Create);
    if (fd == XFD_INVALID) {
        XClassDelete(path);
        return PSA_ERROR_STORAGE_FAILURE;
    }
    header[0] = (uint8_t) (create_flags & 0xFF);
    header[1] = (uint8_t) ((create_flags >> 8) & 0xFF);
    header[2] = (uint8_t) ((create_flags >> 16) & 0xFF);
    header[3] = (uint8_t) ((create_flags >> 24) & 0xFF);
    if (XDevice_write(fd, header, sizeof(header)) != (int64_t) sizeof(header) ||
        (data_length > 0 &&
         XDevice_write(fd, p_data, (int64_t) data_length) != (int64_t) data_length)) {
        status = PSA_ERROR_STORAGE_FAILURE;
    }
    XDevice_close(fd);
    XClassDelete(path);
    return status;
}

psa_status_t psa_its_get(psa_storage_uid_t uid,
                         uint32_t data_offset,
                         uint32_t data_length,
                         void *p_data,
                         size_t *p_data_length)
{
    XString *path = psa_its_path_for_uid(uid);
    XFd fd;
    int64_t fileSize;
    uint32_t flags;
    psa_status_t status = PSA_SUCCESS;

    if (!path) return PSA_ERROR_INSUFFICIENT_MEMORY;
    fd = psa_its_open(path, XIODevice_ReadOnly | XIODevice_Existing);
    if (fd == XFD_INVALID) {
        XClassDelete(path);
        return PSA_ERROR_DOES_NOT_EXIST;
    }
    fileSize = psa_its_file_size(fd);
    if (fileSize < PSA_ITS_HEADER_SIZE ||
        !psa_its_read_header(fd, &flags)) {
        XDevice_close(fd);
        XClassDelete(path);
        return PSA_ERROR_DATA_CORRUPT;
    }
    (void) flags;
    if ((int64_t) data_offset > fileSize - PSA_ITS_HEADER_SIZE ||
        (int64_t) (data_offset + data_length) >
            fileSize - PSA_ITS_HEADER_SIZE) {
        XDevice_close(fd);
        XClassDelete(path);
        return PSA_ERROR_DATA_CORRUPT;
    }
    if (XDevice_seek(fd, (int64_t) (PSA_ITS_HEADER_SIZE + data_offset),
                     XSeekSet) < 0) {
        status = PSA_ERROR_STORAGE_FAILURE;
    } else if (data_length > 0 &&
               XDevice_read(fd, p_data, (int64_t) data_length) !=
                   (int64_t) data_length) {
        status = PSA_ERROR_DATA_CORRUPT;
    }
    if (status == PSA_SUCCESS && p_data_length) *p_data_length = data_length;
    XDevice_close(fd);
    XClassDelete(path);
    return status;
}

psa_status_t psa_its_get_info(psa_storage_uid_t uid,
                              struct psa_storage_info_t *p_info)
{
    XString *path = psa_its_path_for_uid(uid);
    XFd fd;
    int64_t fileSize;
    uint32_t flags;

    if (!path) return PSA_ERROR_INSUFFICIENT_MEMORY;
    fd = psa_its_open(path, XIODevice_ReadOnly | XIODevice_Existing);
    if (fd == XFD_INVALID) {
        XClassDelete(path);
        return PSA_ERROR_DOES_NOT_EXIST;
    }
    fileSize = psa_its_file_size(fd);
    if (fileSize < PSA_ITS_HEADER_SIZE || !psa_its_read_header(fd, &flags)) {
        XDevice_close(fd);
        XClassDelete(path);
        return PSA_ERROR_DATA_CORRUPT;
    }
    XDevice_close(fd);
    p_info->size = (uint32_t) (fileSize - PSA_ITS_HEADER_SIZE);
    p_info->flags = (psa_storage_create_flags_t) flags;
    XClassDelete(path);
    return PSA_SUCCESS;
}

psa_status_t psa_its_remove(psa_storage_uid_t uid)
{
    XString *path = psa_its_path_for_uid(uid);
    psa_status_t status;

    if (!path) return PSA_ERROR_INSUFFICIENT_MEMORY;
    if (XDeviceFile_removePermanent(path)) {
        status = PSA_SUCCESS;
    } else {
        status = PSA_ERROR_DOES_NOT_EXIST;
    }
    XClassDelete(path);
    return status;
}
