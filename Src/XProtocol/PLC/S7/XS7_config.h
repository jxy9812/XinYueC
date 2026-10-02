/** @file XS7_config.h
 * @brief 西门子 S7 协议子功能配置文件
 *
 * 通过此配置文件可以裁剪 S7 协议内部的各个子功能：
 *   1. XS7_ON          - S7 子协议总开关（默认随 XPLC_CORE_ON）
 *   2. XS7_CORE_ON     - 核心（XS7Tpkt / XS7Cotp / XS7Pdu / XS7Address /
 *                        XS7Value / XS7Session / XS7TcpClient + 读写 API）
 *   3. XS7_CONTROL_ON  - 运维：PLC Run/Stop（XS7Control 与对应 API/菜单项）
 *   4. XS7_BLOCK_ON    - 运维：块上传/下载（XS7Block 与对应 API/菜单项）
 *
 * 由 XPlc_config.h 末尾引入，不直接登记到 XProtocol_config.h。
 * 注意：S7 各头/源依赖 XPlc 伞层类型，须保持 XS7_ON => XPLC_CORE_ON=1。
 */

#ifndef XS7_CONFIG_H
#define XS7_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 宏定义为惰性展开：即使本文件被先行包含，使用点前 XPLC_CORE_ON 已定义即可生效 */
#ifndef XS7_ON
#define XS7_ON XPLC_CORE_ON
#endif

#if XS7_ON

/* ========================================================================== */
/*                        子功能开关                                          */
/* ========================================================================== */

/** @brief S7 核心（报文编解码、地址与值、会话、TcpClient、读写 API） */
#ifndef XS7_CORE_ON
#define XS7_CORE_ON 1
#endif

/** @brief 运维：PLC Run/Stop（XS7Control 与对应 API/菜单项同步裁剪） */
#ifndef XS7_CONTROL_ON
#define XS7_CONTROL_ON 1
#endif

/** @brief 运维：块上传/下载（XS7Block 与对应 API/菜单项同步裁剪） */
#ifndef XS7_BLOCK_ON
#define XS7_BLOCK_ON 1
#endif

#endif /* XS7_ON */

#ifdef __cplusplus
}
#endif

#endif /* XS7_CONFIG_H */
