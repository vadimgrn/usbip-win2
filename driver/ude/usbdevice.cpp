/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbdevice.h"
#include "trace.h"
#include "usbdevice.tmh"

#include "driver.h"
#include "network.h"
#include "vhci.h"
#include <usbip\vhci.h>

namespace
{

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbdevice_cleanup(_In_ WDFOBJECT DeviceObject)
{
        auto udev = static_cast<UDECXUSBDEVICE>(DeviceObject);
        Trace(TRACE_LEVEL_INFORMATION, "udev %04x", usbip::ptr4log(udev));
}

_Function_class_(EVT_UDECX_USB_DEVICE_DEFAULT_ENDPOINT_ADD)
_IRQL_requires_same_
NTSTATUS default_endpoint_add(_In_ UDECXUSBDEVICE /*UdecxUsbDevice*/, _In_ _UDECXUSBENDPOINT_INIT* /*UdecxEndpointInit*/)
{
        return STATUS_NOT_IMPLEMENTED;
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINT_ADD)
_IRQL_requires_same_
NTSTATUS endpoint_add(_In_ UDECXUSBDEVICE /*UdecxUsbDevice*/, _In_ UDECX_USB_ENDPOINT_INIT_AND_METADATA* /*EndpointToCreate*/)
{
        return STATUS_NOT_IMPLEMENTED;
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE)
_IRQL_requires_same_
void endpoints_configure(
        _In_ UDECXUSBDEVICE /*UdecxUsbDevice*/, _In_ WDFREQUEST /*Request*/, _In_ UDECX_ENDPOINTS_CONFIGURE_PARAMS* /*Params*/)
{
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto create_usbdevice_init(_In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed)
{
        PAGED_CODE();

        auto init = UdecxUsbDeviceInitAllocate(vhci);
        if (!init) {
                return init;
        }

        UDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS cb;
        UDECX_USB_DEVICE_CALLBACKS_INIT(&cb);

        cb.EvtUsbDeviceDefaultEndpointAdd = default_endpoint_add;
        cb.EvtUsbDeviceEndpointAdd = endpoint_add;
        cb.EvtUsbDeviceEndpointsConfigure = endpoints_configure;

        UdecxUsbDeviceInitSetStateChangeCallbacks(init, &cb);

        UdecxUsbDeviceInitSetSpeed(init, speed);
        UdecxUsbDeviceInitSetEndpointsType(init, UdecxEndpointTypeDynamic);

        return init;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::create_usbdevice(
        _Out_ UDECXUSBDEVICE &udev, _In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed)
{
        PAGED_CODE();

        auto init = create_usbdevice_init(vhci, speed);
        if (!init) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceInitAllocate error");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, usbdevice_context);
        attrs.EvtCleanupCallback = usbdevice_cleanup;

        auto err = UdecxUsbDeviceCreate(&init, &attrs, &udev);
        if (err) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceCreate %!STATUS!", err);
        } else {
                Trace(TRACE_LEVEL_INFORMATION, "udev %04x", ptr4log(udev));
        }

        UdecxUsbDeviceInitFree(init);
        return err;
}
