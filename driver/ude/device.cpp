/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "network.h"
#include "vhci.h"
#include "context.h"

#include <libdrv\strutil.h>

namespace
{

using namespace usbip;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UDECXUSBDEVICE, get_udev_ctx);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto to_udex_speed(_In_ usb_device_speed speed)
{
        switch (speed) {
        case USB_SPEED_SUPER_PLUS:
        case USB_SPEED_SUPER:
                return UdecxUsbSuperSpeed;
        case USB_SPEED_FULL:
                return UdecxUsbFullSpeed;
        case USB_SPEED_LOW:
                return UdecxUsbLowSpeed;
        case USB_SPEED_HIGH:
        case USB_SPEED_WIRELESS:
        case USB_SPEED_UNKNOWN:
        default:
                return UdecxUsbHighSpeed;
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void close_socket(_Inout_ wsk::SOCKET* &sock)
{
        PAGED_CODE();

        if (!sock) {
                return;
        }

        if (auto err = event_callback_control(sock, WSK_EVENT_DISABLE | WSK_EVENT_DISCONNECT, true)) {
                Trace(TRACE_LEVEL_ERROR, "event_callback_control %!STATUS!", err);
        }

        if (auto err = disconnect(sock)) {
                Trace(TRACE_LEVEL_ERROR, "disconnect %!STATUS!", err);
        }

        if (auto err = close(sock)) {
                Trace(TRACE_LEVEL_ERROR, "close %!STATUS!", err);
        }

        sock = nullptr;
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI usbdevice_destroy(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto udev = static_cast<UDECXUSBDEVICE>(Object);
        TraceDbg("udev %04x", ptr04x(udev));

        if (auto ctx = get_device_ctx(udev)) {
                free(ctx->data);
        }
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbdevice_cleanup(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto udev = static_cast<UDECXUSBDEVICE>(Object);
        TraceDbg("udev %04x", ptr04x(udev));

        vhci::forget_device(udev);

        if (auto ctx = get_device_ctx(udev)) {
                close_socket(ctx->data->sock);
//              cancel_pending_irps(*ctx->data);
        }
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
PAGEABLE auto create_init(_In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed)
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
PAGEABLE NTSTATUS usbip::device::create(_Out_ UDECXUSBDEVICE &udev, _In_ WDFDEVICE vhci, _In_ device_ctx_data *data)
{
        PAGED_CODE();

        NT_ASSERT(data);
        auto speed = to_udex_speed(data->speed);

        auto init = create_init(vhci, speed); // must be freed if UdecxUsbDeviceCreate fails
        if (!init) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceInitAllocate error");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, device_ctx);
        attrs.EvtCleanupCallback = usbdevice_cleanup;
        attrs.EvtDestroyCallback = usbdevice_destroy;

        if (auto err = UdecxUsbDeviceCreate(&init, &attrs, &udev)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceCreate %!STATUS!", err);
                UdecxUsbDeviceInitFree(init); // must never be called if success, Udecx does that itself
                return err;
        }

        if (auto ctx = get_device_ctx(udev)) {
                ctx->vhci = vhci;
                ctx->data = data;
                data->ctx = ctx;
        }

        Trace(TRACE_LEVEL_INFORMATION, "udev %04x", ptr04x(udev));
        return STATUS_SUCCESS;
}

/*
 * UDECXUSBDEVICE must be destroyed in two steps:
 * 1.Call UdecxUsbDevicePlugOutAndDelete if UdecxUsbDevicePlugIn was successful.
 *   A device will be plugged out from a hub, but not destroyed.
 * 2.Call WdfObjectDelete to destroy it.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void usbip::device::destroy(_In_ UDECXUSBDEVICE udev)
{
        PAGED_CODE();

        auto &ctx = *get_device_ctx(udev);
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
NTSTATUS usbip::device::schedule_destroy(_In_ UDECXUSBDEVICE udev)
{
        auto func = [] (auto WorkItem)
        {
                if (auto udev = *get_udev_ctx(WorkItem)) {
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

        *get_udev_ctx(wi) = udev;
        WdfObjectReference(udev);

        WdfWorkItemEnqueue(wi);

        static_assert(NT_SUCCESS(STATUS_PENDING));
        return STATUS_PENDING;
}
