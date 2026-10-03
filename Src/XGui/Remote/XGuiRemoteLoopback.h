/**
 * @file       XGuiRemoteLoopback.h
 * @brief      XGuiRemoteLoopbackDevice 内存回环设备契约(冻结头, 只声明 API)。
 * @details    成对创建的内存 XIODevice: A 写入的数据从 B 读出, B 写入的
 *             数据从 A 读出。用途:
 *               - 单进程回归测试: 服务器/客户端会话零网络端到端互通;
 *               - 嵌入式同进程/板内管道互联。
 *             语义(冻结):
 *               - 顺序设备(Sequential), 读写双开;
 *               - 每端固定容量环形缓冲(创建时指定), **全有全无写**: 对端
 *                 接收环剩余空间不足以容纳整块拟写字节时 write 返回 0
 *                 (一个字节都不写), 保证 writeFrame 整帧重试语义不乱流;
 *                 不阻塞、不丢数据、不动态扩容;
 *               - 生命周期: 两端对象的创建/销毁相互独立。共享对偶块
 *                 (两个环形缓冲 + 互斥锁 + 双端存活标志)由对偶引用计数
 *                 持有: 任一端 delete 时经共享块原子清除本端存活标志并
 *                 释放引用, 最后删除的一方释放共享块——对端对象不悬垂;
 *                 对端此后 peerAlive()==false、读到 EOF(bytesAvailable==0
 *                 且设备关闭语义)、写恒返回 0。属主线程约定不变: 每端
 *                 的读写/delete 只在本端属主线程执行(XGuiRemote 会话中
 *                 = GUI 线程), 跨端并发仅经由共享块内的锁与原子标志。
 *             虚表扩展照 XFileDevice 模式(XFileDevice.h:85-92, 宏与枚举
 *             同头声明): 覆写 ReadData/WriteData/IsSequential/
 *             BytesAvailable/Close 等槽。
 * @note       模块开关 XGUI_REMOTE_ON 见 XGuiRemoteProto.h。
 * @author     XinYueC 团队
 */
#ifndef XGUIREMOTELOOPBACK_H
#define XGUIREMOTELOOPBACK_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XIODevice.h"
#include "XGuiRemoteProto.h"

#if XGUI_REMOTE_ON

/** @brief 回环设备虚表枚举(继承 XIODevice, 无新增槽位——仅覆写既有槽;
 *         宏与枚举同头声明, 先例 XFileDevice.h:85-92)。 */
XCLASS_DEFINE_BEGING(XGuiRemoteLoopbackDevice)
XCLASS_DEFINE_EXTEND_END(XGuiRemoteLoopbackDevice, XIODevice)

#define XGUIREMOTELOOPBACK_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XGuiRemoteLoopbackDevice))

/**
 * @brief      XGuiRemoteLoopbackDevice 设备对象; m_parent 必须是第一个成员。
 * @details    共享对偶实现块(环形缓冲/互斥锁/双端存活标志, 引用计数)
 *             封装在 m_d(实现文件内定义), 冻结契约不暴露字段; 任意一端
 *             delete 安全, 见文件头生命周期语义。
 */
typedef struct XGuiRemoteLoopbackDevice {
    XIODevice m_parent;   /**< 基类 XIODevice; 必须是第一个。 */
    void*     m_d;        /**< 共享对偶实现块引用(引用计数持有)。 */
} XGuiRemoteLoopbackDevice;

/* ==================== 生命周期 ==================== */

/** @brief 初始化类虚函数表, 返回共享 XVtable 指针。 */
XVtable* XGuiRemoteLoopbackDevice_class_init(void);

/**
 * @brief      成对创建互联设备(对标构造)。
 * @param      memory       内存类型(XMemoryType)。
 * @param      ringCapacity 每端环形缓冲容量(字节; 建议 ≥ 64KiB;
 *                          过小时上层写背压频繁, 语义仍正确)。
 * @param      peerOut      输出对端设备指针; 可为 NULL(只要单端, 单端
 *                          写入永远背压、读恒 EOF)。
 * @return     本端设备(A); 失败返回 NULL(*peerOut 置 NULL)。
 * @note       两端各自独立 delete; 一端释放后另一端进入对端关闭态。
 */
XGuiRemoteLoopbackDevice* XGuiRemoteLoopbackDevice_createPair_ex(
        XMemoryType memory, size_t ringCapacity,
        XGuiRemoteLoopbackDevice** peerOut);

/** @brief 默认内存类型创建便捷宏(仓库惯例)。 */
#define XGuiRemoteLoopbackDevice_createPair(ringCapacity, peerOut) \
    XGuiRemoteLoopbackDevice_createPair_ex(XCLASS_DEFAULT_MEMORY_TYPE, \
                                           (ringCapacity), (peerOut))

/**
 * @brief  初始化既有设备对象并挂接对端(高级用法: 栈上对象/自管内存)。
 * @param  self        尚未初始化的设备(内部先 XIODevice_init)。
 * @param  peer        对端设备(借用; 可为 NULL)。
 * @param  ringCapacity 本端环形缓冲容量。
 */
void XGuiRemoteLoopbackDevice_init(XGuiRemoteLoopbackDevice* self,
                                   XGuiRemoteLoopbackDevice* peer,
                                   size_t ringCapacity);

/** @brief 析构/反初始化映射(仓库惯例; 断开对端关联并释放环形缓冲)。 */
#define XGuiRemoteLoopbackDevice_deinit_base(self) \
    XIODevice_deinit_base((XIODevice*)(self))
#define XGuiRemoteLoopbackDevice_delete_base(self) \
    XClass_delete_base((XClass*)(self))
#define XGuiRemoteLoopbackDevice_deleteLater       XObject_deleteLater

/* ==================== 访问与语义 ==================== */

/**
 * @brief  取本端 XIODevice 视图(首成员, 供会话层以传输抽象持有)。
 * @note   亦可直接 (XIODevice*) 强转(仓库 XAbstractSocket 同款惯例)。
 */
#define XGuiRemoteLoopbackDevice_asIODevice(dev) ((XIODevice*)(dev))

/**
 * @brief  本端视角对端是否存活(冻结口径: 经共享对偶块原子存活标志判定,
 *         对端未 shutdown 且未 delete 时为 true; 本端不持有对端对象指针,
 *         任意时刻调用均无悬垂)。
 */
bool XGuiRemoteLoopbackDevice_peerAlive(const XGuiRemoteLoopbackDevice* self);

/** @brief 当前环形缓冲占用字节数(等价 XIODevice_bytesAvailable 视图)。 */
size_t XGuiRemoteLoopbackDevice_bufferedBytes(
        const XGuiRemoteLoopbackDevice* self);

/** @brief 关闭本端: 清空本端缓冲并置对端 EOF; 幂等。 */
void XGuiRemoteLoopbackDevice_shutdown(XGuiRemoteLoopbackDevice* self);

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUIREMOTELOOPBACK_H */
