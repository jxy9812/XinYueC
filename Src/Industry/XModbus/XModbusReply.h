/**
 * @file       XModbusReply.h
 * @brief      Modbus 异步回复对象：一次请求的完整结果与生命周期
 *             （严格对齐 Qt6 QModbusReply）。
 * @details    [XProtocol/XModbus] 表示一个异步 Modbus 请求从发出到终结的
 *             全部状态。状态机：No_Started→Requesting→Waiting→Responding
 *             →Finished，另含超时终结态 Timeout；finished 信号在正常完成、
 *             超时、出错三种终结路径都会发（仅 false→true 上升沿一次）。
 *             setError(真实错误)=errorOccurred+finished 一次性终结；
 *             setError(NoError) 仅为内部重试复位路径，不发信号。
 *             结果访问两套口径：result/rawResult/request 返回深拷贝
 *             （调用方释放），*_const 返回内部借用（禁止释放）。
 *             ReplyType 区分 Raw（原始 PDU）/Common（结构化数据）/
 *             Broadcast（广播无回复）。
 *             线程模型：与库一致的单线程事件循环，零锁。
 */
#ifndef XMODBUSREPLY_H
#define XMODBUSREPLY_H

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XModbusDataUnit.h" // For XModbusDataUnit
#include "XModbusPdu.h"      // For XModbusResponse (which is a typedef of XModbusPdu)
#include "XModbusDevice.h"   // For XModbusDevice_Error and XModbusDevice_IntermediateError
#include "XVector.h"           // For intermediate errors list

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Modbus回复状态枚举
 * @details 定义Modbus请求的完整生命周期状态；进入 Finished/Timeout 即
 *          终结（m_finished 置位、finished 信号随上升沿发出）。
 */
typedef enum {
    XModbusReply_State_No_Started,   ///< 未开始（初始态：请求尚未发出）
    XModbusReply_State_Requesting,   ///< 请求中（请求已发出/排队）
    XModbusReply_State_Waiting,      ///< 等待中（已发送，等待从站响应）
    XModbusReply_State_Responding,   ///< 响应中（正在接收/解析响应）
    XModbusReply_State_Finished,     ///< 已结束（正常完成——终结态）
    XModbusReply_State_Timeout       ///< 已超时（超时等异常终结——终结态，正常流程之外）
} XModbusReply_State;

/**
 * @brief 回复类型枚举（对齐 QModbusReply::ReplyType）
 */
typedef enum {
    XModbusReply_Raw = 0,       ///< 原始 PDU 回复 (XModbusResponse)
    XModbusReply_Common,        ///< 结构化数据回复 (XModbusDataUnit)
    XModbusReply_Broadcast      ///< 广播请求（无回复）
} XModbusReply_ReplyType;

/**
* @struct XModbusReply
* @brief Modbus回复核心结构体（继承自XObject）
* @details 封装了Modbus请求的最终结果，包括类型、从站地址、完成状态、
* 错误信息、结构化结果、原始PDU结果以及中间错误列表。
*/
XCLASS_DEFINE_BEGING(XModbusReply)
XCLASS_DEFINE_EXTEND_END(XModbusReply, XObject)

typedef struct XModbusReply 
{
    XObject m_class;                    /**< 基类（第一个成员，由 XClass 管理，禁止手工修改）。 */
    uint8_t    m_state;                 /**< 回复状态（XModbusReply_State；经 setState 迁移并联动 m_finished）。 */
    uint8_t/*XModbusReply_ReplyType*/ m_type;     /**< 回复类型（XModbusReply_ReplyType，创建时定死）。 */
    uint8_t m_serverAddress;               /**< 从站地址（0-255；创建/初始化传入值按 uint8_t 截断，Modbus 合法从站 1-247）。 */
    uint8_t /*XModbusDevice_Error*/ m_error;       /**< 最终错误码（XModbusDevice_Error；NoError=无错误）。 */
    bool m_finished;                    /**< 是否已终结（正常完成/超时/出错三种终结都置位；finished 信号随其 false→true 上升沿发出）。 */
    XString* m_errorString;            /**< 错误描述文本（对象拥有，析构释放；NULL=未设置，errorString() 返回空串）。 */

    XModbusDataUnit* m_result;         /**< 结构化结果（对象拥有，析构释放；仅 Common 类型使用；NULL=无结果）。 */
    XModbusResponse* m_rawResult;      /**< 原始 PDU 结果（对象拥有，析构释放；Raw 类型使用；NULL=无结果）。 */
    XModbusRequest* m_request;          /**< 原始请求（对象拥有，析构释放；由 Client 的 createReply_* 登记副本或接管指针；NULL=无请求）。 */
    XVector* m_intermediateErrors;       /**< 中间错误列表（对象拥有，析构释放；元素为 XModbusDevice_IntermediateError 值拷贝；NULL=尚未记录过）。 */
} XModbusReply;

/******************************************************************************************
* 类初始化/实例创建接口
******************************************************************************************/

/**
 * @brief 初始化XModbusReply的虚函数表
 * @return 类虚函数表指针（单例，不可释放）
 * @note 本类无新增虚槽：仅继承 XObject 并重载 deinit（析构释放
 *       errorString/result/rawResult/request/intermediateErrors）。
 *       虚表在首次调用时初始化并缓存；单线程事件循环模型下无锁，
 *       须在事件循环启动前（或同一线程内）调用，通常由 create 自动完成。
 */
XVtable* XModbusReply_class_init(void);

/**
* @brief 堆上创建XModbusReply实例。
* @param memory 内存分配器类型（XMemoryType；分配与所有权标记按同一类型登记）。
* @param type 回复类型（XModbusReply_ReplyType，创建后不变）。
* @param serverAddress 从站地址（0-255，超出按 uint8_t 截断）。
* @return 新回复对象（调用方 XModbusReply_deleteLater 或 XClassDelete
*         释放）；内存分配失败返回 NULL。
* @note 初始状态 No_Started、无错误、结果/请求指针均为 NULL；
*       结果与请求对象经 setter 登记后由本对象析构释放。
*/
XModbusReply* XModbusReply_create_ex(XMemoryType memory,  XModbusReply_ReplyType type, int serverAddress);

/**
* @brief 栈上/已分配内存初始化XModbusReply实例。
* @param reply 目标对象；NULL 时不执行任何操作，可为未初始化内存
*        （基类与派生字段在内部完成初始化）。
* @param type 回复类型（XModbusReply_ReplyType）。
* @param serverAddress 从站地址（0-255，超出按 uint8_t 截断）。
* @return 无。
* @note 默认状态 No_Started、无错误、全部结果指针为 NULL；与
*       XClassDeinit 成对出现（析构释放全部持有对象），堆对象统一走
*       deleteLater 路径。
*/
void XModbusReply_init(XModbusReply* reply, XModbusReply_ReplyType type, int serverAddress);

/******************************************************************************************
 * Public API (严格对齐 QModbusReply)
 ******************************************************************************************/

 // --- 属性查询 ---

/**
 * @brief 获取回复类型。
 * @param reply XModbusReply实例指针。
 * @return 回复类型（XModbusReply_ReplyType）；reply 为 NULL 返回
 *         XModbusReply_Raw。
 */
XModbusReply_ReplyType XModbusReply_type(const XModbusReply* reply);

/**
 * @brief 获取当前回复状态。
 * @param reply XModbusReply实例指针。
 * @return 当前状态（XModbusReply_State）；reply 为 NULL 返回
 *         XModbusReply_State_No_Started。
 */
XModbusReply_State XModbusReply_state(const XModbusReply* reply);

/**
 * @brief 获取从站地址。
 * @param reply XModbusReply实例指针。
 * @return 从站地址（0-255，创建时超出 uint8_t 的传入值已被截断；
 *         Modbus 合法从站为 1-247）；reply 为 NULL 返回 -1。
 */
int XModbusReply_serverAddress(const XModbusReply* reply);

/**
 * @brief 判断回复是否已终结。
 * @param reply XModbusReply实例指针。
 * @return 已终结（正常完成/超时/出错三种路径都置位）返回 true；
 *         否则或 reply 为 NULL 返回 false。
 * @note 与 finished 信号同源：信号随本标志 false→true 上升沿发出。
 */
bool XModbusReply_isFinished(const XModbusReply* reply);

// --- 结果获取 ---

/**
 * @brief 获取结构化结果（深拷贝）。
 * @param reply XModbusReply实例指针。
 * @return 结果的新副本（调用方 XClassDelete 释放）；reply 为 NULL 或
 *         无结果返回 NULL。
 * @note 副本与回复内部结果互不影响；只读访问用 result_const（零拷贝）。
 */
XModbusDataUnit* XModbusReply_result(const XModbusReply* reply);

/**
 * @brief 获取结构化结果的只读借用引用（零拷贝）。
 * @param reply XModbusReply实例指针。
 * @return 结果的只读借用指针（回复内部存储，禁止释放或修改），仅在
 *         回复存续且结果未被覆盖/清空前有效；reply 为 NULL 或无结果
 *         返回 NULL。
 */
const XModbusDataUnit* XModbusReply_result_const(const XModbusReply* reply);

/**
 * @brief 获取原始PDU结果（深拷贝）。
 * @param reply XModbusReply实例指针。
 * @return 原始PDU结果的新副本（调用方 XClassDelete 释放）；reply 为
 *         NULL 或无结果返回 NULL。
 * @note 副本与回复内部结果互不影响；只读访问用 rawResult_const。
 */
XModbusResponse* XModbusReply_rawResult(const XModbusReply* reply);

/**
 * @brief 获取原始PDU结果的只读借用引用（零拷贝）。
 * @param reply XModbusReply实例指针。
 * @return 原始PDU结果的只读借用指针（回复内部存储，禁止释放或修改），
 *         仅在回复存续且结果未被覆盖/清空前有效；reply 为 NULL 或无
 *         结果返回 NULL。
 */
const XModbusResponse* XModbusReply_rawResult_const(const XModbusReply* reply);

/**
 * @brief 获取原始请求（深拷贝）。
 * @param reply XModbusReply实例指针。
 * @return 请求的新副本（调用方 XClassDelete 释放）；reply 为 NULL 或
 *         无请求返回 NULL。
 * @note 副本与回复内部请求互不影响；只读访问用 request_const。
 */
XModbusRequest* XModbusReply_request(const XModbusReply* reply);

/**
 * @brief 获取原始请求的只读借用引用（零拷贝）。
 * @param reply XModbusReply实例指针。
 * @return 请求的只读借用指针（回复内部存储，禁止释放或修改），仅在
 *         回复存续期间有效；reply 为 NULL 或无请求返回 NULL。
 */
const XModbusRequest* XModbusReply_request_const(const XModbusReply* reply);

// --- 错误信息 ---

/**
 * @brief 获取错误描述字符串（深拷贝副本）。
 * @param reply XModbusReply实例指针。
 * @return 新 XString（调用方 XClassDelete 释放）；reply 为 NULL 返回
 *         NULL；未设置错误描述返回空字符串（非 NULL）。
 * @note 本函数不改动回复内部状态；内部 m_errorString 生命周期归回复。
 */
XString* XModbusReply_errorString(const XModbusReply* reply);

/**
 * @brief 获取错误码。
 * @param reply XModbusReply实例指针。
 * @return 错误码（XModbusDevice_Error）；reply 为 NULL 返回
 *         XModbusDevice_UnknownError。
 */
XModbusDevice_Error XModbusReply_error(const XModbusReply* reply);

// --- 公开结果/状态设置接口（对齐 Qt QModbusReply） ---

/**
 * @brief 设置结构化结果（深拷贝）。
 * @param reply XModbusReply实例指针；NULL 时不执行任何操作。
 * @param unit 源数据单元（借用：函数内深拷贝，不取得所有权）；NULL 时
 *        不执行任何操作，既有结果保持不变。
 * @return 无。副本分配失败时既有结果保持不变。
 * @note 已有结果时经 XClassCopy 原地覆盖，无结果时创建副本；副本由
 *       回复拥有、析构释放。move/ref 变体见 XModbusReply_Protected.h。
 *       对齐 Qt 6.8.3 QModbusReply::setResult。
 */
void XModbusReply_setResult(XModbusReply* reply, const XModbusDataUnit* unit);

/**
 * @brief 设置原始响应 PDU（深拷贝）。
 * @param reply XModbusReply实例指针；NULL 时不执行任何操作。
 * @param response 源响应 PDU（借用：函数内深拷贝，不取得所有权）；
 *        NULL 时不执行任何操作，既有结果保持不变。
 * @return 无。副本分配失败时既有结果保持不变。
 * @note 已有结果时经 XClassCopy 原地覆盖，无结果时创建副本；副本由
 *       回复拥有、析构释放。move/ref 变体见 XModbusReply_Protected.h。
 *       对齐 Qt 6.8.3 QModbusReply::setRawResult。
 */
void XModbusReply_setRawResult(XModbusReply* reply, const XModbusResponse* response);

/**
 * @brief 设置完成标志；false→true 上升沿发出 finished 信号。
 * @param reply XModbusReply实例指针；NULL 或标志无变化时不执行任何操作。
 * @param isFinished true=标记终结并发出 finished；false=回退未完成
 *        （不发信号）。
 * @return 无。
 * @note 只翻完成标志，不改变 XModbusReply_State。对齐 Qt 6.8.3。
 */
void XModbusReply_setFinished(XModbusReply* reply, bool isFinished);

/**
 * @brief 设置错误及描述并终结回复（errorOccurred + finished 一次发齐）。
 * @param reply XModbusReply实例指针；NULL 时不执行任何操作。
 * @param error 错误码（XModbusDevice_Error）；XModbusDevice_NoError 走
 *        复位路径（见 @note），其余值终结回复。
 * @param errorText 错误描述（XString 借用：函数内深拷贝存储，返回后
 *        调用方可立即释放/修改；NULL 时已有描述被清空、无描述保持为空）。
 * @return 无。
 * @note 真实错误（非 NoError）：更新错误码与描述 → 发 errorOccurred →
 *       置 finished 并发 finished——即使状态机尚未走到 Finished 也以
 *       终结收尾。NoError：仅清空描述、不发任何信号、不改完成标志
 *       （内部重试路径借此复位回复；Qt 调用方只对真实错误用本入口）。
 *       对齐 Qt 6.8.3 QModbusReply::setError（QString 主版本）。
 */
void XModbusReply_setError(XModbusReply* reply, XModbusDevice_Error error, const XString* errorText);
/**
 * @brief setError 的 UTF-8 const char* 兼容重载（行为与主版本一致）。
 * @param errorText 错误描述（UTF-8 文本，借用：函数内存副本）；NULL 时
 *        已有描述被清空、无描述保持为空。
 * @return 无。其余语义同 XModbusReply_setError 主版本。
 */
void XModbusReply_setError_2(XModbusReply* reply, XModbusDevice_Error error, const char* errorText);

// --- 中间错误 ---

/**
 * @brief 获取中间错误列表（深拷贝）。
 * @param reply XModbusReply实例指针。
 * @return 新列表（调用方 XClassDelete 释放，元素为
 *         XModbusDevice_IntermediateError 值拷贝）；reply 为 NULL 或
 *         从未记录过中间错误（内部列表未创建）返回 NULL。
 * @note 只读借用与条目级操作见 Protected 接口。
 */
XVector* XModbusReply_intermediateErrors(const XModbusReply* reply);

/**
 * @brief 追加一条中间错误并发出 intermediateErrorOccurred 信号。
 * @param reply XModbusReply实例指针；NULL 时不执行任何操作。
 * @param error 中间错误码（值拷贝入列表）。
 * @return 无。
 * @note 内部列表按需创建；本函数不影响 m_state/m_finished。
 *       对齐 Qt 6.8.3 QModbusReply::addIntermediateError。
 */
void XModbusReply_addIntermediateError(XModbusReply* reply, XModbusDevice_IntermediateError error);

/******************************************************************************************
* 信号接口 (严格对齐 Qt 信号)
******************************************************************************************/

/**
 * @brief 触发 finished 信号。
 * @param reply XModbusReply实例指针；NULL 时不发射。
 * @return 信号标识句柄（信号函数地址，仅供框架内部使用）；调用方
 *         不需使用亦不可释放。
 * @note 正常完成/超时/出错三种终结都收敛到本信号（上升沿一次）；
 *       由 setState/setFinished/setError 内部自动发射。
 */
void* XModbusReply_finished_signal(XModbusReply* reply);

/**
 * @brief 触发 stateChanged 信号。
 * @param reply XModbusReply实例指针；NULL 时不发射。
 * @param state 新状态（随信号参数传递给已连接槽）。
 * @return 信号标识句柄（信号函数地址，仅供框架内部使用）；调用方
 *         不需使用亦不可释放。
 * @note 由 setState 在状态实际变化时自动发射。
 */
void* XModbusReply_stateChanged_signal(XModbusReply* reply, XModbusReply_State state);

/**
 * @brief 触发 errorOccurred 信号。
 * @param reply XModbusReply实例指针；NULL 时不发射。
 * @param error 错误码（随信号参数传递给已连接槽）。
 * @return 信号标识句柄（信号函数地址，仅供框架内部使用）；调用方
 *         不需使用亦不可释放。
 * @note 由 setError（真实错误路径）自动发射。
 */
void* XModbusReply_errorOccurred_signal(XModbusReply* reply, XModbusDevice_Error error);

/**
 * @brief 触发 intermediateErrorOccurred 信号。
 * @param reply XModbusReply实例指针；NULL 时不发射。
 * @param error 中间错误码（随信号参数传递给已连接槽）。
 * @return 信号标识句柄（信号函数地址，仅供框架内部使用）；调用方
 *         不需使用亦不可释放。
 * @note 由 addIntermediateError 自动发射。
 */
void* XModbusReply_intermediateErrorOccurred_signal(XModbusReply* reply, XModbusDevice_IntermediateError error);

#define XModbusReply_deleteLater		XObject_deleteLater
#define XModbusReply_deinitLater		XObject_deinitLater
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusReply_create
#define XModbusReply_create(...) XModbusReply_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XMODBUSREPLY_H
