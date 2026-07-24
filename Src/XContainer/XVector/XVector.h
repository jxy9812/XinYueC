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
XCLASS_DEFINE_ENUM(XVector, Reserve),
XCLASS_DEFINE_ENUM(XVector, Squeeze),
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
/**
 * @brief 宏定义：创建一个默认开启COW（写时拷贝）的动态数组
 * @param typeSize 数组中元素的类型大小（字节数）
 * @return 新创建的XVector指针，失败返回NULL
 * @note 内部调用XVector_create_ex(typeSize,true)，即useCow=true的便捷版本
 */
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
/**
 * @brief 向XVector头部添加一个数组（拷贝语义，对齐Qt QVector::prepend(count)）
 * @param this_vector 目标XVector
 * @param begin 数组起始地址
 * @param n 数组元素数量
 * @return 添加成功返回true，失败返回false
 * @note 宏实现，等价于XVector_insert_1_base(this_vector,0,begin,n)；this_vector/begin为NULL或n为0时返回false
 */
#define XVector_push_front_2(this_vector, begin, n)		XVector_insert_1_base(this_vector, 0, begin, n)
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
/**
 * @brief 向XVector头部添加一个数组（移动语义，对齐Qt QVector::prepend(count)）
 * @param this_vector 目标XVector
 * @param begin 数组起始地址
 * @param n 数组元素数量
 * @return 添加成功返回true，失败返回false
 * @note 宏实现，等价于XVector_insert_move_1_base(this_vector,0,begin,n)；源数组数据可能失效
 */
#define XVector_push_front_move_2(this_vector, begin, n)		XVector_insert_move_1_base(this_vector, 0, begin, n)
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
 * @brief 在XVector尾部原地构造一个元素（对齐C++ std::vector::emplace_back）
 * @param this_vector 目标XVector
 * @return 成功返回新元素的指针，失败返回NULL
 * @note 仅扩展size并返回新槽位指针，不初始化元素内容，由调用者负责赋值
 */
void* XVector_emplace_back(XVector* this_vector);
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
 * @brief 向XVector指定索引位置插入一个元素（拷贝语义，对齐Qt QVector::insert(index,value)）
 * @param this_vector 目标XVector
 * @param index 插入位置索引（0-based）
 * @param pvValue 待插入元素的指针
 * @return 插入成功返回true，失败返回false
 * @note 宏实现，等价于XVector_insert_1_base(this_vector,index,pvValue,1)；index越界由底层处理，通过拷贝插入
 */
#define XVector_insert_2(this_vector, index, pvValue)		XVector_insert_1_base(this_vector, index, pvValue, 1)
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
 * @brief 向XVector指定索引位置插入一个元素（移动语义，对齐Qt QVector::insert(index,value)）
 * @param this_vector 目标XVector
 * @param index 插入位置索引（0-based）
 * @param pvValue 待插入元素的指针
 * @return 插入成功返回true，失败返回false
 * @note 宏实现，等价于XVector_insert_move_1_base(this_vector,index,pvValue,1)；index越界由底层处理，通过移动插入，源数据可能失效
 */
#define XVector_insert_move_2(this_vector, index, pvValue)		XVector_insert_move_1_base(this_vector, index, pvValue, 1)
bool XVector_insert_move_3(XVector* this_vector, int64_t index, XVector* pvValue);

/**
 * @brief 向尾部追加单个元素（拷贝语义，对齐Qt QVector::append）
 * @note 宏实现，别名等价于XVector_push_back_1_base
 */
#define XVector_append_1_base			XVector_push_back_1_base
/**
 * @brief 向尾部追加数组（拷贝语义，对齐Qt QVector::append）
 * @note 宏实现，别名等价于XVector_push_back_2
 */
#define XVector_append_2				XVector_push_back_2
/**
 * @brief 向尾部追加另一个向量（拷贝语义，对齐Qt QVector::append）
 * @note 宏实现，别名等价于XVector_push_back_3
 */
#define XVector_append_3				XVector_push_back_3

/**
 * @brief 向尾部追加单个元素（移动语义，对齐Qt QVector::append）
 * @note 宏实现，别名等价于XVector_push_back_move_1_base
 */
#define XVector_append_move_1   XVector_push_back_move_1_base
/**
 * @brief 向尾部追加数组（移动语义，对齐Qt QVector::append）
 * @note 宏实现，别名等价于XVector_push_back_move_2
 */
#define XVector_append_move_2   XVector_push_back_move_2
/**
 * @brief 向尾部追加另一个向量（移动语义，对齐Qt QVector::append）
 * @note 宏实现，别名等价于XVector_push_back_move_3
 */
#define XVector_append_move_3   XVector_push_back_move_3

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
 * @brief 判断XVector是否包含指定元素（对齐Qt QVector::contains）
 * @param this_vector 目标XVector
 * @param value 待判断的元素指针
 * @return 包含返回true，否则返回false
 * @note 宏实现，等价于XVector_find_base(this_vector,value,NULL)；仅判断存在性，不返回位置
 */
#define XVector_contains(this_vector, value)		XVector_find_base(this_vector, value, NULL)

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
 * @brief 获取XVector中前n个元素组成的新向量（对齐Qt QVector::first(count)）
 * @param this_vector 源向量
 * @param n 要获取的元素数量
 * @return 包含前n个元素的新XVector，失败返回NULL
 * @note 宏实现，等价于XVector_mid(this_vector,0,n)；若n<=0返回空向量，若n大于向量大小返回整个向量的拷贝
 */
#define XVector_first(this_vector, n)		XVector_mid(this_vector, 0, n)

/**
 * @brief 对XVector中的元素进行排序
 * @param this_vector 目标XVector
 * @param order 排序顺序（升序或降序）
 * @note 排序基于元素的内存比较或自定义排序函数；空向量排序无效
 */
void  XVector_sort_base(XVector* this_vector, XSortOrder order);

/**
 * @brief 预留容量（对齐Qt QVector::reserve）
 * @param this_vector 目标XVector
 * @param size 要预留的元素数量
 * @return 成功返回true，失败返回false
 * @note 仅当size大于当前容量时才分配内存，不缩小容量也不改变元素数量；
 *       用于预先分配足够空间以避免多次扩容带来的性能开销
 */
bool  XVector_reserve_base(XVector* this_vector, size_t size);

/**
 * @brief 释放多余容量（对齐Qt QVector::squeeze）
 * @param this_vector 目标XVector
 * @note 将容量缩减到刚好等于当前元素数量，释放未使用的内存；元素数量不变
 */
void  XVector_squeeze_base(XVector* this_vector);

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

/**
 * @brief 调整大小并用指定值填充新增元素（对齐Qt QVector::resize(size, value)）
 * @param this_vector 目标XVector
 * @param size 调整后的元素数量
 * @param pvValue 用于填充新增元素的值指针（为NULL时退化为置0填充）
 * @return 成功返回true，失败返回false
 * @note 新大小大于旧大小时，新增元素用pvValue填充；小于旧大小时，超出部分元素被析构删除
 */
bool  XVector_resize_2(XVector* this_vector, size_t size, void* pvValue);

/**
 * @brief 用指定值填充整个向量（对齐Qt QVector::fill）
 * @param this_vector 目标XVector
 * @param pvValue 填充值指针
 * @param size 填充后的元素数量（为-1表示保持当前元素数量不变）
 * @return 成功返回true，失败返回false
 * @note 会改变元素数量为size，所有元素都被替换为pvValue的拷贝
 */
bool  XVector_fill(XVector* this_vector, void* pvValue, int64_t size);

/**
 * @brief 删除所有等于指定值的元素（对齐Qt QVector::removeAll）
 * @param this_vector 目标XVector
 * @param value 待删除元素的值指针
 * @return 实际删除的元素数量
 * @note 使用容器的比较函数或内存比较判断相等；不存在的值返回0
 */
size_t XVector_removeAll(XVector* this_vector, const void* value);

/**
 * @brief 删除第一个等于指定值的元素（对齐Qt QVector::removeOne）
 * @param this_vector 目标XVector
 * @param value 待删除元素的值指针
 * @return 删除成功返回true，未找到返回false
 */
bool  XVector_removeOne(XVector* this_vector, const void* value);

/**
 * @brief 统计等于指定值的元素个数（对齐Qt QVector::count(value)）
 * @param this_vector 目标XVector
 * @param value 待统计元素的值指针
 * @return 等于该值的元素数量
 * @note 注意与XVector_count_base（返回元素总数）区分
 */
size_t XVector_count_value(const XVector* this_vector, const void* value);

/**
 * @brief 将指定位置的元素移动到新位置（对齐Qt QVector::move）
 * @param this_vector 目标XVector
 * @param from 源位置索引（0-based）
 * @param to 目标位置索引（0-based）
 * @note from与to都必须在有效范围内且不相等；其余元素相应顺移，元素总数不变。
 *       注意：本函数为元素重定位，与移动语义的XVector_move_base（整体资源转移）含义不同
 */
void  XVector_move(XVector* this_vector, int64_t from, int64_t to);

/**
 * @brief 交换两个位置的元素（对齐Qt QVector::swapItemsAt）
 * @param this_vector 目标XVector
 * @param i 第一个位置索引（0-based）
 * @param j 第二个位置索引（0-based）
 * @note 索引越界或相等时不操作
 */
void  XVector_swapItemsAt(XVector* this_vector, int64_t i, int64_t j);

/**
 * @brief 取出并删除指定位置元素（对齐Qt QVector::takeAt）
 * @param this_vector 目标XVector
 * @param index 元素索引（0-based）
 * @return 新分配内存存放被取出元素，失败或越界返回NULL
 * @note 调用者需用XFree_System释放返回的内存；取出后该元素从向量中移除（移动语义）
 */
void* XVector_takeAt(XVector* this_vector, int64_t index);

/**
 * @brief 判断向量首元素是否等于指定值（对齐Qt QVector::startsWith）
 * @param this_vector 目标XVector
 * @param value 待比较的值指针
 * @return 首元素等于该值返回true，向量为空或不等返回false
 */
bool  XVector_startsWith(const XVector* this_vector, const void* value);

/**
 * @brief 判断向量尾元素是否等于指定值（对齐Qt QVector::endsWith）
 * @param this_vector 目标XVector
 * @param value 待比较的值指针
 * @return 尾元素等于该值返回true，向量为空或不等返回false
 */
bool  XVector_endsWith(const XVector* this_vector, const void* value);
/**
 * @brief 获取可写的数据裸指针（对齐Qt QVector::data()）
 * @param this_vector 目标XVector
 * @return 指向底层元素数组的可写指针，向量为空或失败返回NULL
 * @note 非const访问：返回前触发COW分离（若数据被共享），通过该指针写入不会污染共享数据；
 *       仅读取时建议使用XVector_constData以避免不必要的深拷贝
 */
void* XVector_data(XVector* this_vector);

/**
 * @brief 获取只读的数据裸指针（对齐Qt QVector::constData()）
 * @param this_vector 目标XVector
 * @return 指向底层元素数组的只读指针，向量为空或失败返回NULL
 * @note const访问：不触发COW分离，适合只读场景
 */
const void* XVector_constData(const XVector* this_vector);

/**
 * @brief 删除所有满足谓词条件的元素（对齐Qt QVector::removeIf）
 * @param this_vector 目标XVector
 * @param pred 谓词回调，返回true表示该元素需删除（参数1为元素指针，参数2为userData）
 * @param userData 传递给谓词的用户数据指针，可为NULL
 * @return 实际删除的元素数量
 * @note 复用项目现有XEquality回调类型；删除时会对被删元素调用析构回调
 */
size_t XVector_removeIf(XVector* this_vector, XEquality pred, const void* userData);

/**
 * @brief 字典序比较两个XVector（对齐Qt QVector::operator< / operator==）
 * @param lhs 左侧XVector
 * @param rhs 右侧XVector
 * @return lhs<rhs返回XCompare_Less；lhs>rhs返回XCompare_Greater；完全相等返回XCompare_Equality；
 *         参数为NULL或元素类型不一致返回XCompare_Other
 * @note 逐元素比较（使用容器比较函数或内存比较），公共元素全相等时按数量决断
 */
int32_t XVector_compare(const XVector* lhs, const XVector* rhs);

/**
 * @brief 调整大小但不初始化新增元素（对齐Qt QVector::resizeForOverwrite）
 * @param this_vector 目标XVector
 * @param size 调整后的大小
 * @return 调整成功返回true，失败返回false
 * @note 与XVector_resize_base的区别：新增元素不清零，保留未初始化内存，适合随后立即覆盖写入的场景；
 *       缩小时仍会对被删除元素调用析构回调
 */
bool XVector_resizeForOverwrite(XVector* this_vector, size_t size);

/**
 * @brief 判断两个XVector是否共享同一底层数据块（对齐Qt QVector::isSharedWith）
 * @param this_vector 目标XVector
 * @param other 另一个XVector
 * @return 二者指向同一COW共享块返回true，否则返回false
 * @note 仅COW模式可能共享；非COW各自独立分配永远返回false；两个均为空的向量不算共享
 */
bool XVector_isSharedWith(const XVector* this_vector, const XVector* other);

/**
 * @brief 强制COW分离，确保本向量独占数据（对齐Qt QVector::detach）
 * @param this_vector 目标XVector
 * @note 若数据被共享则深拷贝一份；已独占或非COW时为空操作。常用于通过裸指针直接写入前确保不被共享
 */
void XVector_detach(XVector* this_vector);

/**
 * @brief 判断本向量是否独占数据（对齐Qt QVector::isDetached）
 * @param this_vector 目标XVector
 * @return 数据未被共享（引用计数为1）返回true；被共享返回false
 * @note 非COW永远返回true；空向量返回true；语义等价于!共享
 */
bool XVector_isDetached(const XVector* this_vector);

/**
 * @brief 获取指定元素类型大小下理论上可容纳的最大元素数量（对齐Qt QVector::maxSize，静态语义）
 * @param typeSize 单个元素的类型大小（字节数）
 * @return 不发生 capacity*typeSize 乘法溢出的最大元素数；typeSize为0返回0
 * @note 为理论上限，实际受可用内存限制；保证返回值用于reserve/resize时乘法不溢出
 */
size_t XVector_maxSize(size_t typeSize);
/**
 * @brief 获取当前向量理论上可容纳的最大元素数量（对齐STL max_size，成员语义）
 * @param this_vector 目标XVector
 * @note 宏实现，等价于XVector_maxSize(XVector_typeSize_base(this_vector))，自动取本向量的元素类型大小
 */
#define XVector_max_size(this_vector)		XVector_maxSize(XVector_typeSize_base(this_vector))

/**
 * @brief 获取指定位置元素，越界返回默认值（对齐Qt QVector::value）
 * @param this_vector 目标XVector
 * @param index 元素索引（0-based）
 * @param defaultValue 越界时返回的默认值指针
 * @return 索引有效返回指向该元素的指针，越界返回defaultValue
 * @note 宏实现；与XVector_at_base（越界返回NULL）的区别在于提供默认值
 */
#define XVector_value(this_vector, index, defaultValue) \
	(((index) >= 0 && (size_t)(index) < XVector_size_base(this_vector)) ? XVector_at_base(this_vector, index) : (void*)(defaultValue))
/**
 * @brief 复用XContainer的接口，拷贝容器（深拷贝源向量到目标向量）
 * @note 宏实现，等价于XContainer_copy_base；目标向量需先init
 */
#define XVector_copy_base							XContainer_copy_base	
/**
 * @brief 复用XContainer的接口，移动容器资源（转移源向量所有权，源向量变空）
 * @note 宏实现，等价于XContainer_move_base
 */
#define XVector_move_base							XContainer_move_base	
/**
 * @brief 复用XContainer的接口，析构容器（释放资源，可重复调用）
 * @note 宏实现，等价于XContainer_deinit_base
 */
#define XVector_deinit_base							XContainer_deinit_base	
/**
 * @brief 复用XContainer的接口，删除并释放堆对象（先析构再释放内存）
 * @note 宏实现，等价于XContainer_delete_base
 */
#define XVector_delete_base							XContainer_delete_base	
/**
 * @brief 复用XContainer的接口，清空所有元素（保留容量）
 * @note 宏实现，等价于XContainer_clear_base
 */
#define XVector_clear_base							XContainer_clear_base	
/**
 * @brief 复用XContainer的接口，判断容器是否为空
 * @note 宏实现，等价于XContainer_isEmpty_base
 */
#define XVector_isEmpty_base						XContainer_isEmpty_base	
/**
 * @brief 复用XContainer的接口，获取元素数量
 * @note 宏实现，等价于XContainer_size_base
 */
#define XVector_size_base							XContainer_size_base	
/**
 * @brief 复用XContainer的接口，获取当前容量
 * @note 宏实现，等价于XContainer_capacity_base
 */
#define XVector_capacity_base						XContainer_capacity_base
/**
 * @brief 复用XContainer的接口，交换两个容器的内容
 * @note 宏实现，等价于XContainer_swap_base
 */
#define XVector_swap_base							XContainer_swap_base	
/**
 * @brief 复用XContainer的接口，获取单个元素的类型大小（字节数）
 * @note 宏实现，等价于XContainer_typeSize_base
 */
#define XVector_typeSize_base						XContainer_typeSize_base
/**
 * @brief 获取元素数量（对齐Qt QVector::count）
 * @note 宏实现，别名等价于XVector_size_base
 */
#define XVector_count_base							XVector_size_base
/**
 * @brief 获取元素数量（对齐Qt QVector::length）
 * @note 宏实现，别名等价于XVector_size_base
 */
#define XVector_length_base							XVector_size_base

/**
 * @brief 获取元素数量（对齐Qt QVector::size()，无_base后缀便捷版）
 * @param this_vector 目标XVector
 * @return 元素数量
 * @note 宏实现，等价于 XVector_size_base
 */
#define XVector_size(this_vector)					XVector_size_base(this_vector)

/**
 * @brief 替换指定索引的元素（对齐Qt QVector::replace(index, value)）
 * @param this_vector 目标XVector
 * @param index 要替换的索引
 * @param pvValue 新值的指针
 * @return 替换成功返回true
 * @note 宏实现，等价于 XVector_replace_1
 */
#define XVector_replace(this_vector, index, pvValue)	XVector_replace_1(this_vector, index, pvValue)

/**
 * @brief 统计指定值的出现次数（对齐Qt QVector::count(value)）
 * @param this_vector 目标XVector
 * @param value 要统计的值指针
 * @return 出现次数
 * @note 宏实现，等价于 XVector_count_value
 */
#define XVector_count(this_vector, value)			XVector_count_value(this_vector, value)

/**
 * @brief 向头部添加单个元素（拷贝语义，对齐Qt QVector::prepend）
 * @note 宏实现，别名等价于XVector_push_front_1_base
 */
#define XVector_prepend_1_base						XVector_push_front_1_base
/**
 * @brief 向头部添加数组（拷贝语义，对齐Qt QVector::prepend）
 * @note 宏实现，别名等价于XVector_push_front_2
 */
#define XVector_prepend_2							XVector_push_front_2
/**
 * @brief 向头部添加另一个向量（拷贝语义，对齐Qt QVector::prepend）
 * @note 宏实现，别名等价于XVector_push_front_3
 */
#define XVector_prepend_3							XVector_push_front_3
/**
 * @brief 向头部添加单个元素（移动语义，对齐Qt QVector::prepend）
 * @note 宏实现，别名等价于XVector_push_front_move_1_base
 */
#define XVector_prepend_move_1_base					XVector_push_front_move_1_base
/**
 * @brief 向头部添加数组（移动语义，对齐Qt QVector::prepend）
 * @note 宏实现，别名等价于XVector_push_front_move_2
 */
#define XVector_prepend_move_2						XVector_push_front_move_2
/**
 * @brief 向头部添加另一个向量（移动语义，对齐Qt QVector::prepend）
 * @note 宏实现，别名等价于XVector_push_front_move_3
 */
#define XVector_prepend_move_3						XVector_push_front_move_3
/**
 * @brief 删除指定索引位置的单个元素（对齐Qt QVector::removeAt）
 * @param vector 目标XVector
 * @param index 待删除元素索引（0-based）
 * @note 宏实现，等价于XVector_remove_base(vector,index,1)；索引越界时不操作
 */
#define XVector_removeAt_base(vector,index)			XVector_remove_base(vector,index,1)

/**
 * @brief 删除首元素（对齐Qt QVector::removeFirst）
 * @param vector 目标XVector
 * @note 宏实现，等价于XVector_pop_front_base；向量为空时无效
 */
#define XVector_removeFirst_base					XVector_pop_front_base
/**
 * @brief 删除尾元素（对齐Qt QVector::removeLast）
 * @param vector 目标XVector
 * @note 宏实现，等价于XVector_pop_back_base；向量为空时无效
 */
#define XVector_removeLast_base						XVector_pop_back_base
/**
 * @brief 释放多余容量（对齐Qt QVector::shrink_to_fit / STL）
 * @param vector 目标XVector
 * @note 宏实现，等价于XVector_squeeze_base；将容量缩减到刚好等于元素数量
 */
#define XVector_shrink_to_fit						XVector_squeeze_base
/**
 * @brief 取出并删除首元素（对齐Qt QVector::takeFirst）
 * @param this_vector 目标XVector
 * @return 新分配内存存放被取出元素，越界或失败返回NULL
 * @note 宏实现，等价于XVector_takeAt(this_vector,0)；调用者需用XFree_System释放
 */
#define XVector_takeFirst(this_vector)				XVector_takeAt(this_vector, 0)
/**
 * @brief 取出并删除尾元素（对齐Qt QVector::takeLast）
 * @param this_vector 目标XVector
 * @return 新分配内存存放被取出元素，越界或失败返回NULL
 * @note 宏实现，等价于XVector_takeAt(this_vector,size-1)；调用者需用XFree_System释放
 */
#define XVector_takeLast(this_vector)				XVector_takeAt(this_vector, (int64_t)XVector_size_base(this_vector) - 1)
/**
 * @brief 判断两个XVector是否相等（对齐Qt QVector::operator==）
 * @param lhs 左侧XVector
 * @param rhs 右侧XVector
 * @return 元素数量与所有元素均相等返回true，否则返回false
 * @note 宏实现，等价于XVector_compare(lhs,rhs)==XCompare_Equality
 */
#define XVector_equals(lhs, rhs)					(XVector_compare(lhs, rhs) == XCompare_Equality)
/**
 * @brief 字典序小于比较（对齐Qt QVector::operator<）
 * @param lhs 左侧XVector
 * @param rhs 右侧XVector
 * @return lhs按字典序小于rhs返回true，否则返回false
 * @note 宏实现，等价于XVector_compare(lhs,rhs)==XCompare_Less；类型不一致或NULL返回false
 */
#define XVector_lessThan(lhs, rhs)		(XVector_compare(lhs, rhs) == XCompare_Less)
/**
 * @brief 字典序大于比较（对齐Qt QVector::operator>）
 * @param lhs 左侧XVector
 * @param rhs 右侧XVector
 * @return lhs按字典序大于rhs返回true，否则返回false
 * @note 宏实现，等价于XVector_compare(lhs,rhs)==XCompare_Greater；类型不一致或NULL返回false
 */
#define XVector_greaterThan(lhs, rhs)	(XVector_compare(lhs, rhs) == XCompare_Greater)
/**
 * @brief 字典序小于等于比较（对齐Qt QVector::operator<=）
 * @param lhs 左侧XVector
 * @param rhs 右侧XVector
 * @return lhs小于或等于rhs返回true，否则返回false
 * @note 宏实现，等价于XVector_compare结果为Less或Equality；类型不一致或NULL返回false；注意compare会被求值两次
 */
#define XVector_lessEqual(lhs, rhs)		(XVector_compare(lhs, rhs) == XCompare_Less || XVector_compare(lhs, rhs) == XCompare_Equality)
/**
 * @brief 字典序大于等于比较（对齐Qt QVector::operator>=）
 * @param lhs 左侧XVector
 * @param rhs 右侧XVector
 * @return lhs大于或等于rhs返回true，否则返回false
 * @note 宏实现，等价于XVector_compare结果为Greater或Equality；类型不一致或NULL返回false；注意compare会被求值两次
 */
#define XVector_greaterEqual(lhs, rhs)	(XVector_compare(lhs, rhs) == XCompare_Greater || XVector_compare(lhs, rhs) == XCompare_Equality)
/**
 * @brief 获取首元素的只读指针（对齐Qt QVector::constFirst）
 * @param this_vector 目标XVector
 * @return 指向首元素的指针，向量为空返回NULL
 * @note 宏实现，等价于XVector_front_base；const语义，不触发COW分离
 */
#define XVector_constFirst(this_vector)				XVector_front_base(this_vector)
/**
 * @brief 获取尾元素的只读指针（对齐Qt QVector::constLast）
 * @param this_vector 目标XVector
 * @return 指向尾元素的指针，向量为空返回NULL
 * @note 宏实现，等价于XVector_back_base；const语义，不触发COW分离
 */
#define XVector_constLast(this_vector)				XVector_back_base(this_vector)
/**
 * @brief 获取从指定位置到末尾的子向量（对齐Qt QVector::sliced(pos)）
 * @param this_vector 源向量
 * @param pos 起始位置索引（0-based）
 * @return 包含从pos到末尾所有元素的新XVector，失败返回NULL
 * @note 宏实现，等价于XVector_mid(this_vector,pos,-1)
 */
#define XVector_sliced_1(this_vector, pos)			XVector_mid(this_vector, pos, -1)
/**
 * @brief 获取从指定位置开始指定长度的子向量（对齐Qt QVector::sliced(pos,n)）
 * @param this_vector 源向量
 * @param pos 起始位置索引（0-based）
 * @param n 要获取的元素数量
 * @return 包含子向量的新XVector，失败返回NULL
 * @note 宏实现，等价于XVector_mid(this_vector,pos,n)
 */
#define XVector_sliced_2(this_vector, pos, n)		XVector_mid(this_vector, pos, n)
/**
 * @brief 用指定值赋值并调整元素数量（对齐Qt QVector::assign）
 * @param this_vector 目标XVector
 * @param value 填充值指针
 * @param n 填充后的元素数量
 * @return 宏无返回值语义（内部调用XVector_fill返回bool）
 * @note 宏实现，等价于XVector_fill(this_vector,value,n)
 */
#define XVector_assign(this_vector, value, n)		XVector_fill(this_vector, value, n)

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