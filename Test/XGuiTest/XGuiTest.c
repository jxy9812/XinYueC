#include "XGuiTest.h"
#include "XMenu.h"
#include "XAction.h"

void XMenu_XGuiTest(XMenu* root)
{
    XMenu* menu = XMenu_create("图形图像");
    XMenu_addMenu(root, menu);
    XMenu_XImageFormatTest(menu);
    XMenu_XImageTest(menu);
    XMenu_XPixmapTest(menu);
    XMenu_XBitmapTest(menu);
    XMenu_XIconTest(menu);
    XMenu_XPictureTest(menu);
    XMenu_XPainterTest(menu);
    XMenu_XPixmapCacheTest(menu);
    XMenu_XImageIOHandlerTest(menu);
    XMenu_XImageReaderTest(menu);
    XMenu_XImageWriterTest(menu);
#if XIMAGECODEC_ON
    XMenu_XImageCodecTest(menu);
#endif
}
