#include"XIOTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XSaveFile.h"
#include"XFile.h"
#include"XString.h"
#include"XByteArray.h"
#include"XDir.h"
#include"XPrintf.h"

void XSaveFileTest_print_xstring(const char* label, const XString* str) {
    XPrintf("%s: ", label);
    if (str) {
        XPrintf_2(str);
    } else {
        XPrintf_3("(NULL)");
    }
    XPrintf_3("\n");
}

void XSaveFileTest()
{
    XPrintf_3("\n=== XSaveFile 综合测试 ===\n\n");

    XString* testFileName = XString_create_utf8("XSaveFileTest_output.txt");

    // ====== 1. 基本创建与打开 ======
    XPrintf_3("========== 1. 基本创建与打开 ==========\n");
    size_t size = 0;
    XSaveFile* saveFile1 = XSaveFile_create_2(testFileName);
    if (saveFile1) {
        XSaveFileTest_print_xstring("目标文件名", testFileName);

        // 打开文件进行写入
        bool opened = XIODevice_open_base((XIODevice*)saveFile1, XIODevice_WriteOnly | XIODevice_Text);
        XPrintf("打开文件(写入模式): %s\n", opened ? "成功" : "失败");

        if (opened) {
            // 写入数据
            const char* lines[] = {
                "XSaveFile 测试文件\n",
                "这是第一行内容\n",
                "这是第二行内容\n",
                "这是第三行内容\n",
                //"测试完成\n"
                "end\n"
            };

            for (int i = 0; i < 5; i++) {
                XByteArray* writeData = XByteArray_create_utf8(lines[i]);
                size += XContainerSize(writeData);
                XIODevice_write_2((XIODevice*)saveFile1, writeData);
                XByteArray_delete_base(writeData);
            }

            XPrintf_3("写入内容: 完成\n");

            // 提交更改
            bool committed = XSaveFile_commit(saveFile1);
            XPrintf("提交更改: %s\n", committed ? "成功" : "失败");
        }

        XSaveFile_deleteLater(saveFile1);
    }

    // ====== 2. 验证写入的文件 ======
    XPrintf_3("\n========== 2. 验证写入的文件 ==========\n");

    bool exists = XFile_exists_static(testFileName);
    XPrintf("文件存在: %s\n", exists ? "是" : "否");

    if (exists) {
        XFile* readFile = XFile_create_2(testFileName);
        if (readFile && XIODevice_open_base((XIODevice*)readFile, XIODevice_ReadOnly | XIODevice_Text)) {
            XByteArray* content = XIODevice_readAll_3((XIODevice*)readFile);
            if (content) {
                XByteArray_append_1(content, 0);
                XString* strContent = XString_create_utf8((const char*)XByteArray_data(content));
                XPrintf_3("文件内容:\n");
                XPrintf_2(strContent);
                XString_delete_base(strContent);
                XByteArray_delete_base(content);
            }
            XIODevice_close_base((XIODevice*)readFile);
            XFile_deleteLater(readFile);
        }
    }

    // ====== 3. 取消写入测试 ======
    XPrintf_3("\n========== 3. 取消写入测试 ==========\n");

    XString* cancelFileName = XString_create_utf8("XSaveFileTest_cancel.txt");
    XSaveFile* saveFile2 = XSaveFile_create_2(cancelFileName);
    if (saveFile2) {
        bool opened = XIODevice_open_base((XIODevice*)saveFile2, XIODevice_WriteOnly | XIODevice_Text);
        if (opened) {
            // 写入数据
            XByteArray* writeData = XByteArray_create_utf8("这不应该被保存\n");
            XIODevice_write_2((XIODevice*)saveFile2, writeData);
            XByteArray_delete_base(writeData);

            XPrintf_3("写入临时数据: 完成\n");

            // 取消写入
            XSaveFile_cancelWriting(saveFile2);
            XPrintf_3("取消写入: 已调用\n");

            // 尝试提交（应该失败）
            bool committed = XSaveFile_commit(saveFile2);
            XPrintf("提交（已取消）: %s\n", committed ? "成功" : "失败（预期）");
        }
        XSaveFile_deleteLater(saveFile2);
    }

    // 验证取消的文件不存在
    bool cancelExists = XFile_exists_static(cancelFileName);
    XPrintf("取消的文件存在: %s\n", cancelExists ? "是（意外）" : "否（预期）");

    // ====== 4. 直接写入回退模式 ======
    XPrintf_3("\n========== 4. 直接写入回退模式 ==========\n");

    XString* directFileName = XString_create_utf8("XSaveFileTest_direct.txt");
    XSaveFile* saveFile3 = XSaveFile_create_2(directFileName);
    if (saveFile3) {
        // 启用直接写入回退
        XSaveFile_setDirectWriteFallback(saveFile3, true);
        bool directWrite = XSaveFile_directWriteFallback(saveFile3);
        XPrintf("直接写入回退: %s\n", directWrite ? "启用" : "禁用");

        bool opened = XIODevice_open_base((XIODevice*)saveFile3, XIODevice_WriteOnly | XIODevice_Text);
        if (opened) {
            XByteArray* writeData = XByteArray_create_utf8("直接写入模式测试\n");
            XIODevice_write_2((XIODevice*)saveFile3, writeData);
            XByteArray_delete_base(writeData);

            bool committed = XSaveFile_commit(saveFile3);
            XPrintf("提交（直接写入）: %s\n", committed ? "成功" : "失败");
        }
        XSaveFile_deleteLater(saveFile3);
    }

    // 验证直接写入的文件
    bool directExists = XFile_exists_static(directFileName);
    XPrintf("直接写入文件存在: %s\n", directExists ? "是" : "否");

    // ====== 5. 追加模式测试 ======
    XPrintf_3("\n========== 5. 追加模式测试 ==========\n");
    XPrintf_3("注意: XSaveFile 不支持 Append 模式（因为它使用临时文件机制）\n");
    XPrintf_3("如需追加内容，请使用 XFile 类\n");
    
    // 使用 XFile 进行追加操作演示
    XFile* appendFile = XFile_create_2(testFileName);
    if (appendFile) {
        bool opened = XIODevice_open_base((XIODevice*)appendFile, XIODevice_WriteOnly | XIODevice_Append | XIODevice_Text);
        if (opened) {
            XByteArray* writeData = XByteArray_create_utf8("追加的内容（使用 XFile）\n");
            XIODevice_write_2((XIODevice*)appendFile, writeData);
            XByteArray_delete_base(writeData);
            XIODevice_close_base((XIODevice*)appendFile);
            XPrintf_3("使用 XFile 追加内容: 成功\n");
        } else {
            XPrintf_3("打开文件追加模式: 失败\n");
        }
        XFile_deleteLater(appendFile);
    }

    // ====== 6. 清理测试文件 ======
    XPrintf_3("\n========== 6. 清理测试文件 ==========\n");

    XFile_remove_static(testFileName);
    XFile_remove_static(cancelFileName);
    XFile_remove_static(directFileName);

    XPrintf_3("删除测试文件: 完成\n");

    XString_delete_base(testFileName);
    XString_delete_base(cancelFileName);
    XString_delete_base(directFileName);

    XPrintf_3("\n=== XSaveFile 测试完成 ===\n");
}

void XMenu_XSaveFileTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XSaveFile(安全文件保存)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XSaveFileTest);
    }
}
