#include"CXinYueConfig.h"
#if !defined(XSTRINGLIST_H)&& XStringList_ON
#define XSTRINGLIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include<stdio.h>
#include<stdbool.h>
#include"XVector.h"
#include"XStringList_Iterator.h"
#include"XStringList_reverse_iterator.h"

#define XSTRINGVECTOR_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XStringList))

XCLASS_DEFINE_BEGING(XStringList)
XCLASS_DEFINE_EXTEND_END(XStringList, XVector)

typedef struct XStringList
{
    XVector m_vector;
}XStringList;

/**
 * @brief 初始化XStringList的虚函数表
 * @return 指向初始化完成的XStringList虚函数表的指针
 */
XVtable* XStringList_class_init();

/**
 * @brief 创建一个空的XStringList对象
 * @return 成功返回新创建的XStringList指针，失败返回NULL
 */
XStringList* XStringList_create();
XStringList* XStringList_create_copy(const XStringList* other);
XStringList* XStringList_create_move(XStringList* other);

/**
 * @brief 初始化栈上分配的XStringList对象
 * @param strList 指向待初始化的XStringList对象的指针
 */
void XStringList_init(XStringList* strList);

/**
 * @brief 向前端插入一个XString对象（基础版本）
 * @param strList 指向XStringList对象的指针
 * @param value 待插入的XString对象指针
 */
#define XStringList_push_front_base								XVector_push_front_base

/**
* @brief 向前端插入一个XString对象（移动语义）
* @param strList 指向XStringList对象的指针
* @param value 待移动插入的XString对象指针
*/
#define XStringList_push_front_move_base						XVector_push_front_move_base

/**
* @brief 向前端插入一个UTF-8字符串
* @param strList 指向XStringList对象的指针
* @param utf8_str 待插入的UTF-8字符串
*/
void XStringList_push_front_utf8(XStringList* strList, const char* utf8_str);

/**
 * @brief 向后端插入一个XString对象（基础版本）
 * @param strList 指向XStringList对象的指针
 * @param value 待插入的XString对象指针
 */
#define XStringList_push_back_base								XVector_push_back_base

/**
* @brief 向后端插入一个XString对象（移动语义）
* @param strList 指向XStringList对象的指针
* @param value 待移动插入的XString对象指针
*/
#define XStringList_push_back_move_base							XVector_push_back_move_base

/**
* @brief 向后端插入一个UTF-8字符串
* @param strList 指向XStringList对象的指针
* @param utf8_str 待插入的UTF-8字符串
*/
void XStringList_push_back_utf8(XStringList* strList, const char* utf8_str);

/**
* @brief 在指定索引位置插入一个XString对象（基础版本）
* @param strList 指向XStringList对象的指针
* @param index 插入位置的索引（从0开始）
* @param value 待插入的XString对象指针
* @return 成功返回0，失败返回-1
*/
#define XStringList_insert_base									XVector_insert

/**
* @brief 在指定索引位置插入一个XString对象（移动语义）
* @param strList 指向XStringList对象的指针
* @param index 插入位置的索引（从0开始）
* @param value 待移动插入的XString对象指针
* @return 成功返回0，失败返回-1
*/
#define XStringList_insert_move_base							XVector_insert_move

/**
* @brief 在指定索引位置插入一个UTF-8字符串
* @param strList 指向XStringList对象的指针
* @param index 插入位置的索引（从0开始）
* @param utf8_str 待插入的UTF-8字符串
* @return 成功返回0，失败返回-1
*/
void XStringList_insert_utf8(XStringList* strList, int64_t index, const char* utf8_str);

/**
* @brief 获取指定索引位置的XString对象
* @param strList 指向XStringList对象的指针
* @param index 要访问的元素索引（从0开始）
* @return 成功返回指向XString对象的指针，失败返回NULL
*/
#define XStringList_at_base										XVector_at_base

/**
* @brief 获取列表中第一个XString对象
* @param strList 指向XStringList对象的指针
* @return 成功返回指向第一个XString对象的指针，列表为空时返回NULL
*/
#define XStringList_front_base									XVector_front_base

/**
* @brief 获取列表中最后一个XString对象
* @param strList 指向XStringList对象的指针
* @return 成功返回指向最后一个XString对象的指针，列表为空时返回NULL
*/
#define XStringList_back_base									XVector_back_base

/**
* @brief 用指定分隔符拼接列表中的所有字符串
* @param strList 指向XStringList对象的指针
* @param separator 作为分隔符的XString对象指针
* @return 成功返回拼接后的新XString对象，失败返回NULL
*/
XString* XStringList_join(const XStringList* strList, const XString* separator);

/**
* @brief 用指定UTF-8分隔符拼接列表中的所有字符串
* @param strList 指向XStringList对象的指针
* @param separator 作为分隔符的UTF-8字符串
* @return 成功返回拼接后的新XString对象，失败返回NULL
*/
XString* XStringList_join_utf8(const XStringList* strList, const char* separator);

/**
* @brief 移除指定索引位置的XString对象
* @param strList 指向XStringList对象的指针
* @param index 要移除的元素索引（从0开始）
* @return 成功返回0，失败返回-1
*/
#define	XStringList_remove_base									XVector_remove_base

/**
* @brief 移除指定范围内的XString对象
* @param strList 指向XStringList对象的指针
* @param first 范围起始索引（包含）
* @param last 范围结束索引（不包含）
* @return 成功返回0，失败返回-1
*/
#define XStringList_erase_base                                  XVector_erase_base

/**
* @brief 移除列表中最后一个XString对象
* @param strList 指向XStringList对象的指针
* @return 成功返回0，列表为空时返回-1
*/
#define XStringList_pop_back_base								XVector_pop_back_base

/**
* @brief 移除列表中第一个XString对象
* @param strList 指向XStringList对象的指针
* @return 成功返回0，列表为空时返回-1
*/
#define	XStringList_pop_front_base								XVector_pop_front_base

/**
* @brief 调整列表的大小（元素数量）
* @param strList 指向XStringList对象的指针
* @param new_size 新的列表大小
*/
#define	XStringList_resize_base									XVector_resize_base

/**
* @brief 拷贝一个XStringList对象
* @param strList 指向要拷贝的XStringList对象的指针
* @return 成功返回新拷贝的XStringList指针，失败返回NULL
*/
#define XStringList_copy_base									XVector_copy_base	

/**
* @brief 移动一个XStringList对象的内容（转移所有权）
* @param dest 指向目标XStringList对象的指针
* @param src 指向源XStringList对象的指针
*/
#define XStringList_move_base									XVector_move_base	

/**
* @brief 反初始化XStringList对象（释放资源但保留对象本身）
* @param strList 指向XStringList对象的指针
*/
#define XStringList_deinit_base								    XVector_deinit_base	

/**
* @brief 销毁XStringList对象（释放资源和对象内存）
* @param strList 指向要销毁的XStringList对象的指针
*/
#define XStringList_delete_base								    XVector_delete_base	

/**
* @brief 清空列表中的所有元素
* @param strList 指向XStringList对象的指针
*/
#define XStringList_clear_base									XVector_clear_base	

/**
* @brief 判断列表是否为空
* @param strList 指向XStringList对象的指针
* @return 列表为空返回true，否则返回false
*/
#define XStringList_isEmpty_base								XVector_isEmpty_base	

/**
* @brief 获取列表中的元素数量
* @param strList 指向XStringList对象的指针
* @return 返回列表中当前的元素数量
*/
#define XStringList_size_base									XVector_size_base	

/**
* @brief 获取列表当前的容量（可容纳的元素数量）
* @param strList 指向XStringList对象的指针
* @return 返回列表当前的容量
*/
#define XStringList_capacity_base								XVector_capacity_base

/**
* @brief 交换两个列表的内容
* @param a 指向第一个XStringList对象的指针
* @param b 指向第二个XStringList对象的指针
*/
#define XStringList_swap_base									XVector_swap_base	

/**
* @brief 获取列表中元素类型（XString）的大小
* @param strList 指向XStringList对象的指针
* @return 返回XString类型的大小（以字节为单位）
*/
#define XStringList_typeSize_base								XVector_typeSize_base

#ifdef __cplusplus
}
#endif

#endif // !XSTRINGLIST_H
