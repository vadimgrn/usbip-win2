#include <libdrv/wdf_cpp.h>

#include <usb.h>
#include <usbdlib.h>
#include <wdfusb.h>
#include <initguid.h>

#include "device.h"
#include "queue.h"
#include "trace.h"

EXTERN_C_START

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD serverEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP serverEvtDriverContextCleanup;

EXTERN_C_END
