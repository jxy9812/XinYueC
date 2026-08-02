/**
 * @file       XMySqlWireClient.c
 * @brief      基于 XinYueC 网络抽象的 MySQL 协议客户端。
 * @details    该文件是 XSqlMySqlClientApi 的默认源码实现。它不依赖
 *             mysqlclient、MariaDB Connector/C、操作系统套接字 API 或
 *             具体 SSL 后端；特殊平台传输和公钥加密均通过抽象接口访问。
 *             TLS 关闭时 XSslSocket 按普通 TCP 套接字工作。
 */
#include "XSqlMySqlClient.h"

#include "XCryptographicHash.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XString.h"
#include "XTcpSocket.h"
#include "XAbstractSocket_p.h"
#include "XSslSocket.h"
#include "XSsl_platform.h"
#include "XIODevice.h"
#include "XFile.h"
#include "XSqlMySqlClient_platform.h"
#include "zlib.h"

#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define XMYSQL_MAX_PACKET_SIZE 0x00ffffffu
#define XMYSQL_UNIX_SOCKET_PATH_MAX 512u
#define XMYSQL_DEFAULT_UNIX_SOCKET "/run/mysqld/mysqld.sock"
#define XMYSQL_DEFAULT_NAMED_PIPE "\\\\.\\pipe\\MySQL"
#define XMYSQL_DEFAULT_SHARED_MEMORY_BASE_NAME "MYSQL"

/* MySQL 客户端能力位。只选择本源码实现实际处理的能力。 */
#define XMYSQL_CLIENT_LONG_PASSWORD 0x00000001u
#define XMYSQL_CLIENT_FOUND_ROWS 0x00000002u
#define XMYSQL_CLIENT_COMPRESS 0x00000020u
#define XMYSQL_CLIENT_CONNECT_WITH_DB 0x00000008u
#define XMYSQL_CLIENT_ODBC 0x00000040u
#define XMYSQL_CLIENT_LOCAL_FILES 0x00000080u
#define XMYSQL_CLIENT_NO_SCHEMA 0x00000010u
#define XMYSQL_CLIENT_PROTOCOL_41 0x00000200u
#define XMYSQL_CLIENT_IGNORE_SPACE 0x00000100u
#define XMYSQL_CLIENT_INTERACTIVE 0x00000400u
#define XMYSQL_CLIENT_TRANSACTIONS 0x00002000u
#define XMYSQL_CLIENT_SECURE_CONNECTION 0x00008000u
#define XMYSQL_CLIENT_PLUGIN_AUTH 0x00080000u
#define XMYSQL_CLIENT_SSL 0x00000800u
#define XMYSQL_CLIENT_MULTI_STATEMENTS 0x00010000u
#define XMYSQL_CLIENT_MULTI_RESULTS 0x00020000u
#define XMYSQL_CLIENT_PS_MULTI_RESULTS 0x00040000u

/* MySQL 命令和字段类型。 */
#define XMYSQL_TYPE_DECIMAL 0u
#define XMYSQL_COM_QUERY 0x03u
#define XMYSQL_TYPE_TINY 1u
#define XMYSQL_TYPE_SHORT 2u
#define XMYSQL_TYPE_LONG 3u
#define XMYSQL_TYPE_FLOAT 4u
#define XMYSQL_TYPE_DOUBLE 5u
#define XMYSQL_TYPE_NULL 6u
#define XMYSQL_TYPE_TIMESTAMP 7u
#define XMYSQL_TYPE_LONGLONG 8u
#define XMYSQL_TYPE_INT24 9u
#define XMYSQL_TYPE_DATE 10u
#define XMYSQL_TYPE_TIME 11u
#define XMYSQL_TYPE_DATETIME 12u
#define XMYSQL_TYPE_YEAR 13u
#define XMYSQL_TYPE_NEWDATE 14u
#define XMYSQL_TYPE_VARCHAR 15u
#define XMYSQL_TYPE_BIT 16u
#define XMYSQL_TYPE_TIMESTAMP2 17u
#define XMYSQL_TYPE_DATETIME2 18u
#define XMYSQL_TYPE_TIME2 19u
#define XMYSQL_TYPE_JSON 245u
#define XMYSQL_TYPE_NEWDECIMAL 246u
#define XMYSQL_TYPE_ENUM 247u
#define XMYSQL_TYPE_SET 248u
#define XMYSQL_TYPE_TINY_BLOB 249u
#define XMYSQL_TYPE_MEDIUM_BLOB 250u
#define XMYSQL_TYPE_LONG_BLOB 251u
#define XMYSQL_TYPE_BLOB 252u
#define XMYSQL_TYPE_VAR_STRING 253u
#define XMYSQL_TYPE_STRING 254u
#define XMYSQL_TYPE_GEOMETRY 255u
#define XMYSQL_FLAG_UNSIGNED 0x0020u
#define XMYSQL_FLAG_BINARY 0x0080u
#define XMYSQL_SERVER_MORE_RESULTS_EXISTS 0x0008u
#define XMYSQL_COM_STMT_PREPARE 0x16u
#define XMYSQL_COM_STMT_EXECUTE 0x17u
#define XMYSQL_COM_STMT_CLOSE 0x19u

typedef enum XMySqlTlsMode {
    XMySqlTlsDisabled = 0,
    XMySqlTlsPreferred,
    XMySqlTlsRequired,
    XMySqlTlsVerifyCa,
    XMySqlTlsVerifyIdentity
} XMySqlTlsMode;

typedef struct XMySqlWireCell {
    XSqlMySqlValue m_value;
    XByteArray* m_bytes;
} XMySqlWireCell;

struct XSqlMySqlResult {
    XSqlMySqlField* m_fields;
    XString** m_fieldNames;
    XString** m_fieldTables;
    XString** m_fieldDatabases;
    XMySqlWireCell* m_cells;
    size_t m_fieldCount;
    size_t m_rowCount;
    int m_at;
    int64_t m_rowsAffected;
    uint64_t m_lastInsertId;
    bool m_select;
    XSqlMySqlResult* m_next;
    bool m_moreResults;
};

struct XSqlMySqlClient {
    XTcpSocket* m_socket;
    XString* m_driverText;
    XString* m_databaseText;
    XString* m_errorCode;
    XSqlErrorType m_errorType;
    XSqlMySqlError m_error;
    uint8_t m_scramble[20];
    size_t m_scrambleSize;
    uint32_t m_serverCapabilities;
    uint32_t m_serverStatus;
    char m_authPlugin[64];
    bool m_useTls;
    XMySqlTlsMode m_tlsMode;
    bool m_tlsModeSpecified;
    XSslProtocol m_tlsProtocol;
    uint32_t m_clientFlags;
    bool m_preparedQueries;
    int m_connectTimeout;
    int m_readTimeout;
    int m_writeTimeout;
    bool m_multiStatements;
    bool m_localInfile;
    bool m_compress;
    bool m_useSharedMemory;
    XSqlMySqlSharedMemory* m_sharedMemory;
    bool m_compressionActive;
    uint8_t m_compressedSendSequence;
    uint8_t m_compressedReadSequence;
    XByteArray* m_compressedReadBuffer;
    XSslCertificate* m_caCertificate;
    bool m_verifyPeer;
    bool m_reconnect;
    bool m_reconnecting;
    int m_connectionPort;
    XString* m_connectionDatabase;
    XString* m_connectionUser;
    XString* m_connectionPassword;
    XString* m_connectionHost;
    XString* m_connectionOptions;
    bool m_open;
};

static XSqlMySqlClient* xmysql_client_create(void);
static void xmysql_client_destroy(XSqlMySqlClient* client);
static bool xmysql_client_open(XSqlMySqlClient* client, const char* database,
                               const char* user, const char* password,
                               const char* host, int port, const char* options);
static void xmysql_client_close(XSqlMySqlClient* client);
static bool xmysql_client_reconnect(XSqlMySqlClient* client);
static bool xmysql_client_execute(XSqlMySqlClient* client, const char* query,
                                  size_t length, XSqlMySqlResult** result);
static bool xmysql_client_execute_once(XSqlMySqlClient* client, const char* query,
                                       size_t length, XSqlMySqlResult** result);
static bool xmysql_client_execute_prepared(XSqlMySqlClient* client, const char* query,
                                           size_t length, const XSqlMySqlBind* binds,
                                           size_t bindCount, XSqlMySqlResult** result);
static bool xmysql_client_execute_prepared_once(XSqlMySqlClient* client, const char* query,
                                                size_t length, const XSqlMySqlBind* binds,
                                                size_t bindCount, XSqlMySqlResult** result);
static bool xmysql_client_check_prepared_queries(XSqlMySqlClient* client);
static void xmysql_result_destroy(XSqlMySqlResult* result);
static int xmysql_result_column_count(const XSqlMySqlResult* result);
static const XSqlMySqlField* xmysql_result_field(const XSqlMySqlResult* result, int index);
static bool xmysql_result_fetch(XSqlMySqlResult* result, int index);
static const XSqlMySqlValue* xmysql_result_value(const XSqlMySqlResult* result, int index);
static int xmysql_result_size(const XSqlMySqlResult* result);
static int64_t xmysql_result_rows_affected(const XSqlMySqlResult* result);
static uint64_t xmysql_result_last_insert_id(const XSqlMySqlResult* result);
static bool xmysql_result_is_select(const XSqlMySqlResult* result);
static bool xmysql_result_next(XSqlMySqlResult* result, XSqlMySqlResult** next);
static const XSqlMySqlError* xmysql_client_last_error(const XSqlMySqlClient* client);
static void* xmysql_client_handle(const XSqlMySqlClient* client);
static bool xmysql_client_cancel(XSqlMySqlClient* client);
static bool xmysql_client_supports_transactions(const XSqlMySqlClient* client);
static bool xmysql_client_supports_prepared_queries(const XSqlMySqlClient* client);

static const XSqlMySqlClientApi g_xmysql_client_api = {
    xmysql_client_create,
    xmysql_client_destroy,
    xmysql_client_open,
    xmysql_client_close,
    xmysql_client_execute,
    xmysql_client_execute_prepared,
    xmysql_result_destroy,
    xmysql_result_column_count,
    xmysql_result_field,
    xmysql_result_fetch,
    xmysql_result_value,
    xmysql_result_size,
    xmysql_result_rows_affected,
    xmysql_result_last_insert_id,
    xmysql_result_is_select,
    xmysql_result_next,
    xmysql_client_last_error,
    xmysql_client_handle,
    xmysql_client_cancel,
    xmysql_client_supports_transactions,
    xmysql_client_supports_prepared_queries
};

static void xmysql_clear_string(XString** value)
{
    if (value && *value) {
        XString_delete_base(*value);
        *value = NULL;
    }
}

static bool xmysql_store_connection_parameters(XSqlMySqlClient* client,
                                               const char* database, const char* user,
                                               const char* password, const char* host,
                                               int port, const char* options)
{
    XString* databaseText;
    XString* userText;
    XString* passwordText;
    XString* hostText;
    XString* optionsText;
    if (!client) return false;
    databaseText = XString_create_utf8(database ? database : "");
    userText = XString_create_utf8(user ? user : "");
    passwordText = XString_create_utf8(password ? password : "");
    hostText = XString_create_utf8(host ? host : "");
    optionsText = XString_create_utf8(options ? options : "");
    if (!databaseText || !userText || !passwordText || !hostText || !optionsText) {
        if (databaseText) XString_delete_base(databaseText);
        if (userText) XString_delete_base(userText);
        if (passwordText) XString_delete_base(passwordText);
        if (hostText) XString_delete_base(hostText);
        if (optionsText) XString_delete_base(optionsText);
        return false;
    }
    xmysql_clear_string(&client->m_connectionDatabase);
    xmysql_clear_string(&client->m_connectionUser);
    xmysql_clear_string(&client->m_connectionPassword);
    xmysql_clear_string(&client->m_connectionHost);
    xmysql_clear_string(&client->m_connectionOptions);
    client->m_connectionDatabase = databaseText;
    client->m_connectionUser = userText;
    client->m_connectionPassword = passwordText;
    client->m_connectionHost = hostText;
    client->m_connectionOptions = optionsText;
    client->m_connectionPort = port;
    return true;
}

static void xmysql_set_error_with_length(XSqlMySqlClient* client, const char* driverText,
                                         const char* databaseText, size_t databaseTextLength,
                                         unsigned int code, XSqlErrorType type)
{
    if (!client) return;
    xmysql_clear_string(&client->m_driverText);
    xmysql_clear_string(&client->m_databaseText);
    xmysql_clear_string(&client->m_errorCode);
    client->m_driverText = XString_create_utf8(driverText ? driverText : "");
    client->m_databaseText = databaseText
        ? XString_create_with_length_utf8(databaseText, databaseTextLength)
        : XString_create();
    client->m_errorCode = XString_create_fmt_utf8("%u", code);
    client->m_errorType = type;
    client->m_error.m_driverText = client->m_driverText ? XString_toUtf8(client->m_driverText) : "";
    client->m_error.m_databaseText = client->m_databaseText ? XString_toUtf8(client->m_databaseText) : "";
    client->m_error.m_errorCode = client->m_errorCode ? XString_toUtf8(client->m_errorCode) : "0";
    client->m_error.m_type = type;
}

static void xmysql_set_error(XSqlMySqlClient* client, const char* driverText,
                             const char* databaseText, unsigned int code,
                             XSqlErrorType type)
{
    xmysql_set_error_with_length(client, driverText, databaseText,
                                 databaseText ? strlen(databaseText) : 0, code, type);
}

static void xmysql_clear_error(XSqlMySqlClient* client)
{
    if (!client) return;
    xmysql_set_error(client, "", "", 0, XSqlErrorType_NoError);
}

static int xmysql_ascii_casecmp(const char* left, const char* right)
{
    unsigned char leftChar;
    unsigned char rightChar;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (*left && *right) {
        leftChar = (unsigned char)tolower((unsigned char)*left++);
        rightChar = (unsigned char)tolower((unsigned char)*right++);
        if (leftChar != rightChar) return (int)leftChar - (int)rightChar;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}


static bool xmysql_socket_read(XSqlMySqlClient* client, void* data, size_t size, int timeout)
{
    size_t offset = 0;
    XIODevice* device;
    if (!client) return false;
    if (client->m_useSharedMemory)
        return XSqlMySqlSharedMemory_read(client->m_sharedMemory, data, size, timeout);
    device = (XIODevice*)client->m_socket;
    while (offset < size) {
        int64_t count = XIODevice_read_1(device, (char*)data + offset,
                                         (int64_t)(size - offset));
        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (!XIODevice_waitForReadyRead_base(device, timeout)) return false;
    }
    return true;
}

static bool xmysql_socket_write(XSqlMySqlClient* client, const void* data, size_t size,
                                int timeout)
{
    size_t offset = 0;
    XIODevice* device;
    if (!client) return false;
    if (client->m_useSharedMemory)
        return XSqlMySqlSharedMemory_write(client->m_sharedMemory, data, size, timeout);
    device = (XIODevice*)client->m_socket;
    while (offset < size) {
        int64_t count = XIODevice_write_1(device, (const char*)data + offset,
                                          (int64_t)(size - offset));
        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (!XIODevice_waitForBytesWritten_base(device, timeout)) return false;
    }
    return XIODevice_waitForBytesWritten_base(device, timeout);
}

static bool xmysql_read_packet(XSqlMySqlClient* client, XByteArray** payload, uint8_t* sequence,
                               int timeout)
{
    uint8_t header[4];
    uint32_t size;
    XByteArray* result;
    if (!client || !payload) return false;
    *payload = NULL;
    if (!xmysql_socket_read(client, header, sizeof(header), timeout)) return false;
    size = (uint32_t)header[0] | ((uint32_t)header[1] << 8)
        | ((uint32_t)header[2] << 16);
    if (size > XMYSQL_MAX_PACKET_SIZE) return false;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base(result, size)) {
        if (result) XByteArray_delete_base(result);
        return false;
    }
    if (size > 0 && !xmysql_socket_read(client, XByteArray_data(result), size, timeout)) {
        XByteArray_delete_base(result);
        return false;
    }
    if (sequence) *sequence = header[3];
    *payload = result;
    return true;
}

static bool xmysql_send_packet(XSqlMySqlClient* client, const void* data, size_t size,
                               uint8_t sequence, int timeout)
{
    uint8_t header[4];
    if (!client || size > XMYSQL_MAX_PACKET_SIZE) return false;
    header[0] = (uint8_t)(size & 0xffu);
    header[1] = (uint8_t)((size >> 8) & 0xffu);
    header[2] = (uint8_t)((size >> 16) & 0xffu);
    header[3] = sequence;
    return xmysql_socket_write(client, header, sizeof(header), timeout)
        && (size == 0 || xmysql_socket_write(client, data, size, timeout));
}

static void xmysql_compression_clear_buffer(XSqlMySqlClient* client)
{
    if (!client) return;
    if (client->m_compressedReadBuffer) {
        XByteArray_delete_base(client->m_compressedReadBuffer);
        client->m_compressedReadBuffer = NULL;
    }
}

static bool xmysql_send_compressed_packet(XSqlMySqlClient* client, const void* data,
                                          size_t size, uint8_t sequence)
{
    XByteArray* raw = NULL;
    XByteArray* compressed = NULL;
    uint8_t* output;
    size_t outputSize;
    uint32_t uncompressedSize = 0;
    uLongf compressedSize;
    uint8_t header[7];
    bool ok = false;
    if (!client || size > XMYSQL_MAX_PACKET_SIZE
        || (size > 0 && !data)) return false;
    raw = XByteArray_create();
    if (!raw || !XByteArray_resize_base(raw, size + 4)) goto done;
    output = XByteArray_data(raw);
    output[0] = (uint8_t)(size & 0xffu);
    output[1] = (uint8_t)((size >> 8) & 0xffu);
    output[2] = (uint8_t)((size >> 16) & 0xffu);
    output[3] = sequence;
    if (size > 0) memcpy((uint8_t*)output + 4, data, size);

    compressedSize = compressBound((uLong)XByteArray_size_base(raw));
    compressed = XByteArray_create();
    if (!compressed || !XByteArray_resize_base(compressed, compressedSize)) goto done;
    if (compress2(XByteArray_data(compressed), &compressedSize, output,
                  (uLong)XByteArray_size_base(raw), Z_DEFAULT_COMPRESSION) != Z_OK)
        goto done;
    if (compressedSize < XByteArray_size_base(raw)
        && compressedSize <= XMYSQL_MAX_PACKET_SIZE) {
        output = XByteArray_data(compressed);
        outputSize = (size_t)compressedSize;
        uncompressedSize = (uint32_t)XByteArray_size_base(raw);
    } else {
        output = XByteArray_data(raw);
        outputSize = XByteArray_size_base(raw);
    }
    if (outputSize > XMYSQL_MAX_PACKET_SIZE) goto done;
    header[0] = (uint8_t)(outputSize & 0xffu);
    header[1] = (uint8_t)((outputSize >> 8) & 0xffu);
    header[2] = (uint8_t)((outputSize >> 16) & 0xffu);
    header[3] = client->m_compressedSendSequence++;
    header[4] = (uint8_t)(uncompressedSize & 0xffu);
    header[5] = (uint8_t)((uncompressedSize >> 8) & 0xffu);
    header[6] = (uint8_t)((uncompressedSize >> 16) & 0xffu);
    ok = xmysql_socket_write(client, header, sizeof(header), client->m_writeTimeout)
        && (outputSize == 0
            || xmysql_socket_write(client, output, outputSize, client->m_writeTimeout));
    if (!ok)
        xmysql_set_error(client, "Unable to write MySQL compressed packet", "", 0,
                         XSqlErrorType_ConnectionError);
done:
    if (raw) XByteArray_delete_base(raw);
    if (compressed) XByteArray_delete_base(compressed);
    return ok;
}

static bool xmysql_compressed_extract_packet(XSqlMySqlClient* client,
                                             XByteArray** payload, uint8_t* sequence)
{
    const uint8_t* data;
    size_t bufferSize;
    uint32_t packetSize;
    XByteArray* result;
    if (!client || !payload) return false;
    if (!client->m_compressedReadBuffer) {
        client->m_compressedReadBuffer = XByteArray_create();
        if (!client->m_compressedReadBuffer) return false;
    }
    bufferSize = XByteArray_size_base(client->m_compressedReadBuffer);
    if (bufferSize < 4) return false;
    data = XByteArray_data(client->m_compressedReadBuffer);
    packetSize = (uint32_t)data[0] | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16);
    if (packetSize > XMYSQL_MAX_PACKET_SIZE || bufferSize < (size_t)packetSize + 4)
        return false;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base(result, packetSize)) {
        if (result) XByteArray_delete_base(result);
        return false;
    }
    if (packetSize > 0)
        memcpy(XByteArray_data(result), data + 4, packetSize);
    if (sequence) *sequence = data[3];
    XByteArray_remove_base(client->m_compressedReadBuffer, 0, (int64_t)packetSize + 4);
    *payload = result;
    return true;
}

static bool xmysql_read_compressed_packet(XSqlMySqlClient* client)
{
    uint8_t header[7];
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    XByteArray* packet = NULL;
    XByteArray* decoded = NULL;
    uLongf decodedSize;
    const uint8_t* data;
    bool ok = false;
    if (!client || !client->m_socket) return false;
    if (!xmysql_socket_read(client, header, sizeof(header), client->m_readTimeout)) {
        xmysql_set_error(client, "Unable to read MySQL compressed packet header", "", 0,
                         XSqlErrorType_ConnectionError);
        return false;
    }
    compressedSize = (uint32_t)header[0] | ((uint32_t)header[1] << 8)
        | ((uint32_t)header[2] << 16);
    uncompressedSize = (uint32_t)header[4] | ((uint32_t)header[5] << 8)
        | ((uint32_t)header[6] << 16);
    if (compressedSize == 0 || compressedSize > XMYSQL_MAX_PACKET_SIZE
        || uncompressedSize > XMYSQL_MAX_PACKET_SIZE) {
        xmysql_set_error(client, "Invalid MySQL compressed packet header", "", 0,
                         XSqlErrorType_ConnectionError);
        goto done;
    }
    if (header[3] != client->m_compressedReadSequence) {
        char detail[96];
        snprintf(detail, sizeof(detail), "expected=%u actual=%u",
                 (unsigned int)client->m_compressedReadSequence,
                 (unsigned int)header[3]);
        client->m_compressedReadSequence = 0;
        xmysql_set_error(client, "Unexpected MySQL compressed packet sequence", detail, header[3],
                         XSqlErrorType_ConnectionError);
        goto done;
    }
    ++client->m_compressedReadSequence;
    client->m_compressedSendSequence = client->m_compressedReadSequence;
    packet = XByteArray_create();
    if (!packet || !XByteArray_resize_base(packet, compressedSize)) goto done;
    if (!xmysql_socket_read(client, XByteArray_data(packet), compressedSize,
                            client->m_readTimeout)) {
        xmysql_set_error(client, "Unable to read MySQL compressed packet", "", 0,
                         XSqlErrorType_ConnectionError);
        goto done;
    }
    data = XByteArray_data(packet);
    if (uncompressedSize == 0) {
        decoded = packet;
        packet = NULL;
    } else {
        decoded = XByteArray_create();
        if (!decoded || !XByteArray_resize_base(decoded, uncompressedSize)) goto done;
        decodedSize = uncompressedSize;
        if (uncompress(XByteArray_data(decoded), &decodedSize, data, compressedSize) != Z_OK
            || decodedSize != uncompressedSize) {
            xmysql_set_error(client, "Unable to decompress MySQL packet", "", 0,
                             XSqlErrorType_ConnectionError);
            goto done;
        }
    }
    if (!client->m_compressedReadBuffer
        || !XByteArray_push_back_2(client->m_compressedReadBuffer,
                                   XByteArray_data(decoded),
                                   XByteArray_size_base(decoded))) {
        xmysql_set_error(client, "Unable to buffer MySQL compressed packet", "", 0,
                         XSqlErrorType_ConnectionError);
        goto done;
    }
    ok = true;
done:
    if (packet) XByteArray_delete_base(packet);
    if (decoded) XByteArray_delete_base(decoded);
    return ok;
}

static bool xmysql_client_read_packet(XSqlMySqlClient* client, XByteArray** payload,
                                      uint8_t* sequence)
{
    bool ok;
    if (!client || !payload) return false;
    *payload = NULL;
    if (!client->m_compressionActive) {
        ok = xmysql_read_packet(client, payload, sequence,
                                client->m_readTimeout);
        if (!ok && client->m_errorType == XSqlErrorType_NoError)
            xmysql_set_error(client, "Unable to read MySQL packet", "", 0,
                             XSqlErrorType_ConnectionError);
        return ok;
    }
    for (;;) {
        if (xmysql_compressed_extract_packet(client, payload, sequence)) return true;
        if (!xmysql_read_compressed_packet(client)) {
            if (client->m_errorType == XSqlErrorType_NoError)
                xmysql_set_error(client, "Unable to read MySQL compressed packet", "", 0,
                                 XSqlErrorType_ConnectionError);
            return false;
        }
    }
}

static bool xmysql_client_send_packet(XSqlMySqlClient* client, const void* data, size_t size,
                                      uint8_t sequence)
{
    bool resetSequence;
    bool ok;
    if (!client) return false;
    if (!client->m_compressionActive) {
        ok = xmysql_send_packet(client, data, size, sequence,
                                client->m_writeTimeout);
        if (!ok && client->m_errorType == XSqlErrorType_NoError)
            xmysql_set_error(client, "Unable to write MySQL packet", "", 0,
                             XSqlErrorType_ConnectionError);
        return ok;
    }
    resetSequence = sequence == 0;
    if (resetSequence) {
        client->m_compressedSendSequence = 0;
        client->m_compressedReadSequence = 0;
        xmysql_compression_clear_buffer(client);
    }
    ok = xmysql_send_compressed_packet(client, data, size, sequence);
    if (ok)
        client->m_compressedReadSequence = client->m_compressedSendSequence;
    return ok;
}

static bool xmysql_read_lenenc(const uint8_t** cursor, const uint8_t* end,
                               uint64_t* value, bool* isNull)
{
    const uint8_t* p;
    uint8_t marker;
    if (!cursor || !*cursor || !end || *cursor >= end || !value) return false;
    p = *cursor;
    marker = *p++;
    if (isNull) *isNull = false;
    if (marker == 0xfbu) {
        *value = 0;
        if (isNull) *isNull = true;
    } else if (marker < 0xfbu) {
        *value = marker;
    } else if (marker == 0xfcu) {
        if ((size_t)(end - p) < 2) return false;
        *value = (uint64_t)p[0] | ((uint64_t)p[1] << 8);
        p += 2;
    } else if (marker == 0xfdu) {
        if ((size_t)(end - p) < 3) return false;
        *value = (uint64_t)p[0] | ((uint64_t)p[1] << 8)
            | ((uint64_t)p[2] << 16);
        p += 3;
    } else if (marker == 0xfeu) {
        if ((size_t)(end - p) < 8) return false;
        *value = (uint64_t)p[0] | ((uint64_t)p[1] << 8)
            | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24)
            | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40)
            | ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
        p += 8;
    } else {
        return false;
    }
    *cursor = p;
    return true;
}

static bool xmysql_read_lenenc_slice(const uint8_t** cursor, const uint8_t* end,
                                    const uint8_t** data, size_t* size, bool* isNull)
{
    const uint8_t* p = cursor ? *cursor : NULL;
    uint64_t length = 0;
    bool nullValue = false;
    if (!xmysql_read_lenenc(&p, end, &length, &nullValue)) return false;
    if (nullValue) {
        if (data) *data = NULL;
        if (size) *size = 0;
        if (isNull) *isNull = true;
        if (cursor) *cursor = p;
        return true;
    }
    if (length > (uint64_t)(end - p)) return false;
    if (data) *data = p;
    if (size) *size = (size_t)length;
    if (isNull) *isNull = false;
    if (cursor) *cursor = p + length;
    return true;
}

static bool xmysql_append_u8(XByteArray* data, uint8_t value)
{
    return data && XByteArray_push_back_1(data, value);
}

static bool xmysql_append_u32(XByteArray* data, uint32_t value)
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
    return data && XByteArray_push_back_2(data, bytes, sizeof(bytes));
}

static bool xmysql_append_u16(XByteArray* data, uint16_t value)
{
    return data && XByteArray_push_back_1(data, (uint8_t)(value & 0xffu))
        && XByteArray_push_back_1(data, (uint8_t)((value >> 8) & 0xffu));
}

static bool xmysql_append_u64(XByteArray* data, uint64_t value)
{
    int index;
    if (!data) return false;
    for (index = 0; index < 8; ++index) {
        if (!XByteArray_push_back_1(data, (uint8_t)(value & 0xffu))) return false;
        value >>= 8;
    }
    return true;
}

static bool xmysql_append_lenenc_string(XByteArray* data, const void* value, size_t size)
{
    uint8_t prefix[9];
    size_t prefixSize;
    if (!data || size > XMYSQL_MAX_PACKET_SIZE) return false;
    if (size < 0xfbu) {
        prefix[0] = (uint8_t)size;
        prefixSize = 1;
    } else if (size <= 0xffffu) {
        prefix[0] = 0xfcu;
        prefix[1] = (uint8_t)(size & 0xffu);
        prefix[2] = (uint8_t)((size >> 8) & 0xffu);
        prefixSize = 3;
    } else {
        prefix[0] = 0xfdu;
        prefix[1] = (uint8_t)(size & 0xffu);
        prefix[2] = (uint8_t)((size >> 8) & 0xffu);
        prefix[3] = (uint8_t)((size >> 16) & 0xffu);
        prefixSize = 4;
    }
    return XByteArray_push_back_2(data, prefix, prefixSize)
        && (size == 0 || XByteArray_push_back_2(data, value, size));
}

static bool xmysql_append_cstring(XByteArray* data, const char* value)
{
    size_t size = value ? strlen(value) : 0;
    return data && (size == 0 || XByteArray_push_back_2(data, value, size))
        && XByteArray_push_back_1(data, 0);
}

static XSqlMySqlValueType xmysql_type_from_code(uint8_t code, uint16_t flags)
{
    bool isUnsigned = (flags & XMYSQL_FLAG_UNSIGNED) != 0;
    switch (code) {
    case XMYSQL_TYPE_BIT:
    case XMYSQL_TYPE_TINY:
    case XMYSQL_TYPE_SHORT:
    case XMYSQL_TYPE_LONG:
    case XMYSQL_TYPE_LONGLONG:
    case XMYSQL_TYPE_INT24:
    case XMYSQL_TYPE_YEAR:
        return isUnsigned ? XSqlMySqlValueType_UnsignedInteger : XSqlMySqlValueType_Integer;
    case XMYSQL_TYPE_DECIMAL:
    case XMYSQL_TYPE_FLOAT:
    case XMYSQL_TYPE_DOUBLE:
    case XMYSQL_TYPE_NEWDECIMAL:
        return XSqlMySqlValueType_Real;
    case XMYSQL_TYPE_DATE:
    case XMYSQL_TYPE_TIMESTAMP:
    case XMYSQL_TYPE_TIME:
    case XMYSQL_TYPE_DATETIME:
    case XMYSQL_TYPE_TIMESTAMP2:
    case XMYSQL_TYPE_DATETIME2:
    case XMYSQL_TYPE_TIME2:
        return XSqlMySqlValueType_DateTime;
    case XMYSQL_TYPE_BLOB:
    case XMYSQL_TYPE_TINY_BLOB:
    case XMYSQL_TYPE_MEDIUM_BLOB:
    case XMYSQL_TYPE_LONG_BLOB:
    case XMYSQL_TYPE_JSON:
    case XMYSQL_TYPE_GEOMETRY:
        return (flags & XMYSQL_FLAG_BINARY) != 0
            ? XSqlMySqlValueType_ByteArray : XSqlMySqlValueType_String;
    case XMYSQL_TYPE_VARCHAR:
    case XMYSQL_TYPE_VAR_STRING:
    case XMYSQL_TYPE_STRING:
        return (flags & XMYSQL_FLAG_BINARY) != 0
            ? XSqlMySqlValueType_ByteArray : XSqlMySqlValueType_String;
    case XMYSQL_TYPE_NULL:
        return XSqlMySqlValueType_Null;
    default:
        return XSqlMySqlValueType_String;
    }
}

static bool xmysql_bit_to_text(const uint8_t* value, size_t size,
                               char output[32], size_t* outputSize)
{
    uint64_t number = 0;
    size_t index;
    int length;
    if (!value || size > sizeof(number) || !output || !outputSize) return false;
    for (index = 0; index < size; ++index)
        number = (number << 8) | value[index];
    length = snprintf(output, 32, "%llu", (unsigned long long)number);
    if (length < 0 || length >= 32) return false;
    *outputSize = (size_t)length;
    return true;
}

static bool xmysql_result_allocate_fields(XSqlMySqlResult* result, size_t count)
{
    if (!result) return false;
    result->m_fieldCount = count;
    if (count == 0) return true;
    result->m_fields = (XSqlMySqlField*)XCalloc_System(count, sizeof(*result->m_fields));
    result->m_fieldNames = (XString**)XCalloc_System(count, sizeof(*result->m_fieldNames));
    result->m_fieldTables = (XString**)XCalloc_System(count, sizeof(*result->m_fieldTables));
    result->m_fieldDatabases = (XString**)XCalloc_System(count, sizeof(*result->m_fieldDatabases));
    return result->m_fields && result->m_fieldNames && result->m_fieldTables
        && result->m_fieldDatabases;
}

static bool xmysql_result_parse_field(XSqlMySqlResult* result, int index,
                                      const uint8_t* data, size_t size)
{
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    const uint8_t* valueData;
    size_t valueSize;
    bool isNull;
    uint64_t fixedSize;
    uint16_t characterSet;
    uint32_t fieldLength;
    uint8_t typeCode;
    uint16_t flags;
    XString* name = NULL;
    XString* table = NULL;
    XString* database = NULL;
    if (!result || index < 0 || (size_t)index >= result->m_fieldCount) return false;
    if (!xmysql_read_lenenc_slice(&cursor, end, NULL, NULL, &isNull)
        || !xmysql_read_lenenc_slice(&cursor, end, &valueData, &valueSize, &isNull)) return false;
    database = XString_create_with_length_utf8((const char*)valueData, valueSize);
    if (!xmysql_read_lenenc_slice(&cursor, end, &valueData, &valueSize, &isNull)) goto fail;
    table = XString_create_with_length_utf8((const char*)valueData, valueSize);
    if (!xmysql_read_lenenc_slice(&cursor, end, NULL, NULL, &isNull)
        || !xmysql_read_lenenc_slice(&cursor, end, &valueData, &valueSize, &isNull)
        || !xmysql_read_lenenc_slice(&cursor, end, NULL, NULL, &isNull)) goto fail;
    name = XString_create_with_length_utf8((const char*)valueData, valueSize);
    if (!xmysql_read_lenenc(&cursor, end, &fixedSize, &isNull)
        || fixedSize < 0x0cu || fixedSize > (uint64_t)(end - cursor)) goto fail;
    characterSet = (uint16_t)cursor[0] | ((uint16_t)cursor[1] << 8);
    fieldLength = (uint32_t)cursor[2] | ((uint32_t)cursor[3] << 8)
        | ((uint32_t)cursor[4] << 16) | ((uint32_t)cursor[5] << 24);
    typeCode = cursor[6];
    flags = (uint16_t)cursor[7] | ((uint16_t)cursor[8] << 8);
    (void)characterSet;
    result->m_fieldNames[index] = name;
    result->m_fieldTables[index] = table;
    result->m_fieldDatabases[index] = database;
    result->m_fields[index].m_name = XString_toUtf8(name);
    result->m_fields[index].m_table = XString_toUtf8(table);
    result->m_fields[index].m_database = XString_toUtf8(database);
    result->m_fields[index].m_type = xmysql_type_from_code(typeCode, flags);
    result->m_fields[index].m_length = fieldLength;
    result->m_fields[index].m_flags = flags;
    result->m_fields[index].m_nativeType = typeCode;
    result->m_fields[index].m_decimals = cursor[9];
    result->m_fields[index].m_unsigned = (flags & XMYSQL_FLAG_UNSIGNED) != 0;
    return true;
fail:
    if (name) XString_delete_base(name);
    if (table) XString_delete_base(table);
    if (database) XString_delete_base(database);
    return false;
}

static bool xmysql_result_append_row(XSqlMySqlResult* result,
                                     const uint8_t* data, size_t size)
{
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    size_t row;
    int field;
    XMySqlWireCell* cells;
    if (!result || !data || result->m_fieldCount == 0) return false;
    row = result->m_rowCount;
    cells = (XMySqlWireCell*)XRealloc_System(result->m_cells,
        (row + 1) * result->m_fieldCount * sizeof(*cells));
    if (!cells) return false;
    result->m_cells = cells;
    memset(&result->m_cells[row * result->m_fieldCount], 0,
           result->m_fieldCount * sizeof(*result->m_cells));
    result->m_rowCount = row + 1;
    for (field = 0; (size_t)field < result->m_fieldCount; ++field) {
        const uint8_t* valueData;
        size_t valueSize;
        bool isNull;
        XMySqlWireCell* cell = &result->m_cells[row * result->m_fieldCount + field];
        if (!xmysql_read_lenenc_slice(&cursor, end, &valueData, &valueSize, &isNull)) return false;
        cell->m_value.m_type = isNull ? XSqlMySqlValueType_Null
                                      : result->m_fields[field].m_type;
        cell->m_value.m_isNull = isNull;
        if (!isNull) {
            char bitText[32];
            size_t bitTextSize;
            if (result->m_fields[field].m_nativeType == XMYSQL_TYPE_BIT) {
                if (!xmysql_bit_to_text(valueData, valueSize, bitText, &bitTextSize)) return false;
                cell->m_bytes = XByteArray_create_with_data(bitText, bitTextSize);
            } else {
                cell->m_bytes = XByteArray_create_with_data((const char*)valueData, valueSize);
            }
            if (!cell->m_bytes) return false;
            cell->m_value.m_data = XByteArray_data(cell->m_bytes);
            cell->m_value.m_size = result->m_fields[field].m_nativeType == XMYSQL_TYPE_BIT
                ? bitTextSize : valueSize;
        }
    }
    return true;
}

static XSqlMySqlResult* xmysql_result_create(void)
{
    XSqlMySqlResult* result = (XSqlMySqlResult*)XCalloc_System(1, sizeof(*result));
    if (!result) return NULL;
    result->m_at = XSqlLocation_BeforeFirstRow;
    result->m_rowsAffected = -1;
    return result;
}

static void xmysql_result_destroy(XSqlMySqlResult* result)
{
    size_t index;
    if (!result) return;
    if (result->m_next) {
        xmysql_result_destroy(result->m_next);
        result->m_next = NULL;
    }
    if (result->m_cells) {
        for (index = 0; index < result->m_rowCount * result->m_fieldCount; ++index)
            if (result->m_cells[index].m_bytes)
                XByteArray_delete_base(result->m_cells[index].m_bytes);
        XFree_System(result->m_cells);
    }
    if (result->m_fieldNames) {
        for (index = 0; index < result->m_fieldCount; ++index) {
            if (result->m_fieldNames[index]) XString_delete_base(result->m_fieldNames[index]);
            if (result->m_fieldTables[index]) XString_delete_base(result->m_fieldTables[index]);
            if (result->m_fieldDatabases[index]) XString_delete_base(result->m_fieldDatabases[index]);
        }
    }
    if (result->m_fields) XFree_System(result->m_fields);
    if (result->m_fieldNames) XFree_System(result->m_fieldNames);
    if (result->m_fieldTables) XFree_System(result->m_fieldTables);
    if (result->m_fieldDatabases) XFree_System(result->m_fieldDatabases);
    XFree_System(result);
}

static bool xmysql_parse_error(XSqlMySqlClient* client, const uint8_t* data, size_t size,
                               XSqlErrorType type)
{
    uint16_t code = 0;
    const char* text = "MySQL protocol error";
    size_t textOffset = 1;
    if (size > 2) code = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    if (size > 3 && data[3] == '#') textOffset = size > 9 ? 9 : size;
    else textOffset = size > 3 ? 3 : size;
    if (textOffset < size) text = (const char*)data + textOffset;
    xmysql_set_error_with_length(client, "MySQL server returned an error", text,
                                 textOffset < size ? size - textOffset : 0, code, type);
    return false;
}

static bool xmysql_native_password(const char* password, const uint8_t* scramble,
                                   size_t scrambleSize, uint8_t output[20])
{
    XByteArray* first = NULL;
    XByteArray* second = NULL;
    XByteArray* input = NULL;
    XByteArray* third = NULL;
    bool ok = false;
    size_t passwordSize = password ? strlen(password) : 0;
    first = XCryptographicHash_hash(password ? password : "", passwordSize,
                                    XCryptographicHash_Sha1);
    second = first ? XCryptographicHash_hash((const char*)XByteArray_data(first),
                                              XByteArray_size_base(first),
                                              XCryptographicHash_Sha1) : NULL;
    input = XByteArray_create();
    if (input && scramble && scrambleSize <= 20 && second
        && XByteArray_push_back_2(input, scramble, scrambleSize)
        && XByteArray_push_back_2(input, XByteArray_data(second), XByteArray_size_base(second)))
        third = XCryptographicHash_hash((const char*)XByteArray_data(input),
                                        XByteArray_size_base(input), XCryptographicHash_Sha1);
    if (third && first && second && XByteArray_size_base(first) == 20
        && XByteArray_size_base(second) == 20 && XByteArray_size_base(third) == 20) {
        size_t index;
        for (index = 0; index < 20; ++index)
            output[index] = XByteArray_data(third)[index] ^ XByteArray_data(first)[index];
        ok = true;
    }
    if (first) XByteArray_delete_base(first);
    if (second) XByteArray_delete_base(second);
    if (input) XByteArray_delete_base(input);
    if (third) XByteArray_delete_base(third);
    return ok;
}

static bool xmysql_caching_sha2_password(const char* password, const uint8_t* scramble,
                                         size_t scrambleSize, uint8_t output[32])
{
    XByteArray* first = NULL;
    XByteArray* second = NULL;
    XByteArray* input = NULL;
    XByteArray* third = NULL;
    bool ok = false;
    const char* text = password ? password : "";
    size_t length = strlen(text);
    first = XCryptographicHash_hash(text, length, XCryptographicHash_Sha256);
    second = first ? XCryptographicHash_hash((const char*)XByteArray_data(first),
                                             XByteArray_size_base(first),
                                             XCryptographicHash_Sha256) : NULL;
    input = XByteArray_create();
    if (input && first && scramble && scrambleSize <= 20
        && XByteArray_push_back_2(input, XByteArray_data(second), XByteArray_size_base(second))
        && XByteArray_push_back_2(input, scramble, scrambleSize))
        third = XCryptographicHash_hash((const char*)XByteArray_data(input),
                                        XByteArray_size_base(input),
                                        XCryptographicHash_Sha256);
    if (first && second && third && XByteArray_size_base(first) == 32
        && XByteArray_size_base(second) == 32 && XByteArray_size_base(third) == 32) {
        size_t index;
        for (index = 0; index < 32; ++index)
            output[index] = XByteArray_data(first)[index] ^ XByteArray_data(third)[index];
        ok = true;
    }
    if (first) XByteArray_delete_base(first);
    if (second) XByteArray_delete_base(second);
    if (input) XByteArray_delete_base(input);
    if (third) XByteArray_delete_base(third);
    return ok;
}

static bool xmysql_encrypt_caching_sha2_password(const char* password,
                                                 const uint8_t* scramble,
                                                 size_t scrambleSize,
                                                 const uint8_t* publicKey,
                                                 size_t publicKeySize,
                                                 XByteArray** encrypted)
{
    XByteArray* plain = NULL;
    size_t passwordSize;

    if (!password || !scramble || scrambleSize == 0 || !publicKey || publicKeySize == 0
        || !encrypted) return false;
    *encrypted = NULL;
    passwordSize = strlen(password) + 1;
    plain = XByteArray_create_with_data(password, passwordSize);
    if (!plain || !XByteArray_data(plain)) {
        if (plain) XByteArray_delete_base(plain);
        return false;
    }
    for (size_t i = 0; i < passwordSize; ++i)
        XByteArray_data(plain)[i] ^= scramble[i % scrambleSize];
    bool ok = XSsl_publicKeyEncrypt(
        (const uint8_t*)publicKey, publicKeySize, XSSL_Pem, XSSL_KeyAlgorithm_Rsa,
        (const uint8_t*)XByteArray_data(plain), XByteArray_size_base(plain), encrypted);
    XByteArray_delete_base(plain);
    return ok;
}

static bool xmysql_append_auth_token(XSqlMySqlClient* client, XByteArray* response,
                                     const char* password)
{
    uint8_t token[32];
    size_t tokenSize = 0;
    if (!response) return false;
    if (!password || !password[0]) return xmysql_append_u8(response, 0);
    if (strcmp(client->m_authPlugin, "caching_sha2_password") == 0) {
        if (!xmysql_caching_sha2_password(password, client->m_scramble,
                                          client->m_scrambleSize, token)) return false;
        tokenSize = 32;
    } else {
        if (!xmysql_native_password(password, client->m_scramble,
                                    client->m_scrambleSize, token)) return false;
        tokenSize = 20;
    }
    return xmysql_append_u8(response, (uint8_t)tokenSize)
        && XByteArray_push_back_2(response, token, tokenSize);
}

static bool xmysql_option_value(const char* options, const char* name,
                                char* output, size_t outputSize)
{
    const char* cursor = options;
    size_t nameLength;
    if (output && outputSize > 0) output[0] = 0;
    if (!options || !name || !output || outputSize == 0) return false;
    nameLength = strlen(name);
    while (*cursor) {
        const char* token = cursor;
        const char* end;
        const char* equals;
        while (*token == ';' || *token == ',' || *token == ' ' || *token == '\t') ++token;
        end = token;
        while (*end && *end != ';' && *end != ',') ++end;
        equals = memchr(token, '=', (size_t)(end - token));
        if (equals) {
            const char* keyEnd = equals;
            const char* value = equals + 1;
            size_t valueLength;
            while (keyEnd > token && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) --keyEnd;
            while (value < end && (*value == ' ' || *value == '\t')) ++value;
            valueLength = (size_t)(end - value);
            while (valueLength > 0 && (value[valueLength - 1] == ' '
                                       || value[valueLength - 1] == '\t')) --valueLength;
            if ((size_t)(keyEnd - token) == nameLength
                && strncasecmp(token, name, nameLength) == 0) {
                if (valueLength >= outputSize) valueLength = outputSize - 1;
                memcpy(output, value, valueLength);
                output[valueLength] = 0;
                return true;
            }
        } else if ((size_t)(end - token) == nameLength
                   && strncasecmp(token, name, nameLength) == 0) {
            if (outputSize > 1) {
                output[0] = '1';
                output[1] = 0;
            }
            return true;
        }
        cursor = *end ? end + 1 : end;
    }
    return false;
}

static bool xmysql_option_value_alias(const char* options, const char* first,
                                      const char* second, char* output, size_t outputSize)
{
    return xmysql_option_value(options, first, output, outputSize)
        || xmysql_option_value(options, second, output, outputSize);
}

static bool xmysql_option_enabled(const char* options, const char* name, bool defaultValue)
{
    char value[32];
    if (!xmysql_option_value(options, name, value, sizeof(value))) return defaultValue;
    if (!value[0]) return true;
    return xmysql_ascii_casecmp(value, "1") == 0 || xmysql_ascii_casecmp(value, "true") == 0
        || xmysql_ascii_casecmp(value, "yes") == 0 || xmysql_ascii_casecmp(value, "on") == 0
        || xmysql_ascii_casecmp(value, "required") == 0 || xmysql_ascii_casecmp(value, "verify_ca") == 0
        || xmysql_ascii_casecmp(value, "verify_identity") == 0;
}

static XSslProtocol xmysql_tls_protocol(const char* value)
{
    if (!value) return XSSL_SecureProtocols;
    if (xmysql_ascii_casecmp(value, "TLSv1.0") == 0 || xmysql_ascii_casecmp(value, "TLSv1") == 0)
        return XSSL_TlsV1_0;
    if (xmysql_ascii_casecmp(value, "TLSv1.1") == 0)
        return XSSL_TlsV1_1;
    if (xmysql_ascii_casecmp(value, "TLSv1.2") == 0)
        return XSSL_TlsV1_2;
    if (xmysql_ascii_casecmp(value, "TLSv1.3") == 0)
        return XSSL_TlsV1_3;
    return XSSL_SecureProtocols;
}

static int xmysql_option_seconds_ms(const char* options, const char* name, int defaultValue)
{
    char value[32];
    char* end = NULL;
    long seconds;
    if (!xmysql_option_value(options, name, value, sizeof(value)) || !value[0])
        return defaultValue;
    seconds = strtol(value, &end, 10);
    if (!end || *end || seconds < 0) return defaultValue;
    if (seconds > INT_MAX / 1000) return INT_MAX;
    return (int)seconds * 1000;
}

static bool xmysql_parse_handshake(XSqlMySqlClient* client, XByteArray* payload,
                                   const char* database, const char* user,
                                   const char* password, uint8_t* responseSequence)
{
    const uint8_t* data = XByteArray_data(payload);
    const uint8_t* end = data + XByteArray_size_base(payload);
    const uint8_t* cursor = data;
    const uint8_t* pluginData;
    const uint8_t* pluginName;
    size_t pluginSize;
    uint64_t value;
    uint16_t capabilitiesLow;
    uint16_t capabilitiesHigh;
    uint8_t authLength = 0;
    uint32_t clientCapabilities;
    XByteArray* response = NULL;
    if (!client || !payload || !data || XByteArray_size_base(payload) < 5 || *cursor++ != 0x0au)
        return false;
    while (cursor < end && *cursor) ++cursor;
    if (cursor >= end || (size_t)(end - cursor) < 15) return false;
    ++cursor;
    cursor += 4;
    pluginData = cursor;
    cursor += 8;
    ++cursor;
    capabilitiesLow = (uint16_t)cursor[0] | ((uint16_t)cursor[1] << 8);
    cursor += 2;
    if (cursor >= end) return false;
    ++cursor;
    cursor += 2;
    capabilitiesHigh = (uint16_t)cursor[0] | ((uint16_t)cursor[1] << 8);
    client->m_serverCapabilities = (uint32_t)capabilitiesLow
        | ((uint32_t)capabilitiesHigh << 16);
    cursor += 2;
    if (cursor >= end) return false;
    authLength = *cursor++;
    if ((size_t)(end - cursor) < 10) return false;
    cursor += 10;
    client->m_scrambleSize = 8;
    memcpy(client->m_scramble, pluginData, 8);
    if (client->m_serverCapabilities & XMYSQL_CLIENT_SECURE_CONNECTION) {
        size_t secondSize = authLength > 8 ? (size_t)authLength - 8 : 13;
        if (secondSize > (size_t)(end - cursor)) secondSize = (size_t)(end - cursor);
        if (secondSize > 0 && cursor[secondSize - 1] == 0) --secondSize;
        if (secondSize > sizeof(client->m_scramble) - 8)
            secondSize = sizeof(client->m_scramble) - 8;
        memcpy(client->m_scramble + 8, cursor, secondSize);
        client->m_scrambleSize += secondSize;
        cursor += authLength > 8 ? (size_t)authLength - 8 : 13;
        if (cursor > end) cursor = end;
    }
    pluginName = cursor;
    pluginSize = 0;
    while (pluginName + pluginSize < end && pluginName[pluginSize]) ++pluginSize;
    if (pluginSize == 0) {
        pluginName = (const uint8_t*)"mysql_native_password";
        pluginSize = strlen("mysql_native_password");
    }
    if (pluginSize >= sizeof(client->m_authPlugin)) {
        xmysql_set_error(client, "MySQL authentication plugin name is too long", "", 0,
                         XSqlErrorType_ConnectionError);
        return false;
    }
    memcpy(client->m_authPlugin, pluginName, pluginSize);
    client->m_authPlugin[pluginSize] = 0;
    if (strcmp(client->m_authPlugin, "mysql_native_password") != 0
        && strcmp(client->m_authPlugin, "caching_sha2_password") != 0) {
        xmysql_set_error(client, "Unsupported MySQL authentication plugin",
                         client->m_authPlugin, 0, XSqlErrorType_ConnectionError);
        return false;
    }
    clientCapabilities = XMYSQL_CLIENT_LONG_PASSWORD | XMYSQL_CLIENT_PROTOCOL_41
        | XMYSQL_CLIENT_TRANSACTIONS | XMYSQL_CLIENT_SECURE_CONNECTION;
    clientCapabilities |= client->m_clientFlags;
    if (client->m_localInfile && (client->m_serverCapabilities & XMYSQL_CLIENT_LOCAL_FILES))
        clientCapabilities |= XMYSQL_CLIENT_LOCAL_FILES;
    if (client->m_compress && (client->m_serverCapabilities & XMYSQL_CLIENT_COMPRESS))
        clientCapabilities |= XMYSQL_CLIENT_COMPRESS;
    if (database && database[0]) clientCapabilities |= XMYSQL_CLIENT_CONNECT_WITH_DB;
    if (client->m_serverCapabilities & XMYSQL_CLIENT_PLUGIN_AUTH)
        clientCapabilities |= XMYSQL_CLIENT_PLUGIN_AUTH;
    if (client->m_multiStatements && (client->m_serverCapabilities & XMYSQL_CLIENT_MULTI_RESULTS))
        clientCapabilities |= XMYSQL_CLIENT_MULTI_RESULTS | XMYSQL_CLIENT_PS_MULTI_RESULTS;
    if (client->m_multiStatements && (client->m_serverCapabilities & XMYSQL_CLIENT_MULTI_STATEMENTS))
        clientCapabilities |= XMYSQL_CLIENT_MULTI_STATEMENTS;
    if (client->m_useTls && (client->m_serverCapabilities & XMYSQL_CLIENT_SSL))
        clientCapabilities |= XMYSQL_CLIENT_SSL;
    else if (client->m_useTls && client->m_tlsMode == XMySqlTlsPreferred)
        client->m_useTls = false;
    response = XByteArray_create();
    if (!response || !xmysql_append_u32(response, clientCapabilities)
        || !xmysql_append_u32(response, XMYSQL_MAX_PACKET_SIZE)
        || !xmysql_append_u8(response, 45)
        || !XByteArray_resize_base(response, XByteArray_size_base(response) + 23)) goto fail;
    memset(XByteArray_data(response) + XByteArray_size_base(response) - 23, 0, 23);
    if (client->m_useTls) {
        if (!(client->m_serverCapabilities & XMYSQL_CLIENT_SSL)) {
            xmysql_set_error(client, "MySQL server does not advertise TLS support", "", 0,
                             XSqlErrorType_ConnectionError);
            goto fail;
        }
        /* SSLRequest is exactly the fixed 32-byte capability prefix. */
        if (!xmysql_client_send_packet(client, XByteArray_data(response),
                                XByteArray_size_base(response), 1)) goto fail;
        XByteArray_delete_base(response);
        response = NULL;
        XSslSocket_startClientEncryption((XSslSocket*)client->m_socket);
        if (!XSslSocket_waitForEncrypted((XSslSocket*)client->m_socket, 30000)) {
            XString* sslError = XAbstractSocket_errorString((XAbstractSocket*)client->m_socket);
            xmysql_set_error(client, "Unable to establish MySQL TLS connection",
                             sslError ? XString_toUtf8(sslError) : "", 0,
                             XSqlErrorType_ConnectionError);
            if (sslError) XString_delete_base(sslError);
            goto fail;
        }
        response = XByteArray_create();
        if (!response || !xmysql_append_u32(response, clientCapabilities)
            || !xmysql_append_u32(response, XMYSQL_MAX_PACKET_SIZE)
            || !xmysql_append_u8(response, 45)
            || !XByteArray_resize_base(response, XByteArray_size_base(response) + 23)) goto fail;
        memset(XByteArray_data(response) + XByteArray_size_base(response) - 23, 0, 23);
        if (!xmysql_append_cstring(response, user ? user : "")
            || !xmysql_append_auth_token(client, response, password)) goto fail;
    } else if (!xmysql_append_cstring(response, user ? user : "")
               || !xmysql_append_auth_token(client, response, password)) goto fail;
    if (database && database[0] && !xmysql_append_cstring(response, database)) goto fail;
    if (clientCapabilities & XMYSQL_CLIENT_PLUGIN_AUTH
        && !xmysql_append_cstring(response, client->m_authPlugin)) goto fail;
    if (!xmysql_client_send_packet(client, XByteArray_data(response),
                            XByteArray_size_base(response), client->m_useTls ? 2 : 1)) goto fail;
    XByteArray_delete_base(response);
    response = NULL;
    if (responseSequence) *responseSequence = 2;
    return true;
fail:
    if (response) XByteArray_delete_base(response);
    if (client && client->m_errorType == XSqlErrorType_NoError)
        xmysql_set_error(client, "Unable to send MySQL handshake response", "", 0,
                         XSqlErrorType_ConnectionError);
    return false;
}

static bool xmysql_client_handshake(XSqlMySqlClient* client, const char* database,
                                    const char* user, const char* password)
{
    XByteArray* payload = NULL;
    XByteArray* authResponse = NULL;
    uint8_t sequence = 0;
    bool ok = false;
    if (!xmysql_client_read_packet(client, &payload, &sequence)) goto done;
    if (!payload || XByteArray_size_base(payload) == 0) goto done;
    if (XByteArray_data(payload)[0] == 0xffu) {
        xmysql_parse_error(client, XByteArray_data(payload), XByteArray_size_base(payload),
                            XSqlErrorType_ConnectionError);
        goto done;
    }
    if (!xmysql_parse_handshake(client, payload, database, user, password, &sequence)) goto done;
    XByteArray_delete_base(payload);
    payload = NULL;
    if (!xmysql_client_read_packet(client, &authResponse, &sequence)) goto done;
    if (XByteArray_size_base(authResponse) > 0 && XByteArray_data(authResponse)[0] == 0x00u) {
        ok = true;
    } else if (XByteArray_size_base(authResponse) >= 2
               && XByteArray_data(authResponse)[0] == 0x01u
               && strcmp(client->m_authPlugin, "caching_sha2_password") == 0) {
        uint8_t authState = XByteArray_data(authResponse)[1];
        if (authState == 0x03u) {
            ok = true;
        } else if (authState == 0x04u) {
            XByteArray* fullAuth = XByteArray_create();
            XByteArray* finalResponse = NULL;
            if (client->m_useTls && fullAuth && xmysql_append_cstring(fullAuth, password ? password : "")
                && xmysql_client_send_packet(client, XByteArray_data(fullAuth),
                                      XByteArray_size_base(fullAuth), (uint8_t)(sequence + 1))
                && xmysql_client_read_packet(client, &finalResponse, &sequence)) {
                if (XByteArray_size_base(finalResponse) > 0
                    && XByteArray_data(finalResponse)[0] == 0x00u) ok = true;
                else if (XByteArray_size_base(finalResponse) > 0
                         && XByteArray_data(finalResponse)[0] == 0xffu)
                    xmysql_parse_error(client, XByteArray_data(finalResponse),
                                       XByteArray_size_base(finalResponse),
                                       XSqlErrorType_ConnectionError);
            } else if (!client->m_useTls) {
                XByteArray* publicKeyRequest = XByteArray_create();
                XByteArray* publicKeyResponse = NULL;
                XByteArray* encryptedPassword = NULL;
                bool rsaOk = publicKeyRequest
                    && XByteArray_push_back_1(publicKeyRequest, 0x02u)
                    && xmysql_client_send_packet(client,
                                          XByteArray_data(publicKeyRequest),
                                          XByteArray_size_base(publicKeyRequest),
                                          (uint8_t)(sequence + 1))
                    && xmysql_client_read_packet(client, &publicKeyResponse, &sequence)
                    && XByteArray_size_base(publicKeyResponse) > 1
                    && XByteArray_data(publicKeyResponse)[0] == 0x01u
                    && xmysql_encrypt_caching_sha2_password(
                        password, client->m_scramble, client->m_scrambleSize,
                        XByteArray_data(publicKeyResponse) + 1,
                        XByteArray_size_base(publicKeyResponse) - 1,
                        &encryptedPassword)
                    && xmysql_client_send_packet(client,
                                          XByteArray_data(encryptedPassword),
                                          XByteArray_size_base(encryptedPassword),
                                          (uint8_t)(sequence + 1))
                    && xmysql_client_read_packet(client, &finalResponse, &sequence);
                if (rsaOk && XByteArray_size_base(finalResponse) > 0
                    && XByteArray_data(finalResponse)[0] == 0x00u) {
                    ok = true;
                } else if (finalResponse && XByteArray_size_base(finalResponse) > 0
                           && XByteArray_data(finalResponse)[0] == 0xffu) {
                    xmysql_parse_error(client, XByteArray_data(finalResponse),
                                       XByteArray_size_base(finalResponse),
                                       XSqlErrorType_ConnectionError);
                } else {
                    xmysql_set_error(client,
                                     "Unable to complete caching_sha2_password RSA authentication",
                                     "The MySQL server did not return a usable RSA public key",
                                     0, XSqlErrorType_ConnectionError);
                }
                if (publicKeyRequest) XByteArray_delete_base(publicKeyRequest);
                if (publicKeyResponse) XByteArray_delete_base(publicKeyResponse);
                if (encryptedPassword) XByteArray_delete_base(encryptedPassword);
                if (finalResponse) XByteArray_delete_base(finalResponse);
            }
            if (fullAuth) XByteArray_delete_base(fullAuth);
            if (finalResponse) XByteArray_delete_base(finalResponse);
        } else {
            xmysql_set_error(client, "Unsupported caching_sha2_password authentication state",
                             "", authState, XSqlErrorType_ConnectionError);
        }
    } else if (XByteArray_size_base(authResponse) > 0
               && XByteArray_data(authResponse)[0] == 0xffu) {
        xmysql_parse_error(client, XByteArray_data(authResponse),
                           XByteArray_size_base(authResponse), XSqlErrorType_ConnectionError);
    } else {
        xmysql_set_error(client, "Unsupported MySQL authentication exchange", "", 0,
                         XSqlErrorType_ConnectionError);
    }
done:
    if (payload) XByteArray_delete_base(payload);
    if (authResponse) XByteArray_delete_base(authResponse);
    if (ok) {
        client->m_compressionActive = client->m_compress
            && (client->m_serverCapabilities & XMYSQL_CLIENT_COMPRESS) != 0;
        client->m_compressedSendSequence = 0;
        client->m_compressedReadSequence = 0;
        xmysql_compression_clear_buffer(client);
    }
    return ok;
}

static XSqlMySqlClient* xmysql_client_create(void)
{
    return (XSqlMySqlClient*)XCalloc_System(1, sizeof(XSqlMySqlClient));
}

static void xmysql_client_close(XSqlMySqlClient* client)
{
    if (!client) return;
    XSqlMySqlSharedMemory_close(client->m_sharedMemory);
    client->m_sharedMemory = NULL;
    client->m_useSharedMemory = false;
    if (client->m_socket) {
        XTcpSocket_disconnectFromHost_base(client->m_socket);
        XClass_delete_base((XClass*)client->m_socket);
        client->m_socket = NULL;
    }
    if (client->m_caCertificate) {
        XSsl_certificateDestroy(client->m_caCertificate);
        client->m_caCertificate = NULL;
    }
    xmysql_compression_clear_buffer(client);
    client->m_compressionActive = false;
    client->m_compressedSendSequence = 0;
    client->m_compressedReadSequence = 0;
    client->m_open = false;
}

static void xmysql_client_destroy(XSqlMySqlClient* client)
{
    if (!client) return;
    xmysql_client_close(client);
    xmysql_clear_string(&client->m_driverText);
    xmysql_clear_string(&client->m_databaseText);
    xmysql_clear_string(&client->m_errorCode);
    xmysql_clear_string(&client->m_connectionDatabase);
    xmysql_clear_string(&client->m_connectionUser);
    xmysql_clear_string(&client->m_connectionPassword);
    xmysql_clear_string(&client->m_connectionHost);
    xmysql_clear_string(&client->m_connectionOptions);
    XFree_System(client);
}

static bool xmysql_client_open(XSqlMySqlClient* client, const char* database,
                               const char* user, const char* password,
                               const char* host, int port, const char* options)
{
    XString* socketError = NULL;
    char caPath[512];
    char sslCaPath[512];
    char sslCipher[512];
    char sslCrl[512];
    char sslCrlPath[512];
    char sslMode[32];
    char tlsVersion[64];
    char protocol[32];
    char unixSocket[XMYSQL_UNIX_SOCKET_PATH_MAX];
    char namedPipe[XMYSQL_UNIX_SOCKET_PATH_MAX];
    char sharedMemoryBaseName[XMYSQL_UNIX_SOCKET_PATH_MAX];
    char sslPath[512];
    XString* sslPathString = NULL;
    XString* unixSocketText = NULL;
    XString* namedPipeText = NULL;
    bool useUnixSocket = false;
    bool useDefaultUnixSocket = false;
    bool useNamedPipe = false;
    bool useDefaultNamedPipe = false;
    bool useSharedMemory = false;
    bool sharedMemoryBaseNameSpecified = false;
    bool protocolSpecified = false;
    if (!client) return false;
    if (!client->m_reconnecting
        && !xmysql_store_connection_parameters(client, database, user, password,
                                               host, port, options)) {
        xmysql_set_error(client, "Unable to store MySQL connection parameters", "", 0,
                         XSqlErrorType_ConnectionError);
        return false;
    }
    xmysql_client_close(client);
    xmysql_clear_error(client);
    client->m_tlsMode = XMySqlTlsDisabled;
    client->m_tlsModeSpecified = false;
    client->m_tlsProtocol = XSSL_SecureProtocols;
    client->m_clientFlags = 0;
    client->m_preparedQueries = false;
    client->m_compress = xmysql_option_enabled(options, "CLIENT_COMPRESS", false)
        || xmysql_option_enabled(options, "MYSQL_OPT_COMPRESS", false);
    client->m_reconnect = xmysql_option_enabled(options, "MYSQL_OPT_RECONNECT", false);
    client->m_compressionActive = false;
    client->m_compressedSendSequence = 0;
    client->m_compressedReadSequence = 0;
    client->m_connectTimeout = xmysql_option_seconds_ms(options, "MYSQL_OPT_CONNECT_TIMEOUT", 30000);
    client->m_readTimeout = xmysql_option_seconds_ms(options, "MYSQL_OPT_READ_TIMEOUT", 30000);
    client->m_writeTimeout = xmysql_option_seconds_ms(options, "MYSQL_OPT_WRITE_TIMEOUT", 30000);
    unixSocket[0] = '\0';
    namedPipe[0] = '\0';
    sharedMemoryBaseName[0] = '\0';
    if (xmysql_option_enabled(options, "CLIENT_FOUND_ROWS", false))
        client->m_clientFlags |= XMYSQL_CLIENT_FOUND_ROWS;
    if (xmysql_option_enabled(options, "CLIENT_NO_SCHEMA", false))
        client->m_clientFlags |= XMYSQL_CLIENT_NO_SCHEMA;
    if (xmysql_option_enabled(options, "CLIENT_IGNORE_SPACE", false))
        client->m_clientFlags |= XMYSQL_CLIENT_IGNORE_SPACE;
    if (xmysql_option_enabled(options, "CLIENT_INTERACTIVE", false))
        client->m_clientFlags |= XMYSQL_CLIENT_INTERACTIVE;
    if (xmysql_option_enabled(options, "CLIENT_ODBC", false))
        client->m_clientFlags |= XMYSQL_CLIENT_ODBC;
    if (xmysql_option_value_alias(options, "SSL_MODE", "MYSQL_OPT_SSL_MODE",
                                  sslMode, sizeof(sslMode))) {
        client->m_tlsModeSpecified = true;
        if (xmysql_ascii_casecmp(sslMode, "disabled") == 0 || xmysql_ascii_casecmp(sslMode, "SSL_MODE_DISABLED") == 0)
            client->m_tlsMode = XMySqlTlsDisabled;
        else if (xmysql_ascii_casecmp(sslMode, "preferred") == 0 || xmysql_ascii_casecmp(sslMode, "SSL_MODE_PREFERRED") == 0)
            client->m_tlsMode = XMySqlTlsPreferred;
        else if (xmysql_ascii_casecmp(sslMode, "required") == 0 || xmysql_ascii_casecmp(sslMode, "SSL_MODE_REQUIRED") == 0)
            client->m_tlsMode = XMySqlTlsRequired;
        else if (xmysql_ascii_casecmp(sslMode, "verify_ca") == 0 || xmysql_ascii_casecmp(sslMode, "SSL_MODE_VERIFY_CA") == 0)
            client->m_tlsMode = XMySqlTlsVerifyCa;
        else if (xmysql_ascii_casecmp(sslMode, "verify_identity") == 0 || xmysql_ascii_casecmp(sslMode, "SSL_MODE_VERIFY_IDENTITY") == 0)
            client->m_tlsMode = XMySqlTlsVerifyIdentity;
    }
    client->m_useTls = client->m_tlsModeSpecified
        ? client->m_tlsMode != XMySqlTlsDisabled
        : (xmysql_option_enabled(options, "SSL", false)
           || xmysql_option_value_alias(options, "SSL_CA", "MYSQL_OPT_SSL_CA",
                                        caPath, sizeof(caPath))
           || xmysql_option_value_alias(options, "SSL_CERT", "MYSQL_OPT_SSL_CERT",
                                        sslPath, sizeof(sslPath))
           || xmysql_option_value_alias(options, "SSL_KEY", "MYSQL_OPT_SSL_KEY",
                                        sslPath, sizeof(sslPath))
           || xmysql_option_value_alias(options, "SSL_CIPHER", "MYSQL_OPT_SSL_CIPHER",
                                        sslCipher, sizeof(sslCipher))
           || xmysql_option_value_alias(options, "SSL_CRL", "MYSQL_OPT_SSL_CRL",
                                        sslCrl, sizeof(sslCrl))
           || xmysql_option_value_alias(options, "SSL_CRLPATH", "MYSQL_OPT_SSL_CRLPATH",
                                        sslCrlPath, sizeof(sslCrlPath))
           || xmysql_option_value_alias(options, "SSL_CAPATH", "MYSQL_OPT_SSL_CAPATH",
                                        sslCaPath, sizeof(sslCaPath)));
    if (client->m_useTls && client->m_tlsMode == XMySqlTlsDisabled)
        client->m_tlsMode = XMySqlTlsRequired;
    client->m_verifyPeer = client->m_tlsMode == XMySqlTlsVerifyCa
        || client->m_tlsMode == XMySqlTlsVerifyIdentity
        || xmysql_option_enabled(options, "SSL_VERIFY_SERVER_CERT", true);
    if (xmysql_option_value(options, "MYSQL_OPT_TLS_VERSION", tlsVersion, sizeof(tlsVersion)))
        client->m_tlsProtocol = xmysql_tls_protocol(tlsVersion);
    if (xmysql_option_value(options, "MYSQL_OPT_PROTOCOL", protocol, sizeof(protocol))) {
        protocolSpecified = true;
        if (xmysql_ascii_casecmp(protocol, "TCP") == 0
            || xmysql_ascii_casecmp(protocol, "MYSQL_PROTOCOL_TCP") == 0
            || xmysql_ascii_casecmp(protocol, "DEFAULT") == 0
            || xmysql_ascii_casecmp(protocol, "MYSQL_PROTOCOL_DEFAULT") == 0) {
            useUnixSocket = false;
        } else if (xmysql_ascii_casecmp(protocol, "SOCKET") == 0
                   || xmysql_ascii_casecmp(protocol, "MYSQL_PROTOCOL_SOCKET") == 0) {
            useUnixSocket = true;
            useDefaultUnixSocket = true;
        } else if (xmysql_ascii_casecmp(protocol, "PIPE") == 0
                   || xmysql_ascii_casecmp(protocol, "MYSQL_PROTOCOL_PIPE") == 0) {
#if defined(_WIN32)
            useNamedPipe = true;
            useDefaultNamedPipe = true;
#else
            xmysql_set_error(client, "MySQL named pipes are only available on Windows", protocol,
                             0, XSqlErrorType_ConnectionError);
            return false;
#endif
        } else if (xmysql_ascii_casecmp(protocol, "MEMORY") == 0
                   || xmysql_ascii_casecmp(protocol, "MYSQL_PROTOCOL_MEMORY") == 0) {
#if defined(_WIN32)
            useSharedMemory = true;
#else
            xmysql_set_error(client, "MySQL shared memory is only available on Windows", protocol,
                             0, XSqlErrorType_ConnectionError);
            return false;
#endif
        } else {
            /* QMYSQL delegates this option to libmysql.  Its protocol parser
             * warns then falls back to MYSQL_PROTOCOL_DEFAULT. */
            protocolSpecified = false;
        }
    }
#if !defined(_WIN32)
    if ((!protocolSpecified || xmysql_ascii_casecmp(protocol, "DEFAULT") == 0
         || xmysql_ascii_casecmp(protocol, "MYSQL_PROTOCOL_DEFAULT") == 0)
        && (!host || !host[0])) {
        useUnixSocket = true;
        useDefaultUnixSocket = true;
    }
#endif
#if defined(_WIN32)
    if ((!protocolSpecified || xmysql_ascii_casecmp(protocol, "DEFAULT") == 0
         || xmysql_ascii_casecmp(protocol, "MYSQL_PROTOCOL_DEFAULT") == 0)
        && host && strcmp(host, ".") == 0) {
        useNamedPipe = true;
        useDefaultNamedPipe = true;
    }
#endif
    if (xmysql_option_value(options, "UNIX_SOCKET", unixSocket, sizeof(unixSocket))) {
#if defined(_WIN32)
        if (useNamedPipe || !protocolSpecified) {
            if (snprintf(namedPipe, sizeof(namedPipe), "%s", unixSocket)
                >= (int)sizeof(namedPipe)) {
                xmysql_set_error(client, "MySQL named pipe path is too long", unixSocket, 0,
                                 XSqlErrorType_ConnectionError);
                return false;
            }
            useNamedPipe = true;
            useDefaultNamedPipe = false;
        }
#else
        useUnixSocket = true;
        useDefaultUnixSocket = false;
#endif
    }
    sharedMemoryBaseNameSpecified = xmysql_option_value(options,
                                                         "MYSQL_SHARED_MEMORY_BASE_NAME",
                                                         sharedMemoryBaseName,
                                                         sizeof(sharedMemoryBaseName));
    if (useUnixSocket && unixSocket[0] == '\0') {
        if (!useDefaultUnixSocket
            || snprintf(unixSocket, sizeof(unixSocket), "%s", XMYSQL_DEFAULT_UNIX_SOCKET)
                   >= (int)sizeof(unixSocket)) {
            xmysql_set_error(client, "UNIX_SOCKET requires a socket path", "", 0,
                             XSqlErrorType_ConnectionError);
            return false;
        }
    }
    if (useNamedPipe && namedPipe[0] == '\0') {
        if (!useDefaultNamedPipe
            || snprintf(namedPipe, sizeof(namedPipe), "%s", XMYSQL_DEFAULT_NAMED_PIPE)
                   >= (int)sizeof(namedPipe)) {
            xmysql_set_error(client, "MYSQL_PROTOCOL_PIPE requires a pipe name", "", 0,
                             XSqlErrorType_ConnectionError);
            return false;
        }
    }
    if (useSharedMemory && !sharedMemoryBaseNameSpecified
        && snprintf(sharedMemoryBaseName, sizeof(sharedMemoryBaseName), "%s",
                    XMYSQL_DEFAULT_SHARED_MEMORY_BASE_NAME)
               >= (int)sizeof(sharedMemoryBaseName)) {
        xmysql_set_error(client, "MYSQL_SHARED_MEMORY_BASE_NAME is too long", "", 0,
                         XSqlErrorType_ConnectionError);
        return false;
    }
    if ((useNamedPipe || useSharedMemory) && client->m_useTls) {
        if (client->m_tlsMode == XMySqlTlsRequired
            || client->m_tlsMode == XMySqlTlsVerifyCa
            || client->m_tlsMode == XMySqlTlsVerifyIdentity) {
            xmysql_set_error(client, "MySQL local transport does not support TLS", protocol, 0,
                             XSqlErrorType_ConnectionError);
            return false;
        }
        client->m_useTls = false;
        client->m_verifyPeer = false;
    }
    /* Qt QMYSQL enables CLIENT_MULTI_STATEMENTS during mysql_real_connect so
     * stored procedures can return more than one result set. Keep the same
     * default while allowing an explicit 0/false option to disable it. */
    client->m_multiStatements = xmysql_option_enabled(options, "CLIENT_MULTI_STATEMENTS", true)
        && xmysql_option_enabled(options, "MULTI_STATEMENTS", true);
    client->m_localInfile = xmysql_option_enabled(options, "MYSQL_OPT_LOCAL_INFILE", false);
    client->m_socket = (XTcpSocket*)XSslSocket_create();
    if (!client->m_socket) {
        xmysql_set_error(client, "Unable to create MySQL socket", "", 0,
                         XSqlErrorType_ConnectionError);
        return false;
    }
    if (client->m_useTls) {
        if (xmysql_option_value_alias(options, "SSL_CERT", "MYSQL_OPT_SSL_CERT",
                                      sslPath, sizeof(sslPath))) {
            sslPathString = XString_create_utf8(sslPath);
            if (!sslPathString) goto tls_option_fail;
            XSslSocket_setLocalCertificate_2((XSslSocket*)client->m_socket,
                                             sslPathString, XSSL_Pem);
            XString_delete_base(sslPathString);
            sslPathString = NULL;
            if (!XSslSocket_localCertificate((XSslSocket*)client->m_socket)) goto tls_option_fail;
        }
        if (xmysql_option_value_alias(options, "SSL_KEY", "MYSQL_OPT_SSL_KEY",
                                      sslPath, sizeof(sslPath))) {
            sslPathString = XString_create_utf8(sslPath);
            if (!sslPathString) goto tls_option_fail;
            XSslSocket_setPrivateKey_2((XSslSocket*)client->m_socket,
                                       sslPathString, XSSL_KeyAlgorithm_Rsa,
                                       XSSL_Pem, NULL);
            XString_delete_base(sslPathString);
            sslPathString = NULL;
            if (!XSslSocket_privateKey((XSslSocket*)client->m_socket)) goto tls_option_fail;
        }
        if (xmysql_option_value_alias(options, "SSL_CIPHER", "MYSQL_OPT_SSL_CIPHER",
                                      sslCipher, sizeof(sslCipher))) {
            XString* cipherText = XString_create_utf8(sslCipher);
            if (!cipherText
                || !XSslSocket_setCipherSuites((XSslSocket*)client->m_socket, cipherText)) {
                if (cipherText) XString_delete_base(cipherText);
                goto tls_option_fail;
            }
            XString_delete_base(cipherText);
        }
        if (xmysql_option_value_alias(options, "SSL_CRL", "MYSQL_OPT_SSL_CRL",
                                      sslCrl, sizeof(sslCrl))) {
            XString* crlText = XString_create_utf8(sslCrl);
            if (!crlText
                || !XSslSocket_setCrlFile((XSslSocket*)client->m_socket, crlText)) {
                if (crlText) XString_delete_base(crlText);
                goto tls_option_fail;
            }
            XString_delete_base(crlText);
        }
        if (xmysql_option_value_alias(options, "SSL_CRLPATH", "MYSQL_OPT_SSL_CRLPATH",
                                      sslCrlPath, sizeof(sslCrlPath))) {
            XString* crlPathText = XString_create_utf8(sslCrlPath);
            if (!crlPathText
                || !XSslSocket_setCrlPath((XSslSocket*)client->m_socket, crlPathText)) {
                if (crlPathText) XString_delete_base(crlPathText);
                goto tls_option_fail;
            }
            XString_delete_base(crlPathText);
        }
        XSslSocket_setProtocol((XSslSocket*)client->m_socket, client->m_tlsProtocol);
        XString* peer = XString_create_utf8(host && host[0] ? host : "127.0.0.1");
        XSslSocket_setPeerVerifyMode((XSslSocket*)client->m_socket,
                                      client->m_verifyPeer ? XSSL_VerifyPeer : XSSL_VerifyNone);
        if (peer) {
            XSslSocket_setPeerVerifyName((XSslSocket*)client->m_socket, peer);
            XString_delete_base(peer);
        }
        if (xmysql_option_value_alias(options, "SSL_CA", "MYSQL_OPT_SSL_CA",
                                      caPath, sizeof(caPath))) {
            client->m_caCertificate = XSsl_certificateLoad(caPath, XSSL_Pem);
            if (client->m_caCertificate) {
                XSslSocket_addCaCertificate((XSslSocket*)client->m_socket,
                                            client->m_caCertificate);
            } else {
                XString* caPathText = XString_create_utf8(caPath);
                if (!caPathText
                    || !XSslSocket_setCaPath((XSslSocket*)client->m_socket, caPathText)) {
                    if (caPathText) XString_delete_base(caPathText);
                    xmysql_set_error(client, "Unable to configure MySQL TLS CA path", caPath, 0,
                                     XSqlErrorType_ConnectionError);
                    xmysql_client_close(client);
                    return false;
                }
                XString_delete_base(caPathText);
            }
        }
        if (xmysql_option_value_alias(options, "SSL_CAPATH", "MYSQL_OPT_SSL_CAPATH",
                                      sslCaPath, sizeof(sslCaPath))) {
            XString* caPathText = XString_create_utf8(sslCaPath);
            if (!caPathText
                || !XSslSocket_setCaPath((XSslSocket*)client->m_socket, caPathText)) {
                if (caPathText) XString_delete_base(caPathText);
                xmysql_set_error(client, "Unable to configure MySQL TLS CA directory", sslCaPath, 0,
                                 XSqlErrorType_ConnectionError);
                xmysql_client_close(client);
                return false;
            }
            XString_delete_base(caPathText);
        }
    }
    if (useSharedMemory) {
        client->m_sharedMemory = XSqlMySqlSharedMemory_open(
            sharedMemoryBaseName, client->m_connectTimeout);
        if (!client->m_sharedMemory) {
            xmysql_set_error(client, "Unable to connect to MySQL shared memory",
                             sharedMemoryBaseName, 0, XSqlErrorType_ConnectionError);
            xmysql_client_close(client);
            return false;
        }
        client->m_useSharedMemory = true;
    } else if (useNamedPipe) {
        namedPipeText = XString_create_utf8(namedPipe);
        if (!namedPipeText
            || !XAbstractSocket_connectLocalStream_private((XAbstractSocket*)client->m_socket,
                                                           namedPipeText,
                                                           XNetwork_LocalStream_NamedPipe,
                                                           client->m_connectTimeout)) {
            if (namedPipeText) XString_delete_base(namedPipeText);
            xmysql_set_error(client, "Unable to connect to MySQL named pipe", namedPipe, 0,
                             XSqlErrorType_ConnectionError);
            xmysql_client_close(client);
            return false;
        }
        XString_delete_base(namedPipeText);
        namedPipeText = NULL;
    } else if (useUnixSocket) {
        unixSocketText = XString_create_utf8(unixSocket);
        if (!unixSocketText
            || !XAbstractSocket_connectLocalStream_private((XAbstractSocket*)client->m_socket,
                                                            unixSocketText,
                                                            XNetwork_LocalStream_UnixSocket,
                                                            client->m_connectTimeout)) {
            if (unixSocketText) XString_delete_base(unixSocketText);
            xmysql_set_error(client, "Unable to connect to MySQL Unix socket", unixSocket, 0,
                             XSqlErrorType_ConnectionError);
            xmysql_client_close(client);
            return false;
        }
        XString_delete_base(unixSocketText);
        unixSocketText = NULL;
    } else {
        XAbstractSocket_connectToHost_base((XAbstractSocket*)client->m_socket,
                                            host && host[0] ? host : "127.0.0.1",
                                            (uint16_t)(port > 0 ? port : 3306),
                                            XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    }
    if (!useSharedMemory
        && !XTcpSocket_waitForConnected_base(client->m_socket, client->m_connectTimeout)) {
        socketError = XTcpSocket_errorString(client->m_socket);
        xmysql_set_error(client, "Unable to connect to MySQL server",
                         socketError ? XString_toUtf8(socketError) : "", 0,
                         XSqlErrorType_ConnectionError);
        if (socketError) XString_delete_base(socketError);
        xmysql_client_close(client);
        return false;
    }
    if (!xmysql_client_handshake(client, database, user, password)) {
        xmysql_client_close(client);
        return false;
    }
    client->m_open = true;
    client->m_preparedQueries = xmysql_client_check_prepared_queries(client);
    if (client->m_preparedQueries) {
        XSqlMySqlResult* timeZoneResult = NULL;
        bool timeZoneOk = xmysql_client_execute_once(client,
            "SET time_zone = '+00:00'", strlen("SET time_zone = '+00:00'"),
            &timeZoneResult);
        if (timeZoneResult) xmysql_result_destroy(timeZoneResult);
        if (!timeZoneOk) xmysql_clear_error(client);
    }
    return true;
tls_option_fail:
    if (sslPathString) XString_delete_base(sslPathString);
    xmysql_set_error(client, "Unable to load MySQL TLS client credentials", "", 0,
                     XSqlErrorType_ConnectionError);
    xmysql_client_close(client);
    return false;
}

static bool xmysql_client_reconnect(XSqlMySqlClient* client)
{
    XString* database;
    XString* user;
    XString* password;
    XString* host;
    XString* options;
    bool ok;
    if (!client || !client->m_connectionDatabase || !client->m_connectionUser
        || !client->m_connectionPassword || !client->m_connectionHost
        || !client->m_connectionOptions) return false;
    database = XString_create_copy(client->m_connectionDatabase);
    user = XString_create_copy(client->m_connectionUser);
    password = XString_create_copy(client->m_connectionPassword);
    host = XString_create_copy(client->m_connectionHost);
    options = XString_create_copy(client->m_connectionOptions);
    if (!database || !user || !password || !host || !options) {
        if (database) XString_delete_base(database);
        if (user) XString_delete_base(user);
        if (password) XString_delete_base(password);
        if (host) XString_delete_base(host);
        if (options) XString_delete_base(options);
        return false;
    }
    client->m_reconnecting = true;
    ok = xmysql_client_open(client, XString_toUtf8(database), XString_toUtf8(user),
                            XString_toUtf8(password), XString_toUtf8(host),
                            client->m_connectionPort, XString_toUtf8(options));
    client->m_reconnecting = false;
    XString_delete_base(database);
    XString_delete_base(user);
    XString_delete_base(password);
    XString_delete_base(host);
    XString_delete_base(options);
    return ok;
}

static void xmysql_client_parse_ok(XSqlMySqlResult* result, const uint8_t* data, size_t size)
{
    const uint8_t* cursor = data + 1;
    const uint8_t* end = data + size;
    uint64_t affected = 0;
    uint64_t insertId = 0;
    bool isNull;
    if (xmysql_read_lenenc(&cursor, end, &affected, &isNull)
        && xmysql_read_lenenc(&cursor, end, &insertId, &isNull)) {
        result->m_rowsAffected = (int64_t)affected;
        result->m_lastInsertId = insertId;
        if ((size_t)(end - cursor) >= 2)
            result->m_moreResults = ((uint16_t)cursor[0] | ((uint16_t)cursor[1] << 8))
                & XMYSQL_SERVER_MORE_RESULTS_EXISTS;
    }
}

static bool xmysql_result_parse_eof(XSqlMySqlResult* result, const uint8_t* data, size_t size)
{
    if (!result || !data || size < 5 || data[0] != 0xfeu) return false;
    result->m_moreResults = (((uint16_t)data[3] | ((uint16_t)data[4] << 8))
                             & XMYSQL_SERVER_MORE_RESULTS_EXISTS) != 0;
    return true;
}

static bool xmysql_binary_cell(XSqlMySqlResult* result, int field,
                               const uint8_t** cursor, const uint8_t* end)
{
    const XSqlMySqlField* info;
    XMySqlWireCell* cell;
    const uint8_t* data = NULL;
    size_t size = 0;
    char number[96];
    int length = 0;
    if (!result || !cursor || !*cursor || field < 0 || !end || *cursor > end) return false;
    info = &result->m_fields[field];
    if (result->m_rowCount == 0) return false;
    cell = &result->m_cells[(result->m_rowCount - 1) * result->m_fieldCount + (size_t)field];
    switch (info->m_type) {
    case XSqlMySqlValueType_Integer:
    case XSqlMySqlValueType_UnsignedInteger:
        if (info->m_nativeType == XMYSQL_TYPE_BIT) {
            if (!xmysql_read_lenenc_slice(cursor, end, &data, &size, NULL)) return false;
            if (!xmysql_bit_to_text(data, size, number, &size)) return false;
            data = (const uint8_t*)number;
        } else if (info->m_nativeType == XMYSQL_TYPE_TINY
            || info->m_nativeType == XMYSQL_TYPE_YEAR
            || info->m_nativeType == XMYSQL_TYPE_SHORT
            || info->m_nativeType == XMYSQL_TYPE_LONG
            || info->m_nativeType == XMYSQL_TYPE_INT24) {
            size_t width = info->m_nativeType == XMYSQL_TYPE_TINY ? 1
                : (info->m_nativeType == XMYSQL_TYPE_YEAR
                   || info->m_nativeType == XMYSQL_TYPE_SHORT) ? 2 : 4;
            uint64_t value = 0;
            size_t i;
            if ((size_t)(end - *cursor) < width) return false;
            for (i = 0; i < width; ++i) value |= (uint64_t)(*cursor)[i] << (8 * i);
            *cursor += width;
            if (info->m_unsigned) length = snprintf(number, sizeof(number), "%llu",
                                                     (unsigned long long)value);
            else if (width == 1) length = snprintf(number, sizeof(number), "%d", (int8_t)value);
            else if (width == 2) length = snprintf(number, sizeof(number), "%d", (int16_t)value);
            else length = snprintf(number, sizeof(number), "%d", (int32_t)value);
            data = (const uint8_t*)number;
            size = length > 0 ? (size_t)length : 0;
        } else {
            uint64_t value = 0;
            size_t i;
            if ((size_t)(end - *cursor) < 8) return false;
            for (i = 0; i < 8; ++i) value |= (uint64_t)(*cursor)[i] << (8 * i);
            *cursor += 8;
            if (info->m_unsigned) length = snprintf(number, sizeof(number), "%llu",
                                                     (unsigned long long)value);
            else length = snprintf(number, sizeof(number), "%lld", (long long)(int64_t)value);
            data = (const uint8_t*)number;
            size = length > 0 ? (size_t)length : 0;
        }
        break;
    case XSqlMySqlValueType_Real: {
        double value;
        if (info->m_nativeType == XMYSQL_TYPE_DECIMAL
            || info->m_nativeType == XMYSQL_TYPE_NEWDECIMAL) {
            if (!xmysql_read_lenenc_slice(cursor, end, &data, &size, NULL)) return false;
            break;
        } else if (info->m_nativeType == XMYSQL_TYPE_FLOAT) {
            float f;
            if ((size_t)(end - *cursor) < sizeof(f)) return false;
            memcpy(&f, *cursor, sizeof(f));
            *cursor += sizeof(f);
            value = f;
        } else {
            if ((size_t)(end - *cursor) < sizeof(value)) return false;
            memcpy(&value, *cursor, sizeof(value));
            *cursor += sizeof(value);
        }
        length = snprintf(number, sizeof(number), "%.17g", value);
        data = (const uint8_t*)number;
        size = length > 0 ? (size_t)length : 0;
        break;
    }
    case XSqlMySqlValueType_DateTime: {
        uint8_t encodedSize;
        if (*cursor >= end) return false;
        encodedSize = *(*cursor)++;
        if (info->m_nativeType == XMYSQL_TYPE_DATE
            || info->m_nativeType == XMYSQL_TYPE_NEWDATE) {
            uint16_t year;
            if (encodedSize == 0) {
                number[0] = 0;
                length = 0;
            } else if (encodedSize == 4 && (size_t)(end - *cursor) >= 4) {
                year = (uint16_t)(*cursor)[0] | ((uint16_t)(*cursor)[1] << 8);
                length = snprintf(number, sizeof(number), "%04u-%02u-%02u",
                                  year, (*cursor)[2], (*cursor)[3]);
                *cursor += 4;
            } else return false;
        } else if (info->m_nativeType == XMYSQL_TYPE_TIME
                   || info->m_nativeType == XMYSQL_TYPE_TIME2) {
            uint32_t days;
            if (encodedSize == 0) {
                length = snprintf(number, sizeof(number), "00:00:00");
            } else if ((encodedSize == 8 || encodedSize == 12)
                       && (size_t)(end - *cursor) >= encodedSize) {
                days = (uint32_t)(*cursor)[1] | ((uint32_t)(*cursor)[2] << 8)
                    | ((uint32_t)(*cursor)[3] << 16) | ((uint32_t)(*cursor)[4] << 24);
                length = snprintf(number, sizeof(number), "%s%u:%02u:%02u",
                                  (*cursor)[0] ? "-" : "", days * 24u + (*cursor)[5],
                                  (*cursor)[6], (*cursor)[7]);
                if (encodedSize == 12) {
                    uint32_t micros = (uint32_t)(*cursor)[8]
                        | ((uint32_t)(*cursor)[9] << 8) | ((uint32_t)(*cursor)[10] << 16)
                        | ((uint32_t)(*cursor)[11] << 24);
                    length += snprintf(number + length, sizeof(number) - (size_t)length,
                                       ".%06u", micros);
                }
                *cursor += encodedSize;
            } else return false;
        } else {
            uint16_t year;
            if ((encodedSize == 4 || encodedSize == 7 || encodedSize == 11)
                && (size_t)(end - *cursor) >= encodedSize) {
                year = (uint16_t)(*cursor)[0] | ((uint16_t)(*cursor)[1] << 8);
                length = snprintf(number, sizeof(number), "%04u-%02u-%02u %02u:%02u:%02u",
                                  year, (*cursor)[2], (*cursor)[3], (*cursor)[4],
                                  (*cursor)[5], (*cursor)[6]);
                if (encodedSize == 11) {
                    uint32_t micros = (uint32_t)(*cursor)[7]
                        | ((uint32_t)(*cursor)[8] << 8) | ((uint32_t)(*cursor)[9] << 16)
                        | ((uint32_t)(*cursor)[10] << 24);
                    length += snprintf(number + length, sizeof(number) - (size_t)length,
                                       ".%06u", micros);
                }
                *cursor += encodedSize;
            } else return false;
        }
        data = (const uint8_t*)number;
        size = length > 0 ? (size_t)length : 0;
        break;
    }
    default:
        if (!xmysql_read_lenenc_slice(cursor, end, &data, &size, NULL)) return false;
        break;
    }
    cell->m_bytes = XByteArray_create_with_data((const char*)data, size);
    if (!cell->m_bytes) return false;
    cell->m_value.m_data = XByteArray_data(cell->m_bytes);
    cell->m_value.m_size = size;
    cell->m_value.m_type = result->m_fields[field].m_type;
    cell->m_value.m_isNull = false;
    return true;
}

static bool xmysql_result_append_binary_row(XSqlMySqlResult* result,
                                            const uint8_t* data, size_t size)
{
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    size_t bitmapSize;
    size_t row;
    XMySqlWireCell* cells;
    if (!result || !data || size < 1 || data[0] != 0x00u) return false;
    bitmapSize = (result->m_fieldCount + 7 + 2) / 8;
    if (size < 1 + bitmapSize) return false;
    cursor += 1 + bitmapSize;
    row = result->m_rowCount;
    cells = (XMySqlWireCell*)XRealloc_System(result->m_cells,
        (row + 1) * result->m_fieldCount * sizeof(*cells));
    if (!cells) return false;
    result->m_cells = cells;
    memset(&cells[row * result->m_fieldCount], 0,
           result->m_fieldCount * sizeof(*cells));
    result->m_rowCount = row + 1;
    for (size_t field = 0; field < result->m_fieldCount; ++field) {
        const uint8_t* bitmap = data + 1;
        XMySqlWireCell* cell = &cells[row * result->m_fieldCount + field];
        if (bitmap[(field + 2) / 8] & (uint8_t)(1u << ((field + 2) % 8))) {
            cell->m_value.m_type = XSqlMySqlValueType_Null;
            cell->m_value.m_isNull = true;
            continue;
        }
        if (!xmysql_binary_cell(result, (int)field, &cursor, end)) return false;
    }
    return true;
}

static bool xmysql_send_local_infile(XSqlMySqlClient* client,
                                     const XByteArray* request,
                                     uint8_t requestSequence)
{
    const uint8_t* data;
    size_t size;
    XString* path;
    XFile* file;
    uint8_t packet[16384];
    uint8_t sequence;
    if (!client || !request || !client->m_localInfile) return false;
    data = XByteArray_constData((XByteArray*)request);
    size = XByteArray_size_base((XByteArray*)request);
    if (!data || size <= 1 || data[0] != 0xfbu) return false;
    path = XString_create_with_length_utf8((const char*)data + 1, size - 1);
    if (!path) return false;
    file = XFile_create_2(path);
    XString_delete_base(path);
    if (!file || !XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    sequence = (uint8_t)(requestSequence + 1u);
    for (;;) {
        int64_t count = XIODevice_read_1((XIODevice*)file, (char*)packet, sizeof(packet));
        if (count < 0) {
            XClass_delete_base((XClass*)file);
            return false;
        }
        if (count == 0) break;
        if (!xmysql_client_send_packet(client, packet, (size_t)count, sequence++)) {
            XClass_delete_base((XClass*)file);
            return false;
        }
    }
    XClass_delete_base((XClass*)file);
    return xmysql_client_send_packet(client, NULL, 0, sequence);
}

static bool xmysql_read_result(XSqlMySqlClient* client, XByteArray* first,
                               bool binaryRows, uint8_t firstSequence,
                               XSqlMySqlResult** output)
{
    XSqlMySqlResult* result;
    XByteArray* payload = first;
    const uint8_t* data;
    uint64_t columnCount;
    uint8_t sequence = 0;
    bool isNull;
    int field;
    if (output) *output = NULL;
    result = xmysql_result_create();
    if (!client || !payload || !result || XByteArray_size_base(payload) == 0) goto fail;
    data = XByteArray_data(payload);
    if (data[0] == 0xffu) {
        xmysql_parse_error(client, data, XByteArray_size_base(payload),
                           XSqlErrorType_StatementError);
        goto fail;
    }
    if (data[0] == 0xfbu) {
        if (!xmysql_send_local_infile(client, payload, firstSequence)) {
            xmysql_set_error(client, "Unable to read MySQL LOCAL INFILE data", "", 0,
                             XSqlErrorType_StatementError);
            goto fail;
        }
        XByteArray_delete_base(payload);
        payload = NULL;
        if (!xmysql_client_read_packet(client, &payload, &sequence)
            || XByteArray_size_base(payload) == 0) goto fail;
        data = XByteArray_data(payload);
        if (data[0] == 0xffu) {
            xmysql_parse_error(client, data, XByteArray_size_base(payload),
                               XSqlErrorType_StatementError);
            goto fail;
        }
        if (data[0] != 0x00u) goto fail;
        xmysql_client_parse_ok(result, data, XByteArray_size_base(payload));
        XByteArray_delete_base(payload);
        *output = result;
        return true;
    }
    if (data[0] == 0x00u) {
        xmysql_client_parse_ok(result, data, XByteArray_size_base(payload));
        XByteArray_delete_base(payload);
        *output = result;
        return true;
    }
    if (!xmysql_read_lenenc(&data, XByteArray_data(payload) + XByteArray_size_base(payload),
                            &columnCount, &isNull) || isNull || columnCount > INT_MAX
        || !xmysql_result_allocate_fields(result, (size_t)columnCount)) goto fail;
    XByteArray_delete_base(payload);
    payload = NULL;
    for (field = 0; field < (int)columnCount; ++field) {
        if (!xmysql_client_read_packet(client, &payload, &sequence)
            || !xmysql_result_parse_field(result, field, XByteArray_data(payload),
                                          XByteArray_size_base(payload))) goto fail;
        XByteArray_delete_base(payload);
        payload = NULL;
    }
    if (!xmysql_client_read_packet(client, &payload, &sequence)) goto fail;
    if (!(XByteArray_size_base(payload) > 0 && XByteArray_data(payload)[0] == 0xfeu)) goto fail;
    if (XByteArray_size_base(payload) < 9)
        xmysql_result_parse_eof(result, XByteArray_data(payload),
                                XByteArray_size_base(payload));
    XByteArray_delete_base(payload);
    payload = NULL;
    result->m_select = true;
    for (;;) {
        if (!xmysql_client_read_packet(client, &payload, &sequence)) goto fail;
        data = XByteArray_data(payload);
        if (XByteArray_size_base(payload) < 9 && data[0] == 0xfeu) {
            xmysql_result_parse_eof(result, data, XByteArray_size_base(payload));
            XByteArray_delete_base(payload);
            payload = NULL;
            break;
        }
        if (data[0] == 0xffu) {
            xmysql_parse_error(client, data, XByteArray_size_base(payload),
                               XSqlErrorType_StatementError);
            goto fail;
        }
        if (binaryRows ? !xmysql_result_append_binary_row(result, data, XByteArray_size_base(payload))
                       : !xmysql_result_append_row(result, data, XByteArray_size_base(payload))) goto fail;
        XByteArray_delete_base(payload);
        payload = NULL;
    }
    result->m_at = XSqlLocation_BeforeFirstRow;
    *output = result;
    return true;
fail:
    if (payload) XByteArray_delete_base(payload);
    if (result) xmysql_result_destroy(result);
    return false;
}

static bool xmysql_client_execute_once(XSqlMySqlClient* client, const char* query,
                                       size_t length, XSqlMySqlResult** result)
{
    XByteArray* packet = NULL;
    XByteArray* payload = NULL;
    XSqlMySqlResult* output = NULL;
    const uint8_t* data;
    uint8_t sequence;
    if (result) *result = NULL;
    if (!client || !client->m_socket || !client->m_open || !query || !result) return false;
    xmysql_clear_error(client);
    packet = XByteArray_create();
    if (!packet || !xmysql_append_u8(packet, XMYSQL_COM_QUERY)
        || !XByteArray_push_back_2(packet, query, length)
        || !xmysql_client_send_packet(client, XByteArray_data(packet),
                               XByteArray_size_base(packet), 0)
        || !xmysql_client_read_packet(client, &payload, &sequence)) goto fail;
    output = NULL;
    if (!xmysql_read_result(client, payload, false, sequence, &output)) {
        payload = NULL;
        xmysql_set_error(client, "Unable to decode MySQL result", "", 0,
                         XSqlErrorType_StatementError);
        goto fail;
    }
    payload = NULL;
    if (output && output->m_moreResults) {
        XSqlMySqlResult* tail = output;
        while (tail->m_moreResults) {
            XByteArray* nextPayload = NULL;
            XSqlMySqlResult* next = NULL;
            if (!xmysql_client_read_packet(client, &nextPayload, &sequence)
                || !xmysql_read_result(client, nextPayload, false, sequence, &next)) {
                if (nextPayload) XByteArray_delete_base(nextPayload);
                goto fail;
            }
            tail->m_next = next;
            tail = next;
        }
    }
    goto success;
fail:
    if (packet) XByteArray_delete_base(packet);
    if (payload) XByteArray_delete_base(payload);
    if (output) xmysql_result_destroy(output);
    if (client->m_errorType == XSqlErrorType_NoError)
        xmysql_set_error(client, "Unable to execute MySQL query", "", 0,
                         XSqlErrorType_StatementError);
    return false;
success:
    if (packet) XByteArray_delete_base(packet);
    if (payload) XByteArray_delete_base(payload);
    *result = output;
    return true;
}

static bool xmysql_client_execute(XSqlMySqlClient* client, const char* query,
                                  size_t length, XSqlMySqlResult** result)
{
    bool ok = xmysql_client_execute_once(client, query, length, result);
    if (!ok && client && client->m_reconnect
        && client->m_errorType == XSqlErrorType_ConnectionError
        && xmysql_client_reconnect(client))
        ok = xmysql_client_execute_once(client, query, length, result);
    return ok;
}

static bool xmysql_append_binary_bind(XByteArray* packet, const XSqlMySqlBind* bind)
{
    uint64_t integer;
    char text[64];
    int length;
    if (!packet || !bind) return false;
    if (bind->m_isNull) return true;
    switch (bind->m_type) {
    case XSqlMySqlValueType_Integer:
    case XSqlMySqlValueType_UnsignedInteger:
        if (bind->m_size == 1) integer = *(const uint8_t*)bind->m_data;
        else if (bind->m_size == 2) integer = *(const uint16_t*)bind->m_data;
        else if (bind->m_size == 4) integer = *(const uint32_t*)bind->m_data;
        else integer = *(const uint64_t*)bind->m_data;
        if (bind->m_size == 1) return XByteArray_push_back_1(packet, (uint8_t)integer);
        if (bind->m_size == 2) return xmysql_append_u16(packet, (uint16_t)integer);
        if (bind->m_size == 4) return xmysql_append_u32(packet, (uint32_t)integer);
        return xmysql_append_u64(packet, integer);
    case XSqlMySqlValueType_Real:
        if (bind->m_size == sizeof(float))
            return XByteArray_push_back_2(packet, bind->m_data, sizeof(float));
        return XByteArray_push_back_2(packet, bind->m_data, sizeof(double));
    case XSqlMySqlValueType_Date: {
        const XDate* date = (const XDate*)bind->m_data;
        length = date && XDate_isValid(date)
            ? snprintf(text, sizeof(text), "%04d-%02d-%02d", XDate_year(date),
                       XDate_month(date), XDate_day(date)) : 0;
        return length >= 0 && (size_t)length < sizeof(text)
            && xmysql_append_lenenc_string(packet, text, (size_t)length);
    }
    case XSqlMySqlValueType_Time: {
        const XTime* time = (const XTime*)bind->m_data;
        length = time && XTime_isValid(time)
            ? snprintf(text, sizeof(text), "%02d:%02d:%02d.%03d", XTime_hour(time),
                       XTime_minute(time), XTime_second(time), XTime_msec(time)) : 0;
        return length >= 0 && (size_t)length < sizeof(text)
            && xmysql_append_lenenc_string(packet, text, (size_t)length);
    }
    case XSqlMySqlValueType_DateTime: {
        const XDateTime* datetime = (const XDateTime*)bind->m_data;
        const XDate* date = datetime ? &datetime->m_date : NULL;
        const XTime* time = datetime ? &datetime->m_time : NULL;
        length = datetime && XDateTime_isValid(datetime)
            ? snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                       XDate_year(date), XDate_month(date), XDate_day(date),
                       XTime_hour(time), XTime_minute(time), XTime_second(time),
                       XTime_msec(time)) : 0;
        return length >= 0 && (size_t)length < sizeof(text)
            && xmysql_append_lenenc_string(packet, text, (size_t)length);
    }
    default:
        return xmysql_append_lenenc_string(packet, bind->m_data, bind->m_size);
    }
}

static uint8_t xmysql_bind_native_type(const XSqlMySqlBind* bind)
{
    if (!bind) return XMYSQL_TYPE_VAR_STRING;
    if (bind->m_isNull) return XMYSQL_TYPE_NULL;
    switch (bind->m_type) {
    case XSqlMySqlValueType_Integer:
    case XSqlMySqlValueType_UnsignedInteger:
        if (bind->m_size == 1) return XMYSQL_TYPE_TINY;
        if (bind->m_size == 2) return XMYSQL_TYPE_SHORT;
        if (bind->m_size == 4) return XMYSQL_TYPE_LONG;
        return XMYSQL_TYPE_LONGLONG;
    case XSqlMySqlValueType_Real:
        return bind->m_size == sizeof(float) ? XMYSQL_TYPE_FLOAT : XMYSQL_TYPE_DOUBLE;
    case XSqlMySqlValueType_ByteArray:
        return XMYSQL_TYPE_BLOB;
    case XSqlMySqlValueType_Date:
    case XSqlMySqlValueType_Time:
    case XSqlMySqlValueType_DateTime:
        return XMYSQL_TYPE_VAR_STRING;
    default:
        return XMYSQL_TYPE_VAR_STRING;
    }
}

static bool xmysql_client_execute_prepared_once(XSqlMySqlClient* client, const char* query,
                                                size_t length, const XSqlMySqlBind* binds,
                                                size_t bindCount, XSqlMySqlResult** result)
{
    XByteArray* packet = NULL;
    XByteArray* response = NULL;
    XByteArray* discard = NULL;
    uint8_t sequence = 0;
    uint32_t statementId;
    uint16_t parameterCount;
    uint16_t columnCount;
    uint64_t i;
    XSqlMySqlResult* output = NULL;
    if (result) *result = NULL;
    if (!client || !client->m_socket || !client->m_open || !query || !result
        || bindCount > UINT16_MAX) return false;
    packet = XByteArray_create();
    if (!packet || !xmysql_append_u8(packet, XMYSQL_COM_STMT_PREPARE)
        || !XByteArray_push_back_2(packet, query, length)
        || !xmysql_client_send_packet(client, XByteArray_data(packet),
                               XByteArray_size_base(packet), 0)
        || !xmysql_client_read_packet(client, &response, &sequence)
        || XByteArray_size_base(response) < 12
        || XByteArray_data(response)[0] != 0x00u) goto fail;
    statementId = (uint32_t)XByteArray_data(response)[1]
        | ((uint32_t)XByteArray_data(response)[2] << 8)
        | ((uint32_t)XByteArray_data(response)[3] << 16)
        | ((uint32_t)XByteArray_data(response)[4] << 24);
    columnCount = (uint16_t)XByteArray_data(response)[5]
        | ((uint16_t)XByteArray_data(response)[6] << 8);
    parameterCount = (uint16_t)XByteArray_data(response)[7]
        | ((uint16_t)XByteArray_data(response)[8] << 8);
    XByteArray_delete_base(response);
    response = NULL;
    if (parameterCount != bindCount) goto fail;
    for (i = 0; i < parameterCount; ++i) {
        if (!xmysql_client_read_packet(client, &discard, &sequence)) goto fail;
        XByteArray_delete_base(discard);
        discard = NULL;
    }
    if (parameterCount > 0 && !xmysql_client_read_packet(client, &discard, &sequence)) goto fail;
    if (discard) { XByteArray_delete_base(discard); discard = NULL; }
    for (i = 0; i < columnCount; ++i) {
        if (!xmysql_client_read_packet(client, &discard, &sequence)) goto fail;
        XByteArray_delete_base(discard);
        discard = NULL;
    }
    if (columnCount > 0 && !xmysql_client_read_packet(client, &discard, &sequence)) goto fail;
    if (discard) { XByteArray_delete_base(discard); discard = NULL; }

    XByteArray_delete_base(packet);
    packet = XByteArray_create();
    if (!packet || !xmysql_append_u8(packet, XMYSQL_COM_STMT_EXECUTE)
        || !xmysql_append_u32(packet, statementId)
        || !xmysql_append_u8(packet, 0)
        || !xmysql_append_u32(packet, 1)) goto fail;
    {
        size_t nullBytes = (bindCount + 7) / 8;
        size_t byteIndex;
        for (byteIndex = 0; byteIndex < nullBytes; ++byteIndex) {
            uint8_t bits = 0;
            for (i = 0; i < 8 && byteIndex * 8 + i < bindCount; ++i)
                if (binds[byteIndex * 8 + i].m_isNull) bits |= (uint8_t)(1u << i);
            if (!XByteArray_push_back_1(packet, bits)) goto fail;
        }
        if (!XByteArray_push_back_1(packet, 1)) goto fail;
        for (i = 0; i < bindCount; ++i) {
            if (!XByteArray_push_back_1(packet, xmysql_bind_native_type(&binds[i]))) goto fail;
            if (!XByteArray_push_back_1(packet, binds[i].m_unsigned ? 0x80u : 0)) goto fail;
        }
        for (i = 0; i < bindCount; ++i)
            if (!xmysql_append_binary_bind(packet, &binds[i])) goto fail;
    }
    if (!xmysql_client_send_packet(client, XByteArray_data(packet),
                            XByteArray_size_base(packet), 0)
        || !xmysql_client_read_packet(client, &response, &sequence)
        || !xmysql_read_result(client, response, true, sequence, &output)) goto fail;
    response = NULL;
    if (output && output->m_moreResults) {
        XSqlMySqlResult* tail = output;
        while (tail->m_moreResults) {
            XByteArray* nextPayload = NULL;
            XSqlMySqlResult* next = NULL;
            if (!xmysql_client_read_packet(client, &nextPayload, &sequence)
                || !xmysql_read_result(client, nextPayload, true, sequence, &next)) {
                if (nextPayload) XByteArray_delete_base(nextPayload);
                goto fail;
            }
            tail->m_next = next;
            tail = next;
        }
    }
    {
        XByteArray* closePacket = XByteArray_create();
        if (closePacket) {
            if (xmysql_append_u8(closePacket, XMYSQL_COM_STMT_CLOSE)
                && xmysql_append_u32(closePacket, statementId))
                xmysql_client_send_packet(client, XByteArray_data(closePacket),
                                   XByteArray_size_base(closePacket), 0);
            XByteArray_delete_base(closePacket);
        }
    }
    XByteArray_delete_base(packet);
    *result = output;
    return true;
fail:
    if (packet) XByteArray_delete_base(packet);
    if (response) XByteArray_delete_base(response);
    if (discard) XByteArray_delete_base(discard);
    if (output) xmysql_result_destroy(output);
    if (client->m_errorType == XSqlErrorType_NoError)
        xmysql_set_error(client, "Unable to execute MySQL prepared query", "", 0,
                         XSqlErrorType_StatementError);
    return false;
}

static bool xmysql_client_execute_prepared(XSqlMySqlClient* client, const char* query,
                                           size_t length, const XSqlMySqlBind* binds,
                                           size_t bindCount, XSqlMySqlResult** result)
{
    bool ok = xmysql_client_execute_prepared_once(client, query, length, binds,
                                                  bindCount, result);
    if (!ok && client && client->m_reconnect
        && client->m_errorType == XSqlErrorType_ConnectionError
        && xmysql_client_reconnect(client))
        ok = xmysql_client_execute_prepared_once(client, query, length, binds,
                                                 bindCount, result);
    return ok;
}

static bool xmysql_client_check_prepared_queries(XSqlMySqlClient* client)
{
    int32_t first = 1;
    int32_t second = 1;
    XSqlMySqlBind binds[2];
    XSqlMySqlResult* result = NULL;
    bool ok;
    memset(binds, 0, sizeof(binds));
    binds[0].m_type = XSqlMySqlValueType_Integer;
    binds[0].m_data = &first;
    binds[0].m_size = sizeof(first);
    binds[1].m_type = XSqlMySqlValueType_Integer;
    binds[1].m_data = &second;
    binds[1].m_size = sizeof(second);
    ok = xmysql_client_execute_prepared_once(client, "SELECT ? + ?",
                                             strlen("SELECT ? + ?"), binds, 2, &result);
    if (result) xmysql_result_destroy(result);
    if (!ok) xmysql_clear_error(client);
    return ok;
}

static int xmysql_result_column_count(const XSqlMySqlResult* result)
{
    return result && result->m_fieldCount <= INT_MAX ? (int)result->m_fieldCount : 0;
}

static const XSqlMySqlField* xmysql_result_field(const XSqlMySqlResult* result, int index)
{
    return result && index >= 0 && (size_t)index < result->m_fieldCount
        ? &result->m_fields[index] : NULL;
}

static bool xmysql_result_fetch(XSqlMySqlResult* result, int index)
{
    if (!result || !result->m_select || index < 0 || (size_t)index >= result->m_rowCount) {
        if (result) result->m_at = XSqlLocation_AfterLastRow;
        return false;
    }
    result->m_at = index;
    return true;
}

static const XSqlMySqlValue* xmysql_result_value(const XSqlMySqlResult* result, int index)
{
    if (!result || result->m_at < 0 || (size_t)result->m_at >= result->m_rowCount
        || index < 0 || (size_t)index >= result->m_fieldCount) return NULL;
    return &result->m_cells[(size_t)result->m_at * result->m_fieldCount + (size_t)index].m_value;
}

static int xmysql_result_size(const XSqlMySqlResult* result)
{
    return result && result->m_select && result->m_rowCount <= INT_MAX
        ? (int)result->m_rowCount : -1;
}

static int64_t xmysql_result_rows_affected(const XSqlMySqlResult* result)
{
    return result ? result->m_rowsAffected : -1;
}

static uint64_t xmysql_result_last_insert_id(const XSqlMySqlResult* result)
{
    return result ? result->m_lastInsertId : 0;
}

static bool xmysql_result_is_select(const XSqlMySqlResult* result)
{
    return result && result->m_select;
}

static bool xmysql_result_next(XSqlMySqlResult* result, XSqlMySqlResult** next)
{
    if (next) *next = NULL;
    if (!result || !result->m_next || !next) return false;
    *next = result->m_next;
    result->m_next = NULL;
    result->m_moreResults = false;
    return true;
}

static const XSqlMySqlError* xmysql_client_last_error(const XSqlMySqlClient* client)
{
    static XSqlMySqlError empty = { "", "", "0", XSqlErrorType_NoError };
    if (!client) return &empty;
    return &client->m_error;
}

static void* xmysql_client_handle(const XSqlMySqlClient* client)
{
    return client ? client->m_socket : NULL;
}

static bool xmysql_client_cancel(XSqlMySqlClient* client)
{
    (void)client;
    /* QMYSQL also reports CancelQuery as unsupported. MySQL has no in-band
     * cancel for COM_QUERY; closing the connection is not a transparent
     * cancellation because the connection cannot be reused safely. */
    return false;
}

static bool xmysql_client_supports_transactions(const XSqlMySqlClient* client)
{
    return client && client->m_open
        && (client->m_serverCapabilities & XMYSQL_CLIENT_TRANSACTIONS) != 0;
}

static bool xmysql_client_supports_prepared_queries(const XSqlMySqlClient* client)
{
    return client && client->m_open && client->m_preparedQueries;
}

const XSqlMySqlClientApi* XSqlMySqlClient_defaultApi(void)
{
    return &g_xmysql_client_api;
}
