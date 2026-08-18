# XPlatform 平台抽象接口

XPlatform 用于存放 XinYueC 跨平台公共代码依赖的抽象契约。这里描述能力、类型、枚举和后端接入规则，不放置具体操作系统实现、具体数据库客户端实现或第三方库实现。

当前 XPlatform 的重点是数据库抽象层。公共 SQL 类保持数据库无关，具体数据库通过抽象驱动和结果集接口接入。SQLite 已经以源码组件接入 XinYueC，其他后端仍按同一契约逐个实现。这样同一套公共 API 可以在桌面系统、服务器系统和嵌入式系统中复用，后端可以按构建目标静态选择。

GPIO、ADC 和 PWM 也使用同样的纯函数式平台契约。`XGpio` 已提供 GPIO
控制器、输入输出和中断接口；`XAdc` 提供逻辑 ADC 通道、原始值/毫伏值读取
和采样配置；`XPwm` 提供逻辑 PWM 通道、频率、千分比占空比和启停控制。
三者的公共头文件不包含平台 API，未接入具体硬件时由 Drive 下的 unsupported
存根返回 `Unsupported`，测试构建可用固定行为的 mock 后端验证 Shell 和资源
生命周期。仓库中旧的 `XPWMDeviceBase` 仍用于 XIODevice/虚表兼容类；面向
Shell 和新平台驱动的独立通道资源应优先使用 `XPwm`。

## 设计目标

- 公共数据库类只依赖 XinYueC 自身类型，不依赖操作系统 API。
- 公共数据库类不直接包含 SQLite、MySQL、PostgreSQL、ODBC 等客户端头文件。
- 支持多个数据库后端使用相同的数据库、查询、记录和模型 API。
- 支持源码级适配和静态链接，不要求动态插件加载。
- 允许嵌入式构建只编译所需数据库后端，减少代码体积和外部依赖。
- 后端差异集中在驱动、结果集、SQL 方言和元数据适配层。
- 使用 XinYueC 的 XMemory、XString、XVariant、XClass、XObject 和 vtable 约定管理对象与内存。

## 目录边界

### Src/XPlatform

Src/XPlatform 只保存跨平台抽象接口和与平台无关的基础契约，包括：

- 公共能力枚举和数据库类型枚举。
- 数据库驱动抽象。
- 查询结果抽象。
- 驱动创建器和静态注册所需的工厂抽象。
- 后端实现必须遵守的生命周期、错误、所有权和线程约束。

Src/XPlatform 不应该包含：

- Windows、Linux、macOS、Android 或其他系统的具体 API 调用。
- 具体数据库客户端句柄和客户端头文件。
- ODBC、OCI、libpq、SQLite C API 等第三方库实现。
- 动态库加载、平台插件加载或平台相关线程封装。
- 只服务于某个数据库的 SQL 方言代码。

### Src/XCode

Src/XCode 放置面向库用户的公共数据库类和高层行为实现。当前包括：

- XSqlDatabase：数据库连接管理和驱动选择。
- XSqlDriver：公共驱动抽象，定义在 Src/XPlatform/XSql。
- XSqlError：错误信息和值语义。
- XSqlField：字段定义和值。
- XSqlRecord：记录和字段集合。
- XSqlIndex：索引描述。
- XSqlQuery：查询、绑定、执行和结果遍历。
- XSqlQueryModel：查询结果模型。
- XSqlTableModel：表模型和编辑策略。
- XSqlRelation：关系字段描述。
- XSqlRelationalTableModel：带关系字段的表模型。
- XSqlRelationalDelegate：关系模型的编辑委托适配层。

其中高层类只通过 Src/XPlatform/XSql 中的抽象契约访问后端，不应该根据数据库类型直接调用客户端 API。具体源码后端，例如 SQLite，放在 Src/XCode/XSql；第三方数据库源码组件放在 Library 下。

## SQL 分层

SQL 层的数据流如下：

应用代码

→ XSqlDatabase、XSqlQuery、XSql*Model

→ XSqlDriver

→ XSqlResult

→ 具体数据库后端

具体数据库后端

→ 数据库客户端库、源码集成实现或嵌入式数据库内核

这一层次关系的关键点是：

- XSqlDatabase 负责选择和持有连接对应的驱动。
- XSqlDriver 负责连接生命周期、数据库能力、事务、元数据和结果对象创建。
- XSqlResult 负责语句准备、执行、绑定、结果读取、游标移动和影响行数。
- XSqlQuery 负责把公共查询 API 转换为驱动和结果对象的调用。
- 模型类只依赖 XSqlQuery、XSqlRecord 和 XSqlDatabase，不依赖具体数据库。

后端实现只需要替换最后一层驱动和结果对象，公共查询及模型代码不需要为每种数据库复制一份。

## 对象层级和事件

当前对象继承关系按照 Qt 的实际职责划分，而不是让所有类都继承 XObject。

### 继承 XObject 的类

- XSqlDriver：对应 Qt 的 QSqlDriver，需要生命周期、驱动通知信号以及受保护的虚函数扩展点。
- XSqlDriverPlugin：保留 Qt 风格的驱动创建器对象抽象，当前用于源码级工厂兼容，不要求动态加载。
- XSqlQueryModel：对应 Qt 的 QAbstractTableModel，需要模型重置、数据变化等信号。
- XSqlTableModel：继承 XSqlQueryModel，需要插入、更新和删除前的信号。
- XSqlRelationalTableModel：继承 XSqlTableModel，增加关系模型行为。
- XSqlRelationalDelegate：对应 Qt 的委托对象职责，保留对象生命周期和后续事件集成空间。

### 继承 XClass 的类

- XSqlDatabase：连接句柄和值语义，不需要事件循环。
- XSqlError：错误值对象。
- XSqlField：字段值和字段定义对象。
- XSqlRecord：记录值对象。
- XSqlIndex：索引描述值对象。
- XSqlQuery：查询句柄和值语义对象。
- XSqlRelation：关系描述值对象。
- XSqlResult：Qt 中不是 QObject，保持 XClass，并通过 vtable 提供后端结果操作。
- XSqlDriverCreatorBase：工厂契约，不负责事件循环。

对象层级的判断原则是：只有需要信号、槽、对象生命周期或模型事件的类才继承 XObject。值对象、句柄对象和后端操作对象不因为存在虚函数就自动继承 XObject。

XObject 派生类不提供 create_copy、create_move 这类值类型复制接口；XClass 派生的值对象按库中已有的初始化、复制、移动和销毁约定处理。

## 后端适配结构

一个新的数据库后端通常由两个结构体和一个创建器组成：

1. 后端驱动结构体，首成员为 XSqlDriver。
2. 后端结果结构体，首成员为 XSqlResult。
3. 后端驱动创建器，用于静态注册和创建驱动实例。

驱动初始化时，需要使用 XSqlDriver_init 设置：

- 驱动对象的 vtable。
- 驱动实现类型。
- 实际数据库类型。

结果对象初始化时，需要使用 XSqlResult_init 设置结果对象的 vtable、所属驱动和查询状态。

### 驱动操作

后端驱动至少应该实现以下核心操作：

- hasFeature：报告事务、批量执行、预处理、主键、查询大小、通知等能力。
- open：建立数据库连接。
- close：关闭数据库连接并释放后端句柄。
- createResult：创建与该驱动匹配的结果对象。

正式后端通常还需要实现：

- beginTransaction、commitTransaction、rollbackTransaction。
- tables、primaryIndex、record。
- formatValue、escapeIdentifier。
- sqlStatement。
- handle、cancelQuery、maximumIdentifierLength。
- notification、subscribeToNotification、unsubscribeFromNotification 等通知相关操作。

驱动层不应该把第三方客户端句柄暴露到公共头文件。后端可以在自己的结构体中保存客户端句柄，并通过 XSqlDriver_handle_base 提供受控的借用访问。

### 结果操作

后端结果对象至少应该实现以下核心操作：

- data：读取当前行指定列的数据。
- isNull：判断当前列是否为 NULL。
- reset：重置或执行查询。
- fetch：移动到指定行。
- fetchFirst：移动到第一行。
- fetchLast：移动到最后一行。
- size：报告结果集大小，数据库不支持时返回未知状态。
- numRowsAffected：报告受影响行数。

常见后端还需要实现：

- fetchNext、fetchPrevious、fetchNextResult。
- record、lastInsertId。
- prepare、exec、bindValue、addBindValue、execBatch。
- handle。

XSqlDriver 和 XSqlResult 的 vtable 提供默认行为时，只代表公共层可以安全调用，不代表后端已经具备可用的数据库能力。生产后端必须明确实现其数据库客户端所支持的核心操作，并在能力枚举中准确报告不支持的功能。

## 数据库类型和驱动类型

驱动类型和数据库类型是两个不同维度，不能混用。

### XSqlDriverType

XSqlDriverType 描述连接如何实现或通过什么通道访问数据库：

| 枚举 | 用途 |
| --- | --- |
| Unknown | 未知或尚未选择的驱动 |
| MsSqlServer | SQL Server 专用驱动 |
| MySql | MySQL 或 MariaDB 专用驱动 |
| PostgreSql | PostgreSQL 专用驱动 |
| Oracle | Oracle 专用驱动 |
| Sybase | Sybase 专用驱动 |
| Sqlite | SQLite 专用驱动 |
| Interbase | InterBase 或 Firebird 兼容驱动 |
| Db2 | IBM Db2 专用驱动 |
| MimerSql | Mimer SQL 专用驱动 |
| Odbc | 通过 ODBC 访问数据库 |
| Embedded | 嵌入式数据库或设备内置数据库通道 |
| Custom | 用户自定义数据库后端 |

### XSqlDbmsType

XSqlDbmsType 描述实际数据库产品或数据库系统：

| 枚举 | 用途 |
| --- | --- |
| Unknown | 未知数据库 |
| MsSqlServer | Microsoft SQL Server |
| MySql | MySQL 或 MariaDB |
| PostgreSql | PostgreSQL |
| Oracle | Oracle Database |
| Sybase | Sybase |
| Sqlite | SQLite |
| Interbase | InterBase 或 Firebird 兼容数据库 |
| Db2 | IBM Db2 |
| MimerSql | Mimer SQL |

例如，通过 ODBC 访问 SQL Server 时可以使用：

- driverType：XSqlDriverType_Odbc。
- dbmsType：XSqlDbmsType_MsSqlServer。

通过自定义网络协议访问某个内部数据库时可以使用：

- driverType：XSqlDriverType_Custom。
- dbmsType：实际数据库类型，或者 XSqlDbmsType_Unknown。

ODBC、Embedded 和 Custom 是实现路径，不是具体数据库产品，因此不在 XSqlDbmsType 中重复定义。

## 数据库适配状态

当前枚举和抽象接口已经为以下目标预留：

- SQLite。
- MySQL、MariaDB。
- PostgreSQL。
- SQL Server。
- Oracle。
- Sybase。
- InterBase、Firebird 兼容数据库。
- IBM Db2。
- Mimer SQL。
- ODBC。
- 用户自定义数据库和嵌入式数据库。

当前 SQLite 已经有源码后端，其余数据库仍处于抽象契约阶段，不应把枚举存在误认为对应数据库已经可用：

| 后端 | 当前状态 |
| --- | --- |
| SQLite | 已实现源码驱动、源码结果集、静态内置注册和集成测试 |
| MySQL/MariaDB | 已实现源码协议客户端、驱动、结果集和静态注册；官方客户端源码可通过 XSqlMySqlClientApi 替换 |
| PostgreSQL | 已定义适配目标，生产驱动尚未实现 |
| SQL Server | 已定义适配目标，生产驱动尚未实现 |
| Oracle | 已定义适配目标，生产驱动尚未实现 |
| ODBC | 已定义适配通道，生产驱动尚未实现 |
| Custom | Test/XDataTest/XSqlTest.c 中已有内存测试驱动 |

嵌入式优先建议先使用 SQLite 源码后端。当前 `Library/sqlite` 编译 SQLite 3.49.1 amalgamation，并通过 `sqlite3_xin_memory.c` 将 SQLite 初始化前的内存申请接到 XMemory。文件型数据库统一使用 `xin_xfile` VFS，VFS 直接调用 XFile 模块的 `XDeviceFile_*` 抽象接口；桌面、FatFs 或其他嵌入式文件系统只需要替换 XFile 后端。`:memory:` 数据库仍然使用 SQLite 内置内存路径。XinYueC 的 XSqliteDriver 不需要修改。

`XSqlTableModel` 维护新增、修改和已删除行的独立状态；提交时以修改前的主键值生成 `WHERE` 条件，分别执行插入、更新和删除。`OnFieldChange` 会立即写回既有行的字段或整行修改，并立即删除既有行；新行仍等待完整编辑后由 `submit()` 或 `submitAll()` 写入。`XSqlQueryModel` 支持对已缓存结果列插入和删除，这些列只影响模型缓存，不会改写原始查询或数据库。结果集和模型仍一次性缓存，因此 `fetchMore()` 不是流式加载接口。

## 静态注册和源码适配

本层不要求动态插件机制。XSqlDriverPlugin 保留 Qt 风格的工厂抽象，主要用于统一创建器接口和保持 API 形态，不代表必须使用动态库扫描或运行时插件加载。

推荐的注册流程如下：

1. 在后端目录定义驱动结构体、结果结构体和创建器。
2. 使用 XSqlDriverCreatorMethod 或对应创建器接口提供驱动创建方法。
3. 对自定义后端在程序初始化阶段调用 XSqlDatabase_registerSqlDriver 或按类型注册接口；SQLite 由 XSqlDatabase_addDatabase 首次使用时自动完成内置源码注册。
4. 应用通过 XSqlDatabase_addDatabase 或 XSqlDatabase_addDatabase_utf8 选择连接。
5. 连接创建后，公共 XSqlQuery 和模型类不再关心具体后端。

静态注册的优点：

- 只链接目标产品需要的后端。
- 不依赖 dlopen、LoadLibrary、QPluginLoader 等平台或插件 API。
- 适合固件、RTOS 适配层和资源受限的嵌入式程序。
- 编译期可以去掉不需要的数据库客户端依赖。
- 可以让后端和应用使用相同的编译选项、内存分配器和异常策略。

创建器的生命周期必须覆盖数据库连接创建过程。注册表保存的创建器应当是稳定对象；重复的驱动名称或类型按 Qt 语义替换旧创建器，调用方不能继续使用已被替换的创建器注册对象。

## 各类数据库的适配重点

### SQLite

- 使用 XSqlDriverType_Sqlite 和 XSqlDbmsType_Sqlite。
- 具体实现为 `Src/XCode/XSql/XSqliteDriver.c` 和 `XSqliteDriver.h`，结果对象在同一适配单元中实现，公共头文件不暴露 `sqlite3.h`。
- 已实现单连接生命周期、预处理、位置和命名绑定、事务、NULL、BLOB、最后插入行 ID、缓存游标和表/字段/主键元数据。
- 结果集使用 XMemory 缓存行数据，因此支持 `first/last/previous/seek` 和模型一次性加载；大结果集的内存成本需要由产品评估。
- 已实现 SQLite 能力枚举、标识符双引号转义、事件通知和 SQLite 错误码转换；`CancelQuery` 按 Qt QSQLITE 语义保持不支持并返回 `false`。
- `XSqlDatabase_addDatabase(XSqlDriverType_Sqlite, name)` 会自动注册内置源码创建器，不需要动态插件。
- `Library/sqlite/sqlite3.c` 和 `sqlite3.h` 是可替换的源码组件；当前来自 Qt 6.8.3 自带的 SQLite 3.49.1。
- `Library/sqlite/sqlite3_xin_memory.c` 使用带对齐头的 XMemory 包装器提供 SQLite allocator，避免适配层直接使用 C 分配函数。
- `Library/sqlite/sqlite3_xin_vfs.c` 实现 `xin_xfile` VFS，文件打开、删除、定位、读写、扩容、刷新和大小查询均通过 XFile 的 `XDeviceFile_*` 抽象接口。
- 当前 VFS 同时支持 SQLite rollback journal 和 WAL 共享映射；WAL 的 `-shm` 区域通过 `XDeviceFile_map`/`XDeviceFile_unmap` 管理。VFS 使用 XMutex 保护按数据库路径组织的锁组，并为 SQLite 的 8 个 WAL 锁槽分别使用 XReadWriteLock；XFile 尚未提供跨进程锁契约，产品需要多进程并发时应先扩展 XFile 抽象接口。

### MySQL 和 MariaDB

- 使用 XSqlDriverType_MySql 和 XSqlDbmsType_MySql。
- `Src/XPlatform/XSql/XSqlMySqlClient.h` 定义不包含 `mysql.h` 的客户端函数表；驱动只依赖这个抽象接口。
- 默认实现为 `Src/XCode/XSql/XMySqlWireClient.c`，使用 `XSslSocket`（未启用时按普通 TCP 工作）、`XCryptographicHash`、`XByteArray` 和 `XMemory` 实现 MySQL 文本及二进制协议。
- `XMySqlWireClient.c` 只调用 `XSsl_platform.h` 的抽象接口，不再包含任何平台专属传输模块；MySQL 共享内存传输由通用代码 `Src/XCode/XSql/XMySqlSharedMemory.c` 实现，不包含平台头文件：命名共享内存段的打开/映射/解除映射统一走 `XDeviceFile_openSharedMemory`/`XDeviceFile_map`/`XDeviceFile_unmap`/`XDeviceFile_close` 三个共享内存原语，平台 `XDeviceFile_openSharedMemory` 会为每个段内建一个同名信令通道（POSIX 为 Unix domain 流式套接字，Windows 为命名管道），传输层在该通道上做异步接收（阻塞等待通知字节，参考网络套接字异步接收，不做共享内存状态轮询），因此 Windows 与 POSIX（Linux/macOS/BSD）行为一致，Linux 同样支持本机共享内存连接。RSA 公钥加密由 SSL 后端实现，不得把 `windows.h`、`HANDLE`、mbedTLS 或 PSA API 带入协议层。
- `Src/XCode/XSql/XMySqlDriver.c` 负责连接、事务、文本占位符转义、结果集缓存、字段类型转换、最后插入 ID 和元数据。
- 对照 Qt 6.8 `qsql_mysql.cpp`，公共驱动接口已经覆盖 `open/close/createResult/tables/primaryIndex/record/formatValue/handle/escapeIdentifier`、事务、数值精度策略和 `XSqlResult::nextResult`；`XSqlDatabase_moveToThread/thread` 也已接入 XinYueC 的 `XObject` 线程亲和性。
- Qt QMYSQL 的能力位语义保持一致：`MultipleResultSets`、查询大小、BLOB、Unicode、最后插入 ID 和预处理查询可用；`NamedPlaceholders`、`BatchOperations`、`FinishQuery`、`CancelQuery` 不在驱动能力位中伪造为原生支持。命名绑定和 `execBatch(ValuesAsRows)` 仍由公共结果层按 Qt 的兼容回退规则完成。
- `XMySqlDriver_setClientApi` 可在创建连接前接入官方 MySQL `libmysql`/Connector/C 或 MariaDB Connector/C 的源码适配层；第三方头文件和客户端句柄只能留在适配实现内，不能进入 XPlatform 或公共 XSql API。
- 当前默认协议实现支持 MySQL 文本查询、TLS 升级、zlib 压缩协议、`mysql_native_password`、`caching_sha2_password` 快速认证及 TLS 内完整认证、无 TLS RSA 公钥回退、事务、NULL、整数、浮点、文本、BLOB、`DATE`/`DATETIME`/`TIMESTAMP` 类型值、按 Qt 规则返回字符串的 `TIME`、结果集随机读取和基础元数据。
- 已补齐服务端 `COM_STMT_PREPARE`/`COM_STMT_EXECUTE` 二进制预处理、默认开启的多语句多结果集（可用连接选项 `CLIENT_MULTI_STATEMENTS=0` 或 `MULTI_STATEMENTS=0` 关闭）、`ValuesAsRows`/`ValuesAsColumns` 的 Qt 逐行批量回退、结果集 `nextResult()` 和 DECIMAL 高精度字符串保持。批量执行采用 Qt `QSqlResult::execBatch()` 的逐行回退语义，因此 `XSqlDriverFeature_BatchOperations` 仍返回 false；取消查询同样保持 Qt QMYSQL 的 false，不通过关闭连接伪装成可复用的取消。
- QMYSQL 的公开类型映射和字段元数据已对齐：MySQL 高级类型按 Qt 规则收敛为数值、字符串、字节数组、日期或日期时间；`record()` 保留字段名、来源表、required、length、precision、autoValue 和默认值。MySQL 存储过程输出值沿用 QMYSQL 的 SQL 用户变量方式，例如 `CALL proc(@out); SELECT @out`，不伪造客户端输出参数绑定。连接选项已对齐 `SSL_MODE`/`MYSQL_OPT_SSL_MODE`、SSL 证书/密钥/CA/CAPATH/CIPHER/CRL/CRLPATH、TLS 版本、连接/读写超时、`MYSQL_OPT_RECONNECT`、`MYSQL_OPT_LOCAL_INFILE`、`MYSQL_OPT_PROTOCOL`、POSIX `UNIX_SOCKET` 和常用 `CLIENT_*` 标志。未知 `MYSQL_OPT_PROTOCOL` 值按 Qt/libmysql 语义回落到默认协议。`MYSQL_OPT_RECONNECT=1` 只在通信错误后重新认证并重试当前操作，不重放服务端已经返回 SQL 错误的语句。
- POSIX 上空主机名配合默认协议使用 `/run/mysqld/mysqld.sock`，`MYSQL_OPT_PROTOCOL=TCP` 强制 TCP，`SOCKET` 使用该默认路径，也可用 `UNIX_SOCKET` 覆盖；Windows 等不具备 Unix domain socket 的平台明确返回连接错误。Windows 上 `MYSQL_OPT_PROTOCOL=PIPE` 使用 IOCP named pipe 流，默认 `\\.\\pipe\\MySQL`，可通过 `UNIX_SOCKET` 指定自定义管道名；空协议或显式 `DEFAULT` 配合主机名 `.` 同样选择默认管道。
- `MYSQL_OPT_PROTOCOL=MEMORY` 走 `XMySqlSharedMemory` 传输：客户端与服务器通过命名共享内存段通信（连接协商段 `<BASE>_CONNECT_DATA` 完成握手，数据段 `<BASE>_<编号>_DATA` 含服务器→客户端与客户端→服务器两条 16000 字节通道，与官方 MySQL 16KB 分块帧对齐），段内原子字段仅作防御性标记，真正的同步由平台内建信令通道完成（数据方写完一块写 `'D'` 通知，对端读完后写 `'S'` 释放空间，关闭写 `'C'`，全部阻塞等待、无轮询）。`MYSQL_SHARED_MEMORY_BASE_NAME` 指定基础段名（默认 `MYSQL`）。`PIPE` 是 Windows 本地传输，不进行 TLS，`SSL_MODE=REQUIRED`、`VERIFY_CA` 或 `VERIFY_IDENTITY` 会明确拒绝；非 Windows 选择 `PIPE` 返回平台限制错误。`SSL_CIPHER` 支持 mbedTLS 名称及常用 OpenSSL/libmysql 名称，未知密码套件会在 TLS 握手前报错；`SSL_CAPATH` 由 mbedTLS 加载 CA 目录，`SSL_CRL` 加载 CRL 文件，`SSL_CRLPATH` 遍历 CRL 目录；`CLIENT_COMPRESS` 使用 zlib 压缩协议并支持压缩包内多个普通 MySQL 包；本地文件读取只有显式设置 `MYSQL_OPT_LOCAL_INFILE=1` 才启用。连接成功后会按 Qt 6.8 的规则探测 `COM_STMT_PREPARE`，并据服务端事务能力位动态返回 `hasFeature`；支持预处理时设置会话时区为 UTC。连接按 Qt 的线程亲和性规则拒绝跨线程共享，结果集按 Qt QMYSQL 的 `mysql_store_result()` 语义一次性缓存，不把流式读取伪造为驱动能力位。
- Windows 验收待办：需在 Windows 上启动启用 named pipe 的 MySQL 服务，使用 `XMYSQL_TEST_OPTIONS=MYSQL_OPT_PROTOCOL=PIPE;SSL_MODE=DISABLED` 运行 `XSqlMySqlTest`，并覆盖默认和自定义 `UNIX_SOCKET` 管道名、连接超时、认证、预处理、压缩、多结果集及断开清理。当前环境没有 Windows 编译器或 Windows MySQL 服务，因此上述路径只有源码和 Linux 非支持路径验证，尚无 Windows 实机证据。
- Windows `XNETWORK_USE_LWIP` 后端当前仅负责 lwIP/Npcap 网络接口，`XDeviceNetwork_socketConnectLocal()` 不实现 Windows named pipe；因此该后端的 `MYSQL_OPT_PROTOCOL=PIPE` 仍待增加宿主 Win32 本地流适配。Unix domain socket 与 Windows named pipe 都不是 lwIP TCP/IP 协议栈功能，不能用 loopback TCP 代替。
- MySQL/MariaDB 是网络数据库，不使用 SQLite 的 XFile VFS；嵌入式移植需要提供 XinYueC 网络抽象，数据库文件仍由服务器端管理。
- 元数据和 SQL 方言不要泄漏到 XSqlQueryModel 或 XSqlTableModel。

MySQL 真实服务器联调测试位于 `Test/XDataTest/XSqlMySqlTest.c`。测试不把账号密码写入源码，使用以下环境变量：

- `XMYSQL_TEST_HOST`：服务器地址，默认 `127.0.0.1`。
- `XMYSQL_TEST_PORT`：服务器端口，默认 `3306`。
- `XMYSQL_TEST_DATABASE`：测试数据库；未设置时自动创建并在测试结束后删除 `xin_sql_mysql_test_database`，已设置时使用指定数据库。
- `XMYSQL_TEST_USER`：测试账号；未设置时跳过真实联调。
- `XMYSQL_TEST_PASSWORD`：测试密码，默认空字符串。
- `XMYSQL_TEST_TABLE`：测试表名，默认 `xin_sql_mysql_test`。
- `XMYSQL_TEST_RSA`：设置为非空时，若服务器安装 `caching_sha2_password` 插件，则额外创建临时账号验证无 TLS RSA 公钥认证；服务器未安装插件时跳过该项。
- `XMYSQL_TEST_OPTIONS`：传给驱动的分号分隔连接选项，可用于联调 `SSL_MODE`、`CLIENT_COMPRESS=1`、`MYSQL_OPT_RECONNECT=1`、`MYSQL_OPT_LOCAL_INFILE=1`、`CLIENT_MULTI_STATEMENTS=0`、`CLIENT_ODBC=1`、`MYSQL_OPT_PROTOCOL=TCP|PIPE|MEMORY`、`MYSQL_SHARED_MEMORY_BASE_NAME` 等行为。

联调会创建并删除测试表，覆盖驱动注册、连接、建表、插入、位置和命名绑定、Unicode、NULL、BLOB、更新、删除、影响行数、最后插入 ID、游标移动、结束结果集、事务提交和回滚、表/字段/主键元数据、查询模型列缓存、表模型的插入/更新/删除写回、关系模型和关系委托编辑模型；启用对应连接选项后还可覆盖 TLS、多语句多结果集、服务端二进制预处理和 `LOAD DATA LOCAL INFILE`。

### PostgreSQL

- 使用 XSqlDriverType_PostgreSql 和 XSqlDbmsType_PostgreSql。
- 处理服务端预处理、结果状态、NULL、事务和通知。
- 处理多结果、返回结果集和 PostgreSQL 类型映射。
- LISTEN/NOTIFY 只通过 XSqlDriver 的通知抽象暴露。

### SQL Server

- 可以使用 XSqlDriverType_MsSqlServer 配合 XSqlDbmsType_MsSqlServer。
- 也可以使用 XSqlDriverType_Odbc 配合 XSqlDbmsType_MsSqlServer。
- 两种方式的连接、参数、错误和元数据差异应留在后端，不应进入公共模型类。

### Oracle、Sybase、Db2、Mimer SQL

- 驱动类型使用对应的专用枚举。
- 重点适配参数绑定、事务隔离、LOB、日期时间、数值精度和元数据。
- 不支持的能力必须通过 hasFeature 准确返回 false。
- 不能为了让公共 API 看起来完整而伪造后端能力。

### ODBC

- 使用 XSqlDriverType_Odbc。
- 使用 XSqlDbmsType 表示已识别的实际数据库；无法识别时使用 Unknown。
- 通过 ODBC 驱动管理器完成连接，但 ODBC 句柄只保存在后端实现中。
- SQL 方言、参数标记和元数据行为可能由具体 ODBC 驱动决定，适配代码需要保留能力检测。

## 内存、错误和所有权规则

- 公共层使用 XinYueC 的内存分配器，不直接调用 malloc、calloc、realloc、free 或 strdup。
- 后端客户端句柄由后端结构体持有，驱动关闭时按客户端库要求释放。
- XSqlDriver_handle_base 和 XSqlResult_handle_base 返回借用句柄，不转移所有权。
- 后端错误统一通过 XSqlDriver_setLastError 或结果对象的错误路径转换为 XSqlError。
- 公共 API 返回的新 XSqlError、XSqlRecord、XSqlField、XSqlIndex 等对象，调用者应按照对应的 delete_base 接口释放。
- NULL 必须与空字符串、零值和无效值区分，不能在后端转换时丢失 NULL 语义。
- 客户端库的字符串、错误对象和句柄不能直接泄漏到公共 API。
- 可选数据库客户端依赖只能出现在具体后端的构建目标中，不能污染公共 XPlatform 或 XCode 头文件。

## 线程和连接约束

XSqlDatabase 的连接、XSqlDriver 的后端句柄以及模型对象必须遵守其创建线程和所属对象的生命周期约束。后端不能默认允许同一个连接在多个线程之间共享，除非对应数据库客户端明确保证安全，并且驱动层已经实现同步和线程归属处理。

嵌入式后端如果使用单线程事件循环或设备专用资源，也应该在 XSqlDriver 的能力和错误路径中明确体现，而不是让公共模型类猜测这些限制。

## 当前实现位置

抽象接口位于：

- Src/XPlatform/XSql/XSqlGlobal.h
- Src/XPlatform/XSql/XSqlDriver.h
- Src/XPlatform/XSql/XSqlResult.h
- Src/XPlatform/XSql/XSqlDriverPlugin.h
- Src/XPlatform/XSql/XSqlMySqlClient.h

公共数据库类位于：

- Src/XCode/XSql/XSqlDatabase.h
- Src/XCode/XSql/XSqliteDriver.h
- Src/XCode/XSql/XSqliteDriver.c
- Src/XCode/XSql/XMySqlDriver.h
- Src/XCode/XSql/XMySqlDriver.c
- Src/XCode/XSql/XMySqlWireClient.c
- Src/XCode/XSql/XSqlError.h
- Src/XCode/XSql/XSqlField.h
- Src/XCode/XSql/XSqlRecord.h
- Src/XCode/XSql/XSqlIndex.h
- Src/XCode/XSql/XSqlQuery.h
- Src/XCode/XSql/XSqlQueryModel.h
- Src/XCode/XSql/XSqlTableModel.h
- Src/XCode/XSql/XSqlRelation.h
- Src/XCode/XSql/XSqlRelationalTableModel.h
- Src/XCode/XSql/XSqlRelationalDelegate.h

SQLite 源码组件位于：

- Library/sqlite/sqlite3.c
- Library/sqlite/sqlite3.h
- Library/sqlite/sqlite3_xin_memory.c
- Library/sqlite/sqlite3_xin_vfs.c
- Library/sqlite/CMakeLists.txt

XSqlDriver 的抽象头文件位于 XPlatform；`XSql.h` 通过公共包含路径向 XCode 用户提供它。具体数据库后端仍然应该只实现 XPlatform/XSql 中定义的驱动和结果契约。

## 后续实现顺序

建议按照以下顺序增加真实后端：

1. SQLite：补充 XFile 跨进程锁契约和大结果集策略。
2. MySQL/MariaDB：优先接入已选定许可证和版本的官方 C 客户端源码，验证 TLS、字符集、服务器错误、服务端预处理和认证插件。
3. PostgreSQL：验证多结果、通知和更完整的类型映射。
4. ODBC 或 SQL Server：根据实际部署环境决定优先级。
5. Oracle、Db2、Sybase、Mimer SQL：按照项目实际产品需求接入。

每个后端都需要单独测试打开和关闭连接、预处理、参数绑定、NULL、游标移动、事务、影响行数、错误转换、元数据、模型查询、提交、撤销和资源释放。抽象层测试通过不代表具体数据库后端已经通过。
