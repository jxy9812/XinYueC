#include "XCan_config.h"
#if XPROTOCOL_ON
#if XCAN_ON
#if XCAN_DBC_ON
#include "XCanDbcFileParser.h"
#include "XCanSignalDescription.h"
#include "XMemory.h"
#include "XString.h"
#include "XStringList.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// =============== 跨平台字符串分割函数 ===============

/**
 * @brief 跨平台字符串分割函数（替代 POSIX strtok_r）
 * @param str 要分割的字符串（首次调用传入，后续传入 NULL）
 * @param delim 分隔符字符串
 * @param savePtr 保存状态的指针
 * @return 分割出的 token，无更多 token 返回 NULL
 * @note 此函数用于替代 POSIX 的 strtok_r，确保 Windows 兼容性
 */
static char* xStrtok(char* str, const char* delim, char** savePtr)
{
    char* p;
    if (str) {
        *savePtr = str;
    }
    if (!*savePtr || !**savePtr) {
        return NULL;
    }
    /* 跳过前导分隔符 */
    p = *savePtr;
    while (*p && strchr(delim, *p)) {
        p++;
    }
    if (!*p) {
        *savePtr = p;
        return NULL;
    }
    /* 找到 token 结束位置 */
    char* token = p;
    while (*p && !strchr(delim, *p)) {
        p++;
    }
    if (*p) {
        *p = ' ';
        *savePtr = p + 1;
    } else {
        *savePtr = p;
    }
    return token;
}

// =============== 内部常量 ===============

/** @brief 虚拟信号名称，用于简单多路复用时的占位符 */
#define K_QT_DUMMY_SIGNAL "kQtDummySignal"

// =============== 内部辅助函数声明 ===============

static void resetParser(XCanDbcFileParser* parser);
static bool parseFileInternal(XCanDbcFileParser* parser, const char* fileName);
static bool parseDataInternal(XCanDbcFileParser* parser, const char* data);
static bool processLine(XCanDbcFileParser* parser, const char* line);
static bool parseMessage(XCanDbcFileParser* parser, const char* data);
static bool parseSignal(XCanDbcFileParser* parser, const char* data);
static void parseSignalType(XCanDbcFileParser* parser, const char* data);
static void parseComment(XCanDbcFileParser* parser, const char* data);
static void parseExtendedMux(XCanDbcFileParser* parser, const char* data);
static void parseValueDescriptions(XCanDbcFileParser* parser, const char* data);
static void postProcessSignalMultiplexing(XCanDbcFileParser* parser);
static void addCurrentMessage(XCanDbcFileParser* parser);
static void addWarning(XCanDbcFileParser* parser, const char* warning);
static XCanMessageDescription* findMessageByUid(XCanDbcFileParser* parser, XCanBus_UniqueId uid);

// =============== 字符串辅助函数 ===============

/** @brief 去除字符串首尾空白 */
static char* trimString(char* str)
{
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

/** @brief 跳过空白字符 */
static const char* skipSpaces(const char* p)
{
    if (!p) return NULL;
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/** @brief 提取引号内的字符串 */
static char* extractQuotedString(const char** p)
{
    if (!p || !*p) return NULL;
    const char* s = skipSpaces(*p);
    if (*s != '"') return NULL;
    s++; // 跳过开始引号
    const char* end = strchr(s, '"');
    if (!end) return NULL;
    size_t len = end - s;
    char* result = (char*)XMalloc_System(len + 1);
    if (result) {
        strncpy(result, s, len);
        result[len] = '\0';
    }
    *p = end + 1; // 跳过结束引号
    return result;
}

// =============== 初始化与清理 ===============

void XCanDbcFileParser_init(XCanDbcFileParser* parser)
{
    if (!parser) return;
    memset(parser, 0, sizeof(XCanDbcFileParser));
    parser->m_error = XCanDbcFileParser_Error_None;
    parser->m_isProcessingMessage = false;
    parser->m_seenExtraData = false;
    parser->m_lineOffset = 0;
    parser->m_fileName = NULL;
}

void XCanDbcFileParser_deinit(XCanDbcFileParser* parser)
{
    if (!parser) return;

    if (parser->m_messageDescriptions) {
        XMap_delete_base(parser->m_messageDescriptions);
        parser->m_messageDescriptions = NULL;
    }
    if (parser->m_valueDescriptions) {
        XMap_delete_base(parser->m_valueDescriptions);
        parser->m_valueDescriptions = NULL;
    }
    if (parser->m_isProcessingMessage) {
        XCanMessageDescription_deinit(&parser->m_currentMessage);
        parser->m_isProcessingMessage = false;
    }
    if (parser->m_errorString) {
        XString_delete_base(parser->m_errorString);
        parser->m_errorString = NULL;
    }
    if (parser->m_warnings) {
        XStringList_delete_base(parser->m_warnings);
        parser->m_warnings = NULL;
    }
    if (parser->m_fileName) {
        XFree_System(parser->m_fileName);
        parser->m_fileName = NULL;
    }
}

// =============== 内部重置 ===============

static void resetParser(XCanDbcFileParser* parser)
{
    if (!parser) return;

    if (parser->m_messageDescriptions) {
        XMap_delete_base(parser->m_messageDescriptions);
        parser->m_messageDescriptions = NULL;
    }
    if (parser->m_valueDescriptions) {
        XMap_delete_base(parser->m_valueDescriptions);
        parser->m_valueDescriptions = NULL;
    }
    if (parser->m_isProcessingMessage) {
        XCanMessageDescription_deinit(&parser->m_currentMessage);
        parser->m_isProcessingMessage = false;
    }
    if (parser->m_errorString) {
        XString_delete_base(parser->m_errorString);
        parser->m_errorString = NULL;
    }
    if (parser->m_warnings) {
        XStringList_delete_base(parser->m_warnings);
        parser->m_warnings = NULL;
    }

    parser->m_error = XCanDbcFileParser_Error_None;
    parser->m_seenExtraData = false;
    parser->m_lineOffset = 0;
    memset(&parser->m_currentMessage, 0, sizeof(XCanMessageDescription));
}

// =============== 解析接口 ===============

bool XCanDbcFileParser_parse(XCanDbcFileParser* parser, const char* fileName)
{
    if (!parser || !fileName) return false;
    resetParser(parser);
    return parseFileInternal(parser, fileName);
}

bool XCanDbcFileParser_parseFiles(XCanDbcFileParser* parser, const XStringList* fileNames)
{
    if (!parser || !fileNames) return false;
    resetParser(parser);

    size_t count = XStringList_size_base(fileNames);
    bool allSuccess = true;
    size_t totalLines = 0;

    for (size_t i = 0; i < count; i++) {
        XString* fileNameStr = (XString*)XStringList_at_base(fileNames, i);
        if (!fileNameStr) continue;

        const char* fname = XString_toUtf8(fileNameStr);
        if (!fname) continue;

        parser->m_lineOffset = totalLines;

        if (!parseFileInternal(parser, fname)) {
            allSuccess = false;
            /* 继续解析其他文件 */
        }

        /* 更新总行数（近似值） */
        totalLines += 1000; /* 估算值 */
    }

    return allSuccess;
}

bool XCanDbcFileParser_parseData(XCanDbcFileParser* parser, const char* data)
{
    if (!parser || !data) return false;
    resetParser(parser);
    return parseDataInternal(parser, data);
}

// =============== 内部文件解析 ===============

static bool parseFileInternal(XCanDbcFileParser* parser, const char* fileName)
{
    if (!parser || !fileName) return false;

    FILE* fp = fopen(fileName, "r");
    if (!fp) {
        parser->m_error = XCanDbcFileParser_Error_FileReading;
        if (parser->m_errorString) XString_delete_base(parser->m_errorString);
        parser->m_errorString = XString_create_fmt_utf8(
            "Cannot open file: '%s'", fileName);
        return false;
    }

    /* 保存文件名 */
    if (parser->m_fileName) {
        XFree_System(parser->m_fileName);
    }
    parser->m_fileName = XMemory_strdup(fileName);

    /* 读取整个文件到内存 */
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize <= 0) {
        fclose(fp);
        parser->m_error = XCanDbcFileParser_Error_FileReading;
        if (parser->m_errorString) XString_delete_base(parser->m_errorString);
        parser->m_errorString = XString_create_fmt_utf8(
            "Empty file: '%s'", fileName);
        return false;
    }

    char* buffer = (char*)XMalloc_System((size_t)fileSize + 1);
    if (!buffer) {
        fclose(fp);
        parser->m_error = XCanDbcFileParser_Error_FileReading;
        if (parser->m_errorString) XString_delete_base(parser->m_errorString);
        parser->m_errorString = XString_create_utf8("Memory allocation failed");
        return false;
    }

    size_t readSize = fread(buffer, 1, (size_t)fileSize, fp);
    fclose(fp);
    buffer[readSize] = '\0';

    bool result = parseDataInternal(parser, buffer);
    XFree_System(buffer);
    return result;
}

static bool parseDataInternal(XCanDbcFileParser* parser, const char* data)
{
    if (!parser || !data) return false;

    /* 按行解析 */
    char* workBuffer = XMemory_strdup(data);
    if (!workBuffer) {
        parser->m_error = XCanDbcFileParser_Error_Parsing;
        if (parser->m_errorString) XString_delete_base(parser->m_errorString);
        parser->m_errorString = XString_create_utf8("Memory allocation failed");
        return false;
    }

    char* line = workBuffer;
    char* nextLine = NULL;
    size_t lineNum = 0;

    while (line) {
        nextLine = strchr(line, '\n');
        if (nextLine) {
            *nextLine = '\0';
            nextLine++;
        }

        /* 去除行尾回车 */
        char* end = line + strlen(line);
        while (end > line && (*(end - 1) == '\r' || *(end - 1) == '\n')) end--;
        *end = '\0';

        /* 处理行 */
        char* trimmed = trimString(line);
        if (trimmed && *trimmed != '\0' && *trimmed != '/') {
            /* 跳过注释行（以 // 开头） */
            if (trimmed[0] == '/' && trimmed[1] == '/') {
                line = nextLine;
                lineNum++;
                continue;
            }
            if (!processLine(parser, trimmed)) {
                /* 解析错误，但继续处理后续行 */
            }
        }

        line = nextLine;
        lineNum++;
    }

    /* 处理最后一条消息 */
    addCurrentMessage(parser);

    /* 后处理多路复用 */
    postProcessSignalMultiplexing(parser);

    XFree_System(workBuffer);

    /* 检查是否有任何消息被解析 */
    if (!parser->m_messageDescriptions || XMap_size_base(parser->m_messageDescriptions) == 0) {
        if (parser->m_error == XCanDbcFileParser_Error_None) {
            parser->m_error = XCanDbcFileParser_Error_Parsing;
            if (parser->m_errorString) XString_delete_base(parser->m_errorString);
            parser->m_errorString = XString_create_utf8(
                "No valid message descriptions found in DBC data");
        }
        return false;
    }

    return parser->m_error == XCanDbcFileParser_Error_None;
}

// =============== 行处理 ===============

static bool processLine(XCanDbcFileParser* parser, const char* line)
{
    if (!parser || !line) return false;

    const char* p = skipSpaces(line);
    if (!p || *p == '\0') return true;

    /* 根据关键字分发处理 */
    if (strncmp(p, "BO_ ", 4) == 0) {
        /* 先保存当前消息（如果有） */
        addCurrentMessage(parser);
        return parseMessage(parser, p + 4);
    } else if (strncmp(p, "SG_ ", 4) == 0) {
        return parseSignal(parser, p + 4);
    } else if (strncmp(p, "SIG_VALTYPE_ ", 13) == 0) {
        parseSignalType(parser, p + 13);
        return true;
    } else if (strncmp(p, "SG_MUL_VAL_ ", 12) == 0) {
        parseExtendedMux(parser, p + 12);
        return true;
    } else if (strncmp(p, "CM_ ", 4) == 0) {
        parseComment(parser, p + 4);
        return true;
    } else if (strncmp(p, "VAL_ ", 5) == 0) {
        parseValueDescriptions(parser, p + 5);
        return true;
    }

    /* 其他关键字行被忽略 */
    return true;
}

// =============== 消息解析 ===============

static bool parseMessage(XCanDbcFileParser* parser, const char* data)
{
    bool msgNameAllocated = false;

    if (!parser || !data) return false;

    /* 格式: messageId messageName: messageSize transmitter */
    char* work = XMemory_strdup(data);
    if (!work) return false;

    char* savePtr = NULL;
    char* token = xStrtok(work, " ", &savePtr);

    if (!token) {
        XFree_System(work);
        addWarning(parser, "Invalid BO_ line: missing message ID");
        return false;
    }

    /* 解析消息 ID */
    XCanBus_UniqueId uid = (XCanBus_UniqueId)strtoul(token, NULL, 10);

    /* 解析消息名称 */
    token = xStrtok(NULL, " ", &savePtr);
    if (!token) {
        XFree_System(work);
        addWarning(parser, "Invalid BO_ line: missing message name");
        return false;
    }

    /* 名称可能包含冒号，需要处理 "name:" 格式 */
    char* msgName = token;
    char* colonPos = strchr(msgName, ':');
    if (colonPos) *colonPos = '\0';

    /* 解析消息大小 */
    uint8_t msgSize = 0;
    if (!colonPos) {
        /* 名称和大小之间用冒号分隔，可能在下一个 token */
        token = xStrtok(NULL, " ", &savePtr);
        if (token) {
            if (token[0] == ':') {
                msgSize = (uint8_t)atoi(token + 1);
            } else {
                /* 可能是 "name:size" 格式，但冒号在名称中 */
                /* 尝试从当前 token 找冒号 */
                char* c = strchr(token, ':');
                if (c) {
                    msgSize = (uint8_t)atoi(c + 1);
                    *c = '\0';
                    /* 合并到名称 */
                    size_t nameLen = strlen(msgName);
                    size_t extraLen = strlen(token);
                    char* newName = (char*)XMalloc_System(nameLen + extraLen + 2);
                    if (newName) {
                        strcpy(newName, msgName);
                        newName[nameLen] = ' ';
                        strcpy(newName + nameLen + 1, token);
                        msgName = newName;
                        msgNameAllocated = true;
                    }
                }
            }
        }
    } else {
        msgSize = (uint8_t)atoi(colonPos + 1);
    }

    /* 解析发送节点（可选） */
    token = xStrtok(NULL, " ", &savePtr);
    char* transmitter = token ? token : NULL;

    /* 创建消息描述 */
    XCanMessageDescription_init(&parser->m_currentMessage);
    XCanMessageDescription_setUniqueId(&parser->m_currentMessage, uid);
    XCanMessageDescription_setName(&parser->m_currentMessage, msgName);
    XCanMessageDescription_setSize(&parser->m_currentMessage, msgSize);
    if (transmitter) {
        XCanMessageDescription_setTransmitter(&parser->m_currentMessage, transmitter);
    }

    parser->m_isProcessingMessage = true;

    /* msgName 要么指向 work 内部，要么是合并名称时单独分配的。
       单独分配的情况由 msgNameAllocated 标记处理 */
    if (msgNameAllocated) {
        XFree_System(msgName);
    }

    XFree_System(work);
    return true;
}

// =============== 信号解析 ===============

static bool parseSignal(XCanDbcFileParser* parser, const char* data)
{
    if (!parser || !data) return false;
    if (!parser->m_isProcessingMessage) {
        addWarning(parser, "SG_ line outside of BO_ context, ignored");
        return false;
    }

    /* 格式: signalName multiplexerIndicator : startBit|bitLength@byteOrder+-(factor,offset) [min|max] unit receiver */
    char* work = XMemory_strdup(data);
    if (!work) return false;

    char* savePtr = NULL;

    /* 解析信号名称 */
    char* token = xStrtok(work, " ", &savePtr);
    if (!token) {
        XFree_System(work);
        addWarning(parser, "Invalid SG_ line: missing signal name");
        return false;
    }
    char* signalName = token;

    /* 检查多路复用指示符 */
    XCanBus_MultiplexState muxState = XCanBus_MultiplexState_None;
    int muxValue = -1;

    token = xStrtok(NULL, " ", &savePtr);
    if (!token) {
        XFree_System(work);
        addWarning(parser, "Invalid SG_ line: missing multiplexer indicator");
        return false;
    }

    if (token[0] == 'M') {
        /* 多路复用器开关 */
        muxState = XCanBus_MultiplexState_MultiplexorSwitch;
        if (strlen(token) > 1) {
            muxValue = atoi(token + 1);
        }
        token = xStrtok(NULL, " ", &savePtr);
    } else if (token[0] == 'm') {
        /* 多路复用信号 */
        muxState = XCanBus_MultiplexState_MultiplexedSignal;
        if (strlen(token) > 1) {
            muxValue = atoi(token + 1);
        }
        token = xStrtok(NULL, " ", &savePtr);
    }

    if (!token) {
        XFree_System(work);
        addWarning(parser, "Invalid SG_ line: missing colon");
        return false;
    }

    /* 跳过冒号 */
    if (token[0] == ':') {
        token = xStrtok(NULL, " ", &savePtr);
    } else {
        /* 冒号可能在下一个 token */
        token = xStrtok(NULL, " ", &savePtr);
    }

    if (!token) {
        XFree_System(work);
        addWarning(parser, "Invalid SG_ line: missing bit specification");
        return false;
    }

    /* 解析位规格: startBit|bitLength@byteOrder+-(factor,offset) */
    char* bitSpec = token;

    /* 提取 startBit */
    char* pipePos = strchr(bitSpec, '|');
    if (!pipePos) {
        XFree_System(work);
        addWarning(parser, "Invalid SG_ line: missing '|' in bit specification");
        return false;
    }
    *pipePos = '\0';
    uint16_t startBit = (uint16_t)atoi(bitSpec);

    /* 提取 bitLength */
    char* atPos = strchr(pipePos + 1, '@');
    if (!atPos) {
        XFree_System(work);
        addWarning(parser, "Invalid SG_ line: missing '@' in bit specification");
        return false;
    }
    *atPos = '\0';
    uint16_t bitLength = (uint16_t)atoi(pipePos + 1);

    /* 提取字节序和符号 */
    uint8_t endian = 1; /* 默认大端 */
    bool isSigned = false;

    char* endianChar = atPos + 1;
    if (*endianChar == '0') {
        endian = 0; /* 小端（Intel） */
    } else if (*endianChar == '1') {
        endian = 1; /* 大端（Motorola） */
    }

    /* 提取符号 */
    char* signPos = endianChar + 1;
    if (*signPos == '+') {
        isSigned = false;
    } else if (*signPos == '-') {
        isSigned = true;
    }

    /* 提取因子和偏移量 */
    double factor = 1.0;
    double offset = 0.0;

    char* factorStart = strchr(signPos, '(');
    if (factorStart) {
        factorStart++;
        char* commaPos = strchr(factorStart, ',');
        if (commaPos) {
            *commaPos = '\0';
            factor = atof(factorStart);
            char* closeParen = strchr(commaPos + 1, ')');
            if (closeParen) {
                *closeParen = '\0';
                offset = atof(commaPos + 1);
            }
        }
    }

    /* 提取范围 [min|max] */
    double minVal = 0.0, maxVal = 0.0;
    char* rangeStart = strchr(signPos, '[');
    if (!rangeStart) {
        /* 可能在因子之后 */
        rangeStart = strchr(signPos, ' ');
        while (rangeStart && *rangeStart != '[') {
            rangeStart = strchr(rangeStart + 1, '[');
        }
    }
    if (rangeStart) {
        rangeStart++;
        char* pipePos2 = strchr(rangeStart, '|');
        if (pipePos2) {
            *pipePos2 = '\0';
            minVal = atof(rangeStart);
            char* closeBracket = strchr(pipePos2 + 1, ']');
            if (closeBracket) {
                *closeBracket = '\0';
                maxVal = atof(pipePos2 + 1);
            }
        }
    }

    /* 提取单位（在范围之后，引号内） */
    char* unitStr = NULL;
    char* unitStart = strchr(signPos, '"');
    if (!unitStart) {
        /* 可能在范围之后 */
        unitStart = strrchr(signPos, '"');
        if (unitStart) {
            /* 向前找开始引号 */
            char* tmp = unitStart - 1;
            while (tmp >= signPos && *tmp != '"') tmp--;
            if (tmp >= signPos) unitStart = tmp;
        }
    }
    if (unitStart) {
        const char* quotePtr = unitStart;
        unitStr = extractQuotedString(&quotePtr);
    }

    /* 提取接收节点（在单位之后） */
    char* receiver = NULL;
    if (unitStr) {
        /* 接收节点在单位之后 */
        const char* afterUnit = strchr(signPos, '"');
        if (afterUnit) {
            afterUnit = strchr(afterUnit + 1, '"');
            if (afterUnit) {
                afterUnit++;
                receiver = XMemory_strdup(trimString((char*)afterUnit));
            }
        }
    }

    /* 创建信号描述 */
    XCanSignalDescription sigDesc;
    XCanSignalDescription_init(&sigDesc);
    XCanSignalDescription_setName(&sigDesc, signalName);
    XCanSignalDescription_setStartBit(&sigDesc, startBit);
    XCanSignalDescription_setBitLength(&sigDesc, bitLength);
    XCanSignalDescription_setDataEndian(&sigDesc, endian);
    XCanSignalDescription_setFactor(&sigDesc, factor);
    XCanSignalDescription_setOffset(&sigDesc, offset);
    XCanSignalDescription_setRange(&sigDesc, minVal, maxVal);
    XCanSignalDescription_setMultiplexState(&sigDesc, muxState);

    if (isSigned) {
        XCanSignalDescription_setDataFormat(&sigDesc, XCanBus_SignedInteger);
    } else {
        XCanSignalDescription_setDataFormat(&sigDesc, XCanBus_UnsignedInteger);
    }

    if (unitStr) {
        XCanSignalDescription_setPhysicalUnit(&sigDesc, unitStr);
        XFree_System(unitStr);
    }

    if (receiver) {
        XCanSignalDescription_setReceiver(&sigDesc, receiver);
        XFree_System(receiver);
    }

    /* 处理多路复用值 */
    if (muxValue >= 0) {
        /* 对于简单多路复用，使用虚拟信号名称 */
        XString dummyKey;
        XString_init(&dummyKey);
        XString_assign_utf8(&dummyKey, K_QT_DUMMY_SIGNAL);

        XCanSignalDescription_MultiplexValueRange muxRange;
        memset(&muxRange, 0, sizeof(muxRange));

        XVariant* minVar = (XVariant*)XMalloc_System(sizeof(XVariant));
        XVariant* maxVar = (XVariant*)XMalloc_System(sizeof(XVariant));
        if (minVar && maxVar) {
            XVariant_init(minVar, NULL, 0, XVariantType_Int);
            XVariant_init(maxVar, NULL, 0, XVariantType_Int);
            XVariant_setValue_int(minVar, muxValue);
            XVariant_setValue_int(maxVar, muxValue);
            muxRange.m_minimum = minVar;
            muxRange.m_maximum = maxVar;
        }

        XMap* muxSignals = XMap_create(sizeof(XString), sizeof(XCanSignalDescription_MultiplexValueRange), XString_compare);
        XMapBaseSetKeyCopyMethod(muxSignals, XString_copy_base);
        XMapBaseSetKeyMoveMethod(muxSignals, XString_move_base);
        XMapBaseSetKeyDeinitMethod(muxSignals, XString_deinit_base);
        XMapBase_insert_base((XMapBase*)muxSignals, &dummyKey, &muxRange);

        XCanSignalDescription_setMultiplexSignals(&sigDesc, muxSignals);
        XMap_delete_base(muxSignals);

        XClass_deinit_base((XClass*)&dummyKey);
    }

    /* 添加到当前消息 */
    XCanMessageDescription_addSignalDescription(&parser->m_currentMessage, &sigDesc);

    XCanSignalDescription_deinit(&sigDesc);
    XFree_System(work);
    return true;
}

// =============== 信号类型解析 ===============

static void parseSignalType(XCanDbcFileParser* parser, const char* data)
{
    if (!parser || !data) return;

    /* 格式: messageId signalName signalType */
    char* work = XMemory_strdup(data);
    if (!work) return;

    char* savePtr = NULL;
    char* uidStr = xStrtok(work, " ", &savePtr);
    char* sigName = xStrtok(NULL, " ", &savePtr);
    char* typeStr = xStrtok(NULL, " ", &savePtr);

    if (!uidStr || !sigName || !typeStr) {
        XFree_System(work);
        return;
    }

    XCanBus_UniqueId uid = (XCanBus_UniqueId)strtoul(uidStr, NULL, 10);
    int sigType = atoi(typeStr);

    /* 查找消息 */
    XCanMessageDescription* msgDesc = findMessageByUid(parser, uid);
    if (!msgDesc || !msgDesc->m_signalDescriptions) {
        XFree_System(work);
        return;
    }

    /* 查找信号并设置类型 */
    size_t count = XVector_size_base(msgDesc->m_signalDescriptions);
    for (size_t i = 0; i < count; i++) {
        XCanSignalDescription* sig = (XCanSignalDescription*)XVector_at_base(msgDesc->m_signalDescriptions, i);
        if (sig && sig->m_name) {
            const char* name = XString_toUtf8(sig->m_name);
            if (name && strcmp(name, sigName) == 0) {
                switch (sigType) {
                case 0: /* 未定义/默认 */
                    break;
                case 1: /* 有符号整数 */
                    XCanSignalDescription_setDataFormat(sig, XCanBus_SignedInteger);
                    break;
                case 2: /* 无符号整数 */
                    XCanSignalDescription_setDataFormat(sig, XCanBus_UnsignedInteger);
                    break;
                case 3: /* 单精度浮点 */
                    XCanSignalDescription_setDataFormat(sig, XCanBus_Float);
                    break;
                case 4: /* 双精度浮点 */
                    XCanSignalDescription_setDataFormat(sig, XCanBus_Double);
                    break;
                case 5: /* ASCII 字符串 */
                    XCanSignalDescription_setDataFormat(sig, XCanBus_AsciiString);
                    break;
                }
                break;
            }
        }
    }

    XFree_System(work);
}

// =============== 注释解析 ===============

static void parseComment(XCanDbcFileParser* parser, const char* data)
{
    if (!parser || !data) return;

    /* 格式: CM_ SG_ messageId signalName "comment" */
    /* 格式: CM_ BO_ messageId "comment" */
    /* 格式: CM_ "comment" (全局注释，忽略) */
    char* work = XMemory_strdup(data);
    if (!work) return;

    char* savePtr = NULL;
    char* objType = xStrtok(work, " ", &savePtr);

    if (!objType) {
        XFree_System(work);
        return;
    }

    if (strcmp(objType, "SG_") == 0) {
        /* 信号注释 */
        char* uidStr = xStrtok(NULL, " ", &savePtr);
        char* sigName = xStrtok(NULL, " ", &savePtr);
        if (!uidStr || !sigName) {
            XFree_System(work);
            return;
        }

        /* 提取注释文本 */
        const char* remaining = savePtr;
        char* comment = extractQuotedString(&remaining);

        XCanBus_UniqueId uid = (XCanBus_UniqueId)strtoul(uidStr, NULL, 10);
        XCanMessageDescription* msgDesc = findMessageByUid(parser, uid);
        if (msgDesc && msgDesc->m_signalDescriptions && comment) {
            size_t count = XVector_size_base(msgDesc->m_signalDescriptions);
            for (size_t i = 0; i < count; i++) {
                XCanSignalDescription* sig = (XCanSignalDescription*)XVector_at_base(msgDesc->m_signalDescriptions, i);
                if (sig && sig->m_name) {
                    const char* name = XString_toUtf8(sig->m_name);
                    if (name && strcmp(name, sigName) == 0) {
                        XCanSignalDescription_setComment(sig, comment);
                        break;
                    }
                }
            }
        }

        if (comment) XFree_System(comment);

    } else if (strcmp(objType, "BO_") == 0) {
        /* 消息注释 */
        char* uidStr = xStrtok(NULL, " ", &savePtr);
        if (!uidStr) {
            XFree_System(work);
            return;
        }

        const char* remaining = savePtr;
        char* comment = extractQuotedString(&remaining);

        XCanBus_UniqueId uid = (XCanBus_UniqueId)strtoul(uidStr, NULL, 10);
        XCanMessageDescription* msgDesc = findMessageByUid(parser, uid);
        if (msgDesc && comment) {
            XCanMessageDescription_setComment(msgDesc, comment);
        }

        if (comment) XFree_System(comment);
    }

    XFree_System(work);
}

// =============== 扩展多路复用解析 ===============

static void parseExtendedMux(XCanDbcFileParser* parser, const char* data)
{
    if (!parser || !data) return;

    /* 格式: messageId multiplexedSignalName switchName switchValue1-switchValue2 ... ; */
    char* work = XMemory_strdup(data);
    if (!work) return;

    char* savePtr = NULL;
    char* uidStr = xStrtok(work, " ", &savePtr);
    char* muxedSigName = xStrtok(NULL, " ", &savePtr);
    char* switchName = xStrtok(NULL, " ", &savePtr);

    if (!uidStr || !muxedSigName || !switchName) {
        XFree_System(work);
        return;
    }

    XCanBus_UniqueId uid = (XCanBus_UniqueId)strtoul(uidStr, NULL, 10);

    /* 解析开关值范围列表 */
    XVector* valueRanges = XVector_create(sizeof(XCanSignalDescription_MultiplexValueRange));

    char* token = xStrtok(NULL, " ", &savePtr);
    while (token && token[0] != ';') {
        XCanSignalDescription_MultiplexValueRange range;
        memset(&range, 0, sizeof(range));

        char* dashPos = strchr(token, '-');
        if (dashPos) {
            *dashPos = '\0';
            int32_t minVal = atoi(token);
            int32_t maxVal = atoi(dashPos + 1);

            XVariant* minVar = (XVariant*)XMalloc_System(sizeof(XVariant));
            XVariant* maxVar = (XVariant*)XMalloc_System(sizeof(XVariant));
            if (minVar && maxVar) {
                XVariant_init(minVar, NULL, 0, XVariantType_Int);
                XVariant_init(maxVar, NULL, 0, XVariantType_Int);
                XVariant_setValue_int(minVar, minVal);
                XVariant_setValue_int(maxVar, maxVal);
                range.m_minimum = minVar;
                range.m_maximum = maxVar;
            }
        } else {
            int32_t val = atoi(token);
            XVariant* minVar = (XVariant*)XMalloc_System(sizeof(XVariant));
            XVariant* maxVar = (XVariant*)XMalloc_System(sizeof(XVariant));
            if (minVar && maxVar) {
                XVariant_init(minVar, NULL, 0, XVariantType_Int);
                XVariant_init(maxVar, NULL, 0, XVariantType_Int);
                XVariant_setValue_int(minVar, val);
                XVariant_setValue_int(maxVar, val);
                range.m_minimum = minVar;
                range.m_maximum = maxVar;
            }
        }

        XVector_push_back_1_base(valueRanges, &range);
        token = xStrtok(NULL, " ", &savePtr);
    }

    /* 查找消息和信号 */
    XCanMessageDescription* msgDesc = findMessageByUid(parser, uid);
    if (msgDesc && msgDesc->m_signalDescriptions) {
        size_t count = XVector_size_base(msgDesc->m_signalDescriptions);
        for (size_t i = 0; i < count; i++) {
            XCanSignalDescription* sig = (XCanSignalDescription*)XVector_at_base(msgDesc->m_signalDescriptions, i);
            if (sig && sig->m_name) {
                const char* name = XString_toUtf8(sig->m_name);
                if (name && strcmp(name, muxedSigName) == 0) {
                    /* 设置多路复用状态为 SwitchAndSignal */
                    XCanSignalDescription_setMultiplexState(sig,
                        XCanBus_MultiplexState_SwitchAndSignal);
                    /* 添加多路复用信号 */
                    XCanSignalDescription_addMultiplexSignal(sig, switchName, valueRanges);
                    break;
                }
            }
        }
    }

    XVector_delete_base(valueRanges);
    XFree_System(work);
}

// =============== 值描述解析 ===============

static void parseValueDescriptions(XCanDbcFileParser* parser, const char* data)
{
    if (!parser || !data) return;

    /* 格式: messageId signalName value1 "desc1" value2 "desc2" ... ; */
    char* work = XMemory_strdup(data);
    if (!work) return;

    char* savePtr = NULL;
    char* uidStr = xStrtok(work, " ", &savePtr);
    char* sigName = xStrtok(NULL, " ", &savePtr);

    if (!uidStr || !sigName) {
        XFree_System(work);
        return;
    }

    XCanBus_UniqueId uid = (XCanBus_UniqueId)strtoul(uidStr, NULL, 10);

    /* 初始化值描述映射表（如果需要） */
    if (!parser->m_valueDescriptions) {
        parser->m_valueDescriptions = (XCanDbcFileParser_MessageValueDescriptions*)
            XMap_create(sizeof(XCanBus_UniqueId), sizeof(XCanDbcFileParser_SignalValueDescriptions), uint32_t_compare);
    }

    /* 查找或创建信号值描述映射表 */
    XCanDbcFileParser_SignalValueDescriptions* sigValueDesc = NULL;
    {
        XCanBus_UniqueId uidKey = uid;
        sigValueDesc = (XCanDbcFileParser_SignalValueDescriptions*)
            XMap_value_base(parser->m_valueDescriptions, &uidKey);
    }

    if (!sigValueDesc) {
        /* 创建新的信号值描述映射表 */
        XCanDbcFileParser_SignalValueDescriptions newSigDesc;
        memset(&newSigDesc, 0, sizeof(newSigDesc));
        /* 使用 XMap_init 初始化 */
        XMap* tmpMap = XMap_create(sizeof(XString), sizeof(XCanDbcFileParser_ValueDescriptions), XString_compare);
        XMapBaseSetKeyCopyMethod(tmpMap, XString_copy_base);
        XMapBaseSetKeyMoveMethod(tmpMap, XString_move_base);
        XMapBaseSetKeyDeinitMethod(tmpMap, XString_deinit_base);

        XCanBus_UniqueId uidKey = uid;
        XMapBase_insert_base((XMapBase*)parser->m_valueDescriptions, &uidKey, tmpMap);
        XMap_delete_base(tmpMap);

        sigValueDesc = (XCanDbcFileParser_SignalValueDescriptions*)
            XMap_value_base(parser->m_valueDescriptions, &uidKey);
    }

    /* 解析值描述对 */
    const char* remaining = savePtr;
    while (remaining && *remaining) {
        remaining = skipSpaces(remaining);
        if (*remaining == ';' || *remaining == '\0') break;

        /* 解析值 */
        char* endPtr = NULL;
        uint32_t value = (uint32_t)strtoul(remaining, &endPtr, 10);
        if (endPtr == remaining) break;
        remaining = skipSpaces(endPtr);

        /* 解析描述 */
        char* desc = extractQuotedString(&remaining);
        if (!desc) break;

        /* 存储到值描述映射表 */
        if (sigValueDesc) {
            /* 查找或创建信号的值描述 */
            XString sigKey;
            XString_init(&sigKey);
            XString_assign_utf8(&sigKey, sigName);

            XCanDbcFileParser_ValueDescriptions* valDesc = NULL;
            {
                valDesc = (XCanDbcFileParser_ValueDescriptions*)
                    XMap_value_base(sigValueDesc, &sigKey);
            }

            if (!valDesc) {
                XCanDbcFileParser_ValueDescriptions newValDesc;
                memset(&newValDesc, 0, sizeof(newValDesc));
                XMap* tmpValMap = XMap_create(sizeof(uint32_t), sizeof(XString), uint32_t_compare);
                XMapBase_insert_base((XMapBase*)sigValueDesc, &sigKey, tmpValMap);
                XMap_delete_base(tmpValMap);

                valDesc = (XCanDbcFileParser_ValueDescriptions*)
                    XMap_value_base(sigValueDesc, &sigKey);
            }

            if (valDesc) {
                XString descStr;
                XString_init(&descStr);
                XString_assign_utf8(&descStr, desc);
                XMapBase_insert_base((XMapBase*)valDesc, &value, &descStr);
                XClass_deinit_base((XClass*)&descStr);
            }

            XClass_deinit_base((XClass*)&sigKey);
        }

        XFree_System(desc);
    }

    XFree_System(work);
}

// =============== 多路复用后处理 ===============

static void postProcessSignalMultiplexing(XCanDbcFileParser* parser)
{
    if (!parser || !parser->m_messageDescriptions) return;

    /* 收集需要移除的消息 UID */
    XVector* uidsToRemove = XVector_create(sizeof(XCanBus_UniqueId));

    XMap_iterator it = XMap_begin(parser->m_messageDescriptions);
    while (!XMap_iterator_isEnd(&it)) {
        XPair* pair = XMap_iterator_data(&it);
        if (!pair) {
            XMap_iterator_add(parser->m_messageDescriptions, &it);
            continue;
        }

        XCanMessageDescription* msgDesc = (XCanMessageDescription*)XPair_second(pair);
        if (!msgDesc || !msgDesc->m_signalDescriptions) {
            XMap_iterator_add(parser->m_messageDescriptions, &it);
            continue;
        }

        bool useExtendedMux = false;
        XString* multiplexorSignal = NULL;
        bool hasMultipleMux = false;

        size_t sigCount = XVector_size_base(msgDesc->m_signalDescriptions);
        for (size_t i = 0; i < sigCount; i++) {
            XCanSignalDescription* sigDesc = (XCanSignalDescription*)
                XVector_at_base(msgDesc->m_signalDescriptions, i);
            if (!sigDesc) continue;

            XCanBus_MultiplexState muxState = sigDesc->m_multiplexState;
            if (muxState == XCanBus_MultiplexState_MultiplexorSwitch) {
                if (!multiplexorSignal) {
                    multiplexorSignal = sigDesc->m_name;
                } else {
                    /* 多个多路复用器开关，无效配置 */
                    hasMultipleMux = true;
                    XCanBus_UniqueId uid = msgDesc->m_uniqueId;
                    XVector_push_back_1_base(uidsToRemove, &uid);
                    break;
                }
            } else if (muxState == XCanBus_MultiplexState_SwitchAndSignal) {
                useExtendedMux = true;
            }
        }

        if (!hasMultipleMux) {
            if (!useExtendedMux && multiplexorSignal) {
                /* 简单多路复用：替换虚拟信号名称为实际的多路复用器名称 */
                for (size_t i = 0; i < sigCount; i++) {
                    XCanSignalDescription* sigDesc = (XCanSignalDescription*)
                        XVector_at_base(msgDesc->m_signalDescriptions, i);
                    if (!sigDesc) continue;

                    if (sigDesc->m_multiplexState == XCanBus_MultiplexState_MultiplexedSignal) {
                        XMap* muxSignals = sigDesc->m_multiplexSignals;
                        if (muxSignals) {
                            /* 检查是否有虚拟信号条目 */
                            XString dummyKey;
                            XString_init(&dummyKey);
                            XString_assign_utf8(&dummyKey, K_QT_DUMMY_SIGNAL);

                            XCanSignalDescription_MultiplexValueRange* val =
                                (XCanSignalDescription_MultiplexValueRange*)
                                XMap_value_base(muxSignals, &dummyKey);
                            if (val) {
                                /* 替换为实际的多路复用器名称 */
                                XMap_remove_base(muxSignals, &dummyKey);

                                XString muxKey;
                                XString_init(&muxKey);
                                XString_assign_utf8(&muxKey, XString_toUtf8(multiplexorSignal));
                                XMapBase_insert_base((XMapBase*)muxSignals, &muxKey, val);
                                XClass_deinit_base((XClass*)&muxKey);
                            }

                            XClass_deinit_base((XClass*)&dummyKey);
                        }
                    }
                }
            } else if (useExtendedMux) {
                /* 扩展多路复用：检查是否有无效的虚拟信号条目 */
                for (size_t i = 0; i < sigCount; i++) {
                    XCanSignalDescription* sigDesc = (XCanSignalDescription*)
                        XVector_at_base(msgDesc->m_signalDescriptions, i);
                    if (!sigDesc) continue;

                    XMap* muxSignals = sigDesc->m_multiplexSignals;
                    if (muxSignals) {
                        XString dummyKey;
                        XString_init(&dummyKey);
                        XString_assign_utf8(&dummyKey, K_QT_DUMMY_SIGNAL);

                        XCanSignalDescription_MultiplexValueRange* val =
                            (XCanSignalDescription_MultiplexValueRange*)
                            XMap_value_base(muxSignals, &dummyKey);
                        if (val) {
                            /* 存在虚拟信号条目，说明扩展多路复用解析有误 */
                            XCanBus_UniqueId uid = msgDesc->m_uniqueId;
                            XVector_push_back_1_base(uidsToRemove, &uid);
                            XClass_deinit_base((XClass*)&dummyKey);
                            break;
                        }
                        XClass_deinit_base((XClass*)&dummyKey);
                    }
                }
            }
        }

        XMap_iterator_add(parser->m_messageDescriptions, &it);
    }

    /* 移除无效的消息描述 */
    size_t removeCount = XVector_size_base(uidsToRemove);
    for (size_t i = 0; i < removeCount; i++) {
        XCanBus_UniqueId* uid = (XCanBus_UniqueId*)XVector_at_base(uidsToRemove, i);
        if (uid) {
            XMap_remove_base(parser->m_messageDescriptions, uid);
            char warning[128];
            snprintf(warning, sizeof(warning),
                "Message description with unique id %u is skipped because "
                "it has invalid multiplexing description.", (unsigned int)*uid);
            addWarning(parser, warning);
        }
    }

    XVector_delete_base(uidsToRemove);
}

// =============== 辅助方法 ===============

static void addCurrentMessage(XCanDbcFileParser* parser)
{
    if (!parser || !parser->m_isProcessingMessage) return;

    XCanBus_UniqueId uid = parser->m_currentMessage.m_uniqueId;

    if (!XCanMessageDescription_isValid(&parser->m_currentMessage)) {
        char warning[128];
        snprintf(warning, sizeof(warning),
            "Message description with unique id %u is skipped because it's not valid.",
            (unsigned int)uid);
        addWarning(parser, warning);
    } else if (parser->m_messageDescriptions) {
        /* 检查是否已存在相同 UID 的消息 */
        XCanMessageDescription* existing = (XCanMessageDescription*)
            XMap_value_base(parser->m_messageDescriptions, &uid);
        if (existing) {
            char warning[128];
            snprintf(warning, sizeof(warning),
                "Message description with unique id %u is skipped because "
                "such unique id is already used.", (unsigned int)uid);
            addWarning(parser, warning);
        } else {
            XMapBase_insert_base((XMapBase*)parser->m_messageDescriptions, &uid, &parser->m_currentMessage);
        }
    } else {
        /* 首次添加消息，先创建消息描述映射表 */
        parser->m_messageDescriptions = XMap_create(sizeof(XCanBus_UniqueId),
            sizeof(XCanMessageDescription), uint32_t_compare);
        /* 设置值的深拷贝/移动/析构方法，确保 map 内元素正确管理生命周期 */
        XContainerSetDataCopyMethod(parser->m_messageDescriptions, XCanMessageDescription_copy);
        XContainerSetDataMoveMethod(parser->m_messageDescriptions, XCanMessageDescription_move);
        XContainerSetDataDeinitMethod(parser->m_messageDescriptions, XCanMessageDescription_deinit);
        XMapBase_insert_base((XMapBase*)parser->m_messageDescriptions, &uid, &parser->m_currentMessage);
    }

    XCanMessageDescription_deinit(&parser->m_currentMessage);
    memset(&parser->m_currentMessage, 0, sizeof(XCanMessageDescription));
    parser->m_isProcessingMessage = false;
}

static void addWarning(XCanDbcFileParser* parser, const char* warning)
{
    if (!parser || !warning) return;
    if (!parser->m_warnings) {
        parser->m_warnings = XStringList_create();
    }
    XStringList_push_back_utf8(parser->m_warnings, warning);
}

static XCanMessageDescription* findMessageByUid(XCanDbcFileParser* parser, XCanBus_UniqueId uid)
{
    if (!parser || !parser->m_messageDescriptions) return NULL;

    /* 先检查当前正在处理的消息 */
    if (parser->m_isProcessingMessage && parser->m_currentMessage.m_uniqueId == uid) {
        return &parser->m_currentMessage;
    }

    return (XCanMessageDescription*)XMap_value_base(parser->m_messageDescriptions, &uid);
}

// =============== 结果查询 ===============

XVector* XCanDbcFileParser_messageDescriptions(const XCanDbcFileParser* parser)
{
    if (!parser || !parser->m_messageDescriptions) {
        return XVector_create(sizeof(XCanMessageDescription));
    }

    XVector* result = XVector_create(sizeof(XCanMessageDescription));

    XMap_iterator it = XMap_begin(parser->m_messageDescriptions);
    while (!XMap_iterator_isEnd(&it)) {
        XPair* pair = XMap_iterator_data(&it);
        if (pair) {
            XCanMessageDescription* msgDesc = (XCanMessageDescription*)XPair_second(pair);
            if (msgDesc) {
                XVector_push_back_1_base(result, msgDesc);
            }
        }
        XMap_iterator_add(parser->m_messageDescriptions, &it);
    }

    return result;
}

XCanDbcFileParser_MessageValueDescriptions* XCanDbcFileParser_messageValueDescriptions(
    const XCanDbcFileParser* parser)
{
    if (!parser || !parser->m_valueDescriptions) {
        return (XCanDbcFileParser_MessageValueDescriptions*)
            XMap_create(sizeof(XCanBus_UniqueId), sizeof(XCanDbcFileParser_SignalValueDescriptions), uint32_t_compare);
    }
    return (XCanDbcFileParser_MessageValueDescriptions*)XMap_create_copy(parser->m_valueDescriptions);
}

// =============== 错误/警告查询 ===============

XCanDbcFileParser_Error XCanDbcFileParser_error(const XCanDbcFileParser* parser)
{
    return parser ? parser->m_error : XCanDbcFileParser_Error_None;
}

XString* XCanDbcFileParser_errorString(const XCanDbcFileParser* parser)
{
    if (!parser || !parser->m_errorString) return XString_create();
    return XString_create_copy(parser->m_errorString);
}

XStringList* XCanDbcFileParser_warnings(const XCanDbcFileParser* parser)
{
    if (!parser || !parser->m_warnings) return XStringList_create();
    return XStringList_create_copy(parser->m_warnings);
}

// =============== 静态工具方法 ===============

void XCanDbcFileParser_uniqueIdDescription(XCanUniqueIdDescription* out)
{
    if (!out) return;
    XCanUniqueIdDescription_init(out);
    /* DBC 格式使用帧 ID 作为唯一标识符 */
    out->m_source = XCanBus_FrameId;
    out->m_startBit = 0;
    out->m_bitLength = 29; /* 扩展帧 29 位 ID */
    out->m_endian = 1;     /* 大端 */
}

#endif /* XCAN_DBC_ON */
#endif /* XCAN_ON */
#endif /* XPROTOCOL_ON */
