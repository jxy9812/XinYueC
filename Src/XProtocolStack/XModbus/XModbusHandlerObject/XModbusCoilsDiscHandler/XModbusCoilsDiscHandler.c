#include "XModbusCoilsDiscHandler.h"
#include "XMemory.h"
#include "XByteArray.h"
XModbusCoilsDiscHandler* XModbusCoilsDiscHandler_create(uint16_t count)
{
    if (count == 0)
        return NULL;
    uint16_t size = (count % 8 == 0) ? (count / 8) : ((count / 8) + 1);
    XModbusCoilsDiscHandler* ptr = XMemory_malloc(sizeof(XModbusCoilsDiscHandler));
    ptr->count = count;
    ptr->parent.data = XVector_Create(char);
    XVector_resize_base(ptr->parent.data, size);
    return ptr;
}

void XModbusCoilsDiscHandler_delete(XModbusCoilsDiscHandler* pRegHandler)
{
    if (pRegHandler)
    {
        if (pRegHandler->parent.data)
            XVector_delete_base(pRegHandler->parent.data);
        XMemory_free(pRegHandler);
    }
}

bool XModbusCoilsDiscHandler_write(XModbusCoilsDiscHandler* pRegHandler, uint16_t address, uint16_t count, const char* writeArray)
{
    if (pRegHandler == NULL || pRegHandler->parent.data == NULL || writeArray == NULL || count == 0)
        return false;
}
