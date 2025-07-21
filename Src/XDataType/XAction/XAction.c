#include "XAction.h"
#include "XMemory.h"
#include "XVariant.h"
#include <string.h>
XAction* XAction_create(const char* text)
{
	XAction* action = XMemory_malloc(sizeof(XAction));
	if (action)
		XAction_init(action, text);
	return action;
}

void XAction_init(XAction* action, const char* text)
{
	if (action == NULL)
		return;
	action->text = NULL;
	action->action = NULL;
	XAction_setText(action,text);
}

void XAction_setText(XAction* action, const char* text)
{
	if (action == NULL)
		return;
	if (action->text)
		XMemory_free(action->text);
	char* str = NULL;
	if (text)
	{
		size_t len = strlen(text) + 1;
		if (len > 0)
		{
			str = XMemory_malloc(len);
			memcpy(str, text, len);
		}
	}
	action->text = str;
}

void XAction_setAction(XAction* action, Action func)
{
	if (action)
		action->action = func;
}

void XAction_setData(XAction* action, XVariant* data)
{
	if (action == NULL)
		return;
	if (action->data)
		XVariant_delete(action->data);
	action->data = data;
}

void XAction_delete(XAction* action)
{
	if (action == NULL)
		return;
	if (action->text)
		XMemory_free(action->text);
	if (action->data)
		XVariant_delete(action->data);
	XMemory_free(action);
}

void XAction_trigger(XAction* action)
{
	if (action)
		action->action(action->data);
}

const char* XAction_getText(XAction* action)
{
	if (action)
		return action->text;
	return NULL;
}

Action XAction_getAction(XAction* action)
{
	if (action)
		return action->action;
	return NULL;
}

XVariant* XAction_getData(XAction* action)
{
	if (action)
		return action->data;
	return NULL;
}
