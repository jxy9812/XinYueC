#ifndef XCANDBCFILEPARSER_H
#define XCANDBCFILEPARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XCanCommonDefinitions.h"
#include "XCanMessageDescription.h"
#include "XCanUniqueIdDescription.h"
#include "XString.h"
#include "XStringList.h"
#include "XMap.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanDbcFileParser.h
 * @brief CAN DBC 文件解析器头文件（对齐 Qt6 QCanDbcFileParser）
 * @details 提供解析 CAN 数据库（DBC）文件的功能。DBC 文件是 ASCII 文本文件，
 *          包含如何解码和解释原始 CAN 总线数据的信息。
 *          解析器将 DBC 文件解析为 XCanMessageDescription 列表，
 *          这些消息描述可以传递给 XCanFrameProcessor 用于编码或解码 CAN 帧。
 *
 * @par 功能特性
 * - 解析单个或多个 DBC 文件
 * - 解析内存中的 DBC 数据
 * - 获取解析后的消息描述列表
 * - 获取信号值的文本描述（VAL_ 条目）
 * - 错误和警告信息查询
 * - 静态方法获取 DBC 格式的唯一 ID 描述
 *
 * @par 支持的 DBC 关键字
 * - BO_ - 消息定义
 * - SG_ - 信号定义
 * - SIG_VALTYPE_ - 信号类型定义
 * - SG_MUL_VAL_ - 扩展多路复用描述
 * - CM_ - 注释（仅消息和信号描述）
 * - VAL_ - 信号原始值的文本描述
 *
 * @par 使用示例
 * @code
 * XCanDbcFileParser parser;
 * XCanDbcFileParser_init(&parser);
 *
 * // 解析 DBC 文件
 * if (XCanDbcFileParser_parse(&parser, "path/to/file.dbc")) {
 *     // 获取消息描述列表
 *     XVector* messages = XCanDbcFileParser_messageDescriptions(&parser);
 *
 *     // 配置帧处理器
 *     XCanFrameProcessor frameProcessor;
 *     XCanFrameProcessor_init(&frameProcessor);
 *     XCanUniqueIdDescription uidDesc;
 *     XCanDbcFileParser_uniqueIdDescription(&uidDesc);
 *     XCanFrameProcessor_setUniqueIdDescription(&frameProcessor, &uidDesc);
 *     XCanFrameProcessor_setMessageDescriptions(&frameProcessor, messages);
 *
 *     XVector_delete_base(messages);
 *     XCanFrameProcessor_deinit(&frameProcessor);
 * }
 *
 * // 检查错误
 * if (XCanDbcFileParser_error(&parser) != XCanDbcFileParser_Error_None) {
 *     XString* err = XCanDbcFileParser_errorString(&parser);
 *     // 处理错误...
 *     XString_delete_base(err);
 * }
 *
 * XCanDbcFileParser_deinit(&parser);
 * @endcode
 *
 * @note 解析器是有状态的，每次解析开始时会重置所有结果
 * @sa XCanMessageDescription, XCanFrameProcessor
 */

/******************************************************************************************
 * 类型别名定义（对齐 Qt6 QCanDbcFileParser 的类型别名）
 ******************************************************************************************/

/**
 * @brief 值描述映射表：原始信号值 -> 文本描述
 * @details 对应 Qt6 QCanDbcFileParser::ValueDescriptions
 *          键为原始信号值（uint32_t），值为对应的文本描述字符串
 */
typedef XMap XCanDbcFileParser_ValueDescriptions;

/**
 * @brief 信号值描述映射表：信号名称 -> 值描述映射表
 * @details 对应 Qt6 QCanDbcFileParser::SignalValueDescriptions
 *          键为信号名称（XString），值为该信号的值描述映射表
 */
typedef XMap XCanDbcFileParser_SignalValueDescriptions;

/**
 * @brief 消息值描述映射表：消息唯一 ID -> 信号值描述映射表
 * @details 对应 Qt6 QCanDbcFileParser::MessageValueDescriptions
 *          键为消息唯一 ID（XCanBus_UniqueId），值为该消息的信号值描述映射表
 */
typedef XMap XCanDbcFileParser_MessageValueDescriptions;

/******************************************************************************************
 * 错误枚举
 ******************************************************************************************/

/**
 * @brief DBC 文件解析器错误枚举
 * @details 对齐 Qt6 QCanDbcFileParser::Error
 */
typedef enum {
    XCanDbcFileParser_Error_None = 0,       ///< 无错误
    XCanDbcFileParser_Error_FileReading,    ///< 文件打开或读取错误
    XCanDbcFileParser_Error_Parsing         ///< 解析内容时错误
} XCanDbcFileParser_Error;

/******************************************************************************************
 * DBC 文件解析器结构体
 ******************************************************************************************/

/**
 * @brief CAN DBC 文件解析器结构体
 * @details 管理 DBC 文件的解析过程，存储解析结果、错误信息和警告列表。
 *          解析器是有状态的，每次 parse 调用会重置之前的结果。
 */
typedef struct XCanDbcFileParser {
    /* 解析结果 */
    XMap* m_messageDescriptions;            ///< 消息描述映射表（XMap<XCanBus_UniqueId, XCanMessageDescription>）
    XCanDbcFileParser_MessageValueDescriptions* m_valueDescriptions; ///< 值描述映射表

    /* 当前解析状态 */
    bool m_isProcessingMessage;             ///< 是否正在处理消息
    XCanMessageDescription m_currentMessage;///< 当前正在处理的消息描述
    bool m_seenExtraData;                   ///< 是否已看到额外数据

    /* 错误和警告 */
    XCanDbcFileParser_Error m_error;        ///< 当前错误码
    XString* m_errorString;                 ///< 错误描述字符串
    XStringList* m_warnings;                ///< 警告列表

    /* 文件解析状态 */
    char* m_fileName;                       ///< 当前解析的文件名
    size_t m_lineOffset;                    ///< 行号偏移（多文件解析时使用）
} XCanDbcFileParser;

/******************************************************************************************
 * 初始化与清理
 ******************************************************************************************/

/**
 * @brief 初始化 DBC 文件解析器
 * @param parser 待初始化的解析器指针（不可为 NULL）
 */
void XCanDbcFileParser_init(XCanDbcFileParser* parser);

/**
 * @brief 销毁 DBC 文件解析器（释放内部资源）
 * @param parser 待销毁的解析器指针（可为 NULL）
 */
void XCanDbcFileParser_deinit(XCanDbcFileParser* parser);

/******************************************************************************************
 * 解析接口（对齐 QCanDbcFileParser）
 ******************************************************************************************/

/**
 * @brief 解析单个 DBC 文件
 * @param parser 解析器指针（不可为 NULL）
 * @param fileName DBC 文件路径
 * @return 解析成功返回 true，失败返回 false
 * @note 调用此方法会重置之前的所有解析结果
 */
bool XCanDbcFileParser_parse(XCanDbcFileParser* parser, const char* fileName);

/**
 * @brief 解析多个 DBC 文件
 * @param parser 解析器指针（不可为 NULL）
 * @param fileNames 文件路径列表
 * @return 所有文件解析成功返回 true，任一文件失败返回 false
 * @note 调用此方法会重置之前的所有解析结果
 */
bool XCanDbcFileParser_parseFiles(XCanDbcFileParser* parser, const XStringList* fileNames);

/**
 * @brief 解析内存中的 DBC 数据
 * @param parser 解析器指针（不可为 NULL）
 * @param data DBC 格式的字符串数据
 * @return 解析成功返回 true，失败返回 false
 * @note 调用此方法会重置之前的所有解析结果
 */
bool XCanDbcFileParser_parseData(XCanDbcFileParser* parser, const char* data);

/******************************************************************************************
 * 结果查询（对齐 QCanDbcFileParser）
 ******************************************************************************************/

/**
 * @brief 获取解析后的消息描述列表
 * @param parser 解析器指针（不可为 NULL）
 * @return 消息描述列表（XVector<XCanMessageDescription>*）的深拷贝，调用者负责释放
 * @note 仅当解析成功时返回有效数据
 */
XVector* XCanDbcFileParser_messageDescriptions(const XCanDbcFileParser* parser);

/**
 * @brief 获取消息值描述映射表
 * @param parser 解析器指针（不可为 NULL）
 * @return 消息值描述映射表的深拷贝，调用者负责释放
 * @details 包含 VAL_ 条目中定义的信号原始值与文本描述的映射关系。
 *          结构为：MessageUniqueId -> SignalName -> RawValue -> Description
 */
XCanDbcFileParser_MessageValueDescriptions* XCanDbcFileParser_messageValueDescriptions(const XCanDbcFileParser* parser);

/******************************************************************************************
 * 错误/警告查询（对齐 QCanDbcFileParser）
 ******************************************************************************************/

/**
 * @brief 获取当前错误码
 * @param parser 解析器指针（不可为 NULL）
 * @return 错误码
 */
XCanDbcFileParser_Error XCanDbcFileParser_error(const XCanDbcFileParser* parser);

/**
 * @brief 获取错误描述字符串
 * @param parser 解析器指针（不可为 NULL）
 * @return 错误描述字符串的深拷贝，调用者负责释放
 */
XString* XCanDbcFileParser_errorString(const XCanDbcFileParser* parser);

/**
 * @brief 获取警告列表
 * @param parser 解析器指针（不可为 NULL）
 * @return 警告列表（XStringList*）的深拷贝，调用者负责释放
 */
XStringList* XCanDbcFileParser_warnings(const XCanDbcFileParser* parser);

/******************************************************************************************
 * 静态工具方法
 ******************************************************************************************/

/**
 * @brief 获取 DBC 格式的唯一 ID 描述
 * @param out 输出参数，唯一 ID 描述（需已初始化）
 * @details DBC 格式使用帧 ID 作为唯一标识符，起始位 0，长度 29 位，大端字节序。
 *          对应 Qt6 QCanDbcFileParser::uniqueIdDescription()
 */
void XCanDbcFileParser_uniqueIdDescription(XCanUniqueIdDescription* out);

#ifdef __cplusplus
}
#endif

#endif // XCANDBCFILEPARSER_H
