#include "vhci.h"
#include "trace.h"
#include "vhci.tmh"

#include <ntstrsafe.h>

#include <usb.h>
#include <usbdlib.h>
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
        auto vhci = static_cast<WDFDEVICE>(DeviceObject);
        Trace(TRACE_LEVEL_INFORMATION, "vhci %04x", ptr4log(vhci));
}

_Function_class_(EVT_UDECX_WDF_DEVICE_QUERY_USB_CAPABILITY)
_IRQL_requires_same_
NTSTATUS query_usb_capability(
        _In_ WDFDEVICE /*UdecxWdfDevice*/,
        _In_ GUID *CapabilityType,
        _In_ ULONG /*OutputBufferLength*/,
        _Out_writes_to_opt_(OutputBufferLength, *ResultLength) PVOID /*OutputBuffer*/,
        _Out_ ULONG *ResultLength)
{
        *ResultLength = 0;
        auto st = STATUS_NOT_IMPLEMENTED;

        if (*CapabilityType == GUID_USB_CAPABILITY_CHAINED_MDLS) {
                st = STATUS_SUCCESS;
        } else if (*CapabilityType == GUID_USB_CAPABILITY_SELECTIVE_SUSPEND) {
                st = STATUS_NOT_SUPPORTED;
        } else if (*CapabilityType == GUID_USB_CAPABILITY_FUNCTION_SUSPEND) {
                st = STATUS_NOT_SUPPORTED;
        } else if (*CapabilityType == GUID_USB_CAPABILITY_DEVICE_CONNECTION_HIGH_SPEED_COMPATIBLE) {
                st = STATUS_SUCCESS;
        } else if (*CapabilityType == GUID_USB_CAPABILITY_DEVICE_CONNECTION_SUPER_SPEED_COMPATIBLE) {
                st = STATUS_SUCCESS;
        }

        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto setup(_Inout_ WDFDEVICE_INIT *DeviceInit)
{
        PAGED_CODE();

        WDF_PNPPOWER_EVENT_CALLBACKS pnp_power;
        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp_power);
        WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp_power);

        WDF_FILEOBJECT_CONFIG fileobj_cfg;
        WDF_FILEOBJECT_CONFIG_INIT(&fileobj_cfg, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK);
        WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileobj_cfg, WDF_NO_OBJECT_ATTRIBUTES);

        WDF_OBJECT_ATTRIBUTES request_attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&request_attrs, request_context);
        WdfDeviceInitSetRequestAttributes(DeviceInit, &request_attrs);

        return UdecxInitializeWdfDeviceInit(DeviceInit);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto create_device(_Out_ WDFDEVICE &vhci, _In_ WDFDEVICE_INIT *DeviceInit)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, vhci_context);
        attrs.EvtCleanupCallback = vhci_cleanup;

        if (auto err = WdfDeviceCreate(&DeviceInit, &attrs, &vhci)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDeviceCreate %!STATUS!", err);
                return err;
        }

        if (auto ctx = get_vhci_context(vhci)) {
                ctx->vhci = reinterpret_cast<WDFUSBDEVICE>(vhci);
        }

        return create_interfaces(vhci);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto add_usb_device_emulation(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        UDECX_WDF_DEVICE_CONFIG cfg;
        UDECX_WDF_DEVICE_CONFIG_INIT(&cfg, query_usb_capability);

        cfg.NumberOfUsb20Ports = VHUB_NUM_PORTS;
        cfg.NumberOfUsb30Ports = VHUB_NUM_PORTS;

        if (auto err = UdecxWdfDeviceAddUsbDeviceEmulation(vhci, &cfg)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxWdfDeviceAddUsbDeviceEmulation %!STATUS!", err);
                return err;
        }

        return queue_initialize(vhci);
}

} // namespace


_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS DriverDeviceAdd(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *DeviceInit)
{
        PAGED_CODE();

        if (auto err = setup(DeviceInit)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxInitializeWdfDeviceInit %!STATUS!", err);
                return err;
        }

        WDFDEVICE vhci;
        if (auto err = create_device(vhci, DeviceInit)) {
                return err;
        }

        if (auto err = add_usb_device_emulation(vhci)) {
                return err;
        }

        Trace(TRACE_LEVEL_INFORMATION, "vhci %04x", ptr4log(vhci));
        return STATUS_SUCCESS;
}
