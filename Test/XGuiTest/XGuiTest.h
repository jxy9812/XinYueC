#ifndef XGUITEST_H
#define XGUITEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
    void XMenu_XGuiTest(XMenu* root);

    /* XImageFormat 测试 */
    void XMenu_XImageFormatTest(XMenu* root);

    /* XImage 测试 */
    void XMenu_XImageTest(XMenu* root);

    /* XPixmap 测试 */
    void XMenu_XPixmapTest(XMenu* root);

    /* XBitmap 测试 */
    void XMenu_XBitmapTest(XMenu* root);

    /* XIcon 测试 */
    void XMenu_XIconTest(XMenu* root);

    /* XPicture 测试 */
    void XMenu_XPictureTest(XMenu* root);

    /* XPainter 测试 */
    void XMenu_XPainterTest(XMenu* root);

    /* XPixmapCache 测试 */
    void XMenu_XPixmapCacheTest(XMenu* root);

    /* XImageIOHandler 测试 */
    void XMenu_XImageIOHandlerTest(XMenu* root);

    /* XImageReader 测试 */
    void XMenu_XImageReaderTest(XMenu* root);

    /* XImageWriter 测试 */
    void XMenu_XImageWriterTest(XMenu* root);

#if XIMAGECODEC_ON
    /* XImageCodec 测试 */
    void XMenu_XImageCodecTest(XMenu* root);
#endif
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
