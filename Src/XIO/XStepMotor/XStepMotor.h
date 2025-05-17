#ifndef XSTEPMOTOR_H
#define XSTEPMOTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#define   StepMotor_TIM_1MHz_Prescaler (84-1)//步进电机定时器在1Mz的预分频系数
#include"XIODevice.h"
typedef struct XStepMotor XStepMotor;
//步进电机接口
typedef struct XStepMotor_PortFunc
{
	void (*stateChangeCallback)(XStepMotor* io);//状态改变回调函数
}XStepMotor_PortFunc;
//步进电机初始化接口
typedef struct XStepMotor_PortFuncInit
{
	XIODevice_PortFuncInit parentPort;//父对象接口
	XStepMotor_PortFunc SwitchPort;//子类开关接口
}XStepMotor_PortFuncInit;
//步进电机
typedef struct XStepMotor
{
	XIODevice m_parent;//父对象
	XStepMotor_PortFunc m_port;//开关设备接口
}XStepMotor;

//初始化
void XStepMotor_init(XStepMotor* motor);
//使能打开输出
void XStepMotor_ENA(XStepMotor* motor,bool open);
//方向切换
void XStepMotor_DIR(XStepMotor* motor,bool corotation);
//开启定时器运行
void XStepMotor_start(XStepMotor* motor);
//关闭定时器输出
void XStepMotor_stop(XStepMotor* motor);
/*
* @brief  设置旋转速度.
* @param  motor:StepMotor对象
* @param  speed:转速 转/分钟
* @param  acceleration:加速度 转/秒^2
* @param  speed_init:初始化转速(转速为0才生效) 转/分钟
* @retval  
*/
void XStepMotor_setSpeed(XStepMotor* motor,uint16_t speed,uint16_t acceleration,uint16_t speed_init);
/*
* @brief  设置旋转圈数.
* @param  motor:StepMotor对象
* @param  num:圈数  会自动停止
* @retval  
*/
void XStepMotor_setNumRotations(XStepMotor* motor,double num);
/*
* @brief  获取旋转圈数.
* @param  motor:StepMotor对象
* @retval  旋转圈数，正转一圈+1 反转一圈减1 可获取位置信息
*/
double XStepMotor_numRotations(XStepMotor* motor);

/*
* @brief  调速完成.
* @param  motor:StepMotor对象
* @retval  完成返回true
*/
bool XStepMotor_isSpeedRegulationFinish(XStepMotor* motor);
#ifdef __cplusplus
}
#endif
#endif // !StepMotor_H
