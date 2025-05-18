#ifndef XCYLINDER_H
#define XCYLINDER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSwitchDevice.h"
typedef struct XCylinder XCylinder;
//气缸接口初始化
typedef struct XCylinder_PortInit
{
    XSwitchDevice_PortFuncInit sv;
    XSwitchDevice_PortFuncInit ul;
    XSwitchDevice_PortFuncInit dl;
}XCylinder_PortInit;
//气缸
typedef struct XCylinder
{
    XSwitchDevice* m_sv;//电磁阀
    XSwitchDevice* m_ul;//上限位
    XSwitchDevice* m_dl;//下限位
}XCylinder;
//获取一个气缸类
XCylinder* XCylinder_new(XCylinder_PortInit* port);
void XCylinder_free(XCylinder* cylinder);
//轮询扫描状态
void XCylinder_poll(XCylinder* cylinder);

#ifdef __cplusplus
}
#endif
#endif