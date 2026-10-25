/******************************************************************************
 * @file       XPlatformFramebuffer_drm.c
 * @brief      Linux DRM/KMS 显示驱动实现（dumb buffer + mmap 直写 +
 *             WAIT_VBLANK 垂直同步，无 fbdev 环境的正式后端）。
 * @details    实现 XPlatformDisplayDriver 契约（Src/XGui/Platform/
 *             XPlatformDisplayDriver.h）的 DRM/KMS 端，六钩子对应：
 *             - probe           ：open(XPLATFORM_DRM_DEVICE, O_RDWR) ->
 *                                 MODE_GETRESOURCES（CRTC/连接器清单）->
 *                                 逐连接器 MODE_GETCONNECTOR 取
 *                                 connection==DRM_MODE_CONNECTED 且有
 *                                 模式者 -> MODE_GETENCODER 取
 *                                 possible_crtcs -> MODE_CREATE_DUMB
 *                                 （16bpp 优先：与 RGB565 面板直写链
 *                                 XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
 *                                 零转换；SETCRTC 失败退 32bpp XRGB8888）
 *                                 -> MODE_ADDFB -> MODE_SETCRTC（进程
 *                                 open 卡即成为 DRM master，嵌入式无
 *                                 合成器环境恒可获取）-> MODE_MAP_DUMB +
 *                                 mmap；无 /dev/dri 的环境 open 即失败，
 *                                 恒定返回不可用（预期路径）；
 *             - formatNegotiate ：按 dumb buffer 建立的扫描格式映射
 *                                 XImageFormat（RGB565 -> RGB16，
 *                                 XRGB8888 -> RGB32），语义同 fbdev 后端；
 *             - pan             ：单 dumb buffer 直写即可见，仅接受 0 号
 *                                 缓冲（双缓冲页翻转需 PAGE_FLIP 事件泵
 *                                 + vblank 事件上下文，后续批落地；
 *                                 fbdev 模板的 yoffset 翻页模型在 KMS
 *                                 上不存在）；
 *             - cacheSync       ：同 fbdev 模板口径——ARM Linux 经私有
 *                                 cacheflush 系统调用（__ARM_NR_
 *                                 cacheflush）清 DCache；Rockchip dumb
 *                                 buffer 映射通常 write-combine（不需
 *                                 要同步），失败不谎报（返回 false）；
 *             - waitVsync       ：DRM_IOCTL_WAIT_VBLANK（RELATIVE 1 帧，
 *                                 真硬件垂直同步阻塞等待）；不支持时
 *                                 1ms 步进限时睡眠并如实返回 false；
 *             - stride          ：面板扫描格式返回 dumb pitch（硬件对齐
 *                                 可能大于 width*bpp）；其余格式回落
 *                                 公共层 XImageFormat_bytesPerLine。
 *             内核 UAPI 结构与 ioctl 号就地声明（对标 fbdev 后端零外链
 *             原则，不经 libdrm；布局以 linux/drm.h + drm/drm_mode.h
 *             6.1 为准，32 位 ARM 无独立布局分叉）。
 * @note       本文件只在「__linux__ && XGUI_ON && XPLATFORM_FBDEV_ON &&
 *             XPLATFORM_DRM_ON」时参与编译（开关默认 0）；注册表符号
 *             （XPlatformDisplayDriver_register）随 fbdev 编译单元落地，
 *             故 FBDEV 开关必须同开（fbdev 后端本体在无 /dev/fb0 的板
 *             上 probe 恒败、不占用资源，与本后端互斥共存——先注册者
 *             生效）。内存体系：驱动全程无堆分配，状态静态 + mmap 设备
 *             映射；单线程主循环设计（与 fbdev/X11 后端同口径），不加锁。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformFramebuffer_drm.h"

#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON && XPLATFORM_DRM_ON

#include <fcntl.h>
#include <linux/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <stdint.h>

#include "XImageFormat.h"
#include "XPlatformNativeWindow.h" /* isAvailable：反向时序防护。 */

/* ==================== 内核 DRM UAPI 就地声明（零外链） ==================== */

#define XPDRM_IOCTL_BASE 'd'
#define XPDRM_IO(nr) _IO(XPDRM_IOCTL_BASE, nr)
#define XPDRM_IOWR(nr, type) _IOWR(XPDRM_IOCTL_BASE, nr, type)

#define XPDRM_IOCTL_MODE_GETRESOURCES XPDRM_IOWR(0xA0, struct xpdrm_mode_card_res)
#define XPDRM_IOCTL_MODE_GETCRTC XPDRM_IOWR(0xA1, struct xpdrm_mode_crtc)
#define XPDRM_IOCTL_MODE_SETCRTC XPDRM_IOWR(0xA2, struct xpdrm_mode_crtc)
#define XPDRM_IOCTL_MODE_GETENCODER XPDRM_IOWR(0xA6, struct xpdrm_mode_get_encoder)
#define XPDRM_IOCTL_MODE_GETCONNECTOR XPDRM_IOWR(0xA7, struct xpdrm_mode_get_connector)
#define XPDRM_IOCTL_MODE_ADDFB XPDRM_IOWR(0xAE, struct xpdrm_mode_fb_cmd)
#define XPDRM_IOCTL_MODE_RMFB XPDRM_IOWR(0xAF, struct xpdrm_mode_fb_cmd)
#define XPDRM_IOCTL_WAIT_VBLANK XPDRM_IOWR(0x3A, union xpdrm_wait_vblank)
#define XPDRM_IOCTL_MODE_CREATE_DUMB XPDRM_IOWR(0xB2, struct xpdrm_mode_create_dumb)
#define XPDRM_IOCTL_MODE_MAP_DUMB XPDRM_IOWR(0xB3, struct xpdrm_mode_map_dumb)
#define XPDRM_IOCTL_MODE_DESTROY_DUMB XPDRM_IOWR(0xB4, struct xpdrm_mode_destroy_dumb)

/** @brief connector.connection：已连接（有面板在位）。 */
#define XPDRM_MODE_CONNECTED 1u
/** @brief mode.type：连接器 EDID 首选模式（无则退 modes[0]）。 */
#define XPDRM_MODE_TYPE_PREFERRED 0x40u
/** @brief vblank request.type：相对当前计数等待 sequence 帧。 */
#define XPDRM_VBLANK_RELATIVE 0x1u

/* 结构布局严格对照 linux/drm.h + drm/drm_mode.h（内核 6.1）：所有字段
 * 自然对齐、64 位指针字段 8 字节对齐，32 位 ARM 无 packing 分叉。 */

struct xpdrm_mode_modeinfo
{
    uint32_t clock;
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
};

struct xpdrm_mode_card_res
{
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
};

struct xpdrm_mode_crtc
{
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct xpdrm_mode_modeinfo mode;
};

/* 编码器结构对照 drm_mode.h（5.x 起 UAPI 精简：name 域已删除）。 */
struct xpdrm_mode_get_encoder
{
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
};

struct xpdrm_mode_get_connector
{
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;     /**< 当前绑定编码器（0=未绑定）。 */
    uint32_t connector_id;
    uint32_t connector_type; /**< DRM_MODE_CONNECTOR_*（5.x 新增域，
                                  位置插在 connector_id 与 connection
                                  之间——按旧布局偏移会把 type 读成
                                  connection，恒判"未连接"。） */
    uint32_t connection;
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t pad1;
};

struct xpdrm_mode_fb_cmd
{
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

struct xpdrm_mode_create_dumb
{
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

struct xpdrm_mode_map_dumb
{
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct xpdrm_mode_destroy_dumb
{
    uint32_t handle;
};

/* vblank 结构对照 drm.h：request.signal/reply.tval_* 为 long——32 位
 * ARM 上即 4 字节（union 共 16 字节；ioctl 编码号含长度位，勿按 64 位
 * 宿主布局硬编）。 */
struct xpdrm_wait_vblank_request
{
    uint32_t type;
    uint32_t sequence;
    uint32_t signal;
};

struct xpdrm_wait_vblank_reply
{
    uint32_t type;
    uint32_t sequence;
    int32_t tval_sec;
    int32_t tval_usec;
};

union xpdrm_wait_vblank
{
    struct xpdrm_wait_vblank_request request;
    struct xpdrm_wait_vblank_reply reply;
};

/* ==================== 进程级驱动状态（静态，无堆分配） ==================== */

#define XPDRM_MAX_CRTCS 16
#define XPDRM_MAX_CONNECTORS 32
#define XPDRM_MAX_ENCODERS 32
#define XPDRM_MAX_MODES 64

static int g_xpdrmFd = -1;                 /**< DRM 卡描述符；<0 未打开。 */
static uint8_t* g_xpdrmMap = NULL;         /**< dumb buffer mmap 地址（拥有）。 */
static size_t g_xpdrmMapSize = 0;          /**< 映射区总长（字节）。 */
static uint32_t g_xpdrmDumbHandle = 0;     /**< dumb buffer 句柄（销毁用）。 */
static uint32_t g_xpdrmFbId = 0;           /**< 已挂 CRTC 的 FB id（拆除用）。 */
static XPlatformDisplayInfo g_xpdrmInfo;   /**< probe 能力快照。 */
static bool g_xpdrmDeviceReady = false;    /**< 设备已探测并映射。 */
static bool g_xpdrmRegistered = false;     /**< ops 表已进注册表。 */

/** @brief 运行期设备路径覆盖（板级经 XPlatformNativeWindow_useDrmDriver
 *  传入；NULL 用编译期宏）。 */
static const char* g_xpdrmDevicePath = NULL;

/** @brief 模式/资源静态暂存区（probe 期间使用，进程级静态零堆分配）。 */
static uint32_t g_xpdrmCrtcIds[XPDRM_MAX_CRTCS];
static uint32_t g_xpdrmConnIds[XPDRM_MAX_CONNECTORS];
static uint32_t g_xpdrmEncIds[XPDRM_MAX_ENCODERS];
static struct xpdrm_mode_modeinfo g_xpdrmModes[XPDRM_MAX_MODES];
static uint32_t g_xpdrmSetConnId;          /**< SETCRTC 绑定的连接器 id。 */
/** @brief GETCONNECTOR 计数轮的单元素模式暂存（count_modes=1 口径，
 *  避免 count_modes=0 触发内核强制重新探测——阻塞且可能闪屏）。 */
static struct xpdrm_mode_modeinfo g_xpdrmProbeMode;

/* 显示驱动注册表：实现在 XPlatformFramebuffer_posix.c（本编译单元经
 * XPlatformDisplayDriver_register 消费，先注册者生效——板级引导先试
 * DRM 后试 fbdev，无 /dev/fb0 的板上恒为 DRM 活动驱动）。 */

/* ==================== dumb buffer 建链（create -> addfb -> setcrtc） ==================== */

/**
 * @brief      按指定位深建 dumb buffer、挂 FB 并接管 CRTC（单步失败整
 *             段回退：munmap/RMFB/destroy dumb，不留半占用状态）。
 * @param      fd          DRM 卡描述符。
 * @param      crtcId      目标 CRTC id。
 * @param      connectorId 目标连接器 id。
 * @param      mode        目标显示模式（取 hdisplay/vdisplay 建缓冲）。
 * @param      bpp         像素位深（16=RGB565 优先；32=XRGB8888 兜底）。
 * @return     true 建链成功（CRTC 已扫描输出本 FB）；false 失败已清理。
 */
static bool xpdrm_createDumbChain(int fd, uint32_t crtcId,
                                  uint32_t connectorId,
                                  const struct xpdrm_mode_modeinfo* mode,
                                  uint32_t bpp)
{
    struct xpdrm_mode_create_dumb create;
    struct xpdrm_mode_fb_cmd fb;
    struct xpdrm_mode_map_dumb mapReq;
    struct xpdrm_mode_crtc crtc;
    void* map;
    memset(&create, 0, sizeof(create));
    create.height = mode->vdisplay;
    create.width = mode->hdisplay;
    create.bpp = bpp;
    if (ioctl(fd, XPDRM_IOCTL_MODE_CREATE_DUMB, &create) != 0)
        return false;
    memset(&fb, 0, sizeof(fb));
    fb.width = mode->hdisplay;
    fb.height = mode->vdisplay;
    fb.pitch = create.pitch;
    fb.bpp = bpp;
    /* depth 语义对齐 drm_mode_fb_cmd 惯例：16bpp=RGB565(depth16)，
     * 32bpp=XRGB8888(depth24+bpp32)。 */
    fb.depth = (bpp == 16) ? 16u : 24u;
    fb.handle = create.handle;
    if (ioctl(fd, XPDRM_IOCTL_MODE_ADDFB, &fb) != 0)
    {
        struct xpdrm_mode_destroy_dumb destroy;
        memset(&destroy, 0, sizeof(destroy));
        destroy.handle = create.handle;
        ioctl(fd, XPDRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        return false;
    }
    memset(&mapReq, 0, sizeof(mapReq));
    mapReq.handle = create.handle;
    if (ioctl(fd, XPDRM_IOCTL_MODE_MAP_DUMB, &mapReq) != 0 ||
        (map = mmap(NULL, (size_t)create.size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, (off_t)mapReq.offset)) == MAP_FAILED)
    {
        ioctl(fd, XPDRM_IOCTL_MODE_RMFB, &fb);
        {
            struct xpdrm_mode_destroy_dumb destroy;
            memset(&destroy, 0, sizeof(destroy));
            destroy.handle = create.handle;
            ioctl(fd, XPDRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
        return false;
    }
    memset(&crtc, 0, sizeof(crtc));
    g_xpdrmSetConnId = connectorId;
    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&g_xpdrmSetConnId;
    crtc.count_connectors = 1;
    crtc.crtc_id = crtcId;
    crtc.fb_id = fb.fb_id;
    crtc.x = 0;
    crtc.y = 0;
    crtc.mode_valid = 1;
    crtc.mode = *mode;
    if (ioctl(fd, XPDRM_IOCTL_MODE_SETCRTC, &crtc) != 0)
    {
        munmap(map, (size_t)create.size);
        ioctl(fd, XPDRM_IOCTL_MODE_RMFB, &fb);
        {
            struct xpdrm_mode_destroy_dumb destroy;
            memset(&destroy, 0, sizeof(destroy));
            destroy.handle = create.handle;
            ioctl(fd, XPDRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
        return false;
    }
    g_xpdrmFd = fd;
    g_xpdrmMap = (uint8_t*)map;
    g_xpdrmMapSize = (size_t)create.size;
    g_xpdrmDumbHandle = create.handle;
    g_xpdrmFbId = fb.fb_id;
    memset(&g_xpdrmInfo, 0, sizeof(g_xpdrmInfo));
    g_xpdrmInfo.m_width = (int)mode->hdisplay;
    g_xpdrmInfo.m_height = (int)mode->vdisplay;
    /* 单 dumb buffer 直写即可见：双缓冲页翻转待 PAGE_FLIP 事件泵落地
     * （见文件头 pan 注），先如实上报单缓冲。 */
    g_xpdrmInfo.m_doubleBuffered = false;
    g_xpdrmInfo.m_bitsPerPixel = (int)bpp;
    g_xpdrmInfo.m_stride = (size_t)create.pitch;
    g_xpdrmInfo.m_frameBuffer = map;
    g_xpdrmInfo.m_frameBufferSize = (size_t)create.size;
    g_xpdrmInfo.m_format =
        (bpp == 16) ? XImageFormat_RGB16 : XImageFormat_RGB32;
    return true;
}

/* ==================== 设备探测与释放 ==================== */

/**
 * @brief      打开 DRM 卡并完成资源枚举 + dumb 建链（幂等）。
 * @return     true 设备已就绪（含此前已就绪）；false 不可用。
 */
static bool xpdrm_probeDevice(void)
{
    int fd;
    struct xpdrm_mode_card_res res;
    uint32_t ci;
    bool chained = false;
    if (g_xpdrmDeviceReady) return true;

    fd = open(g_xpdrmDevicePath ? g_xpdrmDevicePath : XPLATFORM_DRM_DEVICE,
              O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return false; /* 无 /dev/dri 环境（开发机）：预期失败路径。 */

    /* 两步 GETRESOURCES：先取计数，再挂静态数组取 id 清单。 */
    memset(&res, 0, sizeof(res));
    if (ioctl(fd, XPDRM_IOCTL_MODE_GETRESOURCES, &res) != 0)
    {
        close(fd);
        return false;
    }
    if (res.count_crtcs > XPDRM_MAX_CRTCS)
        res.count_crtcs = XPDRM_MAX_CRTCS;
    if (res.count_connectors > XPDRM_MAX_CONNECTORS)
        res.count_connectors = XPDRM_MAX_CONNECTORS;
    /* 只取 CRTC/连接器 id 清单：FB/编码器两路计数必须清零——首次调用
     * 后 count 已回填真实值而指针为空，内核会向 NULL 拷贝（EFAULT
     * "Bad address"），真机首跑必现。 */
    res.count_fbs = 0;
    res.count_encoders = 0;
    res.fb_id_ptr = 0;
    res.encoder_id_ptr = 0;
    res.crtc_id_ptr = (uint64_t)(uintptr_t)g_xpdrmCrtcIds;
    res.connector_id_ptr = (uint64_t)(uintptr_t)g_xpdrmConnIds;
    if (res.count_crtcs < 1 || res.count_connectors < 1 ||
        ioctl(fd, XPDRM_IOCTL_MODE_GETRESOURCES, &res) != 0)
    {
        close(fd);
        return false;
    }

    for (ci = 0; ci < res.count_connectors && !chained; ++ci)
    {
        struct xpdrm_mode_get_connector conn;
        struct xpdrm_mode_get_encoder enc;
        uint32_t modeIdx = 0;
        uint32_t i;
        uint32_t encId;
        uint32_t crtcId;
        int crtcIdx;
        memset(&conn, 0, sizeof(conn));
        conn.connector_id = g_xpdrmConnIds[ci];
        conn.count_modes = 1; /* 计数轮口径（见 g_xpdrmProbeMode 注）。 */
        conn.modes_ptr = (uint64_t)(uintptr_t)&g_xpdrmProbeMode;
        if (ioctl(fd, XPDRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0)
            continue;
        conn.connector_id = g_xpdrmConnIds[ci];
        conn.count_modes =
            conn.count_modes < XPDRM_MAX_MODES ? conn.count_modes
                                               : XPDRM_MAX_MODES;
        conn.count_encoders =
            conn.count_encoders < XPDRM_MAX_ENCODERS ? conn.count_encoders
                                                     : XPDRM_MAX_ENCODERS;
        /* 属性表不消费：计数清零（同 GETRESOURCES 口径，指针空时计数
         * 残留会让内核向 NULL 拷贝、整个 ioctl EFAULT）。 */
        conn.count_props = 0;
        conn.modes_ptr = (uint64_t)(uintptr_t)g_xpdrmModes;
        conn.encoders_ptr = (uint64_t)(uintptr_t)g_xpdrmEncIds;
        if (conn.count_modes < 1 ||
            ioctl(fd, XPDRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0)
            continue;
        if (conn.connection != XPDRM_MODE_CONNECTED)
            continue; /* 未接面板的连接器（HDMI 空座等）：跳过。 */

        /* 模式选择：EDID 首选优先，缺省 modes[0]。 */
        for (i = 0; i < conn.count_modes; ++i)
        {
            if (g_xpdrmModes[i].type & XPDRM_MODE_TYPE_PREFERRED)
            {
                modeIdx = i;
                break;
            }
        }
        /* 编码器：连接器当前绑定优先（应用侧接续既有扫描out），未绑定
         * 取连接器候选首位。 */
        encId = conn.encoder_id;
        if (encId == 0 && conn.count_encoders > 0)
            encId = g_xpdrmEncIds[0];
        if (encId == 0)
            continue;
        memset(&enc, 0, sizeof(enc));
        enc.encoder_id = encId;
        if (ioctl(fd, XPDRM_IOCTL_MODE_GETENCODER, &enc) != 0)
            continue;
        /* possible_crtcs 位图 -> 资源 CRTC 清单下标；空位图兜底 0 号。 */
        crtcIdx = (enc.possible_crtcs != 0)
                      ? __builtin_ffs((int)enc.possible_crtcs) - 1
                      : 0;
        if (crtcIdx < 0 || crtcIdx >= (int)res.count_crtcs)
            crtcIdx = 0;
        crtcId = g_xpdrmCrtcIds[crtcIdx];

        /* 16bpp 优先（RGB565 面板直写链零转换）；内核/控制器不支持时
         * 退 32bpp XRGB8888（消费侧按 formatNegotiate 建转换路径）。 */
        if (xpdrm_createDumbChain(fd, crtcId, g_xpdrmConnIds[ci],
                                  &g_xpdrmModes[modeIdx], 16))
            chained = true;
        else if (xpdrm_createDumbChain(fd, crtcId, g_xpdrmConnIds[ci],
                                       &g_xpdrmModes[modeIdx], 32))
            chained = true;
    }

    if (!chained)
    {
        close(fd);
        return false;
    }
    /* 就绪守卫必须落位（fbdev 模板同口径）：缺了它每次 probe 都重跑
     * open+建 dumb+SETCRTC 全链——dumb/FB 持续泄漏（前 6 次 present
     * 侥幸成功、第 7 次起内核资源耗尽 probe 恒败，画面冻结），真机
     * RK3506 首跑实证。 */
    g_xpdrmDeviceReady = true;
    return true;
}

/** @brief 释放映射/FB/dumb/设备，状态复位（未就绪安全 no-op）。 */
static void xpdrm_shutdown(void)
{
    if (g_xpdrmMap)
    {
        munmap(g_xpdrmMap, g_xpdrmMapSize);
        g_xpdrmMap = NULL;
    }
    g_xpdrmMapSize = 0;
    if (g_xpdrmFd >= 0)
    {
        if (g_xpdrmFbId != 0)
        {
            struct xpdrm_mode_fb_cmd fb;
            memset(&fb, 0, sizeof(fb));
            fb.fb_id = g_xpdrmFbId;
            ioctl(g_xpdrmFd, XPDRM_IOCTL_MODE_RMFB, &fb);
            g_xpdrmFbId = 0;
        }
        if (g_xpdrmDumbHandle != 0)
        {
            struct xpdrm_mode_destroy_dumb destroy;
            memset(&destroy, 0, sizeof(destroy));
            destroy.handle = g_xpdrmDumbHandle;
            ioctl(g_xpdrmFd, XPDRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
            g_xpdrmDumbHandle = 0;
        }
        close(g_xpdrmFd);
        g_xpdrmFd = -1;
    }
    g_xpdrmDeviceReady = false;
    memset(&g_xpdrmInfo, 0, sizeof(g_xpdrmInfo));
}

/* ==================== 契约六钩子实现 ==================== */

static bool xpdrm_cacheSync(XPlatformDisplayCacheMode mode, void* address,
                            size_t length);

static bool xpdrm_probe(XPlatformDisplayInfo* outInfo)
{
    if (!xpdrm_probeDevice())
    {
        if (outInfo) memset(outInfo, 0, sizeof(*outInfo));
        return false;
    }
    if (outInfo) *outInfo = g_xpdrmInfo;
    return true;
}

static bool xpdrm_formatNegotiate(XImageFormat preferred,
                                  XImageFormat* outFormat)
{
    XImageFormat panel;
    if (outFormat) *outFormat = XImageFormat_Invalid;
    if (!xpdrm_probeDevice()) return false;
    panel = g_xpdrmInfo.m_format;
    if (panel == XImageFormat_Invalid)
    {
        if (outFormat) *outFormat = XImageFormat_Invalid;
        return false;
    }
    if (outFormat) *outFormat = panel;
    if (preferred == XImageFormat_Invalid)
        return true; /* 纯查询：仅报告面板格式。 */
    /* 直写可行当且仅当表面格式与面板扫描格式一致（口径同 fbdev 后端）。 */
    return preferred == panel;
}

static bool xpdrm_pan(int bufferIndex)
{
    if (!g_xpdrmDeviceReady || g_xpdrmFd < 0) return false;
    return bufferIndex == 0; /* 单缓冲直写即可见；翻页不可用。 */
}

static bool xpdrm_cacheSync(XPlatformDisplayCacheMode mode, void* address,
                            size_t length)
{
    void* base;
    size_t len;
    (void)mode; /* 同 fbdev 模板：统一按"写回"尽力处理。 */
    if (!g_xpdrmDeviceReady || !g_xpdrmMap) return false;
    if (address)
    {
        base = address;
        len = length;
        /* 越界区间裁回映射区，防止板级误传拖垮内核调用。 */
        if ((const uint8_t*)base < g_xpdrmMap ||
            (const uint8_t*)base >= g_xpdrmMap + g_xpdrmMapSize)
            return false;
        if ((const uint8_t*)base + len > g_xpdrmMap + g_xpdrmMapSize)
            len = (size_t)(g_xpdrmMap + g_xpdrmMapSize -
                           (const uint8_t*)base);
    }
    else
    {
        base = g_xpdrmMap;
        len = g_xpdrmMapSize;
    }
    if (len == 0) return true;
    /* 同 fbdev 模板：ARM Linux 经私有 cacheflush 系统调用清 DCache；
     * Rockchip dumb buffer 映射通常 write-combine 本就无需同步，失败
     * 不谎报（返回 false，消费链打一次性诊断）。 */
#if defined(__arm__)
    {
        long rc = syscall(0x0f0002, (unsigned long)base,
                          (unsigned long)((char*)base + len), 0);
        if (rc == 0) return true;
    }
#endif
    (void)msync(base, len, MS_SYNC);
    return false; /* 未做 CPU cache 同步，不谎报成功。 */
}

static bool xpdrm_waitVsync(int timeoutMilliseconds)
{
    union xpdrm_wait_vblank vbl;
    unsigned elapsed = 0;
    unsigned limit;
    uint32_t base = 0;
    bool haveBase = false;
    if (!g_xpdrmDeviceReady || g_xpdrmFd < 0) return false;
    /* 非阻塞轮询口径（真机教训：RELATIVE sequence>=1 的 WAIT_VBLANK
     * 是无超时阻塞——RK3506 VOP1 在 legacy SETCRTC 路径下 vblank 中断
     * 可能不来（/proc/interrupts 计数冻结），一次调用即把整条帧泵堵
     * 死。改为 RELATIVE sequence=0 查询当前计数（内核立即返回），以
     * 计数前进沿判定 vsync；计数不前进（中断缺失）限时退化并如实返
     * 回 false，绝不无限等。 */
    limit = timeoutMilliseconds > 0 ? (unsigned)timeoutMilliseconds : 100u;
    while (elapsed < limit)
    {
        memset(&vbl, 0, sizeof(vbl));
        vbl.request.type = XPDRM_VBLANK_RELATIVE;
        vbl.request.sequence = 0; /* 纯查询：立即返回当前计数。 */
        if (ioctl(g_xpdrmFd, XPDRM_IOCTL_WAIT_VBLANK, &vbl) == 0)
        {
            if (!haveBase)
            {
                base = vbl.reply.sequence;
                haveBase = true;
            }
            else if (vbl.reply.sequence != base)
                return true; /* 垂直回扫计数前进：已跨过一帧。 */
        }
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000L; /* 1ms。 */
            nanosleep(&ts, NULL);
        }
        ++elapsed;
    }
    return false;
}

static size_t xpdrm_stride(int width, XImageFormat format)
{
    int depth;
    size_t pixelBytes;
    size_t minimal;
    if (width <= 0 || !g_xpdrmDeviceReady) return 0;
    depth = XImageFormat_bitDepth(format);
    if (depth <= 0) return 0;
    pixelBytes = (size_t)((depth + 7) / 8);
    minimal = (size_t)width * pixelBytes;
    /* 面板扫描格式：dumb pitch 含硬件对齐填充，必须以 pitch 为准。 */
    if (format == g_xpdrmInfo.m_format && g_xpdrmInfo.m_stride >= minimal)
        return g_xpdrmInfo.m_stride;
    /* 其余软件格式回落公共层口径（与 XImage/XBackingStore 一致）。 */
    return (size_t)XImageFormat_bytesPerLine(width, format);
}

/* ==================== ops 表与注册入口 ==================== */

/** @brief DRM 驱动 ops 表（六钩子齐全，静态生命期）。 */
static const XPlatformDisplayDriverOps g_xpdrmOps =
{
    "drm",                                /* m_name。 */
    XPLATFORM_DISPLAY_DRIVER_ABI_VERSION, /* m_abiVersion。 */
    xpdrm_probe,
    xpdrm_formatNegotiate,
    xpdrm_pan,
    xpdrm_cacheSync,
    xpdrm_waitVsync,
    xpdrm_stride
};

bool XPlatformDrm_register(void)
{
    if (g_xpdrmRegistered)
        return XPlatformDisplayDriver_active() == &g_xpdrmOps;
    /* 反向时序防护（口径同 fbdev 后端）：DRM 必须在 GUI/X11 初始化之
     * 前注册，否则直写路径会与窗口 present 并存，属设计外时序。 */
    if (XPlatformNativeWindow_isAvailable())
    {
        fprintf(stderr,
                "[drm] ERROR: native window backend already established; "
                "call XPlatformDrm_register() before XGuiApplication "
                "initialization.\n");
        return false;
    }
    if (!xpdrm_probeDevice())
        return false; /* 无 /dev/dri 或无已连接面板：不注册（预期路径）。 */
    if (!XPlatformDisplayDriver_register(&g_xpdrmOps))
    {
        /* 已有活动驱动（互斥失败）：释放本设备，保持零占用。 */
        xpdrm_shutdown();
        return false;
    }
    g_xpdrmRegistered = true;
    return true;
}

void XPlatformDrm_unregister(void)
{
    if (!g_xpdrmRegistered) return;
    XPlatformDisplayDriver_unregister(&g_xpdrmOps);
    xpdrm_shutdown();
    g_xpdrmRegistered = false;
}

bool XPlatformDrm_isAvailable(void)
{
    return g_xpdrmRegistered &&
           XPlatformDisplayDriver_active() == &g_xpdrmOps;
}

const XPlatformDisplayDriverOps* XPlatformDrm_driverOps(void)
{
    return &g_xpdrmOps;
}

bool XPlatformNativeWindow_useDrmDriver(const char* device)
{
    if (g_xpdrmRegistered)
        return XPlatformDrm_isAvailable(); /* 幂等。 */
    if (device && *device)
        g_xpdrmDevicePath = device;
    return XPlatformDrm_register();
}

#endif /* defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON && XPLATFORM_DRM_ON */
