/**
 * @file       XModbusReply_Protected.h
 * @brief      XModbusReply 受保护接口（结果 move/ref 变体与状态机驱动）。
 * @details    仅供子类和内部实现使用，不对外公开；公开 Qt 对齐接口位于
 *             XModbusReply.h。
 */
#ifndef XMODBUSREPLY_PROTECTED_H
#define XMODBUSREPLY_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

// 额外所有权语义和状态控制接口，公开 Qt 对齐接口位于 XModbusReply.h。

/**
 * @brief 设置结构化结果（移动语义：unit 的资源转入回复）。
 * @param reply XModbusReply实例指针；NULL 时不执行任何操作。
 * @param unit 源数据单元（函数内经 XClassMove/create_move 抽取其资源，
 *        返回后 unit 变为空壳——内存仍由调用方按原分配方式释放，
 *        此后不得再解引用其内容）。
 * @return 无。
 * @note 已有结果时原地 XClassMove 覆盖，无结果时以 create_move 接管；
 *       接管后的对象由回复析构释放。
 */
void XModbusReply_setResult_move(XModbusReply* reply, const XModbusDataUnit* unit);

/**
 * @brief 设置结构化结果（指针接管：unit 的所有权移交给回复）。
 * @param reply XModbusReply实例指针；NULL 时不执行任何操作。
 * @param unit 数据单元指针（移交：成功登记后由回复析构释放，调用方
 *        不得再释放或访问；传 NULL 仅清空既有结果）。
 * @return 无。
 * @warning unit 必须是可由 XClassDelete 释放的堆对象；登记后调用方再
 *          释放即双重释放。
 * @note 已有结果先释放再登记；unit 为 NULL 时仅清空结果。
 */
void XModbusReply_setResult_ref(XModbusReply* reply, const XModbusDataUnit* unit);

/**
 * @brief 设置原始PDU结果（移动语义：response 的资源转入回复）。
 * @param reply XModbusReply实例指针；NULL 时不执行任何操作。
 * @param response 源响应 PDU（函数内经 XClassMove/create_move 抽取其
 *        资源，返回后 response 变为空壳——内存仍由调用方按原分配方式
 *        释放，此后不得再解引用其内容）。
 * @return 无。
 * @note 已有结果时原地 XClassMove 覆盖，无结果时以 create_move 接管；
 *       接管后的对象由回复析构释放。
 */
void XModbusReply_setRawResult_move(XModbusReply* reply, const XModbusResponse* response);

/**
 * @brief 设置原始PDU结果（指针接管：response 的所有权移交给回复）。
 * @param reply XModbusReply实例指针；NULL 时不执行任何操作。
 * @param response 响应 PDU 指针（移交：成功登记后由回复析构释放，
 *        调用方不得再释放或访问；传 NULL 仅清空既有结果）。
 * @return 无。
 * @warning response 必须是可由 XClassDelete 释放的堆对象；登记后调用
 *          方再释放即双重释放。
 * @note 已有结果先释放再登记；response 为 NULL 时仅清空结果。
 */
void XModbusReply_setRawResult_ref(XModbusReply* reply, const XModbusResponse* response);

/**
 * @brief 迁移回复状态并联动完成标志与信号（状态机驱动入口）。
 * @param reply XModbusReply实例指针；NULL 或状态无变化时不执行任何操作。
 * @param state 新状态（XModbusReply_State）。
 * @return 无。
 * @note 状态实际变化即发 stateChanged；进入 Finished/Timeout 置
 *       m_finished=true，进入其余状态（No_Started 除外）置 false；
 *       finished 信号沿 m_finished false→true 上升沿发出一次——
 *       正常完成、超时、出错三种终结都经此收敛。
 */
void XModbusReply_setState(XModbusReply* reply, XModbusReply_State state);

/**
 * @brief 清除所有中间错误（保留容器容量）。
 * @param reply XModbusReply实例指针；NULL 或内部列表未创建时
 *        不执行任何操作。
 * @return 无。
 * @note 仅清元素不发信号；已发出的 intermediateErrors 深拷贝不受影响。
 */
void XModbusReply_clearIntermediateError(XModbusReply* reply);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSREPLY_PROTECTED_H

