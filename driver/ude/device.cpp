/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "network.h"
#include "vhci.h"
#include "context.h"

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
        case USB_SPEED_WIRELESS:
        case USB_SPEED_HIGH:
                return UdecxUsbHighSpeed;
        case USB_SPEED_FULL:
                return UdecxUsbFullSpeed;
        case USB_SPEED_LOW:
        case USB_SPEED_UNKNOWN:
        default:
                return UdecxUsbLowSpeed;
        }
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI device_destroy(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto udev = static_cast<UDECXUSBDEVICE>(Object);
        TraceDbg("udev %04x", ptr04x(udev));

        if (auto ctx = get_device_ctx(udev)) {
                free(ctx->ext);
        }
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void device_cleanup(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto udev = static_cast<UDECXUSBDEVICE>(Object);
        TraceDbg("udev %04x", ptr04x(udev));

        vhci::forget_device(udev);

        if (auto ctx = get_device_ctx(udev)) {
                close_socket(ctx->ext->sock);
//              cancel_pending_irps(*ctx->ext);
        }
}

_Function_class_(EVT_UDECX_USB_DEVICE_D0_ENTRY)
_IRQL_requires_same_
NTSTATUS device_d0_entry(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE udev)
{
        TraceDbg("vhci %04x, udev %04x", ptr04x(vhci), ptr04x(udev));
        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_D0_EXIT)
_IRQL_requires_same_
NTSTATUS device_d0_exit(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE udev, _In_ UDECX_USB_DEVICE_WAKE_SETTING WakeSetting)
{
        TraceDbg("vhci %04x, udev %04x, %!UDECX_USB_DEVICE_WAKE_SETTING!", ptr04x(vhci), ptr04x(udev), WakeSetting);
        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_SET_FUNCTION_SUSPEND_AND_WAKE)
_IRQL_requires_same_
NTSTATUS device_set_function_suspend_and_wake(
        _In_ WDFDEVICE vhci, 
        _In_ UDECXUSBDEVICE udev, 
        _In_ ULONG Interface, 
        _In_ UDECX_USB_DEVICE_FUNCTION_POWER FunctionPower)
{
        TraceDbg("vhci %04x, udev %04x, Interface %lu, %!UDECX_USB_DEVICE_FUNCTION_POWER!", 
                ptr04x(vhci), ptr04x(udev), Interface, FunctionPower);

        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_DEFAULT_ENDPOINT_ADD)
_IRQL_requires_same_
NTSTATUS default_endpoint_add(_In_ UDECXUSBDEVICE udev, [[maybe_unused]] _In_ _UDECXUSBENDPOINT_INIT *init)
{
        TraceDbg("udev %04x", ptr04x(udev));
        return STATUS_NOT_IMPLEMENTED;
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINT_ADD)
_IRQL_requires_same_
NTSTATUS endpoint_add(_In_ UDECXUSBDEVICE udev, [[maybe_unused]] _In_ UDECX_USB_ENDPOINT_INIT_AND_METADATA *EndpointToCreate)
{
        TraceDbg("udev %04x", ptr04x(udev));
        return STATUS_NOT_IMPLEMENTED;
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE)
_IRQL_requires_same_
void endpoints_configure(_In_ UDECXUSBDEVICE udev, [[maybe_unused]] _In_ WDFREQUEST Request, [[maybe_unused]]  _In_ UDECX_ENDPOINTS_CONFIGURE_PARAMS *Params)
{
        TraceDbg("udev %04x", ptr04x(udev));
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

        cb.EvtUsbDeviceLinkPowerEntry = device_d0_entry;
        cb.EvtUsbDeviceLinkPowerExit = device_d0_exit;

        cb.EvtUsbDeviceSetFunctionSuspendAndWake = device_set_function_suspend_and_wake; // required for USB 3 devices
//      cb.EvtUsbDeviceReset = nullptr;

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
PAGEABLE NTSTATUS usbip::device::create(_Out_ UDECXUSBDEVICE &udev, _In_ WDFDEVICE vhci, _In_ device_ctx_ext *ext)
{
        PAGED_CODE();

        NT_ASSERT(ext);
        auto speed = to_udex_speed(ext->dev.speed);

        auto init = create_init(vhci, speed); // must be freed if UdecxUsbDeviceCreate fails
        if (!init) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceInitAllocate error");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, device_ctx);
        attrs.EvtCleanupCallback = device_cleanup;
        attrs.EvtDestroyCallback = device_destroy;
        attrs.ParentObject = vhci; // FIXME: must be usb hub?

        if (auto err = UdecxUsbDeviceCreate(&init, &attrs, &udev)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceCreate %!STATUS!", err);
                UdecxUsbDeviceInitFree(init); // must never be called if success, Udecx does that itself
                return err;
        }

        if (auto ctx = get_device_ctx(udev)) {
                ctx->vhci = vhci;
                ctx->ext = ext;
                ext->ctx = ctx;
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
