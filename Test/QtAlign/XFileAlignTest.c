/**
 * @file       XFileAlignTest.c
 * @brief      XFile / XFileDevice / XSaveFile 与 Qt 6.8 对齐契约测试。
 * @details    覆盖本轮对齐台账（.zcode/qt68/gaps.md）的文件相关缺口：
 *             - #24 XFile 重复 open 必须拒绝且**不覆盖**原句柄（QFile::open
 *               qfile.cpp:906-932；本机 Qt 6.8 实测 open2=false、pos 不变、
 *               后续 read 仍读原句柄）；
 *             - #25 XFile close 交给基类收口（QFileDevice::close
 *               qfiledevice.cpp:326-346 → QIODevice::close
 *               qiodevice.cpp:779-800：pos/事务/读通道数复位）；
 *             - #26 seek 必须通知基类（QFileDevice::seek
 *               qfiledevice.cpp:410-430：fileEngine->seek + QIODevice::seek）；
 *             - #25 XSaveFile close 的**项目有意偏差**（Qt 6.8 的
 *               QSaveFile::close 是 qFatal("QSaveFile::close called")，
 *               qsavefile.cpp:272-275；本框架取空操作，不 abort 进程）。
 *
 *             Qt 参考口径全部来自本机 Qt 6.8.0 探针实测（见各用例注释）与
 *             qtbase v6.8.0 原文；文件一律落在 XDir_tempPath() 下的唯一名，
 *             用例结束删除。
 * @note       C99；对象（XString/XFile/XSaveFile）全部释放，满足运行器
 *             的零泄漏门。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XFile.h"
#include "XSaveFile.h"
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XDir.h"
#include "XString.h"
#include "XDateTime.h"
#include "XMemory.h"

#include <string.h>

#if XFILE_ON && XFILE_OBJECT_ON && XSAVEFILE_ON
/* 模块裁剪（XFILE_* / XSAVEFILE_ON = 0）时本组用例无可测对象：整段不编译，
 * 与 XIODeviceAlignTest 的 socket 用例同口径（不伪造空断言）。 */

/* ==================== 临时文件辅助 ==================== */

/** @brief 用例内唯一名序号（同一毫秒内多次调用也不撞名）。 */
static int s_fileSeq = 0;

/**
 * @brief 在系统临时目录下生成唯一文件名（新 XString，调用方释放）。
 * @param tag 场景标签（拼进文件名，便于失败时定位）。
 */
static XString* xf_makePath(const char* tag)
{
    XString* dir = XDir_tempPath();
    XString* path;
    XString num;
    if (!dir) return NULL;
    path = XString_create_copy(dir);
    XClassDelete((XClass*)dir);
    if (!path) return NULL;
    XString_append_utf8(path, "/xfile_align_");
    XString_append_utf8(path, tag);
    XString_append_utf8(path, "_");
    XString_init(&num);
    XString_setNum_uLong(&num,
        (unsigned long)(XDateTime_currentMSecsSinceEpoch() & 0x3FFFFFFFul) +
        (unsigned long)(++s_fileSeq), 10);
    XString_append(path, &num);
    XClassDeinit((XClass*)&num);
    XString_append_utf8(path, ".txt");
    return path;
}

/** @brief 用 XFile 写一个内容确定的文件（覆盖创建）。 */
static bool xf_writeFile(const XString* path, const char* data, int64_t len)
{
    XFile* f = XFile_create_2(path);
    bool ok;
    if (!f) return false;
    ok = XFile_open_2(f, XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create, 0);
    if (ok && len > 0)
        ok = (XIODevice_write_1((XIODevice*)f, data, len) == len);
    XIODevice_close_base((XIODevice*)f);
    XClassDelete((XClass*)f);
    return ok;
}

/** @brief 读回整个文件内容（NUL 终止），返回字节数；打不开返回 -1。 */
static int64_t xf_readFile(const XString* path, char* buf, int64_t cap)
{
    XFile* f = XFile_create_2(path);
    int64_t n = -1;
    if (!f) return -1;
    if (XFile_open_2(f, XIODevice_ReadOnly, 0)) {
        n = XIODevice_read_1((XIODevice*)f, buf, cap - 1);
        if (n < 0) n = 0;
        buf[n] = '\0';
        XIODevice_close_base((XIODevice*)f);
    }
    XClassDelete((XClass*)f);
    return n;
}

/* ==================== 用例 ==================== */

/**
 * @brief #24：同一 XFile 二次 open 必须拒绝，且原句柄/位置保持不变。
 * @details Qt 6.8 `QFile::open` 首行就是 `if (isOpen()) return
 *          file_already_open(*this);`（qfile.cpp:909-910），本机实测打印
 *          "QFile::open: File (...) already open"、返回 false，且随后 read
 *          仍从原句柄当前位置继续。原实现无守卫，二次 open 直接把新句柄
 *          盖到旧句柄上 ⇒ 旧 OS 句柄（及 IoRing 注册）永久泄漏。
 */
XQ_TEST(XFile, reopenRejectedKeepsHandle)
{
    XString* path = xf_makePath("reopen");
    XFile* f;
    char b[8];
    XFd fd0;
    XQ_REQUIRE(path != NULL);
    XQ_CHECK(xf_writeFile(path, "0123456789", 10));

    f = XFile_create_2(path);
    /* 构造失败时先把已建的路径对象释放掉，再报失败（否则泄漏门记成用例泄漏）。 */
    if (!f) {
        XClassDelete((XClass*)path);
        xq_fail_simple("f != NULL", __FILE__, __LINE__);
        return;
    }
    XQ_CHECK(XFile_open_2(f, XIODevice_ReadOnly, 0));
    fd0 = XIODevice_fd((XIODevice*)f);
    XQ_CHECK(fd0 >= 0);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)f, b, 4), 4);
    XQ_CHECK(memcmp(b, "0123", 4) == 0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 4);

    /* 二次 open：false + 句柄不变 + 位置不变 + 仍可读 */
    XQ_CHECK(!XFile_open_2(f, XIODevice_ReadOnly, 0));
    XQ_CHECK(XIODevice_fd((XIODevice*)f) == fd0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 4);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)f, b, 4), 4);
    XQ_CHECK(memcmp(b, "4567", 4) == 0);

    XIODevice_close_base((XIODevice*)f);
    XClassDelete((XClass*)f);
    XFile_remove_static(path);
    XClassDelete((XClass*)path);
}

/**
 * @brief #25：close 交给基类收口——isOpen/openMode/pos/事务/读通道数全部复位。
 * @details Qt `QFileDevice::close()`（qfiledevice.cpp:326-346）先
 *          `QIODevice::close()`（qiodevice.cpp:779-800：aboutToClose、NotOpen、
 *          pos=0、transactionStarted=false、读通道 0）再关引擎。本机实测：
 *          read 后 close，pos()==0、readChannelCount()==0。
 */
XQ_TEST(XFile, closeResetsBaseState)
{
    XString* path = xf_makePath("close");
    XFile* f;
    char b[8];
    XQ_REQUIRE(path != NULL);
    XQ_CHECK(xf_writeFile(path, "0123456789", 10));

    f = XFile_create_2(path);
    /* 构造失败时先把已建的路径对象释放掉，再报失败（否则泄漏门记成用例泄漏）。 */
    if (!f) {
        XClassDelete((XClass*)path);
        xq_fail_simple("f != NULL", __FILE__, __LINE__);
        return;
    }
    XQ_CHECK(XFile_open_2(f, XIODevice_ReadOnly, 0));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)f, b, 3), 3);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 3);
    XIODevice_startTransaction((XIODevice*)f);
    XQ_CHECK(XIODevice_isTransactionStarted((XIODevice*)f));

    XIODevice_close_base((XIODevice*)f);
    XQ_CHECK(!XIODevice_isOpen((XIODevice*)f));
    XQ_EQ_I64(XIODevice_openMode((XIODevice*)f), XIODevice_NotOpen);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 0);
    XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)f), 0);
    XQ_EQ_I64(XIODevice_writeChannelCount((XIODevice*)f), 0);
    XQ_CHECK(!XIODevice_isTransactionStarted((XIODevice*)f));
    XQ_CHECK(XIODevice_fd((XIODevice*)f) < 0);

    /* 重开从头读：上一会话的预读/位置不残留 */
    XQ_CHECK(XFile_open_2(f, XIODevice_ReadOnly, 0));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)f, b, 4), 4);
    XQ_CHECK(memcmp(b, "0123", 4) == 0);

    XIODevice_close_base((XIODevice*)f);
    XClassDelete((XClass*)f);
    XFile_remove_static(path);
    XClassDelete((XClass*)path);
}

/**
 * @brief Append 打开后位置在文件末尾（Qt 在 open 里 seek(size())）。
 * @details qfile.cpp:923-924；本机 Qt 6.8 实测
 *          `open(WriteOnly|Append)` 后 `pos()==size()`。只把 openMode 的
 *          Append 位交给平台（O_APPEND）而不同步逻辑位置，会让 pos() 与
 *          实际写入点分叉（XFileDevice 的 Pos 取描述符位置）。
 */
XQ_TEST(XFile, appendSeeksToEnd)
{
    XString* path = xf_makePath("append");
    XFile* f;
    char b[16];
    XQ_REQUIRE(path != NULL);
    XQ_CHECK(xf_writeFile(path, "0123456789", 10));

    f = XFile_create_2(path);
    /* 构造失败时先把已建的路径对象释放掉，再报失败（否则泄漏门记成用例泄漏）。 */
    if (!f) {
        XClassDelete((XClass*)path);
        xq_fail_simple("f != NULL", __FILE__, __LINE__);
        return;
    }
    XQ_CHECK(XFile_open_2(f, XIODevice_WriteOnly | XIODevice_Append, 0));
    XQ_EQ_I64(XIODevice_size_base((XIODevice*)f), 10);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 10);
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)f, "XY", 2), 2);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 12);
    XIODevice_close_base((XIODevice*)f);

    XQ_EQ_I64(xf_readFile(path, b, (int64_t)sizeof(b)), 12);
    XQ_CHECK(memcmp(b, "0123456789XY", 12) == 0);

    XClassDelete((XClass*)f);
    XFile_remove_static(path);
    XClassDelete((XClass*)path);
}

/**
 * @brief #26：seek 移动描述符后必须通知基类（位置登记 + 预读作废）。
 * @details Qt `QFileDevice::seek`（qfiledevice.cpp:410-430）=
 *          `fileEngine->seek(off) && QIODevice::seek(off)`。原实现只动
 *          描述符，基类靠「重载 Pos 就按 pos() 校准」兜底；本用例锁住
 *          「seek 后 pos()/bytesAvailable()/下一次 read 的字节」三者一致。
 *          另外 Qt 允许 seek 到 EOF 之外（本机实测 seek(100) 返回 true、
 *          pos()==100），此时可读字节必须为 0 而不是负数。
 */
XQ_TEST(XFile, seekNotifiesBase)
{
    XString* path = xf_makePath("seek");
    XFile* f;
    char b[8];
    XQ_REQUIRE(path != NULL);
    XQ_CHECK(xf_writeFile(path, "0123456789", 10));

    f = XFile_create_2(path);
    /* 构造失败时先把已建的路径对象释放掉，再报失败（否则泄漏门记成用例泄漏）。 */
    if (!f) {
        XClassDelete((XClass*)path);
        xq_fail_simple("f != NULL", __FILE__, __LINE__);
        return;
    }
    XQ_CHECK(XFile_open_2(f, XIODevice_ReadOnly, 0));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)f, b, 3), 3);
    XQ_CHECK(memcmp(b, "012", 3) == 0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 3);

    /* peek 预读 4 字节（描述符前进到 7，逻辑位置仍是 3） */
    XQ_EQ_I64(XIODevice_peek_1((XIODevice*)f, b, 4), 4);
    XQ_CHECK(memcmp(b, "3456", 4) == 0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 3);

    /* 回退 seek：位置必须登记为 1，预读作废（否则下一次 read 会拿到 "34"） */
    XQ_CHECK(XIODevice_seek_base((XIODevice*)f, 1));
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 1);
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)f), 9);
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)f, b, 2), 2);
    XQ_CHECK(memcmp(b, "12", 2) == 0);

    /* seek 到 EOF 之外：允许、pos 前移、可读 0、atEnd */
    XQ_CHECK(XIODevice_seek_base((XIODevice*)f, 20));
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)f), 20);
    XQ_EQ_I64(XIODevice_bytesAvailable_base((XIODevice*)f), 0);
    XQ_CHECK(XIODevice_atEnd_base((XIODevice*)f));

    /* 回退 seek 后重新可读 */
    XQ_CHECK(XIODevice_seek_base((XIODevice*)f, 0));
    XQ_EQ_I64(XIODevice_read_1((XIODevice*)f, b, 2), 2);
    XQ_CHECK(memcmp(b, "01", 2) == 0);

    /* 负值/顺序设备语义：负值拒绝（Qt 本机实测 seek(-1)==false） */
    XQ_CHECK(!XIODevice_seek_base((XIODevice*)f, -1));

    XIODevice_close_base((XIODevice*)f);
    XClassDelete((XClass*)f);
    XFile_remove_static(path);
    XClassDelete((XClass*)path);
}

/**
 * @brief open 必须指定 ReadOnly/WriteOnly（Qt 本机实测 open(NotOpen)==false）。
 * @details qfile.cpp:915-918（"File access not specified"）；本机 Qt 6.8
 *          实测 `open(QIODevice::NotOpen)` 返回 false。
 */
XQ_TEST(XFile, openRequiresAccessMode)
{
    XString* path = xf_makePath("mode");
    XFile* f;
    XQ_REQUIRE(path != NULL);
    XQ_CHECK(xf_writeFile(path, "x", 1));

    f = XFile_create_2(path);
    /* 构造失败时先把已建的路径对象释放掉，再报失败（否则泄漏门记成用例泄漏）。 */
    if (!f) {
        XClassDelete((XClass*)path);
        xq_fail_simple("f != NULL", __FILE__, __LINE__);
        return;
    }
    XQ_CHECK(!XFile_open_2(f, XIODevice_NotOpen, 0));
    XQ_CHECK(!XIODevice_isOpen((XIODevice*)f));
    /* 只有 Truncate|Create、没有读写位：同样拒绝（Create 是本框架扩展位，
     * 不能单独构成访问模式） */
    XQ_CHECK(!XFile_open_2(f, XIODevice_Truncate | XIODevice_Create, 0));
    XQ_CHECK(!XIODevice_isOpen((XIODevice*)f));
    XQ_CHECK(XIODevice_fd((XIODevice*)f) < 0);

    XClassDelete((XClass*)f);
    XFile_remove_static(path);
    XClassDelete((XClass*)path);
}

/**
 * @brief #25（XSaveFile）：close 是**有意空操作**，唯一出口仍是 commit()。
 * @details Qt 6.8 原文：`void QSaveFile::close() { qFatal("QSaveFile::close
 *          called"); }`（qsavefile.cpp:272-275），头文件还把它声明为 private
 *          （qsavefile.h:54）——在 Qt 里调用 close 会直接终止进程。本框架是
 *          现场/嵌入式框架，不能因一次误调 close 就 abort，故保留「不改变
 *          任何状态」的等价实现（项目有意偏差）。本用例锁住该偏差：close
 *          后仍 isOpen、仍可写，commit 正常落盘。
 */
XQ_TEST(XSaveFile, closeIsNoOpUntilCommit)
{
    XString* path = xf_makePath("saveclose");
    XSaveFile* sf;
    char b[8];
    XQ_REQUIRE(path != NULL);

    sf = XSaveFile_create_2(path);
    if (!sf) {
        XClassDelete((XClass*)path);
        xq_fail_simple("sf != NULL", __FILE__, __LINE__);
        return;
    }
    XQ_CHECK(XIODevice_open_base((XIODevice*)sf, XIODevice_WriteOnly));
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)sf, "A", 1), 1);

    XIODevice_close_base((XIODevice*)sf);
    XQ_CHECK(XIODevice_isOpen((XIODevice*)sf));
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)sf, "B", 1), 1);

    XQ_CHECK(XSaveFile_commit(sf));
    XQ_CHECK(!XIODevice_isOpen((XIODevice*)sf));
    XQ_EQ_I64(xf_readFile(path, b, (int64_t)sizeof(b)), 2);
    XQ_CHECK(memcmp(b, "AB", 2) == 0);

    XClassDelete((XClass*)sf);
    XFile_remove_static(path);
    XClassDelete((XClass*)path);
}

/**
 * @brief commit 走基类 close 收口（对齐 Qt commit 里的 QFileDevice::close）。
 * @details qsavefile.cpp:299 `QFileDevice::close(); // calls flush()`。原实现
 *          只把 m_openMode 置 NotOpen，读通道数/位置/事务不复位。
 */
XQ_TEST(XSaveFile, commitClosesBaseState)
{
    XString* path = xf_makePath("savecommit");
    XSaveFile* sf;
    char b[16];
    XQ_REQUIRE(path != NULL);

    sf = XSaveFile_create_2(path);
    if (!sf) {
        XClassDelete((XClass*)path);
        xq_fail_simple("sf != NULL", __FILE__, __LINE__);
        return;
    }
    XQ_CHECK(XIODevice_open_base((XIODevice*)sf, XIODevice_WriteOnly));
    XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)sf), 0);
    XQ_EQ_I64(XIODevice_write_1((XIODevice*)sf, "data", 4), 4);

    XQ_CHECK(XSaveFile_commit(sf));
    XQ_CHECK(!XIODevice_isOpen((XIODevice*)sf));
    XQ_EQ_I64(XIODevice_openMode((XIODevice*)sf), XIODevice_NotOpen);
    XQ_EQ_I64(XIODevice_readChannelCount((XIODevice*)sf), 0);
    XQ_EQ_I64(XIODevice_writeChannelCount((XIODevice*)sf), 0);
    XQ_EQ_I64(XIODevice_pos_base((XIODevice*)sf), 0);
    /* 已提交：再 commit 返回 false（Qt: m_committed 后早退） */
    XQ_CHECK(!XSaveFile_commit(sf));

    XQ_EQ_I64(xf_readFile(path, b, (int64_t)sizeof(b)), 4);
    XQ_CHECK(memcmp(b, "data", 4) == 0);

    XClassDelete((XClass*)sf);
    XFile_remove_static(path);
    XClassDelete((XClass*)path);
}

#endif /* XFILE_ON && XFILE_OBJECT_ON && XSAVEFILE_ON */
