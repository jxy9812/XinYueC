#ifndef XSTEPMOTOR_H
#define XSTEPMOTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#define  STM32F407_168M_StepMotor_TIM_1MHz_Prescaler (84-1)//STM32F407 主频168M步进电机定时器在1Mz的预分频系数
#include"XClass.h"
#include"XPWMDeviceBase.h"
#include"XSwitchDeviceBase.h"
#define XSTEPMOTOR_VTABLE_SIZE (XCLASS_VTABLE_SIZE+13)       //XStepMotor虚函数表大小
//XStepMotor虚函数表枚举
enum XStepMotorVtableEnum
{
	EXStepMotor_IsOpen=XCLASS_VTABLE_SIZE,
	EXStepMotor_Open,
	EXStepMotor_IsRunning,
	EXStepMotor_Close,
	EXStepMotor_Poll,
	EXStepMotor_SetDevice,
	EXStepMotor_SetENA,
	EXStepMotor_SetDIR,
	EXStepMotor_Start,
	EXStepMotor_Stop,
	EXStepMotor_SetStepsPerRevolution,
	EXStepMotor_SetSpeed,
	EXStepMotor_SetRevolutions,
};
//步进电机
typedef struct XStepMotor XStepMotor;
typedef struct XStepMotor
{
	XClass m_parent;//继承类
	uint16_t m_currentSpeed;//当前转速
	uint16_t m_pulsesPerRevolution;//每转脉冲数
	uint64_t m_currentPulses;//当前脉冲数 
	uint64_t m_setPulses;//设置脉冲数 //0是速度模式 其他是距离模式
	int64_t m_directionPulses;//累计方向脉冲数 正++ 反--
	XPWMDeviceBase* m_PUL;//pwm引脚
	XSwitchDeviceBase* m_ENA;//使能引脚
	XSwitchDeviceBase* m_DIR;//方向引脚
}XStepMotor;
XVtable* XStepMotor_class_init();
XStepMotor* XStepMotor_create(XSwitchDeviceBase* ENA, XSwitchDeviceBase* DIR, XPWMDeviceBase* PUL);
//初始化
void XStepMotor_init(XStepMotor* motor, XSwitchDeviceBase* ENA, XSwitchDeviceBase* DIR, XPWMDeviceBase* PUL);
//打开设备
void XStepMotor_open_base(XStepMotor* motor);
//设置设备
void XStepMotor_setDevice_base(XStepMotor* motor, void* device);
//使能打开输出
void XStepMotor_setENA_base(XStepMotor* motor, bool isEnabled);
//方向切换
void XStepMotor_setDIR_base(XStepMotor* motor, bool isForward);
//开启运行
void XStepMotor_start_base(XStepMotor* motor);
//关闭输出
void XStepMotor_stop_base(XStepMotor* motor);
//设置每转脉冲数
void XStepMotor_setStepsPerRevolution_base(XStepMotor* motor, uint16_t steps);
/*
* @brief  设置旋转速度.
* @param  motor:StepMotor对象
* @param  speed:转速 转/分钟
* @retval
*/
void XStepMotor_setSpeed_base(XStepMotor* motor, double speed);
/*
* @brief  设置旋转圈数.
* @param  motor:StepMotor对象
* @param  revolutions:圈数  会自动停止
* @retval
*/
void XStepMotor_setRevolutions_base(XStepMotor* motor, double revolutions);
/*
* @brief  获取旋转圈数.
* @param  motor:StepMotor对象
* @retval  旋转圈数，正转一圈+1 反转一圈减1 可获取位置信息
*/
double XStepMotor_getRevolutions(XStepMotor* motor);

bool XStepMotor_isOpen_base(XStepMotor* motor);

bool XStepMotor_isRunning_base(XStepMotor* motor);

//轮询扫描状态//一次脉冲后需要调用一次，距离模式需要
void XStepMotor_poll(XStepMotor* motor);

void XStepMotor_close_base(XStepMotor* motor);

#define XStepMotor_delete_base	XClass_delete_base

#ifdef __cplusplus
}
#endif
#endif // !StepMotor_H
