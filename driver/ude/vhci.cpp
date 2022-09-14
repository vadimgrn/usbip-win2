#include "vhci.h"
#include "trace.h"
#include "vhci.tmh"

#include <ntstrsafe.h>

#include <usb.h>
#include <usbiodef.h>

#include <wdfusb.h>
#include <Udecx.h>

#include "driver.h"
#include "queue.h"

#include <initguid.h>
#include <usbip\vhci.h>

namespace
{

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
auto create_interfaces(_In_ WDFDEVICE vhci)
{
        const GUID* v[] = {
                &GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
                &GUID_DEVINTERFACE_XHCI_USBIP
        };

        for (auto guid: v) {
                if (auto err = WdfDeviceCreateDeviceInterface(vhci, guid, nullptr)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfDeviceCreateDeviceInterface(%!GUID!) %!STATUS!", guid, err);
                        return err;
                }
        }

        return STATUS_SUCCESS;
}

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void vhci_cleanup(_In_ WDFOBJECT DeviceObject)
{
        [[maybe_unused]] auto hub = static_cast<WDFDEVICE>(DeviceObject);
}

_Function_class_(EVT_UDECX_WDF_DEVICE_QUERY_USB_CAPABILITY)
_IRQL_requires_same_
NTSTATUS query_usb_capability(
        _In_ WDFDEVICE /*UdecxWdfDevice*/,
        _In_ PGUID /*CapabilityType*/,
        _In_ ULONG /*OutputBufferLength*/,
        _Out_writes_to_opt_(OutputBufferLength, *ResultLength) PVOID /*OutputBuffer*/,
        _Out_ PULONG /*ResultLength*/)
{
        return STATUS_NOT_IMPLEMENTED;
}

} // namespace


_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS DriverDeviceAdd(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *DeviceInit)
{
        PAGED_CODE();

        WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;
        WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPowerCallbacks);

        WDF_OBJECT_ATTRIBUTES request_attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&request_attrs, request_context);
        WdfDeviceInitSetRequestAttributes(DeviceInit, &request_attrs);

        WDF_FILEOBJECT_CONFIG file_cfg;
        WDF_FILEOBJECT_CONFIG_INIT(&file_cfg, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK);
        WdfDeviceInitSetFileObjectConfig(DeviceInit, &file_cfg, WDF_NO_OBJECT_ATTRIBUTES);

        if (auto err = UdecxInitializeWdfDeviceInit(DeviceInit)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxInitializeWdfDeviceInit %!STATUS!", err);
                return err;
        }

        WDF_OBJECT_ATTRIBUTES vhci_attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&vhci_attrs, vhci_context);
        vhci_attrs.EvtCleanupCallback = vhci_cleanup;

        WDFDEVICE vhci;
        if (auto err = WdfDeviceCreate(&DeviceInit, &vhci_attrs, &vhci)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDeviceCreate %!STATUS!", err);
                return err;
        }

        if (auto ctx = get_vhci_context(vhci)) {
                ctx->vhci = reinterpret_cast<WDFUSBDEVICE>(vhci);
        }

        if (auto err = create_interfaces(vhci)) {
                return err;
        }

        UDECX_WDF_DEVICE_CONFIG vhci_cfg;
        UDECX_WDF_DEVICE_CONFIG_INIT(&vhci_cfg, query_usb_capability);

        if (auto err = UdecxWdfDeviceAddUsbDeviceEmulation(vhci, &vhci_cfg)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxWdfDeviceAddUsbDeviceEmulation %!STATUS!", err);
                return err;
        }

        if (auto err = QueueInitialize(vhci)) {
                return err;
        }

//        if (auto err = Usb_Initialize(hub)) {
//                return err;
//        }

        return STATUS_SUCCESS;
}
