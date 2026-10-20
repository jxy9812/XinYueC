/*
 * FreeRTOS V9.0.0 portasm —— ESP8266 LX106, call0 ABI
 *
 * 自研 L1 向量 + 上下文切换（96 字节帧）：
 *   帧布局（字偏移）：
 *     0x00 EXIT(未用)  0x04 PC   0x08 PS   0x0C A0   0x10 A1
 *     0x14..0x48 A2..A15  0x4C SAR  0x50 LBEG 0x54 LEND 0x58 LCOUNT
 *
 * 向量偏移（XEA2, 相对 vecbase）：
 *     0x00 Level1中断  0x10 Level2/debug  0x20 NMI  0x30 Kernel异常
 *     0x50 User异常    0x70 Double异常
 *
 * 放入 .text.app_entry* 段 → 链接脚本将其钉进 IRAM（common.ld 第123行）。
 * .literal_position 让 l32r 字面量也留在本段（IRAM 内，距离合法）。
 */

    .section .text.app_entry_v9, "ax"
    .align 4
    /* 编译带 -mtext-section-literals：l32r 字面量内联进本段（IRAM），
     * 不生成独立 .literal.* 段（避免孤儿段落进 irom 导致 l32r 越界）。 */

/* ---------------- 向量槽：经 build.sh 注入 common.ld 的定点段 ----------------
 * 实测：自定义256对齐表(0x40100100)+wsr.vecbase 后，中断确被取用（PS=0 生效
 * 瞬间即触发）但向量取指报 InstrFetchProhibited(cause4, EPC1=被中断PC)。
 * NONOS SDK 的向量位于 0x40100000 基址规范偏移（+0x10 L2/+0x20 NMI/
 * +0x30 Kernel/+0x50 User/+0x70 Double；+0x00 为 call_user_start 死区——
 * SDK 自身证据）。build.sh 在生成的 common.ld 中把下列三个段插到对应
 * SDK 向量段之前（仅 +0x10/+0x30/+0x50 三槽；不动 +0x00/+0x20/+0x70
 * 以维持 ALIGN16 级联），vecbase 置0x40100000(_text_start)。 */
    .section .MyV2.text, "ax"
    .align 4
    j   _xt_f_l2                /* +0x10 L2/debug */

    .section .MyK.text, "ax"
    .align 4
    .global _xt_v9_vec_kslot
_xt_v9_vec_kslot:
    j   _xt_kex_or_l1           /* +0x30 Kernel异常/L1中断合流（RING0 唯一中断入口） */

    .section .MyU.text, "ax"
    .align 4
    .global _xt_v9_vec_uslot
_xt_v9_vec_uslot:
    j   _xt_uexc_or_l1          /* +0x50 User异常/L1中断合流（鉴别入口） */

    .section .text.app_entry_v9, "ax"

/* ---------------- +0x30 Kernel入口：L1中断 vs 真异常鉴别 ----------------
 * 本轮实证（_extract/vector.o 反汇编）：SDK 的 call_user_start 第一条指令
 * 就是 wsr.vecbase←0x40100000 并使能自己的中断——SDK 中断走 +0x50 的完整
 * 分派器（user-vector.o），其 +0x30 只是 break 兜底。因此两个槽都会在
 * 调度器启动前收到 SDK 期中断/异常，此时 pxCurrentTCB==NULL，我们若截胡
 * 必崩（r12 KEX: cause=29 StoreProhibited, l1=1, cc0=0 实证）。
 * ⇒ pxCurrentTCB==NULL 时原样交还 SDK 原生向量（0x40100034/0x40100054），
 * 调度器启动后才启用自研 L1 路径。跳转用 a5（SDK dispatcher 只保存
 * 现场 a0..a4，不得破坏）。 */
    .literal_position
    .align 4
    .global v9_l1_ownbits
v9_l1_ownbits:
    .long 0x000000C0        /* bit6 CCOMPARE0 | bit7 SOFT —— 归 V9 的源 */
    .global v9_sdk_l1_entry
v9_sdk_l1_entry:
    .long 0x40100057        /* SDK _UserExceptionVector 里的 call0 指令 */
    .global v9_psmask_lit
v9_psmask_lit:
    .long 0xFFFFFFE0
_xt_kex_or_l1:
    /* ① 先把被中断现场存进专用 SR：a0→excsave1（SDK stub 第一条就写它）、
     *    a2→excsave2、a3→excsave3。判定只用 a2/a3，算完必须从 excsave 复原
     *    再跳走——否则被中断现场、以及 SDK 自己那个异常帧（_UserExceptionVector_1
     *    只保存 a2→[a1+20]、a3→[a1+24]）拿到的都是「pending & intenable」
     *    的临时值。上轮 EXCVADDR=0xC0 的可疑点正在这里。 */
    wsr   a0, excsave1
    wsr   a2, excsave2
    wsr   a3, excsave3
    /* ② tick 源自愈：NONOS 的 SYS 任务（就在 task_os 的 ets_run 上下文里）
     *    会把 intenable 整体改写成自己的掩码，抹掉 bit6(CCOMPARE0)/bit7。
     *    上轮探针实测 ie 恒为 0x501、cc0 冻结、tick 计数不动
     *    （cap_r3_nogui.log）。这里无条件或回去：只动 intenable，不碰通用
     *    寄存器，交还 SDK 的路径同样执行，所以不依赖 SDK 定时器还在跑。 */
    rsr   a2, exccause
    bnei  a2, 4, _xt_f_kex       /* 4 = Level1Interrupt；其余是真内核异常 */
    /* ③ 判源：pending & intenable 里有我们自己的位 → 走 V9 路径（SDK 的源
     *    留在 pending 里，下一次入口再交还）；否则原样交还 SDK。 */
    rsr   a2, interrupt
    rsr   a3, intenable
    and   a2, a2, a3             /* live = pending & intenable */
    l32r  a3, v9_l1_ownbits      /* 0xC0 = CCOMPARE0 | SOFT */
    and   a2, a2, a3
    bnez  a2, .Lkex_ours
    rsr   a0, excsave1
    rsr   a2, excsave2
    rsr   a3, excsave3
    j     _v9_sdk_l1_handback    /* 只有 SDK 的源 → 原样交还 */
.Lkex_ours:
    /* 只在「已经是我们自己的源」这条路上补 tick 位。放在判源之前会有副作用：
     * SDK 启动期还没有调度器，一旦被我们打开 bit6，CCOMPARE0 立刻以陈旧值
     * 触发，走进 pxCurrentTCB==NULL 的 .Ll1_null 打成 'N' KEX
     * （cap_r4_full.log 第 2 个启动周期就是 KEX N c=00000004）。 */
    rsr   a2, intenable
    addi  a2, a2, 0xC0    /* = ori a2,a2,0xC0（LX106 汇编器无 ori） */
    wsr   a2, intenable
    esync
    rsr   a0, excsave1
    rsr   a2, excsave2
    rsr   a3, excsave3
    j     _xt_l1_vector

/* ---------------- +0x50 合流入口：中断 vs 异常鉴别 ----------------
 * SDK 期（调度器未启动）中断的主通道（见 +0x30 注释）：
 * pxCurrentTCB==NULL → 恢复现场后原样跳回 0x40100054 SDK stub（幂等：其
 * 逻辑只依赖进入时寄存器现场，我们仅借用 a2/a3→excsave2/3，SDK stub 用
 * a0/excsave1）；调度器启动后：INTPEND&INTENABLE 非零 → L1 保存路径，
 * 否则真 User 异常。 */
    .literal_position
    .align 4
_xt_uexc_or_l1:
    /* 同 +0x30：存 excsave1/2/3 → 无条件或回 tick 源 → 判源 → 复原后分流。
     * 本槽本身就是 SDK 的用户向量入口，交还即回到它自己的 ISR 路径。 */
    wsr   a0, excsave1
    wsr   a2, excsave2
    wsr   a3, excsave3
    rsr   a2, interrupt
    rsr   a3, intenable
    and   a2, a2, a3             /* live = pending & intenable */
    beqz  a2, .Lvec_real_exc     /* 无挂起中断 → 真 User 异常 */
    l32r  a3, v9_l1_ownbits      /* 0xC0 */
    and   a2, a2, a3
    bnez  a2, .Lvec_ours
    rsr   a0, excsave1
    rsr   a2, excsave2
    rsr   a3, excsave3
    j     _v9_sdk_l1_handback
.Lvec_ours:
    rsr   a2, intenable
    addi  a2, a2, 0xC0    /* 同 +0x30 的 .Lkex_ours：只在自己有源时补 */
    wsr   a2, intenable
    esync
    rsr   a0, excsave1
    rsr   a2, excsave2
    rsr   a3, excsave3
    j     _xt_l1_vector
.Lvec_real_exc:
    rsr   a0, excsave1
    rsr   a2, excsave2
    rsr   a3, excsave3
    j     _xt_user_exc


/* ---------------- 交还 SDK 原生 level-1 分派器 ----------------
 * 非 V9 的 level-1 中断源（WiFi/LWIP/ets_timer）在这里把控制权还给 SDK 的
 * +0.50 用户向量入口，由它按 exccause==4 走自己的 ISR 路径并负责返回被
 * 中断代码。vPortSdkL1Handback 不返回（port_isr.c 内跳 0x40100054）。 */
    .literal_position
    .align 4
_v9_sdk_l1_handback:
    /* 直接跳 SDK 原生 +0x50 入口的 call0 指令处（0x40100057）——
     * 0x40100054 是它的 wsr a0,excsave1，我们已经替 SDK 做过了。
     * 实测跳 0x40100054 会因重做 wsr 而毁掉 SDK 异常帧（cap_r2_f.log 挂死），
     * 跳 0x40100057 才能完整启动（cap_r2_g.log）。 */
    l32r  a2, v9_sdk_l1_entry
    jx    a2
2:  j     2b

/* ---------------- Level 1 中断入口 ---------------- */
    .align 4
_xt_l1_vector:
    addi  a1, a1, -96
    s32i  a0, a1, 0x0C
    rsr   a0, epc1
    s32i  a0, a1, 0x04
    rsr   a0, ps
    s32i  a0, a1, 0x08
    addi  a0, a1, 96
    s32i  a0, a1, 0x10
    s32i  a2, a1, 0x14
    s32i  a3, a1, 0x18
    /* 诊断：入口计数尽早落盘（a2/a3 刚存帧即可用；位于保存序列最前部，
     * 若向量跑到这里即计数+1——区分‘向量取指失败’vs‘中断未尝试’） */
    movi  a2, vPortL1Entries
    l32i  a3, a2, 0
    addi  a3, a3, 1
    s32i  a3, a2, 0
    movi  a2, vPortEpcAtEntry
    l32i  a3, a1, 4              /* 帧 PC 槽 = 入口时 EPC1 */
    s32i  a3, a2, 0
    s32i  a4, a1, 0x1C
    s32i  a5, a1, 0x20
    s32i  a6, a1, 0x24
    s32i  a7, a1, 0x28
    s32i  a8, a1, 0x2C
    s32i  a9, a1, 0x30
    s32i  a10, a1, 0x34
    s32i  a11, a1, 0x38
    s32i  a12, a1, 0x3C
    s32i  a13, a1, 0x40
    s32i  a14, a1, 0x44
    s32i  a15, a1, 0x48
    rsr   a0, sar
    s32i  a0, a1, 0x4C
    /* XCHAL_HAVE_LOOPS=0（LX106 无零开销循环选项）：0x50..0x58 不用 */
    /* 调度器未启动保护：pxCurrentTCB==NULL 时帧无处回写，后续恢复必崩
     * （r12 KEX 实证）。此时打 'N' 并进 KEX dump 保留完整现场。 */
    call0 vPortTcbValue         /* a2 = pxCurrentTCB（a0 已存帧，可弃） */
    beqz  a2, .Ll1_null
    /* 把当前帧基址写回 pxCurrentTCB->pxTopOfStack（对应 SDK _xt_int_enter 的
     * 收尾步；漏掉这步会导致恢复时用创建时的陈旧帧） */
    mov.n a2, a1                /* 第一参：帧指针 */
    movi  a3, vPortSaveTopOfStackFrame
    callx0 a3
    mov.n a2, a1                /* 第一参：帧指针（再取，a1 不变） */
    movi  a3, vPortL1Dispatch
    callx0 a3
    /* 分发结束：切到（可能已更新的）pxCurrentTCB 的帧 */
    movi  a3, pxPortGetTopOfStackFrame
    callx0 a3
    mov.n a1, a2
    j     _xt_v9_restore

.Ll1_null:
    /* 调度器未启动却收到 L1 中断：打 'N' + KEX dump（现场含 epc1/ps） */
    movi  a2, v9_kex_tag
    movi  a3, 'N'
    s32i  a3, a2, 0
    j     _v9_kex_common

/* ---------------- 恢复：a1 = 帧基址 ---------------- */
    .literal_position
    .align 4
_xt_v9_restore:
    /* refresh running-task shadow pointer (vPortRunningTcb <- pxCurrentTCB):
     * interrupt frames must be written back to the TCB actually executing,
     * not to pxCurrentTCB which an API may have changed already
     * (see vPortRunningTcb comment in port_isr.c re vTaskDelay ordering). */
    call0 vPortSetRunningTcb
    l32i  a3, a1, 0x4C
    wsr   a3, sar
    l32i  a3, a1, 0x08          /* PS 原值 */
    l32r  a4, v9_psmask_lit
    and   a3, a3, a4            /* 清 INTLEVEL/EXCM */
    l32i  a4, a1, 0x04          /* 恢复目标 PC */
    l32i  a2, a1, 0x14
    l32i  a5, a1, 0x20
    l32i  a6, a1, 0x24
    l32i  a7, a1, 0x28
    l32i  a8, a1, 0x2C
    l32i  a9, a1, 0x30
    l32i  a10, a1, 0x34
    l32i  a11, a1, 0x38
    l32i  a12, a1, 0x3C
    l32i  a13, a1, 0x40
    l32i  a14, a1, 0x44
    l32i  a15, a1, 0x48
    l32i  a0, a1, 0x0C
    l32i  a0, a1, 0x0C
    wsr   a3, ps
    l32i  a3, a1, 0x18
    l32i  a1, a1, 0x10
    rsync
    jx    a4

/* ---------------- 启动第一个任务 ---------------- */
    .align 4
    .global vPortStartFirstTask
vPortStartFirstTask:
    movi  a3, pxPortGetTopOfStackFrame
    callx0 a3
    mov.n a1, a2
    j     _xt_v9_restore

/* ---------------- 致命路径：各向量打不同标记 + 诊断（诊断用） ----------------
 * 本轮唯一变量：KEX 输出改走 uart0_putc_raw（带水位轮询），并 spill 出完整
 * 窗口寄存器现场。
 *
 * 关键前提（实证 + 架构）：
 *  - Xtensa 异常入口 CPU 不改 windowbase；call0 ABI 不开窗 ⇒ 这里 mov/s32i
 *    读到的 a0..a15 就是被中断代码的窗口寄存器（工具链无 ruw/rur 形式可用：
 *    实测 `ruw a2,a0` 报 unknown opcode，`rur` 只接受 a0..a3 作源操作数，
 *    故放弃 RUR 方案，直接用当前窗口读）。
 *  - spill 放在异常入口第一拍，早于任何 wsr/call0，避免 gate 覆写现场。
 *  - 裸 s32i 直写 0x60000000 已实证只出 "KXE" 三字符即断（FIFO 128B 不等
 *    水位必丢），标记字符也一并改写到 RAM 标签，由 C dump 打印。
 * 帧布局（64B，a1 已 addi -64）：a0@[0] a2@[8] a3@[12] a4@[16] … a15@[60]，
 * 原始 a1 = 帧基址 + 64（C 侧据此还原）。 */

    .section .text.app_entry_v9, "ax"
    .align 4
_v9_kex_common:
    addi  a1, a1, -64
    s32i  a0, a1, 0x00
    s32i  a2, a1, 0x08
    s32i  a3, a1, 0x0C
    s32i  a4, a1, 0x10
    s32i  a5, a1, 0x14
    s32i  a6, a1, 0x18
    s32i  a7, a1, 0x1C
    s32i  a8, a1, 0x20
    s32i  a9, a1, 0x24
    s32i  a10, a1, 0x28
    s32i  a11, a1, 0x2C
    s32i  a12, a1, 0x30
    s32i  a13, a1, 0x34
    s32i  a14, a1, 0x38
    s32i  a15, a1, 0x3C
    mov.n a2, a1                /* 第一参 = 帧基址 */
    movi  a3, vPortKernelExcDump
    callx0 a3
1:  j     1b

    .section .text.app_entry_v9, "ax"
    .align 4
_xt_f_l2:                       /* 0x10 Level2/debug：'#' 标记后进 KEX dump
                                 * （r11 实证 '#' 洪流=跑飞 PC 落此） */
    movi  a2, v9_kex_tag
    movi  a3, '#'
    s32i  a3, a2, 0
    j     _v9_kex_common

    .align 4
_xt_f_nmi:                      /* 0x20 NMI */
    movi  a2, v9_kex_tag
    movi  a3, '?'
    s32i  a3, a2, 0
    j     _v9_kex_common

    .align 4
_xt_f_kex:                      /* 0x30 Kernel 异常 → C 诊断（打印寄存器） */
    movi  a2, v9_kex_tag
    movi  a3, 'K'
    s32i  a3, a2, 0
    j     _v9_kex_common

    .align 4
_xt_f_dex:                      /* 0x70 Double 异常：'%' 标记后进 KEX dump */
    movi  a2, v9_kex_tag
    movi  a3, '%'
    s32i  a3, a2, 0
    j     _v9_kex_common

    .align 4
_xt_user_exc:
    movi  a2, v9_kex_tag
    movi  a3, '@'
    s32i  a3, a2, 0
    j     _v9_kex_common

