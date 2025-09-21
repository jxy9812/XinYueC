#include"XLibraryTest.h"
#include"cJSON.h"
#include"XMemory.h"
#include"XString.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#if DEMOTEST
void cJsonTest()
{
    const char* json_str = "{\"name\":\"Bob\",\"age\":40,\"city\":\"Chicago\"}";
    // 解析 JSON 字符串
    cJSON* root = cJSON_Parse(json_str);
    if (root == NULL) {
        return 1;
    }

    // 修改 JSON 对象中的值
    cJSON* age = cJSON_GetObjectItemCaseSensitive(root, "age");
    if (cJSON_IsNumber(age)) {
        cJSON_SetNumberValue(age,410);
    }

    // 删除 JSON 对象中的键值对
    cJSON_DeleteItemFromObject(root, "city");

    // 将修改后的 JSON 对象转换为字符串
    char* json_str_modified = cJSON_Print(root);
    if (json_str_modified == NULL) {
        cJSON_Delete(root);
        return 1;
    }

    // 打印修改后的 JSON 字符串
    XPrintf("%s\n", json_str_modified);

    // 释放字符串和 cJSON 对象
    XMemory_free(json_str_modified);
    cJSON_Delete(root);
    XCoreApplication_quit();
}

void XMenu_CJsonTest(XMenu* root)
{
    XMenu* menu = XMenu_create("CJson(Json)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, cJsonTest);
    }
}
#endif