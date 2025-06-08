#ifdef USE_STDPERIPH_DRIVER
#include"XSwitchDeviceSTM32.h"
#include"XMemory.h"
static bool VXIODevice_open(XSwitchDeviceSTM32 *sw, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XSwitchDeviceSTM32 *sw, const char* data, size_t maxSize);//写入
static size_t VXIODevice_read(XSwitchDeviceSTM32 *sw, char* data, size_t maxSize);//读取
static void VXIODevice_close(XSwitchDeviceSTM32 *sw);
XVtable *XSwitchDeviceSTM32_class_init()
{
    	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XSWITCHDEVICESTM32_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XSwitchDeviceBase_class_init());
	// void* table[] = { VXSwitchDevice_setState,VXSwitchDevice_getState };
	// //追加虚函数
	// XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open,VXIODevice_open);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Write,VXIODevice_write);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read,VXIODevice_read);
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close,VXIODevice_close);
#if SHOWCONTAINERSIZE
	printf("XSwitchDeviceSTM32 size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void XSwitchDeviceSTM32_init(XSwitchDeviceSTM32 *sw)
{
	if (sw == NULL)
    	return;
	memset(((XSwitchDeviceBase*)sw) + 1, 0, sizeof(XSwitchDeviceSTM32) - sizeof(XSwitchDeviceBase));
	XSwitchDeviceBase_init(sw, NULL);
	XClassGetVtable(sw) = XSwitchDeviceSTM32_class_init();
}
//启用了F4系列
#ifdef STM32F40_41xxx
#include"stm32f4xx.h"
XSwitchDeviceSTM32 *XSwitchDeviceSTM32_create(XSwitchGPIO *gpio)
{
	XSwitchDeviceSTM32 * sw=XMemory_malloc(sizeof(XSwitchDeviceSTM32));
	if(sw==NULL)
		return NULL;
	XSwitchDeviceSTM32_init(sw);
	sw->m_gpio=*gpio;
    return sw;
}
bool VXIODevice_open(XSwitchDeviceSTM32 *sw, XIODeviceBaseMode mode)
{
	if (XIODeviceBase_isOpen(sw))
     	return true;//已经打开了
	if(!(mode&XIODeviceBase_ReadOnly||mode&XIODeviceBase_WriteOnly))
    	return false;
	GPIO_InitTypeDef GPIO_InitStructure; //定义结构体变量
	RCC_AHB1PeriphClockCmd(sw->m_gpio.GPIO_Clock,ENABLE); //使能端口时钟
	if(mode&XIODeviceBase_ReadOnly)
	{
		GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IN; //输入模式
	}
	else if(mode&XIODeviceBase_WriteOnly)
	{
		GPIO_InitStructure.GPIO_Mode=GPIO_Mode_OUT; //输出模式
		GPIO_InitStructure.GPIO_Speed=GPIO_Speed_100MHz;//速度为100M
		GPIO_InitStructure.GPIO_OType=GPIO_OType_PP;//推挽输出
	}
	GPIO_InitStructure.GPIO_Pin=sw->m_gpio.GPIO_Pin_X;//管脚设置
	GPIO_InitStructure.GPIO_PuPd=sw->m_gpio.GPIO_PuPd;//下拉 低电平
	GPIO_Init(sw->m_gpio.GPIOX,&GPIO_InitStructure); //初始化结构体
	//输出模式下设置默认电平
	if(mode&XIODeviceBase_WriteOnly)
	{
		if(sw->m_gpio.GPIO_PuPd==GPIO_PuPd_DOWN)
		{
			GPIO_ResetBits(sw->m_gpio.GPIOX,sw->m_gpio.GPIO_Pin_X);//初始化当前是低电平
			sw->m_parent.m_state=false;
		}
		else if(sw->m_gpio.GPIO_PuPd==GPIO_PuPd_UP)
		{
			GPIO_SetBits(sw->m_gpio.GPIOX,sw->m_gpio.GPIO_Pin_X);//初始化当前是高电平
			sw->m_parent.m_state=true;
		}
	}
	((XIODeviceBase*)sw)->m_mode=mode;
	return true;
}

size_t VXIODevice_write(XSwitchDeviceSTM32 *sw, const char *data, size_t maxSize)
{
	if(((XIODeviceBase*)sw)->m_mode&XIODeviceBase_WriteOnly)
	{
		GPIO_WriteBit(sw->m_gpio.GPIOX,sw->m_gpio.GPIO_Pin_X,*((bool*)data));
		return 1;
	}
    return 0;
}

size_t VXIODevice_read(XSwitchDeviceSTM32 *sw, char *data, size_t maxSize)
{
    if(((XIODeviceBase*)sw)->m_mode&XIODeviceBase_ReadOnly)
	{
		*((bool*)data)=GPIO_ReadInputDataBit(sw->m_gpio.GPIOX,sw->m_gpio.GPIO_Pin_X);
		return 1;
	}
	else  if(((XIODeviceBase*)sw)->m_mode&XIODeviceBase_WriteOnly)
	{
		*((bool*)data)=GPIO_ReadOutputDataBit(sw->m_gpio.GPIOX,sw->m_gpio.GPIO_Pin_X);
		return 1;
	}
    return 0;
}

void VXIODevice_close(XSwitchDeviceSTM32 *sw)
{
	if(XIODeviceBase_isOpen(sw))
	{
		XSwitchDeviceBase_setState_base(sw,false);
	}
}

#endif
#endif