/**
 * @file       XMovie.c
 * @brief      XMovie 单帧/动画读取控制实现。
 */
#include "XMovie.h"
#include "XMemory.h"
#include "XVarList.h"
#include <string.h>
#include <limits.h>

struct XMoviePrivate
{
    XImageReader*       m_reader;             /**< 对象拥有的图像读取器。 */
    XImage              m_currentImage;      /**< 当前帧图像。 */
    XPixmap             m_currentPixmap;     /**< 当前帧像素图。 */
    XColor              m_backgroundColor;   /**< 当前背景色值。 */
    XRect               m_frameRect;         /**< 当前帧矩形。 */
    XSize               m_scaledSize;        /**< 读取器缩放尺寸。 */
    XString*            m_fileName;          /**< 文件名；对象拥有。 */
    XString*            m_format;            /**< 格式；对象拥有。 */
    XString*            m_errorString;      /**< 最近一次错误文本；对象拥有。 */
    XImageReaderError   m_error;             /**< 最近一次错误。 */
    XMovieState         m_state;             /**< 当前播放状态。 */
    XMovieCacheMode     m_cacheMode;         /**< 缓存策略。 */
    int                 m_speed;             /**< 播放速度百分比。 */
    int                 m_currentFrame;      /**< 当前帧编号，未加载时为 -1。 */
    int                 m_nextFrame;         /**< 下一次读取的帧编号。 */
    int                 m_greatestFrame;     /**< 已成功读取的最大帧编号。 */
    int                 m_nextDelay;         /**< 当前帧对应的下一帧延迟。 */
    int                 m_playCounter;       /**< 剩余循环次数，-1 表示尚未开始计数。 */
    bool                m_haveReadAll;       /**< 是否已经确认读完所有帧。 */
    bool                m_isFirstIteration;  /**< 是否仍处于第一次循环。 */
};

static void VXMovie_deinit(XMovie* self);
static void VXMovie_copy(XMovie* self, const XMovie* other);
static void VXMovie_move(XMovie* self, XMovie* other);

static void XMovie_clearCurrent(XMoviePrivate* data)
{
    if (!data) return;
    XImage_deinit_base(&data->m_currentImage);
    XImage_init(&data->m_currentImage);
    XPixmap_deinit_base(&data->m_currentPixmap);
    XPixmap_init(&data->m_currentPixmap);
    memset(&data->m_frameRect, 0, sizeof(data->m_frameRect));
    data->m_currentFrame = -1;
}

/*
 * Qt 6.8 QMoviePrivate::reset()（qmovie.cpp:250-263）只重置播放游标和
 * 已读帧状态，不销毁 currentPixmap 或 frameRect。这样换设备/文件后，
 * currentFrameNumber 会回到 -1，但 currentImage() 仍表示最近一次已显示的
 * 图像；同时下一次 start() 必须从第 0 帧重新请求。XMovie 没有 Qt 的定时器
 * 和 QMap 帧缓存，仍保留这些状态以保证同步跳帧接口的可观察语义一致。
 */
static void XMovie_resetPlayback(XMoviePrivate* data)
{
    if (!data) return;
    data->m_currentFrame = -1;
    data->m_nextFrame = 0;
    data->m_greatestFrame = -1;
    data->m_nextDelay = 0;
    data->m_playCounter = -1;
    data->m_haveReadAll = false;
    data->m_isFirstIteration = true;
}

static void XMovie_clearPrivate(XMoviePrivate* data)
{
    if (!data) return;
    if (data->m_reader) {
        XClass_delete_base((XClass*)data->m_reader);
        data->m_reader = NULL;
    }
    XMovie_clearCurrent(data);
    if (data->m_fileName) XString_delete_base((XClass*)data->m_fileName);
    if (data->m_format) XString_delete_base((XClass*)data->m_format);
    if (data->m_errorString) XString_delete_base((XClass*)data->m_errorString);
    data->m_fileName = NULL;
    data->m_format = NULL;
    data->m_errorString = NULL;
}

static XString* XMovie_copyString(const XString* value)
{
    return value ? XString_create_copy(value) : NULL;
}

static void XMovie_setError(XMovie* self, XImageReaderError error,
                            const XString* message)
{
    XMoviePrivate* data;
    if (!self || !(data = self->m_data)) return;
    data->m_error = error;
    if (data->m_errorString) XString_delete_base((XClass*)data->m_errorString);
    data->m_errorString = message ? XString_create_copy(message) : XString_create();
}

static void XMovie_captureReaderError(XMovie* self)
{
    XMoviePrivate* data;
    XString* message;
    if (!self || !(data = self->m_data) || !data->m_reader) return;
    message = XImageReader_errorString(data->m_reader);
    XMovie_setError(self, XImageReader_error(data->m_reader), message);
    if (message) XString_delete_base((XClass*)message);
}

static void XMovie_setState(XMovie* self, XMovieState state)
{
    if (!self || !self->m_data || self->m_data->m_state == state) return;
    self->m_data->m_state = state;
    XMovie_stateChanged_signal(self, state);
}

static XImageReader* XMovie_cloneReader(const XImageReader* source)
{
    XImageReader* copy;
    const XString* value;
    XSize size;
    XRect rect;
    if (!source) return NULL;
    copy = XImageReader_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    if (XImageReader_device(source)) {
        XImageReader_setDevice(copy, XImageReader_device(source));
    } else {
        value = XImageReader_fileName_const(source);
        if (value && !XString_isEmpty_base((const XContainer*)value))
            XImageReader_setFileName(copy, value);
    }
    value = XImageReader_format_const(source);
    if (value && !XString_isEmpty_base((const XContainer*)value)) XImageReader_setFormat(copy, value);
    XImageReader_setAutoDetectImageFormat(copy,
                                          XImageReader_autoDetectImageFormat(source));
    XImageReader_setDecideFormatFromContent(copy,
                                             XImageReader_decideFormatFromContent(source));
    XImageReader_setQuality(copy, XImageReader_quality(source));
    XImageReader_scaledSize(source, &size);
    if (size.width || size.height) XImageReader_setScaledSize(copy, &size);
    XImageReader_clipRect(source, &rect);
    if (rect.width || rect.height) XImageReader_setClipRect(copy, &rect);
    XImageReader_scaledClipRect(source, &rect);
    if (rect.width || rect.height) XImageReader_setScaledClipRect(copy, &rect);
    XImageReader_setAutoTransform(copy, XImageReader_autoTransform(source));
    return copy;
}

static int XMovie_speedAdjustedDelay(int delay, int speed)
{
    if (!speed) return 0;
    /* Qt 使用 qint64 计算后再转 int；这样 INT_MIN 等合法 speed 不会
       先在 int 乘法中溢出。 */
    return (int)(((int64_t)delay * (int64_t)100) / (int64_t)speed);
}

static bool XMovie_loadFrame(XMovie* self, int frameNumber, bool emitSignals)
{
    XMoviePrivate* data;
    XImage image;
    XRect oldRect;
    bool resized;
    int frameCount;
    bool sequential;
    if (!self || !(data = self->m_data) || !data->m_reader || frameNumber < 0)
        return false;
    frameCount = XImageReader_imageCount(data->m_reader);
    /* QMovie 支持 imageCount() 尚未知的动画格式；只有正数帧数才是
       可用于边界检查的确定值（qmovie.cpp:315-316, 549-556）。 */
    if (frameCount > 0 && frameNumber >= frameCount)
        return false;
    sequential = frameNumber == data->m_currentFrame + 1 &&
                 frameNumber == data->m_nextFrame;
    /* 连续读取时直接交给 reader->read()，避免先 jump 再 read 改变动画
       处理器的内部游标；非连续跳转才对应 Qt infoForFrame() 的
       jumpToImage() 路径（qmovie.cpp:323-365）。 */
    if (!sequential && !XImageReader_jumpToImage(data->m_reader, frameNumber)) {
        XMovie_captureReaderError(self);
        return false;
    }
    XImage_init(&image);
    if (!XImageReader_read(data->m_reader, &image)) {
        XMovie_captureReaderError(self);
        XImage_deinit_base(&image);
        return false;
    }
    oldRect = data->m_frameRect;
    XImage_deinit_base(&data->m_currentImage);
    XImage_move_base(&data->m_currentImage, &image);
    XImage_deinit_base(&image);
    XPixmap_fromImage(&data->m_currentImage, 0, &data->m_currentPixmap);
    XImage_rect(&data->m_currentImage, &data->m_frameRect);
    data->m_currentFrame = frameNumber;
    data->m_nextFrame = frameNumber == INT_MAX ? INT_MAX : frameNumber + 1;
    if (frameNumber > data->m_greatestFrame)
        data->m_greatestFrame = frameNumber;
    data->m_haveReadAll = false;
    if (XImageReader_supportsAnimation(data->m_reader)) {
        if (data->m_speed)
            data->m_nextDelay = XMovie_speedAdjustedDelay(
                XImageReader_nextImageDelay(data->m_reader), data->m_speed);
    } else {
        /* 非动画的多帧格式没有可靠的帧延迟，Qt 固定采用 1000ms。 */
        if (data->m_speed)
            data->m_nextDelay = XMovie_speedAdjustedDelay(1000, data->m_speed);
    }
    XMovie_captureReaderError(self);
    resized = oldRect.width != data->m_frameRect.width ||
              oldRect.height != data->m_frameRect.height;
    if (emitSignals && resized)
        XMovie_resized_signal(self, &(XSize){data->m_frameRect.width,
                                             data->m_frameRect.height});
    if (emitSignals) {
        /* Qt _q_loadNextFrame() 的顺序是 resized -> updated ->
           frameChanged（qmovie.cpp:482-488）。 */
        XMovie_updated_signal(self, &data->m_frameRect);
        XMovie_frameChanged_signal(self, frameNumber);
    }
    return true;
}

XVtable* XMovie_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMovie)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMovie_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMovie_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXMovie_move);
    return XVTABLE_DEFAULT;
}

XMovie* XMovie_create_ex(XMemoryType memory)
{
    XMovie* self = (XMovie*)XMemory_malloc(sizeof(XMovie), memory);
    if (!self) return NULL;
    XMovie_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XMovie_init(XMovie* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XMovie));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XMovie);
    self->m_data = (XMoviePrivate*)XMalloc_System(sizeof(XMoviePrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XMoviePrivate));
    self->m_data->m_reader = XImageReader_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XImage_init(&self->m_data->m_currentImage);
    XPixmap_init(&self->m_data->m_currentPixmap);
    self->m_data->m_backgroundColor = XColor_create();
    self->m_data->m_errorString = XString_create();
    self->m_data->m_error = XImageReaderError_UnknownError;
    self->m_data->m_state = XMovieState_NotRunning;
    self->m_data->m_cacheMode = XMovieCacheMode_CacheNone;
    self->m_data->m_speed = 100;
    self->m_data->m_currentFrame = -1;
    /* QSize 的默认值是 (-1,-1)，表示尚未请求缩放；这与 Qt
       QImageReader/QMovie 的属性初值一致。 */
    self->m_data->m_scaledSize.width = -1;
    self->m_data->m_scaledSize.height = -1;
    self->m_data->m_nextFrame = 0;
    self->m_data->m_greatestFrame = -1;
    self->m_data->m_nextDelay = 0;
    self->m_data->m_playCounter = -1;
    self->m_data->m_isFirstIteration = true;
}

void XMovie_init_device(XMovie* self, XIODevice* device, const XString* format)
{
    XMovie_init(self);
    if (!self || !self->m_data) return;
    XMovie_setDevice(self, device);
    XMovie_setFormat(self, format);
}

void XMovie_init_device_2(XMovie* self, XIODevice* device, const char* format)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    XMovie_init_device(self, device, value);
    if (value) XString_delete_base((XClass*)value);
}

void XMovie_init_file(XMovie* self, const XString* fileName, const XString* format)
{
    XMovie_init(self);
    if (!self || !self->m_data) return;
    XMovie_setFileName(self, fileName);
    XMovie_setFormat(self, format);
}

void XMovie_init_file_2(XMovie* self, const char* fileName, const char* format)
{
    XString* file = fileName ? XString_create_utf8(fileName) : NULL;
    XString* type = format ? XString_create_utf8(format) : NULL;
    XMovie_init_file(self, file, type);
    if (file) XString_delete_base((XClass*)file);
    if (type) XString_delete_base((XClass*)type);
}

XMovie* XMovie_create_device_ex(XMemoryType memory, XIODevice* device,
                                const XString* format)
{
    XMovie* self = XMovie_create_ex(memory);
    if (self) {
        XMovie_setDevice(self, device);
        XMovie_setFormat(self, format);
    }
    return self;
}

XMovie* XMovie_create_device_ex_2(XMemoryType memory, XIODevice* device,
                                  const char* format)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    XMovie* self = XMovie_create_device_ex(memory, device, value);
    if (value) XString_delete_base((XClass*)value);
    return self;
}

XMovie* XMovie_create_file_ex(XMemoryType memory, const XString* fileName,
                               const XString* format)
{
    XMovie* self = XMovie_create_ex(memory);
    if (self) {
        XMovie_setFileName(self, fileName);
        XMovie_setFormat(self, format);
    }
    return self;
}

XMovie* XMovie_create_file_ex_2(XMemoryType memory, const char* fileName,
                                const char* format)
{
    XString* file = fileName ? XString_create_utf8(fileName) : NULL;
    XString* type = format ? XString_create_utf8(format) : NULL;
    XMovie* self = XMovie_create_file_ex(memory, file, type);
    if (file) XString_delete_base((XClass*)file);
    if (type) XString_delete_base((XClass*)type);
    return self;
}

static void VXMovie_deinit(XMovie* self)
{
    if (!self) return;
    if (self->m_data) {
        XMovie_clearPrivate(self->m_data);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXMovie_copy(XMovie* self, const XMovie* other)
{
    XMoviePrivate* source;
    if (!self || !other || self == other || !(source = other->m_data)) return;
    if (XClassIsVtableNull(self)) XMovie_init(self);
    if (!self->m_data) return;
    XMovie_clearPrivate(self->m_data);
    memset(self->m_data, 0, sizeof(XMoviePrivate));
    self->m_data->m_reader = XMovie_cloneReader(source->m_reader);
    XImage_init(&self->m_data->m_currentImage);
    XImage_copy_base(&self->m_data->m_currentImage, &source->m_currentImage);
    XPixmap_init(&self->m_data->m_currentPixmap);
    XPixmap_copy_base(&self->m_data->m_currentPixmap, &source->m_currentPixmap);
    self->m_data->m_backgroundColor = source->m_backgroundColor;
    self->m_data->m_frameRect = source->m_frameRect;
    self->m_data->m_scaledSize = source->m_scaledSize;
    self->m_data->m_fileName = XMovie_copyString(source->m_fileName);
    self->m_data->m_format = XMovie_copyString(source->m_format);
    self->m_data->m_errorString = XMovie_copyString(source->m_errorString);
    self->m_data->m_error = source->m_error;
    self->m_data->m_state = source->m_state;
    self->m_data->m_cacheMode = source->m_cacheMode;
    self->m_data->m_speed = source->m_speed;
    self->m_data->m_currentFrame = source->m_currentFrame;
    self->m_data->m_nextFrame = source->m_nextFrame;
    self->m_data->m_greatestFrame = source->m_greatestFrame;
    self->m_data->m_nextDelay = source->m_nextDelay;
    self->m_data->m_playCounter = source->m_playCounter;
    self->m_data->m_haveReadAll = source->m_haveReadAll;
    self->m_data->m_isFirstIteration = source->m_isFirstIteration;
}

static void VXMovie_move(XMovie* self, XMovie* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XMovie_init(self);
    if (!self->m_data) return;
    XMovie_clearPrivate(self->m_data);
    XFree_System(self->m_data);
    self->m_data = other->m_data;
    other->m_data = NULL;
}


XStringList* XMovie_supportedFormats(void) { return XImageReader_supportedImageFormats(); }

void XMovie_setDevice(XMovie* self, XIODevice* device)
{
    if (!self || !self->m_data || !self->m_data->m_reader) return;
    XMovie_stop(self);
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = NULL;
    XImageReader_setDevice(self->m_data->m_reader, device);
    XMovie_resetPlayback(self->m_data);
    XMovie_setError(self, XImageReaderError_UnknownError, NULL);
}

XIODevice* XMovie_device(const XMovie* self)
{
    return self && self->m_data && self->m_data->m_reader
        ? XImageReader_device(self->m_data->m_reader) : NULL;
}

void XMovie_setFileName(XMovie* self, const XString* fileName)
{
    XString* copy;
    if (!self || !self->m_data || !self->m_data->m_reader) return;
    XMovie_stop(self);
    copy = fileName ? XString_create_copy(fileName) : NULL;
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = copy;
    XImageReader_setFileName(self->m_data->m_reader, fileName);
    XMovie_resetPlayback(self->m_data);
    XMovie_setError(self, XImageReaderError_UnknownError, NULL);
}

void XMovie_setFileName_2(XMovie* self, const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL;
    XMovie_setFileName(self, value);
    if (value) XString_delete_base((XClass*)value);
}

XString* XMovie_fileName(const XMovie* self)
{
    const XString* value = XMovie_fileName_const(self);
    return value ? XString_create_copy(value) : XString_create();
}
const XString* XMovie_fileName_const(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_fileName : NULL; }
const char* XMovie_fileName_2(const XMovie* self)
{ return XString_toUtf8(XMovie_fileName_const(self)); }

void XMovie_setFormat(XMovie* self, const XString* format)
{
    XString* copy;
    if (!self || !self->m_data || !self->m_data->m_reader) return;
    copy = format ? XString_create_copy(format) : NULL;
    if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
    self->m_data->m_format = copy;
    XImageReader_setFormat(self->m_data->m_reader, format);
}
void XMovie_setFormat_2(XMovie* self, const char* format)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    XMovie_setFormat(self, value);
    if (value) XString_delete_base((XClass*)value);
}
XString* XMovie_format(const XMovie* self)
{
    const XString* value = XMovie_format_const(self);
    return value ? XString_create_copy(value) : XString_create();
}
const XString* XMovie_format_const(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_format : NULL; }
const char* XMovie_format_2(const XMovie* self)
{ return XString_toUtf8(XMovie_format_const(self)); }

void XMovie_setBackgroundColor(XMovie* self, const XColor* color)
{
    if (!self || !self->m_data || !color) return;
    self->m_data->m_backgroundColor = *color;
    if (self->m_data->m_reader && XColor_isValid(color))
        XImageReader_setBackgroundColor(self->m_data->m_reader, XColor_rgba(color));
}
XColor XMovie_backgroundColor(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_backgroundColor : XColor_create(); }
XMovieState XMovie_state(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_state : XMovieState_NotRunning; }
void XMovie_frameRect(const XMovie* self, XRect* out)
{
    if (!out) return;
    if (!self || !self->m_data) memset(out, 0, sizeof(*out));
    else *out = self->m_data->m_frameRect;
}
void XMovie_currentImage(const XMovie* self, XImage* out)
{
    if (!out) return;
    if (XClassIsVtableNull(out)) XImage_init(out);
    else XImage_deinit_base(out);
    if (self && self->m_data) XImage_copy_base(out, &self->m_data->m_currentImage);
}
void XMovie_currentPixmap(const XMovie* self, XPixmap* out)
{
    if (!out) return;
    if (XClassIsVtableNull(out)) XPixmap_init(out);
    else XPixmap_deinit_base(out);
    if (self && self->m_data) XPixmap_copy_base(out, &self->m_data->m_currentPixmap);
}
bool XMovie_isValid(const XMovie* self)
{
    if (!self || !self->m_data || !self->m_data->m_reader) return false;
    /* 读出至少一帧后，即使顺序设备已经到达 EOF，QMovie 仍然有效；
       这是 QMoviePrivate::isValid() 的 greatestFrameNumber 快路径
       （qmovie.cpp:512-527）。 */
    if (self->m_data->m_greatestFrame >= 0) return true;
    return XImageReader_canRead(self->m_data->m_reader);
}
XImageReaderError XMovie_lastError(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_error : XImageReaderError_UnknownError; }
XString* XMovie_lastErrorString(const XMovie* self)
{
    const XString* value = XMovie_lastErrorString_const(self);
    return value ? XString_create_copy(value) : XString_create();
}
const XString* XMovie_lastErrorString_const(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_errorString : NULL; }
const char* XMovie_lastErrorString_2(const XMovie* self)
{ return XString_toUtf8(XMovie_lastErrorString_const(self)); }

bool XMovie_jumpToFrame(XMovie* self, int frameNumber)
{
    XMoviePrivate* data;
    if (!self || !(data = self->m_data) || frameNumber < 0) return false;
    if (data->m_currentFrame == frameNumber) return true;
    data->m_nextFrame = frameNumber;
    return XMovie_loadFrame(self, frameNumber, true);
}
int XMovie_loopCount(const XMovie* self)
{ return self && self->m_data && self->m_data->m_reader ? XImageReader_loopCount(self->m_data->m_reader) : 0; }
int XMovie_frameCount(const XMovie* self)
{
    int count;
    if (!self || !self->m_data || !self->m_data->m_reader) return 0;
    count = XImageReader_imageCount(self->m_data->m_reader);
    if (count > 0) return count;
    return self->m_data->m_haveReadAll && self->m_data->m_greatestFrame >= 0
        ? self->m_data->m_greatestFrame + 1 : 0;
}
int XMovie_nextFrameDelay(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_nextDelay : 0; }
int XMovie_currentFrameNumber(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_currentFrame : -1; }
int XMovie_speed(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_speed : 100; }
void XMovie_setSpeed(XMovie* self, int percentSpeed)
{
    /* QMovie 将 speed 作为普通 int 属性保存；Qt 测试明确覆盖 0、
       INT_MIN 和 INT_MAX（tst_qmovie.cpp:75-82），不能把负数归零。 */
    if (self && self->m_data) self->m_data->m_speed = percentSpeed;
}
void XMovie_scaledSize(const XMovie* self, XSize* out)
{
    if (!out) return;
    if (!self || !self->m_data) memset(out, 0, sizeof(*out));
    else *out = self->m_data->m_scaledSize;
}
void XMovie_setScaledSize(XMovie* self, const XSize* size)
{
    if (!self || !self->m_data || !size) return;
    self->m_data->m_scaledSize = *size;
    if (self->m_data->m_reader) XImageReader_setScaledSize(self->m_data->m_reader, size);
}
XMovieCacheMode XMovie_cacheMode(const XMovie* self)
{ return self && self->m_data ? self->m_data->m_cacheMode : XMovieCacheMode_CacheNone; }
void XMovie_setCacheMode(XMovie* self, XMovieCacheMode mode)
{ if (self && self->m_data) self->m_data->m_cacheMode = mode == XMovieCacheMode_CacheAll ? mode : XMovieCacheMode_CacheNone; }

void XMovie_start(XMovie* self)
{
    XMoviePrivate* data;
    XRect oldRect;
    bool resized;
    if (!self || !self->m_data) return;
    data = self->m_data;
    /* qmovie.cpp:944-952：运行中重复 start() 无操作，暂停状态只恢复
       播放，不重新读取第 0 帧。 */
    if (data->m_state == XMovieState_Running) return;
    if (data->m_state == XMovieState_Paused) {
        XMovie_setPaused(self, false);
        return;
    }
    /* QMovie 的第一帧由 _q_loadNextFrame(true) 读取。它先进入 Running
       并发出 started，再发出 resized/updated/frameChanged；C99 版本无事件
       循环，后续帧由 jumpToNextFrame() 驱动（qmovie.cpp:468-506）。 */
    oldRect = data->m_frameRect;
    if (!XMovie_loadFrame(self, data->m_nextFrame, false)) {
        XMovie_captureReaderError(self);
        XMovie_error_signal(self, XMovie_lastError(self));
        XMovie_finished_signal(self);
        return;
    }
    XMovie_setState(self, XMovieState_Running);
    XMovie_started_signal(self);
    resized = oldRect.width != data->m_frameRect.width ||
              oldRect.height != data->m_frameRect.height;
    if (resized)
        XMovie_resized_signal(self, &(XSize){data->m_frameRect.width,
                                             data->m_frameRect.height});
    XMovie_updated_signal(self, &data->m_frameRect);
    XMovie_frameChanged_signal(self, data->m_currentFrame);
}

bool XMovie_jumpToNextFrame(XMovie* self)
{
    XMoviePrivate* data;
    int frame;
    int count;
    int loops;
    if (!self || !(data = self->m_data) || !data->m_reader) return false;
    frame = data->m_currentFrame < 0 ? 0 : data->m_currentFrame + 1;
    count = XMovie_frameCount(self);
    if (count > 0 && frame >= count) {
        /* QMoviePrivate::next() 遇到 end marker 后按照 loopCount 决定
           是否重置到第 0 帧（qmovie.cpp:428-447）。 */
        if (data->m_isFirstIteration) {
            data->m_playCounter = XMovie_loopCount(self);
            data->m_isFirstIteration = false;
        }
        loops = data->m_playCounter;
        if (loops == 0) {
            data->m_haveReadAll = true;
            if (data->m_state != XMovieState_Paused) {
                XMovie_setState(self, XMovieState_NotRunning);
                XMovie_finished_signal(self);
            }
            return false;
        }
        if (loops > 0) --data->m_playCounter;
        frame = 0;
    }
    if (!XMovie_loadFrame(self, frame, true)) {
        XMovie_captureReaderError(self);
        if (data->m_state != XMovieState_Paused) {
            XMovie_setState(self, XMovieState_NotRunning);
            XMovie_finished_signal(self);
        }
        return false;
    }
    return true;
}
void XMovie_setPaused(XMovie* self, bool paused)
{
    if (!self || !self->m_data) return;
    if (paused) {
        if (self->m_data->m_state == XMovieState_NotRunning) return;
        if (self->m_data->m_state != XMovieState_Paused)
            XMovie_setState(self, XMovieState_Paused);
    } else {
        if (self->m_data->m_state == XMovieState_Running) return;
        XMovie_setState(self, XMovieState_Running);
    }
}
void XMovie_stop(XMovie* self)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_state == XMovieState_NotRunning) return;
    XMovie_setState(self, XMovieState_NotRunning);
    /* Qt stop() 保留当前帧，只把下一次 start() 的游标放回 0
       （qmovie.cpp:964-972）。 */
    self->m_data->m_nextFrame = 0;
}

static void XMovie_emit(XMovie* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

void* XMovie_started_signal(XMovie* self)
{
    if (!self) return (void*)(size_t)XMovie_started_signal;
    XMovie_emit(self, (size_t)XMovie_started_signal, NULL);
    return (void*)(size_t)XMovie_started_signal;
}
void* XMovie_resized_signal(XMovie* self, const XSize* size)
{
    if (!self) return (void*)(size_t)XMovie_resized_signal;
    XSize value = size ? *size : (XSize){0, 0};
    XMovie_emit(self, (size_t)XMovie_resized_signal, XVarList_Create(XVar(XSize, value)));
    return (void*)(size_t)XMovie_resized_signal;
}
void* XMovie_updated_signal(XMovie* self, const XRect* rect)
{
    if (!self) return (void*)(size_t)XMovie_updated_signal;
    XRect value = rect ? *rect : (XRect){0, 0, 0, 0};
    XMovie_emit(self, (size_t)XMovie_updated_signal, XVarList_Create(XVar(XRect, value)));
    return (void*)(size_t)XMovie_updated_signal;
}
void* XMovie_stateChanged_signal(XMovie* self, XMovieState state)
{
    if (!self) return (void*)(size_t)XMovie_stateChanged_signal;
    XMovie_emit(self, (size_t)XMovie_stateChanged_signal,
                XVarList_Create(XVar(XMovieState, state)));
    return (void*)(size_t)XMovie_stateChanged_signal;
}
void* XMovie_error_signal(XMovie* self, XImageReaderError error)
{
    if (!self) return (void*)(size_t)XMovie_error_signal;
    XMovie_emit(self, (size_t)XMovie_error_signal,
                XVarList_Create(XVar(XImageReaderError, error)));
    return (void*)(size_t)XMovie_error_signal;
}
void* XMovie_finished_signal(XMovie* self)
{
    if (!self) return (void*)(size_t)XMovie_finished_signal;
    XMovie_emit(self, (size_t)XMovie_finished_signal, NULL);
    return (void*)(size_t)XMovie_finished_signal;
}
void* XMovie_frameChanged_signal(XMovie* self, int frameNumber)
{
    if (!self) return (void*)(size_t)XMovie_frameChanged_signal;
    XMovie_emit(self, (size_t)XMovie_frameChanged_signal,
                XVarList_Create(XVar(int, frameNumber)));
    return (void*)(size_t)XMovie_frameChanged_signal;
}
