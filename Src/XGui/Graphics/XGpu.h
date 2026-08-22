/****************************************************************************
 * @file       XGpu.h
 * @brief      统一 GPU 驱动入口。
 * @details    XGpu 把 OpenGL 与 Vulkan 归并为一个平台无关的运行时对象。
 *             上层通过统一的后端选择、适配器信息、绑定与呈现 API 工作；
 *             GLX/WGL/Vulkan 的具体类型仅存在于 Drive 的驱动适配层。
 ****************************************************************************/
#ifndef XGPU_H
#define XGPU_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XGuiConfig.h"
#include "XMemory.h"

#if XWINDOW_ON
typedef struct XWindow XWindow;
#endif

#if XPLATFORMINTEGRATION_ON

/** @brief GPU 后端；Auto 由运行时按表面与可用驱动选择。 */
typedef enum XGpuBackend
{
    XGpuBackend_None = 0,
    XGpuBackend_Auto,
    XGpuBackend_OpenGL,
    XGpuBackend_Vulkan
} XGpuBackend;

/** @brief 平台无关的适配器能力描述。 */
typedef struct XGpuAdapterInfo
{
    XGpuBackend m_backend;            /**< 被选择的驱动后端。 */
    uint32_t m_apiVersion;            /**< 驱动 API 版本编码；未知为 0。 */
    uint32_t m_adapterCount;          /**< 可见物理适配器数，至少为 1。 */
    bool m_supportsPresentation;      /**< 可向关联窗口呈现。 */
    bool m_supportsExplicitCommands;  /**< 显式命令/同步模型（Vulkan）。 */
} XGpuAdapterInfo;

typedef struct XGpu XGpu;

/**
 * @brief      创建统一 GPU 运行时。
 * @details    Auto 在带窗口表面时优先选可呈现的 OpenGL；无窗口时优先
 *             Vulkan。指定后端不可用时不隐式切换，直接返回 NULL。
 */
XGpu* XGpu_create_ex(XMemoryType memory, XGpuBackend preferredBackend,
                     XWindow* surfaceWindow);
#define XGpu_create(preferredBackend, surfaceWindow) \
    XGpu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (preferredBackend), (surfaceWindow))
void XGpu_destroy(XGpu* self);
bool XGpu_isValid(const XGpu* self);
XGpuBackend XGpu_backend(const XGpu* self);
XGpuAdapterInfo XGpu_adapterInfo(const XGpu* self);
bool XGpu_makeCurrent(XGpu* self);
void XGpu_doneCurrent(XGpu* self);
bool XGpu_present(XGpu* self);
void* XGpu_getProcAddress(const XGpu* self, const char* name);

#endif /* XPLATFORMINTEGRATION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XGPU_H */
