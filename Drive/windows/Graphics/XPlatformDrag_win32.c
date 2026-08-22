/******************************************************************************
 * @file       XPlatformDrag_win32.c
 * @brief      Win32 OLE 出站拖放后端（Unicode 文本/URI 列表）。
 ******************************************************************************/
#include "XPlatformDrag.h"

#if XPLATFORMINTEGRATION_ON && XPLATFORMWINDOW_ON && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <string.h>
#include "XWindow.h"
#include "XPlatformNativeWindow.h"
#include "XMemory.h"
#include "XString.h"

struct XPlatformDrag { int unused; };

typedef struct XPDragEnum
{
    IEnumFORMATETC iface;
    LONG refs;
    UINT index;
    UINT count;
    FORMATETC formats[3];
} XPDragEnum;

typedef struct XPDragData
{
    IDataObject iface;
    LONG refs;
    const XMimeData* mime;
} XPDragData;

typedef struct XPDragSource
{
    IDropSource iface;
} XPDragSource;

static HRESULT STDMETHODCALLTYPE enum_QueryInterface(IEnumFORMATETC* self,
                                                      REFIID iid, void** out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IEnumFORMATETC)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE enum_AddRef(IEnumFORMATETC* self)
{ return (ULONG)InterlockedIncrement(&((XPDragEnum*)self)->refs); }
static ULONG STDMETHODCALLTYPE enum_Release(IEnumFORMATETC* self)
{
    XPDragEnum* e = (XPDragEnum*)self;
    ULONG refs = (ULONG)InterlockedDecrement(&e->refs);
    if (!refs) XFree_System(e);
    return refs;
}
static HRESULT STDMETHODCALLTYPE enum_Next(IEnumFORMATETC* self, ULONG count,
                                            FORMATETC* out, ULONG* fetched)
{
    XPDragEnum* e = (XPDragEnum*)self;
    ULONG copied = 0;
    if (!out || (count != 1 && !fetched)) return E_POINTER;
    while (copied < count && e->index < e->count) {
        out[copied] = e->formats[e->index++];
        ++copied;
    }
    if (fetched) *fetched = copied;
    return copied == count ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE enum_Skip(IEnumFORMATETC* self, ULONG count)
{
    XPDragEnum* e = (XPDragEnum*)self;
    e->index += count;
    if (e->index > e->count) e->index = e->count;
    return e->index < e->count ? S_OK : S_FALSE;
}
static HRESULT STDMETHODCALLTYPE enum_Reset(IEnumFORMATETC* self)
{ ((XPDragEnum*)self)->index = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE enum_Clone(IEnumFORMATETC* self,
                                             IEnumFORMATETC** out)
{
    XPDragEnum* e;
    if (!out) return E_POINTER;
    *out = NULL;
    e = (XPDragEnum*)XMalloc_System(sizeof(*e));
    if (!e) return E_OUTOFMEMORY;
    memcpy(e, self, sizeof(*e));
    e->refs = 1;
    *out = &e->iface;
    return S_OK;
}
static const IEnumFORMATETCVtbl g_enumVtbl = {
    enum_QueryInterface, enum_AddRef, enum_Release, enum_Next,
    enum_Skip, enum_Reset, enum_Clone
};

static HRESULT STDMETHODCALLTYPE data_QueryInterface(IDataObject* self,
                                                      REFIID iid, void** out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDataObject)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE data_AddRef(IDataObject* self)
{ return (ULONG)InterlockedIncrement(&((XPDragData*)self)->refs); }
static ULONG STDMETHODCALLTYPE data_Release(IDataObject* self)
{
    XPDragData* d = (XPDragData*)self;
    ULONG refs = (ULONG)InterlockedDecrement(&d->refs);
    if (!refs) XFree_System(d);
    return refs;
}

static HGLOBAL xpdrag_text(const XPDragData* d, bool wide)
{
    XString* text;
    const char* utf8;
    HGLOBAL block;
    void* dst;
    int chars;
    if (!d || !d->mime) return NULL;
    text = XMimeData_hasFormat(d->mime, "text/uri-list") ?
           XMimeData_data(d->mime, "text/uri-list") :
           XMimeData_text(d->mime);
    if (!text) return NULL;
    utf8 = XString_toUtf8(text);
    if (!utf8) { XString_delete_base((XClass*)text); return NULL; }
    if (wide) {
        chars = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
        block = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(chars > 0 ? chars : 1) * sizeof(WCHAR));
        if (block) {
            dst = GlobalLock(block);
            if (dst) MultiByteToWideChar(CP_UTF8, 0, utf8, -1,
                                          (LPWSTR)dst, chars);
            GlobalUnlock(block);
        }
    } else {
        SIZE_T bytes = strlen(utf8) + 1u;
        block = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (block) {
            dst = GlobalLock(block);
            if (dst) memcpy(dst, utf8, bytes);
            GlobalUnlock(block);
        }
    }
    XString_delete_base((XClass*)text);
    return block;
}

static HRESULT STDMETHODCALLTYPE data_GetData(IDataObject* self,
                                               FORMATETC* format, STGMEDIUM* medium)
{
    XPDragData* d = (XPDragData*)self;
    bool uri;
    if (!format || !medium) return E_POINTER;
    memset(medium, 0, sizeof(*medium));
    if (!(format->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
    uri = XMimeData_hasFormat(d->mime, "text/uri-list");
    if (format->cfFormat == CF_UNICODETEXT) {
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = xpdrag_text(d, true);
    } else if (format->cfFormat == CF_TEXT ||
               (uri && format->cfFormat == RegisterClipboardFormatA("text/uri-list"))) {
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = xpdrag_text(d, false);
    } else return DV_E_FORMATETC;
    if (!medium->hGlobal) return E_OUTOFMEMORY;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE data_GetDataHere(IDataObject* self,
                                                   FORMATETC* f, STGMEDIUM* m)
{ (void)self; (void)f; (void)m; return DATA_E_FORMATETC; }
static HRESULT STDMETHODCALLTYPE data_QueryGetData(IDataObject* self, FORMATETC* f)
{
    (void)self;
    if (!f || !(f->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
    return (f->cfFormat == CF_UNICODETEXT || f->cfFormat == CF_TEXT ||
            f->cfFormat == RegisterClipboardFormatA("text/uri-list")) ?
           S_OK : DV_E_FORMATETC;
}
static HRESULT STDMETHODCALLTYPE data_GetCanonical(IDataObject* s, FORMATETC* in, FORMATETC* out)
{ (void)s; (void)in; (void)out; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE data_SetData(IDataObject* s, FORMATETC* f, STGMEDIUM* m, BOOL rel)
{ (void)s; (void)f; (void)m; (void)rel; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE data_EnumFormatEtc(IDataObject* self, DWORD dir,
                                                    IEnumFORMATETC** out)
{
    XPDragEnum* e;
    XPDragData* d = (XPDragData*)self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (dir != DATADIR_GET) return E_NOTIMPL;
    e = (XPDragEnum*)XMalloc_System(sizeof(*e));
    if (!e) return E_OUTOFMEMORY;
    memset(e, 0, sizeof(*e));
    e->iface.lpVtbl = &g_enumVtbl;
    e->refs = 1;
    e->formats[e->count++] = (FORMATETC){CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    e->formats[e->count++] = (FORMATETC){CF_TEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    if (XMimeData_hasFormat(d->mime, "text/uri-list"))
        e->formats[e->count++] = (FORMATETC){RegisterClipboardFormatA("text/uri-list"), NULL,
                                             DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    (void)d;
    *out = &e->iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE data_Advise(IDataObject* s, FORMATETC* f, DWORD a,
                                              BOOL r, IAdviseSink* n, DWORD* c)
{ (void)s; (void)f; (void)a; (void)r; (void)n; (void)c; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE data_Unadvise(IDataObject* s, DWORD c)
{ (void)s; (void)c; return OLE_E_ADVISENOTSUPPORTED; }
static HRESULT STDMETHODCALLTYPE data_EnumAdvise(IDataObject* s, IEnumSTATDATA** e)
{ (void)s; (void)e; return OLE_E_ADVISENOTSUPPORTED; }
static const IDataObjectVtbl g_dataVtbl = {
    data_QueryInterface, data_AddRef, data_Release, data_GetData,
    data_GetDataHere, data_QueryGetData, data_GetCanonical, data_SetData,
    data_EnumFormatEtc, data_Advise, data_Unadvise, data_EnumAdvise
};

static HRESULT STDMETHODCALLTYPE source_QueryInterface(IDropSource* self,
                                                         REFIID iid, void** out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDropSource)) {
        *out = self; return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE source_AddRef(IDropSource* self) { (void)self; return 1; }
static ULONG STDMETHODCALLTYPE source_Release(IDropSource* self) { (void)self; return 1; }
static HRESULT STDMETHODCALLTYPE source_QueryContinueDrag(IDropSource* self,
                                                            BOOL escape, DWORD keys)
{
    (void)self;
    if (escape) return DRAGDROP_S_CANCEL;
    if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE source_GiveFeedback(IDropSource* self, DWORD effect)
{ (void)self; (void)effect; return DRAGDROP_S_USEDEFAULTCURSORS; }
static const IDropSourceVtbl g_sourceVtbl = {
    source_QueryInterface, source_AddRef, source_Release,
    source_QueryContinueDrag, source_GiveFeedback
};

XPlatformDrag* XPlatformDrag_create(void)
{
    XPlatformDrag* drag = (XPlatformDrag*)XMalloc_System(sizeof(*drag));
    if (drag) memset(drag, 0, sizeof(*drag));
    return drag;
}
void XPlatformDrag_delete(XPlatformDrag* self) { if (self) XFree_System(self); }
bool XPlatformDrag_isAvailable(const XPlatformDrag* self)
{ (void)self; return true; }
XPlatformDragResult XPlatformDrag_exec(XPlatformDrag* self, XWindow* source,
                                       const XMimeData* mime, uint32_t actions)
{
    XPDragData* data;
    XPDragSource sourceObj;
    DWORD effect = DROPEFFECT_NONE;
    HRESULT hr;
    int coinit;
    (void)self; (void)source;
#if !XMIMEDATA_ON
    (void)mime; (void)actions;
    return XPlatformDragResult_Unsupported;
#else
    if (!mime || !actions || (!XMimeData_hasText(mime) &&
                              !XMimeData_hasFormat(mime, "text/uri-list")))
        return XPlatformDragResult_Cancelled;
    coinit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(coinit) && coinit != RPC_E_CHANGED_MODE) return XPlatformDragResult_Cancelled;
    data = (XPDragData*)XMalloc_System(sizeof(*data));
    if (!data) { if (coinit == S_OK) CoUninitialize(); return XPlatformDragResult_Cancelled; }
    memset(data, 0, sizeof(*data));
    data->iface.lpVtbl = &g_dataVtbl;
    data->refs = 1;
    data->mime = mime;
    memset(&sourceObj, 0, sizeof(sourceObj));
    sourceObj.iface.lpVtbl = &g_sourceVtbl;
    hr = DoDragDrop(&data->iface, &sourceObj,
                    ((actions & XPlatformDragAction_Copy) ? DROPEFFECT_COPY : 0) |
                    ((actions & XPlatformDragAction_Move) ? DROPEFFECT_MOVE : 0) |
                    ((actions & XPlatformDragAction_Link) ? DROPEFFECT_LINK : 0),
                    &effect);
    data->iface.lpVtbl->Release(&data->iface);
    if (coinit == S_OK) CoUninitialize();
    if (hr != DRAGDROP_S_DROP) return XPlatformDragResult_Cancelled;
    if (effect & DROPEFFECT_MOVE) return XPlatformDragResult_Moved;
    if (effect & DROPEFFECT_LINK) return XPlatformDragResult_Linked;
    return XPlatformDragResult_Copied;
#endif
}

#endif
