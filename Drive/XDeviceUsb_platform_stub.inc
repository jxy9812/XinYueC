/* ============================================================================
 * USB 平台存根实现。
 * ========================================================================== */

#ifndef XDEVICE_USB_PLATFORM_STUB_GADGET_ONLY

/* Host 控制器。 */
void* XDeviceUsbHost_platformControllerCreate(const XDeviceUsbControllerConfig* config, int* error)
{
    (void)config;
    if (error) *error = (int)XDeviceUsbError_Unsupported;
    return NULL;
}

bool XDeviceUsbHost_platformControllerOpen(void* controller,
                                       const XDeviceUsbControllerConfig* config, int* error)
{
    (void)controller;
    (void)config;
    if (error) *error = (int)XDeviceUsbError_Unsupported;
    return false;
}

void XDeviceUsbHost_platformControllerClose(void* controller)
{
    (void)controller;
}

void XDeviceUsbHost_platformControllerDelete(void* controller)
{
    (void)controller;
}

bool XDeviceUsbHost_platformControllerEnumerate(void* controller,
                                            XDeviceUsbEnumerateCallback callback,
                                            void* userData)
{
    (void)controller;
    (void)callback;
    (void)userData;
    return false;
}

bool XDeviceUsbHost_platformControllerSetHotplug(void* controller,
                                             XDeviceUsbHotplugCallback callback,
                                             void* userData)
{
    (void)controller;
    (void)callback;
    (void)userData;
    return false;
}

XDeviceUsbProcessResult XDeviceUsbHost_platformControllerProcessEvents(void* controller,
                                                                    int32_t timeoutMs)
{
    (void)controller;
    (void)timeoutMs;
    return XDeviceUsbProcessResult_Error;
}

XDeviceUsbFeatures XDeviceUsbHost_platformControllerFeatures(void* controller)
{
    (void)controller;
    return XDeviceUsbFeature_None;
}

XDeviceUsbNativeHandle XDeviceUsbHost_platformControllerHandle(void* controller)
{
    (void)controller;
    return XDEVICE_USB_INVALID_NATIVE_HANDLE;
}

XDeviceUsbError XDeviceUsbHost_platformControllerLastError(void* controller)
{
    (void)controller;
    return XDeviceUsbError_Unsupported;
}

int32_t XDeviceUsbHost_platformControllerNativeError(void* controller)
{
    (void)controller;
    return 0;
}

void XDeviceUsbHost_platformControllerClearError(void* controller)
{
    (void)controller;
}

/* Host 外接设备。 */
void* XDeviceUsbHost_platformDeviceOpen(void* controller,
                                    const XDeviceUsbDeviceSelector* selector, int* error)
{
    (void)controller;
    (void)selector;
    if (error) *error = (int)XDeviceUsbError_Unsupported;
    return NULL;
}

void XDeviceUsbHost_platformDeviceClose(void* device)
{
    (void)device;
}

bool XDeviceUsbHost_platformDeviceGetInfo(void* device, XDeviceUsbDeviceInfo* info)
{
    (void)device;
    (void)info;
    return false;
}

bool XDeviceUsbHost_platformDeviceSetConfiguration(void* device, uint8_t value)
{
    (void)device;
    (void)value;
    return false;
}

bool XDeviceUsbHost_platformDeviceGetConfiguration(void* device, uint8_t* value)
{
    (void)device;
    (void)value;
    return false;
}

bool XDeviceUsbHost_platformDeviceClaimInterface(void* device, uint8_t interfaceNumber)
{
    (void)device;
    (void)interfaceNumber;
    return false;
}

bool XDeviceUsbHost_platformDeviceReleaseInterface(void* device, uint8_t interfaceNumber)
{
    (void)device;
    (void)interfaceNumber;
    return false;
}

bool XDeviceUsbHost_platformDeviceSetAlternateSetting(void* device, uint8_t interfaceNumber,
                                                  uint8_t alternate)
{
    (void)device;
    (void)interfaceNumber;
    (void)alternate;
    return false;
}

size_t XDeviceUsbHost_platformDeviceEndpointCount(void* device, uint8_t interfaceNumber,
                                              uint8_t alternate)
{
    (void)device;
    (void)interfaceNumber;
    (void)alternate;
    return 0;
}

bool XDeviceUsbHost_platformDeviceGetEndpointInfo(void* device, uint8_t interfaceNumber,
                                              uint8_t alternate, size_t index,
                                              XDeviceUsbEndpointInfo* endpoint)
{
    (void)device;
    (void)interfaceNumber;
    (void)alternate;
    (void)index;
    (void)endpoint;
    return false;
}

bool XDeviceUsbHost_platformDeviceClearHalt(void* device, XDeviceUsbEndpointAddress endpoint)
{
    (void)device;
    (void)endpoint;
    return false;
}

bool XDeviceUsbHost_platformDeviceReset(void* device)
{
    (void)device;
    return false;
}

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceControlTransfer(
    void* device, const XDeviceUsbControlRequest* request, void* data,
    size_t capacity, size_t* transferred, int32_t timeoutMs)
{
    (void)device;
    (void)request;
    (void)data;
    (void)capacity;
    (void)transferred;
    (void)timeoutMs;
    return XDeviceUsbTransferResult_Unsupported;
}

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceTransfer(
    void* device, XDeviceUsbEndpointAddress endpoint, void* data,
    size_t length, size_t* transferred, int32_t timeoutMs)
{
    (void)device;
    (void)endpoint;
    (void)data;
    (void)length;
    (void)transferred;
    (void)timeoutMs;
    return XDeviceUsbTransferResult_Unsupported;
}

XDeviceUsbTransferId XDeviceUsbHost_platformDeviceSubmitTransfer(
    void* device, const XDeviceUsbTransferRequest* request,
    XDeviceUsbTransferCallback callback, void* userData)
{
    (void)device;
    (void)request;
    (void)callback;
    (void)userData;
    return XDEVICE_USB_INVALID_TRANSFER_ID;
}

XDeviceUsbTransferResult XDeviceUsbHost_platformDeviceCancelTransfer(void* device,
                                                                  XDeviceUsbTransferId transferId)
{
    (void)device;
    (void)transferId;
    return XDeviceUsbTransferResult_Unsupported;
}

XDeviceUsbFeatures XDeviceUsbHost_platformDeviceFeatures(void* device)
{
    (void)device;
    return XDeviceUsbFeature_None;
}

XDeviceUsbNativeHandle XDeviceUsbHost_platformDeviceHandle(void* device)
{
    (void)device;
    return XDEVICE_USB_INVALID_NATIVE_HANDLE;
}

XDeviceUsbError XDeviceUsbHost_platformDeviceLastError(void* device)
{
    (void)device;
    return XDeviceUsbError_Unsupported;
}

int32_t XDeviceUsbHost_platformDeviceNativeError(void* device)
{
    (void)device;
    return 0;
}

void XDeviceUsbHost_platformDeviceClearError(void* device)
{
    (void)device;
}

#endif /* XDEVICE_USB_PLATFORM_STUB_GADGET_ONLY */

#ifndef XDEVICE_USB_PLATFORM_STUB_HOST_ONLY

/* Device/Gadget 控制器。 */
void* XDeviceUsbGadget_platformCreate(const XDeviceUsbGadgetConfig* config, int* error)
{
    (void)config;
    if (error) *error = (int)XDeviceUsbError_Unsupported;
    return NULL;
}

bool XDeviceUsbGadget_platformOpen(void* controller,
                                   const XDeviceUsbGadgetConfig* config, int* error)
{
    (void)controller;
    (void)config;
    if (error) *error = (int)XDeviceUsbError_Unsupported;
    return false;
}

void XDeviceUsbGadget_platformClose(void* controller)
{
    (void)controller;
}

void XDeviceUsbGadget_platformDelete(void* controller)
{
    (void)controller;
}

bool XDeviceUsbGadget_platformIsStarted(void* controller)
{
    (void)controller;
    return false;
}

bool XDeviceUsbGadget_platformIsConfigured(void* controller)
{
    (void)controller;
    return false;
}

bool XDeviceUsbGadget_platformGetConfig(void* controller, XDeviceUsbGadgetConfig* config)
{
    (void)controller;
    (void)config;
    return false;
}

bool XDeviceUsbGadget_platformConfigure(void* controller,
                                        const XDeviceUsbGadgetConfig* config)
{
    (void)controller;
    (void)config;
    return false;
}

XDeviceUsbGadgetState XDeviceUsbGadget_platformStatus(void* controller)
{
    (void)controller;
    return XDeviceUsbGadgetState_Closed;
}

bool XDeviceUsbGadget_platformStart(void* controller)
{
    (void)controller;
    return false;
}

bool XDeviceUsbGadget_platformStop(void* controller)
{
    (void)controller;
    return false;
}

bool XDeviceUsbGadget_platformSetSetupCallback(void* controller,
                                               XDeviceUsbGadgetSetupCallback callback,
                                               void* userData)
{
    (void)controller;
    (void)callback;
    (void)userData;
    return false;
}

bool XDeviceUsbGadget_platformSetEventCallback(void* controller,
                                              XDeviceUsbGadgetEventCallback callback,
                                              void* userData)
{
    (void)controller;
    (void)callback;
    (void)userData;
    return false;
}

XDeviceUsbProcessResult XDeviceUsbGadget_platformProcessEvents(void* controller,
                                                                int32_t timeoutMs)
{
    (void)controller;
    (void)timeoutMs;
    return XDeviceUsbProcessResult_Error;
}

bool XDeviceUsbGadget_platformGetEndpointInfo(void* controller,
                                              XDeviceUsbGadgetEndpointInfo* endpoint)
{
    (void)controller;
    (void)endpoint;
    return false;
}

XDeviceUsbTransferResult XDeviceUsbGadget_platformTransfer(
    void* controller, XDeviceUsbEndpointAddress endpoint, void* data,
    size_t length, size_t* transferred, int32_t timeoutMs)
{
    (void)controller;
    (void)endpoint;
    (void)data;
    (void)length;
    (void)transferred;
    (void)timeoutMs;
    return XDeviceUsbTransferResult_Unsupported;
}

XDeviceUsbTransferId XDeviceUsbGadget_platformSubmitTransfer(
    void* controller, XDeviceUsbEndpointAddress endpoint, void* data,
    size_t length, int32_t timeoutMs,
    XDeviceUsbGadgetTransferCallback callback, void* userData)
{
    (void)controller;
    (void)endpoint;
    (void)data;
    (void)length;
    (void)timeoutMs;
    (void)callback;
    (void)userData;
    return XDEVICE_USB_INVALID_TRANSFER_ID;
}

XDeviceUsbTransferResult XDeviceUsbGadget_platformCancelTransfer(void* controller,
                                                                  XDeviceUsbTransferId transferId)
{
    (void)controller;
    (void)transferId;
    return XDeviceUsbTransferResult_Unsupported;
}

bool XDeviceUsbGadget_platformSetEndpointStalled(void* controller,
                                                 XDeviceUsbEndpointAddress endpoint,
                                                 bool stalled)
{
    (void)controller;
    (void)endpoint;
    (void)stalled;
    return false;
}

bool XDeviceUsbGadget_platformClearEndpointQueue(void* controller,
                                                 XDeviceUsbEndpointAddress endpoint)
{
    (void)controller;
    (void)endpoint;
    return false;
}

bool XDeviceUsbGadget_platformRemoteWakeup(void* controller)
{
    (void)controller;
    return false;
}

XDeviceUsbGadgetFeatures XDeviceUsbGadget_platformFeatures(void* controller)
{
    (void)controller;
    return XDeviceUsbGadgetFeature_None;
}

XDeviceUsbNativeHandle XDeviceUsbGadget_platformHandle(void* controller)
{
    (void)controller;
    return XDEVICE_USB_INVALID_NATIVE_HANDLE;
}

XDeviceUsbError XDeviceUsbGadget_platformLastError(void* controller)
{
    (void)controller;
    return XDeviceUsbError_Unsupported;
}

int32_t XDeviceUsbGadget_platformNativeError(void* controller)
{
    (void)controller;
    return 0;
}

void XDeviceUsbGadget_platformClearError(void* controller)
{
    (void)controller;
}

#endif /* XDEVICE_USB_PLATFORM_STUB_HOST_ONLY */
