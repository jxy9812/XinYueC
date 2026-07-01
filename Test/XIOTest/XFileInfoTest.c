#include"XIOTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XFileInfo.h"
#include"XFile.h"
#include"XString.h"
#include"XDir.h"
#include"XDateTime.h"
#include"XPrintf.h"

void XFileInfoTest_print_xstring(const char* label, const XString* str) {
    XPrintf("%s: ", label);
    if (str) {
        XPrintf_2(str);
    } else {
        XPrintf_3("(NULL)");
    }
    XPrintf_3("\n");
}

void XFileInfoTest_print_datetime(const char* label, XDateTime* dt) {
    XPrintf("%s: ", label);
    if (dt && XDateTime_isValid(dt)) {
        XString* dtStr = XDateTime_toString_iso(dt);
        XPrintf_2(dtStr);
        XString_delete_base(dtStr);
    } else {
        XPrintf_3("(无效)");
    }
    XPrintf_3("\n");
}

void XFileInfoTest()
{
    XPrintf_3("\n=== XFileInfo 综合测试 ===\n\n");

    // ====== 1. 创建测试文件 ======
    XPrintf_3("========== 1. 创建测试文件 ==========\n");

    XString* testFileName = XString_create_utf8("XFileInfoTest_temp.txt");
    XFile* testFile = XFile_create_2(testFileName);
    if (testFile) {
        bool opened = XIODevice_open_base((XIODevice*)testFile, XIODevice_WriteOnly| XIODevice_NewOnly | XIODevice_Text);
        if (opened) {
            const char* content = "Hello, XFileInfo!\nThis is a test file.\n";
            XByteArray* writeData = XByteArray_create_utf8(content);
            XIODevice_write_2((XIODevice*)testFile, writeData);
            XByteArray_delete_base(writeData);
            XIODevice_close_base((XIODevice*)testFile);
        }
        XFile_deleteLater(testFile);
    }
    XPrintf_3("创建测试文件: 完成\n");

    // ====== 2. 基本文件信息 ======
    XPrintf_3("\n========== 2. 基本文件信息 ==========\n");

    XFileInfo* info1 = XFileInfo_create_2(testFileName);
    if (info1) {
        // 文件路径
        XFileInfoTest_print_xstring("文件路径", XFileInfo_filePath(info1));
        
        XString* absPath = XFileInfo_absoluteFilePath(info1);
        XFileInfoTest_print_xstring("绝对路径", absPath);
        XString_delete_base(absPath);

        XString* fileName = XFileInfo_fileName(info1);
        XFileInfoTest_print_xstring("文件名", fileName);
        XString_delete_base(fileName);

        XString* basePath = XFileInfo_path(info1);
        XFileInfoTest_print_xstring("所在目录", basePath);
        XString_delete_base(basePath);

        XFileInfo_delete_base(info1);
    }

    // ====== 3. 文件名解析 ======
    XPrintf_3("\n========== 3. 文件名解析 ==========\n");

    XFileInfo* info2 = XFileInfo_create_2(testFileName);
    if (info2) {
        XString* baseName = XFileInfo_baseName(info2);
        XFileInfoTest_print_xstring("基本名称", baseName);
        XString_delete_base(baseName);

        XString* suffix = XFileInfo_suffix(info2);
        XFileInfoTest_print_xstring("后缀", suffix);
        XString_delete_base(suffix);

        XString* completeBaseName = XFileInfo_completeBaseName(info2);
        XFileInfoTest_print_xstring("完整基本名称", completeBaseName);
        XString_delete_base(completeBaseName);

        XFileInfo_delete_base(info2);
    }

    // ====== 4. 文件类型检查 ======
    XPrintf_3("\n========== 4. 文件类型检查 ==========\n");

    XFileInfo* info3 = XFileInfo_create_2(testFileName);
    if (info3) {
        XPrintf("文件存在: %s\n", XFileInfo_exists(info3) ? "是" : "否");
        XPrintf("是文件: %s\n", XFileInfo_isFile(info3) ? "是" : "否");
        XPrintf("是目录: %s\n", XFileInfo_isDir(info3) ? "是" : "否");
        XPrintf("是符号链接: %s\n", XFileInfo_isSymLink(info3) ? "是" : "否");
        XPrintf("是隐藏文件: %s\n", XFileInfo_isHidden(info3) ? "是" : "否");
        XPrintf("是根目录: %s\n", XFileInfo_isRoot(info3) ? "是" : "否");

        XFileInfo_delete_base(info3);
    }

    // ====== 5. 文件属性 ======
    XPrintf_3("\n========== 5. 文件属性 ==========\n");

    XFileInfo* info4 = XFileInfo_create_2(testFileName);
    if (info4) {
        int64_t size = XFileInfo_size(info4);
        XPrintf("文件大小: %lld 字节\n", (long long)size);

        XPrintf("可读: %s\n", XFileInfo_isReadable(info4) ? "是" : "否");
        XPrintf("可写: %s\n", XFileInfo_isWritable(info4) ? "是" : "否");
        XPrintf("可执行: %s\n", XFileInfo_isExecutable(info4) ? "是" : "否");

        XFileInfo_delete_base(info4);
    }

    // ====== 6. 时间信息 ======
    XPrintf_3("\n========== 6. 时间信息 ==========\n");

    XFileInfo* info5 = XFileInfo_create_2(testFileName);
    if (info5) {
        XDateTime birthTime = XFileInfo_birthTime(info5);
        XFileInfoTest_print_datetime("创建时间", &birthTime);

        XDateTime modTime = XFileInfo_lastModified(info5);
        XFileInfoTest_print_datetime("修改时间", &modTime);

        XDateTime readTime = XFileInfo_lastRead(info5);
        XFileInfoTest_print_datetime("访问时间", &readTime);

        XFileInfo_delete_base(info5);
    }

    // ====== 7. 路径类型检查 ======
    XPrintf_3("\n========== 7. 路径类型检查 ==========\n");

    XFileInfo* info6 = XFileInfo_create_2(testFileName);
    if (info6) {
        XPrintf("是绝对路径: %s\n", XFileInfo_isAbsolute(info6) ? "是" : "否");
        XPrintf("是相对路径: %s\n", XFileInfo_isRelative(info6) ? "是" : "否");

        XFileInfo_delete_base(info6);
    }

    // 检查绝对路径
    XString* absTestPath = XString_create_utf8("/usr/local");
    XFileInfo* absInfo = XFileInfo_create_2(absTestPath);
    if (absInfo) {
        XPrintf("\"/usr/local\" 是绝对路径: %s\n", XFileInfo_isAbsolute(absInfo) ? "是" : "否");
        XFileInfo_delete_base(absInfo);
    }
    XString_delete_base(absTestPath);

    // ====== 8. 目录信息 ======
    XPrintf_3("\n========== 8. 目录信息 ==========\n");

    XString* dotPath = XString_create_utf8(".");
    XFileInfo* dirInfo = XFileInfo_create_2(dotPath);
    if (dirInfo) {
        XPrintf_3("当前目录信息:\n");
        XPrintf("  存在: %s\n", XFileInfo_exists(dirInfo) ? "是" : "否");
        XPrintf("  是目录: %s\n", XFileInfo_isDir(dirInfo) ? "是" : "否");
        XPrintf("  是文件: %s\n", XFileInfo_isFile(dirInfo) ? "是" : "否");

        XFileInfo_delete_base(dirInfo);
    }
    XString_delete_base(dotPath);

    // ====== 9. 清理测试文件 ======
    XPrintf_3("\n========== 9. 清理测试文件 ==========\n");

    bool removed = XFile_remove_static(testFileName);
    XPrintf("删除测试文件: %s\n", removed ? "成功" : "失败");

    XString_delete_base(testFileName);

    XPrintf_3("\n=== XFileInfo 测试完成 ===\n");
}

void XMenu_XFileInfoTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XFileInfo(文件信息)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XFileInfoTest);
    }
}
