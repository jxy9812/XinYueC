#include "XDrawingAnchor.h"
#include "XMemory.h"
#include <stdlib.h>#include <string.h>
XDrawingAnchor* XDrawingAnchor_create(XDrawing* drawing, XDrawingAnchor_ObjectType objectType) {
    XDrawingAnchor* self = (XDrawingAnchor*)XMalloc_System(sizeof(XDrawingAnchor));
    if (!self) return NULL; memset(self, 0, sizeof(XDrawingAnchor));
    self->m_drawing = drawing; self->m_objectType = objectType;
    static int s_id = 1; self->m_id = s_id++;
    return self;
}
void XDrawingAnchor_delete(XDrawingAnchor* self) { if (self) XFree_System(self); }
void XDrawingAnchor_setPicture(XDrawingAnchor* self, const char* imagePath) { (void)self; (void)imagePath; }
void XDrawingAnchor_setChart(XDrawingAnchor* self, XChart* chart) { if (self) self->m_chartFile = chart; }
int XDrawingAnchor_row(const XDrawingAnchor* self) { (void)self; return 0; }
int XDrawingAnchor_col(const XDrawingAnchor* self) { (void)self; return 0; }
int XDrawingAnchor_id(XDrawingAnchor* self) { return self ? self->m_id : -1; }
