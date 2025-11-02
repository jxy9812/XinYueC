#include"CXinYueConfig.h"
/**
* @brief 条件编译控制
* @note 防止头文件重复包含，且仅在XList模块启用时编译
*/
#if !defined(XLISTBASE_H)&& XList_ON
#define XLISTBASE_H

/**
* @brief C++兼容声明
* @note 支持C++编译时的C语言链接规范，避免名称修饰问题
*/
#ifdef __cplusplus
extern "C" {
#endif

#include<stdbool.h>        /** @brief 布尔类型支持，提供bool、true、false定义 */
#include<stdio.h>          /** @brief 标准输入输出库，用于调试输出 */
#include"XContainerObject.h" /** @brief 容器基类，XListBase继承自该类 */
#include"XFunctionCallback.h" /** @brief 函数回调支持，提供容器操作所需的回调函数类型 */
#include"XListBase_iterator.h" /** @brief 链表迭代器定义，支持链表遍历操作 */


/**
* @brief XListBase虚函数表大小定义
* @note 基于容器基类虚函数表大小扩展，确定链表基类所需的虚函数表容量
*/
#define XLISTBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XListBase))      

/**
* @brief XListBase虚函数表枚举
* @details 定义链表基类操作对应的虚函数索引，扩展自容器基类的虚函数表
* @note 枚举值从容器基类虚函数表大小开始，避免索引冲突
*/
XCLASS_DEFINE_BEGING(XListBase)
XCLASS_DEFINE_ENUM(XListBase, Push_Front) = XCLASS_VTABLE_GET_SIZE(XContainerObject), /** @brief 头插元素（拷贝/移动）虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Push_Front_Node), /** @brief 头插节点虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Push_Back), /** @brief 尾插元素（拷贝/移动）虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Push_Back_Node), /** @brief 尾插节点虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Insert), /** @brief 指定位置插入元素虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Insert_Array), /** @brief 指定位置插入数组虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Pop_Front), /** @brief 头删元素虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Pop_Back), /** @brief 尾删元素虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Erase), /** @brief 删除指定节点虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Remove), /** @brief 删除指定值元素虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Front), /** @brief 获取头元素虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Back), /** @brief 获取尾元素虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Find), /** @brief 查找元素虚函数索引 */
XCLASS_DEFINE_ENUM(XListBase, Sort), /** @brief 排序虚函数索引 */
XCLASS_DEFINE_END(XListBase)

/**
* @brief 链表基类结构体定义
* @details 作为所有链表（如单链表、双向链表）的基类，继承自容器对象基类
* @note 封装通用链表操作，具体实现由派生类（如XListDLinked、XListSLinked）提供
*/
typedef struct XListBase
{
	XContainerObject m_class; /** @brief 继承自容器基类，包含通用容器属性（如大小、容量、数据操作方法等） */
	//XEquality m_equality;//相等比较函数（预留字段，暂未使用）
}XListBase;

/**
* @brief 节点类型声明
* @note 提前声明链表节点结构体，避免循环依赖，具体定义由派生类实现
*/
typedef struct XListBaseNode XListBaseNode;


/**
* @brief 类初始化接口
*/

/**
* @brief 初始化链表基类实例
* @param this_list 待初始化的链表指针
* @param typeSize 单个元素的类型大小（字节数，如sizeof(int)）
* @note 需先手动分配链表实例内存，再调用此函数初始化基类成员
*/
void XListBase_init(XListBase* this_list, size_t typeSize);


/**
* @brief 插入操作接口
*/

/**
* @brief 头插节点（直接插入已有节点，转移节点所有权）
* @param list 链表实例指针
* @param node 待插入的节点指针
* @return 插入成功返回true，失败返回false
* @note 用于高效插入已创建的节点，避免重复分配内存
*/
bool XListBase_push_front_node_base(XListBase* list, XListBaseNode* node);

/**
* @brief 头插元素（拷贝语义）
* @param this_list 链表实例指针
* @param pvData 待插入的元素数据指针
* @return 插入成功返回新节点指针，失败返回NULL
* @note 拷贝pvData指向的数据到新节点，再插入链表头部
*/
XListBaseNode* XListBase_push_front_base(XListBase* this_list, void* pvData);

/**
* @brief 头插元素（类型安全宏，拷贝语义）
* @param this_list 链表实例指针
* @param type 元素数据类型（如int、float）
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，再调用XListBase_push_front_base实现头插，简化类型转换
*/
#define XListBase_Push_Front_Base(this_list,type,value){type t=value;XListBase_push_front_base(this_list,&t);}

/**
* @brief 头插元素（移动语义）
* @param this_list 链表实例指针
* @param pvData 待移动的元素数据指针（所有权转移）
* @return 插入成功返回新节点指针，失败返回NULL
* @note 移动pvData指向的数据到新节点，减少拷贝开销，适用于临时对象
*/
XListBaseNode* XListBase_push_front_move_base(XListBase* this_list, void* pvData);

/**
* @brief 头插元素（类型安全宏，移动语义）
* @param this_list 链表实例指针
* @param type 元素数据类型（如int、float）
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，再调用XListBase_push_front_move_base实现头插，结合移动语义提升性能
*/
#define XListBase_Push_Front_Move_Base(this_list,type,value){type t=value;XListBase_push_front_move_base(this_list,&t);}

/**
* @brief 尾插节点（直接插入已有节点，转移节点所有权）
* @param list 链表实例指针
* @param node 待插入的节点指针
* @return 插入成功返回true，失败返回false
* @note 用于高效插入已创建的节点，避免重复分配内存
*/
bool XListBase_push_back_node_base(XListBase* list, XListBaseNode* node);

/**
* @brief 尾插元素（拷贝语义）
* @param this_list 链表实例指针
* @param pvData 待插入的元素数据指针
* @return 插入成功返回新节点指针，失败返回NULL
* @note 拷贝pvData指向的数据到新节点，再插入链表尾部
*/
XListBaseNode* XListBase_push_back_base(XListBase* this_list, void* pvData);

/**
* @brief 尾插元素（类型安全宏，拷贝语义）
* @param this_list 链表实例指针
* @param type 元素数据类型（如int、float）
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，再调用XListBase_push_back_base实现尾插，简化类型转换
*/
#define XListBase_Push_Back_Base(this_list,type,value){type t=value;XListBase_push_back_base(this_list,&t);}

/**
* @brief 尾插元素（移动语义）
* @param this_list 链表实例指针
* @param pvData 待移动的元素数据指针（所有权转移）
* @return 插入成功返回新节点指针，失败返回NULL
* @note 移动pvData指向的数据到新节点，减少拷贝开销，适用于临时对象
*/
XListBaseNode* XListBase_push_back_move_base(XListBase* this_list, void* pvData);

/**
* @brief 尾插元素（类型安全宏，移动语义）
* @param this_list 链表实例指针
* @param type 元素数据类型（如int、float）
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，再调用XListBase_push_back_move_base实现尾插，结合移动语义提升性能
*/
#define XListBase_Push_Back_Move_Base(this_list,type,value){type t=value;XListBase_push_back_move_base(this_list,&t);}

/**
* @brief 指定节点前插入元素（拷贝语义）
* @param this_list 链表实例指针
* @param curNode 目标节点（新元素插入到此节点之前）
* @param pvData 待插入的元素数据指针
* @return 插入成功返回true，失败返回false
* @note 拷贝数据到新节点，插入到curNode之前
*/
bool XListBase_insert_base(XListBase* this_list, XListBaseNode* curNode, void* pvData);

/**
* @brief 指定节点前插入元素（移动语义）
* @param this_list 链表实例指针
* @param curNode 目标节点（新元素插入到此节点之前）
* @param pvData 待移动的元素数据指针（所有权转移）
* @return 插入成功返回true，失败返回false
* @note 移动数据到新节点，插入到curNode之前，减少拷贝开销
*/
bool XListBase_insert_move_base(XListBase* this_list, XListBaseNode* curNode, void* pvData);

/**
* @brief 指定节点前插入数组（拷贝语义）
* @param this_list 链表实例指针
* @param curNode 目标节点（数组插入到此节点之前）
* @param array 待插入的数组指针
* @param count 数组元素数量
* @return 实际插入的元素数量（成功时等于count，失败时可能小于count）
* @note 拷贝数组中所有元素到新节点，依次插入到curNode之前
*/
size_t  XListBase_insert_array_base(XListBase* this_list, XListBaseNode* curNode, void* array, size_t count);

/**
* @brief 指定节点前插入数组（移动语义）
* @param this_list 链表实例指针
* @param curNode 目标节点（数组插入到此节点之前）
* @param array 待移动的数组指针（所有权转移）
* @param count 数组元素数量
* @return 实际插入的元素数量（成功时等于count，失败时可能小于count）
* @note 移动数组中所有元素到新节点，依次插入到curNode之前，减少拷贝开销
*/
size_t  XListBase_insert_array_move_base(XListBase* this_list, XListBaseNode* curNode, void* array, size_t count);


/**
* @brief 删除操作接口
*/

/**
* @brief 删除头元素
* @param this_list 链表实例指针
* @return 删除成功返回true，失败（如链表为空）返回false
* @note 移除并释放链表头部的第一个元素
*/
bool  XListBase_pop_front_base(XListBase* this_list);

/**
* @brief 删除尾元素
* @param this_list 链表实例指针
* @return 删除成功返回true，失败（如链表为空）返回false
* @note 移除并释放链表尾部的最后一个元素
*/
bool  XListBase_pop_back_base(XListBase* this_list);

/**
* @brief 删除指定迭代器指向的节点
* @param this_list 链表实例指针
* @param it 指向待删除节点的迭代器
* @param next 输出参数，指向删除节点的下一个节点的迭代器（可为NULL）
* @note 释放迭代器指向的节点，通过next获取下一个有效节点，避免迭代器失效
*/
void  XListBase_erase_base(XListBase* this_list, const XListBase_iterator* it, XListBase_iterator* next);

/**
* @brief 删除首个匹配的元素（拷贝语义）
* @param this_list 链表实例指针
* @param pvData 待删除的元素数据指针（用于匹配）
* @return 删除成功返回true，未找到元素返回false
* @note 查找并删除与pvData指向数据相等的首个元素
*/
bool  XListBase_remove_base(XListBase* this_list, void* pvData);

/**
* @brief 删除首个匹配的元素（类型安全宏，拷贝语义）
* @param this_list 链表实例指针
* @param type 元素数据类型（如int、float）
* @param value 待删除的元素值
* @note 自动创建临时变量存储value，再调用XListBase_remove_base实现删除，简化类型转换
*/
#define XListBase_Remove_Base(this_list,type,value){type t=value;XListBase_remove_base(this_list,&t);}


/**
* @brief 遍历与查询操作接口
*/

/**
* @brief 获取头元素指针
* @param this_list 链表实例指针
* @return 头元素的指针（链表为空返回NULL）
* @note 仅返回指针，不检查有效性，需确保链表非空
*/
void* XListBase_front_base(XListBase* this_list);

/**
* @brief 获取头元素（类型安全宏）
* @param list 链表实例指针
* @param Type 元素数据类型（如int、float）
* @return 头元素的值（通过指针解引用获取）
* @note 自动转换指针类型并解引用，简化头元素访问
*/
#define XListBase_Front_Base(list,Type) (*(Type*)XListBase_front_base(list))

/**
* @brief 获取尾元素指针
* @param this_list 链表实例指针
* @return 尾元素的指针（链表为空返回NULL）
* @note 仅返回指针，不检查有效性，需确保链表非空
*/
void* XListBase_back_base(XListBase* this_list);

/**
* @brief 获取尾元素（类型安全宏）
* @param list 链表实例指针
* @param Type 元素数据类型（如int、float）
* @return 尾元素的值（通过指针解引用获取）
* @note 自动转换指针类型并解引用，简化尾元素访问
*/
#define XListBase_Back_Base(list,Type) (*(Type*)XListBase_back_base(list))

/**
* @brief 查找元素并获取迭代器
* @param this_list 链表实例指针
* @param findVal 待查找的元素数据指针
* @param it 输出参数，用于存储找到的元素对应的迭代器（未找到时迭代器无效）
* @return 找到元素返回true，未找到返回false
* @note 通过元素值匹配查找，需确保元素类型支持相等比较
*/
bool XListBase_find_base(const  XListBase* this_list, const void* findVal, XListBase_iterator* it);

/**
* @brief 判断链表是否包含指定元素
* @param this_list 链表实例指针
* @param value 待判断的元素数据指针
* @return 包含元素返回true，否则返回false
* @note 内部调用XListBase_find_base实现，仅判断存在性，不返回位置
*/
bool XListBase_contains(const XListBase* this_list, const void* value);

/**
* @brief 对链表元素进行排序
* @param this_list 链表实例指针
* @param order 排序顺序（升序或降序，XSortOrder枚举）
* @note 排序算法由派生类实现，基于元素类型的比较规则
*/
void XListBase_sort_base(XListBase* this_list, XSortOrder order);


/**
* @brief 通用容器操作接口（继承自XContainerObject）
*/

/**
* @brief 拷贝链表（深拷贝）
* @param this_stackOne 目标链表指针
* @param this_stackTwo 源链表指针
* @note 宏定义，等价于XContainerObject_copy_base，拷贝所有元素到目标链表
*/
#define XListBase_copy_base				XContainerObject_copy_base	

/**
* @brief 移动链表资源（所有权转移）
* @param this_stackOne 目标链表指针
* @param this_stackTwo 源链表指针
* @note 宏定义，等价于XContainerObject_move_base，转移资源所有权，避免深拷贝
*/
#define XListBase_move_base				XContainerObject_move_base	

/**
* @brief 销毁链表内部资源（保留实例本身）
* @param this_list 链表实例指针
* @note 宏定义，等价于XContainerObject_deinit_base，释放元素和节点内存，链表实例可复用
*/
#define XListBase_deinit_base			XContainerObject_deinit_base	

/**
* @brief 删除链表（释放所有资源）
* @param this_list 链表实例指针
* @note 宏定义，等价于XContainerObject_delete_base，释放内部资源及链表实例本身
*/
#define XListBase_delete_base			XContainerObject_delete_base	

/**
* @brief 清空链表（保留容量）
* @param this_list 链表实例指针
* @note 宏定义，等价于XContainerObject_clear_base，删除所有元素，元素数量置0
*/
#define XListBase_clear_base			XContainerObject_clear_base	

/**
* @brief 判断链表是否为空
* @param this_list 链表实例指针
* @return 为空返回true，否则返回false
* @note 宏定义，等价于XContainerObject_isEmpty_base，通过元素数量判断
*/
#define XListBase_isEmpty_base			XContainerObject_isEmpty_base	

/**
* @brief 获取链表元素数量
* @param this_list 链表实例指针
* @return 元素个数（size_t类型）
* @note 宏定义，等价于XContainerObject_size_base
*/
#define XListBase_size_base				XContainerObject_size_base	

/**
* @brief 获取链表容量（最大可容纳元素数）
* @param this_list 链表实例指针
* @return 容量值（size_t类型）
* @note 宏定义，等价于XContainerObject_capacity_base
*/
#define XListBase_capacity_base			XContainerObject_capacity_base

/**
* @brief 交换两个链表的内容
* @param this_listOne 第一个链表指针
* @param this_listTwo 第二个链表指针
* @note 宏定义，等价于XContainerObject_swap_base，交换元素、大小、容量等所有属性
*/
#define XListBase_swap_base				XContainerObject_swap_base	

/**
* @brief 获取链表元素类型大小
* @param this_list 链表实例指针
* @return 元素类型大小（字节数，size_t类型）
* @note 宏定义，等价于XContainerObject_typeSize_base
*/
#define XListBase_typeSize_base			XContainerObject_typeSize_base

/**
* @brief C++兼容声明结束
*/
#ifdef __cplusplus
}
#endif

#endif // XLISTBASE_H