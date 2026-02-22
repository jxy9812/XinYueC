#include "XModbusDeviceIdentification.h"
#include "XMemory.h"
#include "XVector.h"
#include "XMap.h"
#include "XByteArray.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
// 虚函数重载
static void VXModbusDeviceIdentification_deinit(XModbusDeviceIdentification* id);

XVtable* XModbusDeviceIdentification_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

        // 重载析构函数
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusDeviceIdentification_deinit);

#if SHOWCONTAINERSIZE
    printf("XModbusDeviceIdentification size: %zu\n", sizeof(XModbusDeviceIdentification));
#endif
    return XVTABLE_DEFAULT;
}

XModbusDeviceIdentification* XModbusDeviceIdentification_create(void) {
    XModbusDeviceIdentification* id = (XModbusDeviceIdentification*)XMemory_malloc(sizeof(XModbusDeviceIdentification));
    if (id) XModbusDeviceIdentification_init(id);
    return id;
}

void XModbusDeviceIdentification_init(XModbusDeviceIdentification* id) {
    if (!id) return;
    XClass_init((XClass*)id);
    XClassGetVtable(id) = XModbusDeviceIdentification_class_init();
    id->m_objects = XMap_Create(int, XByteArray,XCompare_int); 
    XContainerSetDataCopyMethod(id->m_objects, XByteArray_copy_base);
    XContainerSetDataMoveMethod(id->m_objects, XByteArray_move_base);
    XContainerSetDataDeinitMethod(id->m_objects, XByteArray_deinit_base);
    id->m_conformityLevel = XModbusDeviceIdentification_BasicConformityLevel;
}

static void VXModbusDeviceIdentification_deinit(XModbusDeviceIdentification* id) {
    if (!id) return;

    // 释放映射中的所有 XByteArray*
    if (id->m_objects) {
        XMap_delete_base(id->m_objects);
        id->m_objects = NULL;
    }

    XClass_deinit_base((XClass*)id);
}

// --- 核心接口实现 ---
bool XModbusDeviceIdentification_isValid(const XModbusDeviceIdentification* id) {
    if (!id || !id->m_objects) return false;

    XByteArray* vendor = XModbusDeviceIdentification_value(id, XModbusDeviceIdentification_VendorNameObjectId);
    XByteArray* product = XModbusDeviceIdentification_value(id, XModbusDeviceIdentification_ProductCodeObjectId);
    XByteArray* revision = XModbusDeviceIdentification_value(id, XModbusDeviceIdentification_MajorMinorRevisionObjectId);

    bool valid = vendor && !XByteArray_isEmpty_base(vendor) &&
        product && !XByteArray_isEmpty_base(product) &&
        revision && !XByteArray_isEmpty_base(revision);

    // 清理临时拷贝
    if (vendor) XByteArray_delete_base(vendor);
    if (product) XByteArray_delete_base(product);
    if (revision) XByteArray_delete_base(revision);

    return valid;
}

XVector* XModbusDeviceIdentification_objectIds(const XModbusDeviceIdentification* id) {
    if (!id || !id->m_objects) return NULL;
    return XMap_keys_base(id->m_objects); // 假设有一个获取所有 uint8_t key 的便捷函数
}

void XModbusDeviceIdentification_remove(XModbusDeviceIdentification* id, int objectId) {
    if (!id || !id->m_objects) return;
    XMap_remove_base(id->m_objects, &objectId);
}

bool XModbusDeviceIdentification_contains(const XModbusDeviceIdentification* id, int objectId) {
    if (!id || !id->m_objects) return false;
    return XMapBase_contains(id->m_objects, &objectId);
}

bool XModbusDeviceIdentification_insert(XModbusDeviceIdentification* id, int objectId, const uint8_t* data, size_t size) {
    if (!id || !id->m_objects || !data || size > 245 || objectId >= XModbusDeviceIdentification_UndefinedObjectId) {
        return false;
    }

    // 移除旧值（如果存在）
    XModbusDeviceIdentification_remove(id, objectId);

    // 创建新的 XByteArray 并插入
    XByteArray* newData = XByteArray_create_with_data(data, size);
    XMap_insert_valueMove_base(id->m_objects, &objectId, newData);
    XByteArray_delete_base(newData);
    return true;
}

XByteArray* XModbusDeviceIdentification_value(const XModbusDeviceIdentification* id, int objectId) {
    if (!id || !id->m_objects) return NULL;
    XByteArray* value = (XByteArray*)XMap_value_base(id->m_objects, objectId);
    if (value) {
        return XByteArray_create_copy(value);
    }
    return NULL;
}

XModbusDeviceIdentification_ConformityLevel XModbusDeviceIdentification_conformityLevel(const XModbusDeviceIdentification* id) {
    return id ? id->m_conformityLevel : XModbusDeviceIdentification_BasicConformityLevel;
}

void XModbusDeviceIdentification_setConformityLevel(XModbusDeviceIdentification* id, XModbusDeviceIdentification_ConformityLevel level) {
    if (id) {
        id->m_conformityLevel = level;
    }
}

// --- 从字节数组解析 ---
XModbusDeviceIdentification* XModbusDeviceIdentification_fromByteArray(const uint8_t* data, size_t size) {
    if (!data || size < 7) { // 最小有效帧长度
        return NULL;
    }

    XModbusDeviceIdentification* id = XModbusDeviceIdentification_create();
    if (!id) return NULL;

    // 假设 data 是一个有效的 MEI (Modbus Encapsulated Interface) Read Device ID 响应
    // 格式: [MEI Type][ReadDevId][ConformityLevel][MoreFollows][NextObjId][NumObjects][ObjId][Length][Data]...
    // 这里我们只处理最简单的单帧响应
    uint8_t conformityLevel = data[2];
    uint8_t numObjects = data[5];

    XModbusDeviceIdentification_setConformityLevel(id, (XModbusDeviceIdentification_ConformityLevel)conformityLevel);

    size_t offset = 6;
    for (int i = 0; i < numObjects && offset < size; ++i) {
        uint8_t objId = data[offset++];
        uint8_t objLen = data[offset++];
        if (offset + objLen > size) {
            // 数据不完整，清理并返回 NULL
            XModbusDeviceIdentification_delete_base(id);
            return NULL;
        }
        XModbusDeviceIdentification_insert(id, objId, &data[offset], objLen);
        offset += objLen;
    }

    return id;
}