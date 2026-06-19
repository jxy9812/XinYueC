#include"XIOTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XDir.h"
#include"XString.h"
#include"XStringList.h"
#include"XFileInfo.h"
#include"XPrintf.h"

void XDirTest_print_xstring(const char* label, const XString* str) {
    XPrintf("%s: ", label);
    if (str) {
        XPrintf_string(str);
    } else {
        XPrintf_utf8("(NULL)");
    }
    XPrintf_utf8("\n");
}

void XDirTest_print_stringlist(const char* label, const XStringList* list) {
    XPrintf("%s:\n", label);
    if (!list) {
        XPrintf_utf8("  (NULL)\n");
        return;
    }
    size_t count = XStringList_size_base(list);
    for (size_t i = 0; i < count; i++) {
        XString* item = (XString*)XStringList_at_base(list, (int64_t)i);
        if (item ) {
            XPrintf("  [%d] ", (int)i);
            XPrintf_string(item);
            XPrintf_utf8("\n");
        }
    }
}

void XDirTest()
{
    XPrintf_utf8("\n=== XDir 综合测试 ===\n\n");

    // ====== 1. 基本创建与路径操作 ======
    XPrintf_utf8("========== 1. 基本创建与路径操作 ==========\n");

    // 1.1 创建指向当前目录的 XDir
    XDir* dir1 = XDir_create_1();
    if (dir1) {
        XString* currentPath = XDir_currentPath();
        XDirTest_print_xstring("当前目录路径", currentPath);
        
        const XString* path = XDir_path(dir1);
        XDirTest_print_xstring("XDir 路径", path);
        
        XString* absPath = XDir_absolutePath(dir1);
        XDirTest_print_xstring("绝对路径", absPath);
        XString_delete_base(absPath);
        
        XString_delete_base(currentPath);
        XDir_delete_base(dir1);
    }

    // 1.2 创建指定路径的 XDir
    XString* testPath = XString_create_utf8(".");
    XDir* dir2 = XDir_create_2(testPath);
    if (dir2) {
        XString* dirName = XDir_dirName(dir2);
        XDirTest_print_xstring("目录名称", dirName);
        XString_delete_base(dirName);
        
        XDir_delete_base(dir2);
    }
    XString_delete_base(testPath);

    // ====== 2. 目录内容枚举 ======
    XPrintf_utf8("\n========== 2. 目录内容枚举 ==========\n");

    XString* dotPath = XString_create_utf8(".");
    XDir* dir3 = XDir_create_2(dotPath);
    if (dir3) {
        // 设置过滤器：只显示文件和目录
        XDir_setFilter(dir3, XDir_Files | XDir_Dirs | XDir_NoDotAndDotDot);
        XDir_setSorting(dir3, XDir_Name);

        // 获取条目列表
        XStringList* entries = XDir_entryList_1(dir3, XDir_NoFilter, XDir_NoSort);
        XDirTest_print_stringlist("当前目录内容", entries);
        XStringList_delete_base(entries);

        // 使用名称过滤器
        XStringList* filters = XStringList_create();
        XStringList_push_back_utf8(filters, "*.c");
        XStringList_push_back_utf8(filters, "*.h");
        
        XStringList* filtered = XDir_entryList_2(dir3, filters, XDir_Files, XDir_Name);
        XDirTest_print_stringlist("C/H 文件", filtered);
        XStringList_delete_base(filtered);
        XStringList_delete_base(filters);

        XDir_delete_base(dir3);
    }
    XString_delete_base(dotPath);

    // ====== 3. 目录导航 ======
    XPrintf_utf8("\n========== 3. 目录导航 ==========\n");

    XString* srcPath = XString_create_utf8("Src");
    XDir* dir4 = XDir_create_2(srcPath);
    if (dir4) {
        XDirTest_print_xstring("初始路径", XDir_path(dir4));

        // 切换到子目录
        XString* xcodeSubdir = XString_create_utf8("XCode");
        if (XDir_cd(dir4, xcodeSubdir)) {
            XDirTest_print_xstring("cd XCode 后", XDir_path(dir4));
        }
        XString_delete_base(xcodeSubdir);

        // 切换到上级目录
        if (XDir_cdUp(dir4)) {
            XDirTest_print_xstring("cdUp 后", XDir_path(dir4));
        }

        XDir_delete_base(dir4);
    }
    XString_delete_base(srcPath);

    // ====== 4. 目录操作 ======
    XPrintf_utf8("\n========== 4. 目录操作 ==========\n");

    // 创建测试目录
    XString* testDirPath = XString_create_utf8("TestDir_XDirTest");
    XDir* dir5 = XDir_create_1();
    if (dir5) {
        // 创建目录
        bool created = XDir_mkdir(dir5, testDirPath);
        XPrintf("创建目录 \"TestDir_XDirTest\": %s\n", created ? "成功" : "失败");

        // 检查目录是否存在
        bool exists = XDir_exists_2(dir5, testDirPath);
        XPrintf("目录是否存在: %s\n", exists ? "是" : "否");

        // 删除目录
        bool removed = XDir_rmdir(dir5, testDirPath);
        XPrintf("删除目录: %s\n", removed ? "成功" : "失败");

        XDir_delete_base(dir5);
    }
    XString_delete_base(testDirPath);

    // ====== 5. 特殊目录 ======
    XPrintf_utf8("\n========== 5. 特殊目录 ==========\n");

    XString* homePath = XDir_homePath();
    XDirTest_print_xstring("用户主目录", homePath);
    XString_delete_base(homePath);

    XString* tempPath = XDir_tempPath();
    XDirTest_print_xstring("临时目录", tempPath);
    XString_delete_base(tempPath);

    XString* rootPath = XDir_rootPath();
    XDirTest_print_xstring("根目录", rootPath);
    XString_delete_base(rootPath);

    // ====== 6. 路径操作静态函数 ======
    XPrintf_utf8("\n========== 6. 路径操作静态函数 ==========\n");

    XString* messyPath = XString_create_utf8("./Src/../Src/./XCode//");
    XString* cleanPath = XDir_cleanPath(messyPath);
    XDirTest_print_xstring("清理路径 \"./Src/../Src/./XCode//\"", cleanPath);
    XString_delete_base(messyPath);
    XString_delete_base(cleanPath);

    // 检查绝对路径
    XString* absTestPath = XString_create_utf8("/usr/local");
    bool isAbs = XDir_isAbsolutePath(absTestPath);
    XPrintf("\"/usr/local\" 是绝对路径: %s\n", isAbs ? "是" : "否");
    XString_delete_base(absTestPath);

    XString* relTestPath = XString_create_utf8("Src/XCode");
    bool isRel = XDir_isRelativePath(relTestPath);
    XPrintf("\"Src/XCode\" 是相对路径: %s\n", isRel ? "是" : "否");
    XString_delete_base(relTestPath);

    XPrintf_utf8("\n=== XDir 测试完成 ===\n");
}

void XMenu_XDirTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XDir(目录操作)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XDirTest);
    }
}
