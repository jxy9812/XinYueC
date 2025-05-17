#ifndef XSTEPMOTOR_H
#define XSTEPMOTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#define   StepMotor_TIM_1MHz_Prescaler (84-1)//步进电机定时器在1Mz的预分频系数
#include"XIODevice.h"
//步进电机
typedef struct 
{
	bool m;
}XStepMotor;

//初始化
void StepMotor_init(XStepMotor* motor);
//使能打开输出
void StepMotor_ENA(XStepMotor* motor,bool open);
//方向切换
void StepMotor_DIR(XStepMotor* motor,bool corotation);
//开启定时器运行
void StepMotor_start(XStepMotor* motor);
//关闭定时器输出
void StepMotor_stop(XStepMotor* motor);
/*
* @brief  设置旋转速度.
* @param  motor:StepMotor对象
* @param  speed:转速 转/分钟
* @param  acceleration:加速度 转/秒^2
* @param  speed_init:初始化转速(转速为0才生效) 转/分钟
* @retval  
*/
void StepMotor_setSpeed(XStepMotor* motor,uint16_t speed,uint16_t acceleration,uint16_t speed_init);
/*
* @brief  设置旋转圈数.
* @param  motor:StepMotor对象
* @param  num:圈数  会自动停止
* @retval  
*/
void StepMotor_setNumRotations(XStepMotor* motor,double num);
/*
* @brief  获取旋转圈数.
* @param  motor:StepMotor对象
* @retval  旋转圈数，正转一圈+1 反转一圈减1 可获取位置信息
*/
double StepMotor_numRotations(XStepMotor* motor);

/*
* @brief  调速完成.
* @param  motor:StepMotor对象
* @retval  完成返回true
*/
bool StepMotor_isSpeedRegulationFinish(XStepMotor* motor);
#ifdef __cplusplus
}
#endif
#endif // !StepMotor_H
