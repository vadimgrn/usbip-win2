/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "device_queue.h"
#include "context.h"
#include "network.h"
#include "vhci.h"
#include "device_ioctl.h"
#include "wsk_context.h"
#include "wsk_receive.h"
#include "ioctl.h"

#include <libdrv\ch9.h>
#include <libdrv\dbgcommon.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto to_udex_speed(_In_ usb_device_speed speed)
{
        PAGED_CODE();

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

/*
 * The socket is closed, there is no concurrency with send_complete from device_ioctl.cpp
 * @see device::send_cmd_unlink
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void cancel_pending_requests(_Inout_ device_ctx &dev)
{
        PAGED_CODE();
        NT_ASSERT(!dev.sock());

        for (NTSTATUS err{}; !err; ) {
                switch (WDFREQUEST request;  err = WdfIoQueueRetrieveNextRequest(dev.queue, &request)) {
                case STATUS_SUCCESS:
                        complete(request, STATUS_CANCELLED);
                        break;
                case STATUS_NO_MORE_ENTRIES:
                        break;
                default:
                        Trace(TRACE_LEVEL_ERROR, "WdfIoQueueRetrieveNextRequest %!STATUS!", err);
                }
        }
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void NTAPI device_destroy(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto dev = static_cast<UDECXUSBDEVICE>(Object);
        TraceDbg("dev %04x", ptr04x(dev));

        auto &ctx = *get_device_ctx(dev);

        WdfObjectDelete(ctx.queue); // see device::create_queue
        free(ctx.ext);
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void device_cleanup(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto dev = static_cast<UDECXUSBDEVICE>(Object);
        auto &ctx = *get_device_ctx(dev);

        TraceDbg("dev %04x", ptr04x(dev));

        vhci::reclaim_roothub_port(dev);

        close_socket(ctx.sock());
        cancel_pending_requests(ctx);
}

_Function_class_(EVT_UDECX_USB_ENDPOINT_RESET)
_IRQL_requires_same_
void endpoint_reset(_In_ UDECXUSBENDPOINT endp, _In_ WDFREQUEST request)
{
        NT_ASSERT(!has_urb(request));
        TraceDbg("endp %04x, req %04x", ptr04x(endp), ptr04x(request));

        auto st = device::clear_endpoint_stall(endp, request);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "endp %04x, %!STATUS!", ptr04x(endp), st);
                WdfRequestComplete(request, st);
        }
}

_Function_class_(EVT_WDF_IO_QUEUE_STATE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI purge_complete(_In_ WDFQUEUE queue, _In_ WDFCONTEXT)
{
        auto endp = get_endpoint(queue);
        TraceDbg("endp %04x, queue %04x", ptr04x(endp), ptr04x(queue));
        UdecxUsbEndpointPurgeComplete(endp);
}

_Function_class_(EVT_UDECX_USB_ENDPOINT_PURGE)
_IRQL_requires_same_
void endpoint_purge(_In_ UDECXUSBENDPOINT endp)
{
        auto queue = get_endpoint_ctx(endp)->queue;
        TraceDbg("endp %04x, queue %04x", ptr04x(endp), ptr04x(queue));
        WdfIoQueuePurge(queue, purge_complete, WDF_NO_CONTEXT);
}

_Function_class_(EVT_UDECX_USB_ENDPOINT_START)
_IRQL_requires_same_
void endpoint_start(_In_ UDECXUSBENDPOINT endp)
{
        auto queue = get_endpoint_ctx(endp)->queue;
        TraceDbg("endp %04x, queue %04x", ptr04x(endp), ptr04x(queue));
        WdfIoQueueStart(queue);
}

_Function_class_(EVT_UDECX_USB_DEVICE_D0_ENTRY)
_IRQL_requires_same_
NTSTATUS device_d0_entry(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev)
{
        TraceDbg("vhci %04x, dev %04x", ptr04x(vhci), ptr04x(dev));
        return STATUS_NOT_IMPLEMENTED;
}

_Function_class_(EVT_UDECX_USB_DEVICE_D0_EXIT)
_IRQL_requires_same_
NTSTATUS device_d0_exit(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev, _In_ UDECX_USB_DEVICE_WAKE_SETTING WakeSetting)
{
        TraceDbg("vhci %04x, dev %04x, %!UDECX_USB_DEVICE_WAKE_SETTING!", ptr04x(vhci), ptr04x(dev), WakeSetting);
        return STATUS_NOT_IMPLEMENTED;
}

_Function_class_(EVT_UDECX_USB_DEVICE_SET_FUNCTION_SUSPEND_AND_WAKE)
_IRQL_requires_same_
NTSTATUS device_set_function_suspend_and_wake(
        _In_ WDFDEVICE vhci, 
        _In_ UDECXUSBDEVICE dev, 
        _In_ ULONG Interface, 
        _In_ UDECX_USB_DEVICE_FUNCTION_POWER FunctionPower)
{
        TraceDbg("vhci %04x, dev %04x, Interface %lu, %!UDECX_USB_DEVICE_FUNCTION_POWER!", 
                  ptr04x(vhci), ptr04x(dev), Interface, FunctionPower);

        return STATUS_NOT_IMPLEMENTED;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_endpoint_queue(_Inout_ WDFQUEUE &queue, _In_ UDECXUSBENDPOINT endp)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT(&cfg, WdfIoQueueDispatchParallel);
        cfg.EvtIoInternalDeviceControl = device::internal_device_control;

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, UDECXUSBENDPOINT);
        attrs.EvtCleanupCallback = [] (auto obj) { TraceDbg("Queue %04x cleanup", ptr04x(obj)); };
//      attrs.SynchronizationScope = WdfSynchronizationScopeQueue; // EvtIoInternalDeviceControl is used only
        attrs.ParentObject = endp;

        auto dev = get_endpoint_ctx(endp)->device;
        auto vhci = get_device_ctx(dev)->vhci;

        if (auto err = WdfIoQueueCreate(vhci, &cfg, &attrs, &queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }
        get_endpoint(queue) = endp;

        UdecxUsbEndpointSetWdfIoQueue(endp, queue);
        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINT_ADD)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS endpoint_add(_In_ UDECXUSBDEVICE dev, _In_ UDECX_USB_ENDPOINT_INIT_AND_METADATA *data)
{
        PAGED_CODE();

        auto epd = data->EndpointDescriptor; // NULL if default control pipe is adding
        auto bEndpointAddress = epd ? epd->bEndpointAddress : UCHAR(USB_DEFAULT_ENDPOINT_ADDRESS);

        UdecxUsbEndpointInitSetEndpointAddress(data->UdecxUsbEndpointInit, bEndpointAddress);

        UDECX_USB_ENDPOINT_CALLBACKS cb;
        UDECX_USB_ENDPOINT_CALLBACKS_INIT(&cb, endpoint_reset);
        cb.EvtUsbEndpointStart = endpoint_start;
        cb.EvtUsbEndpointPurge = endpoint_purge;

        UdecxUsbEndpointInitSetCallbacks(data->UdecxUsbEndpointInit, &cb);

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, endpoint_ctx);
        attrs.EvtCleanupCallback = [] (auto obj) { TraceDbg("Endpoint %04x cleanup", ptr04x(obj)); }; 
        attrs.ParentObject = dev;

        UDECXUSBENDPOINT endp;
        if (auto err = UdecxUsbEndpointCreate(&data->UdecxUsbEndpointInit, &attrs, &endp)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbEndpointCreate %!STATUS!", err);
                return err;
        }

        auto &ctx = *get_endpoint_ctx(endp);
        ctx.device = dev;

        if (epd) {
                ctx.descriptor = *epd;
        } else {
                auto &d = ctx.descriptor;

                d.bLength = sizeof(d);
                d.bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
                NT_ASSERT(usb_default_control_pipe(d));

                get_device_ctx(dev)->ep0 = endp;
        }

        if (auto err = create_endpoint_queue(ctx.queue, endp)) {
                return err;
        }

        {
                auto &d = ctx.descriptor;
                TraceDbg("dev %04x, endp %04x{Address %#04x: %s %s[%d], Interval %d}, queue %04x", 
                        ptr04x(dev), ptr04x(endp), d.bEndpointAddress, 
                        usbd_pipe_type_str(usb_endpoint_type(d)),
                        usb_endpoint_dir_out(d) ? "Out" : "In", 
                        usb_endpoint_num(d), d.bInterval, ptr04x(ctx.queue));
        }

        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_DEFAULT_ENDPOINT_ADD)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS default_endpoint_add(_In_ UDECXUSBDEVICE dev, _In_ _UDECXUSBENDPOINT_INIT *init)
{
        PAGED_CODE();
        UDECX_USB_ENDPOINT_INIT_AND_METADATA data{ init };
        return endpoint_add(dev, &data);
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE)
_IRQL_requires_same_
void endpoints_configure(
        _In_ UDECXUSBDEVICE dev, _In_ WDFREQUEST request, _In_ UDECX_ENDPOINTS_CONFIGURE_PARAMS *params)
{
        NT_ASSERT(!has_urb(request));
        auto st = STATUS_SUCCESS; 

        switch (params->ConfigureType) {
        case UdecxEndpointsConfigureTypeDeviceInitialize:
                TraceDbg("dev %04x, ToConfigure[%lu]%!BIN!", ptr04x(dev), params->EndpointsToConfigureCount, 
                          WppBinary(params->EndpointsToConfigure,
                                    USHORT(params->EndpointsToConfigureCount*sizeof(*params->EndpointsToConfigure))));
                break;
        case UdecxEndpointsConfigureTypeEndpointsReleasedOnly:
                TraceDbg("dev %04x, Released[%lu]%!BIN!", ptr04x(dev), params->ReleasedEndpointsCount, 
                          WppBinary(params->ReleasedEndpoints, 
                                    USHORT(params->ReleasedEndpointsCount*sizeof(*params->ReleasedEndpoints))));
                break;
        case UdecxEndpointsConfigureTypeDeviceConfigurationChange:
                st = device::select_configuration(dev, request, params->NewConfigurationValue);
                break;
        case UdecxEndpointsConfigureTypeInterfaceSettingChange:
                st = device::select_interface(dev, request, params->InterfaceNumber, params->NewInterfaceSetting);
                break;
        }

        if (st != STATUS_PENDING) {
                WdfRequestComplete(request, st);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_init(_In_ WDFDEVICE vhci, _In_ UDECX_USB_DEVICE_SPEED speed)
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

        cb.EvtUsbDeviceDefaultEndpointAdd = default_endpoint_add;
        cb.EvtUsbDeviceEndpointAdd = endpoint_add;
        cb.EvtUsbDeviceEndpointsConfigure = endpoints_configure;

        UdecxUsbDeviceInitSetStateChangeCallbacks(init, &cb);

        UdecxUsbDeviceInitSetSpeed(init, speed);
        UdecxUsbDeviceInitSetEndpointsType(init, UdecxEndpointTypeDynamic);

        return init;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto init_device(_In_ UDECXUSBDEVICE dev, _Inout_ device_ctx &ctx)
{
        PAGED_CODE();

        if (auto err = init_receive_usbip_header(ctx)) {
                return err;
        }

        if (auto err = device::create_queue(dev)) {
                return err;
        }

        sched_receive_usbip_header(ctx);
        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::device::create(_Out_ UDECXUSBDEVICE &dev, _In_ WDFDEVICE vhci, _In_ device_ctx_ext *ext)
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
        attrs.ParentObject = vhci; // FIXME: by default?

        if (auto err = UdecxUsbDeviceCreate(&init, &attrs, &dev)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceCreate %!STATUS!", err);
                UdecxUsbDeviceInitFree(init); // must never be called if success, Udecx does that itself
                return err;
        }

        auto &ctx = *get_device_ctx(dev);

        ctx.vhci = vhci;
        ctx.ext = ext;
        ext->ctx = &ctx;

        if (auto err = init_device(dev, ctx)) {
                return err;
        }

        Trace(TRACE_LEVEL_INFORMATION, "dev %04x", ptr04x(dev));
        return STATUS_SUCCESS;
}

/*
 * UDECXUSBDEVICE must be destroyed in two steps:
 * 1.Call UdecxUsbDevicePlugOutAndDelete if UdecxUsbDevicePlugIn was successful.
 *   A device will be plugged out from a hub, but not destroyed.
 * 2.Call WdfObjectDelete to destroy it.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::destroy(_In_ UDECXUSBDEVICE dev)
{
        auto &ctx = *get_device_ctx(dev);
        static_assert(sizeof(ctx.destroyed) == sizeof(CHAR));

        if (InterlockedExchange8(reinterpret_cast<CHAR*>(&ctx.destroyed), true)) {
                TraceDbg("dev %04x was already destroyed, port %d", ptr04x(dev), ctx.port);
                return;
        }

        Trace(TRACE_LEVEL_INFORMATION, "dev %04x, port %d", ptr04x(dev), ctx.port);

        if (auto err = UdecxUsbDevicePlugOutAndDelete(dev)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugOutAndDelete(dev=%04x) %!STATUS!", ptr04x(dev), err);
        }

        WdfObjectDelete(dev);
}
