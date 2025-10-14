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
//
typedef struct XCoreApplication
{
    XObject m_class;//父对象
    bool m_quit;//是否退出
    int m_argc;
    char** m_argv;
    XEventLoop* m_eventLoop;//事件调度器
    XCommandLineParser* cmdParser;// 新增：命令行解析器
}XCoreApplication;//
XVtable* XCoreApplication_class_init();
XCoreApplication* XCoreApplication_global();
XCoreApplication* XCoreApplication_create(int argc, char** argv);
void XCoreApplication_init(XCoreApplication* app, int argc, char** argv);
void XCoreApplication_delete(XCoreApplication* app);
//获取事件调度器
XEventDispatcher* XCoreApplication_getDispatcher();
XEventLoop* XCoreApplication_getEventLoop();
XTimerGroupBase* XCoreApplication_getTimerGroup();
//请求退出
void XCoreApplication_quit();
void  XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags);
//进入事件循环
int XCoreApplication_exec();
/**
 * @brief 投递信号发送（异步处理）
 * @param sendSignalFunc 信号发送函数
 * @param signalSlot 信号槽
 * @param m_signal 信号
 * @param args 参数
 * @return 是否成功加入队列
 */
bool XCoreApplication_postSendSignal(void(*sendFunc)(XSignalSlot*, size_t, void*), XSignalSlot* signalSlot, size_t signal, void* args, XAtomic_int32_t* ref_count, XEventPriority priority);
//投递函数(异步投递)
bool XCoreApplication_postFunc(XObject* receiver, void(*func)(void*), void* args, XEventPriority priority);
bool XCoreApplication_addFd(XObject* object, int fd, XEventType events);
bool XCoreApplication_removeFd(int fd);
/**
 * @brief 获取命令行解析器
 * @return 解析器实例
 */
XCommandLineParser* XCoreApplication_getCommandLineParser(XCoreApplication* app);

/**
 * @brief 注册命令行选项
 * @param app 应用实例
 * @param shortName 短选项名（如 "c"）
 * @param longName 长选项名（如 "config"）
 * @param description 选项描述
 * @param requiresValue 是否需要参数值
 */
void XCoreApplication_addCommandLineOption(XCoreApplication* app,
    const char* shortName,
    const char* longName,
    const char* description,
    bool requiresValue);

/**
 * @brief 解析命令行参数
 * @param app 应用实例
 * @return 解析是否成功
 */
bool XCoreApplication_parseCommandLine(XCoreApplication* app);

/**
 * @brief 检查是否存在指定选项
 * @param app 应用实例
 * @param option 选项名（短选项不带 '-', 长选项不带 '--'）
 * @return 存在返回 true
 */
bool XCoreApplication_hasOption(XCoreApplication* app, const char* option);

/**
 * @brief 获取选项值
 * @param app 应用实例
 * @param option 选项名
 * @return 选项值（NULL 表示无此选项）
 */
const char* XCoreApplication_getOptionValue(XCoreApplication* app, const char* option);

/**
 * @brief 获取位置参数列表
 * @param app 应用实例
 * @return 位置参数向量（char* 类型）
 */
XVector* XCoreApplication_positionalArguments(XCoreApplication* app);

/**
 * @brief 打印帮助信息并退出
 * @param app 应用实例
 * @param description 程序描述
 */
void XCoreApplication_printHelpAndExit(XCoreApplication* app, const char* description);

/**
 * @brief 打印版本信息并退出
 * @param app 应用实例
 * @param version 版本字符串
 */
void XCoreApplication_printVersionAndExit(XCoreApplication* app, const char* version);
/*                                    信号                                        */
void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app);
#ifdef __cplusplus
}
#endif
#endif