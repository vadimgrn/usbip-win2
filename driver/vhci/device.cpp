#include "device.h"
#include "trace.h"
#include "device.tmh"

#include <usb.h>
#include <usbdlib.h>
#include <wdfusb.h>

#include "driver.h"
#include "queue.h"

#include <initguid.h>
#include <usbip\vhci.h>

namespace
{

_Function_class_(EVT_WDF_DEVICE_PREPARE_HARDWARE)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS DevicePrepareHardware(_In_ WDFDEVICE Device, _In_ WDFCMRESLIST, _In_ WDFCMRESLIST)
{
        PAGED_CODE();
        TraceMsg("%04x", ptr4log(Device));

        auto status = STATUS_SUCCESS;
        auto ctx = DeviceGetContext(Device);

        if (!ctx->UsbDevice) {
                WDF_USB_DEVICE_CREATE_CONFIG createParams;
                WDF_USB_DEVICE_CREATE_CONFIG_INIT(&createParams, USBD_CLIENT_CONTRACT_VERSION_602);

                status = WdfUsbTargetDeviceCreateWithParameters(Device, &createParams, WDF_NO_OBJECT_ATTRIBUTES, &ctx->UsbDevice);
                if (!NT_SUCCESS(status)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfUsbTargetDeviceCreateWithParameters %!STATUS!", status);
                        return status;
                }
        }

        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(&configParams, 0, nullptr);

        status = WdfUsbTargetDeviceSelectConfig(ctx->UsbDevice, WDF_NO_OBJECT_ATTRIBUTES, &configParams);
        if (!NT_SUCCESS(status)) {
                Trace(TRACE_LEVEL_ERROR, "WdfUsbTargetDeviceSelectConfig %!STATUS!", status);
                return status;
        }

        TraceMsg("%!STATUS!", status);
        return status;
}

} // namespace


_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS DriverDeviceAdd(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *DeviceInit)
{
        PAGED_CODE();

        WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

        pnpPowerCallbacks.EvtDevicePrepareHardware = DevicePrepareHardware;
        WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

        WDF_OBJECT_ATTRIBUTES deviceAttributes;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, device_context);

        WDFDEVICE device;
        auto status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
        if (!NT_SUCCESS(status)) {
                return status;
        }

        if (auto ctx = DeviceGetContext(device)) {
                ctx->PrivateDeviceData = 0;
        }

        status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_XHCI_USBIP, nullptr);
        if (NT_SUCCESS(status)) {
                status = QueueInitialize(device);
        }

        return status;
}

