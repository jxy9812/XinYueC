#include"CXinYueConfig.h"
#if !defined(XVECTOR_H)&& XVector_ON
#define XVECTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include<stdarg.h>
#include"XContainer.h"
#include"XVector_iterator.h"
#include"XVector_reverse_iterator.h"
#define XVECTOR_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XVector))       //XVector容器虚函数表大小
//XVector虚函数表枚举
XCLASS_DEFINE_BEGING(XVector)
XCLASS_DEFINE_ENUM(XVector, Resize) = XCLASS_VTABLE_GET_SIZE(XContainer),
XCLASS_DEFINE_ENUM(XVector, Push_Front),
XCLASS_DEFINE_ENUM(XVector, Push_Back),
XCLASS_DEFINE_ENUM(XVector, Insert_Array),
XCLASS_DEFINE_ENUM(XVector, Pop_Front),
XCLASS_DEFINE_ENUM(XVector, Pop_Back),
XCLASS_DEFINE_ENUM(XVector, Erase),
XCLASS_DEFINE_ENUM(XVector, Remove),
XCLASS_DEFINE_ENUM(XVector, Rcopy),
XCLASS_DEFINE_ENUM(XVector, At),
XCLASS_DEFINE_ENUM(XVector, Front),
XCLASS_DEFINE_ENUM(XVector, Back),
XCLASS_DEFINE_ENUM(XVector, Find),
XCLASS_DEFINE_ENUM(XVector, Sort),
XCLASS_DEFINE_END(XVector)

typedef struct XVector
{
	XContainer m_class;
}XVector;

/**
 * @brief 初始化XVector的虚函数表
 * @return 初始化完成的XVector虚函数表指针，失败返回NULL
 */
XVtable* XVector_class_init();

/**
 * @brief 创建一个动态数组（XVector）
 * @param typeSize 数组中元素的类型大小（字节数）
 * @return 新创建的XVector指针，失败返回NULL
 * @note 需确保typeSize大于0，否则创建失败
 */
XVector* XVector_create_ex(size_t typeSize, bool useCow);
#define XVector_create(typeSize)   XVector_create_ex(typeSize,true)
/**
 * @brief 通过拷贝另一个XVector创建新的XVector
 * @param other 被拷贝的源XVector
 * @return 新创建的XVector指针，失败返回NULL
 * @note 若other为NULL，返回NULL；新向量与源向量元素完全一致
 */
XVector* XVector_create_copy(const XVector* other);

/**
 * @brief 通过移动另一个XVector的资源创建新的XVector
 * @param other 被移动的源XVector
 * @return 新创建的XVector指针，失败返回NULL
 * @note 移动后源XVector的资源将被转移，源向量变为空状态
 */
XVector* XVector_create_move(XVector* other);

/**
 * @brief 宏定义：根据元素类型创建XVector
 * @param Type 元素类型
 * @return 新创建的XVector指针，失败返回NULL
 * @note 内部调用XVector_create，自动计算Type的大小
 */
#define XVector_Create(Type) XVector_create(sizeof(Type))

/**
 * @brief 初始化指定的XVector
 * @param this_vector 待初始化的XVector指针
 * @param typeSize 数组中元素的类型大小（字节数）
 * @note 需确保this_vector不为NULL且typeSize大于0，否则初始化无效
 */
void XVector_init(XVector* this_vector, size_t typeSize, bool useCow);

/**
 * @brief 调整XVector的大小
 * @param this_vector 目标XVector
 * @param size 调整后的大小
 * @return 调整成功返回true，失败返回false
 * @note 若新大小超过当前大小，新增元素将被初始化为0；若小于当前大小，超出部分元素将被删除
 */
bool XVector_resize_base(XVector* this_vector, size_t size);

/**
 * @brief 向XVector头部添加一个元素（拷贝语义）
 * @param this_vector 目标XVector
 * @param pvValue 待添加元素的指针
 * @return 添加成功返回true，失败返回false
 * @note 若this_vector或pvValue为NULL，返回false；内部通过拷贝方式添加元素
 */
bool XVector_push_front_1_base(XVector* this_vector, void* pvValue);
bool XVector_push_front_2(XVector* this_vector, const void* begin, size_t n);
bool XVector_push_front_3(XVector* this_vector,const XVector* pvValue);
/**
 * @brief 宏定义：向XVector头部添加指定类型的元素（拷贝语义）
 * @param this_vector 目标XVector
 * @param type 元素类型
 * @param value 待添加的元素值
 * @note 内部通过创建临时变量拷贝值后调用XVector_push_front_base
 */
#define XVector_Push_Front_Base(this_vector,type,value){type t=value;XVector_push_front_1_base(this_vector,&t);}

 /**
  * @brief 向XVector头部添加一个元素（移动语义）
  * @param this_vector 目标XVector
  * @param pvValue 待添加元素的指针
  * @return 添加成功返回true，失败返回false
  * @note 若this_vector或pvValue为NULL，返回false；内部通过移动方式添加元素，源数据可能失效
  */
bool XVector_push_front_move_1_base(XVector* this_vector, void* pvValue);
bool XVector_push_front_move_2(XVector* this_vector,void* begin, size_t n);
bool XVector_push_front_move_3(XVector* this_vector,XVector* pvValue);
/**
 * @brief 向XVector尾部添加一个元素（拷贝语义）
 * @param this_vector 目标XVector
 * @param pvValue 待添加元素的指针
 * @return 添加成功返回true，失败返回false
 * @note 若this_vector或pvValue为NULL，返回false；内部通过拷贝方式添加元素
 */
bool XVector_push_back_1_base(XVector* this_vector, void* pvValue);
bool XVector_push_back_2(XVector* this_vector, const void* begin, size_t n);
bool XVector_push_back_3(XVector* this_vector, const XVector* pvValue);
/**
 * @brief 宏定义：向XVector尾部添加指定类型的元素（拷贝语义）
 * @param this_vector 目标XVector
 * @param type 元素类型
 * @param value 待添加的元素值
 * @note 内部通过创建临时变量拷贝值后调用XVector_push_back_base
 */
#define XVector_Push_Back_Base(this_vector,type,value){type t=value;XVector_push_back_1_base(this_vector,&t);}

 /**
  * @brief 向XVector尾部添加一个元素（移动语义）
  * @param this_vector 目标XVector
  * @param pvValue 待添加元素的指针
  * @return 添加成功返回true，失败返回false
  * @note 若this_vector或pvValue为NULL，返回false；内部通过移动方式添加元素，源数据可能失效
  */
bool XVector_push_back_move_1_base(XVector* this_vector, void* pvValue);
bool XVector_push_back_move_2(XVector* this_vector, void* begin, size_t n);
bool XVector_push_back_move_3(XVector* this_vector,XVector* pvValue);
/**
 * @brief 向XVector指定索引位置插入一个数组（拷贝语义）
 * @param this_vector 目标XVector
 * @param index 插入位置索引（0-based）
 * @param begin 数组起始地址
 * @param n 数组元素数量
 * @return 插入成功返回true，失败返回false
 * @note 若index超出范围，可能插入到头部或尾部；n为0时插入无效
 */
bool XVector_insert_1_base(XVector* this_vector, int64_t index, const void* begin, size_t n);
/**
 * @brief 向XVector指定索引位置插入一个元素（拷贝语义）
 * @param this_vector 目标XVector
 * @param index 插入位置索引（0-based）
 * @param pvValue 待插入元素的指针
 * @return 插入成功返回true，失败返回false
 * @note 若index超出范围（<0或>=大小），可能插入到头部或尾部；内部通过拷贝方式插入
 */
bool XVector_insert_2(XVector* this_vector, int64_t index, const void* pvValue);
bool XVector_insert_3(XVector* this_vector, int64_t index, const XVector* pvValue);

/**
 * @brief 宏定义：向XVector指定索引位置插入指定类型的元素（拷贝语义）
 * @param this_vector 目标XVector
 * @param index 插入位置索引（0-based）
 * @param type 元素类型
 * @param value 待插入的元素值
 * @note 内部通过创建临时变量拷贝值后调用XVector_insert
 */
#define XVector_Insert(this_vector,index,type,value){type t=value;XVector_insert_2(this_vector,index,&t);}

 /**
  * @brief 向XVector指定索引位置插入一个数组（移动语义）
  * @param this_vector 目标XVector
  * @param index 插入位置索引（0-based）
  * @param begin 数组起始地址
  * @param n 数组元素数量
  * @return 插入成功返回true，失败返回false
  * @note 若index超出范围，可能插入到头部或尾部；n为0时插入无效；源数组数据可能失效
  */
bool XVector_insert_move_1_base(XVector* this_vector, int64_t index, const void* begin, size_t n);
 /**
  * @brief 向XVector指定索引位置插入一个元素（移动语义）
  * @param this_vector 目标XVector
  * @param index 插入位置索引（0-based）
  * @param pvValue 待插入元素的指针
  * @return 插入成功返回true，失败返回false
  * @note 若index超出范围（<0或>=大小），可能插入到头部或尾部；内部通过移动方式插入，源数据可能失效
  */
bool XVector_insert_move_2(XVector* this_vector, int64_t index, const void* pvValue);
bool XVector_insert_move_3(XVector* this_vector, int64_t index, XVector* pvValue);

/**
 * @brief 向XVector尾部追加一个数组（拷贝语义）
 * @param this_vector 目标XVector
 * @param begin 数组起始地址
 * @param n 数组元素数量
 * @return 追加成功返回true，失败返回false
 * @note n为0时追加无效；内部通过拷贝方式添加元素
 */
#define XVector_append_1   XVector_push_back_1_base
#define XVector_append_2   XVector_push_back_2
#define XVector_append_3   XVector_push_back_3

/**
 * @brief 向XVector尾部追加一个数组（移动语义）
 * @param this_vector 目标XVector
 * @param begin 数组起始地址
 * @param n 数组元素数量
 * @return 追加成功返回true，失败返回false
 * @note n为0时追加无效；内部通过移动方式添加元素，源数组数据可能失效
 */
#define XVector_append_move_1   XVector_push_back_move_1_base
#define XVector_append_move_2   XVector_push_back_move_2

/**
 * @brief 删除XVector的第一个元素
 * @param this_vector 目标XVector
 * @note 若XVector为空，该操作无效
 */
void XVector_pop_front_base(XVector* this_vector);

/**
 * @brief 删除XVector的最后一个元素
 * @param this_vector 目标XVector
 * @note 若XVector为空，该操作无效
 */
void XVector_pop_back_base(XVector* this_vector);

/**
 * @brief 删除迭代器指向的元素，并获取下一个迭代器
 * @param this_vector 目标XVector
 * @param it 指向待删除元素的迭代器
 * @param next 输出参数，存储删除后的下一个迭代器
 * @note 若it为无效迭代器，操作无效；next可为NULL，此时不返回下一个迭代器
 */
void XVector_erase_base(XVector* this_vector, const XVector_iterator* it, XVector_iterator* next);

/**
 * @brief 删除XVector中指定范围的元素
 * @param this_vector 目标XVector
 * @param index 起始删除位置索引（0-based）
 * @param n 要删除的元素数量（n<0表示删除从index到末尾的所有元素）
 * @note 若index超出范围，操作无效；若n为0，不删除任何元素
 */
void XVector_remove_base(XVector* this_vector, int64_t index, int64_t n);

/**
 * @brief 将另一个XVector的元素逆序拷贝到当前XVector
 * @param this_One 目标XVector（接收逆序数据）
 * @param this_Two 源XVector（提供数据）
 * @note 拷贝后this_One的元素为this_Two元素的逆序；若任一向量为NULL，操作无效
 */
void XVector_rcopy_base(XVector* this_One, const XVector* this_Two);

/**
 * @brief 获取XVector中指定索引位置元素的指针
 * @param this_vector 目标XVector
 * @param index 元素索引（0-based）
 * @return 指向元素的指针，失败返回NULL
 * @note 若index超出范围（<0或>=大小），返回NULL
 */
void* XVector_at_base(const XVector* this_vector, int64_t index);

/**
 * @brief 宏定义：获取XVector中指定索引位置的元素值
 * @param vector 目标XVector
 * @param index 元素索引（0-based）
 * @param type 元素类型
 * @return 指定索引位置的元素值
 * @note 内部通过XVector_at_base获取指针后解引用，需确保索引有效
 */
#define XVector_At_Base(vector,index,type) (*((type*)XVector_at_base(vector,index)))

 /**
  * @brief 获取XVector中第一个元素的指针
  * @param this_vector 目标XVector
  * @return 指向第一个元素的指针，若向量为空返回NULL
  */
void* XVector_front_base(const  XVector* this_vector);

/**
 * @brief 宏定义：获取XVector中第一个元素的值
 * @param vector 目标XVector
 * @param type 元素类型
 * @return 第一个元素的值
 * @note 需确保向量非空，否则可能导致未定义行为
 */
#define XVector_Front_Base(vector,type) (*((type*)XVector_front_base(vector)))

 /**
  * @brief 获取XVector中最后一个元素的指针
  * @param this_vector 目标XVector
  * @return 指向最后一个元素的指针，若向量为空返回NULL
  */
void* XVector_back_base(const  XVector* this_vector);

/**
 * @brief 宏定义：获取XVector中最后一个元素的值
 * @param vector 目标XVector
 * @param type 元素类型
 * @return 最后一个元素的值
 * @note 需确保向量非空，否则可能导致未定义行为
 */
#define XVector_Back_Base(vector,type) (*((type*)XVector_back_base(vector)))

 /**
  * @brief 在XVector中查找指定元素，获取其迭代器
  * @param this_vector 目标XVector
  * @param findVal 待查找元素的指针
  * @param it 输出参数，存储找到的元素的迭代器
  * @return 找到元素返回true，否则返回false
  * @note 若it为NULL，仅返回是否找到；查找使用元素的内存比较或自定义比较函数
  */
bool XVector_find_base(const XVector* this_vector, const void* findVal, XVector_iterator* it);

/**
 * @brief 判断XVector是否包含指定元素
 * @param this_vector 目标XVector
 * @param value 待判断的元素指针
 * @return 包含返回true，否则返回false
 * @note 内部调用XVector_find_base实现，仅判断存在性
 */
bool XVector_contains(const XVector* this_vector, const void* value);

/**
 * @brief 查找元素在XVector中的索引位置
 * @param this_vector 目标向量
 * @param value 要查找的元素值
 * @param from 起始查找位置（默认为0）
 * @return 找到返回索引，未找到返回-1
 */
int64_t XVector_indexOf(const XVector* this_vector, const void* value, int64_t from);

/**
 * @brief 从指定位置开始反向查找元素在XVector中最后出现的索引
 * @param this_vector 目标向量
 * @param value 要查找的元素值
 * @param from 起始查找位置（默认为-1，表示从最后一个元素开始）
 * @return 找到返回索引，未找到返回-1
 */
int64_t XVector_lastIndexOf(const XVector* this_vector, const void* value, int64_t from);

/**
 * @brief 获取XVector中最后n个元素组成的新向量
 * @param this_vector 源向量
 * @param n 要获取的元素数量
 * @return 包含最后n个元素的新XVector，失败返回NULL
 * @note 若n大于向量大小，则返回整个向量的拷贝；若n<=0，返回空向量
 */
XVector* XVector_last(const XVector* this_vector, int64_t n);

/**
 * @brief 获取XVector中从指定位置开始的子向量
 * @param this_vector 源向量
 * @param pos 起始位置索引（从0开始）
 * @param length 要获取的元素数量（默认为-1，表示获取从pos到末尾的所有元素）
 * @return 包含子向量的新XVector，失败返回NULL
 * @note 若pos超出范围或length为0，返回空向量；若length超出剩余元素数量，返回从pos到末尾的所有元素
 */
XVector* XVector_mid(const XVector* this_vector, int64_t pos, int64_t length);

/**
 * @brief 获取XVector中前n个元素组成的新向量
 * @param this_vector 源向量
 * @param n 要获取的元素数量
 * @return 包含前n个元素的新XVector，失败返回NULL
 * @note 若n <= 0，返回空向量；若n大于向量大小，返回整个向量的拷贝
 */
XVector* XVector_first(const XVector* this_vector, int64_t n);

/**
 * @brief 对XVector中的元素进行排序
 * @param this_vector 目标XVector
 * @param order 排序顺序（升序或降序）
 * @note 排序基于元素的内存比较或自定义排序函数；空向量排序无效
 */
void  XVector_sort_base(XVector* this_vector, XSortOrder order);

/**
 * @brief 替换XVector中指定索引位置的元素（拷贝语义）
 * @param this_vector 目标XVector
 * @param index 要替换的元素索引（0-based）
 * @param pvValue 新元素的指针
 * @return 替换成功返回true，失败返回false
 * @note 若index超出范围，返回false；内部通过拷贝方式替换
 */
bool  XVector_replace_1(XVector* this_vector, int64_t index, void* pvValue);
bool  XVector_replace_2(XVector* this_vector, int64_t index,const XVector* pvValue);
/**
 * @brief 替换XVector中指定索引位置的元素（移动语义）
 * @param this_vector 目标XVector
 * @param index 要替换的元素索引（0-based）
 * @param pvValue 新元素的指针
 * @return 替换成功返回true，失败返回false
 * @note 若index超出范围，返回false；内部通过移动方式替换，源数据可能失效
 */
bool  XVector_replace_move_1(XVector* this_vector, int64_t index, void* pvValue);
bool  XVector_replace_move_2(XVector* this_vector, int64_t index, XVector* pvValue);
#define XVector_copy_base							XContainer_copy_base	
#define XVector_move_base							XContainer_move_base	
#define XVector_deinit_base							XContainer_deinit_base	
#define XVector_delete_base							XContainer_delete_base	
#define XVector_clear_base							XContainer_clear_base	
#define XVector_isEmpty_base						XContainer_isEmpty_base	
#define XVector_size_base							XContainer_size_base	
#define XVector_capacity_base						XContainer_capacity_base
#define XVector_swap_base							XContainer_swap_base	
#define XVector_typeSize_base						XContainer_typeSize_base
#define XVector_count_base							XVector_size_base
#define XVector_length_base							XVector_size_base
#define XVector_prepend_1							XVector_push_front_1_base
#define XVector_prepend_2							XVector_push_front_2
#define XVector_prepend_3							XVector_push_front_3
#define XVector_prepend_move_1						XVector_push_front_move_1_base
#define XVector_prepend_move_2						XVector_push_front_move_2
#define XVector_prepend_move_3						XVector_push_front_move_3
#define XVector_removeAt_base(vector,index)			XVector_remove_base(vector,index,1)

/**
 * @brief 格式化构造字符串核心函数（内部使用）
 * @param vector 目标XVector（存储字符串数据）
 * @param appendNull 是否追加空字符('\0')
 * @param format 格式化字符串
 * @param argList 可变参数列表
 * @return 构造成功返回true，失败返回false
 * @note 用于实现字符串的格式化构建，通常不直接调用
 */
bool XVector_format_text_core(XVector* vector, bool appendNull, const char* format, va_list args);

/**
 * @brief 向XVector追加格式化文本
 * @param this_vector 目标XVector（存储字符串数据）
 * @param appendNull 是否追加空字符('\0')
 * @param format 格式化字符串
 * @param ... 可变参数
 * @return 追加成功返回true，失败返回false
 * @note 向量元素类型需为char，否则可能导致未定义行为
 */
bool XVector_append_text_fmt(XVector* this_vector, bool appendNull, const char* format, ...);

/**
 * @brief 创建存储格式化文本的XVector
 * @param appendNull 是否追加空字符('\0')
 * @param format 格式化字符串
 * @param ... 可变参数
 * @return 新创建的XVector指针，失败返回NULL
 * @note 新向量元素类型为char，存储格式化后的字符串
 */
XVector* XVector_create_text_fmt(bool appendNull, const char* format, ...);
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H