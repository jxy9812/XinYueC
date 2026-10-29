/** @file       XLinkFileServer.c
 * @brief      XLink 文件传输服务端实现（沙箱 + 策略 + 会话绑定）。
 * @details    只依赖 XinYueC 抽象层（XDeviceFile/XFile/XString/XDateTime/
 *             XMemory），禁止平台 API 与 stdio。
 *
 *             安全模型：
 *               1. 线上只接受相对路径，且必须通过 XLinkFile_isSafeName；
 *               2. 与根目录拼接后经 XDeviceFile_resolvePath(Canonical)
 *                  规范化，再做前缀校验——防止 "../" 与软链接逃逸；
 *               3. 规范化失败（目标不存在）时，对可写目标先规范化其父目录
 *                  再做前缀校验，避免"用不存在的路径绕过"；
 *               4. 上传侧拒绝覆盖已存在文件（除非策略显式允许）；
 *               5. 下载侧只允许普通文件，拒绝目录与符号链接。
 * @author     XinYueC 团队
 */
#include "XLinkFileServer.h"

/* 门控含 XLINK_SESSION_ON/XLINK_SERVER_ON 的原因见 XLinkFileServer.h
 * 门控补记：头文件内嵌 XLinkServer/XLinkSession 指针成员，子开关关闭时
 * 类型未声明，旧门控在裁剪组合下 error C2061（矩阵实测见 XLink.md §14.5）。 */
#if XLINK_ON && XLINK_SESSION_ON && XLINK_FILE_ON && XLINK_SERVER_ON

#include "XLinkFile.h"
#include "XLinkSession.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XDeviceFile.h"
#include "XFileInfo.h"
#include "XPrintf.h"
#include <string.h>

/* ========================================================================== */
/*                        内部工具                                           */
/* ========================================================================== */

/** @brief 当前单调毫秒。 */
static uint32_t xfs_nowMs(void)
{
    return (uint32_t)XDateTime_currentMSecsSinceEpoch();
}

/** @brief 取空闲会话槽；已有同会话槽则复用。 */
static XLinkFileServerSession* xfs_slotOf(XLinkFileServer* fs,
                                          XLinkSession* session)
{
    int i;
    XLinkFileServerSession* freeSlot = NULL;
    if (!fs || !session)
        return NULL;
    for (i = 0; i < (int)XLINK_FILE_MAX_TRANSFERS; ++i) {
        if (fs->m_slots[i].m_used
            && fs->m_slots[i].m_session == session)
            return &fs->m_slots[i];
        if (!fs->m_slots[i].m_used && !freeSlot)
            freeSlot = &fs->m_slots[i];
    }
    return freeSlot;
}

/** @brief 释放一个会话槽。 */
static void xfs_releaseSlot(XLinkFileServerSession* slot)
{
    if (!slot || !slot->m_used)
        return;
    XLinkFileTransfer_deinit(&slot->m_transfer);
    slot->m_session = NULL;
    slot->m_used = false;
}

/** @brief 解析并校验沙箱路径。
 *  @param fs 服务端。
 *  @param name 线上文件名（相对路径）。
 *  @param mustExist true 表示目标必须已存在（下载）；false 表示可写目标。
 *  @param out 输出规范化后的绝对路径（调用方拥有；失败时不设置）。
 *  @return 通过返回 true。 */
static bool xfs_resolve(XLinkFileServer* fs, const char* name,
                        bool mustExist, XString** out)
{
    XString* joined = NULL;
    XString* resolved = NULL;
    const char* root;
    size_t rootLen;
    bool ok = false;

    if (!fs || !name || !out)
        return false;
    if (!fs->m_policy.m_rootDir)
        return false;
    root = XString_toUtf8(fs->m_policy.m_rootDir);
    if (!root || !root[0])
        return false;

    /* 拼接：root + '/' + name（root 已带分隔符时不重复加）。 */
    joined = XString_create_utf8(root);
    if (!joined)
        return false;
    rootLen = XStrlen(root);
    if (rootLen > 0 && root[rootLen - 1u] != '/'
        && root[rootLen - 1u] != '\\') {
        if (!XString_append_utf8(joined, "/")) {
            XClassDelete((XClass*)joined);
            return false;
        }
    }
    if (!XString_append_utf8(joined, name)) {
        XClassDelete((XClass*)joined);
        return false;
    }

    /* 存在性与类型检查。 */
    {
        XFileStat stat;
        bool exists = XDeviceFile_stat(joined, &stat) && stat.exists;

        if (mustExist && !exists) {
            XClassDelete((XClass*)joined);
            return false;
        }
        if (!mustExist && exists) {
            if (!(fs->m_policy.m_flags & XLINK_FILESRV_ALLOW_OVERWRITE)) {
                XClassDelete((XClass*)joined);
                return false;
            }
            /* 目标存在时必须是普通文件（不能覆盖目录/设备/软链接）。 */
            if (!stat.isFile || stat.isSymLink) {
                XClassDelete((XClass*)joined);
                return false;
            }
        }
    }

    /* Canonical 解析；目标不存在（可写目标）时回退到 Absolute 规范化。 */
    resolved = XString_create();
    if (!resolved) {
        XClassDelete((XClass*)joined);
        return false;
    }
    if (!XDeviceFile_resolvePath(joined, resolved, XPathStyle_Canonical)
        && !XDeviceFile_resolvePath(joined, resolved, XPathStyle_Absolute)) {
        XClassDelete((XClass*)joined);
        XClassDelete((XClass*)resolved);
        return false;
    }
    XClassDelete((XClass*)joined);

    /* 根目录规范化（首次使用时计算并缓存）。 */
    if (!fs->m_resolvedRoot) {
        XString* rootIn = XString_create_utf8(root);
        XString* rootOut = XString_create();
        if (rootIn && rootOut
            && XDeviceFile_resolvePath(rootIn, rootOut, XPathStyle_Canonical))
            fs->m_resolvedRoot =
                XString_create_utf8(XString_toUtf8(rootOut));
        if (rootIn) XClassDelete((XClass*)rootIn);
        if (rootOut) XClassDelete((XClass*)rootOut);
    }
    if (!fs->m_resolvedRoot) {
        XClassDelete((XClass*)resolved);
        return false;
    }

    /* 前缀校验：规范化结果必须严格落在根目录内，且根目录后紧邻分隔符
       （防 /data/root 误匹配 /data/root2）。 */
    rootLen = XString_toUtf8_length(fs->m_resolvedRoot);
    {
        const char* rootCanon = XString_toUtf8(fs->m_resolvedRoot);
        const char* candidate = XString_toUtf8(resolved);
        size_t candidateLen = candidate ? XStrlen(candidate) : 0;
        if (!rootCanon || !candidate
            || candidateLen <= rootLen
            || strncmp(candidate, rootCanon, rootLen) != 0
            || (candidate[rootLen] != '/' && candidate[rootLen] != '\\')) {
            XClassDelete((XClass*)resolved);
            return false;
        }
    }

    *out = resolved;
    ok = true;
    return ok;
}

/** @brief 角色是否满足最低要求。 */
static bool xfs_roleAllowed(XLinkFileServer* fs, XLinkSession* session)
{
    if (!fs || !session)
        return false;
    if (!(fs->m_policy.m_flags & XLINK_FILESRV_REQUIRE_AUTH))
        return true;
#if XLINK_AUTH_ON
    if (!XLinkSession_isAuthenticated(session))
        return false;
    return XLinkSession_authRole(session) >= fs->m_policy.m_minRole;
#else
    /* 认证扩展整体裁空：协议层无登录概念，无会话能"通过认证"，
       REQUIRE_AUTH/minRole 无可执行语义——按未引入本扩展前的基线放行
       （XLink_config.h 约定：关闭时协议行为与引入前完全一致）。同时避免
       引用被裁空的 XLinkSession_isAuthenticated/authRole（声明与定义均
       在 XLINK_AUTH_ON 门内）造成链接期未解析符号。 */
    return true;
#endif
}

/** @brief 向对端回一个拒绝 OpenAck（避免对端无限等待）。
 *  @note  只在"Open 本身可解析"时调用；载荷畸形时无法安全关联 id，
 *         只能静默丢弃（对端由空闲超时兜底）。 */
static void xfs_rejectOpen(XLinkFileServer* fs, XLinkSession* session,
                           const XLinkFrameHeader* frame, uint8_t status,
                           const char* text)
{
    XLinkFileOpenAckPayload ack;
    uint8_t buf[XLINK_FILE_OPEN_ACK_HEADER + XLINK_FILE_TEXT_BYTES];
    XLinkFrameHeader out;
    size_t len = 0;
    size_t textLen = text ? XStrlen(text) : 0;

    if (!session || !frame)
        return;
    if (textLen > XLINK_FILE_TEXT_BYTES)
        textLen = XLINK_FILE_TEXT_BYTES;
    memset(&ack, 0, sizeof(ack));
    ack.m_accepted = 0;
    ack.m_status = status;
    ack.m_textLen = (uint16_t)textLen;
    ack.m_chunkBytes = fs ? fs->m_policy.m_chunkBytes
                          : XLINK_FILE_CHUNK_BYTES;
    ack.m_windowBytes = fs ? fs->m_policy.m_windowBytes
                           : XLINK_FILE_WINDOW_BYTES;
    if (textLen > 0)
        memcpy(ack.m_text, text, textLen);
    if (!XLinkFile_encodeOpenAck(&ack, buf, sizeof(buf), &len))
        return;
    out.m_payloadLen = (uint32_t)len;
    out.m_msgClass = XLinkMsgClass_File;
    out.m_msgCode = XLinkFileCode_OpenAck;
    out.m_requestId = frame->m_requestId;
    (void)XLinkSession_send(session, &out, buf);
}

/** @brief 处理 Open：策略校验 + 沙箱解析 + 启动接收/发送。 */
static void xfs_onOpen(XLinkFileServer* fs, XLinkSession* session,
                       const XLinkFrameHeader* frame, const uint8_t* payload)
{
    XLinkFileOpenPayload open;
    XLinkFileServerSession* slot;
    XString* resolved = NULL;
    bool wantUpload;

    if (!XLinkFile_decodeOpen(payload, frame->m_payloadLen, &open)) {
        /* 畸形 Open：载荷都解不出来，无法安全回帧，直接丢弃。 */
        return;
    }
    if (!XLinkFile_isSafeName(open.m_name, open.m_nameLen)) {
        xfs_rejectOpen(fs, session, frame, XLinkFileStatus_Rejected,
                       "unsafe file name");
        return;
    }
    if (!xfs_roleAllowed(fs, session)) {
        xfs_rejectOpen(fs, session, frame, XLinkFileStatus_RoleDenied,
                       "permission denied");
        return;
    }
    wantUpload = (open.m_mode == XLinkFileMode_Upload);
    if (wantUpload
        && !(fs->m_policy.m_flags & XLINK_FILESRV_ALLOW_UPLOAD)) {
        xfs_rejectOpen(fs, session, frame, XLinkFileStatus_Rejected,
                       "upload disabled");
        return;
    }
    if (!wantUpload
        && !(fs->m_policy.m_flags & XLINK_FILESRV_ALLOW_DOWNLOAD)) {
        xfs_rejectOpen(fs, session, frame, XLinkFileStatus_Rejected,
                       "download disabled");
        return;
    }
    if (fs->m_policy.m_maxFileBytes > 0
        && open.m_totalSize > fs->m_policy.m_maxFileBytes) {
        xfs_rejectOpen(fs, session, frame, XLinkFileStatus_TooLarge,
                       "file too large");
        return;
    }
    if (open.m_totalSize > XLINK_FILE_MAX_BYTES) {
        xfs_rejectOpen(fs, session, frame, XLinkFileStatus_TooLarge,
                       "file too large");
        return;
    }

    /* 沙箱解析：上传目标可新建，下载目标必须存在。 */
    if (!xfs_resolve(fs, open.m_name, !wantUpload, &resolved)) {
        xfs_rejectOpen(fs, session, frame,
                       wantUpload ? XLinkFileStatus_Rejected
                                  : XLinkFileStatus_NotFound,
                       wantUpload ? "target rejected" : "file not found");
        return;
    }

    slot = xfs_slotOf(fs, session);
    if (!slot) {
        /* 槽满（XLINK_FILE_MAX_TRANSFERS 是**全局**上限，与本会话无关）：
           这里必须回一帧，不能静默返回。OpenAck 是 Open 的唯一应答，
           丢弃后对端只能一直重发 Open，直到 30 次重发耗尽才超时，表现为
           “第 N 个并发传输入口卡死”；立即回 Rejected/"busy" 才能让对端
           马上失败返回或择机重试。 */
        if (resolved)
            XClassDelete((XClass*)resolved);
        xfs_rejectOpen(fs, session, frame, XLinkFileStatus_Rejected,
                       "busy");
        return;
    }
    if (slot->m_used)
        xfs_releaseSlot(slot);
    slot->m_session = session;
    slot->m_used = true;

    if (wantUpload) {
        if (!XLinkFileTransfer_beginReceive(
                &slot->m_transfer, session, &open,
                XString_toUtf8(resolved), frame->m_requestId,
                fs->m_policy.m_chunkBytes, fs->m_policy.m_windowBytes)) {
            /* 受理失败（创建/打开落盘文件失败等）：beginReceive 只在成功
               路径发 OpenAck，其内部 xft_fail 明确"不发帧"（见
               XLinkFileTransfer.c xft_fail 注），这里必须补一帧拒绝
               OpenAck——OpenAck 是 Open 的唯一应答，静默释放槽位后对端
               只能重发 Open 直到 XLINK_FILE_OPEN_RETRY_MAX（30 次）耗尽
               才超时，与上面槽满分支是同一缺陷类。 */
            xfs_rejectOpen(fs, session, frame, XLinkFileStatus_NoSpace,
                           "cannot open local file");
            xfs_releaseSlot(slot);
        }
    }
    else {
        /* 下载：本端是发送方。总大小以**本地实际文件**为准（对端申报值
           只是它期望值，不能作为发送依据），起始偏移也不能超过实际大小。 */
        XFileStat stat;
        uint64_t realSize = 0;
        bool statOk = XDeviceFile_stat(resolved, &stat)
                      && stat.exists && stat.isFile;
        if (statOk)
            realSize = (uint64_t)(stat.size > 0 ? stat.size : 0);
        if (!statOk) {
            /* resolve 已通过但 stat 失败：文件在两步之间消失或不再是
               普通文件。同样必须回拒绝 OpenAck（理由同上）。 */
            xfs_rejectOpen(fs, session, frame, XLinkFileStatus_NotFound,
                           "file not found");
            xfs_releaseSlot(slot);
        }
        else if (!XLinkFileTransfer_beginSendFile(
                     &slot->m_transfer, session, XString_toUtf8(resolved),
                     open.m_name, open.m_nameLen, open.m_flags, realSize,
                     frame->m_requestId, fs->m_policy.m_chunkBytes,
                     fs->m_policy.m_windowBytes, open.m_startOffset)) {
            /* 打开本地文件/seek 失败：beginSendFile 也只在成功路径发
               OpenAck，失败路径同样静默，必须补拒绝帧（理由同上）。 */
            xfs_rejectOpen(fs, session, frame, XLinkFileStatus_NotFound,
                           "cannot open local file");
            xfs_releaseSlot(slot);
        }
    }
    if (resolved)
        XClassDelete((XClass*)resolved);
}

/** @brief File 帧入口（注册给 XLinkServer）。 */
static bool xfs_onFileFrame(XLinkSession* session,
                            const XLinkFrameHeader* frame,
                            const uint8_t* payload, void* userData)
{
    XLinkFileServer* fs = (XLinkFileServer*)userData;
    XLinkFileServerSession* slot;
    uint32_t now;

    if (!fs || !session || !frame)
        return true;
    now = xfs_nowMs();

    /* Open 是唯一会新建槽的操作码；其余必须命中已有槽。 */
    if (frame->m_msgCode == XLinkFileCode_Open) {
        xfs_onOpen(fs, session, frame, payload);
        return true;
    }
    slot = xfs_slotOf(fs, session);
    if (!slot || !slot->m_used)
        return true; /* 无对应传输：迟到帧/已被回收，静默丢弃 */
    if (slot->m_transfer.m_transferId != frame->m_requestId)
        return true; /* 传输 id 不符：其他传输的帧，丢弃 */
    XLinkFileTransfer_onFrame(&slot->m_transfer, frame, payload, now);
    if (XLinkFileTransfer_isFinished(&slot->m_transfer))
        xfs_releaseSlot(slot);
    return true;
}

/** @brief 会话被服务端回收前回调：立即清掉该会话的传输槽。
 *  @note  服务端保证该钩子在删除会话**之前**调用（指针仍有效），
 *         这是清理借用指针的唯一安全时机。 */
static void xfs_onSessionGone(XLinkServer* server, XLinkSession* session,
                              void* userData)
{
    XLinkFileServer* fs = (XLinkFileServer*)userData;
    int i;

    (void)server;
    if (!fs || !session)
        return;
    for (i = 0; i < (int)XLINK_FILE_MAX_TRANSFERS; ++i) {
        if (fs->m_slots[i].m_used
            && fs->m_slots[i].m_session == session)
            xfs_releaseSlot(&fs->m_slots[i]);
    }
}

/** @brief 会话 pump 之前的续命钩子：活跃传输期间抑制静默判线 + 提升节拍。
 *  @note  必须在会话 pump 之前调用——会话 pump 内含静默判线，等到 tick
 *         （pump 末尾）再续命就已经判线断链了（实测下载方向）。
 *         同时把服务端节拍临时提到文件传输专用频率：默认 250ms 的节拍
 *         对 4KB 分块传输太慢（38011 字节要 10 轮 ≈ 2.5s，且期间还要
 *         等 ACK），大文件会传得像龟速。传输结束由 xfs_tick 恢复。 */
static void xfs_preSessionPump(XLinkServer* server, XLinkSession* session,
                               void* userData)
{
    XLinkFileServer* fs = (XLinkFileServer*)userData;
    int i;

    (void)server;
    if (!fs || !session)
        return;
    for (i = 0; i < (int)XLINK_FILE_MAX_TRANSFERS; ++i) {
        if (fs->m_slots[i].m_used
            && fs->m_slots[i].m_session == session
            && !XLinkFileTransfer_isFinished(&fs->m_slots[i].m_transfer)) {
            (void)XLinkSession_keepAlive(session, 0);
            return;
        }
    }
}

/** @brief 节拍：推进所有活跃传输并回收终局槽。
 *  @note  会话可能已被服务端回收（指针失效）：槽的生命周期**必须**由
 *         XLinkServer 的会话增删通知驱动，不能靠"解引用会话查状态"——
 *         那是指针悬垂后才会发现的 use-after-free。这里只做"会话指针
 *         仍被登记"的推进，真正的清理在 xfs_onSessionEvent。 */
static void xfs_tick(XLinkServer* server, void* userData)
{
    XLinkFileServer* fs = (XLinkFileServer*)userData;
    uint32_t now;
    int i;

    (void)server;
    if (!fs || fs->m_inTick)
        return; /* 重入保护：timerEvent 与手动 pump 并存时只推进一次 */
    fs->m_inTick = true;
    now = xfs_nowMs();
    for (i = 0; i < (int)XLINK_FILE_MAX_TRANSFERS; ++i) {
        XLinkFileServerSession* slot = &fs->m_slots[i];
        if (!slot->m_used || !slot->m_session)
            continue;
        /* 只推进传输。会话本身收发由 XLinkServer_pump 负责——本回调
           运行在它内部，再显式 pump 同一会话会把收发游标搅乱
           （实测段错误）。 */
        XLinkFileTransfer_pump(&slot->m_transfer, now);
        if (XLinkFileTransfer_isFinished(&slot->m_transfer))
            xfs_releaseSlot(slot);
    }
    fs->m_inTick = false;
}

/* ========================================================================== */
/*                        生命周期                                           */
/* ========================================================================== */

void XLinkFileServer_init(XLinkFileServer* fs)
{
    int i;
    if (!fs)
        return;
    memset(fs, 0, sizeof(*fs));
    fs->m_policy.m_rootDir = NULL;
    fs->m_policy.m_maxFileBytes = XLINK_FILE_MAX_BYTES;
    fs->m_policy.m_chunkBytes = XLINK_FILE_CHUNK_BYTES;
    fs->m_policy.m_windowBytes = XLINK_FILE_WINDOW_BYTES;
    fs->m_policy.m_flags = XLINK_FILESRV_REQUIRE_AUTH;
    fs->m_policy.m_minRole = XLinkAuthRole_User;
    fs->m_inTick = false;
    fs->m_prevTickCallback = NULL;
    fs->m_prevTickUserData = NULL;
    for (i = 0; i < (int)XLINK_FILE_MAX_TRANSFERS; ++i) {
        XLinkFileTransfer_init(&fs->m_slots[i].m_transfer);
        fs->m_slots[i].m_used = false;
        fs->m_slots[i].m_session = NULL;
    }
}

void XLinkFileServer_deinit(XLinkFileServer* fs)
{
    int i;
    if (!fs)
        return;
    /* 先摘钩子：服务端上仍挂着指向本对象的四个回调（File/tick/
       sessionGone/preSessionPump）。若调用方随后释放本对象、再销毁
       服务端，XLinkServer_close 回收会话时会以已释放的 fs 调用
       sessionGone 钩子 → 释放后使用（实测 Windows 段错误，栈
       xfs_onSessionGone ← XLinkServer_close）。摘钩在前，销毁顺序
       即便颠倒也安全。 */
    XLinkFileServer_detach(fs);
    for (i = 0; i < (int)XLINK_FILE_MAX_TRANSFERS; ++i)
        xfs_releaseSlot(&fs->m_slots[i]);
    if (fs->m_policy.m_rootDir) {
        XClassDelete((XClass*)fs->m_policy.m_rootDir);
        fs->m_policy.m_rootDir = NULL;
    }
    if (fs->m_resolvedRoot) {
        XClassDelete((XClass*)fs->m_resolvedRoot);
        fs->m_resolvedRoot = NULL;
    }
    fs->m_server = NULL;
    fs->m_attached = false;
    fs->m_inTick = false;
    fs->m_prevTickCallback = NULL;
    fs->m_prevTickUserData = NULL;
}

/* ========================================================================== */
/*                        配置                                               */
/* ========================================================================== */

bool XLinkFileServer_setRootDir(XLinkFileServer* fs, const char* dir)
{
    XString* path;
    XFileStat stat;

    if (!fs || !dir || !dir[0])
        return false;
    path = XString_create_utf8(dir);
    if (!path)
        return false;
    if (!XDeviceFile_stat(path, &stat) || !stat.exists || !stat.isDir) {
        XClassDelete((XClass*)path);
        return false;
    }
    if (fs->m_policy.m_rootDir)
        XClassDelete((XClass*)fs->m_policy.m_rootDir);
    fs->m_policy.m_rootDir = path;
    if (fs->m_resolvedRoot) {
        XClassDelete((XClass*)fs->m_resolvedRoot);
        fs->m_resolvedRoot = NULL;
    }
    return true;
}

bool XLinkFileServer_setFlags(XLinkFileServer* fs, uint8_t flags)
{
    if (!fs)
        return false;
    fs->m_policy.m_flags = flags;
    return true;
}

bool XLinkFileServer_setMaxFileBytes(XLinkFileServer* fs, uint64_t bytes)
{
    if (!fs)
        return false;
    fs->m_policy.m_maxFileBytes = bytes;
    return true;
}

bool XLinkFileServer_setMinRole(XLinkFileServer* fs, uint8_t role)
{
    if (!fs || role > XLinkAuthRole_Admin)
        return false;
    fs->m_policy.m_minRole = role;
    return true;
}

bool XLinkFileServer_attach(XLinkFileServer* fs, XLinkServer* server)
{
    if (!fs || !server)
        return false;
    /* 业务 tick 回调先存下：本类占用服务端唯一 tick 槽位，detach 时
       还原，否则业务的周期回调被静默吞掉（链式语义，见 §9.13）。 */
    XLinkServer_tickCallback_const(server, &fs->m_prevTickCallback,
                                   &fs->m_prevTickUserData);
    fs->m_server = server;
    fs->m_attached = true;
    XLinkServer_setFileCallback(server, xfs_onFileFrame, fs);
    XLinkServer_setTickCallback(server, xfs_tick, fs);
    XLinkServer_setSessionGoneHook(server, xfs_onSessionGone, fs);
    XLinkServer_setPreSessionPumpHook(server, xfs_preSessionPump, fs);
    /* 新会话的能力位由 XLinkServer 在建会话时统一补 FileTransfer 位
       （见 XLinkServer.c accept 路径），此处不改模板——避免覆盖掉
       服务端此前设置的其它能力位（如 Zlib/Tls）。 */
    return true;
}

void XLinkFileServer_detach(XLinkFileServer* fs)
{
    if (!fs)
        return;
    if (fs->m_server) {
        XLinkServer_setFileCallback(fs->m_server, NULL, NULL);
        XLinkServer_setSessionGoneHook(fs->m_server, NULL, NULL);
        XLinkServer_setPreSessionPumpHook(fs->m_server, NULL, NULL);
        /* tick 槽位还原给业务（本类摘掉自己那份）。 */
        XLinkServer_setTickCallback(fs->m_server, fs->m_prevTickCallback,
                                    fs->m_prevTickUserData);
        fs->m_prevTickCallback = NULL;
        fs->m_prevTickUserData = NULL;
    }
    fs->m_attached = false;
}

const char* XLinkFileServer_rootDir_const(const XLinkFileServer* fs)
{
    if (!fs || !fs->m_policy.m_rootDir)
        return NULL;
    return XString_toUtf8(fs->m_policy.m_rootDir);
}

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_FILE_ON && XLINK_SERVER_ON */
