/**
 * @file        ftp_e2e_test.c
 * @brief       XFtp 端到端真服务器联调测试（独立程序，不走菜单）
 * @note        跨平台：使用 XinYueC 自带 API，零平台 API
 *              使用：先启动 ftp_test_server.py
 *                    然后运行 FtpE2E_Test.exe
 */

#include "XFtp.h"
#include "XTest.h"
#include "XFile.h"
#include "XThread.h"
#include "XCoreApplication.h"
#include "XString.h"
#include "XByteArray.h"
#include "XObject.h"
#include "XClass.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FTP_TEST_HOST   "127.0.0.1"
#define FTP_TEST_PORT   2121
#define FTPS_TEST_PORT  2122
#define FTP_TEST_USER   "u1"
#define FTP_TEST_PASS   "p1"

static bool s_use_ssl = false;
static int s_test_port = FTP_TEST_PORT;

static int s_cmd_finished = 0;
static int s_cmd_finished_id = 0;
static bool s_cmd_finished_error = false;
static int s_waiting_for_id = 0;
static int s_listing_count = 0;
static int s_total_size = 0;

/* 捕获最近的 XFileInfo（MLST/MLSD 单条用） */
static XFileInfo* s_last_info = NULL;
static int64_t s_last_info_size = 0;
static int s_last_info_isDir = 0;

/* 捕获最近的 rawCommandReply（SIZE/MDTM 用） */
static int s_last_raw_code = 0;
static char s_last_raw_text[256] = {0};

static void on_commandFinished(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args) return;
    XVarList_start(args);
    int id = XVarList_arg(args, int);
    bool error = XVarList_arg(args, bool);
    s_cmd_finished_id = id;
    if (s_waiting_for_id == 0 || s_waiting_for_id == id) {
        s_cmd_finished_error = error;
        s_cmd_finished = 1;
    }
}

static void on_listInfo(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args) return;
    XVarList_start(args);
    XFileInfo* info = XVarList_arg(args, XFileInfo*);
    s_listing_count++;
    if (info && info->m_stat.size > 0) s_total_size += (int)info->m_stat.size;
    /* 缓存最近一条：MLST 单条查询用 */
    if (info) {
        s_last_info = info;
        s_last_info_size = info->m_stat.size;
        s_last_info_isDir = info->m_stat.isDir;
    }
}

static void on_dataTransferProgress(XFtp* ftp, int64_t current, int64_t total)
{
    (void)ftp; (void)current; (void)total;
}

/* rawCommandReply 信号是 2 个参数（int code, const char* text），
 * 走 XSlotFunc1：从 XVarList 提取 */
static void on_rawCommandReply(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args) return;
    XVarList_start(args);
    s_last_raw_code = XVarList_arg(args, int);
    const char* text = XVarList_arg(args, const char*);
    if (text) {
        size_t n = strlen(text);
        if (n >= sizeof(s_last_raw_text)) n = sizeof(s_last_raw_text) - 1;
        memcpy(s_last_raw_text, text, n);
        s_last_raw_text[n] = '\0';
    } else {
        s_last_raw_text[0] = '\0';
    }
}

/* 等待命令完成信号。expected_id: >0 只接受该 id，0 接受任意 */
static bool wait_cmd(int timeout_ms, int expected_id)
{
    int waited = 0;
    s_cmd_finished = 0;  /* 清掉残留信号 */
    if (expected_id > 0) s_waiting_for_id = expected_id;
    while (!s_cmd_finished && waited < timeout_ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(50);
        waited += 50;
    }
    bool ok = s_cmd_finished != 0;
    s_cmd_finished = 0;
    s_cmd_finished_error = false;
    s_cmd_finished_id = 0;
    s_waiting_for_id = 0;
    return ok;
}

static bool wait_state(XFtp* ftp, XFtp_State target, int timeout_ms)
{
    int waited = 0;
    while (XFtp_state(ftp) != target && waited < timeout_ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(50);
        waited += 50;
    }
    return XFtp_state(ftp) == target;
}

static bool connect_and_login(XFtp* ftp)
{
    char url[128];
    snprintf(url, sizeof(url), "ftp://%s:%u/", FTP_TEST_HOST, (unsigned)s_test_port);
    int id = XFtp_connectToUrl(ftp, url);
    if (id < 0) return false;
    if (!wait_cmd(5000, id)) return false;
    int loginId = XFtp_login(ftp, FTP_TEST_USER, FTP_TEST_PASS);
    if (loginId < 0) return false;
    return wait_state(ftp, XFtp_State_LoggedIn, 5000);
}

typedef struct {
    const char* name;
    bool (*run)(XFtp* ftp);
    bool passed;
} TestCase;

static bool t_connect(XFtp* ftp)
{
    /* 已在主循环连好，直接验证状态 */
    XCoreApplication_processEvents(0);
    XThread_msleep(100);
    XCoreApplication_processEvents(0);
    return XFtp_state(ftp) == XFtp_State_LoggedIn;
}

static bool t_feat(XFtp* ftp)
{
    /* FEAT 在连接握手时自动发，等 1 秒让响应回来 */
    for (int i = 0; i < 20; i++) {
        XCoreApplication_processEvents(0);
        XThread_msleep(50);
    }
    bool utf8 = XFtp_supportsFeature(ftp, XFtp_Feature_UTF8);
    bool mlsd = XFtp_supportsFeature(ftp, XFtp_Feature_MLSD);
    bool epsv = XFtp_supportsFeature(ftp, XFtp_Feature_EPSV);
    XPrintf("    [协商结果: UTF8=%d MLSD=%d EPSV=%d]\n", utf8, mlsd, epsv);
    return utf8 && mlsd && epsv;
}

static bool t_list(XFtp* ftp)
{
    const char* fixture = "xftp_list_fixture.txt";
    int fixtureId = XFtp_put(ftp, fixture, "list fixture", 12);
    if (fixtureId < 0 || !wait_cmd(5000, fixtureId)) return false;

    s_listing_count = 0;
    s_total_size = 0;
    int id = XFtp_list(ftp, ".");
    if (id < 0 || !wait_cmd(5000, id)) {
        id = XFtp_remove(ftp, fixture);
        if (id >= 0) wait_cmd(2000, id);
        return false;
    }
    XPrintf("    [列出 %d 项，总大小 %d 字节]\n", s_listing_count, s_total_size);
    bool ok = s_listing_count > 0;
    id = XFtp_remove(ftp, fixture);
    if (id >= 0) wait_cmd(2000, id);
    return ok;
}

static bool t_cd_pwd(XFtp* ftp)
{
    int id = XFtp_cd(ftp, ".");
    if (id < 0) return false;
    if (!wait_cmd(3000, id)) return false;
    id = XFtp_cdup(ftp);
    if (id < 0) return false;
    return wait_cmd(3000, id);
}

static bool t_mkdir_rmdir(XFtp* ftp)
{
    int id = XFtp_mkdir(ftp, "xftp_e2e_dir");
    if (id < 0) return false;
    if (!wait_cmd(3000, id)) return false;
    id = XFtp_rmdir(ftp, "xftp_e2e_dir");
    if (id < 0) return false;
    return wait_cmd(3000, id);
}

static bool t_put_get(XFtp* ftp)
{
    const char* data = "Hello XFtp E2E test! 这是一段测试数据。";
    int sz = (int)strlen(data);
    int id = XFtp_put(ftp, "xftp_test.txt", data, sz);
    if (id < 0) return false;
    if (!wait_cmd(5000, id)) return false;
    XPrintf("    [PUT 上传 %d 字节 OK]\n", sz);
    id = XFtp_get(ftp, "xftp_test.txt", NULL, 0);
    if (id < 0) return false;
    if (!wait_cmd(5000, id)) return false;
    XPrintf("    [GET 下载 OK]\n");
    return true;
}

static bool t_rename_remove(XFtp* ftp)
{
    int id = XFtp_put(ftp, "xftp_old.txt", "data", 4);
    if (id < 0) return false;
    if (!wait_cmd(3000, id)) return false;
    id = XFtp_rename(ftp, "xftp_old.txt", "xftp_new.txt");
    if (id < 0) return false;
    if (!wait_cmd(3000, id)) return false;
    id = XFtp_remove(ftp, "xftp_new.txt");
    if (id < 0) return false;
    return wait_cmd(3000, id);
}

static bool t_raw(XFtp* ftp)
{
    int id = XFtp_rawCommand(ftp, "PWD");
    if (id < 0) return false;
    if (!wait_cmd(3000, id)) return false;
    id = XFtp_rawCommand(ftp, "SYST");
    if (id < 0) return false;
    if (!wait_cmd(3000, id)) return false;
    id = XFtp_rawCommand(ftp, "NOOP");
    if (id < 0) return false;
    return wait_cmd(3000, id);
}

static bool t_resume(XFtp* ftp)
{
    /* 上传 48 字节已知内容 */
    const char* data = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789AB";
    int sz = (int)strlen(data);  /* 48 */
    int id = XFtp_put(ftp, "xftp_resume.txt", data, sz);
    if (id < 0) return false;
    if (!wait_cmd(5000, id)) return false;

    /* 从 offset=10 断点续传下载到本地 XFile（作为 device）*/
    int64_t offset = 10;
    XString* lfname = XString_create_utf8("xftp_resume_local.bin");
    XFile* wf = XFile_create_2(lfname);
    if (!wf) { XString_delete_base(lfname); return false; }
    if (!XIODevice_open_base((XIODevice*)wf,
                             XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create)) {
        XClass_delete_base((XClass*)wf);
        XString_delete_base(lfname);
        return false;
    }

    id = XFtp_get_resume(ftp, "xftp_resume.txt", wf, offset, 0);
    bool got = (id >= 0) && wait_cmd(5000, id);
    XIODevice_close_base((XIODevice*)wf);
    if (!got) {
        XClass_delete_base((XClass*)wf);
        XString_delete_base(lfname);
        return false;
    }

    /* 读回本地文件，验证内容 == data[offset..]（续传只应得到 offset 之后的字节）*/
    XFile* rf = XFile_create_2(lfname);
    bool ok = false;
    if (rf && XIODevice_open_base((XIODevice*)rf, XIODevice_ReadOnly)) {
        XByteArray* content = XIODevice_readAll_3((XIODevice*)rf);
        if (content) {
            int64_t gotsz = XByteArray_size_base(content);
            int64_t expect = sz - offset;  /* 38 */
            ok = (gotsz == expect) &&
                 (memcmp(XByteArray_data(content), data + offset, (size_t)expect) == 0);
            XPrintf("    [断点续传 offset=%lld: 期望 %lld 字节，实得 %lld 字节，内容%s]\n",
                (long long)offset, (long long)expect, (long long)gotsz,
                ok ? "正确" : "错误");
            XByteArray_delete_base(content);
        }
        XIODevice_close_base((XIODevice*)rf);
    }
    if (rf) XClass_delete_base((XClass*)rf);
    XClass_delete_base((XClass*)wf);
    XString_delete_base(lfname);

    /* 清理远程文件 */
    id = XFtp_remove(ftp, "xftp_resume.txt");
    if (id >= 0) wait_cmd(2000, id);

    return ok;
}

/* 下载远程文件到内存并校验内容，成功返回 true */
static bool download_verify(XFtp* ftp, const char* remote,
                            const char* expect, int64_t expectLen)
{
    /* 用唯一文件名避免前次残留导致 XFile 打开/写入行为异常 */
    static int s_dl_seq = 0;
    char nameBuf[64];
    snprintf(nameBuf, sizeof(nameBuf), "xftp_dl_%d.bin", s_dl_seq++);
    XString* lfn = XString_create_utf8(nameBuf);
    XFile* wf = XFile_create_2(lfn);
    if (!wf) { XString_delete_base(lfn); return false; }
    if (!XIODevice_open_base((XIODevice*)wf,
                             XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create)) {
        XClass_delete_base((XClass*)wf); XString_delete_base(lfn); return false;
    }
    int id = XFtp_get(ftp, remote, wf, 0);
    bool got = (id >= 0) && wait_cmd(30000, id);
    XIODevice_close_base((XIODevice*)wf);
    if (!got) {
        XClass_delete_base((XClass*)wf); XString_delete_base(lfn); return false;
    }

    XFile* rf = XFile_create_2(lfn);
    bool ok = false;
    if (rf && XIODevice_open_base((XIODevice*)rf, XIODevice_ReadOnly)) {
        XByteArray* c = XIODevice_readAll_3((XIODevice*)rf);
        if (c) {
            int64_t gotsz = XByteArray_size_base(c);
            ok = (gotsz == expectLen) &&
                 (memcmp(XByteArray_data(c), expect, (size_t)expectLen) == 0);
            XByteArray_delete_base(c);
        }
        XIODevice_close_base((XIODevice*)rf);
    }
    if (rf) XClass_delete_base((XClass*)rf);
    XClass_delete_base((XClass*)wf);
    XString_delete_base(lfn);
    return ok;
}

/* APPE 追加上传：先 STOR 写 "Hello"，再 APPE 追加 " World"，校验拼接结果 */
static bool t_appe(XFtp* ftp)
{
    const char* part1 = "Hello";
    const char* part2 = " World";
    int id = XFtp_put(ftp, "xftp_appe.txt", part1, (int64_t)strlen(part1));
    if (id < 0 || !wait_cmd(5000, id)) return false;
    id = XFtp_put_append(ftp, "xftp_appe.txt", part2, (int64_t)strlen(part2));
    if (id < 0 || !wait_cmd(5000, id)) return false;

    const char* expect = "Hello World";
    bool ok = download_verify(ftp, "xftp_appe.txt", expect, (int64_t)strlen(expect));
    XPrintf("    [APPE 追加上传: %s]\n", ok ? "拼接正确" : "内容错误");

    id = XFtp_remove(ftp, "xftp_appe.txt");
    if (id >= 0) wait_cmd(2000, id);
    return ok;
}

/* PORT 主动模式：客户端监听、服务器回连。切到 Active 做 GET，再切回 Passive */
static bool t_port_active(XFtp* ftp)
{
    if (s_use_ssl) {
        XFtp_setTransferMode(ftp, XFtp_TransferMode_Active);
        int id = XFtp_list(ftp, ".");
        bool waited = (id >= 0) && wait_cmd(5000, id);
        XFtp_Error error = XFtp_error(ftp);
        const char* errorText = XFtp_errorString(ftp);
        /* 当前跨平台实现明确拒绝 FTPS 主动数据通道；错误可能同步产生，
         * 因此不能要求 commandFinished 一定先于 wait_cmd 返回。 */
        bool rejected = id >= 0 && error == XFtp_Error_ActiveModeFailed;
        XFtp_setTransferMode(ftp, XFtp_TransferMode_Passive);
        XPrintf("    [主动 FTPS 按契约拒绝: id=%d wait=%d error=%d text=%s %s]\n",
                id, waited ? 1 : 0, error, errorText,
                rejected ? "OK" : "失败");
        return rejected;
    }

    const char* data = "Active mode PORT test data 主动模式测试";
    int64_t sz = (int64_t)strlen(data);
    /* 先用被动模式上传一个文件 */
    int id = XFtp_put(ftp, "xftp_port.txt", data, sz);
    if (id < 0 || !wait_cmd(5000, id)) return false;

    /* 切到主动模式 */
    XFtp_setTransferMode(ftp, XFtp_TransferMode_Active);

    /* 主动模式下列目录（验证 PORT 数据通路）*/
    s_listing_count = 0;
    id = XFtp_list(ftp, ".");
    bool listOk = (id >= 0) && wait_cmd(6000, id);

    /* 主动模式下 GET 并校验内容 */
    bool getOk = download_verify(ftp, "xftp_port.txt", data, sz);

    /* 切回被动模式 */
    XFtp_setTransferMode(ftp, XFtp_TransferMode_Passive);

    XPrintf("    [主动模式 LIST=%s, GET=%s]\n", listOk ? "OK" : "失败", getOk ? "OK" : "失败");

    id = XFtp_remove(ftp, "xftp_port.txt");
    if (id >= 0) wait_cmd(2000, id);
    return listOk && getOk;
}

/* 大文件分块传输：256KB 数据，远超单次缓冲，验证多块上传/下载完整性 */
static bool t_large_file(XFtp* ftp)
{
    const int64_t N = 256 * 1024;  /* 256KB */
    char* buf = (char*)malloc((size_t)N);
    if (!buf) return false;
    for (int64_t i = 0; i < N; i++) buf[i] = (char)((i * 31 + 7) & 0xFF);

    int id = XFtp_put(ftp, "xftp_large.bin", buf, N);
    bool putOk = (id >= 0) && wait_cmd(15000, id);
    if (!putOk) { free(buf); return false; }

    bool getOk = download_verify(ftp, "xftp_large.bin", buf, N);
    XPrintf("    [大文件 %lld KB: PUT=%s, GET 校验=%s]\n",
        (long long)(N / 1024), putOk ? "OK" : "失败", getOk ? "OK" : "失败");

    free(buf);
    id = XFtp_remove(ftp, "xftp_large.bin");
    if (id >= 0) wait_cmd(2000, id);
    return getOk;
}

/* ABOR 中断传输：发起下载后立即中断，验证连接仍可继续使用 */
static bool t_abor(XFtp* ftp)
{
    /* 准备一个稍大的文件，让传输有窗口被中断 */
    const int64_t N = 64 * 1024;
    char* buf = (char*)malloc((size_t)N);
    if (!buf) return false;
    memset(buf, 0x5A, (size_t)N);
    int id = XFtp_put(ftp, "xftp_abor.bin", buf, N);
    free(buf);
    if (id < 0 || !wait_cmd(8000, id)) return false;

    /* 发起 GET，立即中断传输 */
    id = XFtp_get(ftp, "xftp_abor.bin", NULL, 0);
    XCoreApplication_processEvents(0);
    XFtp_abortTransfer(ftp);

    /* 让事件循环处理中断收尾 */
    for (int i = 0; i < 40; i++) {
        XCoreApplication_processEvents(0);
        XThread_msleep(50);
    }

    /* 中断后连接应仍可用：发 PWD 应正常返回 */
    id = XFtp_rawCommand(ftp, "PWD");
    bool recoverOk = (id >= 0) && wait_cmd(4000, id);
    XPrintf("    [ABOR 中断后连接%s]\n", recoverOk ? "仍可用" : "已损坏");

    id = XFtp_remove(ftp, "xftp_abor.bin");
    if (id >= 0) wait_cmd(2000, id);
    return recoverOk;
}

/* ===== 14: SIZE 文件大小查询（RFC 3659） ===== */
static bool t_size(XFtp* ftp)
{
    /* 先用 PUT 创建一个已知大小的文件 */
    const char* data = "Hello, World! This is 50 bytes long.........";  /* 50 bytes */
    int64_t sz = (int64_t)strlen(data);
    int id = XFtp_put(ftp, "xftp_size.txt", data, sz);
    if (id < 0 || !wait_cmd(5000, id)) return false;

    /* 查 SIZE */
    s_last_raw_code = 0;
    s_last_raw_text[0] = '\0';
    id = XFtp_size(ftp, "xftp_size.txt");
    if (id < 0 || !wait_cmd(3000, id)) return false;

    /* 解析 "213 <size>" */
    int parsedSize = 0;
    bool ok = (s_last_raw_code == 213);
    if (ok) {
        const char* sp = strchr(s_last_raw_text, ' ');
        if (sp) parsedSize = atoi(sp + 1);
        ok = (parsedSize == (int)sz);
    }
    XPrintf("    [SIZE: 期望 %lld 字节，实得 %d 字节（213 %s）]\n",
        (long long)sz, parsedSize, s_last_raw_text);
    int rid = XFtp_remove(ftp, "xftp_size.txt");
    if (rid >= 0) wait_cmd(2000, rid);
    return ok;
}

/* ===== 15: MDTM 文件修改时间（RFC 3659） ===== */
static bool t_mdtm(XFtp* ftp)
{
    const char* data = "MDTM test file";
    int64_t sz = (int64_t)strlen(data);
    int id = XFtp_put(ftp, "xftp_mdtm.txt", data, sz);
    if (id < 0 || !wait_cmd(5000, id)) return false;

    s_last_raw_code = 0;
    s_last_raw_text[0] = '\0';
    id = XFtp_mdtm(ftp, "xftp_mdtm.txt");
    if (id < 0 || !wait_cmd(3000, id)) return false;

    /* 解析 "213 YYYYMMDDHHMMSS"（14 位） */
    bool ok = (s_last_raw_code == 213);
    const char* ts = strchr(s_last_raw_text, ' ');
    if (ts) ts++;
    else ts = s_last_raw_text;
    if (ok) {
        int len = (int)strlen(ts);
        ok = (len == 14);
        for (int i = 0; ok && i < 14; i++) {
            if (ts[i] < '0' || ts[i] > '9') ok = false;
        }
    }
    XPrintf("    [MDTM: 213 %s %s]\n", ts, ok ? "格式正确" : "格式错误");
    int rid = XFtp_remove(ftp, "xftp_mdtm.txt");
    if (rid >= 0) wait_cmd(2000, rid);
    return ok;
}

/* ===== 16: MLST 单文件元信息（RFC 3659） ===== */
static bool t_mlst(XFtp* ftp)
{
    const char* data = "MLST test payload 1234567890";
    int64_t sz = (int64_t)strlen(data);
    int id = XFtp_put(ftp, "xftp_mlst.txt", data, sz);
    if (id < 0 || !wait_cmd(5000, id)) return false;

    s_last_info = NULL;
    s_last_info_size = 0;
    s_last_info_isDir = 0;
    id = XFtp_mlst(ftp, "xftp_mlst.txt");
    if (id < 0 || !wait_cmd(3000, id)) return false;

    bool ok = (s_last_info != NULL);
    if (ok) {
        /* MLST 应返回 size 和 type=file */
        ok = (s_last_info_size == sz) && (s_last_info_isDir == 0);
    }
    XPrintf("    [MLST: size=%lld 期望 %lld, isDir=%d %s]\n",
        (long long)s_last_info_size, (long long)sz, s_last_info_isDir, ok ? "匹配" : "不匹配");
    int rid = XFtp_remove(ftp, "xftp_mlst.txt");
    if (rid >= 0) wait_cmd(2000, rid);
    return ok;
}

/* ===== 17: 错误码细化（4xx/5xx → 具体 Error 码） ===== */
static bool t_error_codes(XFtp* ftp)
{
    /* 530 Not logged in：先用一个未登录的临时连接验证
     * 由于整个测试共享一个 ftp，测 530 不现实，改测 550（文件不存在） */

    /* 测 1：SIZE 不存在的文件 → 550 → XFtp_Error_CommandFailed */
    s_last_raw_code = 0;
    int id = XFtp_size(ftp, "xftp_does_not_exist_xyz.txt");
    if (id < 0 || !wait_cmd(3000, id)) return false;
    bool ok1 = (s_last_raw_code == 550) && (XFtp_error(ftp) == XFtp_Error_CommandFailed);
    XPrintf("    [550 不存在: code=%d err=%d %s]\n",
        s_last_raw_code, XFtp_error(ftp), ok1 ? "OK" : "错");

    /* 测 2：DELE 不存在的文件 → 550 */
    s_last_raw_code = 0;
    id = XFtp_remove(ftp, "xftp_does_not_exist_xyz.txt");
    if (id < 0 || !wait_cmd(3000, id)) return false;
    bool ok2 = (s_last_raw_code == 550) && (XFtp_error(ftp) == XFtp_Error_CommandFailed);
    XPrintf("    [550 DELE 不存在: code=%d err=%d %s]\n",
        s_last_raw_code, XFtp_error(ftp), ok2 ? "OK" : "错");

    /* 测 3：RMD 不存在的目录 → 550 */
    s_last_raw_code = 0;
    id = XFtp_rmdir(ftp, "xftp_does_not_exist_xyz_dir");
    if (id < 0 || !wait_cmd(3000, id)) return false;
    bool ok3 = (s_last_raw_code == 550) && (XFtp_error(ftp) == XFtp_Error_CommandFailed);
    XPrintf("    [550 RMD 不存在: code=%d err=%d %s]\n",
        s_last_raw_code, XFtp_error(ftp), ok3 ? "OK" : "错");

    /* 测 4：MKD 已存在目录 → 550 */
    s_last_raw_code = 0;
    id = XFtp_mkdir(ftp, "xftp_err_mk");
    if (id < 0 || !wait_cmd(2000, id)) return false;
    /* 这次应该成功（257） */
    bool mkok = (s_last_raw_code == 257);
    s_last_raw_code = 0;
    id = XFtp_mkdir(ftp, "xftp_err_mk");
    if (id < 0 || !wait_cmd(2000, id)) return false;
    bool ok4 = mkok && (s_last_raw_code == 550) && (XFtp_error(ftp) == XFtp_Error_CommandFailed);
    XPrintf("    [550 MKD 已存在: code=%d err=%d %s]\n",
        s_last_raw_code, XFtp_error(ftp), ok4 ? "OK" : "错");

    /* 清理 */
    int rid = XFtp_rmdir(ftp, "xftp_err_mk");
    if (rid >= 0) wait_cmd(2000, rid);
    return ok1 && ok2 && ok3 && ok4;
}

int main(int argc, char* argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ssl") == 0) {
            s_use_ssl = true;
            s_test_port = FTPS_TEST_PORT;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            s_test_port = atoi(argv[++i]);
        }
    }

    XCoreApplication_create(argc, argv);
    XPrintf("=== XFtp 端到端真服务器联调测试 ===\n");
    XPrintf("服务器: %s:%d%s\n", FTP_TEST_HOST, s_test_port,
            s_use_ssl ? " (Explicit FTPS)" : "");
    XPrintf("用户: %s / %s\n\n", FTP_TEST_USER, FTP_TEST_PASS);

    XFtp* ftp = XFtp_create();
    if (!ftp) {
        XPrintf("[致命] XFtp_create 失败\n");
        return 1;
    }
    if (s_use_ssl) {
        XFtp_setSsl(ftp, true);
        /* 测试服务器使用临时自签名证书；仍验证 TLS 握手和加密 IO。 */
        XFtp_setSslPeerVerifyMode(ftp, XSSL_VerifyNone);
    }
    XObject_connect_1((XObject*)ftp, XFtp_commandFinished_signal,
                      (XObject*)ftp, on_commandFinished, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp, XFtp_listInfo_signal,
                      (XObject*)ftp, on_listInfo, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp, XFtp_dataTransferProgress_signal,
                      (XObject*)ftp, on_dataTransferProgress, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp, XFtp_rawCommandReply_signal,
                      (XObject*)ftp, on_rawCommandReply, XConnectionType_Direct);

    TestCase tests[] = {
        { "连接+登录+状态机",       t_connect,       false },
        { "FEAT 协商",              t_feat,          false },
        { "LIST/MLSD 列表",         t_list,          false },
        { "CWD/CDUP",               t_cd_pwd,        false },
        { "MKDIR/RMDIR",            t_mkdir_rmdir,   false },
        { "PUT 上传+GET 下载",      t_put_get,       false },
        { "断点续传 REST",          t_resume,        false },
        { "APPE 追加上传",          t_appe,          false },
        { "PORT 主动模式",          t_port_active,   false },
        { "大文件分块传输",         t_large_file,    false },
        { "ABOR 中断传输",          t_abor,          false },
        { "RENAME/REMOVE",          t_rename_remove, false },
        { "原始命令 PWD/SYST/NOOP", t_raw,           false },
        { "SIZE 文件大小查询",      t_size,          false },
        { "MDTM 修改时间查询",      t_mdtm,          false },
        { "MLST 单文件元信息",      t_mlst,          false },
        { "错误码细化 4xx/5xx",     t_error_codes,   false },
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0]));

    /* 先连一次 */
    XPrintf("[0/%d] 准备：连接+登录...\n", n);
    bool baseOk = connect_and_login(ftp);
    if (!baseOk) {
        XPrintf("[致命] 连接或登录失败，请检查 ftp_test_server.py 是否运行\n");
        XClass_delete_base((XClass*)ftp);
        XCoreApplication_delete_base(XCoreApplication_instance());
        return 2;
    }
    XPrintf("    [登录成功，state=%d]\n", XFtp_state(ftp));

    int passCnt = 0;
    for (int i = 0; i < n; i++) {
        XPrintf("[%d/%d] %s ... ", i + 1, n, tests[i].name);
        bool ok = tests[i].run(ftp);
        tests[i].passed = ok;
        if (ok) passCnt++;
        XPrintf("%s\n", ok ? "[通过]" : "[失败]");
    }

    XPrintf("\n=== 结果汇总 ===\n");
    for (int i = 0; i < n; i++) {
        XPrintf("  %s %s\n", tests[i].passed ? "[通过]" : "[失败]", tests[i].name);
    }
    XPrintf("\n通过率: %d / %d\n", passCnt, n);

    XClass_delete_base((XClass*)ftp);
    XCoreApplication_delete_base(XCoreApplication_instance());
    return passCnt == n ? 0 : 1;
}
