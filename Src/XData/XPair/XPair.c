#include"XPair.h"
#include"XContainer.h"
#include"XVariantTypeOps.h"
#include"XVariant.h"
//#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>

/*
 * XPair 将两个值存放在同一段字节缓冲区中。第一段数据位于 XPair
 * 之后，并且由 sizeof(XPair) 保证自然对齐；当第一段数据较小或与
 * 第二段数据的对齐要求不兼容时，第二段数据必须显式增加填充，例如
 * int 后面存放指针的情况。
 */
static size_t XPair_secondOffset(size_t firstTypeSize)
{
	return ALIGN_UP(firstTypeSize, sizeof(void*));
}

void XPair_init(XPair* this_pair, const size_t firstTypeSize, const size_t secondTypeSize)
{
	if (!this_pair ||firstTypeSize == 0 || secondTypeSize == 0)return;
	this_pair->m_firstTypeSize = firstTypeSize;
	this_pair->m_secondTypeSize = secondTypeSize;

	memset(XPair_first(this_pair), 0, firstTypeSize);
	memset(XPair_second(this_pair), 0, secondTypeSize);
}
XPair* XPair_create(const size_t firstTypeSize, const size_t secondTypeSize)
{
	if (firstTypeSize == 0 || secondTypeSize == 0)
	{
		printf("有类型设置错误");
		return NULL;
	}
	size_t size = XPair_size1(firstTypeSize, secondTypeSize);
	XPair* this_pair = (XPair*)XMalloc_System(size);
	if (!this_pair)return NULL;
	XPair_init(this_pair, firstTypeSize, secondTypeSize);
	return this_pair;
}

XPair* XPair_create_copy(const XPair* other)
{
	if (other == NULL)
		return NULL;
	XPair* pair = XPair_create(other->m_firstTypeSize, other->m_secondTypeSize);
	if(pair==NULL)
		return NULL;
	XPair_copy(pair,other);
	return pair;
}

XPair* XPair_create_move(XPair* other)
{
	if (other == NULL)
		return NULL;
	XPair* pair = XPair_create(other->m_firstTypeSize, other->m_secondTypeSize);
	if (pair == NULL)
		return NULL;
	XPair_move(pair, other);
	return pair;
}

void XPair_copy(XPair* this_pair, const XPair* copy)
{
	if (this_pair == NULL || copy == NULL||this_pair->m_firstTypeSize!=copy->m_firstTypeSize||this_pair->m_secondTypeSize!=copy->m_secondTypeSize)
		return;
	memcpy(XPair_first(this_pair), XPair_first((XPair*)copy), copy->m_firstTypeSize);
	memcpy(XPair_second(this_pair), XPair_second((XPair*)copy), copy->m_secondTypeSize);
}

void XPair_move(XPair* this_pair, XPair* move)
{
	if (this_pair == NULL || move == NULL || this_pair->m_firstTypeSize != move->m_firstTypeSize || this_pair->m_secondTypeSize != move->m_secondTypeSize)
		return;
	memcpy(XPair_first(this_pair), XPair_first(move), move->m_firstTypeSize);
	memcpy(XPair_second(this_pair), XPair_second(move), move->m_secondTypeSize);
	memset(XPair_first(move), 0, move->m_firstTypeSize);
	memset(XPair_second(move), 0, move->m_secondTypeSize);
}

void XPair_insert(XPair* this_pair, void* firstData, void* secondData)
{
	XPair_insertFirst(this_pair, firstData);
	XPair_insertSecond(this_pair, secondData);
}

void XPair_insertFirst(XPair* this_pair, void* firstData)
{
	if (ISNULL(this_pair, ""))
		return;
	if(firstData)
		memcpy(XPair_first(this_pair), firstData, this_pair->m_firstTypeSize);
	else
		memset(XPair_first(this_pair), 0, this_pair->m_firstTypeSize);
}

void XPair_insertSecond(XPair* this_pair, void* secondData)
{
	if (ISNULL(this_pair, ""))
		return;
	if(secondData!=NULL)
	{
		memcpy(XPair_second(this_pair), secondData, this_pair->m_secondTypeSize);
	}
	else
	{
		memset(XPair_second(this_pair), 0, this_pair->m_secondTypeSize);
	}
}
void* XPair_first(XPair* this_pair)
{
	if (ISNULL(this_pair, ""))
		return;
	return this_pair+1;
}
void* XPair_second(XPair* this_pair)
{
	if (ISNULL(this_pair, ""))
		return;
	return ((uint8_t*)(this_pair+1)) + XPair_secondOffset(this_pair->m_firstTypeSize);
}
size_t XPair_size1(size_t firstTypeSize, size_t secondTypeSize)
{
	if (!firstTypeSize|| !secondTypeSize)return 0;
	return ALIGN_UP(sizeof(XPair) + XPair_secondOffset(firstTypeSize) + secondTypeSize,
	                sizeof(void*));
}
size_t XPair_size2(XPair* this_pair)
{
	if (!this_pair)return 0;
	return XPair_size1(this_pair->m_firstTypeSize, this_pair->m_secondTypeSize);
}
void XPair_delete(XPair* this_pair)
{
	XFree_System(this_pair);
}

int32_t XPair_compare(const XPair* lhs, const XPair* rhs)
{
	if (memcmp(XPair_first(lhs), XPair_first(rhs), lhs->m_firstTypeSize)==0&&
		memcmp(XPair_second(lhs), XPair_second(rhs), lhs->m_secondTypeSize)==0)
		return XCompare_Equality;
	return XCompare_Other;
}

XVARIANT_TYPE_OPS_DEFINE(XPair, 0, NULL, NULL, NULL, NULL,
	XPair_compare, "XPair");

XVariant* XPair_toVariant(const XPair* pair)
{
	XVariant* variant;
	if (!pair)
		return NULL;
	variant = XVariant_create(NULL, XPair_size2((XPair*)pair), XVariantType_Pair);
	if (!variant)
		return NULL;
	XPair_init((XPair*)variant->m_data, pair->m_firstTypeSize, pair->m_secondTypeSize);
	XPair_copy((XPair*)variant->m_data, pair);
	return variant;
}

XVariant* XPair_toVariant_move(XPair* pair)
{
	XVariant* variant;
	if (!pair)
		return NULL;
	variant = XVariant_create(NULL, XPair_size2(pair), XVariantType_Pair);
	if (!variant)
		return NULL;
	XPair_init((XPair*)variant->m_data, pair->m_firstTypeSize, pair->m_secondTypeSize);
	XPair_move((XPair*)variant->m_data, pair);
	return variant;
}

XVariant* XPair_toVariant_ref(XPair* pair)
{
	XVariant* variant;
	if (!pair)
		return NULL;
	variant = XVariant_create(NULL, 0, XVariantType_Pair);
	if (variant)
		XVariant_setDataRef(variant, pair, XPair_size2(pair), XVariantType_Pair);
	return variant;
}

XPair* XPair_fromVariant(const XVariant* variant)
{
	XPair* source = (XPair*)XVariant_toRef(variant, XVariantType_Pair);
	return source ? XPair_create_copy(source) : NULL;
}

XPair* XPair_fromVariant_ref(const XVariant* variant)
{
	return (XPair*)XVariant_toRef(variant, XVariantType_Pair);
}

static bool XPair_prepareVariant(XVariant* variant, const XPair* pair)
{
	size_t size;
	XPair* target;
	if (!variant || !pair)
		return false;
	size = XPair_size2((XPair*)pair);
	target = (XPair*)variant->m_data;
	if (variant->m_type != XVariantType_Pair || !target ||
		variant->m_dataSize != size ||
		target->m_firstTypeSize != pair->m_firstTypeSize ||
		target->m_secondTypeSize != pair->m_secondTypeSize) {
		if (variant->m_data)
			XVariant_deinit_base(variant);
		variant->m_data = XMalloc_System(size);
		if (!variant->m_data)
			return false;
		variant->m_dataSize = size;
		XPair_init((XPair*)variant->m_data, pair->m_firstTypeSize,
		           pair->m_secondTypeSize);
		variant->m_type = XVariantType_Pair;
	}
	return true;
}

void XPair_setVariant(XVariant* variant, const XPair* pair)
{
	if (XPair_prepareVariant(variant, pair))
		XPair_copy((XPair*)variant->m_data, pair);
}

void XPair_setVariant_move(XVariant* variant, XPair* pair)
{
	if (XPair_prepareVariant(variant, pair))
		XPair_move((XPair*)variant->m_data, pair);
}

void XPair_setVariant_ref(XVariant* variant, XPair* pair)
{
	if (!variant || !pair)
		return;
	XVariant_setDataRef(variant, pair, XPair_size2(pair), XVariantType_Pair);
}
