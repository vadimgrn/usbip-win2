#include "device.h"
#include "trace.h"
#include "device.tmh"

#include <ntstrsafe.h>

#include <usb.h>
#include <usbiodef.h>
#include <usbdlib.h>

#include <wdfusb.h>
#include <wdfobject.h>

#include <Udecx.h>

#include "driver.h"
#include "queue.h"

#include <initguid.h>
#include <usbip\vhci.h>

namespace
{

struct request_context
{
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(request_context, RequestGetContext)
/*
auto get_device_name(_Out_ UNICODE_STRING &name, _In_ int instance_id)
{
#define BASE_DEVICE_NAME L"\\Device\\USBFDO-"

        enum { 
                maxuchar_cch = 3, // "255"
                devname_cch = ARRAYSIZE(BASE_DEVICE_NAME) + maxuchar_cch,
        };

        RtlInitUnicodeString(&name, L"");

                DECLARE_UNICODE_STRING_SIZE(DeviceName, devname_cch*sizeof(WCHAR));

                if (auto err = RtlUnicodeStringPrintf(&DeviceName, L"%ws%d", BASE_DEVICE_NAME, i)) {
                        Trace(TRACE_LEVEL_ERROR, "RtlUnicodeStringPrintf %!STATUS!", err);
                        return err;
                }
        }
}
*/

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void ControllerCleanup(_In_ WDFOBJECT)
{
}

} // namespace


_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS DriverDeviceAdd(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *DeviceInit)
{
        PAGED_CODE();

        WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
        WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

        WDF_OBJECT_ATTRIBUTES RequestAttributes;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&RequestAttributes, request_context);
        WdfDeviceInitSetRequestAttributes(DeviceInit, &RequestAttributes);

        WDF_FILEOBJECT_CONFIG fileConfig;
        WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK);

        WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

        if (auto err = UdecxInitializeWdfDeviceInit(DeviceInit)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxInitializeWdfDeviceInit %!STATUS!", err);
                return err;
        }

        WDF_OBJECT_ATTRIBUTES DeviceAttributes;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&DeviceAttributes, device_context);
        DeviceAttributes.EvtCleanupCallback = ControllerCleanup;

        WdfDeviceInitSetDeviceClass(DeviceInit, &GUID_DEVINTERFACE_USB_HOST_CONTROLLER);

#define BASE_DEVICE_NAME L"\\Device\\USBFDO-"

        enum { 
                maxuchar_cch = 3, // "255"
                devname_cch = ARRAYSIZE(BASE_DEVICE_NAME) + maxuchar_cch,
        };

        WDFDEVICE device{};

        for (ULONG i = 0; i < MAXUCHAR; ++i) {

                DECLARE_UNICODE_STRING_SIZE(DeviceName, devname_cch*sizeof(WCHAR));

                if (auto err = RtlUnicodeStringPrintf(&DeviceName, L"%ws%d", BASE_DEVICE_NAME, i)) {
                        Trace(TRACE_LEVEL_ERROR, "RtlUnicodeStringPrintf %!STATUS!", err);
                        return err;
                }

                if (auto err = WdfDeviceInitAssignName(DeviceInit, &DeviceName)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfDeviceInitAssignName %!STATUS!", err);
                        return err;
                }
                
                if (auto err = WdfDeviceCreate(&DeviceInit, &DeviceAttributes, &device)) {
                        if (err != STATUS_OBJECT_NAME_COLLISION) {
                                Trace(TRACE_LEVEL_ERROR, "WdfDeviceCreate %!STATUS!", err);
                                return err;
                        }
                }
        }

        if (auto ctx = DeviceGetContext(device)) {
                ctx->PrivateDeviceData = 0;
        }

        const GUID* guids[] = {
                &GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
                &GUID_DEVINTERFACE_XHCI_USBIP
        };

        for (auto guid: guids) {
                if (auto err = WdfDeviceCreateDeviceInterface(device, guid, nullptr)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfDeviceCreateDeviceInterface %!STATUS!", err);
                        return err;
                }
        }

        if (auto err = QueueInitialize(device)) {
                Trace(TRACE_LEVEL_ERROR, "QueueInitialize %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}
