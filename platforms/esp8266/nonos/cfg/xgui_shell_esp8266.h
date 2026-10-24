/**
 * @file xgui_shell_esp8266.h
 * @brief ESP8266 NONOS3「shell + XGui(SSD1306) + lwip + FreeRTOS V9」合体固件裁剪档案。
 * @details 合并三方已验证档案（build/esp8266_console/esp8266_console_minimal.h、
 *          build/arduino_gui/xgui_esp8266_ssd1306.h、build/arduino_final/lwipopts.h）：
 *   - 底座 = xgui_esp8266_ssd1306.h（XGui/字体/backingstore 全开，PARTIAL 128x8
 *     RGB565 tile），再把 shell 一族开关打开（gui 档案把它们全置 0，故本档案
 *     在 include 之后 #undef 重定义）；
 *   - configTOTAL_HEAP_SIZE：本轮起堆走仓库 V9 heap_4 单一分配器（configTOTAL_
 *     HEAP_SIZE 是 heap_4 的静态 ucHeap[] 大小）。本板 DRAM .data+.bss 之后到
 *     0x3FFFFC00 只有 ~55KB，故取 24KB（GUI 档案 16KB + shell 档案 32KB 之间）。
 *   - configCPU_CLOCK_HZ：仓库 FreeRTOSConfig.h 写的是 (SystemCoreClock)，NONOS3
 *     下没有该符号的可靠声明；移植层用它算 TICK_CYCLES，这里钉死 80MHz。
 *   - XFREERTOS_V7_COMPAT=0（两份底座档案一致，V9 原生提供 pdMS_TO_TICKS/TLS）。
 * 由 build_fw_final.sh 以 -include 强制包含，必须早于 CXinYueConfig.h 与任何
 * 模块 Config.h。所有开关在各模块配置头里都是 #ifndef 保护，故可先行定义。
 */
#ifndef XGUI_SHELL_ESP8266_H
#define XGUI_SHELL_ESP8266_H

#include <stddef.h>

/* ==================================================================== */
/* ★ V9 总闸（本轮引入，默认 0 = 关）                                   */
/*                                                                       */
/* XESP8266_FREERTOS_V9_ON = 0（默认，关）：                             */
/*   - 不编译仓库 FreeRTOS V9 内核（tasks/list/queue + heap_4）与       */
/*     XTENSA_LX106 移植层（port.c / port_isr.c / portasm.S）；          */
/*     Drive/FreeRTOS/** 在 build_fw_final.sh 里按本宏整目录剔除。       */
/*   - 不调用 xTaskStartScheduler：主循环 = os_timer(5ms) → cont_run →   */
/*     XCoreApplication_processEvents（app.c pump_entry）。               */
/*   - 堆分配与 V9 完全解耦：heap_4 不参与，malloc/free 与 NONOS 的     */
/*     pvPort* 胖签名全部转接框架自带的 XMemory 两块池（stubs.c）。       */
/*   - 同步原语后端 = sync_baremetal.c（单线程语义）。                   */
/*   - 链接脚本不注入自研 .MyV2/.MyK/.MyU 向量槽（回到 SDK 原生向量）。  */
/*                                                                       */
/* XESP8266_FREERTOS_V9_ON = 1（开，将来接线时用）：                    */
/*   - build_fw_final.sh 编译并链接 Drive/FreeRTOS/** 与移植层，         */
/*     heap_4.c 重新接管堆，链接脚本注入自研向量槽（V9_VECTORS=1）。     */
/*   ⚠ 该分支本轮【未在真机验证】：打开时需要同时恢复 -D__FreeRTOS__   */
/*     （CXinYueConfig.h:24 靠它判 XPLATFORM_FREERTOS）与 Sync 适配层，  */
/*     届时 heap_4 静态堆会重新吃 DRAM，任务栈 4~8KB/个 也会回来。       */
/*   切换只需改这一行 —— 构建脚本读的就是这里的取值。                   */
/* ==================================================================== */
/* app 的事件循环泵跑在 cont 上下文上（app.c 的 s_pumpCont）。SDK 预编译的
 * cont_init 默认 4096B；本轮收到 2048B，省下的 2KB 直接给 XMemory arena
 * （否则 arena 涨上去就会把 bss 顶推出 SDK 早期 boot 的安全区，见下方
 * XVP_GLOBAL_ARENA_BYTES 处的实测）。
 * ⚠ 必须定义在下面 #include 底座之前：底座 xgui_esp8266_ssd1306.h:626-627
 *   自己也 #define CONT_STACKSIZE 4096（#ifndef 保护），写在它之后就晚了。
 * 高水位由 app.c 的 cont_stack_used() 每拍实测并打印 [cont] stackUsed。 */
#ifndef CONT_STACKSIZE
#define CONT_STACKSIZE 4096
#endif

#ifndef XESP8266_FREERTOS_V9_ON
#define XESP8266_FREERTOS_V9_ON 0
#endif

/* ==================================================================== */
/* ★ 网络总闸（2026-10-22 引入；2026-10-23 框架原生事件循环验收后回开）   */
/*                                                                       */
/* XESP8266_NET_ON = 1（现默认，开）：完整 SoftAP（XinYueAP）+ lwip      */
/*   定时器泵（sys_check_timeouts，5ms）。镜像 bug（fix_rodata_pos       */
/*   长度/CRC 覆写代码段）与 UART 帧格式 bug 修复后，网络初始化的        */
/*   "崩溃"实为取指损坏的假象，已随之消失。                              */
/*                                                                       */
/* XESP8266_NET_ON = 0（诊断用，关）：                                   */
/*   - 不初始化 WiFi（wifi_set_opmode_current 置 NULLMODE）；            */
/*   - 不注册 lwip 定时器泵（sys_check_timeouts）；                       */
/*   - lwip 仍编译链接（SDK libnet80211/libwpa 硬依赖 pbuf_alloc 等      */
/*     符号，无法从镜像剔除）。                                          */
/* ==================================================================== */
#ifndef XESP8266_NET_ON
#define XESP8266_NET_ON 1
#endif

/* ==================================================================== */
/* ★ 线程功能总闸（2026-10-22 引入，默认 0 = 关）                           */
/*                                                                       */
/* 裸机单线程固件里没有调度器、没有真正的并发，框架的 XThread 整套      */
/* （XThread 对象 / XThreadPool / 线程亲和性迁移）用不上，且会引入      */
/* 无人调度的"半线程"语义：XThread_create 出来的线程永远不会跑，      */
/* 它的 XThreadData 却会参与事件投递的线程归属判定 → 事件被投到        */
/* 一个没有事件循环的"线程"上，永远不会被消费（静默丢事件）。           */
/*                                                                       */
/* 因此裸机关掉 XTHREAD_ON。注意 XTHREADDATA_ON 必须保持 1：            */
/* 事件循环/对象系统/互斥锁都依赖线程私有数据与事件分发器这一层基础设施  */
/* （见 XSync_config.h 的说明），它与"能否创建线程"无关。                 */
/* 裸机下 XThreadStorage（TLS）退化为一个全局静态槽（见                  */
/* esp8266/nonos/platform/sync_baremetal.c），单线程语义正确。            */
/* ==================================================================== */
#ifndef XTHREAD_ON
#define XTHREAD_ON 0
#endif
#ifndef XTHREADDATA_ON
#define XTHREADDATA_ON 1
#endif
#ifndef XTHREADPOOL_ON
#define XTHREADPOOL_ON 0
#endif

/* ==================================================================== */
/* ★ Shell 异步输出队列（2026-10-22 打开）                                 */
/*                                                                       */
/* XCONSOLE_SHELL_ASYNC_OUTPUT_ON = 0（框架默认）时，异步模式下命令执行   */
/* 的输出（help 列表、uptime 结果等）写入 Shell 的内部输出缓冲后**没有任何 */
/* 人调用 XConsoleShell_flushOutput 刷新**——xcs_async_read_ready 里的    */
/* flush 调用点整个被 #if 编译掉。症状：敲命令无回显无结果，只有走 XPrintf  */
/* 直出的错误文本（"未知命令"）可见。                                      */
/* = 1：read_ready 每轮 pump 后刷新输出队列，命令结果正常上串口。         */
/* ==================================================================== */
#ifndef XCONSOLE_SHELL_ASYNC_OUTPUT_ON
#define XCONSOLE_SHELL_ASYNC_OUTPUT_ON 1
#endif

/* ==================================================================== */
/* ★ Shell RAM 裁剪（2026-10-23，只裁有 RAM 收益的项）                     */
/*                                                                       */
/* COMPLETION_ON = 0：关 Tab 补全。省 ~1.9KB .bss（g_xcs_pathCandidates  */
/*   48×40 静态候选表）+ ~2KB 补全函数栈峰值（candidates+ptrs 都在栈上）  */
/*   ——后者直接缓解 cont 栈 8192 打满的问题。板载串口调试不需要补全。     */
/* HISTORY_ON = 0：关命令历史。省 8 条×64B 历史行（~512B）与 history      */
/*   命令。板载交互价值低。                                              */
/* ASYNC_OUTPUT_CAPACITY 512→128：命令输出缓冲。板载命令输出都是短行，    */
/*   128B 够用（超长输出分多段刷，read_ready 每轮都 flush）。             */
/* 合计 RAM 收益 ~2.8KB，flash ~1.5KB。                                   */
/* ==================================================================== */
#ifndef XCONSOLE_SHELL_COMPLETION_ON
#define XCONSOLE_SHELL_COMPLETION_ON 0
#endif
#ifndef XCONSOLE_SHELL_HISTORY_ON
#define XCONSOLE_SHELL_HISTORY_ON 0
#endif
#ifndef XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY
#define XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY 128
#endif

/* ==================================================================== */
/* ★ 取证表裁剪（2026-10-23，lwh 耗尽修复的一部分）                        */
/*                                                                       */
/* XPB_TRACE_ON = 0：RX pbuf 环形取证表（32×16B = 512B .bss）。驱动注释   */
/*   自述"定位到根因后随修复一并移除"——P0 泄漏定案已完成，裁。            */
/*   ⚠ XTAKE 陈旧回调守卫是 P0 防护件（挡 SDK 陈旧回调二次释放），        */
/*   【不是】取证件，保留不动。                                           */
/* ==================================================================== */
#ifndef XPB_TRACE_ON
#define XPB_TRACE_ON 0
#endif

/* heap_4 静态堆区大小（字节）。必须在 gui 档案（含 FreeRTOSConfig.h）之前定义，
 * 因为两份底座都用 #ifndef 保护它。
 *
 * ⚠️ 实测上限（真机二分，见 build_v9_final.log / cap_t16v_on.log）：
 * .rodata 留在 DRAM 时，24KB/35KB 两种堆都在 app_entry 之后、SDK 首次
 * flash 读之前挂死（ROM 0x400042B9，cause 0x14），16KB 能完整启动——但
 * 16KB 连 5 个 V9 任务栈（OS 5120+GUI 4608+APP 8192+HB 2048x2）都放不下，
 * shell 链更无从谈起。
 * ⚠️ 总装轮（本轮）解法：.rodata(26KB) 整段搬进 irom0_0_seg（flash，由
 * build_fw_final.sh RODATA_IN_FLASH=1 调 move_rodata_to_flash.py，分区表
 * 已用 .dram.rodata 钉回 DRAM）。搬完后非堆静态 ~35KB，28KB 堆顶
 * (~0x3FFF79A8) 压在已验证安全的 bss 顶界 0x3FFF8808 之下——真机实证
 * （cap_R1c/R1d/R1f，36KB 堆 bss 顶 0x3FFF99D0）SDK 早期 boot 在 bss 顶
 * 越过 ~0x3FFF9xxx 后永久停摆（Mo 之后无输出无 KEX 无复位），与档案里
 * 「24KB/35KB 堆在 SDK 首次 flash 读之前挂死」的 mystery 同源：疑似 blob
 * 在 bss 之上保留固定 DRAM 工作区，阈值在 (0x3FFF8808, 0x3FFFA808) 之间。 */
#ifndef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE ((size_t)(28 * 1024))
#endif

/* 移植层 TICK_CYCLES = configCPU_CLOCK_HZ / configTICK_RATE_HZ。先定义以防
 * FreeRTOSConfig.h 用 #ifndef；include 后再 #undef 一次以防它无条件重定义。 */
#ifndef configCPU_CLOCK_HZ
#define configCPU_CLOCK_HZ ((unsigned long)80000000L)
#endif

/* XConsoleShellMemory.c 的 `mem` 命令直接使用 XMultiPool / XFixedPool 类型
 * 与公共 API。本轮总装把 XMEMORY_MULTIPOOL_ON 打开（见下方 include 之后），
 * 故此处不再需要类型别名补位；若将来再关掉 MULTIPOOL，XMultiPool.h 整块被裁
 * 时才需要下面这段补位。
#if 0
typedef struct XMultiPool XMultiPool;
typedef struct XFixedPool XFixedPool;
size_t XFixedPool_totalSize(const struct XFixedPool* pool);
size_t XFixedPool_freeSize(const struct XFixedPool* pool);
#endif

/* ==================================================================== */
/* 本轮总装：分配器改用框架自带的 XMemory（去掉 V9 heap_4 与手搓堆）       */
/*                                                                       */
/* ⚠️ 这一段必须【放在 include 底座之前】：底座 xgui_esp8266_ssd1306.h     */
/* 在 XMEMORY_MULTIPOOL_ON=0 的前提下会打一份 XMultiPool 补声明           */
/* （xgui_esp8266_ssd1306.h:585-600，int XMultiPool_is_from_pool(...)），*/
/* 而真头 XMultiPool.h:255 是 bool 版本 → 两者同时可见即 conflicting     */
/* types 编译错（首轮构建实证：XMultiPool.c/XMemory.c/XVarList.c/        */
/* XCoreApplication.c/XConsoleShellMemory.c 五个 TU 报错）。先开开关，    */
/* 底座那段补声明就自然出局。                                            */
/*                                                                       */
/* - XMEMORY_MULTIPOOL_ON=1：全局多级固定池（32/64/128/256 四档）。      */
/*   依赖约束见 XMemory_config.h:61——MULTIPOOL 强制要求 FIXEDPOOL=1，      */
/*   故两者一起打开（FIXEDPOOL 默认即级联 MULTIPOOL，这里显式钉住）。     */
/* - XMEMORY_VARIABLEPOOL_ON=1：全局 TLSF 可变池。本工程已去掉 FreeRTOS   */
/*   （不再定义 __FreeRTOS__ → XPLATFORM_BAREMETAL=1），                 */
/*   XVariablePool.c:986-1002 走 .bss 静态 arena 分支，零堆依赖。         */
/* - XVP_GLOBAL_ARENA_BYTES=12KB：非桌面默认是 32KB(FreeRTOS)/16KB(裸机) */
/*   （XMemory_config.h:164-171），裸机下这块是常驻 .bss，本板 DRAM 只剩   */
/*   ~96KB 扣掉 .rodata，故按固件实际用量收到 12KB。多级池另约 2.3KB。    */
/*                                                                       */
/* ⚠️ 配对坑（XMultiPool.c:145-152）：XMultiPool_malloc 找不到匹配档位直接  */
/*   return NULL，【不回退】可变池。因此 C512=0 且阈值同步降到 256——     */
/*   否则 257B~512B 的申请会打空（=硬失败，调用方拿到 NULL）。             */
/*   512B 档默认 8 块（约 4.2KB 子池内存）是给桌面 FreeType 的 257-512B   */
/*   临时块准备的；ESP8266 只有点阵字体，纯浪费 → 本轮置 0 整档砍掉，     */
/*   这 4.2KB 直接还给 arena。                                            */
/* ==================================================================== */
#undef XMEMORY_MULTIPOOL_ON
#define XMEMORY_MULTIPOOL_ON 1
#undef XMEMORY_FIXEDPOOL_ON
#define XMEMORY_FIXEDPOOL_ON 1
#undef XMEMORY_VARIABLEPOOL_ON
#define XMEMORY_VARIABLEPOOL_ON 1

/* 底座：XGui 档案（内部会 include FreeRTOSConfig.h 并把
 * configMAX_PRIORITIES 钉成 8、configUSE_PORT_OPTIMISED_TASK_SELECTION 置 0）。 */
#include "../app/xgui_esp8266_ssd1306.h"

#undef configCPU_CLOCK_HZ
#define configCPU_CLOCK_HZ ((unsigned long)80000000L)

/* 档位块数。子池内存从 arena 出（XFixedPool_create → XMalloc_System →
 * 可变池），所以块数直接吃 arena。块数按真机分配直方图定（stubs.c
 * xm_histogram_dump，样本见 cap_R2b_shell.log 与 cap_R10_shell.log）。
 *
 * ⚠ 第 2 轮实测根因（cap_R10_shell.log:449 起，PC 一开始 ping 就刷屏）：
 *   "XMultiPool,所有池子都耗尽了,当前请求块大小:152" / ":132" —— lwip 收包
 *   的 pbuf 正好 132B/152B，全部落到 C256 档（128 档装不下 152），而 C256
 *   只有 2 块 → 耗尽即 NULL 且【不回退】可变池（XMultiPool.c:145-152）→
 *   glue2esp 报 GLUE_ERR_ABRT(-13)，串口 "[lwip] TX FAILED by esp side,
 *   code=-13"（cap_R10_shell.log:137），PC 侧 ping 100% 丢包。
 *   同一份日志的 poolUsed=204/1928 说明 32/64 两档几乎空转：真正会【耗尽】
 *   的是 lwip 那批 132~600B 的 pbuf，而它们完全能由 TLSF 可变池从 arena 出。
 * ⇒ 本轮修法：混合阈值 256→64，128/256/512 三档整档关闭（再省 ~1KB arena）。
 *   65B 及以上一律走 arena（实测稳态 varPoolUsed=14496/24544，余 ~10KB），
 *   只有 ≤64B 的高频小对象（WiFi/lwip 每 5s 约 2000 次 <=32B + 1000 次
 *   <=64B）才进固定池 —— 这正是多级池该服务的负载形态，且 poolUsed=204
 *   远低于 16×28+8×60=928 的容量，histogram 里 32/64 两档 fail 恒为 0。
 *
 * 【第 12 轮改回】R11 的「阈值 64 + 128/256 整档关闭」是一个死法：
 *   65B 以上的全部流量被压到 arena 上，ping 负载下 arena 实测
 *   15656/24544，而 C128/C256 档的空转让 poolTotal 掉到 1928B（浪费 DRAM）。
 *   现在 Src/XMemory/XMemory.c 已给 hybrid_malloc/calloc/realloc 的
 *   MULTIPOOL 分支加了「池耗尽 → 回退可变池/系统堆」的兜底（5 处），
 *   不再需要靠压阈值来躲避打空，所以快路径可以重新覆盖到 256B。*/
/* 2026-10-21：XMultiPool 平台自实现装配（默认流程经此宏禁用，见
 * XMultiPool.c）：32/64/128/256 四档。
 * ⚠ 2026-10-22 实证：256 档【不可砍】。C256=0 时 132/152B 的 lwip 收包请求
 * 落在 256 档 → XMultiPool 返回 NULL 且不自动回退可变池 → allocFail 全灭、
 * ping 100% 丢包（rxReq=165 rxOk=0 allocFail=165，cap_probe2.log）。
 * 曾误砍过一次（以为 lwip 移独立堆后框架无此粒度负载），已回滚为 4 块。 */
/* ⚠ 2026-10-22 回退：不使用平台自定义装配钩子（XMP_GLOBAL_INIT_CUSTOM）。
 * 实证：钩子模式下即便 C256=4、poolTotal=3168 正确，lwip 收包仍 100% 失败
 * （rxReq 涨、rxOk=0、allocFail 同步涨）；而同配置走框架默认装配的
 * build/arduino_final 固件 30/30 ping 0% 丢包。差异在钩子与默认流程对
 * 子池的挂接方式，未完全定位前不要启用钩子。
 * 宏保留在 XMultiPool.c/stubs.c 里备查，这里保持注释状态。 */
/* #define XMP_GLOBAL_INIT_CUSTOM xmp_global_init_custom */
#define XMP_GLOBAL_SUB_POOL_CAPACITY 8       /* 平台自管骨架的子池容量（默认 5，留余量） */

#undef XMP_GLOBAL_C32
#define XMP_GLOBAL_C32 16        /* 16 x ~36B  ≈ 576B */
#undef XMP_GLOBAL_C64
#define XMP_GLOBAL_C64 12        /* 12 x ~68B  ≈ 816B */
#undef XMP_GLOBAL_C128
#define XMP_GLOBAL_C128 8        /*  8 x ~132B ≈ 1056B（ping 热点档） */
#undef XMP_GLOBAL_C256
#define XMP_GLOBAL_C256 4        /* 4 x ~260B ≈ 1040B（132/152B 收包热点档，勿砍） */
/* 512 档整档关闭（默认嵌入式为 8 块 ≈ 4.2KB，为桌面 FreeType 临时块而设） */
#undef XMP_GLOBAL_C512
#define XMP_GLOBAL_C512 0

/* ---- lwip TCP 窗口/发送缓冲（ask 第 2 轮指定的覆盖，带生效条件说明） ----
 * 仓库默认是 8192/8192（SRC/XCode/XNetwork/XNetwork_config.h:108-113，
 * 非桌面分支 :132-144 同样是 8192）。本固件按 ask 覆盖成 1×MSS / 2×MSS。
 * ⚠ 生效条件（改动时务必一起看）：消费这两个宏的只有
 *   Library/lwip/include/lwipopts.h:175-190（#error 校验 + TCP_WND/TCP_SND_BUF）
 *   与 Library/lwip/platform/XDeviceNetwork_lwip.c。
 *   本总装的 lwip core 编译用的是 -I. 优先的 build/arduino_final/lwipopts.h
 *   （build_fw_final.sh:126），那份文件不 include XNetwork_config.h，TCP_WND
 *   /TCP_SND_BUF 直接写成 2*TCP_MSS；platform/XDeviceNetwork_lwip.c 也不在
 *   filelist_final.txt 里。所以【这两行在本固件里目前不产生任何编译期影响】，
 *   留在这里是为了 (a) 档案自洽、(b) 将来把仓库 lwipopts 接回来时不用重改。
 *   本固件真正的 TCP 行为参数在 build/arduino_final/lwipopts.h:119-124。 */
#undef XNETWORK_LWIP_RECV_BUFFER_SIZE
#define XNETWORK_LWIP_RECV_BUFFER_SIZE 1460   /* 1 × TCP_MSS(仓库 TCP_MSS=1460) */
#undef XNETWORK_LWIP_SEND_BUFFER_SIZE
#define XNETWORK_LWIP_SEND_BUFFER_SIZE 2920   /* 2 × TCP_MSS（仓库 lwipopts 要求 >= 2×MSS） */

/* 混合阈值 = 全局池最大档（XMemory.c 的 hybrid 分界：<=阈值走多级池，
 * >阈值走可变池）。必须 ≤ 最大档，否则超过档位的申请会打空。 */
#undef XMEMORY_HYBRID_THRESHOLD
#define XMEMORY_HYBRID_THRESHOLD 256

/* 统计：XMemory.h:343 默认已开，这里显式钉住并在串口打印（stubs.c xm_stat_dump） */
#undef XMEMORY_STATISTICS_ON
#define XMEMORY_STATISTICS_ON 1

/* 一次性分配尺寸直方图（本轮新增，用于按真实分布回头调 C32/C64/C128） */
#ifndef XM_HISTO_ON
#define XM_HISTO_ON 1
#endif
/* 泄漏取证：全局池 malloc/free 配对统计（stubs.c 的 xm_leak_dump 打印）。 */
#ifndef XMEMORY_HISTO_FREE_ON
#define XMEMORY_HISTO_FREE_ON 1
#endif

/* arena = 全局 TLSF 可变池的全部内存，裸机下是常驻 .bss 静态缓冲
 * （XVariablePool.c 的 XPLATFORM_BAREMETAL 分支），零堆依赖。
 * ⚠ 12KB 不够（cap_R2b_shell.log:119/153、cap_R6_shell.log 真机数字）：
 *   启动完成 varPoolUsed=9904/12256；SoftAP 起来 5s 后 11700/12256，只剩
 *   556B，于是每拍 XGui 重绘的 XImageData（>1024B 档，直方图每拍 fail+1）
 *   在 XImageData_create（XImage.c:578 的 XMalloc_System）里打空 →
 *   "[gui] tile image init FAIL"；WiFi 关联期的客户端结构同样申请不到 →
 *   PC 连不上 XinYueAP。
 * ⚠ DRAM 上限（本轮新结论，把上一轮两条实测对齐成一条规则）：
 *   build_fw_final.sh 把 dram0_0_seg 从 SDK 默认 len=0x14000 扩到 0x17C00，
 *   等于把 0x3FFFC000..0x3FFFFC00 这 8KB（SDK 系统栈区）划给了应用。
 *   真机对照：arena 20KB → _bss_end=0x3fffce00（越过 0x3FFFC000）→
 *   cap_R3_shell.log 停在 eboot 的 ~ld 之后，ECsp/rV2/Mo 与 app_entry 的
 *   'E'/'C' 一个都没出（SDK 早期 boot 撞进系统栈区后静默停摆）；
 *   而所有 _bss_end < 0x3FFFC000 的构建（arena 12KB/16KB、bss 顶
 *   0x3fffa610~0x3fffbe00）都能起来。⇒ 规则：_bss_end 必须 < 0x3FFFC000。
 * 本轮（第 2 轮）取 arena=24KB：R7 实测（cap_R7_shell.log）14KB 已经不够 ——
 * after-init varPoolUsed=12992/14304（91%），余 1312B，SDK 胶水层
 * lwip-esp.c:221 的 pbuf_wrapper_get() 要 352B 直接打空（TX code=-1），
 * 紧接着 ieee80211_hostap_attach 解 NULL → exception 28 → 0.7s 重启循环。
 * 16KB（cap_R8_shell.log/cap_R8b_shell.log）撑住了开机与 21s 的客户端关联，
 * 但客户端一连上（cap_R8b 13.5s 起 RX#1 帧涌入）varPoolUsed 由 12992 涨到
 * 14868/16352，只剩 1484B，21.25s 板子复位（ROM 重启行 "boot not set /
 * SDK ver / phy ver" 可见）。
 * 本轮 lwip 侧再砍 PBUF_POOL 8→2 / TCP_SEG 8→2 / PBUF 8→4 等
 * （build/arduino_final/lwipopts.h:70-104，单省 ~7KB arena），
 * arena 净增 8KB 后稳态余量应到 ~9.6KB。
 * ⚠ DRAM 上限复核：16KB 那一版链接器给出 _bss_end=0x3fff5a90，距铁律上限
 * 0x3FFFC000 还有 25968B；本轮 +8KB 后 _bss_end≈0x3fff7a90（余 17648B），
 * 构建脚本每次都会把 _bss_end 打进 build 日志，需复核该行确认未越界。
 * 静态部分（cont 栈 4096B、s_fb、s_staticShell）一行未动。 */
#undef XVP_GLOBAL_ARENA_BYTES
/* 2026-10-23 布局回锚：shell RAM 裁剪（关补全/历史/缩输出缓冲）让 .bss
 * 缩了 904B → _heap_start 下移 → lwh 起点移动，恰好翻转了 esp 胶水越权写
 * 的命中模式（WiFi 收包 allocFail 永久 100%、ping 期间偶发重启；A/B 实证
 * 与 lwipopts 参数无关，见 lwipopts.h 留档）。修法=把裁剪省的 904B 原地
 * 扩进 arena，.bss 总大小与 _heap_start 回到已验证的 28/30 基线布局；
 * RAM 收益不丢——arena 余量 2412→3316B（裁剪的静态省项直接变成 arena
 * 余量），lwh 回到 16,840B 基线值。 */
#define XVP_GLOBAL_ARENA_BYTES (18 * 1024 + 904)  /* 基线 18400 + shell 裁剪回填 904 */
/* 单线程：全局可变池不加自旋锁（XMemory_config.h:189-191 的裸机默认即 0） */
#undef XVP_GLOBAL_THREADSAFE
#define XVP_GLOBAL_THREADSAFE 0

/* ---- shell 一族：gui 档案把三个总闸全置 0，这里按 console 档案打开 ---- */
#undef XCONSOLE_SHELL_ON
#define XCONSOLE_SHELL_ON 1
#undef XCONSOLE_SHELL_CONSOLE_ON
#define XCONSOLE_SHELL_CONSOLE_ON 1
#undef XCONSOLE_SHELL_ASYNC_ON
#define XCONSOLE_SHELL_ASYNC_ON 1

#define XCONSOLE_SHELL_ASYNC_AUTO_START_ON 1
#define XCONSOLE_SHELL_ADC_ON 0
#define XCONSOLE_SHELL_CALLBACK_COMMAND_ON 1
#define XCONSOLE_SHELL_CAN_ON 0
#define XCONSOLE_SHELL_CLEAR_ON 1
#define XCONSOLE_SHELL_COMMAND_CAPACITY 16
#define XCONSOLE_SHELL_COMMAND_ON 1
#define XCONSOLE_SHELL_DATETIME_ON 0
#define XCONSOLE_SHELL_DATE_ON 0
#define XCONSOLE_SHELL_EDITOR_ON 0
#define XCONSOLE_SHELL_EDITOR_TUI_ON 0
#define XCONSOLE_SHELL_EXECUTOR_ON 0
#define XCONSOLE_SHELL_EXIT_ON 1
#define XCONSOLE_SHELL_EXTERNAL_PROCESS_ON 0
#define XCONSOLE_SHELL_FILESYSTEM_ON 0
#define XCONSOLE_SHELL_GPIO_ON 0
#define XCONSOLE_SHELL_HELP_ON 1
#define XCONSOLE_SHELL_HISTORY_CAPACITY 8
#define XCONSOLE_SHELL_I2C_ON 0
#define XCONSOLE_SHELL_INFO_ON 1
#define XCONSOLE_SHELL_IO_ON 1
#define XCONSOLE_SHELL_LINE_BUFFER_SIZE 64
#define XCONSOLE_SHELL_LOGIN_ON 0
#define XCONSOLE_SHELL_LOGIN_REQUIRED_ON 0
#define XCONSOLE_SHELL_MAX_ARGUMENTS 8
#define XCONSOLE_SHELL_MAX_PATH 64
#define XCONSOLE_SHELL_MAX_SESSIONS 1
#define XCONSOLE_SHELL_MEMORY_ON 1
#define XCONSOLE_SHELL_MEMORY_POOL_ON 1
#define XCONSOLE_SHELL_MULTI_SESSION_ON 0
#define XCONSOLE_SHELL_NETWORK_ON 0
#define XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE 128
#define XCONSOLE_SHELL_PARSER_ON 1
#define XCONSOLE_SHELL_PWM_ON 0
#define XCONSOLE_SHELL_REBOOT_ON 1
#define XCONSOLE_SHELL_RESET_ON 1
#define XCONSOLE_SHELL_SHUTDOWN_ON 0
#define XCONSOLE_SHELL_SPI_ON 0
#define XCONSOLE_SHELL_TASKS_ON 1
#define XCONSOLE_SHELL_UPTIME_ON 1
#define XCONSOLE_SHELL_VIM_ON 0
#define XCONSOLE_SHELL_XIODEVICE_BACKEND_ON 0
#define XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON 0
#define XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON 0
#define XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON 0
#define XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON 0
#define XCONSOLE_SHELL_XTELNETSERVER_BACKEND_ON 0

#endif /* XGUI_SHELL_ESP8266_H */
