/**
 * @file       XVirtualKeyboardDictionaryManager.h
 * @brief      XVirtualKeyboardDictionaryManager 词典管理器进程单例公开
 *             API（对标 Qt 6.8 QVirtualKeyboardDictionaryManager，
 *             qvirtualkeyboarddictionarymanager.h:16-50 全公共面）。
 * @details    API 照抄：instance()+createDictionary/dictionary+四属性
 *             （available/base/extra/active）四信号。偏离如实标注：本
 *             轮实现=注册表/列表管理面；availableDictionaries 恒空列
 *             表（无内容源——Qt 由布局/词典文件喂给）；pinyin 用户词
 *             典开关（pinyininputmethod.cpp:52-56 口径）接到
 *             activeDictionaries 空=关，行为等价。持久化/学习 N-A。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON；实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDDICTIONARYMANAGER_H
#define XVIRTUALKEYBOARDDICTIONARYMANAGER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"

#if XVIRTUALKEYBOARD_ON

/* 前向声明。 */
typedef struct XVirtualKeyboardDictionary XVirtualKeyboardDictionary;

/** @brief 声明 XVirtualKeyboardDictionaryManager 虚函数枚举：继承
 *         XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardDictionaryManager)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardDictionaryManager, XObject)

/**
 * @brief      词典管理器单例对象；m_class 必须为第一个成员。
 * @details    m_data 私有块保存词典注册表（对象拥有词典）。
 */
typedef struct XVirtualKeyboardDictionaryManager
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块，由对象拥有；仅供实现使用。 */
} XVirtualKeyboardDictionaryManager;

/** @brief 初始化类虚函数表并返回共享表指针。 */
XVtable* XVirtualKeyboardDictionaryManager_class_init(void);

/**
 * @brief      进程单例（对标 instance()；惰性创建）。
 * @return     单例借用指针；不得释放。
 */
XVirtualKeyboardDictionaryManager*
XVirtualKeyboardDictionaryManager_instance(void);

/**
 * @brief      创建命名词典（对标 createDictionary；同名已存在返回既
 *             有实例；管理器拥有）。
 * @param      name 词典名（UTF-8 借用）；NULL/空串拒绝。
 * @return     词典借用指针（管理器拥有，进程期有效）；失败返回 NULL。
 */
XVirtualKeyboardDictionary*
XVirtualKeyboardDictionaryManager_createDictionary(
        XVirtualKeyboardDictionaryManager* self, const char* name);

/**
 * @brief      按名取词典（对标 dictionary；未创建返回 NULL）。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @param      name 词典名（UTF-8 借用）；NULL/空串视为不存在。
 * @return     词典借用指针（管理器拥有，不得释放）；未创建或入参非
 *             法返回 NULL。
 */
XVirtualKeyboardDictionary* XVirtualKeyboardDictionaryManager_dictionary(
        const XVirtualKeyboardDictionaryManager* self, const char* name);

/**
 * @brief      移除词典（Qt deleteDictionary 等价扩展；管理器拥有期销毁）。
 * @param      self 管理器对象；可为 NULL，NULL 时不执行操作。
 * @param      name 待移除词典名（UTF-8 借用）。
 * @return     已移除返回 true；不存在或入参非法返回 false。
 */
bool XVirtualKeyboardDictionaryManager_removeDictionary(
        XVirtualKeyboardDictionaryManager* self, const char* name);

/**
 * @brief      可用词典名列表（对标 availableDictionaries）。
 * @details    本轮恒 0（无内容源——如实标注；创建的内存词典不计入）。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @param      outNames 调用方提供的存储空间（const char* 数组，元素为
 *             内部字符串借用指针，不得释放）；可为 NULL，NULL 时仅查
 *             询条数。
 * @param      maxCount outNames 容量上限（条数）；outNames 为 NULL 时
 *             忽略。
 * @return     实际写入条数（本轮恒 0）；outNames 为 NULL 时返回可用
 *             条数（同为本轮恒 0）。
 */
int XVirtualKeyboardDictionaryManager_availableDictionaries(
        const XVirtualKeyboardDictionaryManager* self, const char** outNames,
        int maxCount);

/**
 * @brief      基础词典名列表（对标 baseDictionaries；创建的词典默认
 *             落 base 集）。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @param      outNames 调用方提供的存储空间（const char* 数组，元素为
 *             内部字符串借用指针，不得释放）；可为 NULL，NULL 时仅查
 *             询条数。
 * @param      maxCount outNames 容量上限（条数）；outNames 为 NULL 时
 *             忽略。
 * @return     实际写入条数。
 */
int XVirtualKeyboardDictionaryManager_baseDictionaries(
        const XVirtualKeyboardDictionaryManager* self, const char** outNames,
        int maxCount);

/**
 * @brief      扩展词典名列表（对标 extraDictionaries）。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @param      outNames 调用方提供的存储空间（const char* 数组，元素为
 *             内部字符串借用指针，不得释放）；可为 NULL，NULL 时仅查
 *             询条数。
 * @param      maxCount outNames 容量上限（条数）；outNames 为 NULL 时
 *             忽略。
 * @return     实际写入条数。
 */
int XVirtualKeyboardDictionaryManager_extraDictionaries(
        const XVirtualKeyboardDictionaryManager* self, const char** outNames,
        int maxCount);

/**
 * @brief      激活词典名列表（对标 activeDictionaries；拼音插件用户
 *             词典开关接此：空=关）。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @param      outNames 调用方提供的存储空间（const char* 数组，元素为
 *             内部字符串借用指针，不得释放）；可为 NULL，NULL 时仅查
 *             询条数。
 * @param      maxCount outNames 容量上限（条数）；outNames 为 NULL 时
 *             忽略。
 * @return     实际写入条数。
 */
int XVirtualKeyboardDictionaryManager_activeDictionaries(
        const XVirtualKeyboardDictionaryManager* self, const char** outNames,
        int maxCount);

/**
 * @brief      设置基础词典集合（名字必须已创建；发 baseDictionariesChanged）。
 * @param      self 管理器对象；可为 NULL，NULL 时不执行操作。
 * @param      names 词典名 UTF-8 数组（借用，调用期间有效）；NULL 等
 *             价清空。
 * @param      count 条目数；names 为 NULL 时应为 0。
 * @return     全部名字有效并已设置返回 true；任一名字未创建/入参非
 *             法返回 false 且对象保持不变。
 */
bool XVirtualKeyboardDictionaryManager_setBaseDictionaries(
        XVirtualKeyboardDictionaryManager* self, const char* const* names,
        int count);

/**
 * @brief      设置扩展词典集合（发 extraDictionariesChanged）。
 * @param      self 管理器对象；可为 NULL，NULL 时不执行操作。
 * @param      names 词典名 UTF-8 数组（借用，调用期间有效）；NULL 等
 *             价清空。
 * @param      count 条目数；names 为 NULL 时应为 0。
 * @return     全部名字有效并已设置返回 true；任一名字不存在/入参非
 *             法返回 false 且对象保持不变。
 */
bool XVirtualKeyboardDictionaryManager_setExtraDictionaries(
        XVirtualKeyboardDictionaryManager* self, const char* const* names,
        int count);

/**
 * @brief      设置激活词典集合（发 activeDictionariesChanged）。
 * @param      self 管理器对象；可为 NULL，NULL 时不执行操作。
 * @param      names 词典名 UTF-8 数组（借用，调用期间有效）；NULL 等
 *             价清空。
 * @param      count 条目数；names 为 NULL 时应为 0。
 * @return     全部名字有效并已设置返回 true；任一名字不存在/入参非
 *             法返回 false 且对象保持不变。
 */
bool XVirtualKeyboardDictionaryManager_setActiveDictionaries(
        XVirtualKeyboardDictionaryManager* self, const char* const* names,
        int count);

/* ==================== 信号（四属性四信号，纯 ID getter） ==================== */

/**
 * @brief      availableDictionariesChanged() 信号标识。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardDictionaryManager_availableDictionariesChanged_signal(
        XVirtualKeyboardDictionaryManager* self);
/**
 * @brief      baseDictionariesChanged() 信号标识。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardDictionaryManager_baseDictionariesChanged_signal(
        XVirtualKeyboardDictionaryManager* self);
/**
 * @brief      extraDictionariesChanged() 信号标识。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardDictionaryManager_extraDictionariesChanged_signal(
        XVirtualKeyboardDictionaryManager* self);
/**
 * @brief      activeDictionariesChanged() 信号标识。
 * @param      self 管理器对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardDictionaryManager_activeDictionariesChanged_signal(
        XVirtualKeyboardDictionaryManager* self);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDDICTIONARYMANAGER_H */
