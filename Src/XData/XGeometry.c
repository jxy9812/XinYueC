/******************************************************************************
 * @file       XGeometry.c
 * @brief      基础空间几何类型实现
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGeometry.h"
#include "XMemory.h"
#include <string.h>
#include <limits.h>
#include "XCompare.h"
#include "XVariantTypeOps.h"
#include "XVariant.h"

static int clamp_int64_to_int(int64_t value)
{
    if (value > INT_MAX) return INT_MAX;
    if (value < INT_MIN) return INT_MIN;
    return (int)value;
}

static int64_t abs_int_to_i64(int value)
{
    return value < 0 ? -(int64_t)value : (int64_t)value;
}

static bool rect_edges(const XRect* rect, int64_t* left, int64_t* top,
                       int64_t* right, int64_t* bottom)
{
    int64_t x1;
    int64_t y1;
    int64_t x2;
    int64_t y2;
    if (!rect) return false;
    x1 = rect->x;
    y1 = rect->y;
    x2 = x1 + (int64_t)rect->width;
    y2 = y1 + (int64_t)rect->height;
    if (x2 < x1) { int64_t t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { int64_t t = y1; y1 = y2; y2 = t; }
    if (left) *left = x1;
    if (top) *top = y1;
    if (right) *right = x2;
    if (bottom) *bottom = y2;
    return x2 > x1 && y2 > y1;
}

static void rect_from_edges(int64_t left, int64_t top, int64_t right,
                            int64_t bottom, XRect* out)
{
    if (!out) return;
    if (right <= left || bottom <= top)
    {
        out->x = clamp_int64_to_int(left);
        out->y = clamp_int64_to_int(top);
        out->width = 0;
        out->height = 0;
        return;
    }
    out->x = clamp_int64_to_int(left);
    out->y = clamp_int64_to_int(top);
    out->width = clamp_int64_to_int(right - left);
    out->height = clamp_int64_to_int(bottom - top);
}

static bool region_reserve(XRegion* self, int required)
{
    int newCapacity;
    XRect* rects;
    if (!self || required < 0) return false;
    if (required <= self->capacity) return true;
    newCapacity = self->capacity > 0 ? self->capacity : 4;
    while (newCapacity < required)
    {
        if (newCapacity > INT_MAX / 2) { newCapacity = required; break; }
        newCapacity *= 2;
    }
    if ((size_t)newCapacity > SIZE_MAX / sizeof(XRect)) return false;
    rects = (XRect*)XRealloc_System(self->rects,
                                    (size_t)newCapacity * sizeof(XRect));
    if (!rects) return false;
    self->rects = rects;
    self->capacity = newCapacity;
    return true;
}

static void region_replace(XRegion* out, XRegion* replacement)
{
    if (!out || !replacement || out == replacement) return;
    /* 运算结果通常比输出区域小。容量足够时直接覆盖元素，保留 out
       的矩形数组，避免每次求交/求并都释放并重新申请。 */
    if (replacement->count <= out->capacity &&
        (replacement->count == 0 || out->rects))
    {
        if (replacement->count > 0)
            memcpy(out->rects, replacement->rects,
                   (size_t)replacement->count * sizeof(XRect));
        out->count = replacement->count;
        XRegion_deinit(replacement);
        return;
    }
    {
        XRegion old = *out;
        *out = *replacement;
        *replacement = old;
    }
    XRegion_deinit(replacement);
}

static void region_remove_at(XRegion* self, int index)
{
    if (!self || index < 0 || index >= self->count) return;
    if (index + 1 < self->count)
        memmove(&self->rects[index], &self->rects[index + 1],
                (size_t)(self->count - index - 1) * sizeof(XRect));
    --self->count;
}

static bool region_rects_can_merge(const XRect* lhs, const XRect* rhs)
{
    if (!lhs || !rhs) return false;
    if (XRect_containsRect(lhs, rhs) || XRect_containsRect(rhs, lhs)) return true;
    if (lhs->x == rhs->x && lhs->width == rhs->width)
        return XRect_intersects(lhs, rhs) ||
               (int64_t)XRect_bottom(lhs) + 1 == XRect_top(rhs) ||
               (int64_t)XRect_bottom(rhs) + 1 == XRect_top(lhs);
    if (lhs->y == rhs->y && lhs->height == rhs->height)
        return XRect_intersects(lhs, rhs) ||
               (int64_t)XRect_right(lhs) + 1 == XRect_left(rhs) ||
               (int64_t)XRect_right(rhs) + 1 == XRect_left(lhs);
    return false;
}

void XPoint_init(XPoint* self, int x, int y)
{
    if (!self) return;
    self->x = x;
    self->y = y;
}

bool XPoint_isNull(const XPoint* self)
{
    return !self || (self->x == 0 && self->y == 0);
}

int XPoint_manhattanLength(const XPoint* self)
{
    int64_t length;
    if (!self) return 0;
    length = abs_int_to_i64(self->x) + abs_int_to_i64(self->y);
    return clamp_int64_to_int(length);
}

XPoint XPoint_add(const XPoint* lhs, const XPoint* rhs)
{
    int64_t x;
    int64_t y;
    XPoint out;
    x = lhs ? lhs->x : 0;
    y = lhs ? lhs->y : 0;
    if (rhs) { x += rhs->x; y += rhs->y; }
    out.x = clamp_int64_to_int(x);
    out.y = clamp_int64_to_int(y);
    return out;
}

XPoint XPoint_subtract(const XPoint* lhs, const XPoint* rhs)
{
    int64_t x;
    int64_t y;
    XPoint out;
    x = lhs ? lhs->x : 0;
    y = lhs ? lhs->y : 0;
    if (rhs) { x -= rhs->x; y -= rhs->y; }
    out.x = clamp_int64_to_int(x);
    out.y = clamp_int64_to_int(y);
    return out;
}

void XSize_init(XSize* self, int width, int height)
{
    if (!self) return;
    self->width = width;
    self->height = height;
}

bool XSize_isNull(const XSize* self)
{
    return !self || (self->width == 0 && self->height == 0);
}

bool XSize_isEmpty(const XSize* self)
{
    return !self || self->width <= 0 || self->height <= 0;
}

bool XSize_isValid(const XSize* self)
{
    return self && self->width >= 0 && self->height >= 0;
}

XSize XSize_boundedTo(const XSize* self, const XSize* other)
{
    XSize out = { 0, 0 };
    if (self && other)
    {
        out.width = self->width < other->width ? self->width : other->width;
        out.height = self->height < other->height ? self->height : other->height;
    }
    return out;
}

XSize XSize_expandedTo(const XSize* self, const XSize* other)
{
    XSize out = { 0, 0 };
    if (self && other)
    {
        out.width = self->width > other->width ? self->width : other->width;
        out.height = self->height > other->height ? self->height : other->height;
    }
    return out;
}

XSize XSize_transposed(const XSize* self)
{
    XSize out = { 0, 0 };
    if (self)
    {
        out.width = self->height;
        out.height = self->width;
    }
    return out;
}

void XSize_scale(XSize* self, int width, int height, uint32_t mode)
{
    int sourceWidth;
    int sourceHeight;
    int64_t widthProduct;
    int64_t heightProduct;
    if (!self) return;
    if (width <= 0 || height <= 0)
    {
        self->width = width;
        self->height = height;
        return;
    }
    sourceWidth = self->width;
    sourceHeight = self->height;
    if (mode == 0 || sourceWidth <= 0 || sourceHeight <= 0)
    {
        self->width = width;
        self->height = height;
        return;
    }
    widthProduct = (int64_t)sourceWidth * height;
    heightProduct = (int64_t)sourceHeight * width;
    if ((mode == 1 && widthProduct >= heightProduct) ||
        (mode == 2 && widthProduct <= heightProduct))
    {
        self->width = width;
        self->height = clamp_int64_to_int(((int64_t)sourceHeight * width + sourceWidth / 2) / sourceWidth);
    }
    else
    {
        self->height = height;
        self->width = clamp_int64_to_int(((int64_t)sourceWidth * height + sourceHeight / 2) / sourceHeight);
    }
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

bool XRect_isNull(const XRect* self)
{
    return !self || (self->width == 0 && self->height == 0);
}

XRect XRect_normalized(const XRect* self)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    XRect out = { 0, 0, 0, 0 };
    if (!rect_edges(self, &left, &top, &right, &bottom))
    {
        if (!self) return out;
        out.x = clamp_int64_to_int(left);
        out.y = clamp_int64_to_int(top);
        return out;
    }
    rect_from_edges(left, top, right, bottom, &out);
    return out;
}

int XRect_left(const XRect* self) { return self ? self->x : 0; }
int XRect_top(const XRect* self) { return self ? self->y : 0; }
int XRect_right(const XRect* self)
{
    return self ? clamp_int64_to_int((int64_t)self->x + self->width - 1) : 0;
}
int XRect_bottom(const XRect* self)
{
    return self ? clamp_int64_to_int((int64_t)self->y + self->height - 1) : 0;
}

XSize XRect_size(const XRect* self)
{
    XSize out = { 0, 0 };
    if (self)
    {
        out.width = self->width;
        out.height = self->height;
    }
    return out;
}

XPoint XRect_topLeft(const XRect* self)
{
    XPoint out = { 0, 0 };
    if (self)
    {
        out.x = self->x;
        out.y = self->y;
    }
    return out;
}

XPoint XRect_center(const XRect* self)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    XPoint out = { 0, 0 };
    if (!rect_edges(self, &left, &top, &right, &bottom))
    {
        if (self)
        {
            out.x = self->x;
            out.y = self->y;
        }
        return out;
    }
    out.x = clamp_int64_to_int(left + (right - left - 1) / 2);
    out.y = clamp_int64_to_int(top + (bottom - top - 1) / 2);
    return out;
}

bool XRect_contains(const XRect* self, int x, int y)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    if (!rect_edges(self, &left, &top, &right, &bottom)) return false;
    return x >= left && x < right && y >= top && y < bottom;
}

bool XRect_containsRect(const XRect* self, const XRect* other)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t oLeft;
    int64_t oTop;
    int64_t oRight;
    int64_t oBottom;
    if (!rect_edges(self, &left, &top, &right, &bottom) ||
        !rect_edges(other, &oLeft, &oTop, &oRight, &oBottom)) return false;
    return oLeft >= left && oTop >= top && oRight <= right && oBottom <= bottom;
}

bool XRect_intersects(const XRect* self, const XRect* other)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t oLeft;
    int64_t oTop;
    int64_t oRight;
    int64_t oBottom;
    if (!rect_edges(self, &left, &top, &right, &bottom) ||
        !rect_edges(other, &oLeft, &oTop, &oRight, &oBottom)) return false;
    return left < oRight && oLeft < right && top < oBottom && oTop < bottom;
}

XRect XRect_intersected(const XRect* self, const XRect* other)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t oLeft;
    int64_t oTop;
    int64_t oRight;
    int64_t oBottom;
    XRect out = { 0, 0, 0, 0 };
    if (!rect_edges(self, &left, &top, &right, &bottom) ||
        !rect_edges(other, &oLeft, &oTop, &oRight, &oBottom))
        return out;
    if (oLeft > left) left = oLeft;
    if (oTop > top) top = oTop;
    if (oRight < right) right = oRight;
    if (oBottom < bottom) bottom = oBottom;
    rect_from_edges(left, top, right, bottom, &out);
    return out;
}

XRect XRect_united(const XRect* self, const XRect* other)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t oLeft;
    int64_t oTop;
    int64_t oRight;
    int64_t oBottom;
    bool haveSelf;
    bool haveOther;
    XRect out = { 0, 0, 0, 0 };
    haveSelf = rect_edges(self, &left, &top, &right, &bottom);
    haveOther = rect_edges(other, &oLeft, &oTop, &oRight, &oBottom);
    if (!haveSelf && !haveOther) return out;
    if (!haveSelf) { rect_from_edges(oLeft, oTop, oRight, oBottom, &out); return out; }
    if (!haveOther) { rect_from_edges(left, top, right, bottom, &out); return out; }
    if (oLeft < left) left = oLeft;
    if (oTop < top) top = oTop;
    if (oRight > right) right = oRight;
    if (oBottom > bottom) bottom = oBottom;
    rect_from_edges(left, top, right, bottom, &out);
    return out;
}

XRect XRect_adjusted(const XRect* self, int dx1, int dy1, int dx2, int dy2)
{
    XRect out = { 0, 0, 0, 0 };
    if (!self) return out;
    out.x = clamp_int64_to_int((int64_t)self->x + dx1);
    out.y = clamp_int64_to_int((int64_t)self->y + dy1);
    out.width = clamp_int64_to_int((int64_t)self->width + dx2 - dx1);
    out.height = clamp_int64_to_int((int64_t)self->height + dy2 - dy1);
    return out;
}

XRect XRect_translated(const XRect* self, int dx, int dy)
{
    XRect out = { 0, 0, 0, 0 };
    if (!self) return out;
    out = *self;
    out.x = clamp_int64_to_int((int64_t)self->x + dx);
    out.y = clamp_int64_to_int((int64_t)self->y + dy);
    return out;
}

void XRect_translate(XRect* self, int dx, int dy)
{
    if (self) *self = XRect_translated(self, dx, dy);
}

void XRect_moveCenter(XRect* self, const XPoint* center)
{
    if (!self || !center) return;
    self->x = clamp_int64_to_int((int64_t)center->x - self->width / 2);
    self->y = clamp_int64_to_int((int64_t)center->y - self->height / 2);
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
    if (self->rects) XFree_System(self->rects);
    self->rects = NULL;
    self->count = 0;
    self->capacity = 0;
}

void XRegion_clear(XRegion* self)
{
    if (self) self->count = 0;
}

bool XRegion_isEmpty(const XRegion* self)
{
    return !self || self->count == 0;
}

void XRegion_copy(const XRegion* self, XRegion* out)
{
    if (!out || self == out) return;
    /* 先确保容量，再改变元素数量；扩容失败时保留 out 的旧快照。 */
    if (!self || self->count <= 0 || !self->rects) {
        XRegion_clear(out);
        return;
    }
    if (!region_reserve(out, self->count)) return;
    memcpy(out->rects, self->rects, (size_t)self->count * sizeof(XRect));
    out->count = self->count;
}

void XRegion_boundingRect(const XRegion* self, XRect* out)
{
    int64_t left = 0;
    int64_t top = 0;
    int64_t right = 0;
    int64_t bottom = 0;
    bool have = false;
    if (!out) return;
    for (int i = 0; self && i < self->count; ++i)
    {
        int64_t rLeft;
        int64_t rTop;
        int64_t rRight;
        int64_t rBottom;
        if (!rect_edges(&self->rects[i], &rLeft, &rTop, &rRight, &rBottom)) continue;
        if (!have)
        {
            left = rLeft; top = rTop; right = rRight; bottom = rBottom; have = true;
        }
        else
        {
            if (rLeft < left) left = rLeft;
            if (rTop < top) top = rTop;
            if (rRight > right) right = rRight;
            if (rBottom > bottom) bottom = rBottom;
        }
    }
    if (!have) XRect_init(out, 0, 0, 0, 0);
    else rect_from_edges(left, top, right, bottom, out);
}

bool XRegion_contains(const XRegion* self, int x, int y)
{
    for (int i = 0; self && i < self->count; ++i)
        if (XRect_contains(&self->rects[i], x, y)) return true;
    return false;
}

bool XRegion_intersects(const XRegion* self, const XRect* rect)
{
    for (int i = 0; self && i < self->count; ++i)
        if (XRect_intersects(&self->rects[i], rect)) return true;
    return false;
}

static bool region_add_rect(XRegion* self, const XRect* rect)
{
    if (!self || !rect || XRect_isEmpty(rect)) return true;
    XRect value = XRect_normalized(rect);
    if (XRect_isEmpty(&value)) return true;
    /* Keep folding the new rectangle into every compatible existing one.
     * This makes merging transitive (A joins B, then A+B joins C). */
    for (;;)
    {
        bool mergedAny = false;
        for (int i = self->count - 1; i >= 0; --i)
        {
            XRect current = self->rects[i];
            if (XRect_containsRect(&current, &value)) return true;
            if (!region_rects_can_merge(&current, &value)) continue;
            value = XRect_united(&current, &value);
            region_remove_at(self, i);
            mergedAny = true;
        }
        if (!mergedAny) break;
    }
    if (!region_reserve(self, self->count + 1)) return false;
    self->rects[self->count++] = value;
    return true;
}

void XRegion_addRect(XRegion* self, const XRect* rect)
{
    (void)region_add_rect(self, rect);
}

void XRegion_united(const XRegion* self, const XRegion* other, XRegion* out)
{
    XRegion result;
    bool success = true;
    if (!out) return;
    XRegion_init(&result);
    for (int i = 0; success && self && i < self->count; ++i)
        success = region_add_rect(&result, &self->rects[i]);
    for (int i = 0; success && other && i < other->count; ++i)
        success = region_add_rect(&result, &other->rects[i]);
    if (!success) { XRegion_deinit(&result); return; }
    region_replace(out, &result);
}

void XRegion_intersected(const XRegion* self, const XRegion* other, XRegion* out)
{
    XRegion result;
    bool success = true;
    if (!out) return;
    XRegion_init(&result);
    for (int i = 0; success && self && i < self->count; ++i)
        for (int j = 0; success && other && j < other->count; ++j)
        {
            XRect intersection = XRect_intersected(&self->rects[i], &other->rects[j]);
            success = region_add_rect(&result, &intersection);
        }
    if (!success) { XRegion_deinit(&result); return; }
    region_replace(out, &result);
}

static bool region_subtract_rect(const XRect* source, const XRect* cutter, XRegion* out)
{
    XRect intersection;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t cutLeft;
    int64_t cutTop;
    int64_t cutRight;
    int64_t cutBottom;
    if (!source || !out) return true;
    if (!XRect_intersects(source, cutter)) return region_add_rect(out, source);
    intersection = XRect_intersected(source, cutter);
    rect_edges(source, &left, &top, &right, &bottom);
    rect_edges(&intersection, &cutLeft, &cutTop, &cutRight, &cutBottom);
    if (cutTop > top) { XRect r; rect_from_edges(left, top, right, cutTop, &r); if (!region_add_rect(out, &r)) return false; }
    if (cutBottom < bottom) { XRect r; rect_from_edges(left, cutBottom, right, bottom, &r); if (!region_add_rect(out, &r)) return false; }
    if (cutLeft > left) { XRect r; rect_from_edges(left, cutTop, cutLeft, cutBottom, &r); if (!region_add_rect(out, &r)) return false; }
    if (cutRight < right) { XRect r; rect_from_edges(cutRight, cutTop, right, cutBottom, &r); if (!region_add_rect(out, &r)) return false; }
    return true;
}

void XRegion_subtracted(const XRegion* self, const XRegion* other, XRegion* out)
{
    XRegion current;
    XRegion next;
    bool success = true;
    if (!out) return;
    XRegion_init(&current);
    XRegion_init(&next);
    for (int i = 0; success && self && i < self->count; ++i)
        success = region_add_rect(&current, &self->rects[i]);
    for (int j = 0; success && other && j < other->count && current.count > 0; ++j)
    {
        XRegion_clear(&next);
        for (int i = 0; i < current.count; ++i)
            if (!region_subtract_rect(&current.rects[i], &other->rects[j], &next))
            {
                success = false;
                break;
            }
        { XRegion temp = current; current = next; next = temp; }
    }
    if (!success)
    {
        XRegion_deinit(&current);
        XRegion_deinit(&next);
        return;
    }
    XRegion_deinit(&next);
    region_replace(out, &current);
}


int32_t XPoint_compare(const XPoint* lhs, const XPoint* rhs)
{
    if (lhs == rhs) return XCompare_Equality;
    if (!lhs) return XCompare_Less;
    if (!rhs) return XCompare_Greater;
    if (lhs->x < rhs->x) return XCompare_Less;
    if (lhs->x > rhs->x) return XCompare_Greater;
    if (lhs->y < rhs->y) return XCompare_Less;
    if (lhs->y > rhs->y) return XCompare_Greater;
    return XCompare_Equality;
}

XVARIANT_TYPE_OPS_DEFINE(XPoint, sizeof(XPoint), NULL, NULL, NULL, NULL,
	XPoint_compare, "XPoint");

XVariant* XPoint_toVariant(XPoint point)
{
    return XVariant_create((void*)&point, sizeof(XPoint), XVariantType_Point);
}

XPoint XPoint_fromVariant(const XVariant* variant)
{
    XPoint point = {0, 0};
    XPoint* source = XPoint_fromVariant_ref(variant);
    if (source)
        point = *source;
    return point;
}

XPoint* XPoint_fromVariant_ref(const XVariant* variant)
{
    return (XPoint*)XVariant_toRef(variant, XVariantType_Point);
}

void XPoint_setVariant(XVariant* variant, XPoint point)
{
    if (!variant)
        return;
    if (variant->m_type != XVariantType_Point || !variant->m_data ||
        variant->m_dataSize != sizeof(XPoint)) {
        if (variant->m_data)
            XVariant_deinit_base(variant);
        variant->m_data = XMalloc_System(sizeof(XPoint));
        if (!variant->m_data)
            return;
        variant->m_dataSize = sizeof(XPoint);
        variant->m_type = XVariantType_Point;
    }
    *(XPoint*)variant->m_data = point;
}

/* ==================== XPointF 浮点点坐标 ==================== */

void XPointF_init(XPointF* self, float x, float y)
{
    if (!self) return;
    self->x = x;
    self->y = y;
}

bool XPointF_isNull(const XPointF* self)
{
    float ax = self ? (self->x < 0.0f ? -self->x : self->x) : 0.0f;
    float ay = self ? (self->y < 0.0f ? -self->y : self->y) : 0.0f;
    return ax < 1e-6f && ay < 1e-6f;
}

XPoint XPointF_toPoint(const XPointF* self)
{
    XPoint out;
    float x = self ? self->x : 0.0f;
    float y = self ? self->y : 0.0f;
    /* Qt qRound：加 0.5 后向零方向截断（负数需先取反再截断保持一致）。 */
    if (x >= 0.0f) { out.x = (int)(x + 0.5f); }
    else { out.x = -(int)(-x + 0.5f); }
    if (y >= 0.0f) { out.y = (int)(y + 0.5f); }
    else { out.y = -(int)(-y + 0.5f); }
    return out;
}

XPointF XPointF_add(const XPointF* lhs, const XPointF* rhs)
{
    XPointF out;
    out.x = (lhs ? lhs->x : 0.0f) + (rhs ? rhs->x : 0.0f);
    out.y = (lhs ? lhs->y : 0.0f) + (rhs ? rhs->y : 0.0f);
    return out;
}

XPointF XPointF_subtract(const XPointF* lhs, const XPointF* rhs)
{
    XPointF out;
    out.x = (lhs ? lhs->x : 0.0f) - (rhs ? rhs->x : 0.0f);
    out.y = (lhs ? lhs->y : 0.0f) - (rhs ? rhs->y : 0.0f);
    return out;
}

/* ==================== XMargins 边距 ==================== */

void XMargins_init(XMargins* self, int left, int top, int right, int bottom)
{
    if (!self) return;
    self->left = left;
    self->top = top;
    self->right = right;
    self->bottom = bottom;
}

bool XMargins_isNull(const XMargins* self)
{
    return !self || (self->left == 0 && self->top == 0 &&
                     self->right == 0 && self->bottom == 0);
}

int XMargins_horizontal(const XMargins* self)
{
    return self ? self->left + self->right : 0;
}

int XMargins_vertical(const XMargins* self)
{
    return self ? self->top + self->bottom : 0;
}
