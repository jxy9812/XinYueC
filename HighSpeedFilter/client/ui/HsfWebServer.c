/**
 * @file       HsfWebServer.c
 * @brief      网页服务（webui）随客户端起停 + 端口设置（实现）。
 * @details    见 HsfWebServer.h 的职责边界。核心是**把「网页端」当成一
 *             个子进程管起来**：
 *               ① 找 python 解释器（环境变量 → PATH 上的 python/python3）；
 *               ② 找网页端包目录（环境变量 → 程序目录旁的几处候选，必须
 *                  能看到 hsfweb/__main__.py 才认）；
 *               ③ 起 `python -m hsfweb --server <上游> --port <上游端口>
 *                  --web-port <网页端口>`，工作目录 = 网页端包目录，
 *                  stdout/stderr 落 <程序目录>/cfg/hsf_web.log；
 *               ④ 轮询子进程：退出即判失败，并从日志尾巴里读出原因
 *                  （端口被占用 ⇒ 「端口 N 被占用」，不是笼统的「失败」）。
 *             失败一律**如实报出**（不静默重试、不假装在跑）。
 *
 *             [为什么自己管进程而不是让用户手工起] 网页端此前只能手工
 *             起、端口写死在命令行；客户端与网页同开是常态 ⇒ 起停/端口
 *             必须是客户端里的设置项（用户 2026-11-14 口径）。
 *
 *             [为什么不用框架的 XSettings 之外的东西] 端口/开关走既有
 *             HsfSettingsStore（<程序目录>/cfg/hsf_client.ini），与客户端
 *             其它本地设置同一份文件、同一个落盘纪律。
 */
#include "HsfWebServer.h"

#include "HsfLinkClient.h"
#include "HsfSettingsStore.h"
#include "XCoreApplication.h"
#include "XDevice.h"
#include "XDeviceFile.h"
#include "XFileInfo.h"
#include "XIODevice.h"
#include "XPrintf.h"
#include "XProcess.h"
#include "XString.h"
#include "XSystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 设置键（<程序目录>/cfg/hsf_client.ini） ---- */
#define HSF_WEB_KEY_ENABLED "WebServer/Enabled" /**< 是否随客户端启动（int 0/1） */
#define HSF_WEB_KEY_PORT    "WebServer/Port"    /**< 网页监听端口 */

/* ---- 环境变量（部署/排障覆盖；不设也能跑） ---- */
#define HSF_WEB_ENV_PYTHON  "HSF_WEB_PYTHON" /**< 指定 python 解释器（绝对路径优先） */
#define HSF_WEB_ENV_DIR     "HSF_WEB_DIR"    /**< 指定网页端包目录 */

/** @brief 登录窗记的「上次连过的地址」（同字面：server/lastHost）。
 *  @details 未登录时用它当上游——网页端要在用户点登录**之前**就起来
 *           （用户口径「启动客户端，网页也启动」）。 */
#define HSF_WEB_KEY_LAST_HOST "server/lastHost"

/* ---- 其它常量 ---- */
#define HSF_WEB_LOG_NAME    "hsf_web.log"     /**< 网页端 stdout 日志（cfg/ 下） */
#define HSF_WEB_LOG_ERR     "hsf_web_err.log" /**< 网页端 stderr 日志（cfg/ 下） */
#define HSF_WEB_CFG_DIR     "cfg"          /**< 配置/日志子目录名 */
#define HSF_WEB_DEFAULT_HOST "127.0.0.1"   /**< 未登录时的上游缺省地址 */
#define HSF_WEB_DEFAULT_UP   6100          /**< 未登录时的上游缺省端口 */
#define HSF_WEB_LOG_TAIL     512           /**< 读日志尾巴的字节数 */

/* ---- 模块状态 ---- */
static XProcess* s_proc = NULL;   /**< 网页服务子进程（NULL=未起）。 */
static char      s_err[192] = "";  /**< 最近一次失败原因（空=无失败）。 */
static char      s_url[72]  = "";  /**< 网页地址（随端口更新）。 */
static bool      s_exited = false; /**< 子进程已退出（状态行报失败而非「已停止」）。 */
static int       s_exitCode = 0;   /**< 子进程退出码（诊断用）。 */

/* ==================== 小工具 ==================== */

/** @brief 应用目录（UTF-8 借用；随进程不变）。 */
static const char* webAppDir(void)
{
    const XString* s = XCoreApplication_applicationDirPath();
    return s ? XString_toUtf8(s) : "";
}

/** @brief 路径是否存在（文件或目录皆可）。 */
static bool webPathExists(const char* utf8)
{
    XString* s;
    bool ok;
    if (!utf8 || !utf8[0]) return false;
    s = XString_fromUtf8(utf8);
    if (!s) return false;
    ok = XFileInfo_exists_static(s);
    XClassDelete((XClass*)s);
    return ok;
}

/** @brief 拼路径（out = dir + 分隔 + leaf）。 */
static void webJoinPath(char* out, int cap, const char* dir, const char* leaf)
{
    size_t n = 0;
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (dir && dir[0]) {
        n = strlen(dir);
        if (n >= (size_t)cap) n = (size_t)cap - 1;
        memcpy(out, dir, n);
        out[n] = '\0';
    }
    if (leaf && leaf[0]) {
        size_t dn = strlen(out);
        if (dn > 0 && out[dn - 1] != '/' && out[dn - 1] != '\\' &&
            dn + 1 < (size_t)cap) {
            out[dn] = '/';
            out[dn + 1] = '\0';
        }
        snprintf(out + strlen(out), (size_t)cap - strlen(out), "%s", leaf);
    }
}

/** @brief 网页端包目录候选（找到第一个含 hsfweb/__main__.py 的即用）。 */
static bool webFindPackageDir(char* out, int cap)
{
    const char* env = XSystem_environment(HSF_WEB_ENV_DIR);
    char cand[512];
    char probe[600];
    int i;

    if (out && cap > 0) out[0] = '\0';
    if (env && env[0]) {
        webJoinPath(probe, (int)sizeof(probe), env, "hsfweb/__main__.py");
        if (webPathExists(probe)) {
            snprintf(out, (size_t)cap, "%s", env);
            return true;
        }
    }
    /* 候选：① <程序目录>/webui_py（部署形态：网页端与客户端同目录）
     *       ② <程序目录>/../HighSpeedFilter/webui_py（仓库形态：bin/ 上级）
     *       ③ <程序目录>/../../HighSpeedFilter/webui_py（更深的部署根） */
    {
        const char* app = webAppDir();
        static const char* const kLeafs[3] = {
            "webui_py",
            "../HighSpeedFilter/webui_py",
            "../../HighSpeedFilter/webui_py"
        };
        for (i = 0; i < 3; ++i) {
            webJoinPath(cand, (int)sizeof(cand), app, kLeafs[i]);
            webJoinPath(probe, (int)sizeof(probe), cand, "hsfweb/__main__.py");
            if (webPathExists(probe)) {
                snprintf(out, (size_t)cap, "%s", cand);
                return true;
            }
        }
    }
    return false;
}

/** @brief 上游地址（客户端连的那台；未登录则用上次连过的地址）。 */
static void webUpstream(char* host, int hostCap, int* port)
{
    const char* addr = HsfLinkClient_serverAddress();
    const char* colon;
    host[0] = '\0';
    *port = HSF_WEB_DEFAULT_UP;
    if (addr && addr[0]) {
        colon = strrchr(addr, ':');
        if (colon) {
            size_t n = (size_t)(colon - addr);
            if (n > 0 && n < (size_t)hostCap) {
                memcpy(host, addr, n);
                host[n] = '\0';
            }
            *port = atoi(colon + 1);
        }
    }
    if (!host[0]) {
        /* [未登录] 用登录窗记下的「上次连过的地址」——网页端要在用户
         * 还没点登录时就起来（用户口径「启动客户端，网页也启动」）。 */
        char last[128];
        last[0] = '\0';
        HsfSettingsStore_getString(HSF_WEB_KEY_LAST_HOST, last,
                                   (size_t)sizeof(last));
        if (last[0]) snprintf(host, (size_t)hostCap, "%s", last);
    }
    if (!host[0]) snprintf(host, (size_t)hostCap, "%s", HSF_WEB_DEFAULT_HOST);
    if (*port <= 0 || *port > 65535) *port = HSF_WEB_DEFAULT_UP;
}

/** @brief 网页端日志路径（<程序目录>/cfg/<name>）。 */
static void webLogPath(char* out, int cap, const char* name)
{
    char dir[512];
    webJoinPath(dir, (int)sizeof(dir), webAppDir(), HSF_WEB_CFG_DIR);
    webJoinPath(out, cap, dir, name);
}

/** @brief 读日志尾巴（找失败原因用；读不到给空串）。 */
static void webLogTailOf(const char* name, char* out, int cap)
{
    char path[600];
    XFileStat st;
    XString* sp;
    XDeviceOpenOptions options;
    XFd fd;
    int64_t size;
    int error = 0;
    int64_t got = 0;

    if (!out || cap <= 0) return;
    out[0] = '\0';
    webLogPath(path, (int)sizeof(path), name);
    sp = XString_fromUtf8(path);
    if (!sp) return;
    if (!XDeviceFile_stat(sp, &st) || !st.exists) {
        XClassDelete((XClass*)sp);
        return;
    }
    size = st.size;
    memset(&options, 0, sizeof(options));
    options.m_openMode = XIODevice_ReadOnly;
    options.m_target = sp;
    fd = XDevice_open(XDeviceType_File, &options, &error);
    if (fd == XFD_INVALID) {
        XClassDelete((XClass*)sp);
        return;
    }
    if (size > HSF_WEB_LOG_TAIL) {
        (void)XDeviceFile_seek(fd, size - HSF_WEB_LOG_TAIL, XSeekSet);
    }
    got = XDeviceFile_read(fd, out, (int64_t)cap - 1);
    if (got < 0) got = 0;
    out[got] = '\0';
    XDeviceFile_close(fd);
    XClassDelete((XClass*)sp);
}

/** @brief 从日志尾巴里认失败原因（端口占用是最常见的一种）。 */
static void webClassifyFailure(char* out, int cap, int webPort)
{
    char tail[1024];
    /* [先看 stderr] traceback/异常都在 stderr（stdout 是启动横幅）。 */
    webLogTailOf(HSF_WEB_LOG_ERR, tail, (int)sizeof(tail));
    if (!tail[0]) webLogTailOf(HSF_WEB_LOG_NAME, tail, (int)sizeof(tail));
    if (strstr(tail, "10048") || strstr(tail, "address already in use") ||
        strstr(tail, "Only one usage of each socket address")) {
        snprintf(out, (size_t)cap, "端口 %d 被占用（换端口或关掉占它的程序）",
                 webPort);
    } else if (strstr(tail, "ModuleNotFoundError") ||
               strstr(tail, "No module named")) {
        snprintf(out, (size_t)cap, "网页端包不完整（缺 hsfweb 模块）");
    } else if (tail[0]) {
        /* 有日志但认不出 ⇒ 取最后一行当原因（多半是 traceback 末行）。 */
        char* nl = strrchr(tail, '\n');
        char* last = nl ? (nl + 1) : tail;
        size_t n = strlen(last);
        while (n > 0 && (last[n - 1] == '\r' || last[n - 1] == ' ')) {
            last[--n] = '\0';
        }
        if (n > 0) snprintf(out, (size_t)cap, "%s", last);
        else snprintf(out, (size_t)cap, "见日志 cfg/%s", HSF_WEB_LOG_NAME);
    } else {
        snprintf(out, (size_t)cap, "见日志 cfg/%s", HSF_WEB_LOG_NAME);
    }
}

/* ==================== 设置读写 ==================== */

bool HsfWebServer_enabled(void)
{
    return HsfSettingsStore_getInt(HSF_WEB_KEY_ENABLED, 1) != 0;
}

int HsfWebServer_port(void)
{
    int port = HsfSettingsStore_getInt(HSF_WEB_KEY_PORT,
                                       HSF_WEB_DEFAULT_PORT);
    if (port < HSF_WEB_MIN_PORT) port = HSF_WEB_MIN_PORT;
    if (port > HSF_WEB_MAX_PORT) port = HSF_WEB_MAX_PORT;
    return port;
}

const char* HsfWebServer_url(void)
{
    snprintf(s_url, sizeof(s_url), "http://127.0.0.1:%d",
             HsfWebServer_port());
    return s_url;
}

/* ==================== 起停 ==================== */

bool HsfWebServer_running(void)
{
    if (!s_proc) return false;
    return XProcess_state(s_proc) != XProcessState_NotRunning;
}

bool HsfWebServer_start(void)
{
#if defined(__ANDROID__)
    /* 安卓没有外部进程与 python ⇒ 直接说不支持，不做假动作。 */
    snprintf(s_err, sizeof(s_err), "安卓端不支持网页服务");
    return false;
#else
    const char* envPy = XSystem_environment(HSF_WEB_ENV_PYTHON);
    const char* cands[3];
    const char* argv[10];
    char dir[512];
    char host[128];
    char logPath[600];
    char portArg[16];
    char webArg[16];
    int upstreamPort = HSF_WEB_DEFAULT_UP;
    int webPort = HsfWebServer_port();
    int ncand = 0;
    int argc = 0;
    int i;

    if (HsfWebServer_running()) return true;

    s_err[0] = '\0';
    s_exited = false;

    /* ① 网页端包目录 */
    if (!webFindPackageDir(dir, (int)sizeof(dir))) {
        snprintf(s_err, sizeof(s_err),
                 "找不到网页端包目录（放到程序目录 webui_py/ 下）");
        XPrintf("HsfWebServer: %s（程序目录 %s）\n", s_err, webAppDir());
        return false;
    }
    /* ② python 解释器候选（指定了就只用它；否则依次试 python/python3） */
    if (envPy && envPy[0]) {
        cands[ncand++] = envPy;
    } else {
        cands[ncand++] = "python";
        cands[ncand++] = "python3";
    }
    /* ③ 上游（客户端连的那台） */
    webUpstream(host, (int)sizeof(host), &upstreamPort);
    snprintf(portArg, sizeof(portArg), "%d", upstreamPort);
    snprintf(webArg, sizeof(webArg), "%d", webPort);
    argc = 0;
    argv[argc++] = "-m";
    argv[argc++] = "hsfweb";
    argv[argc++] = "--server";
    argv[argc++] = host;
    argv[argc++] = "--port";
    argv[argc++] = portArg;
    argv[argc++] = "--web-port";
    argv[argc++] = webArg;

    /* ④ 逐个候选解释器试（起不来换下一个；全不行才报失败） */
    for (i = 0; i < ncand; ++i) {
        XProcess* proc = XProcess_create();
        const char* python = cands[i];
        if (!proc) {
            snprintf(s_err, sizeof(s_err), "进程对象创建失败");
            return false;
        }
        (void)XProcess_setWorkingDirectory_utf8(proc, dir);
        /* stdout/stderr 落日志（重定向失败不致命：子进程照跑，输出进控制台）。
         * [stdout 与 stderr 必须两个文件] 框架的文件打开用
         * FILE_SHARE_READ（**不共享写**）⇒ 同一个文件开两次（stdout+stderr
         * 各一次）第二次必失败 ⇒ 重定向句柄为空 ⇒ 子进程根本起不来，报
         * 「Failed to start process」（2026-11-14 实锅：起不来的真因是
         * 同文件开两次，不是 python 找不到）。 */
        webLogPath(logPath, (int)sizeof(logPath), HSF_WEB_LOG_NAME);
        (void)XProcess_setStandardOutputFile_utf8(proc, logPath, true);
        webLogPath(logPath, (int)sizeof(logPath), HSF_WEB_LOG_ERR);
        (void)XProcess_setStandardErrorFile_utf8(proc, logPath, true);
        if (XProcess_start_utf8(proc, python, argv, (size_t)argc,
                                XIODevice_ReadOnly)) {
            s_proc = proc;
            XPrintf("HsfWebServer: 网页服务已拉起（%s -m hsfweb --server %s "
                    "--port %s --web-port %s，目录 %s）\n",
                    python, host, portArg, webArg, dir);
            return true;
        }
        {
            /* 起不来：把后端的错误串记下来（认不出就说是解释器问题）。 */
            const XString* es = XProcess_errorString_const(proc);
            const char* esu = es ? XString_toUtf8(es) : "";
            if (esu && esu[0])
                snprintf(s_err, sizeof(s_err), "拉起失败（%s）：%s", python,
                         esu);
            else
                snprintf(s_err, sizeof(s_err),
                         "拉起失败：找不到可用的 python 解释器（试过 %s）",
                         python);
            XPrintf("HsfWebServer: %s（目录 %s）\n", s_err, dir);
        }
        XClassDelete((XClass*)proc);
    }
    return false;
#endif
}

void HsfWebServer_stop(void)
{
    if (!s_proc) return;
    XProcess_kill(s_proc);
    (void)XProcess_waitForFinished(s_proc, 2000);
    XClassDelete((XClass*)s_proc);
    s_proc = NULL;
    s_exited = false;
    XPrintf("HsfWebServer: 网页服务已停止\n");
}

bool HsfWebServer_restart(void)
{
    HsfWebServer_stop();
    s_err[0] = '\0';
    return HsfWebServer_start();
}

void HsfWebServer_poll(void)
{
    if (!s_proc) return;
    (void)XProcess_poll(s_proc, 0);
    if (XProcess_state(s_proc) == XProcessState_NotRunning && !s_exited) {
        s_exited = true;
        s_exitCode = XProcess_exitCode(s_proc);
        {
            char why[160];
            webClassifyFailure(why, (int)sizeof(why), HsfWebServer_port());
            snprintf(s_err, sizeof(s_err), "%s", why);
        }
        XPrintf("HsfWebServer: 网页服务已退出（码 %d）：%s\n", s_exitCode,
                s_err);
        /* 子进程没了 ⇒ 释放句柄，等下次 start 重新拉。 */
        XClassDelete((XClass*)s_proc);
        s_proc = NULL;
    }
}

const char* HsfWebServer_status(char* buf, int cap)
{
    if (!buf || cap <= 0) return "";
    if (HsfWebServer_running()) {
        snprintf(buf, (size_t)cap, "运行中 · %s", HsfWebServer_url());
    } else if (s_err[0]) {
        snprintf(buf, (size_t)cap, "%s", s_err);
    } else {
        snprintf(buf, (size_t)cap, "已停止");
    }
    return buf;
}

/* ==================== 设置改动 ⇒ 即时生效 ==================== */

void HsfWebServer_setEnabled(bool on)
{
    HsfSettingsStore_setInt(HSF_WEB_KEY_ENABLED, on ? 1 : 0);
    HsfSettingsStore_sync();
    if (on) {
        (void)HsfWebServer_start();
    } else {
        HsfWebServer_stop();
        s_err[0] = '\0';
    }
}

void HsfWebServer_setPort(int port)
{
    if (port < HSF_WEB_MIN_PORT) port = HSF_WEB_MIN_PORT;
    if (port > HSF_WEB_MAX_PORT) port = HSF_WEB_MAX_PORT;
    HsfSettingsStore_setInt(HSF_WEB_KEY_PORT, port);
    HsfSettingsStore_sync();
    /* 端口变了要重启子进程才换监听口（在跑就重启）。 */
    if (HsfWebServer_running() || s_exited) {
        HsfWebServer_stop();
        s_err[0] = '\0';
        (void)HsfWebServer_start();
    }
}
