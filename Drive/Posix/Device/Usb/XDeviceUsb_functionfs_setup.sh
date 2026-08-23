#!/bin/sh
set -eu

# Creates the configfs part required by XDeviceUsb_gadget_linux.c.
# The application still writes FunctionFS descriptors through ep0.

GADGET_NAME=${1:-xinyuec}
FUNCTION_NAME=${2:-xinyuec}
VID=${3:-0x1d50}
PID=${4:-0x615e}
UDC_NAME=${5:-}
CONFIGFS=/sys/kernel/config
GADGET="$CONFIGFS/usb_gadget/$GADGET_NAME"
FUNCTION="ffs.$FUNCTION_NAME"
FFS_ROOT="/dev/ffs/$FUNCTION_NAME"

if [ "$(id -u)" -ne 0 ]; then
    echo "run as root" >&2
    exit 1
fi

modprobe libcomposite 2>/dev/null || true
mountpoint -q "$CONFIGFS" || mount -t configfs none "$CONFIGFS"
mkdir -p "$GADGET"
printf '%s' "$VID" > "$GADGET/idVendor"
printf '%s' "$PID" > "$GADGET/idProduct"
printf '%s' 0x0200 > "$GADGET/bcdUSB"

mkdir -p "$GADGET/strings/0x409"
printf '%s' XinYueC > "$GADGET/strings/0x409/manufacturer"
printf '%s' XDeviceUsb > "$GADGET/strings/0x409/product"
printf '%s' "$GADGET_NAME" > "$GADGET/strings/0x409/serialnumber"

mkdir -p "$GADGET/configs/c.1/strings/0x409"
printf '%s' "XDeviceUsb $FUNCTION_NAME" > "$GADGET/configs/c.1/strings/0x409/configuration"
printf '%s' 120 > "$GADGET/configs/c.1/MaxPower"

mkdir -p "$CONFIGFS/usb_gadget/$GADGET_NAME/functions/$FUNCTION"
if [ ! -e "$GADGET/configs/c.1/$FUNCTION" ]; then
    ln -s "$GADGET/functions/$FUNCTION" "$GADGET/configs/c.1/$FUNCTION"
fi

mkdir -p /dev/ffs
mountpoint -q "$FFS_ROOT" || mount -t functionfs "$FUNCTION_NAME" "$FFS_ROOT"

if [ -n "$UDC_NAME" ]; then
    printf '%s' "$UDC_NAME" > "$GADGET/UDC"
fi

echo "FunctionFS ready: $FFS_ROOT"
echo "Use XDeviceUsbGadgetChannel.m_name = $FFS_ROOT"
if [ -z "$UDC_NAME" ]; then
    echo "Bind later with: echo <udc-name> > $GADGET/UDC"
fi
