#include"XIOTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XFile.h"
#include"XString.h"
#include"XByteArray.h"
#include"XDir.h"
#include"XPrintf.h"
#include"XFileDescriptor.h"
#include"XDateTime.h"
#include <string.h>

/* ============================================================================
 * 测试辅助
 * ============================================================================ */

static int g_pass = 0;
static int g_fail = 0;

static void CHECK(bool cond, const char* msg) {
    if (cond) {
        g_pass++;
    } else {
        g_fail++;
        XPrintf("  [FAIL] %s\n", msg);
    }
}

static void XFileTest_print_xstring(const char* label, const XString* str) {
    XPrintf("%s: ", label);
    if (str) {
        XPrintf_2(str);
    } else {
        XPrintf_3("(NULL)");
    }
    XPrintf_3("\n");
}

/* ============================================================================
 * 1. 构造与析构
 * ============================================================================ */

static void test_create_destroy(void)
{
    XPrintf_3("\n--- 1. 构造与析构 ---\n");

    /* 1.1 XFile_create 空对象 */
    XFile* f = XFile_create();
    CHECK(f != NULL, "XFile_create 返回非 NULL");
    if (f) {
        const XString* name = XFile_fileName_base((XFileDevice*)f);
        CHECK(name != NULL, "空对象 fileName 非 NULL");
        CHECK(XString_size_base(name) == 0, "空对象 fileName 为空字符串");
        XFile_deleteLater(f);
    }

    /* 1.2 XFile_create_2 带文件名 */
    XString* fn = XString_create_utf8("test_ctor.txt");
    XFile* f2 = XFile_create_2(fn);
    CHECK(f2 != NULL, "XFile_create_2 返回非 NULL");
    if (f2) {
        const XString* got = XFile_fileName_base((XFileDevice*)f2);
        CHECK(XString_compare(got, fn) == 0, "XFile_create_2 文件名正确");
        XFile_deleteLater(f2);
    }
    XString_delete_base(fn);

    /* 1.3 栈上 XFile_init / XFile_init_2 */
    XFile sf;
    XFile_init(&sf);
    CHECK(XString_size_base(XFile_fileName_base((XFileDevice*)&sf)) == 0,
          "XFile_init 栈对象 fileName 为空");
    XClass_deinit_base(&sf);

    XString* fn2 = XString_create_utf8("stack_init.txt");
    XFile sf2;
    XFile_init_2(&sf2, fn2);
    CHECK(XString_compare(XFile_fileName_base((XFileDevice*)&sf2), fn2) == 0,
          "XFile_init_2 栈对象文件名正确");
    XClass_deinit_base(&sf2);
    XString_delete_base(fn2);
}

/* ============================================================================
 * 2. setFileName / fileName
 * ============================================================================ */

static void test_setFileName(void)
{
    XPrintf_3("\n--- 2. setFileName / fileName ---\n");

    XFile* f = XFile_create();
    XString* fn = XString_create_utf8("setname_test.txt");
    XFile_setFileName(f, fn);
    const XString* got = XFile_fileName_base((XFileDevice*)f);
    CHECK(XString_compare(got, fn) == 0, "setFileName 后 fileName 正确");

    /* 再次设置覆盖 */
    XString* fn2 = XString_create_utf8("setname_test2.txt");
    XFile_setFileName(f, fn2);
    got = XFile_fileName_base((XFileDevice*)f);
    CHECK(XString_compare(got, fn2) == 0, "setFileName 覆盖后 fileName 正确");

    XString_delete_base(fn);
    XString_delete_base(fn2);
    XFile_deleteLater(f);
}

/* ============================================================================
 * 3. 打开模式测试
 * ============================================================================ */

static void test_open_modes(void)
{
    XPrintf_3("\n--- 3. 打开模式测试 ---\n");

    const char* fname = "xfile_modes_test.txt";
    XString* fn = XString_create_utf8(fname);

    /* 清理残留 */
    XFile_remove_static(fn);

    /* 3.1 WriteOnly | NewOnly 创建新文件 */
    XFile* f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    CHECK(ok, "WriteOnly|NewOnly 创建新文件成功");
    CHECK(XIODevice_isOpen((XIODevice*)f), "打开后 isOpen == true");
    CHECK(XIODevice_isWritable((XIODevice*)f), "WriteOnly 模式 isWritable == true");
    CHECK(!XIODevice_isReadable((XIODevice*)f), "WriteOnly 模式 isReadable == false");
    CHECK(!XIODevice_isSequential((XIODevice*)f), "文件 isSequential == false");
    CHECK(XIODevice_openMode((XIODevice*)f) == (XIODevice_WriteOnly | XIODevice_NewOnly),
          "openMode 返回正确模式");
    XIODevice_close_base((XIODevice*)f);
    CHECK(!XIODevice_isOpen((XIODevice*)f), "关闭后 isOpen == false");
    XFile_deleteLater(f);

    /* 3.2 NewOnly 对已存在文件应失败 (Qt: QFile::NewOnly fails if file exists) */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    CHECK(!ok, "WriteOnly|NewOnly 对已存在文件应失败");
    XFile_deleteLater(f);

    /* 3.3 ReadOnly | Existing 打开已存在文件 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly | XIODevice_Existing);
    CHECK(ok, "ReadOnly|Existing 打开已存在文件成功");
    CHECK(XIODevice_isReadable((XIODevice*)f), "ReadOnly 模式 isReadable == true");
    CHECK(!XIODevice_isWritable((XIODevice*)f), "ReadOnly 模式 isWritable == false");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 3.4 Existing 对不存在文件应失败 */
    XString* nofile = XString_create_utf8("xfile_no_such_file.txt");
    XFile_remove_static(nofile);
    f = XFile_create_2(nofile);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly | XIODevice_Existing);
    CHECK(!ok, "ReadOnly|Existing 对不存在文件应失败");
    XFile_deleteLater(f);
    XString_delete_base(nofile);

    /* 3.5 ReadWrite 模式 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadWrite);
    CHECK(ok, "ReadWrite 打开成功");
    CHECK(XIODevice_isReadable((XIODevice*)f), "ReadWrite isReadable == true");
    CHECK(XIODevice_isWritable((XIODevice*)f), "ReadWrite isWritable == true");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 3.6 Append 模式 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_Append);
    CHECK(ok, "Append 模式打开成功");
    if (ok) {
        XIODevice_write_1((XIODevice*)f, "APPEND", 6);
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 3.7 Truncate 模式 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_Truncate);
    CHECK(ok, "Truncate 模式打开成功");
    if (ok) {
        int64_t sz = XIODevice_size_base((XIODevice*)f);
        CHECK(sz == 0, "Truncate 后文件大小为 0");
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 3.8 Text 模式 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_Text);
    CHECK(ok, "Text 模式打开成功");
    CHECK(XIODevice_isTextModeEnabled((XIODevice*)f), "Text 模式 isTextModeEnabled == true");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 清理 */
    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 4. XFile_open_2 (带权限)
 * ============================================================================ */

static void test_open_with_permissions(void)
{
    XPrintf_3("\n--- 4. XFile_open_2 (带权限) ---\n");

    XString* fn = XString_create_utf8("xfile_perm_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    bool ok = XFile_open_2(f, XIODevice_WriteOnly | XIODevice_NewOnly,
                           XFile_ReadOwner | XFile_WriteOwner | XFile_ReadUser);
    CHECK(ok, "XFile_open_2 带权限打开成功");
    if (ok) {
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 5. XFile_open_3 (从文件描述符)
 * ============================================================================ */

static void test_open_from_fd(void)
{
    XPrintf_3("\n--- 5. XFile_open_3 (从文件描述符) ---\n");

    XString* fn = XString_create_utf8("xfile_fd_test.txt");
    XFile_remove_static(fn);

    /* 先用正常方式创建文件并写入数据 */
    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_1((XIODevice*)f, "FD_TEST_DATA", 12);
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 通过 XFileSystem_open 获取 XFd，再用 XFile_open_3 */
    int err = 0;
    XFd fd = XFileSystem_open(fn, XIODevice_ReadOnly, &err);
    CHECK(fd >= 0, "XFileSystem_open 获取 fd 成功");

    if (fd >= 0) {
        XFile* f2 = XFile_create();
        bool ok = XFile_open_3(f2, fd, XIODevice_ReadOnly, XFileDevice_AutoCloseHandle);
        CHECK(ok, "XFile_open_3 从 fd 打开成功");
        if (ok) {
            CHECK(XIODevice_isOpen((XIODevice*)f2), "open_3 后 isOpen == true");
            char buf[32] = {0};
            int64_t n = XIODevice_read_1((XIODevice*)f2, buf, sizeof(buf));
            CHECK(n == 12, "open_3 读取字节数正确");
            CHECK(memcmp(buf, "FD_TEST_DATA", 12) == 0, "open_3 读取内容正确");
            XIODevice_close_base((XIODevice*)f2);
        }
        XFile_deleteLater(f2);
    }

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 6. 写入操作
 * ============================================================================ */

static void test_write_operations(void)
{
    XPrintf_3("\n--- 6. 写入操作 ---\n");

    XString* fn = XString_create_utf8("xfile_write_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    CHECK(ok, "写入测试: 打开文件成功");

    if (ok) {
        /* 6.1 write_1 (char*, len) */
        int64_t n = XIODevice_write_1((XIODevice*)f, "Hello", 5);
        CHECK(n == 5, "write_1 写入 5 字节");

        /* 6.2 write_2 (XByteArray) */
        XByteArray* ba = XByteArray_create_utf8(", XFile!");
        n = XIODevice_write_2((XIODevice*)f, ba);
        CHECK(n == 8, "write_2 写入 8 字节");
        XByteArray_delete_base(ba);

        /* 6.3 write_3 (C 字符串) */
        n = XIODevice_write_3((XIODevice*)f, "\nLine2\n");
        CHECK(n == 7, "write_3 写入 7 字节");

        /* 6.4 putChar */
        bool pc = XIODevice_putChar((XIODevice*)f, 'X');
        CHECK(pc, "putChar 写入成功");

        /* 6.5 bytesToWrite (文件设备通常为 0) */
        int64_t btw = XIODevice_bytesToWrite_base((XIODevice*)f);
        CHECK(btw == 0, "bytesToWrite == 0 (无写缓冲)");

        /* 6.6 flush */
        bool flushed = XFile_flush((XFileDevice*)f);
        CHECK(flushed, "flush 成功");

        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 验证写入内容 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    if (ok) {
        XByteArray* all = XIODevice_readAll_3((XIODevice*)f);
        CHECK(XByteArray_size_base(all) == 21, "写入总字节数正确 (5+8+7+1=21)");
        const char* data = (const char*)XByteArray_data(all);
        CHECK(memcmp(data, "Hello, XFile!\nLine2\nX", 21) == 0, "写入内容完全正确");
        XByteArray_delete_base(all);
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 7. 读取操作
 * ============================================================================ */

static void test_read_operations(void)
{
    XPrintf_3("\n--- 7. 读取操作 ---\n");

    XString* fn = XString_create_utf8("xfile_read_test.txt");
    XFile_remove_static(fn);

    /* 准备测试文件 */
    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "Line1\nLine2\nLine3\n");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    CHECK(ok, "读取测试: 打开文件成功");

    if (ok) {
        /* 7.1 size */
        int64_t sz = XIODevice_size_base((XIODevice*)f);
        CHECK(sz == 18, "文件大小 18 字节");

        /* 7.2 bytesAvailable */
        int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)f);
        CHECK(avail == 18, "初始 bytesAvailable == 18");

        /* 7.3 atEnd (初始不在末尾) */
        CHECK(!XIODevice_atEnd_base((XIODevice*)f), "初始 atEnd == false");

        /* 7.4 pos (初始为 0) */
        int64_t p = XIODevice_pos_base((XIODevice*)f);
        CHECK(p == 0, "初始 pos == 0");

        /* 7.5 read_1 (char*, maxlen) */
        char buf[6] = {0};
        int64_t n = XIODevice_read_1((XIODevice*)f, buf, 5);
        CHECK(n == 5, "read_1 读取 5 字节");
        CHECK(memcmp(buf, "Line1", 5) == 0, "read_1 内容正确");

        /* 7.6 getChar */
        char c = 0;
        bool gc = XIODevice_getChar((XIODevice*)f, &c);
        CHECK(gc, "getChar 成功");
        CHECK(c == '\n', "getChar 读到换行符");

        /* 7.7 readLine_1 */
        char lineBuf[32] = {0};
        n = XIODevice_readLine_1((XIODevice*)f, lineBuf, sizeof(lineBuf));
        CHECK(n == 6, "readLine_1 读取 6 字节 (含换行)");
        CHECK(memcmp(lineBuf, "Line2\n", 6) == 0, "readLine_1 内容正确");

        /* 7.8 peek */
        XByteArray* peeked = XIODevice_peek_3((XIODevice*)f, 5);
        CHECK(XByteArray_size_base(peeked) == 5, "peek_3 返回 5 字节");
        CHECK(memcmp(XByteArray_data(peeked), "Line3", 5) == 0, "peek 内容正确");
        /* peek 不移动位置 */
        int64_t posAfterPeek = XIODevice_pos_base((XIODevice*)f);
        CHECK(posAfterPeek == 12, "peek 后 pos 不变 (12)");
        XByteArray_delete_base(peeked);

        /* 7.9 readLine_3 (返回 XByteArray) */
        XByteArray* line3 = XIODevice_readLine_3((XIODevice*)f);
        CHECK(XByteArray_size_base(line3) == 6, "readLine_3 读取 6 字节");
        CHECK(memcmp(XByteArray_data(line3), "Line3\n", 6) == 0, "readLine_3 内容正确");
        XByteArray_delete_base(line3);

        /* 7.10 atEnd (现在应在末尾) */
        CHECK(XIODevice_atEnd_base((XIODevice*)f), "读完后 atEnd == true");

        /* 7.11 bytesAvailable (应为 0) */
        avail = XIODevice_bytesAvailable_base((XIODevice*)f);
        CHECK(avail == 0, "读完后 bytesAvailable == 0");

        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 7.12 readAll_1 (char*, buffSize) */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    if (ok) {
        char allBuf[64] = {0};
        int64_t n = XIODevice_readAll_1((XIODevice*)f, allBuf, sizeof(allBuf));
        CHECK(n == 18, "readAll_1 读取 18 字节");
        CHECK(memcmp(allBuf, "Line1\nLine2\nLine3\n", 18) == 0, "readAll_1 内容正确");
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 7.13 readAll_2 (XByteArray, isAppend) */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    if (ok) {
        XByteArray* ba = XByteArray_create();
        int64_t n = XIODevice_readAll_2((XIODevice*)f, ba, false);
        CHECK(n == 18, "readAll_2 读取 18 字节");
        XByteArray_delete_base(ba);
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 7.14 read_3 (返回 XByteArray) */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    if (ok) {
        XByteArray* chunk = XIODevice_read_3((XIODevice*)f, 6);
        CHECK(XByteArray_size_base(chunk) == 6, "read_3 读取 6 字节");
        CHECK(memcmp(XByteArray_data(chunk), "Line1\n", 6) == 0, "read_3 内容正确");
        XByteArray_delete_base(chunk);
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 8. seek / pos / reset / skip / ungetChar
 * ============================================================================ */

static void test_seek_pos_reset(void)
{
    XPrintf_3("\n--- 8. seek / pos / reset / skip / ungetChar ---\n");

    XString* fn = XString_create_utf8("xfile_seek_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "0123456789ABCDEF");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    CHECK(ok, "seek 测试: 打开文件成功");

    if (ok) {
        /* 8.1 seek 到中间 */
        bool seeked = XIODevice_seek_base((XIODevice*)f, 10);
        CHECK(seeked, "seek(10) 成功");
        CHECK(XIODevice_pos_base((XIODevice*)f) == 10, "seek 后 pos == 10");

        char c = 0;
        XIODevice_getChar((XIODevice*)f, &c);
        CHECK(c == 'A', "seek(10) 后读到 'A'");

        /* 8.2 reset 到开头 */
        bool reset = XIODevice_reset_base((XIODevice*)f);
        CHECK(reset, "reset 成功");
        CHECK(XIODevice_pos_base((XIODevice*)f) == 0, "reset 后 pos == 0");

        /* 8.3 skip */
        int64_t skipped = XIODevice_skip((XIODevice*)f, 5);
        CHECK(skipped == 5, "skip(5) 跳过 5 字节");
        CHECK(XIODevice_pos_base((XIODevice*)f) == 5, "skip 后 pos == 5");

        XIODevice_getChar((XIODevice*)f, &c);
        CHECK(c == '5', "skip(5) 后读到 '5'");

        /* 8.4 ungetChar */
        XIODevice_ungetChar((XIODevice*)f, 'Z');
        XIODevice_getChar((XIODevice*)f, &c);
        CHECK(c == 'Z', "ungetChar 后 getChar 读到 'Z'");

        /* 8.5 seek 到末尾 */
        seeked = XIODevice_seek_base((XIODevice*)f, 16);
        CHECK(seeked, "seek(16) 到末尾成功");
        CHECK(XIODevice_atEnd_base((XIODevice*)f), "seek 到末尾后 atEnd == true");

        /* 8.6 seek 负数应失败 */
        seeked = XIODevice_seek_base((XIODevice*)f, -1);
        CHECK(!seeked, "seek(-1) 应失败");

        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 9. handle / error / unsetError
 * ============================================================================ */

static void test_handle_error(void)
{
    XPrintf_3("\n--- 9. handle / error / unsetError ---\n");

    XString* fn = XString_create_utf8("xfile_handle_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);

    /* 未打开时 handle 应为 -1 */
    XFd h = XFile_handle((XFileDevice*)f);
    CHECK(h == XFD_INVALID, "未打开时 handle == XFD_INVALID");

    /* 打开后 handle 应有效 */
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    h = XFile_handle((XFileDevice*)f);
    CHECK(h >= 0, "打开后 handle >= 0");

    /* error 应为 NoError */
    XFileDeviceError err = XFile_error((XFileDevice*)f);
    CHECK(err == XFileDevice_NoError, "正常操作后 error == NoError");

    /* unsetError */
    XFile_unsetError((XFileDevice*)f);
    err = XFile_error((XFileDevice*)f);
    CHECK(err == XFileDevice_NoError, "unsetError 后 error == NoError");

    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 打开不存在文件应产生错误 */
    XString* nofile = XString_create_utf8("xfile_no_such_handle.txt");
    XFile_remove_static(nofile);
    f = XFile_create_2(nofile);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly | XIODevice_Existing);
    CHECK(!ok, "打开不存在文件失败");
    err = XFile_error((XFileDevice*)f);
    CHECK(err != XFileDevice_NoError, "打开失败后 error != NoError");
    XFile_deleteLater(f);
    XString_delete_base(nofile);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 10. fileTime / setFileTime
 * ============================================================================ */

static void test_file_time(void)
{
    XPrintf_3("\n--- 10. fileTime / setFileTime ---\n");

    XString* fn = XString_create_utf8("xfile_time_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "time test");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    CHECK(ok, "fileTime 测试: 打开文件成功");

    if (ok) {
        /* 获取修改时间 */
        XDateTime modTime = XFile_fileTime((XFileDevice*)f, XFile_ModificationTime);
        CHECK(XDateTime_isValid(&modTime), "ModificationTime 有效");
        int64_t secs = XDateTime_toSecsSinceEpoch(&modTime);
        CHECK(secs > 0, "ModificationTime 时间戳 > 0");

        /* 获取访问时间 */
        XDateTime accTime = XFile_fileTime((XFileDevice*)f, XFile_AccessTime);
        CHECK(XDateTime_isValid(&accTime), "AccessTime 有效");

        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* setFileTime: 需要写模式打开 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadWrite);
    if (ok) {
        XDateTime newTime;
        XDateTime_setSecsSinceEpoch(&newTime, 1000000000); /* 2001-09-09 */
        bool set = XFile_setFileTime((XFileDevice*)f, &newTime, XFile_ModificationTime);
        CHECK(set, "setFileTime 设置修改时间成功");

        if (set) {
            XDateTime readBack = XFile_fileTime((XFileDevice*)f, XFile_ModificationTime);
            int64_t readSecs = XDateTime_toSecsSinceEpoch(&readBack);
            CHECK(readSecs == 1000000000, "setFileTime 后读回时间正确");
        }
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 11. resize (成员 + 静态)
 * ============================================================================ */

static void test_resize(void)
{
    XPrintf_3("\n--- 11. resize ---\n");

    XString* fn = XString_create_utf8("xfile_resize_test.txt");
    XFile_remove_static(fn);

    /* 创建 16 字节文件 */
    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "0123456789ABCDEF");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 11.1 成员 resize 缩小 */
    f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadWrite);
    CHECK(ok, "resize 测试: 打开文件成功");
    if (ok) {
        bool resized = XFile_resize_base((XFileDevice*)f, 8);
        CHECK(resized, "resize(8) 成功");
        int64_t sz = XIODevice_size_base((XIODevice*)f);
        CHECK(sz == 8, "resize 后 size == 8");
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 11.2 成员 resize 扩大 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadWrite);
    if (ok) {
        bool resized = XFile_resize_base((XFileDevice*)f, 32);
        CHECK(resized, "resize(32) 成功");
        int64_t sz = XIODevice_size_base((XIODevice*)f);
        CHECK(sz == 32, "resize 后 size == 32");
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 11.3 静态 resize */
    bool staticResized = XFile_resize_static(fn, 4);
    CHECK(staticResized, "XFile_resize_static(4) 成功");

    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    if (ok) {
        int64_t sz = XIODevice_size_base((XIODevice*)f);
        CHECK(sz == 4, "静态 resize 后 size == 4");
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 12. permissions (成员 + 静态)
 * ============================================================================ */

static void test_permissions(void)
{
    XPrintf_3("\n--- 12. permissions ---\n");

    XString* fn = XString_create_utf8("xfile_perms_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "perm");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 12.1 成员 permissions */
    f = XFile_create_2(fn);
    XFilePermissions perms = XFile_permissions_base((XFileDevice*)f);
    CHECK(perms != 0, "成员 permissions 非零");
    CHECK((perms & XFile_ReadOwner) != 0, "所有者可读");
    CHECK((perms & XFile_WriteOwner) != 0, "所有者可写");
    XFile_deleteLater(f);

    /* 12.2 静态 permissions */
    XFilePermissions staticPerms = XFile_permissions_static(fn);
    CHECK(staticPerms != 0, "静态 permissions 非零");
    CHECK(staticPerms == perms, "静态与成员 permissions 一致");

    /* 12.3 setPermissions (去掉其他用户的写权限) */
    XFilePermissions newPerms = XFile_ReadOwner | XFile_WriteOwner | XFile_ReadUser;
    bool setOk = XFile_setPermissions_static(fn, newPerms);
    CHECK(setOk, "setPermissions_static 成功");

    XFilePermissions afterPerms = XFile_permissions_static(fn);
    CHECK((afterPerms & XFile_WriteOther) == 0, "设置后其他用户无写权限");
    CHECK((afterPerms & XFile_ReadOwner) != 0, "设置后所有者仍可读");

    /* 恢复权限以便删除 */
    XFile_setPermissions_static(fn, XFile_PermsOwner | XFile_ReadUser);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 13. exists (成员 + 静态)
 * ============================================================================ */

static void test_exists(void)
{
    XPrintf_3("\n--- 13. exists ---\n");

    XString* fn = XString_create_utf8("xfile_exists_test.txt");
    XFile_remove_static(fn);

    /* 不存在 */
    CHECK(!XFile_exists_static(fn), "文件不存在时 exists_static == false");

    XFile* f = XFile_create_2(fn);
    CHECK(!XFile_exists(f), "文件不存在时成员 exists == false");

    /* 创建文件 */
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_close_base((XIODevice*)f);

    CHECK(XFile_exists_static(fn), "文件存在时 exists_static == true");
    CHECK(XFile_exists(f), "文件存在时成员 exists == true");

    XFile_deleteLater(f);
    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 14. remove (成员 + 静态)
 * ============================================================================ */

static void test_remove(void)
{
    XPrintf_3("\n--- 14. remove ---\n");

    /* 14.1 静态 remove */
    XString* fn1 = XString_create_utf8("xfile_rm_static.txt");
    XFile_remove_static(fn1);
    XFile* f = XFile_create_2(fn1);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    CHECK(XFile_exists_static(fn1), "remove 前文件存在");
    bool removed = XFile_remove_static(fn1);
    CHECK(removed, "XFile_remove_static 成功");
    CHECK(!XFile_exists_static(fn1), "remove 后文件不存在");

    /* 14.2 成员 remove */
    XString* fn2 = XString_create_utf8("xfile_rm_member.txt");
    XFile_remove_static(fn2);
    f = XFile_create_2(fn2);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_close_base((XIODevice*)f);

    removed = XFile_remove(f);
    CHECK(removed, "XFile_remove 成员删除成功");
    CHECK(!XFile_exists_static(fn2), "成员 remove 后文件不存在");
    XFile_deleteLater(f);

    /* 14.3 删除不存在文件应失败 */
    removed = XFile_remove_static(fn1);
    CHECK(!removed, "删除不存在文件应失败");

    XString_delete_base(fn1);
    XString_delete_base(fn2);
}

/* 14.4 打开状态下的 remove: Qt 行为是先关闭再删除 */
static void test_remove_open(void)
{
    XPrintf_3("\n--- 14.4 remove (打开中) ---\n");
    XString* fn = XString_create_utf8("xfile_remove_open.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "open-remove");
    bool ok = XFile_remove(f);
    CHECK(ok, "打开中 XFile_remove 成功");
    CHECK(!XFile_exists_static(fn), "打开中 remove 后文件不存在");
    XFile_deleteLater(f);
    XString_delete_base(fn);
}

/* ============================================================================
 * 15. rename (成员 + 静态)
 * ============================================================================ */

static void test_rename(void)
{
    XPrintf_3("\n--- 15. rename ---\n");

    /* 15.1 静态 rename */
    XString* src = XString_create_utf8("xfile_rename_src.txt");
    XString* dst = XString_create_utf8("xfile_rename_dst.txt");
    XFile_remove_static(src);
    XFile_remove_static(dst);

    XFile* f = XFile_create_2(src);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "rename data");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    bool renamed = XFile_rename_static(src, dst);
    CHECK(renamed, "XFile_rename_static 成功");
    CHECK(!XFile_exists_static(src), "rename 后源文件不存在");
    CHECK(XFile_exists_static(dst), "rename 后目标文件存在");

    /* 15.2 成员 rename */
    XString* dst2 = XString_create_utf8("xfile_rename_dst2.txt");
    XFile_remove_static(dst2);
    f = XFile_create_2(dst);
    renamed = XFile_rename(f, dst2);
    CHECK(renamed, "XFile_rename 成员重命名成功");
    CHECK(!XFile_exists_static(dst), "成员 rename 后原文件不存在");
    CHECK(XFile_exists_static(dst2), "成员 rename 后新文件存在");
    XFile_deleteLater(f);

    /* 15.3 rename 到已存在文件应失败 (Qt 行为) */
    XString* existing = XString_create_utf8("xfile_rename_existing.txt");
    XFile_remove_static(existing);
    f = XFile_create_2(existing);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    f = XFile_create_2(dst2);
    renamed = XFile_rename(f, existing);
    CHECK(!renamed, "rename 到已存在文件应失败 (Qt 行为)");
    XFile_deleteLater(f);

    XFile_remove_static(dst2);
    XFile_remove_static(existing);
    XString_delete_base(src);
    XString_delete_base(dst);
    XString_delete_base(dst2);
    XString_delete_base(existing);
}

/* ============================================================================
 * 16. copy (成员 + 静态)
 * ============================================================================ */

static void test_copy(void)
{
    XPrintf_3("\n--- 16. copy ---\n");

    XString* src = XString_create_utf8("xfile_copy_src.txt");
    XString* dst = XString_create_utf8("xfile_copy_dst.txt");
    XFile_remove_static(src);
    XFile_remove_static(dst);

    /* 创建源文件 */
    XFile* f = XFile_create_2(src);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "copy test data");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 16.1 静态 copy */
    bool copied = XFile_copy_static(src, dst);
    CHECK(copied, "XFile_copy_static 成功");
    CHECK(XFile_exists_static(src), "copy 后源文件仍存在");
    CHECK(XFile_exists_static(dst), "copy 后目标文件存在");

    /* 验证内容一致 */
    f = XFile_create_2(dst);
    XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    XByteArray* data = XIODevice_readAll_3((XIODevice*)f);
    CHECK(XByteArray_size_base(data) == 14, "copy 后目标文件大小正确");
    CHECK(memcmp(XByteArray_data(data), "copy test data", 14) == 0, "copy 后内容一致");
    XByteArray_delete_base(data);
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 16.2 copy 到已存在文件应失败 (Qt 行为) */
    XString* dst2 = XString_create_utf8("xfile_copy_dst2.txt");
    XFile_remove_static(dst2);
    f = XFile_create_2(dst2);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    copied = XFile_copy_static(src, dst2);
    CHECK(!copied, "copy 到已存在文件应失败 (Qt 行为)");

    /* 16.3 成员 copy */
    XString* dst3 = XString_create_utf8("xfile_copy_dst3.txt");
    XFile_remove_static(dst3);
    f = XFile_create_2(src);
    copied = XFile_copy(f, dst3);
    CHECK(copied, "XFile_copy 成员复制成功");
    CHECK(XFile_exists_static(dst3), "成员 copy 后目标文件存在");
    XFile_deleteLater(f);

    XFile_remove_static(src);
    XFile_remove_static(dst);
    XFile_remove_static(dst2);
    XFile_remove_static(dst3);
    XString_delete_base(src);
    XString_delete_base(dst);
    XString_delete_base(dst2);
    XString_delete_base(dst3);
}

/* ============================================================================
 * 17. link / symLinkTarget
 * ============================================================================ */

static void test_link(void)
{
    XPrintf_3("\n--- 17. link / symLinkTarget ---\n");

    XString* target = XString_create_utf8("xfile_link_target.txt");
    XString* linkName = XString_create_utf8("xfile_link_name.txt");
    XFile_remove_static(target);
    XFile_remove_static(linkName);

    /* 创建目标文件 */
    XFile* f = XFile_create_2(target);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "link target");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* 17.1 静态 link */
    bool linked = XFile_link_static(target, linkName);
    CHECK(linked, "XFile_link_static 创建符号链接成功");
    CHECK(XFile_exists_static(linkName), "链接文件存在");

    /* 17.2 静态 symLinkTarget */
    XString* resolved = XFile_symLinkTarget_static(linkName);
    CHECK(resolved != NULL, "symLinkTarget_static 返回非 NULL");
    if (resolved) {
        /* 链接目标应包含原文件名 */
        const char* resolvedStr = XString_toUtf8(resolved);
        CHECK(resolvedStr != NULL && strstr(resolvedStr, "xfile_link_target.txt") != NULL,
              "symLinkTarget 指向正确目标");
        XString_delete_base(resolved);
    }

    /* 17.3 成员 symLinkTarget */
    f = XFile_create_2(linkName);
    XString* resolved2 = XFile_symLinkTarget(f);
    CHECK(resolved2 != NULL, "成员 symLinkTarget 返回非 NULL");
    if (resolved2) XString_delete_base(resolved2);
    XFile_deleteLater(f);

    /* 17.4 成员 link */
    XString* linkName2 = XString_create_utf8("xfile_link_name2.txt");
    XFile_remove_static(linkName2);
    f = XFile_create_2(target);
    linked = XFile_link(f, linkName2);
    CHECK(linked, "XFile_link 成员创建链接成功");
    XFile_deleteLater(f);

    /* 通过链接读取内容 */
    f = XFile_create_2(linkName);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    CHECK(ok, "通过符号链接打开文件成功");
    if (ok) {
        XByteArray* data = XIODevice_readAll_3((XIODevice*)f);
        CHECK(XByteArray_size_base(data) == 11, "通过链接读取大小正确");
        CHECK(memcmp(XByteArray_data(data), "link target", 11) == 0, "通过链接读取内容正确");
        XByteArray_delete_base(data);
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(linkName);
    XFile_remove_static(linkName2);
    XFile_remove_static(target);
    XString_delete_base(target);
    XString_delete_base(linkName);
    XString_delete_base(linkName2);
}

/* ============================================================================
 * 18. moveToTrash
 * ============================================================================ */

static void test_moveToTrash(void)
{
    XPrintf_3("\n--- 18. moveToTrash ---\n");

    /* 注意: 当前实现 moveToTrash 等价于 remove */
    XString* fn = XString_create_utf8("xfile_trash_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "trash me");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    CHECK(XFile_exists_static(fn), "moveToTrash 前文件存在");

    /* 静态 moveToTrash */
    bool trashed = XFile_moveToTrash_static(fn, NULL);
    CHECK(trashed, "XFile_moveToTrash_static 成功");
    CHECK(!XFile_exists_static(fn), "moveToTrash 后文件不存在");

    /* 成员 moveToTrash */
    XString* fn2 = XString_create_utf8("xfile_trash_test2.txt");
    XFile_remove_static(fn2);
    f = XFile_create_2(fn2);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "trash me 2");
    XIODevice_close_base((XIODevice*)f);

    trashed = XFile_moveToTrash(f);
    CHECK(trashed, "XFile_moveToTrash 成员成功");
    CHECK(!XFile_exists_static(fn2), "成员 moveToTrash 后文件不存在");
    XFile_deleteLater(f);

    XString_delete_base(fn);
    XString_delete_base(fn2);
}

/* 18.1 打开中的 moveToTrash: Qt 行为是先关闭再放入回收站 */
static void test_moveToTrash_open(void)
{
    XPrintf_3("\n--- 18.1 moveToTrash (打开中) ---\n");
    XString* fn = XString_create_utf8("xfile_trash_open.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "open-trash");
    bool ok = XFile_moveToTrash(f);
    CHECK(ok, "打开中 XFile_moveToTrash 成功");
    CHECK(!XFile_exists_static(fn), "打开中 moveToTrash 后文件不存在");
    XFile_deleteLater(f);
    XString_delete_base(fn);
}

/* ============================================================================
 * 19. map / unmap (内存映射)
 * ============================================================================ */

static void test_mmap(void)
{
    XPrintf_3("\n--- 19. map / unmap ---\n");

    XString* fn = XString_create_utf8("xfile_mmap_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "MMAP_TEST_DATA_1234");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    CHECK(ok, "mmap 测试: 打开文件成功");

    if (ok) {
        void* mapped = XFile_map((XFileDevice*)f, 0, 19, XFileDevice_NoOptions);
        CHECK(mapped != NULL, "map 返回非 NULL");
        if (mapped) {
            CHECK(memcmp(mapped, "MMAP_TEST_DATA_1234", 19) == 0, "map 内容正确");
            bool unmapped = XFile_unmap((XFileDevice*)f, mapped);
            CHECK(unmapped, "unmap 成功");
        }

        /* 部分映射 */
        mapped = XFile_map((XFileDevice*)f, 5, 4, XFileDevice_NoOptions);
        CHECK(mapped != NULL, "部分 map(5,4) 返回非 NULL");
        if (mapped) {
            CHECK(memcmp(mapped, "TEST", 4) == 0, "部分 map 内容正确");
            XFile_unmap((XFileDevice*)f, mapped);
        }

        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 20. encodeName / decodeName
 * ============================================================================ */

static void test_encode_decode(void)
{
    XPrintf_3("\n--- 20. encodeName / decodeName ---\n");

    XString* fn = XString_create_utf8("encode_decode_test.txt");

    /* 20.1 encodeName */
    XByteArray* encoded = XFile_encodeName(fn);
    CHECK(encoded != NULL, "encodeName 返回非 NULL");
    CHECK(XByteArray_size_base(encoded) > 0, "encodeName 结果非空");

    /* 20.2 decodeName (XByteArray) */
    XString* decoded = XFile_decodeName(encoded);
    CHECK(decoded != NULL, "decodeName 返回非 NULL");
        /* Qt 行为: encodeName/decodeName 在 Linux 上是 UTF-8 ↔ XString 互转;
           字节比较避免 XStringView 在字符级别可能的差异. */
        const char* decC = XString_toUtf8(decoded);
        const char* fnC = XString_toUtf8(fn);
        CHECK(decC && fnC && strcmp(decC, fnC) == 0, "decodeName 还原正确 (按字节)");

    /* 20.3 decodeName_2 (C 字符串) */
    XString* decoded2 = XFile_decodeName_2("c_string_test.txt");
    CHECK(decoded2 != NULL, "decodeName_2 返回非 NULL");
    XString* expected = XString_create_utf8("c_string_test.txt");
        const char* decC2 = XString_toUtf8(decoded2);
        const char* expC2 = XString_toUtf8(expected);
        CHECK(decC2 && expC2 && strcmp(decC2, expC2) == 0, "decodeName_2 还原正确 (按字节)");

    XByteArray_delete_base(encoded);
    XString_delete_base(decoded);
    XString_delete_base(decoded2);
    XString_delete_base(expected);
    XString_delete_base(fn);
}

/* ============================================================================
 * 21. Append 模式追加写入验证
 * ============================================================================ */

static void test_append_mode(void)
{
    XPrintf_3("\n--- 21. Append 模式追加写入 ---\n");

    XString* fn = XString_create_utf8("xfile_append_test.txt");
    XFile_remove_static(fn);

    /* 写入初始内容 */
    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "FIRST");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    /* Append 追加 */
    f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_Append);
    CHECK(ok, "Append 模式打开成功");
    if (ok) {
        XIODevice_write_3((XIODevice*)f, "SECOND");
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 验证内容 */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    if (ok) {
        XByteArray* all = XIODevice_readAll_3((XIODevice*)f);
        CHECK(XByteArray_size_base(all) == 11, "Append 后总大小 11");
        CHECK(memcmp(XByteArray_data(all), "FIRSTSECOND", 11) == 0, "Append 后内容正确");
        XByteArray_delete_base(all);
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 22. ReadWrite 混合读写
 * ============================================================================ */

static void test_readwrite(void)
{
    XPrintf_3("\n--- 22. ReadWrite 混合读写 ---\n");

    XString* fn = XString_create_utf8("xfile_rw_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadWrite | XIODevice_NewOnly);
    CHECK(ok, "ReadWrite|NewOnly 打开成功");

    if (ok) {
        /* 写入 */
        XIODevice_write_3((XIODevice*)f, "ABCDEFGHIJ");

        /* seek 回开头读取 */
        XIODevice_seek_base((XIODevice*)f, 0);
        char buf[5] = {0};
        int64_t n = XIODevice_read_1((XIODevice*)f, buf, 4);
        CHECK(n == 4, "ReadWrite 读取 4 字节");
        CHECK(memcmp(buf, "ABCD", 4) == 0, "ReadWrite 读取内容正确");

        /* seek 到位置 5 覆盖写入 */
        XIODevice_seek_base((XIODevice*)f, 5);
        XIODevice_write_1((XIODevice*)f, "XY", 2);

        /* 验证最终内容: 原 0..9=ABCDEFGHIJ, 写 XY 在 5,6 -> ABCDEXYHIJ (10 字节) */
        XIODevice_seek_base((XIODevice*)f, 0);
        XByteArray* all = XIODevice_readAll_3((XIODevice*)f);
        int finalLen = (int)XByteArray_size_base(all);
        const char* finalData = (const char*)XByteArray_data(all);
        CHECK(finalLen == 10, "ReadWrite 后总大小 10 字节");
        CHECK(memcmp(finalData, "ABCDEXYHIJ", 10) == 0, "ReadWrite 覆盖写入后内容正确 (ABCDEXYHIJ)");
        XByteArray_delete_base(all);

        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 23. setTextModeEnabled
 * ============================================================================ */

static void test_text_mode(void)
{
    XPrintf_3("\n--- 23. setTextModeEnabled ---\n");

    XString* fn = XString_create_utf8("xfile_textmode_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);

    CHECK(!XIODevice_isTextModeEnabled((XIODevice*)f), "默认非文本模式");
    XIODevice_setTextModeEnabled((XIODevice*)f, true);
    CHECK(XIODevice_isTextModeEnabled((XIODevice*)f), "setTextModeEnabled(true) 后为文本模式");
    XIODevice_setTextModeEnabled((XIODevice*)f, false);
    CHECK(!XIODevice_isTextModeEnabled((XIODevice*)f), "setTextModeEnabled(false) 后为非文本模式");

    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 24. readChannelCount / writeChannelCount
 * ============================================================================ */

static void test_channels(void)
{
    XPrintf_3("\n--- 24. readChannelCount / writeChannelCount ---\n");

    XString* fn = XString_create_utf8("xfile_channel_test.txt");
    XFile_remove_static(fn);

    XFile* f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);

    int rcc = XIODevice_readChannelCount((XIODevice*)f);
    int wcc = XIODevice_writeChannelCount((XIODevice*)f);
    CHECK(rcc >= 1, "readChannelCount >= 1");
    CHECK(wcc >= 1, "writeChannelCount >= 1");

    int crc = XIODevice_currentReadChannel((XIODevice*)f);
    int cwc = XIODevice_currentWriteChannel((XIODevice*)f);
    CHECK(crc == 0, "currentReadChannel == 0");
    CHECK(cwc == 0, "currentWriteChannel == 0");

    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XString_delete_base(fn);
}

/* ============================================================================
 * 25. 边界条件与错误处理
 * ============================================================================ */

static void test_edge_cases(void)
{
    XPrintf_3("\n--- 25. 边界条件与错误处理 ---\n");

    /* 25.1 空文件名打开应失败 */
    XFile* f = XFile_create();
    bool ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    CHECK(!ok, "空文件名打开应失败");
    XFile_deleteLater(f);

    /* 25.2 未打开时读取应失败/返回 0 */
    XString* fn = XString_create_utf8("xfile_edge_test.txt");
    XFile_remove_static(fn);
    f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_write_3((XIODevice*)f, "data");
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    f = XFile_create_2(fn);
    /* 未打开就读取 */
    char buf[16];
    int64_t n = XIODevice_read_1((XIODevice*)f, buf, sizeof(buf));
    CHECK(n <= 0, "未打开时 read 应返回 <= 0");
    XFile_deleteLater(f);

    /* 25.3 空文件读取 */
    XString* emptyFn = XString_create_utf8("xfile_empty_test.txt");
    XFile_remove_static(emptyFn);
    f = XFile_create_2(emptyFn);
    XIODevice_open_base((XIODevice*)f, XIODevice_WriteOnly | XIODevice_NewOnly);
    XIODevice_close_base((XIODevice*)f);
    XFile_deleteLater(f);

    f = XFile_create_2(emptyFn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    CHECK(ok, "打开空文件成功");
    if (ok) {
        CHECK(XIODevice_size_base((XIODevice*)f) == 0, "空文件 size == 0");
        CHECK(XIODevice_atEnd_base((XIODevice*)f), "空文件 atEnd == true");
        XByteArray* data = XIODevice_readAll_3((XIODevice*)f);
        CHECK(XByteArray_size_base(data) == 0, "空文件 readAll 返回 0 字节");
        XByteArray_delete_base(data);
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    /* 25.4 重复关闭 */
    f = XFile_create_2(fn);
    XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    XIODevice_close_base((XIODevice*)f);
    XIODevice_close_base((XIODevice*)f); /* 第二次关闭不应崩溃 */
    CHECK(true, "重复关闭不崩溃");
    XFile_deleteLater(f);

    /* 25.5 readLine_2 (XByteArray) */
    f = XFile_create_2(fn);
    ok = XIODevice_open_base((XIODevice*)f, XIODevice_ReadOnly);
    if (ok) {
        XByteArray* lineBa = XByteArray_create();
        int64_t ln = XIODevice_readLine_2((XIODevice*)f, lineBa);
        CHECK(ln == 4, "readLine_2 读取 4 字节 (无换行)");
        XByteArray_delete_base(lineBa);
        XIODevice_close_base((XIODevice*)f);
    }
    XFile_deleteLater(f);

    XFile_remove_static(fn);
    XFile_remove_static(emptyFn);
    XString_delete_base(fn);
    XString_delete_base(emptyFn);
}

/* ============================================================================
 * 主测试入口
 * ============================================================================ */

void XFileTest()
{
    XPrintf_3("\n=== XFile 完整 API 测试 ===\n");
    g_pass = 0;
    g_fail = 0;

    test_create_destroy();
    test_setFileName();
    test_open_modes();
    test_open_with_permissions();
    test_open_from_fd();
    test_write_operations();
    test_read_operations();
    test_seek_pos_reset();
    test_handle_error();
    test_file_time();
    test_resize();
    test_permissions();
    test_exists();
    test_remove();
    test_remove_open();
    test_rename();
    test_copy();
    test_link();
    test_moveToTrash();
    test_moveToTrash_open();
    test_mmap();
    test_encode_decode();
    test_append_mode();
    test_readwrite();
    test_text_mode();
    test_channels();
    test_edge_cases();

    XPrintf_3("\n=== XFile 测试结果 ===\n");
    XPrintf("通过: %d, 失败: %d, 总计: %d\n", g_pass, g_fail, g_pass + g_fail);
    if (g_fail == 0) {
        XPrintf_3("全部测试通过!\n");
    } else {
        XPrintf("有 %d 项测试失败，请检查!\n", g_fail);
    }
}

void XMenu_XFileTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XFile(文件操作)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "完整API测试");
        XAction_setAction(action, XFileTest);
    }
}
