#ifndef XRUNNABLE_H
#define XRUNNABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XClass.h"
 /**
 * @brief 开始定义XRunnable类的虚函数表枚举
 * @note QRunnable只有一个纯虚函数run()
 */
XCLASS_DEFINE_BEGING(XRunnable)
// QRunnable特有的虚函数
XCLASS_DEFINE_ENUM(XRunnable, Run) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_END(XRunnable)
typedef struct XRunnable XRunnable;
 
/**
 * @brief 初始化XRunnable类的虚函数表
 * @return 指向初始化完成的XVtable的指针
 */
XVtable* XRunnable_class_init();

/**
 * @brief 在堆上创建XRunnable实例并初始化
 * @return 指向新创建的XRunnable对象的指针，失败返回NULL
 * @note 这是一个抽象基类，通常应该由子类实现
 */
XRunnable* XRunnable_create();

/**
 * @brief 初始化XRunnable实例
 * @param runnable 待初始化的XRunnable对象指针（非NULL）
 */
void XRunnable_init(XRunnable* runnable);

/**
 * @brief 销毁XRunnable实例
 * @param runnable 要销毁的XRunnable对象指针（非NULL）
 */
#define XRunnable_delete_base      XClass_delete_base

#define XRunnable_deinit_base      XClass_deinit_base
/**
 * @brief 执行可运行任务（虚函数调用）
 * @param runnable XRunnable对象指针（非NULL）
 * @note 这是纯虚函数，必须由子类实现
 */
void XRunnable_run_base(XRunnable* runnable);

/**
 * @brief 获取autoDelete属性值
 * @param runnable XRunnable对象指针（非NULL）
 * @return autoDelete属性值
 */
bool XRunnable_autoDelete(const XRunnable* runnable);

/**
 * @brief 设置autoDelete属性值
 * @param runnable XRunnable对象指针（非NULL）
 * @param autoDelete 是否自动删除
 */
void XRunnable_setAutoDelete(XRunnable* runnable, bool autoDelete);

/**
 * @brief 创建基于函数指针的XRunnable实例
 * @param function 要执行的函数指针（非NULL）
 * @param argsList 传递给函数的用户数据（可为NULL）
 * @param auto_delete 是否自动删除
 * @return 创建的XRunnable实例指针
 * @note 
 */
XRunnable* XRunnable_create_from_function(XCallableToRun function,XVarList* argsList,bool auto_delete);

#ifdef __cplusplus
}
#endif

#endif // XRUNNABLE_H