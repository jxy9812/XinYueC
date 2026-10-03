/**
 * @file       XVirtualKeyboardDictionary.h
 * @brief      XVirtualKeyboardDictionary 键盘词典公开 API（对标 Qt 6.8
 *             QVirtualKeyboardDictionary，qvirtualkeyboarddictionary.h
 *             全公共面）。
 * @details    API 照抄：name() 只读、contents/setContents/resetContents、
 *             contentsChanged 信号。构造私有（仅 DictionaryManager 创
 *             建，Qt friend 口径；创建入口落 DictionaryManager 保护头）。
 *             本轮实现=内存词典面；持久化/学习/解码器挂接 N-A
 *             （scopeOut）。contents 为 UTF-8 串数组（Qt 为
 *             QStringList），逐条经 contentsAt 取用。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON；实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDDICTIONARY_H
#define XVIRTUALKEYBOARDDICTIONARY_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"

#if XVIRTUALKEYBOARD_ON

/** @brief 声明 XVirtualKeyboardDictionary 虚函数枚举：继承 XObject
 *         （无新增槽位）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardDictionary)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardDictionary, XObject)

/**
 * @brief      词典对象；m_class 必须为第一个成员。
 * @details    m_data 私有块保存名字与词条（对象拥有）。
 */
typedef struct XVirtualKeyboardDictionary
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块，由对象拥有；仅供实现使用。 */
} XVirtualKeyboardDictionary;

/**
 * @brief      初始化类虚函数表并返回共享表指针。
 * @return     类共享虚函数表指针（进程期常驻，借用）；不失败。
 */
XVtable* XVirtualKeyboardDictionary_class_init(void);

/* ==================== 生命周期（所有权边界） ==================== */


/**
 * @brief      返回词典名（对标 name；创建时确定，只读）。
 * @param      self 词典对象借用指针；可为 NULL。
 * @return     UTF-8 借用指针（内部存储，不得释放或修改）；self 为
 *             NULL 返 NULL。
 */
const char* XVirtualKeyboardDictionary_name(
        const XVirtualKeyboardDictionary* self);

/**
 * @brief      返回词条数（对标 contents().count()）。
 * @param      self 词典对象借用指针；可为 NULL。
 * @return     词条条数；self 为 NULL 返回 0。
 */
int XVirtualKeyboardDictionary_contentsCount(
        const XVirtualKeyboardDictionary* self);

/**
 * @brief      返回第 index 条词条（对标 contents().at(index)）。
 * @param      self 词典对象借用指针；可为 NULL。
 * @param      index 词条索引（0 基）。
 * @return     UTF-8 借用指针（内部存储，不得释放或修改）；越界或
 *             self 为 NULL 返回 NULL。
 */
const char* XVirtualKeyboardDictionary_contentsAt(
        const XVirtualKeyboardDictionary* self, int index);

/**
 * @brief      设置词条集（对标 setContents；拷贝语义，变化发
 *             contentsChanged）。
 * @param      self 词典对象；可为 NULL。
 * @param      contents 词条 UTF-8 数组（借用，调用期间有效）；NULL 等价清空。
 * @param      count 条数；contents 为 NULL 时应为 0。
 * @return     无返回值；self 为 NULL 时保持原状态（拷贝失败同样保持
 *             原状态）。
 */
void XVirtualKeyboardDictionary_setContents(XVirtualKeyboardDictionary* self,
                                            const char* const* contents,
                                            int count);

/**
 * @brief      清空词条（对标 resetContents；变化发 contentsChanged）。
 * @param      self 词典对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XVirtualKeyboardDictionary_resetContents(
        XVirtualKeyboardDictionary* self);

/**
 * @brief      contentsChanged() 信号标识（词条集变化）。
 * @param      self 词典对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardDictionary_contentsChanged_signal(
        XVirtualKeyboardDictionary* self);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDDICTIONARY_H */
