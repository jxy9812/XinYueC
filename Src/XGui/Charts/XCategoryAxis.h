/**
 * @file       XCategoryAxis.h
 * @brief      XCategoryAxis 类别轴（对标 Qt Charts 6.8 QBarCategoryAxis）。
 * @details    以 m_base 组合继承 XAbstractAxis（可见性/网格/标题等公共
 *             属性在基类）；本类持有类别字符串列表。Qt 的 QBarCategoryAxis
 *             用「类别字符串」作为 min/max（不是数值），故本类额外提供
 *             字符串形态的 setMin/setMax/min/max/setRange（_2 后缀的
 *             UTF-8 重载 + XString 主版本），数值形态的 setRange 保留为
 *             类别下标裁剪语义（内部渲染需要）。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCATEGORYAXIS_H
#define XCATEGORYAXIS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractAxis.h"

#if XCHARTS_ON

/** @brief 类别轴（m_base 必须是第一个成员；对标 QBarCategoryAxis）。 */
typedef struct XCategoryAxis
{
    XAbstractAxis m_base;     /**< 基类成员；必须是第一个（可见/范围/标题等）。 */
    XString** m_categories;   /**< 类别标签数组（对象拥有；堆）。 */
    int m_count;              /**< 类别数。 */
    int m_capacity;           /**< 容量。 */
} XCategoryAxis;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XCategoryAxis_class_init(void);
/** @brief 初始化类别轴。 @param self 目标轴指针。 @return 无返回值。 */
void XCategoryAxis_init(XCategoryAxis* self);
/** @brief 使用指定内存类型创建类别轴。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XCategoryAxis* XCategoryAxis_create_ex(XMemoryType memory);
#define XCategoryAxis_create() XCategoryAxis_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/** @brief 反初始化（释放类别数组与基类资源）。 @param self 目标轴指针。 @return 无返回值。 */
void XCategoryAxis_deinit_impl(XCategoryAxis* self);

/* ==================== 类型查询（对标 QBarCategoryAxis） ==================== */

/** @brief 查询轴类型（对标 QBarCategoryAxis::type）。 @param self 目标轴指针。 @return 固定返回 AxisTypeBarCategory。 */
int XCategoryAxis_type(const XCategoryAxis* self);

/* ==================== 类别集合（对标 QBarCategoryAxis） ==================== */

/** @brief 追加类别（XString 主版本；对标 append(QString)）。
 * @param self 目标轴指针。
 * @param label 借用 XString*；不能为 NULL。
 * @return 下标；失败 -1。 */
int XCategoryAxis_append(XCategoryAxis* self, const XString* label);
/** @brief 追加类别（UTF-8 编码版本；对标 append(QString)）。
 * @param self 目标轴指针。
 * @param label UTF-8 类别名（借用，不转移所有权）；不能为 NULL。
 * @return 新类别下标；参数非法或分配失败返回 -1。 */
int XCategoryAxis_append_utf8(XCategoryAxis* self, const char* label);
/** @brief 追加类别（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param label UTF-8 类别名（借用）；不能为 NULL。
 * @return 新类别下标；失败返回 -1。 */
int XCategoryAxis_append_2(XCategoryAxis* self, const char* label);
/** @brief 批量追加类别（对标 append(QStringList)）。
 * @param self 目标轴指针。
 * @param labels 借用 XString* 数组；NULL 项跳过。
 * @param count 数量。
 * @return 实际追加数。 */
int XCategoryAxis_appendList(XCategoryAxis* self, const XString* const* labels,
                             int count);
/** @brief 批量设置类别（替换全部；对标 setCategories）。
 * @param self 目标轴指针。
 * @param labels 借用 XString* 数组。
 * @param count 数量。
 * @return 无返回值。 */
void XCategoryAxis_setCategories(XCategoryAxis* self,
                                 const XString* const* labels, int count);
/** @brief 移除首个名为 label 的类别（对标 remove(QString)）。 @param self 目标轴指针。 @param label 借用 XString*。 @return 移除成功返回 true。 */
bool XCategoryAxis_remove(XCategoryAxis* self, const XString* label);
/** @brief 移除首个名为 label 的类别（UTF-8 编码版本；对标 remove）。
 * @param self 目标轴指针。
 * @param label UTF-8 类别名（借用）；不能为 NULL。
 * @return 找到并移除返回 true；未找到返回 false 且轴不变。 */
bool XCategoryAxis_remove_utf8(XCategoryAxis* self, const char* label);
/** @brief 移除类别（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param label UTF-8 类别名（借用）；不能为 NULL。
 * @return 找到并移除返回 true；未找到返回 false。 */
bool XCategoryAxis_remove_2(XCategoryAxis* self, const char* label);
/** @brief 在指定下标插入类别（对标 insert）。 @param self 目标轴指针。 @param index 插入位置（越界钳位）。 @param label 借用 XString*。 @return 插入成功返回 true。 */
bool XCategoryAxis_insert(XCategoryAxis* self, int index, const XString* label);
/** @brief 在指定下标插入类别（UTF-8 编码版本；对标 insert）。
 * @param self 目标轴指针。
 * @param index 插入位置（越界按 [0,count] 钳位）。
 * @param label UTF-8 类别名（借用）；不能为 NULL。
 * @return 插入成功返回 true。 */
bool XCategoryAxis_insert_utf8(XCategoryAxis* self, int index, const char* label);
/** @brief 插入类别（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param index 插入位置。
 * @param label UTF-8 类别名（借用）；不能为 NULL。
 * @return 插入成功返回 true。 */
bool XCategoryAxis_insert_2(XCategoryAxis* self, int index, const char* label);
/** @brief 替换类别名（对标 replace）。 @param self 目标轴指针。 @param oldLabel 借用 XString* 旧名。 @param newLabel 借用 XString* 新名。 @return 替换成功返回 true。 */
bool XCategoryAxis_replace(XCategoryAxis* self, const XString* oldLabel,
                           const XString* newLabel);
/** @brief 替换类别名（UTF-8 编码版本；对标 replace）。
 * @param self 目标轴指针。
 * @param oldLabel UTF-8 旧类别名（借用）；不能为 NULL。
 * @param newLabel UTF-8 新类别名（借用）；不能为 NULL。
 * @return 找到并替换返回 true；未找到返回 false 且轴不变。 */
bool XCategoryAxis_replace_utf8(XCategoryAxis* self, const char* oldLabel,
                                const char* newLabel);
/** @brief 替换类别名（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param oldLabel UTF-8 旧名（借用）。
 * @param newLabel UTF-8 新名（借用）。
 * @return 找到并替换返回 true。 */
bool XCategoryAxis_replace_2(XCategoryAxis* self, const char* oldLabel,
                             const char* newLabel);
/** @brief 清空全部类别（对标 clear）。 @param self 目标轴指针。 @return 无返回值。 */
void XCategoryAxis_clear(XCategoryAxis* self);

/** @brief 查询类别数（对标 count）。 @param self 目标轴指针。 @return 类别数。 */
int XCategoryAxis_count(const XCategoryAxis* self);
/** @brief 按下标取类别（借用；对标 at(int)）。
 * @param self 目标轴指针。
 * @param index 下标。
 * @return 借用 XString*；越界返回 NULL。归对象所有，随类别增删或析构失效，调用者不得释放。
 */
const XString* XCategoryAxis_at_const(const XCategoryAxis* self, int index);
/** @brief 按下标取类别（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @param index 下标。
 * @return 借用 XString*；越界返回 NULL，调用者不得释放。 */
const XString* XCategoryAxis_at(const XCategoryAxis* self, int index);
/** @brief 按下标取类别（UTF-8 借用视图）。
 * @param self 目标轴指针。
 * @param index 下标。
 * @return 借用 UTF-8 串；越界返回空串，调用者不得释放。 */
const char* XCategoryAxis_at_2(const XCategoryAxis* self, int index);

/* ==================== 字符串范围（对标 QBarCategoryAxis 的 min/max） ==================== */

/** @brief 设置最小类别（字符串形态；对标 setMin(QString)）。
 * @details 截断 [0, minIndex] 之外的类别被隐藏；找不到类别时为空操作。
 * @param self 目标轴指针。 @param minCategory 借用 XString*；NULL 复位。
 * @return 无返回值。 */
void XCategoryAxis_setMinCategory(XCategoryAxis* self,
                                  const XString* minCategory);
/** @brief 设置最小类别（UTF-8 编码版本；对标 setMin）。
 * @param self 目标轴指针。
 * @param minCategory UTF-8 类别名（借用）；NULL 复位为全部类别。
 * @return 无返回值；类别不存在时轴不变。 */
void XCategoryAxis_setMinCategory_utf8(XCategoryAxis* self, const char* minCategory);
/** @brief 设置最小类别（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param minCategory UTF-8 类别名（借用）。
 * @return 无返回值。 */
void XCategoryAxis_setMinCategory_2(XCategoryAxis* self, const char* minCategory);
/** @brief 查询最小类别（借用；对标 min）。
 * @param self 目标轴指针。
 * @return 借用 XString*；未设置返回首类别，无类别返回 NULL。调用者不得释放。
 */
const XString* XCategoryAxis_minCategory_const(const XCategoryAxis* self);
/** @brief 查询最小类别（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @return 借用 XString*；无类别返回 NULL，调用者不得释放。 */
const XString* XCategoryAxis_minCategory(const XCategoryAxis* self);
/** @brief 查询最小类别（UTF-8 借用视图）。
 * @param self 目标轴指针。
 * @return 借用 UTF-8 串；无类别返回空串，调用者不得释放。 */
const char* XCategoryAxis_minCategory_2(const XCategoryAxis* self);
/** @brief 设置最大类别（字符串形态；对标 setMax(QString)）。 @param self 目标轴指针。 @param maxCategory 借用 XString*；NULL 复位。 @return 无返回值。 */
void XCategoryAxis_setMaxCategory(XCategoryAxis* self,
                                  const XString* maxCategory);
/** @brief 设置最大类别（UTF-8 编码版本；对标 setMax）。
 * @param self 目标轴指针。
 * @param maxCategory UTF-8 类别名（借用）；NULL 复位为全部类别。
 * @return 无返回值；类别不存在时轴不变。 */
void XCategoryAxis_setMaxCategory_utf8(XCategoryAxis* self, const char* maxCategory);
/** @brief 设置最大类别（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param maxCategory UTF-8 类别名（借用）。
 * @return 无返回值。 */
void XCategoryAxis_setMaxCategory_2(XCategoryAxis* self, const char* maxCategory);
/** @brief 查询最大类别（借用；对标 max）。
 * @param self 目标轴指针。
 * @return 借用 XString*；未设置返回末类别，无类别返回 NULL。调用者不得释放。
 */
const XString* XCategoryAxis_maxCategory_const(const XCategoryAxis* self);
/** @brief 查询最大类别（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @return 借用 XString*；无类别返回 NULL，调用者不得释放。 */
const XString* XCategoryAxis_maxCategory(const XCategoryAxis* self);
/** @brief 查询最大类别（UTF-8 借用视图）。
 * @param self 目标轴指针。
 * @return 借用 UTF-8 串；无类别返回空串，调用者不得释放。 */
const char* XCategoryAxis_maxCategory_2(const XCategoryAxis* self);
/** @brief 设置字符串范围（对标 setRange(QString,QString)）。 @param self 目标轴指针。 @param minCategory 借用 XString* 最小类别。 @param maxCategory 借用 XString* 最大类别。 @return 无返回值。 */
void XCategoryAxis_setRangeCategory(XCategoryAxis* self,
                                    const XString* minCategory,
                                    const XString* maxCategory);
/** @brief 设置字符串范围（UTF-8 编码版本；对标 setRange(QString,QString)）。
 * @param self 目标轴指针。
 * @param minCategory UTF-8 最小类别（借用）。
 * @param maxCategory UTF-8 最大类别（借用）。
 * @return 无返回值；任一类别不存在时轴不变。 */
void XCategoryAxis_setRangeCategory_utf8(XCategoryAxis* self,
                                         const char* minCategory,
                                         const char* maxCategory);
/** @brief 设置字符串范围（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param minCategory UTF-8 最小类别（借用）。
 * @param maxCategory UTF-8 最大类别（借用）。
 * @return 无返回值。 */
void XCategoryAxis_setRangeCategory_2(XCategoryAxis* self,
                                      const char* minCategory,
                                      const char* maxCategory);

/* ==================== Qt 命名的 min/max/categories（对标 QBarCategoryAxis） ==================== */

/** @brief 设置最小类别（字符串形态；对标 setMin(QString)；等价 setMinCategory）。
 * @param self 目标轴指针。 @param minCategory 借用 XString*。 @return 无返回值。 */
/** @brief 设置最小类别（Qt 命名形态；等价 setMinCategory）。
 * @param self 目标轴指针。
 * @param minCategory 借用 XString*；NULL 复位。
 * @return 无返回值。 */
void XCategoryAxis_setMinCategoryQt(XCategoryAxis* self, const XString* minCategory);
/** @brief 设置最小类别（XString 主版本；对标 QBarCategoryAxis::setMin）。
 * @param self 目标轴指针。
 * @param minCategory 借用 XString*；NULL 复位为全部类别。
 * @return 无返回值；类别不存在时轴不变。
 */
void XCategoryAxis_setMin(XCategoryAxis* self, const XString* minCategory);
/** @brief 设置最小类别（UTF-8 编码版本）。 @param self 目标轴指针。 @param minCategory UTF-8 类别名（借用）。 @return 无返回值。 */
void XCategoryAxis_setMin_utf8(XCategoryAxis* self, const char* minCategory);
/** @brief 设置最大类别（XString 主版本；对标 QBarCategoryAxis::setMax）。
 * @param self 目标轴指针。
 * @param maxCategory 借用 XString*；NULL 复位为全部类别。
 * @return 无返回值；类别不存在时轴不变。
 */
void XCategoryAxis_setMax(XCategoryAxis* self, const XString* maxCategory);
/** @brief 设置最大类别（UTF-8 编码版本）。 @param self 目标轴指针。 @param maxCategory UTF-8 类别名（借用）。 @return 无返回值。 */
void XCategoryAxis_setMax_utf8(XCategoryAxis* self, const char* maxCategory);
/** @brief 设置类别字符串范围（XString 主版本；对标 QBarCategoryAxis::setRange）。
 * @param self 目标轴指针。
 * @param minCategory 借用 XString* 最小类别。
 * @param maxCategory 借用 XString* 最大类别。
 * @return 无返回值。
 */
void XCategoryAxis_setRange(XCategoryAxis* self, const XString* minCategory,
                            const XString* maxCategory);
/** @brief 设置类别字符串范围（UTF-8 编码版本）。 @param self 目标轴指针。 @param minCategory UTF-8 最小类别。 @param maxCategory UTF-8 最大类别。 @return 无返回值。 */
void XCategoryAxis_setRange_utf8(XCategoryAxis* self, const char* minCategory,
                                 const char* maxCategory);

/* ==================== 数值范围（渲染内部语义；Qt 无对应） ==================== */

/** @brief 查询轴范围下限（QBarCategoryAxis 语义：恒 0）。
 * @param self 目标轴指针。 @return 下限。 */
double XCategoryAxis_min(const XCategoryAxis* self);
/** @brief 查询轴范围上限（= 类别数）。
 * @param self 目标轴指针。 @return 上限。 */
double XCategoryAxis_max(const XCategoryAxis* self);
/** @brief 设置类别轴数值范围（仅允许 [0, count] 裁剪；内部渲染语义，Qt 无对应）。
 * @param self 目标轴指针。
 * @param min 下限（按 0 钳位）。
 * @param max 上限（按 count 钳位；须 >= min）。
 * @return 无返回值。
 */
void XCategoryAxis_setRangeIndex(XCategoryAxis* self, double min, double max);

/* ==================== 信号（对标 QBarCategoryAxis Q_SIGNALS） ==================== */

/** @brief categoriesChanged 信号（无载荷）。 */
void* XCategoryAxis_categoriesChanged_signal(XCategoryAxis* self);
/** @brief countChanged 信号（无载荷；Qt 无此信号的载荷）。 */
void* XCategoryAxis_countChanged_signal(XCategoryAxis* self, int count);
/** @brief minChanged 信号（载荷：最小类别名）。 */
void* XCategoryAxis_minChanged_signal(XCategoryAxis* self, const char* min);
/** @brief maxChanged 信号（载荷：最大类别名）。 */
void* XCategoryAxis_maxChanged_signal(XCategoryAxis* self, const char* max);
/** @brief rangeChanged 信号（载荷：最小/最大类别名）。 */
void* XCategoryAxis_rangeChanged_signal(XCategoryAxis* self,
                                        const char* min, const char* max);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCATEGORYAXIS_H */
