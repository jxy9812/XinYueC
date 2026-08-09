#ifndef XCLASS_CONFIG_H
#define XCLASS_CONFIG_H

/*
 * XClass 编译期配置。
 *
 * 这里的配置只负责 XClass/XVtable 模块。数值必须在编译期确定，方便
 * 裸机项目通过编译选项覆盖，也避免为运行时配置增加额外状态和代码。
 */

/**
 * @brief 是否把默认虚函数表放在静态栈表区域。
 *
 * 1：每个类使用静态 XVtable 和静态函数指针数组，不经过堆分配；
 * 0：每个类通过 XVtable_create() 在堆上创建虚函数表。
 *
 * 栈模式适合虚函数槽位固定的嵌入式固件，堆模式适合需要动态追加槽位
 * 或者不希望为未使用槽位预留静态空间的构建。默认保持原有栈模式。
 */
#ifndef XCLASS_VTABLE_USE_STACK
#define XCLASS_VTABLE_USE_STACK 1
#endif

/**
 * @brief 是否保留虚函数表名称。
 *
 * 1：允许 class_init 设置和读取 name；
 * 0：从 XVtable 结构中移除 name 成员，相关宏退化为空操作/NULL，适合
 *    对内存占用敏感且不需要调试类名的嵌入式构建。
 */
#ifndef XCLASS_VTABLE_ENABLE_NAME
#define XCLASS_VTABLE_ENABLE_NAME 1
#endif

/**
 * @brief 是否在类初始化时输出虚函数表槽位数量。
 *
 * 该选项只影响调试输出，不影响虚函数表布局。
 */
#ifndef XCLASS_VTABLE_SHOW_SIZE
#if defined(SHOWCONTAINERSIZE)
#define XCLASS_VTABLE_SHOW_SIZE SHOWCONTAINERSIZE
#else
#define XCLASS_VTABLE_SHOW_SIZE 0
#endif
#endif

/* 检查配置只接受 0/1，避免条件编译出现难以定位的结果。 */
#if (XCLASS_VTABLE_USE_STACK != 0) && (XCLASS_VTABLE_USE_STACK != 1)
#error "XClass: XCLASS_VTABLE_USE_STACK 只能设置为 0 或 1"
#endif
#if (XCLASS_VTABLE_ENABLE_NAME != 0) && (XCLASS_VTABLE_ENABLE_NAME != 1)
#error "XClass: XCLASS_VTABLE_ENABLE_NAME 只能设置为 0 或 1"
#endif
#if (XCLASS_VTABLE_SHOW_SIZE != 0) && (XCLASS_VTABLE_SHOW_SIZE != 1)
#error "XClass: XCLASS_VTABLE_SHOW_SIZE 只能设置为 0 或 1"
#endif

/* 旧宏保留为兼容别名；新代码统一使用 XCLASS_* 配置名。 */
#ifndef VTABLE_ISSTACK
#define VTABLE_ISSTACK XCLASS_VTABLE_USE_STACK
#endif
#ifndef SHOWCONTAINERSIZE
#define SHOWCONTAINERSIZE XCLASS_VTABLE_SHOW_SIZE
#endif

#endif /* XCLASS_CONFIG_H */
