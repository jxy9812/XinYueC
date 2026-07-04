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

static void XFileTest_print_xstring(const char* label, const XString* str) {
    XPrintf("%s: ", label);
    if (str) {
        XPrintf_2(str);
    } else {
        XPrintf_3("(NULL)");
    }
    XPrintf_3("\n");
}

static void XFileTest_print_bytearray(const char* label, const XByteArray* ba) {
    XPrintf("%s: ", label);
    if (!ba) {
        XPrintf_3("(NULL)\n");
        return;
    }
    XPrintf_3("[");
    size_t len = XByteArray_size_base(ba);
    if (len > 100) len = 100;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = XByteArray_at_base(ba, (int64_t)i);
        if (byte) {
            char c = (char)byte;
            if (c >= 32 && c < 127) {
                putchar(c);
            } else {
                putchar('.');
            }
        }
    }
    XPrintf_3("]\n");
}

void XFileTest()
{
    XPrintf_3("\n=== XFile 综合测试 ===\n\n");

    // ====== 1. 基本创建与打开 ======
    XPrintf_3("========== 1. 基本创建与打开 ==========\n");

    // 1.1 创建空文件对象
    XFile* file1 = XFile_create();
    if (file1) {
        XPrintf_3("创建空 XFile 对象: 成功\n");
        XFile_deleteLater(file1);
    }

    // 1.2 创建并打开文件
    XString* testFileName = XString_create_utf8("XFileTest_temp.txt");
    XFile* file2 = XFile_create_2(testFileName);
    if (file2) {
        XFileTest_print_xstring("文件名", testFileName);

        // 打开文件进行写入
        bool opened = XIODevice_open_base((XIODevice*)file2, XIODevice_WriteOnly |XIODevice_NewOnly| XIODevice_Text);
        XPrintf("打开文件(写入模式): %s\n", opened ? "成功" : "失败");

        if (opened) {
            // 写入数据
            const char* content = "Hello, XFile!\nThis is test content.\n";
            XByteArray* writeData = XByteArray_create_utf8(content);
            
            int64_t bytesWritten = XIODevice_write_2((XIODevice*)file2, writeData);
            XPrintf("写入字节数: %lld\n", (long long)bytesWritten);
            
            XByteArray_delete_base(writeData);
            XIODevice_close_base((XIODevice*)file2);
        }

        XFile_deleteLater(file2);
    }

    // ====== 2. 读取文件 ======
    XPrintf_3("\n========== 2. 读取文件 ==========\n");

    XFile* file3 = XFile_create_2(testFileName);
    if (file3) {
        bool opened = XIODevice_open_base((XIODevice*)file3, XIODevice_ReadOnly | XIODevice_Text);
        XPrintf("打开文件(读取模式): %s\n", opened ? "成功" : "失败");

        if (opened) {
            // 获取文件大小
            int64_t fileSize = XIODevice_size_base((XIODevice*)file3);
            XPrintf("文件大小: %lld 字节\n", (long long)fileSize);

            // 读取全部内容
            XByteArray* readData = XIODevice_readAll_3((XIODevice*)file3);
            XFileTest_print_bytearray("文件内容", readData);
            XByteArray_delete_base(readData);

            XIODevice_close_base((XIODevice*)file3);
        }

        XFile_deleteLater(file3);
    }

    // ====== 3. 文件操作 ======
    XPrintf_3("\n========== 3. 文件操作 ==========\n");

    // 检查文件是否存在
    bool exists = XFile_exists_static(testFileName);
    XPrintf("文件存在: %s\n", exists ? "是" : "否");

    // 复制文件
    XString* copyFileName = XString_create_utf8("XFileTest_temp_copy.txt");
    bool copied = XFile_copy_static(testFileName, copyFileName);
    XPrintf("复制文件: %s\n", copied ? "成功" : "失败");

    // 检查复制的文件是否存在
    bool copyExists = XFile_exists_static(copyFileName);
    XPrintf("复制文件存在: %s\n", copyExists ? "是" : "否");

    // 重命名文件
    XString* renameFileName = XString_create_utf8("XFileTest_temp_renamed.txt");
    bool renamed = XFile_rename_static(copyFileName, renameFileName);
    XPrintf("重命名文件: %s\n", renamed ? "成功" : "失败");

    // 检查重命名后的文件
    bool renameExists = XFile_exists_static(renameFileName);
    XPrintf("重命名文件存在: %s\n", renameExists ? "是" : "否");

    // ====== 4. 清理临时文件 ======
    XPrintf_3("\n========== 4. 清理临时文件 ==========\n");
    
    bool removed1 = XFile_remove_static(testFileName);
    XPrintf("删除原文件: %s\n", removed1 ? "成功" : "失败");

    bool removed2 = XFile_remove_static(renameFileName);
    XPrintf("删除重命名文件: %s\n", removed2 ? "成功" : "失败");

    // 清理
    XString_delete_base(testFileName);
    XString_delete_base(copyFileName);
    XString_delete_base(renameFileName);

    XPrintf_3("\n=== XFile 测试完成 ===\n");
}

void XMenu_XFileTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XFile(文件操作)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XFileTest);
    }
}
