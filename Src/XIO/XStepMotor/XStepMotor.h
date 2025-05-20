#ifndef XSTEPMOTOR_H
#define XSTEPMOTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#define   StepMotor_TIM_1MHz_Prescaler (84-1)//步进电机定时器在1Mz的预分频系数
#include"XIODevice.h"
#include"XPWMDevice.h"
#include"XSwitchDevice.h"
	typedef struct XStepMotor XStepMotor;
	//步进电机接口
	typedef struct XStepMotor_PortFunc
	{
		bool b;
		//void (*stateChangeCallback)(XStepMotor* io);//状态改变回调函数
	}XStepMotor_PortFunc;
	//步进电机初始化接口
	typedef struct XStepMotor_PortFuncInit
	{
		//XIODevice_PortFuncInit parentPort;//父对象接口
		XPWMDevice_PortFuncInit PUL;//脉冲
		XSwitchDevice_PortFuncInit ENA;//使能
		XSwitchDevice_PortFuncInit DIR;//方向
		XStepMotor_PortFunc StepMotorPort;//子类开关接口
	}XStepMotor_PortFuncInit;
	//步进电机
	typedef struct XStepMotor
	{
		//XIODevice m_parent;//父对象
		//uint8_t m_acceleration;//加速度
		uint16_t m_currentSpeed;//当前转速
		//uint16_t m_setSpeed;//设置转速
		uint16_t m_pulsesPerRevolution;//每转脉冲数
		uint64_t m_currentPulses;//当前脉冲数 
		uint64_t m_setPulses;//设置脉冲数 //0是速度模式 其他是距离模式
		int64_t m_directionPulses;//累计方向脉冲数 正++ 反--
		XPWMDevice* m_PUL;//pwm引脚
		XSwitchDevice* m_ENA;//使能引脚
		XSwitchDevice* m_DIR;//方向引脚
		XStepMotor_PortFunc m_port;//开关设备接口
	}XStepMotor;
	XStepMotor* XStepMotor_new(XStepMotor_PortFuncInit* port);
	//初始化
	void XStepMotor_init(XStepMotor* motor, XStepMotor_PortFuncInit* port);
	//打开设备
	void XStepMotor_open(XStepMotor* motor);
	void XStepMotor_setDevice(XStepMotor* motor, void* device);

	//使能打开输出
	void XStepMotor_setENA(XStepMotor* motor, bool open);
	//方向切换
	void XStepMotor_setDIR(XStepMotor* motor, bool corotation);
	//开启运行
	void XStepMotor_start(XStepMotor* motor);
	//关闭输出
	void XStepMotor_stop(XStepMotor* motor);
	//设置每转脉冲数
	void XStepMotor_setPulsesPerRevolution(XStepMotor* motor, uint16_t num);
	/*
	* @brief  设置旋转速度.
	* @param  motor:StepMotor对象
	* @param  speed:转速 转/分钟
	* @retval
	*/
	void XStepMotor_setSpeed(XStepMotor* motor, uint16_t speed);
	/*
	* @brief  设置旋转圈数.
	* @param  motor:StepMotor对象
	* @param  num:圈数  会自动停止
	* @retval
	*/
	void XStepMotor_setNumRotations(XStepMotor* motor, double num);
	/*
	* @brief  获取旋转圈数.
	* @param  motor:StepMotor对象
	* @retval  旋转圈数，正转一圈+1 反转一圈减1 可获取位置信息
	*/
	double XStepMotor_numRotations(XStepMotor* motor);

	//轮询扫描状态
	void XStepMotor_poll(XStepMotor* motor);

	//一次完整的电平变化后调用 回调函数
	void XStepMotor_timerCallback(XStepMotor* motor);
#ifdef __cplusplus
}
#endif
#endif // !StepMotor_H
