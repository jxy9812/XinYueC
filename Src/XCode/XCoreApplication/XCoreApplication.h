#ifndef XCOREAPPLICATION_H
#define XCOREAPPLICATION_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XObject.h"
#include"XEventType.h"
#include"XEventLoop.h"
#include"XCommandLineParser.h"
#include"XCommandLineOptionGroup.h"

/**
 * @brief 核心应用程序类
 * 管理应用程序生命周期、事件循环和命令行解析
 */
typedef struct XCoreApplication
{
    XObject m_class;               // 父对象
    bool m_quit;                   // 是否退出标志
    int m_argc;                    // 命令行参数数量
    char** m_argv;                 // 命令行参数数组
    XEventLoop* m_eventLoop;       // 事件调度器
    XCommandLineParser* m_cmdParser; // 命令行解析器
} XCoreApplication;

/**
 * @brief 获取应用程序类的虚函数表
 * @return 虚函数表指针
 */
XVtable* XCoreApplication_class_init();

/**
 * @brief 获取全局应用程序实例
 * @return 全局唯一的应用程序实例
 */
XCoreApplication* XCoreApplication_global();

/**
 * @brief 创建应用程序实例
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 新创建的应用程序实例
 */
XCoreApplication* XCoreApplication_create(int argc, char** argv);

/**
 * @brief 初始化应用程序实例
 * @param app 应用程序实例
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 */
void XCoreApplication_init(XCoreApplication* app, int argc, char** argv);

/**
 * @brief 销毁应用程序实例
 * @param app 要销毁的应用程序实例，传NULL无操作
 */
void XCoreApplication_delete(XCoreApplication* app);

/**
 * @brief 获取事件分发器
 * @return 事件分发器指针，应用程序未初始化返回NULL
 */
XEventDispatcher* XCoreApplication_getDispatcher();

/**
 * @brief 获取事件循环
 * @return 事件循环指针，应用程序未初始化返回NULL
 */
XEventLoop* XCoreApplication_getEventLoop();

/**
 * @brief 获取定时器组
 * @return 定时器组指针，应用程序未初始化返回NULL
 */
XTimerGroupBase* XCoreApplication_getTimerGroup();

/**
 * @brief 退出应用程序
 */
void XCoreApplication_quit();

/**
 * @brief 处理待处理事件
 * @param flags 事件处理标志
 */
void XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags);

/**
 * @brief 启动应用程序事件循环
 * @return 退出码
 */
int XCoreApplication_exec();

/**
 * @brief 发送信号到事件循环
 * @param sendFunc 信号发送函数
 * @param signalSlot 信号槽
 * @param signal 信号ID
 * @param args 信号参数
 * @param ref_count 引用计数
 * @param priority 事件优先级
 * @return 成功返回true，失败返回false
 */
bool XCoreApplication_postSendSignal(void(*sendFunc)(XSignalSlot*, size_t, void*),
    XSignalSlot* signalSlot, size_t signal, void* args,
    XAtomic_int32_t* ref_count, XEventPriority priority);

/**
 * @brief 向事件循环提交函数执行
 * @param receiver 接收者对象
 * @param func 要执行的函数
 * @param args 函数参数
 * @param priority 事件优先级
 * @return 成功返回true，失败返回false
 */
bool XCoreApplication_postFunc(XObject* receiver, void(*func)(void*), void* args, XEventPriority priority);

/**
 * @brief 添加文件描述符到事件循环
 * @param object 关联的对象
 * @param fd 文件描述符
 * @param events 关注的事件类型
 * @return 成功返回true，失败返回false
 */
bool XCoreApplication_addFd(XObject* object, int fd, XEventType events);

/**
 * @brief 从事件循环移除文件描述符
 * @param fd 文件描述符
 * @return 成功返回true，失败返回false
 */
bool XCoreApplication_removeFd(int fd);

/**
 * @brief 获取命令行解析器
 * @param app 应用程序实例
 * @return 命令行解析器指针，应用程序为NULL返回NULL
 */
XCommandLineParser* XCoreApplication_getCommandLineParser(XCoreApplication* app);

/**
 * @brief 向应用程序添加命令行选项
 * @param app 应用程序实例
 * @param shortName 短选项名
 * @param longName 长选项名
 * @param description 选项描述
 * @param requiresValue 是否需要参数值
 * @param isHidden 是否隐藏选项
 * @param defaultValue 默认值
 */
void XCoreApplication_addCommandLineOption(XCoreApplication* app,
    const char* shortName,
    const char* longName,
    const char* description,
    bool requiresValue,
    bool isHidden,
    const char* defaultValue);

/**
 * @brief 向应用程序添加选项组
 * @param app 应用程序实例
 * @param group 选项组
 */
void XCoreApplication_addOptionGroup(XCoreApplication* app, XCommandLineOptionGroup* group);

/**
 * @brief 解析命令行参数
 * @param app 应用程序实例
 * @return 解析成功返回true，否则返回false
 */
bool XCoreApplication_parseCommandLine(XCoreApplication* app);

/**
 * @brief 检查是否存在指定选项
 * @param app 应用程序实例
 * @param option 选项名
 * @return 存在返回true，否则返回false
 */
bool XCoreApplication_hasOption(XCoreApplication* app, const char* option);

/**
 * @brief 获取选项值
 * @param app 应用程序实例
 * @param option 选项名
 * @return 选项值，不存在返回NULL
 */
const char* XCoreApplication_getOptionValue(XCoreApplication* app, const char* option);

/**
 * @brief 获取选项出现次数
 * @param app 应用程序实例
 * @param option 选项名
 * @return 出现次数
 */
int XCoreApplication_optionCount(XCoreApplication* app, const char* option);

/**
 * @brief 获取位置参数列表
 * @param app 应用程序实例
 * @return 位置参数向量
 */
XVector* XCoreApplication_positionalArguments(XCoreApplication* app);

/**
 * @brief 获取互斥组冲突列表
 * @param app 应用程序实例
 * @return 冲突选项向量
 */
XVector* XCoreApplication_exclusiveGroupConflicts(XCoreApplication* app);

/**
 * @brief 设置应用程序描述
 * @param app 应用程序实例
 * @param description 描述文本
 */
void XCoreApplication_setApplicationDescription(XCoreApplication* app, const char* description);

/**
 * @brief 打印帮助信息并退出
 * @param app 应用程序实例
 * @param description 应用程序描述
 */
void XCoreApplication_printHelpAndExit(XCoreApplication* app, const char* description);

/**
 * @brief 打印版本信息并退出
 * @param app 应用程序实例
 * @param version 版本字符串
 */
void XCoreApplication_printVersionAndExit(XCoreApplication* app, const char* version);

/**
 * @brief 获取即将退出的信号
 * @param app 应用程序实例
 * @return 信号指针
 */
void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app);

#ifdef __cplusplus
}
#endif
#endif
