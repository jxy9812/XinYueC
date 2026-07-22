/******************************************************************************
 * @file       XGuiTypes.c
 * @brief      XGui 基础几何类型实现
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiTypes.h"
#include <stdlib.h>
#include <string.h>
#include "XCompare.h"


void XPoint_init(XPoint* self, int x, int y)
{
    if (!self) return;
    self->x = x;
    self->y = y;
}

void XSize_init(XSize* self, int width, int height)
{
    if (!self) return;
    self->width = width;
    self->height = height;
}

void XRect_init(XRect* self, int x, int y, int width, int height)
{
    if (!self) return;
    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;
}

bool XRect_isEmpty(const XRect* self)
{
    if (!self) return true;
    return (self->width <= 0 || self->height <= 0);
}

bool XRect_contains(const XRect* self, int x, int y)
{
    if (!self) return false;
    return (x >= self->x && x < self->x + self->width &&
            y >= self->y && y < self->y + self->height);
}

void XSizeF_init(XSizeF* self, float width, float height)
{
    if (!self) return;
    self->width = width;
    self->height = height;
}

void XRegion_init(XRegion* self)
{
    if (!self) return;
    self->rects = NULL;
    self->count = 0;
    self->capacity = 0;
}

void XRegion_deinit(XRegion* self)
{
    if (!self) return;
    if (self->rects) free(self->rects);
    self->rects = NULL;
    self->count = 0;
    self->capacity = 0;
}

void XRegion_addRect(XRegion* self, const XRect* rect)
{
    if (!self || !rect) return;
    if (self->count >= self->capacity)
    {
        int newCap = self->capacity ? self->capacity * 2 : 4;
        XRect* newRects = (XRect*)realloc(self->rects, (size_t)newCap * sizeof(XRect));
        if (!newRects) return;
        self->rects = newRects;
        self->capacity = newCap;
    }
    self->rects[self->count++] = *rect;
}


int32_t XPoint_compare(const XPoint* lhs, const XPoint* rhs)
{
    if (lhs->x == rhs->x && lhs->y == rhs->y)
        return XCompare_Equality;
    if (lhs->x * lhs->y < rhs->x * rhs->y)
        return XCompare_Less;
    return XCompare_Greater;
}
