/**
 * @file       XSqlDatabase.h
 * @brief      SQL 数据库连接类，对齐 Qt 6.8 QSqlDatabase。
 * @details    连接和驱动注册使用静态源码注册表，不依赖动态插件加载。
 */
#ifndef XSQLDATABASE_H
#define XSQLDATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlDriver.h"

typedef struct XSqlQuery XSqlQuery;
typedef struct XSqlDatabasePrivate XSqlDatabasePrivate;
typedef struct XThread XThread;

/**
 * @brief 驱动创建回调。
 * @return 新驱动对象的所有权；数据库连接创建成功后由注册表接管，创建失败时调用方负责释放；失败返回 NULL。
 */
typedef XSqlDriver* (*XSqlDriverCreateMethod)(void);

XCLASS_DEFINE_BEGING(XSqlDriverCreatorBase)
XCLASS_DEFINE_ENUM(XSqlDriverCreatorBase, CreateObject) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_END(XSqlDriverCreatorBase)

/**
 * @brief 驱动创建器基类。
 * @details 对齐 Qt QSqlDriverCreatorBase；对象本身使用 XClass 管理，不参与事件循环。
 */
typedef struct XSqlDriverCreatorBase {
    XClass m_class;                 /**< 第一个成员，由 XClass 管理。 */
    XSqlDriverCreateMethod m_create; /**< 驱动创建回调；创建器对象不接管回调函数本身。 */
} XSqlDriverCreatorBase;

XCLASS_DEFINE_BEGING(XSqlDriverCreator)
XCLASS_DEFINE_EXTEND_END(XSqlDriverCreator, XSqlDriverCreatorBase)

/**
 * @brief C 语言中的 QSqlDriverCreator<T> 等价类型。
 * @details m_create 保存具体后端创建回调；对象不能通过事件系统使用。
 */
typedef struct XSqlDriverCreator {
    XSqlDriverCreatorBase m_parent; /**< 驱动创建器基类。 */
} XSqlDriverCreator;

/**
 * @brief 初始化驱动创建器基类虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlDriverCreatorBase_class_init(void);
/**
 * @brief 初始化驱动创建器虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlDriverCreator_class_init(void);
/**
 * @brief 创建带回调的驱动创建器。
 * @param createMethod 驱动创建回调；不能为 NULL。
 * @return 新创建器，调用者必须使用 XSqlDriverCreator_delete_base 释放；失败返回 NULL。
 */
XSqlDriverCreator* XSqlDriverCreator_create(XSqlDriverCreateMethod createMethod);
/**
 * @brief 初始化栈上驱动创建器。
 * @param creator 待初始化创建器；不能为 NULL。
 * @param createMethod 驱动创建回调；不能为 NULL。
 * @return 无。
 */
void XSqlDriverCreator_init(XSqlDriverCreator* creator, XSqlDriverCreateMethod createMethod);
/**
 * @brief 通过创建器生成驱动对象。
 * @param creator 创建器；不能为 NULL。
 * @return 新驱动对象，调用者取得所有权；失败返回 NULL。
 */
XSqlDriver* XSqlDriverCreatorBase_createObject_base(const XSqlDriverCreatorBase* creator);

#define XSqlDriverCreator_deinit_base XClass_deinit_base
#define XSqlDriverCreator_delete_base XClass_delete_base
#define XSqlDriverCreatorBase_deinit_base XClass_deinit_base
#define XSqlDriverCreatorBase_delete_base XClass_delete_base

XCLASS_DEFINE_BEGING(XSqlDatabase)
XCLASS_DEFINE_EXTEND_END(XSqlDatabase, XClass)

/**
 * @brief 数据库连接对象。
 */
typedef struct XSqlDatabase {
    XClass m_class;              /**< 第一个成员，由 XClass 管理。 */
    XSqlDatabasePrivate* m_d;    /**< 共享连接数据，仅供实现使用。 */
} XSqlDatabase;

/**
 * @brief 默认连接名。
 * @details 对齐 Qt QSqlDatabase::defaultConnection；字符串由库静态持有，调用者不得释放。
 */
extern const char* XSqlDatabase_defaultConnection;

/**
 * @brief 初始化数据库句柄虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlDatabase_class_init(void);
/**
 * @brief 初始化空数据库句柄。
 * @param database 待初始化对象；不能为 NULL。
 * @return 无。
 */
void XSqlDatabase_init(XSqlDatabase* database);
/**
 * @brief 创建空数据库句柄。
 * @return 新句柄，调用者必须使用 XSqlDatabase_delete_base 释放；失败返回 NULL。
 */
XSqlDatabase* XSqlDatabase_create(void);
/**
 * @brief 复制数据库句柄。
 * @param other 源句柄；借用，不能为 NULL。
 * @return 新句柄；底层连接数据按 Qt 隐式共享语义共享，调用者负责释放。
 */
XSqlDatabase* XSqlDatabase_create_copy(const XSqlDatabase* other);
/**
 * @brief 移动创建数据库句柄。
 * @param other 源句柄；不能为 NULL，成功后句柄为空但仍需反初始化。
 * @return 新句柄，调用者必须使用 XSqlDatabase_delete_base 释放；失败返回 NULL。
 */
XSqlDatabase* XSqlDatabase_create_move(XSqlDatabase* other);

/** @brief 调用 XClass 的析构入口销毁数据库句柄内部共享引用。 */
#define XSqlDatabase_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlDatabase_create 系列函数返回的数据库句柄。 */
#define XSqlDatabase_delete_base XClass_delete_base
/** @brief 将数据库句柄深复制到既有目标对象的基础复制入口。 */
#define XSqlDatabase_copy_base XClass_copy_base
/** @brief 将数据库句柄资源移入既有目标对象的基础移动入口。 */
#define XSqlDatabase_move_base XClass_move_base

/**
 * @brief 按驱动枚举添加连接。
 * @param type 驱动类型。
 * @param connectionName UTF-8 连接名；借用，NULL 使用默认连接名。
 * @return 新连接句柄，调用者负责释放；驱动不可用或内存不足返回 NULL。
 */
XSqlDatabase* XSqlDatabase_addDatabase(XSqlDriverType type, const char* connectionName);
/**
 * @brief 按驱动对象添加连接。
 * @param driver 驱动对象；转移所有权，不能为 NULL。
 * @param connectionName UTF-8 连接名；借用，NULL 使用默认连接名。
 * @return 新连接句柄，调用者负责释放；失败时 driver 仍由调用者负责。
 */
XSqlDatabase* XSqlDatabase_addDatabase_driver(XSqlDriver* driver, const char* connectionName);
/**
 * @brief 按 UTF-8 驱动名称添加连接。
 * @param type 驱动名称；借用。
 * @param connectionName UTF-8 连接名；借用，NULL 使用默认连接名。
 * @return 新连接句柄，调用者负责释放；驱动不可用时返回 NULL。
 */
XSqlDatabase* XSqlDatabase_addDatabase_utf8(const char* type, const char* connectionName);
/**
 * @brief 使用 XString 驱动名和连接名添加连接。
 * @param type 驱动名称；借用，可为 NULL。
 * @param connectionName 连接名；借用，NULL 使用默认连接名。
 * @return 新连接句柄所有权；驱动不可用或内存不足返回 NULL。
 */
XSqlDatabase* XSqlDatabase_addDatabase_2(const XString* type, const XString* connectionName);
/**
 * @brief 克隆连接配置到新连接句柄。
 * @param other 源连接；借用，不能为 NULL。
 * @param connectionName 新连接名；借用。
 * @return 新连接句柄，调用者负责释放；失败返回 NULL。
 */
XSqlDatabase* XSqlDatabase_cloneDatabase(const XSqlDatabase* other, const char* connectionName);
/**
 * @brief 使用 XString 连接名克隆连接配置。
 * @param other 源连接；借用，不能为 NULL。
 * @param connectionName 新连接名；借用，可为 NULL。
 * @return 新连接句柄所有权；源连接无效或内存不足返回 NULL。
 */
XSqlDatabase* XSqlDatabase_cloneDatabase_2(const XSqlDatabase* other, const XString* connectionName);
/**
 * @brief 按连接名克隆连接配置。
 * @param otherConnectionName 源连接名；借用。
 * @param connectionName 新连接名；借用。
 * @return 新连接句柄，调用者负责释放；源连接不存在时返回 NULL。
 */
XSqlDatabase* XSqlDatabase_cloneDatabase_name(const char* otherConnectionName, const char* connectionName);
/**
 * @brief 使用 XString 连接名克隆连接配置。
 * @param otherConnectionName 源连接名；借用，NULL 表示默认连接。
 * @param connectionName 新连接名；借用，NULL 使用默认连接名。
 * @return 新连接句柄所有权；源连接不存在或内存不足返回 NULL。
 */
XSqlDatabase* XSqlDatabase_cloneDatabase_name_2(const XString* otherConnectionName, const XString* connectionName);
/**
 * @brief 按连接名获取数据库句柄。
 * @param connectionName UTF-8 连接名；借用，NULL 表示默认连接。
 * @param open 是否在获取时打开连接。
 * @return 新句柄，调用者负责释放；连接不存在或不属于当前线程时返回空句柄。
 */
XSqlDatabase* XSqlDatabase_database(const char* connectionName, bool open);
/**
 * @brief 使用 XString 连接名获取数据库句柄。
 * @param connectionName 连接名；借用，NULL 表示默认连接。
 * @param open 是否在取得句柄时打开连接。
 * @return 新句柄所有权；连接不存在、线程不匹配或打开失败时返回空句柄。
 */
XSqlDatabase* XSqlDatabase_database_2(const XString* connectionName, bool open);
/**
 * @brief 移除连接注册表中的连接。
 * @param connectionName UTF-8 连接名；借用，NULL 表示默认连接。
 * @return 无；仍被使用的连接移除行为由实现决定。
 */
void XSqlDatabase_removeDatabase(const char* connectionName);
/**
 * @brief 使用 XString 连接名移除连接注册。
 * @param connectionName 连接名；借用，NULL 表示默认连接。
 * @return 无；仍被其他句柄使用时按 Qt 语义仅移除注册表条目。
 */
void XSqlDatabase_removeDatabase_2(const XString* connectionName);
/**
 * @brief 判断连接名是否存在。
 * @param connectionName UTF-8 连接名；借用，NULL 表示默认连接。
 * @return 存在返回 true，否则返回 false。
 */
bool XSqlDatabase_contains(const char* connectionName);
/**
 * @brief 使用 XString 连接名判断连接是否存在。
 * @param connectionName 连接名；借用，NULL 表示默认连接。
 * @return 已注册返回 true，否则返回 false。
 */
bool XSqlDatabase_contains_2(const XString* connectionName);
/**
 * @brief 获取已注册驱动名称列表。
 * @return 新字符串列表，调用者必须使用 XStringList_delete_base 释放。
 */
XStringList* XSqlDatabase_drivers(void);
/**
 * @brief 获取连接名称列表。
 * @return 新字符串列表，调用者必须使用 XStringList_delete_base 释放。
 */
XStringList* XSqlDatabase_connectionNames(void);
/**
 * @brief 注册源码驱动创建器。
 * @param name UTF-8 驱动名称；借用，不能为 NULL。
 * @param creator 创建器；借用，注册表不接管其所有权。
 * @return 注册成功返回 true，否则返回 false。
 */
bool XSqlDatabase_registerSqlDriver(const char* name, XSqlDriverCreatorBase* creator);
/**
 * @brief 使用 XString 驱动名注册源码驱动创建器。
 * @param name 驱动名称；借用，不能为 NULL。
 * @param creator 创建器；借用，注册表不接管所有权，必须在注册存续期内有效。
 * @return 注册成功返回 true；名称或创建器无效时返回 false。
 */
bool XSqlDatabase_registerSqlDriver_2(const XString* name, XSqlDriverCreatorBase* creator);
/**
 * @brief 按驱动枚举注册源码驱动创建器。
 * @param type 驱动类型。
 * @param creator 创建器；借用，注册表不接管其所有权。
 * @return 注册成功返回 true，否则返回 false。
 */
bool XSqlDatabase_registerSqlDriver_type(XSqlDriverType type, XSqlDriverCreatorBase* creator);
/**
 * @brief 判断驱动是否可用。
 * @param name UTF-8 驱动名称；借用。
 * @return 已注册返回 true，否则返回 false。
 */
bool XSqlDatabase_isDriverAvailable(const char* name);
/**
 * @brief 使用 XString 驱动名判断驱动是否可用。
 * @param name 驱动名称；借用，可为 NULL。
 * @return 已注册并可创建返回 true，否则返回 false。
 */
bool XSqlDatabase_isDriverAvailable_2(const XString* name);
/**
 * @brief 打开数据库连接。
 * @param database 数据库句柄；不能为 NULL。
 * @return 打开成功返回 true，否则返回 false；跨驱动所属线程调用时返回 false。
 */
bool XSqlDatabase_open(XSqlDatabase* database);
/**
 * @brief 使用用户名和密码打开连接。
 * @param database 数据库句柄；不能为 NULL。
 * @param user 用户名；借用。
 * @param password 密码；借用。
 * @return 打开成功返回 true，否则返回 false。
 */
bool XSqlDatabase_open_2(XSqlDatabase* database, const XString* user, const XString* password);
/**
 * @brief 使用 UTF-8 用户名和密码打开连接。
 * @param database 数据库句柄；不能为 NULL。
 * @param user UTF-8 用户名；借用。
 * @param password UTF-8 密码；借用。
 * @return 打开成功返回 true，否则返回 false。
 */
bool XSqlDatabase_open_utf8(XSqlDatabase* database, const char* user, const char* password);
/**
 * @brief 关闭数据库连接。
 * @param database 数据库句柄；不能为 NULL。
 * @return 无。
 */
void XSqlDatabase_close(XSqlDatabase* database);
/**
 * @brief 判断连接是否已打开。
 * @param database 数据库句柄；NULL 返回 false。
 * @return 已打开返回 true，否则返回 false。
 */
bool XSqlDatabase_isOpen(const XSqlDatabase* database);
/**
 * @brief 判断最近一次打开是否失败。
 * @param database 数据库句柄；NULL 返回 true。
 * @return 打开失败返回 true，否则返回 false。
 */
bool XSqlDatabase_isOpenError(const XSqlDatabase* database);
/**
 * @brief 获取表名列表。
 * @param database 数据库句柄；不能为 NULL。
 * @param type 表类型过滤标志。
 * @return 新字符串列表，调用者必须使用 XStringList_delete_base 释放。
 */
XStringList* XSqlDatabase_tables(const XSqlDatabase* database, XSqlTableType type);
/**
 * @brief 获取主键索引副本。
 * @param database 数据库句柄；不能为 NULL。
 * @param tableName 表名；借用。
 * @return 新索引，调用者必须使用 XSqlIndex_delete_base 释放。
 */
XSqlIndex* XSqlDatabase_primaryIndex(const XSqlDatabase* database, const XString* tableName);
/**
 * @brief 使用 UTF-8 表名获取主键索引副本。
 * @param database 数据库句柄；不能为 NULL。
 * @param tableName UTF-8 表名；借用。
 * @return 新索引，调用者必须使用 XSqlIndex_delete_base 释放。
 */
XSqlIndex* XSqlDatabase_primaryIndex_utf8(const XSqlDatabase* database, const char* tableName);
/**
 * @brief 获取表记录描述副本。
 * @param database 数据库句柄；不能为 NULL。
 * @param tableName 表名；借用。
 * @return 新记录，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlDatabase_record(const XSqlDatabase* database, const XString* tableName);
/**
 * @brief 使用 UTF-8 表名获取记录描述副本。
 * @param database 数据库句柄；不能为 NULL。
 * @param tableName UTF-8 表名；借用。
 * @return 新记录，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlDatabase_record_utf8(const XSqlDatabase* database, const char* tableName);
/**
 * @brief 执行 SQL 查询。
 * @param database 数据库句柄；不能为 NULL。
 * @param query SQL 文本；借用。
 * @return 新查询对象，调用者必须使用 XSqlQuery_delete_base 释放；失败或跨线程调用返回 NULL。
 */
XSqlQuery* XSqlDatabase_exec(const XSqlDatabase* database, const XString* query);
/**
 * @brief 使用 UTF-8 SQL 执行查询。
 * @param database 数据库句柄；不能为 NULL。
 * @param query UTF-8 SQL 字符串；借用。
 * @return 新查询对象，调用者必须使用 XSqlQuery_delete_base 释放；失败返回 NULL。
 */
XSqlQuery* XSqlDatabase_exec_utf8(const XSqlDatabase* database, const char* query);
/**
 * @brief 获取最近错误副本。
 * @param database 数据库句柄；NULL 返回未知错误对象。
 * @return 新错误对象，调用者必须使用 XSqlError_delete_base 释放。
 */
XSqlError* XSqlDatabase_lastError(const XSqlDatabase* database);
/**
 * @brief 判断数据库句柄是否有效。
 * @param database 数据库句柄；NULL 返回 false。
 * @return 句柄关联有效驱动返回 true，否则返回 false。
 */
bool XSqlDatabase_isValid(const XSqlDatabase* database);
/**
 * @brief 开始数据库事务。
 * @param database 数据库句柄；不能为 NULL，必须由所属线程调用。
 * @return 成功返回 true；驱动不支持、连接未打开或执行失败返回 false，并更新最近错误。
 */
bool XSqlDatabase_transaction(XSqlDatabase* database);
/**
 * @brief 提交当前数据库事务。
 * @param database 数据库句柄；不能为 NULL，必须由所属线程调用。
 * @return 成功返回 true；没有活动事务、连接未打开或执行失败返回 false，并更新最近错误。
 */
bool XSqlDatabase_commit(XSqlDatabase* database);
/**
 * @brief 回滚当前数据库事务。
 * @param database 数据库句柄；不能为 NULL，必须由所属线程调用。
 * @return 成功返回 true；没有活动事务、连接未打开或执行失败返回 false，并更新最近错误。
 */
bool XSqlDatabase_rollback(XSqlDatabase* database);
/**
 * @brief 设置下次打开连接使用的数据库名。
 * @param database 数据库句柄；不能为 NULL。
 * @param name 数据库名；借用并深复制，可为 NULL 以清空设置。
 * @return 无；已打开连接不会重新选择数据库。
 */
void XSqlDatabase_setDatabaseName(XSqlDatabase* database, const XString* name);
/**
 * @brief 设置下次打开连接使用的用户名。
 * @param database 数据库句柄；不能为 NULL。
 * @param name 用户名；借用并深复制，可为 NULL 以清空设置。
 * @return 无；已打开连接不受影响。
 */
void XSqlDatabase_setUserName(XSqlDatabase* database, const XString* name);
/**
 * @brief 设置下次打开连接使用的密码。
 * @param database 数据库句柄；不能为 NULL。
 * @param password 密码；借用并深复制，可为 NULL 以清空设置。
 * @return 无；已打开连接不受影响，调用方应自行保护原始密码数据。
 */
void XSqlDatabase_setPassword(XSqlDatabase* database, const XString* password);
/**
 * @brief 设置下次打开连接使用的主机名或本地传输端点。
 * @param database 数据库句柄；不能为 NULL。
 * @param host 主机名；借用并深复制，可为 NULL 以使用驱动默认值。
 * @return 无；已打开连接不受影响。
 */
void XSqlDatabase_setHostName(XSqlDatabase* database, const XString* host);
/**
 * @brief 设置下次打开连接使用的端口。
 * @param database 数据库句柄；不能为 NULL。
 * @param port 端口；负值表示让驱动使用默认端口。
 * @return 无；已打开连接不受影响。
 */
void XSqlDatabase_setPort(XSqlDatabase* database, int port);
/**
 * @brief 设置下次打开连接使用的连接选项。
 * @param database 数据库句柄；不能为 NULL。
 * @param options 选项文本；借用并深复制，可为 NULL 以清空全部选项。
 * @return 无；已打开连接不受影响。
 */
void XSqlDatabase_setConnectOptions(XSqlDatabase* database, const XString* options);
/** @brief 获取数据库名副本。 @param database 数据库句柄；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlDatabase_databaseName(const XSqlDatabase* database);
/** @brief 获取用户名副本。 @param database 数据库句柄；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlDatabase_userName(const XSqlDatabase* database);
/** @brief 获取密码副本。 @param database 数据库句柄；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlDatabase_password(const XSqlDatabase* database);
/** @brief 获取主机名副本。 @param database 数据库句柄；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlDatabase_hostName(const XSqlDatabase* database);
/** @brief 获取驱动名副本。 @param database 数据库句柄；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlDatabase_driverName(const XSqlDatabase* database);
/** @brief 获取配置端口。 @param database 数据库句柄；可为 NULL。 @return 已设置端口；NULL 或未设置时返回 -1。 */
int XSqlDatabase_port(const XSqlDatabase* database);
/** @brief 获取连接选项副本。 @param database 数据库句柄；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlDatabase_connectOptions(const XSqlDatabase* database);
/** @brief 获取连接名副本。 @param database 数据库句柄；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlDatabase_connectionName(const XSqlDatabase* database);
/** @brief 设置数值精度策略。 @param database 数据库句柄；不能为 NULL。 @param policy 策略；影响以后创建的结果对象。 @return 无；已存在结果保持原策略。 */
void XSqlDatabase_setNumericalPrecisionPolicy(XSqlDatabase* database, XSqlNumericalPrecisionPolicy policy);
/** @brief 获取数值精度策略。 @param database 数据库句柄；可为 NULL。 @return 当前策略；NULL 时返回 HighPrecision。 */
XSqlNumericalPrecisionPolicy XSqlDatabase_numericalPrecisionPolicy(const XSqlDatabase* database);
/**
 * @brief 将连接关联线程。
 * @param database 数据库句柄；不能为 NULL。
 * @param targetThread 目标线程抽象指针；NULL 表示移除线程关联。
 * @return 移动成功返回 true，否则返回 false。
 */
bool XSqlDatabase_moveToThread(XSqlDatabase* database, XThread* targetThread);
/**
 * @brief 获取连接关联线程。
 * @param database 数据库句柄；NULL 返回 NULL。
 * @return 线程抽象借用指针；调用者不得释放。
 */
XThread* XSqlDatabase_thread(const XSqlDatabase* database);
/**
 * @brief 获取驱动借用指针。
 * @param database 数据库句柄；NULL 返回 NULL。
 * @return 当前线程可访问的内部驱动；跨线程调用返回 NULL，连接销毁后失效，调用者不得释放。
 */
XSqlDriver* XSqlDatabase_driver(const XSqlDatabase* database);

#ifdef __cplusplus
}
#endif

#endif /* XSQLDATABASE_H */
