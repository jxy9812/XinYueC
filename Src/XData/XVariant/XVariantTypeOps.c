#include "XVariantTypeOps.h"

extern const XVariantTypeOps XPair_variantTypeOps;
extern const XVariantTypeOps XPoint_variantTypeOps;
extern const XVariantTypeOps XDate_variantTypeOps;
extern const XVariantTypeOps XTime_variantTypeOps;
extern const XVariantTypeOps XDateTime_variantTypeOps;
extern const XVariantTypeOps XByteArray_variantTypeOps;
extern const XVariantTypeOps XString_variantTypeOps;
extern const XVariantTypeOps XStringList_variantTypeOps;
extern const XVariantTypeOps XVariantList_variantTypeOps;
extern const XVariantTypeOps XMap_variantTypeOps;
extern const XVariantTypeOps XHashMap_variantTypeOps;
extern const XVariantTypeOps XJsonDocument_variantTypeOps;
extern const XVariantTypeOps XJsonArray_variantTypeOps;
extern const XVariantTypeOps XJsonObject_variantTypeOps;
extern const XVariantTypeOps XJsonValue_variantTypeOps;
extern const XVariantTypeOps XBsonArray_variantTypeOps;
extern const XVariantTypeOps XBsonDocument_variantTypeOps;
extern const XVariantTypeOps XBsonValue_variantTypeOps;

static const XVariantTypeOps* const g_variantTypeOps[XVariantType_ExtensionCount] =
{
	&XPair_variantTypeOps,
	&XPoint_variantTypeOps,
	&XDate_variantTypeOps,
	&XTime_variantTypeOps,
	&XDateTime_variantTypeOps,
	&XByteArray_variantTypeOps,
	&XString_variantTypeOps,
	&XStringList_variantTypeOps,
	&XVariantList_variantTypeOps,
	&XMap_variantTypeOps,
	&XHashMap_variantTypeOps,
	&XJsonDocument_variantTypeOps,
	&XJsonArray_variantTypeOps,
	&XJsonObject_variantTypeOps,
	&XJsonValue_variantTypeOps,
	&XBsonArray_variantTypeOps,
	&XBsonDocument_variantTypeOps,
	&XBsonValue_variantTypeOps
};

const XVariantTypeOps* XVariantTypeOps_forType(int type)
{
	if (type < XVariantType_ExtensionFirst || type >= XVariantType_User)
		return NULL;
	return g_variantTypeOps[type - XVariantType_ExtensionFirst];
}

const char* XVariantTypeOps_nameForType(int type)
{
	const XVariantTypeOps* ops = XVariantTypeOps_forType(type);
	return ops ? ops->typeName : NULL;
}
