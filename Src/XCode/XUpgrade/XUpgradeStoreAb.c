// XUpgradeStoreAb.c
/**
 * @file       XUpgradeStoreAb.c
 * @brief      升级存储 A/B 后端实现：流式写非活动槽 + 元数据块双写仲裁 + 宿主模拟分区。
 * @details    XUpgradeStore 契约（Src/XPlatform/XUpgrade/XUpgradeStore.h）的
 *             A/B 后端实现（设计文档 XUpgrade.md §9，M3）。分区定位、槽块读
 *             写与元数据块擦写等扇区级原语经 XUpgradeStore_setPartitionBackend
 *             由 Drive 注册（模式同 XSystem_setRebootHandler），流程编排全部
 *             在本文件：
 *             - staging：stageBegin 仲裁元数据取非活动槽（slotCount/slotSize
 *               预检，按 expectedSize 预擦除）；stageWrite 把数据经 m_write
 *               流式直写槽内（不落临时文件，Fatfs 空间可能不足），同时维护
 *               写路径流式 SHA-256；stageEnd 把已写区间整槽回读再算 SHA-256，
 *               与写路径摘要交叉比对（后端级校验，可配默认开），摘要随
 *               activate 写入元数据块（bootloader 可选校验）。
 *             - activate：按双写规则置 requestedSlot=新槽、bootAttempts=0、
 *               sequence+1，先写影子块（非胜出块）再写胜出块，任意断电时刻
 *               至少一块合法；随后 XSystem_reboot(Bootloader)（宿主模拟经
 *               XSystem_setRebootHandler 注入回调，不真重启）。
 *             - confirmBoot：自检通过后按同一双写规则置 activeSlot=
 *               requestedSlot、bootAttempts=0、sequence+1。
 *             - 仲裁：CRC 合法且 sequence 大者胜；相等且均合法取元数据块 A；
 *               两块皆坏报 Io（应用侧停用自动升级，bootloader 回退板级缺省
 *               槽，永不两个都动）。
 *             - boot 计数超限回退逻辑（§9.3 规则 2/3）收口为可被测试直接
 *               调用的纯函数 XUpgradeStoreAb_bootPolicyStep（模拟 bootloader
 *               决策，不做任何 IO）。
 *             - 不变式（§16 矩阵 8-11）：从不覆写 activeSlot 指向槽的镜像
 *               数据；任意时刻断电设备要么启动旧槽，要么启动新槽并仍可回旧
 *               槽，不存在两槽同时不可用的状态。
 *             currentVersion/listVersions 的版本来源：镜像头内嵌版本格式未
 *             定稿（§17.3）前，以 staging 完成时写入 update/ 工作区的槽版本
 *             标记（update/abslot/slot&lt;N&gt;.ver，XSaveFile 原子写）为过渡载
 *             体；标记缺失/非法视作未激活（走启动自检回滚路径），镜像头格
 *             式定稿后可整体切换，元数据块 60 字节布局不变。
 *             宿主模拟（XUPGRADE_AB_SIM_ON）：installSimBackend 以
 *             slot_a/b.img + meta_a/b.bin 四个文件模拟双槽与双元数据块，每
 *             次写后即落盘；文件无擦除概念，erase 原语直接返回 Ok。仅供测试
 *             与桌面演练，生产固件置 XUPGRADE_AB_SIM_ON=0 裁出。
 */

#include "XUpgradeStore.h"

#include "xupgrade_shared.h" /* shared xups_* helpers */
#include <string.h>
#include <stdlib.h>

#include "XMemory.h"
#include "XString.h"
#include "XIODevice.h"
#include "XFile.h"
#include "XFileDevice.h"
#include "XFileInfo.h"
#include "XDir.h"
#include "XSaveFile.h"
#include "XCrc.h"
#include "XSystem.h"

#if XUPGRADE_ON
#if XUPGRADE_AB_ON

#if XUPGRADE_VERIFY_ON
/* XUpgradeVerify.c 的流式 SHA-256 封装未设公开头（与 XUpgradePackage.c 同口
 * 径：本地 extern 声明，两侧签名一致）；句柄为不透明类型。 */
typedef struct XUpgradeVerifySha256 XUpgradeVerifySha256;
extern XUpgradeVerifySha256* XUpgradeVerify_sha256Create(void);
extern void XUpgradeVerify_sha256Delete(XUpgradeVerifySha256* ctx);
extern bool XUpgradeVerify_sha256Begin(XUpgradeVerifySha256* ctx);
extern bool XUpgradeVerify_sha256Update(XUpgradeVerifySha256* ctx, const uint8_t* data, size_t len);
extern bool XUpgradeVerify_sha256End(XUpgradeVerifySha256* ctx, uint8_t* outDigest);
#endif /* XUPGRADE_VERIFY_ON */

/* ========================================================================== */
/*                        A/B 后端内部布局常量                                 */
/* ========================================================================== */

/** @brief 整槽回读哈希/擦除的切块字节数：堆上临时缓冲按该步长分块（内存占用固定）。 */
#ifndef XUPGRADE_AB_IO_CHUNK
#define XUPGRADE_AB_IO_CHUNK 4096u
#endif
/** @brief installSimBackend 允许的最小单槽容量（字节；契约规定 64 KiB）。 */
#define XUPGRADE_AB_SIM_SLOT_MIN 65536u
/** @brief installSimBackend 单槽容量缺省值（字节；契约规定 4 MiB）。 */
#define XUPGRADE_AB_SIM_SLOT_DEFAULT (4u * 1024u * 1024u)
/** @brief 模拟槽/元数据文件的擦除填充字节（真实 flash 擦除态读出值）。 */
#define XUPGRADE_AB_FLASH_ERASED 0xFFu
/** @brief 模拟槽文件名：slot_a.img（镜像槽 A）。 */
#define XUPGRADE_AB_SIM_SLOT_A "slot_a.img"
/** @brief 模拟槽文件名：slot_b.img（镜像槽 B）。 */
#define XUPGRADE_AB_SIM_SLOT_B "slot_b.img"
/** @brief 模拟元数据块文件名：meta_a.bin（元数据块 A，60 字节）。 */
#define XUPGRADE_AB_SIM_META_A "meta_a.bin"
/** @brief 模拟元数据块文件名：meta_b.bin（元数据块 B，60 字节）。 */
#define XUPGRADE_AB_SIM_META_B "meta_b.bin"
/** @brief update/ 工作区内 A/B 槽版本标记子目录名：<root>/update/abslot/。 */
#define XUPGRADE_AB_MARK_DIR "abslot"
/** @brief 槽版本标记文件名前缀：<root>/update/abslot/slot&lt;N&gt;.ver。 */
#define XUPGRADE_AB_MARK_PREFIX "slot"
/** @brief 槽版本标记文件名后缀。 */
#define XUPGRADE_AB_MARK_SUFFIX ".ver"

/* ========================================================================== */
/*                        编译期断言（契约强制）                               */
/* ========================================================================== */

/* XUpgradeBootMeta 序列化定长 60 字节、无填充（XUpgradeStore.h 契约：实现文
 * 件必须以编译期断言固定；C99 数组越界技巧）。 */
typedef char xupgrade_ab_metaLayoutAssert[(XUPGRADE_BOOT_META_SIZE == 60u) ? 1 : -1];
typedef char xupgrade_ab_metaSizeofAssert[(sizeof(XUpgradeBootMeta) == XUPGRADE_BOOT_META_SIZE) ? 1 : -1];

/* ========================================================================== */
/*                        派生结构体与虚函数表枚举                              */
/* ========================================================================== */

/**
 * @brief A/B 后端派生结构体（实现私有）。
 * @details 首成员为基类 XUpgradeStore；staging 上下文与最近一次 staging 结果
 *          由本结构维护，一个实例同一时间只允许一路 staging 流（契约约束）。
 */
typedef struct XUpgradeStoreAb {
    XUpgradeStore m_parent;        /**< 基类，第一个成员，由 XObject 管理；禁止手工修改。 */
    const XUpgradePartitionBackend* m_backend; /**< 分区原语回调集（create 时快照的全局注册）；借用，不取得所有权。 */
    XString* m_installRoot;        /**< 安装根目录完整路径（UTF-8 内部存储，create 时复制）；仅 update/ 工作区使用。 */
    XMemoryType m_memory;          /**< 对象与 IO 缓冲使用的分配器类型（create_ex 入参）。 */
    bool m_stageActive;            /**< 是否处于 staging 流中（stageBegin→stageEnd 之间）。 */
    uint32_t m_stageSlot;          /**< 本次 staging 的写入槽索引（非活动槽）。 */
    uint64_t m_stageSlotSize;      /**< 写入槽容量（字节，stageBegin 时查询）。 */
    uint64_t m_stageOffset;        /**< 已写入字节数（下一次 stageWrite 的槽内偏移）。 */
    XString* m_stageVersion;       /**< 本次 staging 的版本串副本；不在流中时为 NULL。 */
    bool m_stagedValid;            /**< 是否存在已完成的 staging（activate 的前置条件）。 */
    uint32_t m_stagedSlot;         /**< 最近一次完成 staging 的槽索引。 */
    bool m_stagedShaValid;         /**< m_stagedSha 是否有效（XUPGRADE_VERIFY_ON=0 时恒 false）。 */
    uint8_t m_stagedSha[XUPGRADE_SHA256_BIN_SIZE]; /**< 最近一次完成 staging 的整槽回读摘要；activate 写入元数据 m_imageSha256。 */
    XString* m_stagedVersion;      /**< 最近一次完成 staging 的版本串副本；无则为 NULL。 */
} XUpgradeStoreAb;

XCLASS_DEFINE_BEGING(XUpgradeStoreAb)
XCLASS_DEFINE_EXTEND_END(XUpgradeStoreAb, XUpgradeStore)

static XVtable* XUpgradeStoreAb_class_init(void);

/* ========================================================================== */
/*                        内部静态函数声明                                     */
/* ========================================================================== */

static void VXUpgradeStoreAb_deinit(XUpgradeStoreAb* store);
static XUpgradeError VXUpgradeStoreAb_stageBegin(XUpgradeStore* self, const char* componentId,
                                                 const char* version, uint64_t expectedSize,
                                                 const char** errText);
static XUpgradeError VXUpgradeStoreAb_stageWrite(XUpgradeStore* self, const void* data, uint32_t len);
static XUpgradeError VXUpgradeStoreAb_stageEnd(XUpgradeStore* self, const char** errText);
static XUpgradeError VXUpgradeStoreAb_activate(XUpgradeStore* self, const char* componentId,
                                               const char* version, const char** errText);
static bool VXUpgradeStoreAb_currentVersion(XUpgradeStore* self, const char* componentId,
                                            char* out, uint32_t cap);
static XUpgradeError VXUpgradeStoreAb_rollback(XUpgradeStore* self, const char* componentId,
                                               const char* version, const char** errText);
static XUpgradeError VXUpgradeStoreAb_listVersions(XUpgradeStore* self, const char* componentId,
                                                   XUpgradeStoreEmitVersion emit, void* userData);
static XUpgradeError VXUpgradeStoreAb_confirmBoot(XUpgradeStore* self, const char* componentId,
                                                  const char* version, const char** errText);
/* ========================================================================== */
/*                        internal helpers                                    */
/* ========================================================================== */

/* setErr/cstrLen/validName/extractVersion are consolidated in
 * xupgrade_shared.h (xups_* prefix); buildPath keeps a 3-part thin wrapper
 * delegating to the shared 4-part implementation. */

static XString* xupgrade_ab_buildPath(const XString* base, const char* part1,
                                      const char* part2, const char* part3)
{
    return xups_build_path(base, part1, part2, part3, NULL);
}

/**
 * @brief 递归创建目录（先例 XUpgradeEngine.c xup_make_dirs）。
 * @param path 目录完整路径；UTF-8 借用。
 * @return 创建成功（或已存在）返回 true；path 为 NULL 或创建失败返回 false。
 */
static bool xupgrade_ab_makeDirs(const XString* path)
{
    XDir* dir;
    XString* dot;
    bool ok;
    if (!path) return false;
    dir = XDir_create_2(path);
    if (!dir) return false;
    dot = XString_create_utf8(".");
    ok = dot && XDir_mkpath(dir, dot);
    if (dot) XClassDelete(dot);
    XClassDelete((XClass*)dir);
    return ok;
}

/* ========================================================================== */
/*                  分区原语注册与槽位查询（XUpgradeStore 契约）                */
/* ========================================================================== */

/** @brief 全局分区原语注册槽；仅初始化阶段读写（模式同 XSystem_setRebootHandler，不加锁）。 */
static const XUpgradePartitionBackend* g_xupgrade_ab_partitionBackend = NULL;
#if XUPGRADE_AB_SIM_ON
/** @brief 当前注册是否为宿主模拟后端（与显式 Drive 注册互斥的判定依据）。 */
static bool g_xupgrade_ab_partitionIsSim = false;
#endif

void XUpgradeStore_setPartitionBackend(const XUpgradePartitionBackend* backend)
{
    g_xupgrade_ab_partitionBackend = backend;
#if XUPGRADE_AB_SIM_ON
    /* 显式 Drive 注册覆盖模拟后端（互斥规则的另一方向：装模拟后端时见
     * XUpgradeStoreAb_installSimBackend 的冲突检查）。 */
    g_xupgrade_ab_partitionIsSim = false;
#endif
}

/**
 * @brief 查询镜像槽数并校验落在契约区间 [2, XUPGRADE_AB_SLOT_MAX]。
 * @param be 分区原语回调集；不能为 NULL。
 * @param outCount 输出槽数；不能为 NULL。
 * @return 成功返回 XUpgradeError_Ok；回调缺失返回 NotSupported；回调失败或
 *         槽数越出契约区间（配置错误）返回对应错误码/Io。
 */
static XUpgradeError xupgrade_ab_querySlotCount(const XUpgradePartitionBackend* be,
                                                uint32_t* outCount)
{
    XUpgradeError err;
    if (!be || !outCount) return XUpgradeError_InvalidArgument;
    *outCount = 0;
    if (!be->m_slotCount) return XUpgradeError_NotSupported;
    err = be->m_slotCount(be->m_userData, outCount);
    if (err != XUpgradeError_Ok) return err;
    if (*outCount < 2u || *outCount > XUPGRADE_AB_SLOT_MAX) {
        return XUpgradeError_Io;   /* 槽数少于 2 视作配置错误（契约）。 */
    }
    return XUpgradeError_Ok;
}

/**
 * @brief 查询指定槽容量。
 * @return 成功返回 XUpgradeError_Ok；回调缺失返回 NotSupported；槽索引越界
 *         或回调失败原样返回其错误码。
 */
static XUpgradeError xupgrade_ab_querySlotSize(const XUpgradePartitionBackend* be,
                                               uint32_t slotIndex, uint64_t* outSize)
{
    if (!be || !outSize) return XUpgradeError_InvalidArgument;
    *outSize = 0;
    if (!be->m_slotSize) return XUpgradeError_NotSupported;
    return be->m_slotSize(be->m_userData, slotIndex, outSize);
}

/* ========================================================================== */
/*              元数据块序列化 / CRC / 仲裁 / 双写（§9.2/§9.3/§9.4）            */
/* ========================================================================== */

/**
 * @brief 按小端写一个 u32 到字节缓冲。
 * @param out 输出缓冲；调用方提供，至少 4 字节。
 * @param value 待写入值。
 */
static void xupgrade_ab_storeU32(uint8_t* out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

/**
 * @brief 从字节缓冲按小端读一个 u32。
 * @param in 输入缓冲；至少 4 字节。
 * @return 读取的值。
 */
static uint32_t xupgrade_ab_loadU32(const uint8_t* in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8)
           | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

/**
 * @brief 把元数据块内存表示按 §9.2 布局序列化（小端、无填充、60 字节定长）。
 * @param meta 内存表示；不能为 NULL。
 * @param out 输出缓冲；调用方提供，容量不小于 XUPGRADE_BOOT_META_SIZE。
 * @return 无。
 * @note 偏移布局：0 magic、4 crc32、8 activeSlot、12 requestedSlot、
 *       16 bootAttempts、20 maxAttempts、24 imageSha256[32]、56 sequence。
 *       编译期断言已固定 sizeof(XUpgradeBootMeta)==60，但序列化不依赖主机
 *       字节序与结构体布局假设（逐字段小端展开）。
 */
static void xupgrade_ab_metaSerialize(const XUpgradeBootMeta* meta, uint8_t* out)
{
    if (!meta || !out) return;
    xupgrade_ab_storeU32(out + 0, meta->m_magic);
    xupgrade_ab_storeU32(out + 4, meta->m_crc32);
    xupgrade_ab_storeU32(out + 8, meta->m_activeSlot);
    xupgrade_ab_storeU32(out + 12, meta->m_requestedSlot);
    xupgrade_ab_storeU32(out + 16, meta->m_bootAttempts);
    xupgrade_ab_storeU32(out + 20, meta->m_maxAttempts);
    memcpy(out + 24, meta->m_imageSha256, XUPGRADE_SHA256_BIN_SIZE);
    xupgrade_ab_storeU32(out + 56, meta->m_sequence);
}

/**
 * @brief 按 §9.2 布局反序列化元数据块（xupgrade_ab_metaSerialize 的逆变换）。
 * @param in 输入缓冲；容量不小于 XUPGRADE_BOOT_META_SIZE。
 * @param out 输出内存表示；不能为 NULL，失败时（入参为 NULL）不写入。
 * @return 成功返回 true；in 为 NULL 返回 false。
 */
static bool xupgrade_ab_metaDeserialize(const uint8_t* in, XUpgradeBootMeta* out)
{
    if (!in || !out) return false;
    out->m_magic = xupgrade_ab_loadU32(in + 0);
    out->m_crc32 = xupgrade_ab_loadU32(in + 4);
    out->m_activeSlot = xupgrade_ab_loadU32(in + 8);
    out->m_requestedSlot = xupgrade_ab_loadU32(in + 12);
    out->m_bootAttempts = xupgrade_ab_loadU32(in + 16);
    out->m_maxAttempts = xupgrade_ab_loadU32(in + 20);
    memcpy(out->m_imageSha256, in + 24, XUPGRADE_SHA256_BIN_SIZE);
    out->m_sequence = xupgrade_ab_loadU32(in + 56);
    return true;
}

/**
 * @brief 计算 60 字节序列化块的 CRC32（覆盖 0..3 与 8..59 两段，跳过 crc 自身）。
 * @param buf 已序列化的元数据块字节；容量不小于 XUPGRADE_BOOT_META_SIZE。
 * @return CRC32 值（ISO-HDLC，与引擎 state.json 同算法族）。
 */
static uint32_t xupgrade_ab_metaCrcOf(const uint8_t* buf)
{
    uint32_t crc = XCrc32_initialize(XCrc32_Algorithm_IsoHdlc);
    crc = XCrc32_update(XCrc32_Algorithm_IsoHdlc, crc, buf, 4);
    crc = XCrc32_update(XCrc32_Algorithm_IsoHdlc, crc, buf + 8,
                        XUPGRADE_BOOT_META_SIZE - 8);
    return crc;
}

/**
 * @brief 就地为元数据块重算并写入 CRC32 字段。
 * @param meta 待更新内存表示；不能为 NULL。
 * @return 无。
 * @note 序列化→算 CRC→回填 m_crc32；后续再次序列化得到合法磁盘块。
 */
static void xupgrade_ab_metaUpdateCrc(XUpgradeBootMeta* meta)
{
    uint8_t buf[XUPGRADE_BOOT_META_SIZE];
    if (!meta) return;
    xupgrade_ab_metaSerialize(meta, buf);
    meta->m_crc32 = xupgrade_ab_metaCrcOf(buf);
}

/**
 * @brief 校验元数据块合法性：魔数吻合且重序列化后 CRC 自洽。
 * @param meta 待校验内存表示；可为 NULL。
 * @return 合法返回 true；NULL、魔数不符或 CRC 不符返回 false。
 * @note 分区原语按契约不解释内容（不校验 CRC），合法性判定统一收口在此：
 *       把收到的内存表示按本侧同一序列化规则重排字节再算 CRC，可同时暴露
 *       原语侧布局错误与介质位翻转。
 */
static bool xupgrade_ab_metaValid(const XUpgradeBootMeta* meta)
{
    uint8_t buf[XUPGRADE_BOOT_META_SIZE];
    if (!meta || meta->m_magic != XUPGRADE_BOOT_META_MAGIC) return false;
    xupgrade_ab_metaSerialize(meta, buf);
    return xupgrade_ab_metaCrcOf(buf) == meta->m_crc32;
}

/**
 * @brief 构造出厂态元数据块（双块一致，A 槽为 active）。
 * @param meta 输出内存表示；不能为 NULL。
 * @return 无。
 * @note sequence 从 1 起（0 视作未初始化）；maxAttempts 取契约缺省
 *       XUPGRADE_BOOT_ATTEMPTS_DEFAULT；imageSha256 全零（无镜像可校验）。
 *       仅宿主模拟装机与 sequence 绕回恢复使用；真实板级出厂元数据由
 *       bootloader/产测写入。
 */
static void xupgrade_ab_metaInitFactory(XUpgradeBootMeta* meta)
{
    if (!meta) return;
    memset(meta, 0, sizeof(*meta));
    meta->m_magic = XUPGRADE_BOOT_META_MAGIC;
    meta->m_activeSlot = 0;
    meta->m_requestedSlot = 0;
    meta->m_bootAttempts = 0;
    meta->m_maxAttempts = XUPGRADE_BOOT_ATTEMPTS_DEFAULT;
    meta->m_sequence = 1;
    xupgrade_ab_metaUpdateCrc(meta);
}

/**
 * @brief 仲裁：读双块，取 CRC 合法且 sequence 大者为胜出块（§9.3 规则 1）。
 * @param be 分区原语回调集；不能为 NULL。
 * @param outWinner 输出胜出块内存表示；不能为 NULL，两块皆坏时不写入。
 * @param outWinnerIndex 输出胜出块编号 [0, XUPGRADE_BOOT_META_COPIES)；不能为 NULL。
 * @return 成功返回 XUpgradeError_Ok；回调缺失返回 NotSupported；回调失败原
 *         样返回；两块皆坏（魔数或 CRC 非法）返回 XUpgradeError_Io。
 * @note sequence 相等且均合法（异常场景，如双块同写中断的极端落点）取元数
 *       据块 A（metaIndex 0）为胜出；AbStore 下一次合法更新恢复双块一致。
 */
static XUpgradeError xupgrade_ab_metaArbitrate(const XUpgradePartitionBackend* be,
                                               XUpgradeBootMeta* outWinner,
                                               uint32_t* outWinnerIndex)
{
    XUpgradeBootMeta blocks[XUPGRADE_BOOT_META_COPIES];
    bool valid[XUPGRADE_BOOT_META_COPIES];
    uint32_t i;
    int32_t winner = -1;

    if (!be || !outWinner || !outWinnerIndex) return XUpgradeError_InvalidArgument;
    if (!be->m_metaRead) return XUpgradeError_NotSupported;
    for (i = 0; i < XUPGRADE_BOOT_META_COPIES; ++i) {
        XUpgradeError err = be->m_metaRead(be->m_userData, i, &blocks[i]);
        if (err != XUpgradeError_Ok) return err;
        valid[i] = xupgrade_ab_metaValid(&blocks[i]);
    }
    for (i = 0; i < XUPGRADE_BOOT_META_COPIES; ++i) {
        if (!valid[i]) continue;
        if (winner < 0 || blocks[i].m_sequence > blocks[winner].m_sequence) {
            winner = (int32_t)i;
        }
        /* sequence 相等时保留先找到的块：枚举顺序 A→B，即相等取 A（契约）。 */
    }
    if (winner < 0) {
        return XUpgradeError_Io;   /* 两块皆坏：存储级故障（契约规则 1）。 */
    }
    *outWinner = blocks[winner];
    *outWinnerIndex = (uint32_t)winner;
    return XUpgradeError_Ok;
}

/**
 * @brief 按双写规则提交一次元数据更新：先写影子块（非胜出块）再写胜出块（§9.4 规则 2）。
 * @param be 分区原语回调集；不能为 NULL。
 * @param winnerIndex 仲裁胜出块编号；调用方经 xupgrade_ab_metaArbitrate 取得。
 * @param newState 新状态内存表示（CRC 已就绪）；不能为 NULL。
 * @return 成功返回 XUpgradeError_Ok；回调缺失返回 NotSupported；任一块写入
 *         失败返回 XUpgradeError_Io。
 * @note 断电不变式：写影子块中断电→胜出块仍为旧状态（本次更新丢失，设备
 *       安全回旧槽）；写胜出块中断电→影子块已为新状态（更新生效）。两块
 *       皆坏需两次连续断电叠加，不属于单次更新窗口（契约规则 2 注）。
 */
static XUpgradeError xupgrade_ab_metaWriteBoth(const XUpgradePartitionBackend* be,
                                               uint32_t winnerIndex,
                                               const XUpgradeBootMeta* newState)
{
    uint32_t shadow;
    if (!be || !newState) return XUpgradeError_InvalidArgument;
    if (!be->m_metaWrite) return XUpgradeError_NotSupported;
    shadow = (winnerIndex + 1u) % XUPGRADE_BOOT_META_COPIES;
    if (be->m_metaWrite(be->m_userData, shadow, newState) != XUpgradeError_Ok) {
        return XUpgradeError_Io;
    }
    if (be->m_metaWrite(be->m_userData, winnerIndex, newState) != XUpgradeError_Ok) {
        return XUpgradeError_Io;
    }
    return XUpgradeError_Ok;
}

/**
 * @brief 在胜出块基础上递增 sequence 并重算 CRC。
 * @param winner 胜出块内存表示；不能为 NULL。
 * @param out 新状态内存表示；不能为 NULL，可为 winner 本体。
 * @return 无。
 * @note u32 绕回时跳过 0 回到 1（0 视作未初始化态）；按契约绕回前的双块整
 *       体重置由维护流程负责，应用侧只保证不写出 0。
 */
static void xupgrade_ab_metaBumpSequence(const XUpgradeBootMeta* winner, XUpgradeBootMeta* out)
{
    uint32_t seq;
    if (!winner || !out) return;
    *out = *winner;
    seq = winner->m_sequence + 1u;
    if (seq == 0u) seq = 1u;
    out->m_sequence = seq;
    xupgrade_ab_metaUpdateCrc(out);
}

/* ========================================================================== */
/*          boot 计数回退策略纯函数（§9.3 规则 2/3，测试直接调用）              */
/* ========================================================================== */

/**
 * @brief 模拟 bootloader 的启动决策纯函数（§9.3 规则 2/3，不做任何 IO）。
 * @param meta 仲裁胜出的元数据块内存表示；不能为 NULL 且必须通过
 *             xupgrade_ab_metaValid（魔数+CRC 合法）。
 * @param slotCount 镜像槽数；必须落在 [2, XUPGRADE_AB_SLOT_MAX]。
 * @param outMeta 输出写回用的元数据（bootAttempts 已按规则调整，sequence 不
 *                变）；调用方提供存储空间，不能为 NULL，写回时由调用方按双
 *                写规则递增 sequence 后落盘。
 * @param outBootSlot 输出本应启动的槽索引；不能为 NULL。
 * @param outWriteBack 输出是否需要把 outMeta 写回元数据块（两条规则都要求
 *                     写回，恒为 true；保留输出以便测试断言与未来规则扩展）；
 *                     不能为 NULL。
 * @return 决策成功返回 XUpgradeError_Ok；参数非法（空指针、魔数/CRC 非法、
 *         槽数越界、activeSlot/requestedSlot 越出 [0,slotCount)）返回
 *         XUpgradeError_InvalidArgument。
 * @note 规则（与 bootloader 团队评审的 §9.3 一致）：
 *       1. bootAttempts >= maxAttempts：置 requestedSlot=activeSlot、
 *          bootAttempts=0，启动旧槽（硬件级自动回滚）；
 *       2. 否则 bootAttempts 自增 1 写回，启动 requestedSlot。
 *       本函数为非 static 符号以便测试 TU 直接 extern 调用（同
 *       XUpgradeVerify_sha256 的无头共享口径）；不属于公开 API，普通调用方
 *       请走 XUpgradeStore_confirmBoot_base/引擎 XUpgrade_confirmBoot。
 */
XUpgradeError XUpgradeStoreAb_bootPolicyStep(const XUpgradeBootMeta* meta, uint32_t slotCount,
                                             XUpgradeBootMeta* outMeta, uint32_t* outBootSlot,
                                             bool* outWriteBack)
{
    if (!meta || !outMeta || !outBootSlot || !outWriteBack) {
        return XUpgradeError_InvalidArgument;
    }
    if (!xupgrade_ab_metaValid(meta)) {
        return XUpgradeError_InvalidArgument;
    }
    if (slotCount < 2u || slotCount > XUPGRADE_AB_SLOT_MAX) {
        return XUpgradeError_InvalidArgument;
    }
    if (meta->m_activeSlot >= slotCount || meta->m_requestedSlot >= slotCount) {
        return XUpgradeError_InvalidArgument;
    }
    if (meta->m_bootAttempts >= meta->m_maxAttempts) {
        /* 规则 2：计数超限→回切旧槽。maxAttempts=0 与 bootAttempts=0 组合会
         * 命中本分支（永不接受新槽），故写侧保证 maxAttempts 非 0（见
         * xupgrade_ab_effectiveMaxAttempts）。 */
        *outMeta = *meta;
        outMeta->m_requestedSlot = meta->m_activeSlot;
        outMeta->m_bootAttempts = 0;
        *outBootSlot = meta->m_activeSlot;
        *outWriteBack = true;
        return XUpgradeError_Ok;
    }
    /* 规则 3：计数自增写回，继续尝试新槽；u32 上界防御（不回绕）。 */
    *outMeta = *meta;
    if (outMeta->m_bootAttempts != 0xFFFFFFFFu) {
        outMeta->m_bootAttempts = meta->m_bootAttempts + 1u;
    }
    *outBootSlot = meta->m_requestedSlot;
    *outWriteBack = true;
    return XUpgradeError_Ok;
}

/**
 * @brief 计算有效的 boot 尝试上限（保留胜出块配置，0 回落契约缺省值）。
 * @param winner 胜出块内存表示；可为 NULL（返回缺省值）。
 * @return 有效 maxAttempts；恒非 0。
 * @note maxAttempts=0 会让 §9.3 规则 2 在 bootAttempts=0 时立即触发回退
 *       （新槽永远无法启动），故 0 视作未配置，回落 XUPGRADE_BOOT_ATTEMPTS_DEFAULT。
 */
static uint32_t xupgrade_ab_effectiveMaxAttempts(const XUpgradeBootMeta* winner)
{
    if (!winner || winner->m_maxAttempts == 0u) {
        return XUPGRADE_BOOT_ATTEMPTS_DEFAULT;
    }
    return winner->m_maxAttempts;
}

/* ========================================================================== */
/*            update/ 工作区槽版本标记（currentVersion 过渡载体）               */
/* ========================================================================== */

/**
 * @brief 计算槽版本标记路径 <root>/update/abslot/slot&lt;N&gt;.ver。
 * @param store A/B 后端对象；不能为 NULL。
 * @param slotIndex 槽索引。
 * @return 新路径字符串（堆上 XString，调用方 XClassDelete 释放）；分配失败返回 NULL。
 */
static XString* xupgrade_ab_markPath(const XUpgradeStoreAb* store, uint32_t slotIndex)
{
    char name[32];
    /* 槽数上限 4，name 缓冲按十进制最长 1 位+前后缀，余量充足。 */
    name[0] = '\0';
    {
        char* p = name;
        const char* s;
        for (s = XUPGRADE_AB_MARK_PREFIX; *s; ++s) *p++ = *s;
        *p++ = (char)('0' + (slotIndex % 10u));
        for (s = XUPGRADE_AB_MARK_SUFFIX; *s; ++s) *p++ = *s;
        *p = '\0';
    }
    return xupgrade_ab_buildPath(store->m_installRoot, XUPGRADE_DIR_UPDATE,
                                 XUPGRADE_AB_MARK_DIR, name);
}

static XString* xupgrade_ab_buildPath(const XString* base, const char* part1,
                                      const char* part2, const char* part3)
{
    return xups_build_path(base, part1, part2, part3, NULL);
}
                                       char* out, uint32_t cap)
{
    size_t begin = 0;
    size_t end = size;
    size_t length;
    size_t i;
    if (!data || !out || cap == 0) return false;
    while (begin < end && (data[begin] == ' ' || data[begin] == '\t'
                           || data[begin] == '\r' || data[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (data[end - 1] == ' ' || data[end - 1] == '\t'
                           || data[end - 1] == '\r' || data[end - 1] == '\n')) {
        --end;
    }
    length = end - begin;
    if (length == 0 || length + 1 > (size_t)cap || length + 1 > XUPGRADE_VERSION_MAX) return false;
    for (i = begin; i < end; ++i) {
        unsigned char c = data[i];
        if (c >= 0x80) return false;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
              || c == '.' || c == '_' || c == '-')) {
            return false;
        }
    }
    memcpy(out, &data[begin], length);
    out[length] = '\0';
    return true;
}

/**
 * @brief 原子写槽版本标记（stageEnd 收尾调用；XSaveFile 临时替换语义）。
 * @param store A/B 后端对象；不能为 NULL。
 * @param slotIndex 槽索引。
 * @param version 版本串；已由调用方校验白名单。
 * @return 成功返回 true；目录创建、内存分配或写盘失败返回 false。
 */
static bool xupgrade_ab_writeMark(const XUpgradeStoreAb* store, uint32_t slotIndex,
                                  const char* version)
{
    XString* dirPath = NULL;
    XString* markPath = NULL;
    XSaveFile* save = NULL;
    size_t versionLen;
    bool result = false;

    /* 1. 按需创建 update/abslot/ 目录（绝对路径递归创建，先例 xup_make_dirs）。 */
    dirPath = xupgrade_ab_buildPath(store->m_installRoot, XUPGRADE_DIR_UPDATE,
                                    XUPGRADE_AB_MARK_DIR, NULL);
    if (!dirPath) return false;
    if (!xupgrade_ab_makeDirs(dirPath)) goto cleanup;

    /* 2. XSaveFile 原子写：内容为版本串+换行（语义同目录后端指针文件）。 */
    markPath = xupgrade_ab_markPath(store, slotIndex);
    if (!markPath) goto cleanup;
    save = XSaveFile_create_2(markPath);
    if (!save) goto cleanup;
    if (!XIODevice_open_base((XIODevice*)save, XIODevice_WriteOnly)) goto cleanup;
    versionLen = xups_cstr_len(version);
    if (XIODevice_write_1((XIODevice*)save, version, (int64_t)versionLen) != (int64_t)versionLen
        || XIODevice_write_1((XIODevice*)save, "\n", 1) != 1) {
        goto cleanup;
    }
    if (!XSaveFile_commit(save)) goto cleanup;
    result = true;

cleanup:
    if (save) XClassDelete(save);
    if (markPath) XClassDelete(markPath);
    if (dirPath) XClassDelete(dirPath);
    return result;
}

/**
 * @brief 丢弃当前 staging 流上下文（失败路径与 deinit 共用）。
 * @param store A/B 后端对象；可为 NULL。
 * @note 已写槽内数据不作回滚（非活动槽本就不可启动，下一次 stageBegin 会重
 *       新擦除覆盖）；只清内存上下文与版本串副本。
 */
static void xupgrade_ab_abortStage(XUpgradeStoreAb* store)
{
    if (!store) return;
    if (store->m_stageVersion) {
        XClassDelete(store->m_stageVersion);
        store->m_stageVersion = NULL;
    }
    store->m_stageActive = false;
    store->m_stageSlot = 0;
    store->m_stageSlotSize = 0;
    store->m_stageOffset = 0;
}

/* ========================================================================== */
/*                        虚函数实现（static，仅供 class_init 注册）            */
/* ========================================================================== */

static void VXUpgradeStoreAb_deinit(XUpgradeStoreAb* store)
{
    if (!store) return;
    xupgrade_ab_abortStage(store);
    if (store->m_stagedVersion) {
        XClassDelete(store->m_stagedVersion);
        store->m_stagedVersion = NULL;
    }
    if (store->m_installRoot) {
        XClassDelete(store->m_installRoot);
        store->m_installRoot = NULL;
    }
    store->m_backend = NULL;
    store->m_stagedValid = false;
    store->m_stagedShaValid = false;
    XClass_Deinit_Parent(XUpgradeStore, (XUpgradeStore*)store);
}

static XUpgradeError VXUpgradeStoreAb_stageBegin(XUpgradeStore* self, const char* componentId,
                                                 const char* version, uint64_t expectedSize,
                                                 const char** errText)
{
    XUpgradeStoreAb* store = (XUpgradeStoreAb*)self;
    XUpgradeBootMeta winner;
    uint32_t winnerIndex = 0;
    uint32_t slotCount = 0;
    uint32_t slot = 0;
    uint64_t slotSize = 0;
    uint64_t eraseLen;
    XUpgradeError err;

    xups_set_err(errText, NULL);
    if (!store || !store->m_installRoot || !store->m_backend) {
        xups_set_err(errText, "store not initialized");
        return XUpgradeError_InvalidArgument;
    }
    /* 组件线仅接受 "image"（§3：镜像目标与 app/fontres 组件互斥）。 */
    if (!xupgrade_ab_cstrEquals(componentId, "image")) {
        xups_set_err(errText, "component not supported by A/B backend");
        return XUpgradeError_NotSupported;
    }
    if (!xups_valid_name(version, XUPGRADE_VERSION_MAX)) {
        xups_set_err(errText, "invalid version");
        return XUpgradeError_InvalidArgument;
    }
    if (store->m_stageActive) {
        xups_set_err(errText, "another staging flow is active");
        return XUpgradeError_Busy;
    }

    /* 1. 槽数预检 + 元数据仲裁（两块皆坏=存储级故障，Io）。 */
    err = xupgrade_ab_querySlotCount(store->m_backend, &slotCount);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "query slot count failed");
        return err;
    }
    err = xupgrade_ab_metaArbitrate(store->m_backend, &winner, &winnerIndex);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "boot metadata both blocks invalid");
        return err;
    }
    if (winner.m_activeSlot >= slotCount) {
        xups_set_err(errText, "active slot out of range");
        return XUpgradeError_Io;
    }

    /* 2. 取非活动槽：优先避开 active 与 requested（requested 为未确认的旧
     *    更新时，新镜像顶掉它属 A/B 语义）；双槽布局即 1-active。 */
    {
        uint32_t i;
        slot = slotCount;   /* 哨兵：未命中 */
        for (i = 0; i < slotCount; ++i) {
            if (i != winner.m_activeSlot && i != winner.m_requestedSlot) {
                slot = i;
                break;
            }
        }
        if (slot >= slotCount) {
            for (i = 0; i < slotCount; ++i) {
                if (i != winner.m_activeSlot) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot >= slotCount) {
            xups_set_err(errText, "no inactive slot available");
            return XUpgradeError_Io;
        }
    }

    /* 3. 槽容量预检（expectedSize=manifest 组件 m_size；0 表示未知）。 */
    err = xupgrade_ab_querySlotSize(store->m_backend, slot, &slotSize);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "query slot size failed");
        return err;
    }
    if (expectedSize > 0 && expectedSize > slotSize) {
        xups_set_err(errText, "image exceeds slot capacity");
        return XUpgradeError_Io;
    }

    /* 4. 预擦除写入区间（写前擦除是 AbStore 的编排责任；expectedSize 未知
     *    时整槽擦除）。 */
    eraseLen = (expectedSize > 0) ? expectedSize : slotSize;
    if (!store->m_backend->m_erase) {
        xups_set_err(errText, "erase primitive not implemented");
        return XUpgradeError_NotSupported;
    }
    if (store->m_backend->m_erase(store->m_backend->m_userData, slot, 0,
                                  (uint32_t)eraseLen) != XUpgradeError_Ok) {
        xups_set_err(errText, "erase slot failed");
        return XUpgradeError_Io;
    }

    /* 5. 记录 staging 上下文；写入槽=非活动槽（直写，不落临时文件）。新一次
     *    staging 即将覆写非活动槽，先前完成的 staging 记录随之作废（同一槽
     *    位不能激活半成品）。 */
    store->m_stageVersion = XString_create_utf8(version);
    if (!store->m_stageVersion) {
        xups_set_err(errText, "out of memory");
        return XUpgradeError_Io;
    }
    store->m_stagedValid = false;
    store->m_stagedShaValid = false;
    store->m_stageActive = true;
    store->m_stageSlot = slot;
    store->m_stageSlotSize = slotSize;
    store->m_stageOffset = 0;
    return XUpgradeError_Ok;
}

static XUpgradeError VXUpgradeStoreAb_stageWrite(XUpgradeStore* self, const void* data, uint32_t len)
{
    XUpgradeStoreAb* store = (XUpgradeStoreAb*)self;

    if (!store || !store->m_installRoot || !store->m_backend) return XUpgradeError_InvalidArgument;
    if (!store->m_stageActive) return XUpgradeError_NotReady;
    if (len == 0) return XUpgradeError_Ok;
    if (!data) return XUpgradeError_InvalidArgument;
    if (!store->m_backend->m_write) return XUpgradeError_NotSupported;

    /* 槽内越界防护（stageBegin 已按 expectedSize 预检；未知容量时在此兜底）。
     * 写失败按契约 staging 流作废，需重新 stageBegin。 */
    if (store->m_stageOffset + (uint64_t)len > store->m_stageSlotSize
        || store->m_stageOffset + (uint64_t)len < store->m_stageOffset) {
        xupgrade_ab_abortStage(store);
        return XUpgradeError_Io;
    }
    if (store->m_backend->m_write(store->m_backend->m_userData, store->m_stageSlot,
                                  store->m_stageOffset, (const uint8_t*)data, len)
        != XUpgradeError_Ok) {
        xupgrade_ab_abortStage(store);
        return XUpgradeError_Io;
    }
    store->m_stageOffset += (uint64_t)len;
    return XUpgradeError_Ok;
}

#if XUPGRADE_VERIFY_ON
/**
 * @brief 对已写区间整槽回读并计算 SHA-256（stageEnd 的后端级校验步骤）。
 * @param store A/B 后端对象；不能为 NULL。
 * @param slot 槽索引。
 * @param total 已写入字节数。
 * @param outDigest 输出摘要；调用方提供，容量不小于 XUPGRADE_SHA256_BIN_SIZE。
 * @return 成功返回 XUpgradeError_Ok；哈希上下文创建失败、回读失败或流式计
 *         算失败返回 XUpgradeError_Io。
 * @note 分块回读（XUPGRADE_AB_IO_CHUNK 步长），内存占用固定；摘要随
 *       activate 写入元数据块（bootloader 可选校验）。
 *       XUPGRADE_VERIFY_ON=0 时本函数不被调用（无整槽校验，摘要记零）。
 */
static XUpgradeError xupgrade_ab_readbackSha(const XUpgradeStoreAb* store, uint32_t slot,
                                             uint64_t total, uint8_t* outDigest)
{
    XUpgradeVerifySha256* ctx = NULL;
    uint8_t* buf = NULL;
    uint64_t done = 0;
    XUpgradeError result = XUpgradeError_Ok;

    ctx = XUpgradeVerify_sha256Create();
    buf = (uint8_t*)XMemory_malloc(XUPGRADE_AB_IO_CHUNK, store->m_memory);
    if (!ctx || !buf) {
        result = XUpgradeError_Io;
        goto cleanup;
    }
    if (!XUpgradeVerify_sha256Begin(ctx)) {
        result = XUpgradeError_Io;
        goto cleanup;
    }
    while (done < total) {
        uint64_t remain = total - done;
        uint32_t want = (remain > (uint64_t)XUPGRADE_AB_IO_CHUNK)
                        ? XUPGRADE_AB_IO_CHUNK : (uint32_t)remain;
        if (store->m_backend->m_read(store->m_backend->m_userData, slot, done, buf, want)
            != XUpgradeError_Ok) {
            result = XUpgradeError_Io;
            goto cleanup;
        }
        if (!XUpgradeVerify_sha256Update(ctx, buf, want)) {
            result = XUpgradeError_Io;
            goto cleanup;
        }
        done += want;
    }
    if (!XUpgradeVerify_sha256End(ctx, outDigest)) {
        result = XUpgradeError_Io;
        goto cleanup;
    }

cleanup:
    if (buf) XMemory_free(buf, store->m_memory);
    if (ctx) XUpgradeVerify_sha256Delete(ctx);
    return result;
}
#endif /* XUPGRADE_VERIFY_ON */

static XUpgradeError VXUpgradeStoreAb_stageEnd(XUpgradeStore* self, const char** errText)
{
    XUpgradeStoreAb* store = (XUpgradeStoreAb*)self;
    bool shaValid = false;
    uint8_t readback[XUPGRADE_SHA256_BIN_SIZE];
    XUpgradeError result = XUpgradeError_Ok;

    xups_set_err(errText, NULL);
    if (!store || !store->m_installRoot || !store->m_backend) {
        xups_set_err(errText, "store not initialized");
        return XUpgradeError_InvalidArgument;
    }
    if (!store->m_stageActive || !store->m_stageVersion) {
        xups_set_err(errText, "no active staging flow");
        return XUpgradeError_NotReady;
    }
#if XUPGRADE_VERIFY_ON
    /* 后端级校验：整槽回读哈希（可配默认开）；失败=staging 流作废。 */
    memset(readback, 0, sizeof(readback));
    result = xupgrade_ab_readbackSha(store, store->m_stageSlot,
                                     store->m_stageOffset, readback);
    if (result != XUpgradeError_Ok) {
        xups_set_err(errText, "slot read-back hash failed");
        xupgrade_ab_abortStage(store);
        return result;
    }
    shaValid = true;
#else
    memset(readback, 0, sizeof(readback));
#endif /* XUPGRADE_VERIFY_ON */

    /* 记录最近一次完成的 staging（activate 的依据）：槽位、回读摘要与版本
     * 串（版本串所有权自 staging 流移交给 staged 记录）。 */
    store->m_stagedValid = true;
    store->m_stagedSlot = store->m_stageSlot;
    store->m_stagedShaValid = shaValid;
    memcpy(store->m_stagedSha, readback, XUPGRADE_SHA256_BIN_SIZE);
    if (store->m_stagedVersion) {
        XClassDelete(store->m_stagedVersion);
    }
    store->m_stagedVersion = store->m_stageVersion;
    store->m_stageVersion = NULL;
    store->m_stageActive = false;

    /* 槽版本标记原子写入 update/ 工作区（currentVersion 的过渡载体）；
     * 写失败则撤销 staged 记录（该槽不能被激活路径引用）。 */
    if (!xupgrade_ab_writeMark(store, store->m_stagedSlot,
                               XString_toUtf8(store->m_stagedVersion))) {
        store->m_stagedValid = false;
        store->m_stagedShaValid = false;
        xups_set_err(errText, "write slot version mark failed");
        return XUpgradeError_Io;
    }
    xups_set_err(errText, NULL);
    return XUpgradeError_Ok;
}

static XUpgradeError VXUpgradeStoreAb_activate(XUpgradeStore* self, const char* componentId,
                                               const char* version, const char** errText)
{
    XUpgradeStoreAb* store = (XUpgradeStoreAb*)self;
    XUpgradeBootMeta winner;
    XUpgradeBootMeta newState;
    uint32_t winnerIndex = 0;
    uint32_t slotCount = 0;
    XUpgradeError err;

    xups_set_err(errText, NULL);
    if (!store || !store->m_installRoot || !store->m_backend) {
        xups_set_err(errText, "store not initialized");
        return XUpgradeError_InvalidArgument;
    }
    if (!xupgrade_ab_cstrEquals(componentId, "image")) {
        xups_set_err(errText, "component not supported by A/B backend");
        return XUpgradeError_NotSupported;
    }
    if (!xups_valid_name(version, XUPGRADE_VERSION_MAX)) {
        xups_set_err(errText, "invalid version");
        return XUpgradeError_InvalidArgument;
    }
    /* activate 要求该版本已 staging 完成（契约）。 */
    if (!store->m_stagedValid || !store->m_stagedVersion
        || !XString_equals_utf8(store->m_stagedVersion, version, XChar_CaseSensitive)) {
        xups_set_err(errText, "no staged image for this version");
        return XUpgradeError_NotReady;
    }

    err = xupgrade_ab_querySlotCount(store->m_backend, &slotCount);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "query slot count failed");
        return err;
    }
    err = xupgrade_ab_metaArbitrate(store->m_backend, &winner, &winnerIndex);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "boot metadata both blocks invalid");
        return err;
    }
    if (winner.m_activeSlot >= slotCount || store->m_stagedSlot >= slotCount) {
        xups_set_err(errText, "slot index out of range");
        return XUpgradeError_Io;
    }

    /* 双写：requestedSlot=新槽、bootAttempts=0、sequence+1；activeSlot 保持
     * 不变（confirmBoot 才收敛），从不覆写 activeSlot 指向槽的镜像数据。 */
    xupgrade_ab_metaBumpSequence(&winner, &newState);
    newState.m_activeSlot = winner.m_activeSlot;
    newState.m_requestedSlot = store->m_stagedSlot;
    newState.m_bootAttempts = 0;
    newState.m_maxAttempts = xupgrade_ab_effectiveMaxAttempts(&winner);
    if (store->m_stagedShaValid) {
        memcpy(newState.m_imageSha256, store->m_stagedSha, XUPGRADE_SHA256_BIN_SIZE);
    } else {
        memset(newState.m_imageSha256, 0, XUPGRADE_SHA256_BIN_SIZE);
    }
    xupgrade_ab_metaUpdateCrc(&newState);
    err = xupgrade_ab_metaWriteBoth(store->m_backend, winnerIndex, &newState);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "write boot metadata failed");
        return err;
    }

    /* §9.4：元数据切换完成后经 XSystem_reboot 抽象进 bootloader（宿主模拟
     * 经 XSystem_setRebootHandler 注入回调，不真重启）。请求被拒不回滚元数
     * 据：requestedSlot 已持久化，重启时机由宿主/看门狗决定，下次上电
     * bootloader 仍按新元数据启动。 */
    (void)XSystem_reboot(XSystemRebootMode_Bootloader);

    /* staging 结果已消费：同一版本不可重复 activate（需重新 staging）。 */
    store->m_stagedValid = false;
    store->m_stagedShaValid = false;
    if (store->m_stagedVersion) {
        XClassDelete(store->m_stagedVersion);
        store->m_stagedVersion = NULL;
    }
    xups_set_err(errText, NULL);
    return XUpgradeError_Ok;
}

static bool VXUpgradeStoreAb_currentVersion(XUpgradeStore* self, const char* componentId,
                                            char* out, uint32_t cap)
{
    XUpgradeStoreAb* store = (XUpgradeStoreAb*)self;
    XUpgradeBootMeta winner;
    uint32_t winnerIndex = 0;
    uint32_t slotCount = 0;
    XString* markPath = NULL;
    XFile* file = NULL;
    XByteArray* content = NULL;
    char version[XUPGRADE_VERSION_MAX];
    bool result = false;

    if (!store || !store->m_installRoot || !store->m_backend || cap == 0) return false;
    /* 组件线仅接受 "image"（与 stageBegin/activate 同口径）。 */
    if (!xupgrade_ab_cstrEquals(componentId, "image")) return false;
    if (xupgrade_ab_querySlotCount(store->m_backend, &slotCount) != XUpgradeError_Ok) return false;
    /* 读失败/内容非法视作未激活（设计文档 §2），调用方据此走启动自检回滚。 */
    if (xupgrade_ab_metaArbitrate(store->m_backend, &winner, &winnerIndex) != XUpgradeError_Ok) {
        return false;
    }
    if (winner.m_activeSlot >= slotCount) return false;

    markPath = xupgrade_ab_markPath(store, winner.m_activeSlot);
    file = markPath ? XFile_create_2(markPath) : NULL;
    if (file && XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        content = XIODevice_readAll_3((XIODevice*)file);
        XIODevice_close_base((XIODevice*)file);
    }
    if (file) XClassDelete(file);
    if (markPath) XClassDelete(markPath);
    if (!content) return false;
    if (!xups_extract_version(XByteArray_data(content),
                                    (size_t)XContainer_size_base((const XContainer*)content),
                                    version, (uint32_t)sizeof(version))) {
        XClassDelete(content);
        return false;
    }
    XClassDelete(content);

    if ((uint32_t)xups_cstr_len(version) + 1 > cap) return false;
    {
        uint32_t i;
        for (i = 0; version[i]; ++i) out[i] = version[i];
        out[i] = '\0';
    }
    result = true;
    return result;
}

static XUpgradeError VXUpgradeStoreAb_rollback(XUpgradeStore* self, const char* componentId,
                                               const char* version, const char** errText)
{
    XUpgradeStoreAb* store = (XUpgradeStoreAb*)self;
    XUpgradeBootMeta winner;
    XUpgradeBootMeta newState;
    uint32_t winnerIndex = 0;
    uint32_t slotCount = 0;
    XUpgradeError err;

    xups_set_err(errText, NULL);
    if (!store || !store->m_installRoot || !store->m_backend) {
        xups_set_err(errText, "store not initialized");
        return XUpgradeError_InvalidArgument;
    }
    if (!xupgrade_ab_cstrEquals(componentId, "image")) {
        xups_set_err(errText, "component not supported by A/B backend");
        return XUpgradeError_NotSupported;
    }
    if (!xups_valid_name(version, XUPGRADE_VERSION_MAX)) {
        xups_set_err(errText, "invalid version");
        return XUpgradeError_InvalidArgument;
    }

    err = xupgrade_ab_querySlotCount(store->m_backend, &slotCount);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "query slot count failed");
        return err;
    }
    err = xupgrade_ab_metaArbitrate(store->m_backend, &winner, &winnerIndex);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "boot metadata both blocks invalid");
        return err;
    }
    if (winner.m_activeSlot >= slotCount) {
        xups_set_err(errText, "active slot out of range");
        return XUpgradeError_Io;
    }

    /* 回滚=requestedSlot 写回 activeSlot 旧槽（旧槽镜像从不覆写，天然完好）；
     * imageSha256 保留胜出块原值（confirmBoot 后它描述的正是 activeSlot 镜
     * 像）。版本参数只做白名单校验，不与槽版本标记强比对：A/B 回滚锚点是
     * activeSlot 槽位本身。 */
    xupgrade_ab_metaBumpSequence(&winner, &newState);
    newState.m_requestedSlot = winner.m_activeSlot;
    newState.m_bootAttempts = 0;
    newState.m_maxAttempts = xupgrade_ab_effectiveMaxAttempts(&winner);
    xupgrade_ab_metaUpdateCrc(&newState);
    err = xupgrade_ab_metaWriteBoth(store->m_backend, winnerIndex, &newState);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "write boot metadata failed");
        return XUpgradeError_Io;
    }
    xups_set_err(errText, NULL);
    return XUpgradeError_Ok;
}

static XUpgradeError VXUpgradeStoreAb_listVersions(XUpgradeStore* self, const char* componentId,
                                                   XUpgradeStoreEmitVersion emit, void* userData)
{
    char version[XUPGRADE_VERSION_MAX];

    if (!self) return XUpgradeError_InvalidArgument;
    if (!emit) return XUpgradeError_InvalidArgument;
    /* A/B 后端仅有 activeSlot 一个可用版本（契约）：枚举至多一条。 */
    if (!VXUpgradeStoreAb_currentVersion(self, componentId, version, (uint32_t)sizeof(version))) {
        return XUpgradeError_Ok;
    }
    return (XUpgradeError)emit(userData, version);
}

static XUpgradeError VXUpgradeStoreAb_confirmBoot(XUpgradeStore* self, const char* componentId,
                                                  const char* version, const char** errText)
{
    XUpgradeStoreAb* store = (XUpgradeStoreAb*)self;
    XUpgradeBootMeta winner;
    XUpgradeBootMeta newState;
    uint32_t winnerIndex = 0;
    uint32_t slotCount = 0;
    XUpgradeError err;

    xups_set_err(errText, NULL);
    if (!store || !store->m_installRoot || !store->m_backend) {
        xups_set_err(errText, "store not initialized");
        return XUpgradeError_InvalidArgument;
    }
    if (!xupgrade_ab_cstrEquals(componentId, "image")) {
        xups_set_err(errText, "component not supported by A/B backend");
        return XUpgradeError_NotSupported;
    }
    if (!xups_valid_name(version, XUPGRADE_VERSION_MAX)) {
        xups_set_err(errText, "invalid version");
        return XUpgradeError_InvalidArgument;
    }

    err = xupgrade_ab_querySlotCount(store->m_backend, &slotCount);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "query slot count failed");
        return err;
    }
    err = xupgrade_ab_metaArbitrate(store->m_backend, &winner, &winnerIndex);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "boot metadata both blocks invalid");
        return err;
    }
    if (winner.m_requestedSlot >= slotCount) {
        xups_set_err(errText, "requested slot out of range");
        return XUpgradeError_Io;
    }

    /* 确认新版本生效：activeSlot=requestedSlot、bootAttempts=0、sequence+1
     * 双写（§9.4）；确认前由 bootloader 计数回退兜底（§9.3）。 */
    xupgrade_ab_metaBumpSequence(&winner, &newState);
    newState.m_activeSlot = winner.m_requestedSlot;
    newState.m_bootAttempts = 0;
    newState.m_maxAttempts = xupgrade_ab_effectiveMaxAttempts(&winner);
    xupgrade_ab_metaUpdateCrc(&newState);
    err = xupgrade_ab_metaWriteBoth(store->m_backend, winnerIndex, &newState);
    if (err != XUpgradeError_Ok) {
        xups_set_err(errText, "write boot metadata failed");
        return XUpgradeError_Io;
    }
    xups_set_err(errText, NULL);
    return XUpgradeError_Ok;
}

/* ========================================================================== */
/*                        虚函数表初始化（标准五步）                            */
/* ========================================================================== */

static XVtable* XUpgradeStoreAb_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XUpgradeStoreAb)
    XVTABLE_INHERIT_XCLASS(XUpgradeStore);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_StageBegin, VXUpgradeStoreAb_stageBegin);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_StageWrite, VXUpgradeStoreAb_stageWrite);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_StageEnd, VXUpgradeStoreAb_stageEnd);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_Activate, VXUpgradeStoreAb_activate);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_CurrentVersion, VXUpgradeStoreAb_currentVersion);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_Rollback, VXUpgradeStoreAb_rollback);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_ListVersions, VXUpgradeStoreAb_listVersions);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_ConfirmBoot, VXUpgradeStoreAb_confirmBoot);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXUpgradeStoreAb_deinit);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造函数                                             */
/* ========================================================================== */

XUpgradeStore* XUpgradeStoreAb_create_ex(XMemoryType memory, const char* installRootUtf8)
{
    XUpgradeStoreAb* store;
    const XUpgradePartitionBackend* backend = g_xupgrade_ab_partitionBackend;
    uint32_t slotCount = 0;

    if (!installRootUtf8 || !installRootUtf8[0]) return NULL;
    if (!backend) return NULL;                       /* 未注册分区原语。 */
    if (xupgrade_ab_querySlotCount(backend, &slotCount) != XUpgradeError_Ok) return NULL;

    store = (XUpgradeStoreAb*)XMemory_calloc(1, sizeof(XUpgradeStoreAb), memory);
    if (!store) return NULL;
    XUpgradeStore_init(&store->m_parent);      /* 基类 init：XObject + XUpgradeStore 虚表 */
    XClassSetVtable(store, XUpgradeStoreAb);   /* 覆盖为派生类虚表（重载 8 个业务槽+deinit） */
    store->m_installRoot = XString_create_utf8(installRootUtf8);
    if (!store->m_installRoot) {
        XClass_Deinit_Parent(XUpgradeStore, (XUpgradeStore*)store);
        XMemory_free(store, memory);
        return NULL;
    }
    store->m_backend = backend;
    store->m_memory = memory;
    Set_Class_Memory(store, memory);
    Set_Class_IsHeap(store, true);
    return &store->m_parent;
}

/* ========================================================================== */
/*               宿主模拟分区后端（XUPGRADE_AB_SIM_ON，仅测试）                 */
/* ========================================================================== */

#if XUPGRADE_AB_SIM_ON

/**
 * @brief 宿主模拟分区后端的会话状态（installSimBackend 创建/销毁）。
 * @details 四个文件持久化：slot_a/b.img（镜像槽，容量 m_slotSize，擦除态为
 *          0xFF）与 meta_a/b.bin（元数据块，60 字节）。仅初始化阶段安装/卸
 *          载（与 XUpgradeStore_setPartitionBackend 同一线程纪律，不加锁）。
 */
typedef struct XUpgradeAbSimState {
    XString* m_workDir;   /**< 模拟分区持久化目录（UTF-8 内部存储）。 */
    uint32_t m_slotSize;  /**< 单槽容量（字节）。 */
} XUpgradeAbSimState;

/** @brief 模拟后端全局会话；未安装时为 NULL。 */
static XUpgradeAbSimState* g_xupgrade_ab_simState = NULL;
/** @brief 模拟后端回调集（安装时把 m_userData 指向会话状态）。 */
static XUpgradePartitionBackend g_xupgrade_ab_simBackend;
/** @brief 进程退出兜底钩子是否已注册。 */
static bool g_xupgrade_ab_simAtExitRegistered = false;

/** @brief 释放模拟会话内存（卸载/重装/装机失败/退出兜底共用）。 */
static void xupgrade_ab_simState_free(void)
{
    if (g_xupgrade_ab_simState) {
        XClassDelete(g_xupgrade_ab_simState->m_workDir);
        XMemory_free(g_xupgrade_ab_simState, XMEMORY_TYPE_SYSTEM);
        g_xupgrade_ab_simState = NULL;
    }
}

/* 进程退出兜底（对齐 XGpuRenderBackend 的 atexit 惯例）：会话为进程期
 * 全局，唯一显式释放入口是 installSimBackend(NULL)；调用方漏调时由退出
 * 钩子释放，不再驻留至进程结束。不触碰后端登记（进程退出期无并发）。 */
static void xupgrade_ab_simState_atexit(void)
{
    xupgrade_ab_simState_free();
    g_xupgrade_ab_simBackend.m_userData = NULL;
}

/**
 * @brief 拼装模拟文件路径 &lt;workDir&gt;/&lt;name&gt;。
 * @return 新路径字符串（堆上 XString，调用方 XClassDelete 释放）；分配失败返回 NULL。
 */
static XString* xupgrade_ab_simFilePath(const char* name)
{
    if (!g_xupgrade_ab_simState || !g_xupgrade_ab_simState->m_workDir) return NULL;
    return xupgrade_ab_buildPath(g_xupgrade_ab_simState->m_workDir, name, NULL, NULL);
}

/**
 * @brief 槽索引对应的模拟槽/元数据文件名。
 * @param slotIndex 槽索引（0=A、1=B）。
 * @param meta 传 true 取元数据块文件名，false 取镜像槽文件名。
 * @return 静态文件名；索引越界返回 NULL。
 */
static const char* xupgrade_ab_simFileName(uint32_t slotIndex, bool meta)
{
    if (meta) {
        if (slotIndex == 0) return XUPGRADE_AB_SIM_META_A;
        if (slotIndex == 1) return XUPGRADE_AB_SIM_META_B;
        return NULL;
    }
    if (slotIndex == 0) return XUPGRADE_AB_SIM_SLOT_A;
    if (slotIndex == 1) return XUPGRADE_AB_SIM_SLOT_B;
    return NULL;
}

static XUpgradeError xupgrade_ab_sim_slotCount(void* userData, uint32_t* outCount)
{
    (void)userData;
    if (!outCount) return XUpgradeError_InvalidArgument;
    *outCount = 2;
    return XUpgradeError_Ok;
}

static XUpgradeError xupgrade_ab_sim_slotSize(void* userData, uint32_t slotIndex,
                                              uint64_t* outSize)
{
    XUpgradeAbSimState* state = (XUpgradeAbSimState*)userData;
    if (!state || !outSize) return XUpgradeError_InvalidArgument;
    if (slotIndex >= 2u) return XUpgradeError_InvalidArgument;
    *outSize = state->m_slotSize;
    return XUpgradeError_Ok;
}

static XUpgradeError xupgrade_ab_sim_read(void* userData, uint32_t slotIndex, uint64_t offset,
                                          uint8_t* out, uint32_t len)
{
    XUpgradeAbSimState* state = (XUpgradeAbSimState*)userData;
    const char* name;
    XString* path = NULL;
    XFile* file = NULL;
    uint32_t got = 0;

    if (!state) return XUpgradeError_InvalidArgument;
    if (!out && len != 0) return XUpgradeError_InvalidArgument;
    name = xupgrade_ab_simFileName(slotIndex, false);
    if (!name) return XUpgradeError_InvalidArgument;
    if (len == 0) return XUpgradeError_Ok;

    /* 文件缺失=擦除态（全 0xFF）；存在但打开失败才是 Io。 */
    path = xupgrade_ab_simFilePath(name);
    if (!path) return XUpgradeError_Io;
    memset(out, XUPGRADE_AB_FLASH_ERASED, len);
    if (XFileInfo_exists_static(path)) {
        file = XFile_create_2(path);
        if (!file || !XFile_open_2(file, XIODevice_ReadOnly, 0)) {
            XClassDelete(path);
            if (file) XClassDelete(file);
            return XUpgradeError_Io;
        }
        if (XIODevice_seek_base((XIODevice*)file, (int64_t)offset)) {
            int64_t n = XIODevice_read_1((XIODevice*)file, (char*)out, (int64_t)len);
            if (n > 0) got = (uint32_t)n;
        }
        XIODevice_close_base((XIODevice*)file);
        XClassDelete(file);
    }
    /* 截断/越界尾部按擦除态补 0xFF（文件语义与 flash 对齐）。 */
    if (got < len) {
        memset(out + got, XUPGRADE_AB_FLASH_ERASED, len - got);
    }
    XClassDelete(path);
    return XUpgradeError_Ok;
}

static XUpgradeError xupgrade_ab_sim_write(void* userData, uint32_t slotIndex, uint64_t offset,
                                           const uint8_t* data, uint32_t len)
{
    XUpgradeAbSimState* state = (XUpgradeAbSimState*)userData;
    const char* name;
    XString* path = NULL;
    XFile* file = NULL;
    bool opened = false;
    int64_t written;
    XUpgradeError result = XUpgradeError_Ok;

    if (!state) return XUpgradeError_InvalidArgument;
    if (!data && len != 0) return XUpgradeError_InvalidArgument;
    name = xupgrade_ab_simFileName(slotIndex, false);
    if (!name) return XUpgradeError_InvalidArgument;
    if (len == 0) return XUpgradeError_Ok;

    path = xupgrade_ab_simFilePath(name);
    if (!path) return XUpgradeError_Io;
    file = XFile_create_2(path);
    if (!file || !XFile_open_2(file, XIODevice_ReadWrite, 0)) {
        result = XUpgradeError_Io;
        goto cleanup;
    }
    opened = true;
    /* 追加区先扩展（模拟扇区写放大），再定位写入。 */
    if (XIODevice_size_base((XIODevice*)file) < (int64_t)(offset + (uint64_t)len)) {
        if (!XFile_resize_base((XFileDevice*)file, (int64_t)(offset + (uint64_t)len))) {
            result = XUpgradeError_Io;
            goto cleanup;
        }
    }
    if (!XIODevice_seek_base((XIODevice*)file, (int64_t)offset)) {
        result = XUpgradeError_Io;
        goto cleanup;
    }
    written = XIODevice_write_1((XIODevice*)file, (const char*)data, (int64_t)len);
    if (written != (int64_t)len) {
        result = XUpgradeError_Io;
        goto cleanup;
    }

cleanup:
    if (file) {
        if (opened) XIODevice_close_base((XIODevice*)file);
        XClassDelete(file);
    }
    if (path) XClassDelete(path);
    return result;
}

static XUpgradeError xupgrade_ab_sim_erase(void* userData, uint32_t slotIndex, uint64_t offset,
                                           uint32_t len)
{
    XUpgradeAbSimState* state = (XUpgradeAbSimState*)userData;
    if (!state) return XUpgradeError_InvalidArgument;
    if (slotIndex >= 2u) return XUpgradeError_InvalidArgument;
    if (offset + (uint64_t)len < offset) return XUpgradeError_InvalidArgument;
    if ((uint64_t)state->m_slotSize > 0 && offset + (uint64_t)len > (uint64_t)state->m_slotSize) {
        return XUpgradeError_InvalidArgument;
    }
    /* 宿主文件无擦除概念：erase 直接返回 Ok（契约）。 */
    (void)offset;
    (void)len;
    return XUpgradeError_Ok;
}

static XUpgradeError xupgrade_ab_sim_metaRead(void* userData, uint32_t metaIndex,
                                              XUpgradeBootMeta* out)
{
    XUpgradeAbSimState* state = (XUpgradeAbSimState*)userData;
    const char* name;
    XString* path = NULL;
    XFile* file = NULL;
    uint8_t buf[XUPGRADE_BOOT_META_SIZE];
    size_t got = 0;

    if (!state) return XUpgradeError_InvalidArgument;
    if (!out) return XUpgradeError_InvalidArgument;
    name = xupgrade_ab_simFileName(metaIndex, true);
    if (!name) return XUpgradeError_InvalidArgument;

    memset(buf, XUPGRADE_AB_FLASH_ERASED, sizeof(buf));
    path = xupgrade_ab_simFilePath(name);
    if (!path) return XUpgradeError_Io;
    if (XFileInfo_exists_static(path)) {
        file = XFile_create_2(path);
        if (!file || !XFile_open_2(file, XIODevice_ReadOnly, 0)) {
            XClassDelete(path);
            if (file) XClassDelete(file);
            return XUpgradeError_Io;
        }
        {
            int64_t n = XIODevice_read_1((XIODevice*)file, (char*)buf, (int64_t)sizeof(buf));
            if (n > 0) got = (size_t)n;
        }
        XIODevice_close_base((XIODevice*)file);
        XClassDelete(file);
    }
    if (got < sizeof(buf)) {
        memset(buf + got, XUPGRADE_AB_FLASH_ERASED, sizeof(buf) - got);
    }
    (void)xupgrade_ab_metaDeserialize(buf, out);
    XClassDelete(path);
    return XUpgradeError_Ok;
}

static XUpgradeError xupgrade_ab_sim_metaWrite(void* userData, uint32_t metaIndex,
                                               const XUpgradeBootMeta* meta)
{
    XUpgradeAbSimState* state = (XUpgradeAbSimState*)userData;
    const char* name;
    XString* path = NULL;
    XFile* file = NULL;
    uint8_t buf[XUPGRADE_BOOT_META_SIZE];
    int64_t written;

    if (!state) return XUpgradeError_InvalidArgument;
    if (!meta) return XUpgradeError_InvalidArgument;
    name = xupgrade_ab_simFileName(metaIndex, true);
    if (!name) return XUpgradeError_InvalidArgument;

    /* 原样覆写（契约）：先擦后写语义由 60 字节整块截断写表达。 */
    xupgrade_ab_metaSerialize(meta, buf);
    path = xupgrade_ab_simFilePath(name);
    if (!path) return XUpgradeError_Io;
    file = XFile_create_2(path);
    if (!file || !XFile_open_2(file, (XIODeviceBaseMode)(XIODevice_WriteOnly | XIODevice_Truncate), 0)) {
        XClassDelete(path);
        if (file) XClassDelete(file);
        return XUpgradeError_Io;
    }
    written = XIODevice_write_1((XIODevice*)file, (const char*)buf, (int64_t)sizeof(buf));
    XIODevice_close_base((XIODevice*)file);
    XClassDelete(file);
    XClassDelete(path);
    if (written != (int64_t)sizeof(buf)) return XUpgradeError_Io;
    return XUpgradeError_Ok;
}

/**
 * @brief 确保模拟槽文件存在且为整槽擦除态（装机时调用；已存在则不动）。
 * @param slotIndex 槽索引。
 * @return 就绪返回 true；创建/扩容失败返回 false。
 */
static bool xupgrade_ab_simEnsureSlotFile(uint32_t slotIndex)
{
    const char* name = xupgrade_ab_simFileName(slotIndex, false);
    XString* path = NULL;
    XFile* file = NULL;
    bool opened = false;
    bool ok = false;
    uint8_t zero[XUPGRADE_AB_IO_CHUNK];

    if (!name) return false;
    path = xupgrade_ab_simFilePath(name);
    if (!path) return false;
    if (XFileInfo_exists_static(path)) {
        ok = true;              /* 已有槽文件（保留既有内容/注入的故障态）。 */
        goto cleanup;
    }
    file = XFile_create_2(path);
    if (!file) goto cleanup;
    if (!XFile_open_2(file, (XIODeviceBaseMode)(XIODevice_WriteOnly | XIODevice_Truncate), 0)) {
        goto cleanup;
    }
    opened = true;
    memset(zero, XUPGRADE_AB_FLASH_ERASED, sizeof(zero));
    {
        uint64_t remain = g_xupgrade_ab_simState->m_slotSize;
        while (remain > 0) {
            uint32_t chunk = (remain > (uint64_t)sizeof(zero))
                             ? (uint32_t)sizeof(zero) : (uint32_t)remain;
            if (XIODevice_write_1((XIODevice*)file, (const char*)zero, (int64_t)chunk)
                != (int64_t)chunk) {
                goto cleanup;
            }
            remain -= chunk;
        }
    }
    ok = true;

cleanup:
    if (file) {
        if (opened) XIODevice_close_base((XIODevice*)file);
        XClassDelete(file);
    }
    if (path) XClassDelete(path);
    return ok;
}

/**
 * @brief 装机/首次使用时补齐出厂元数据（缺文件才写；不覆盖已有状态）。
 * @return 双块就绪返回 true；写盘失败返回 false。
 */
static bool xupgrade_ab_simEnsureFactoryMeta(void)
{
    uint32_t i;
    XUpgradeBootMeta factory;
    for (i = 0; i < XUPGRADE_BOOT_META_COPIES; ++i) {
        const char* name = xupgrade_ab_simFileName(i, true);
        XString* path = name ? xupgrade_ab_simFilePath(name) : NULL;
        bool exists = path && XFileInfo_exists_static(path);
        if (path) XClassDelete(path);
        if (exists) continue;
        xupgrade_ab_metaInitFactory(&factory);
        if (xupgrade_ab_sim_metaWrite(g_xupgrade_ab_simState, i, &factory) != XUpgradeError_Ok) {
            return false;
        }
    }
    return true;
}

bool XUpgradeStoreAb_installSimBackend(const char* workDirUtf8, uint32_t slotSizeBytes)
{
    /* 卸载：仅当当前注册是模拟后端时清除；真实 Drive 注册不受影响。传 NULL
     * 且无模拟后端时为幂等空操作，同样返回 true。 */
    if (!workDirUtf8) {
        if (g_xupgrade_ab_partitionIsSim) {
            g_xupgrade_ab_partitionBackend = NULL;
            g_xupgrade_ab_partitionIsSim = false;
        }
        if (g_xupgrade_ab_simState) {
            xupgrade_ab_simState_free();
        }
        g_xupgrade_ab_simBackend.m_userData = NULL;
        return true;
    }

    /* 与显式 Drive 注册互斥（契约）；重复安装模拟后端=切换目录（先卸后装）。 */
    if (g_xupgrade_ab_partitionBackend && !g_xupgrade_ab_partitionIsSim) {
        return false;
    }
    if (slotSizeBytes != 0 && slotSizeBytes < XUPGRADE_AB_SIM_SLOT_MIN) {
        return false;
    }
    /* 卸载旧会话（文件保留：模拟分区持久化）。 */
    if (g_xupgrade_ab_simState) {
        xupgrade_ab_simState_free();
        g_xupgrade_ab_simBackend.m_userData = NULL;
    }

    {
        XUpgradeAbSimState* state =
            (XUpgradeAbSimState*)XMemory_calloc(1, sizeof(XUpgradeAbSimState), XMEMORY_TYPE_SYSTEM);
        XString* workDir;
        if (!state) return false;
        workDir = XString_create_utf8(workDirUtf8);
        if (!workDir || !workDirUtf8[0]) {
            if (workDir) XClassDelete(workDir);
            XMemory_free(state, XMEMORY_TYPE_SYSTEM);
            return false;
        }
        state->m_workDir = workDir;
        state->m_slotSize = (slotSizeBytes != 0) ? slotSizeBytes : XUPGRADE_AB_SIM_SLOT_DEFAULT;
        g_xupgrade_ab_simState = state;
        g_xupgrade_ab_simBackend.m_userData = state;
        if (!g_xupgrade_ab_simAtExitRegistered) {
            if (atexit(xupgrade_ab_simState_atexit) == 0)
                g_xupgrade_ab_simAtExitRegistered = true;
        }
    }

    /* 建目录 + 四文件（缺才建；槽文件铺擦除态，元数据写出厂块）。 */
    if (!xupgrade_ab_makeDirs(g_xupgrade_ab_simState->m_workDir)
        || !xupgrade_ab_simEnsureSlotFile(0)
        || !xupgrade_ab_simEnsureSlotFile(1)
        || !xupgrade_ab_simEnsureFactoryMeta()) {
        xupgrade_ab_simState_free();
        g_xupgrade_ab_simBackend.m_userData = NULL;
        return false;
    }

    g_xupgrade_ab_simBackend.m_slotCount = xupgrade_ab_sim_slotCount;
    g_xupgrade_ab_simBackend.m_slotSize = xupgrade_ab_sim_slotSize;
    g_xupgrade_ab_simBackend.m_read = xupgrade_ab_sim_read;
    g_xupgrade_ab_simBackend.m_write = xupgrade_ab_sim_write;
    g_xupgrade_ab_simBackend.m_erase = xupgrade_ab_sim_erase;
    g_xupgrade_ab_simBackend.m_metaRead = xupgrade_ab_sim_metaRead;
    g_xupgrade_ab_simBackend.m_metaWrite = xupgrade_ab_sim_metaWrite;
    g_xupgrade_ab_partitionBackend = &g_xupgrade_ab_simBackend;
    g_xupgrade_ab_partitionIsSim = true;
    return true;
}

#else /* !XUPGRADE_AB_SIM_ON */

bool XUpgradeStoreAb_installSimBackend(const char* workDirUtf8, uint32_t slotSizeBytes)
{
    /* 生产固件裁剪（XUPGRADE_AB_SIM_ON=0）：模拟后端不存在，恒失败。 */
    (void)workDirUtf8;
    (void)slotSizeBytes;
    return false;
}

#endif /* XUPGRADE_AB_SIM_ON */

#endif /* XUPGRADE_AB_ON */
#endif /* XUPGRADE_ON */
