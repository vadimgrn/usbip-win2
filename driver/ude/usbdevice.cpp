/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "usbdevice.h"
#include "trace.h"
#include "usbdevice.tmh"

#include "driver.h"
#include "network.h"
#include "vhci.h"
#include "context.h"

namespace
{

using namespace usbip;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UDECXUSBDEVICE, get_udexusbdevice_context);

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbdevice_cleanup(_In_ WDFOBJECT DeviceObject)
{
        auto udev = static_cast<UDECXUSBDEVICE>(DeviceObject);
        Trace(TRACE_LEVEL_INFORMATION, "udev %04x", ptr04x(udev));

        vhci::forget_usbdevice(udev);
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
PAGEABLE NTSTATUS usbip::usbdevice::create(
        _Out_ UDECXUSBDEVICE &udev, _In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed)
{
        PAGED_CODE();

        auto init = create_usbdevice_init(vhci, speed); // must be freed if UdecxUsbDeviceCreate fails
        if (!init) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceInitAllocate error");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, usbdevice_context);
        attrs.EvtCleanupCallback = usbdevice_cleanup;

        if (auto err = UdecxUsbDeviceCreate(&init, &attrs, &udev)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceCreate %!STATUS!", err);
                UdecxUsbDeviceInitFree(init); // must never be called if success, Udecx will do that itself
                return err;
        }

        if (auto ctx = get_usbdevice_context(udev)) {
                ctx->vhci = vhci;
        }

        Trace(TRACE_LEVEL_INFORMATION, "udev %04x", ptr04x(udev));
        return STATUS_SUCCESS;
}

/*
 * UDECXUSBDEVICE must be destroyed in two steps:
 * 1.Call UdecxUsbDevicePlugOutAndDelete if UdecxUsbDevicePlugIn was successful.
 *   A device will be plugged out from a hub, but not destroyed.
 * 2.Call WdfObjectDelete to destroy it, EvtCleanupCallback will be called.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void usbip::usbdevice::destroy(_In_ UDECXUSBDEVICE udev)
{
        PAGED_CODE();

        auto &ctx = *get_usbdevice_context(udev);
        static_assert(sizeof(ctx.destroyed) == sizeof(CHAR));

        if (InterlockedExchange8(reinterpret_cast<CHAR*>(&ctx.destroyed), true)) {
                TraceDbg("udev %04x was already destroyed, port %d", ptr04x(udev), ctx.port);
                return;
        }

        Trace(TRACE_LEVEL_INFORMATION, "udev %04x, port %d", ptr04x(udev), ctx.port);

        if (auto err = UdecxUsbDevicePlugOutAndDelete(udev)) { // PASSIVE_LEVEL
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugOutAndDelete(udev=%04x) %!STATUS!", ptr04x(udev), err);
        }

        WdfObjectDelete(udev);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::usbdevice::schedule_destroy(_In_ UDECXUSBDEVICE udev)
{
        auto func = [] (auto WorkItem)
        {
                if (auto udev = *get_udexusbdevice_context(WorkItem)) {
                        destroy(udev);
                        WdfObjectDereference(udev);
                }
                WdfObjectDelete(WorkItem); // can be omitted
        };

        WDF_WORKITEM_CONFIG cfg;
        WDF_WORKITEM_CONFIG_INIT(&cfg, func);

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, UDECXUSBDEVICE);
        attrs.ParentObject = udev;

        WDFWORKITEM wi{};
        if (auto err = WdfWorkItemCreate(&cfg, &attrs, &wi)) {
                Trace(TRACE_LEVEL_ERROR, "WdfWorkItemCreate %!STATUS!", err);
                return err;
        }

        *get_udexusbdevice_context(wi) = udev;
        WdfObjectReference(udev);

        WdfWorkItemEnqueue(wi);

        static_assert(NT_SUCCESS(STATUS_PENDING));
        return STATUS_PENDING;
}
