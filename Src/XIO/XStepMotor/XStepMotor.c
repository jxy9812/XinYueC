#include "XStepMotor.h"
#include <stdio.h>
#include "XMap.h"
#include "XEquality.h"
#include "XLess.h"
//#include "FreeRTOS.h"

XStepMotor* XStepMotor_new(XStepMotor_PortFuncInit* port)
{
	if (port == NULL)
		return NULL;
	XStepMotor* motor= XMemory_malloc(sizeof(XSwitchDevice));
	if (motor == NULL)
		return motor;

}
