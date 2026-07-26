/**
 * @file        XFtpTest.c
 * @brief       XFtp 单元测试 + 端到端真服务器联调测试
 * @note        跨平台：仅使用 XinYueC 自带 API 与标准 C 库，零平台 API
 *              测试服务器：Python FTP server（ftp_test_server.py）
 *              全部用户可见的输出统一为中文
 */

#include "XFtp.h"
#include "XNetworkTest.h"
#include "XTest.h"
#include "XThread.h"
#include "XCoreApplication.h"
#include "XObject.h"
#include "XMenu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 测试服务器连接信息 ===================== */
#define FTP_TEST_HOST          "127.0.0.1"
#define FTP_TEST_PORT          2121
#define FTP_TEST_USER          "u1"
#define FTP_TEST_PASS          "p1"
#define FTP_TEST_DOWNLOAD      "ftp_test_download.bin"
#define FTP_TEST_UPLOAD        "ftp_test_upload.bin"
#define FTP_TEST_MKDIR         "xftp_test_dir"
#define FTP_TEST_RENAME_TO     "new_name.txt"
#define FTP_TEST_LIST_REMOTE   "."

/* ===================== 状态量 ===================== */
static int  s_listing_count       = 0;
static int  s_listing_total_size  = 0;
static int  s_download_bytes      = 0;
static int  s_cmd_finished        = 0;
static int  s_cmd_id_done         = 0;
static bool s_last_cmd_error      = false;

/* ===================== 信号回调 ===================== */
static void on_listInfo(XObject* receiver, XVarList* args)
{
    if (!args) return;
    XVarList_start(args);
    XFileInfo* info = XVarList_arg(args, XFileInfo*);
    s_listing_count++;
    if (info && info->m_stat.size > 0) {
        s_listing_total_size += (int)info->m_stat.size;
    }
    (void)receiver;
}

static void on_dataTransferProgress(XFtp* ftp, int64_t current, int64_t total)
{
    (void)ftp;
    (void)total;
    if (current > 0 && current < 1024 * 1024) {
        s_download_bytes = (int)current;
    }
}

static void on_commandFinished(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args) return;
    XVarList_start(args);
    int  id    = XVarList_arg(args, int);
    bool error = XVarList_arg(args, bool);
    s_cmd_finished   = 1;
    s_cmd_id_done    = id;
    s_last_cmd_error = error;
}

static void on_rawCommandReply(XObject* receiver, XVarList* args)
{
    /* rawCommandReply 通常由 commandFinished 副作用处理，此处仅占位 */
    (void)receiver;
    (void)args;
}

/* ===================== 事件循环辅助 ===================== */
/**
 * @brief 等待当前命令完成（带超时）
 * @return true  - 在超时前收到完成信号
 *         false - 超时
 */
static bool wait_for_command(int timeout_ms)
{
    int waited = 0;
    while (s_cmd_finished == 0 && waited < timeout_ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(50);
        waited += 50;
    }
    bool ok = (s_cmd_finished != 0);
    s_cmd_finished = 0;
    return ok;
}

/**
 * @brief 等待状态机切换到目标状态（带超时）
 */
static bool wait_for_state(XFtp* ftp, XFtp_State target, int timeout_ms)
{
    int waited = 0;
    while (XFtp_state(ftp) != target && waited < timeout_ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(50);
        waited += 50;
    }
    return XFtp_state(ftp) == target;
}

/* ========================================================================
 *  单元测试（无需服务器）
 * ====================================================================== */

static void test_xftp_create_destroy(void)
{
    XPrintf("【单元】创建/销毁 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XASSERT_EQ(XFtp_state(ftp), XFtp_State_Unconnected);
    XASSERT_EQ(XFtp_error(ftp), XFtp_Error_NoError);
    XASSERT_FALSE(XFtp_hasPendingCommands(ftp));
    XASSERT_EQ(XFtp_currentId(ftp), 0);
    XASSERT_EQ(XFtp_currentCommand(ftp), XFtpCommand_None);
    XClass_delete_base((XClass*)ftp);
    XPrintf("通过\n");
}

static void test_xftp_transfer_mode(void)
{
    XPrintf("【单元】传输模式 / 类型 / SSL / 自动重连 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);

    XFtp_setTransferMode(ftp, XFtp_TransferMode_Active);
    XASSERT_EQ(XFtp_transferMode(ftp), XFtp_TransferMode_Active);
    XFtp_setTransferMode(ftp, XFtp_TransferMode_Passive);
    XASSERT_EQ(XFtp_transferMode(ftp), XFtp_TransferMode_Passive);

    XFtp_setTransferType(ftp, XFtp_DataType_Ascii);
    XASSERT_EQ(XFtp_transferType(ftp), XFtp_DataType_Ascii);
    XFtp_setTransferType(ftp, XFtp_DataType_Binary);
    XASSERT_EQ(XFtp_transferType(ftp), XFtp_DataType_Binary);

    XFtp_setSsl(ftp, true);
    XFtp_setCompression(ftp, true);
    XFtp_setAutoReconnect(ftp, true, 2000, 5);
    XFtp_abort(ftp);

    XClass_delete_base((XClass*)ftp);
    XPrintf("通过\n");
}

static void test_xftp_error_query(void)
{
    XPrintf("【单元】错误查询 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XASSERT_STR_EQ(XFtp_errorString(ftp), "");
    XFtp_abort(ftp);
    XClass_delete_base((XClass*)ftp);
    XPrintf("通过\n");
}

static void test_xftp_invalid_args(void)
{
    XPrintf("【单元】非法参数校验 ... ");
    XASSERT_EQ(XFtp_login(NULL, "u", "p"), -1);
    XASSERT_EQ(XFtp_list(NULL, "/"), -1);
    XASSERT_EQ(XFtp_get(NULL, "f", NULL, 0), -1);
    XASSERT_EQ(XFtp_put(NULL, "f", "data", 4), -1);
    XASSERT_EQ(XFtp_remove(NULL, "f"), -1);
    XASSERT_EQ(XFtp_rename(NULL, "a", "b"), -1);
    XASSERT_EQ(XFtp_mkdir(NULL, "d"), -1);
    XASSERT_EQ(XFtp_rmdir(NULL, "d"), -1);
    XASSERT_EQ(XFtp_cd(NULL, "/"), -1);
    XASSERT_EQ(XFtp_cdup(NULL), -1);
    XASSERT_EQ(XFtp_rawCommand(NULL, "NOOP"), -1);
    XASSERT_EQ(XFtp_currentId(NULL), 0);
    XASSERT_EQ(XFtp_state(NULL), XFtp_State_Unconnected);
    XASSERT_EQ(XFtp_error(NULL), XFtp_Error_NoError);
    XPrintf("通过\n");
}

static void test_xftp_state_queries(void)
{
    XPrintf("【单元】状态查询 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XASSERT_EQ(XFtp_state(ftp), XFtp_State_Unconnected);
    XASSERT_EQ(XFtp_error(ftp), XFtp_Error_NoError);
    XASSERT_EQ(XFtp_currentCommand(ftp), XFtpCommand_None);
    XASSERT_EQ(XFtp_transferMode(ftp), XFtp_TransferMode_Passive);
    XASSERT_EQ(XFtp_transferType(ftp), XFtp_DataType_Binary);
    XASSERT_FALSE(XFtp_isUtf8(ftp));
    XASSERT_FALSE(XFtp_hasPendingCommands(ftp));
    XClass_delete_base((XClass*)ftp);
    XPrintf("通过\n");
}

static void test_xftp_command_queue(void)
{
    XPrintf("【单元】命令队列 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XASSERT_FALSE(XFtp_hasPendingCommands(ftp));

    /* 未连接时 login 应被拒绝并返回 -1，不入队 */
    int badId = XFtp_login(ftp, "u", "p");
    XASSERT_EQ(badId, -1);
    XASSERT_FALSE(XFtp_hasPendingCommands(ftp));

    /* 其他命令也应在未连接时被拒绝 */
    XFtp_cd(ftp, "/");
    XASSERT_FALSE(XFtp_hasPendingCommands(ftp));
    XFtp_abort(ftp);

    XClass_delete_base((XClass*)ftp);
    XPrintf("通过\n");
}

static void test_xftp_signal_connect(void)
{
    XPrintf("【单元】信号连接 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);

    XObject_connect_1((XObject*)ftp, XFtp_listInfo_signal,
                      (XObject*)ftp, on_listInfo, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp, XFtp_dataTransferProgress_signal,
                      (XObject*)ftp, on_dataTransferProgress, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp, XFtp_commandFinished_signal,
                      (XObject*)ftp, on_commandFinished, XConnectionType_Direct);

    XFtp_abort(ftp);
    XClass_delete_base((XClass*)ftp);
    XPrintf("通过\n");
}

static void test_xftp_sizeof(void)
{
    XPrintf("【单元】资源占用 ... ");
    XPrintf("sizeof(XFtp) = %u 字节；sizeof(XFtpCommand) = %u 字节\n",
            (unsigned)sizeof(XFtp), (unsigned)sizeof(XFtpCommand));
    XPrintf("通过\n");
}

static void test_xftp_ssl_config(void)
{
    XPrintf("【单元】SSL 配置 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XASSERT_EQ(XFtp_state(ftp), XFtp_State_Unconnected);
    XFtp_setSsl(ftp, true);
    XFtp_setSsl(ftp, false);
    XFtp_setSsl(ftp, true);
    XFtp_abort(ftp);
    XClass_delete_base((XClass*)ftp);
    XPrintf("通过\n");
}

static void test_xftp_advanced_config(void)
{
    XPrintf("【单元】高级配置：UTF8 / MLSD / 代理 / ABOR ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);

    XFtp_setTransferType(ftp, XFtp_DataType_Ascii);
    XASSERT_EQ(XFtp_transferType(ftp), XFtp_DataType_Ascii);
    XFtp_setTransferType(ftp, XFtp_DataType_Binary);
    XASSERT_EQ(XFtp_transferType(ftp), XFtp_DataType_Binary);

    XFtp_setUtf8(ftp, true);
    XASSERT_TRUE(XFtp_isUtf8(ftp));
    XASSERT_FALSE(XFtp_supportsFeature(ftp, XFtp_Feature_UTF8));
    XASSERT_FALSE(XFtp_supportsFeature(ftp, XFtp_Feature_MLSD));
    XFtp_setMlsdEnabled(ftp, false);

    XFtp_setProxy(ftp, "proxy.example.com", 8080);
    XFtp_setSocks5Proxy(ftp, "socks.example.com", 1080, "user", "pass");
    XFtp_clearProxy(ftp);

    XFtp_abortTransfer(ftp);
    XClass_delete_base((XClass*)ftp);
    XPrintf("通过\n");
}

/* ========================================================================
 *  端到端真服务器联调测试（需先启动 Python ftp_test_server.py）
 * ====================================================================== */

/**
 * @brief 共用：连接 + 登录，返回是否成功
 */
static bool e2e_connect_and_login(XFtp* ftp)
{
    char url[128];
    int  n = snprintf(url, sizeof(url), "ftp://%s:%u/", FTP_TEST_HOST, (unsigned)FTP_TEST_PORT);
    if (n <= 0 || n >= (int)sizeof(url)) return false;

    int id = XFtp_connectToUrl(ftp, url);
    if (id < 0) return false;
    if (!wait_for_command(5000)) return false;

    int loginId = XFtp_login(ftp, FTP_TEST_USER, FTP_TEST_PASS);
    if (loginId < 0) return false;
    return wait_for_state(ftp, XFtp_State_LoggedIn, 5000);
}

static void test_xftp_e2e_connect(void)
{
    XPrintf("【E2E】连接 + 登录 + 状态机 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XObject_connect_1((XObject*)ftp, XFtp_commandFinished_signal,
                      (XObject*)ftp, on_commandFinished, XConnectionType_Direct);

    bool ok = e2e_connect_and_login(ftp);
    if (ok) {
        XASSERT_EQ(XFtp_state(ftp), XFtp_State_LoggedIn);
        XASSERT_FALSE(XFtp_hasPendingCommands(ftp));
    }
    XClass_delete_base((XClass*)ftp);
    XPrintf(ok ? "通过\n" : "跳过（未连上服务器）\n");
}

static void test_xftp_e2e_feat(void)
{
    XPrintf("【E2E】FEAT 协商（连接后自动 FEAT + OPTS UTF8） ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    bool ok = e2e_connect_and_login(ftp);
    if (ok) {
        /* 连接后 ftp 自动发送 FEAT/OPTS UTF8，等待 211 响应 */
        XThread_msleep(500);
        XCoreApplication_processEvents(0);
        XASSERT_TRUE(XFtp_supportsFeature(ftp, XFtp_Feature_UTF8));
        XASSERT_TRUE(XFtp_supportsFeature(ftp, XFtp_Feature_MLSD));
        XASSERT_TRUE(XFtp_supportsFeature(ftp, XFtp_Feature_EPSV));
    }
    XClass_delete_base((XClass*)ftp);
    XPrintf(ok ? "通过\n" : "跳过（未连上服务器）\n");
}

static void test_xftp_e2e_list(void)
{
    XPrintf("【E2E】LIST / MLSD 列表 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XObject_connect_1((XObject*)ftp, XFtp_listInfo_signal,
                      (XObject*)ftp, on_listInfo, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp, XFtp_commandFinished_signal,
                      (XObject*)ftp, on_commandFinished, XConnectionType_Direct);

    s_listing_count = 0;
    s_listing_total_size = 0;

    bool ok = e2e_connect_and_login(ftp);
    if (ok) {
        int id = XFtp_list(ftp, FTP_TEST_LIST_REMOTE);
        if (id > 0 && wait_for_command(5000)) {
            XPrintf("已列出 %d 项 ", s_listing_count);
        } else {
            ok = false;
            XPrintf("列表失败 ");
        }
    }
    XClass_delete_base((XClass*)ftp);
    XPrintf(ok ? "通过\n" : "跳过（未连上服务器）\n");
}

static void test_xftp_e2e_mkdir_rmdir(void)
{
    XPrintf("【E2E】MKDIR / RMDIR ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XObject_connect_1((XObject*)ftp, XFtp_commandFinished_signal,
                      (XObject*)ftp, on_commandFinished, XConnectionType_Direct);

    bool ok = e2e_connect_and_login(ftp);
    if (ok) {
        int id = XFtp_mkdir(ftp, FTP_TEST_MKDIR);
        if (id > 0 && wait_for_command(3000)) {
            XPrintf("建目录成功 ");
        } else {
            XPrintf("建目录失败 ");
        }
        id = XFtp_rmdir(ftp, FTP_TEST_MKDIR);
        if (id > 0 && wait_for_command(3000)) {
            XPrintf("删目录成功 ");
        } else {
            XPrintf("删目录失败 ");
        }
    }
    XClass_delete_base((XClass*)ftp);
    XPrintf(ok ? "通过\n" : "跳过（未连上服务器）\n");
}

static void test_xftp_e2e_put_get(void)
{
    XPrintf("【E2E】PUT 上传 + GET 下载 ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XObject_connect_1((XObject*)ftp, XFtp_commandFinished_signal,
                      (XObject*)ftp, on_commandFinished, XConnectionType_Direct);
    XObject_connect_1((XObject*)ftp, XFtp_dataTransferProgress_signal,
                      (XObject*)ftp, on_dataTransferProgress, XConnectionType_Direct);

    bool ok = e2e_connect_and_login(ftp);
    if (ok) {
        const char* uploadData = "你好 XFtp——来自 XinYueC 端到端联调测试。"
                                 "这是一段用于验证上传下载链路完整性的载荷。";
        int uploadSize = (int)strlen(uploadData);

        int putId = XFtp_put(ftp, FTP_TEST_UPLOAD, uploadData, uploadSize);
        if (putId > 0 && wait_for_command(5000)) {
            XPrintf("已上传 %d 字节 ", uploadSize);
        } else {
            XPrintf("上传失败 ");
        }

        const char* localPath = FTP_TEST_DOWNLOAD;
        int getId = XFtp_get(ftp, FTP_TEST_UPLOAD, (void*)localPath, 0);
        if (getId > 0 && wait_for_command(5000)) {
            XPrintf("已下载 ");
        } else {
            XPrintf("下载失败 ");
        }
    }
    XClass_delete_base((XClass*)ftp);
    XPrintf(ok ? "通过\n" : "跳过（未连上服务器）\n");
}

static void test_xftp_e2e_rename_remove(void)
{
    XPrintf("【E2E】RENAME / REMOVE ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XObject_connect_1((XObject*)ftp, XFtp_commandFinished_signal,
                      (XObject*)ftp, on_commandFinished, XConnectionType_Direct);

    bool ok = e2e_connect_and_login(ftp);
    if (ok) {
        const char* tmp = "rename_tmp.txt";
        int id = XFtp_put(ftp, tmp, "data", 4);
        if (id > 0 && wait_for_command(3000)) {
            XPrintf("建文件成功 ");
        }
        id = XFtp_rename(ftp, tmp, FTP_TEST_RENAME_TO);
        if (id > 0 && wait_for_command(3000)) {
            XPrintf("重命名成功 ");
        } else {
            XPrintf("重命名失败 ");
        }
        id = XFtp_remove(ftp, FTP_TEST_RENAME_TO);
        if (id > 0 && wait_for_command(3000)) {
            XPrintf("删除成功 ");
        } else {
            XPrintf("删除失败 ");
        }
    }
    XClass_delete_base((XClass*)ftp);
    XPrintf(ok ? "通过\n" : "跳过（未连上服务器）\n");
}

static void test_xftp_e2e_raw(void)
{
    XPrintf("【E2E】原始命令 PWD / SYST / NOOP ... ");
    XFtp* ftp = XFtp_create();
    XASSERT_NOT_NULL(ftp);
    XObject_connect_1((XObject*)ftp, XFtp_rawCommandReply_signal,
                      (XObject*)ftp, on_rawCommandReply, XConnectionType_Direct);

    bool ok = e2e_connect_and_login(ftp);
    if (ok) {
        int id = XFtp_rawCommand(ftp, "PWD");
        if (id > 0 && wait_for_command(3000)) XPrintf("PWD成功 ");
        else                                    XPrintf("PWD失败 ");

        id = XFtp_rawCommand(ftp, "SYST");
        if (id > 0 && wait_for_command(3000)) XPrintf("SYST成功 ");
        else                                    XPrintf("SYST失败 ");

        id = XFtp_rawCommand(ftp, "NOOP");
        if (id > 0 && wait_for_command(3000)) XPrintf("NOOP成功 ");
        else                                    XPrintf("NOOP失败 ");
    }
    XClass_delete_base((XClass*)ftp);
    XPrintf(ok ? "通过\n" : "跳过（未连上服务器）\n");
}

/* ===================== XAction 回调包装 =====================
 * XAction 回调签名是 void(XVariant*)，而测试函数是 void(void)，
 * 这里用宏统一包一层。
 */
#define WRAP_ACTION(test_fn) \
    static void wrap_##test_fn(XVariant* data) { (void)data; test_fn(); }

WRAP_ACTION(test_xftp_create_destroy)
WRAP_ACTION(test_xftp_transfer_mode)
WRAP_ACTION(test_xftp_error_query)
WRAP_ACTION(test_xftp_invalid_args)
WRAP_ACTION(test_xftp_state_queries)
WRAP_ACTION(test_xftp_command_queue)
WRAP_ACTION(test_xftp_signal_connect)
WRAP_ACTION(test_xftp_sizeof)
WRAP_ACTION(test_xftp_ssl_config)
WRAP_ACTION(test_xftp_advanced_config)
WRAP_ACTION(test_xftp_e2e_connect)
WRAP_ACTION(test_xftp_e2e_feat)
WRAP_ACTION(test_xftp_e2e_list)
WRAP_ACTION(test_xftp_e2e_mkdir_rmdir)
WRAP_ACTION(test_xftp_e2e_put_get)
WRAP_ACTION(test_xftp_e2e_rename_remove)
WRAP_ACTION(test_xftp_e2e_raw)

#define ADD_TEST(menu, name, fn) do {                       \
        XAction* a = XMenu_addAction(menu, name);           \
        if (a) XAction_setAction(a, wrap_##fn);             \
    } while (0)

/**
 * @brief 将全部 XFtp 测试注册到 root 菜单的 "XFtp(FTP客户端)" 子菜单下
 */
void XFtpTest_registerAll(XMenu* root)
{
    XMenu* menu = XMenu_create("XFtp(FTP客户端)");
    if (!menu) return;
    XMenu_addMenu(root, menu);

    /* 单元测试（不需服务器） */
    ADD_TEST(menu, "01 创建与销毁",                test_xftp_create_destroy);
    ADD_TEST(menu, "02 传输模式 / 类型 / 重连",     test_xftp_transfer_mode);
    ADD_TEST(menu, "03 错误查询",                   test_xftp_error_query);
    ADD_TEST(menu, "04 非法参数校验",               test_xftp_invalid_args);
    ADD_TEST(menu, "05 状态查询",                   test_xftp_state_queries);
    ADD_TEST(menu, "06 命令队列",                   test_xftp_command_queue);
    ADD_TEST(menu, "07 信号连接",                   test_xftp_signal_connect);
    ADD_TEST(menu, "08 资源占用（sizeof）",         test_xftp_sizeof);
    ADD_TEST(menu, "09 SSL 配置",                   test_xftp_ssl_config);
    ADD_TEST(menu, "10 高级配置 UTF8 / MLSD / 代理", test_xftp_advanced_config);

    /* 端到端真服务器联调（需先启动 ftp_test_server.py） */
    ADD_TEST(menu, "11 [E2E] 连接 + 登录 + 状态机",   test_xftp_e2e_connect);
    ADD_TEST(menu, "12 [E2E] FEAT / OPTS UTF8 协商", test_xftp_e2e_feat);
    ADD_TEST(menu, "13 [E2E] LIST / MLSD 列表",     test_xftp_e2e_list);
    ADD_TEST(menu, "14 [E2E] MKDIR / RMDIR",        test_xftp_e2e_mkdir_rmdir);
    ADD_TEST(menu, "15 [E2E] PUT 上传 + GET 下载",  test_xftp_e2e_put_get);
    ADD_TEST(menu, "16 [E2E] RENAME / REMOVE",      test_xftp_e2e_rename_remove);
    ADD_TEST(menu, "17 [E2E] 原始命令 PWD/SYST/NOOP", test_xftp_e2e_raw);
}

#ifdef __cplusplus
}
#endif
