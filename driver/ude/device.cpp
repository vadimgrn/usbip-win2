/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "driver.h"
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
#include <libdrv\usbdsc.h>

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

_Function_class_(EVT_WDF_DEVICE_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void NTAPI device_destroy(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto dev = static_cast<UDECXUSBDEVICE>(Object);
        TraceDbg("dev %04x", ptr04x(dev));

        auto &ctx = *get_device_ctx(dev);

        if (auto ptr = ctx.actconfig) {
                ExFreePoolWithTag(ptr, POOL_TAG);
        }

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

        NT_ASSERT(WDF_IO_QUEUE_PURGED(WdfIoQueueGetState(ctx.queue, nullptr, nullptr)));
}

/*
 * @see UDECX_WDF_DEVICE_CONFIG.UDECX_WDF_DEVICE_RESET_ACTION, 
 *      default is UdecxWdfDeviceResetActionResetEachUsbDevice. 
 */
_Function_class_(EVT_UDECX_USB_DEVICE_POST_ENUMERATION_RESET)
_IRQL_requires_same_
inline void device_reset(
        _In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev, _In_ WDFREQUEST request, _In_ BOOLEAN AllDevicesReset)
{
        if (AllDevicesReset) {
                Trace(TRACE_LEVEL_ERROR, "vhci %04x, dev %04x, AllDevicesReset(true) is not supported", 
                        ptr04x(vhci), ptr04x(dev));

                WdfRequestComplete(request, STATUS_NOT_SUPPORTED);
                return;
        }

        TraceDbg("dev %04x", ptr04x(dev));

        auto st = device::reset_port(dev, request);
        if (st != STATUS_PENDING) {
                Trace(TRACE_LEVEL_ERROR, "dev %04x, reset port %!STATUS!", ptr04x(dev), st);
                WdfRequestComplete(request, st);
        }
}

_Function_class_(EVT_UDECX_USB_ENDPOINT_RESET)
_IRQL_requires_same_
void endpoint_reset(_In_ UDECXUSBENDPOINT endp, _In_ WDFREQUEST request)
{
        NT_ASSERT(!has_urb(request));
        TraceDbg("endp %04x, req %04x", ptr04x(endp), ptr04x(request));

        auto st = device::clear_endpoint_stall(endp, request);
        if (st != STATUS_PENDING) {
                Trace(TRACE_LEVEL_ERROR, "endp %04x, %!STATUS!", ptr04x(endp), st);
                WdfRequestComplete(request, st);
        }
}

_Function_class_(EVT_UDECX_USB_ENDPOINT_PURGE)
_IRQL_requires_same_
void endpoint_purge(_In_ UDECXUSBENDPOINT endpoint)
{
        auto &endp = *get_endpoint_ctx(endpoint);
        auto &dev = *get_device_ctx(endp.device);

        TraceDbg("dev %04x, endp %04x, queue %04x", ptr04x(endp.device), ptr04x(endpoint), ptr04x(endp.queue));

        while (auto request = device::dequeue_request(dev, endpoint)) {
                device::send_cmd_unlink(endp.device, request);
        }

        auto purge_complete = [] (auto queue, auto) // EVT_WDF_IO_QUEUE_STATE
        { 
                auto endp = get_endpoint(queue);
                UdecxUsbEndpointPurgeComplete(endp);
        };

        WdfIoQueuePurge(endp.queue, purge_complete, WDF_NO_CONTEXT);
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
inline NTSTATUS device_d0_entry(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev)
{
        TraceDbg("vhci %04x, dev %04x", ptr04x(vhci), ptr04x(dev));
        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_D0_EXIT)
_IRQL_requires_same_
inline NTSTATUS device_d0_exit(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev, _In_ UDECX_USB_DEVICE_WAKE_SETTING WakeSetting)
{
        TraceDbg("vhci %04x, dev %04x, %!UDECX_USB_DEVICE_WAKE_SETTING!", ptr04x(vhci), ptr04x(dev), WakeSetting);
        return STATUS_SUCCESS;
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

        return STATUS_NOT_SUPPORTED;
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
PAGED NTSTATUS endpoint_add(_In_ UDECXUSBDEVICE device, _In_ UDECX_USB_ENDPOINT_INIT_AND_METADATA *data)
{
        PAGED_CODE();

        auto &epd = data->EndpointDescriptor ? *data->EndpointDescriptor : EP0;
        UdecxUsbEndpointInitSetEndpointAddress(data->UdecxUsbEndpointInit, epd.bEndpointAddress);

        UDECX_USB_ENDPOINT_CALLBACKS cb;
        UDECX_USB_ENDPOINT_CALLBACKS_INIT(&cb, endpoint_reset);
        cb.EvtUsbEndpointStart = endpoint_start; // does not work without it
        cb.EvtUsbEndpointPurge = endpoint_purge;

        UdecxUsbEndpointInitSetCallbacks(data->UdecxUsbEndpointInit, &cb);

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, endpoint_ctx);
        attrs.EvtCleanupCallback = [] (auto obj) { TraceDbg("Endpoint %04x cleanup", ptr04x(obj)); }; 
        attrs.ParentObject = device;

        UDECXUSBENDPOINT endp;
        if (auto err = UdecxUsbEndpointCreate(&data->UdecxUsbEndpointInit, &attrs, &endp)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbEndpointCreate %!STATUS!", err);
                return err;
        }
        auto &ctx = *get_endpoint_ctx(endp);

        ctx.device = device;

        if (auto &dev = *get_device_ctx(device); auto epd_len = data->EndpointDescriptorBufferLength) {

                NT_ASSERT(epd.bLength == epd_len);
                NT_ASSERT(sizeof(ctx.descriptor) >= epd_len);
                RtlCopyMemory(&ctx.descriptor, &epd, epd_len);

                if (!dev.actconfig) {
                        NT_ASSERT(!ctx.InterfaceNumber);
                        NT_ASSERT(!ctx.AlternateSetting);
                } else if (auto ifd = usbdlib::find_intf(dev.actconfig, epd)) {
                        ctx.InterfaceNumber = ifd->bInterfaceNumber;
                        ctx.AlternateSetting = ifd->bAlternateSetting;
                } else {
                        Trace(TRACE_LEVEL_ERROR, "dev %04x, interface not found for endpoint%!BIN!", 
                                                  ptr04x(device), WppBinary(&epd, epd.bLength));
                        return STATUS_INVALID_PARAMETER;
                }
        } else {
                static_cast<USB_ENDPOINT_DESCRIPTOR&>(ctx.descriptor) = epd;
                dev.ep0 = endp;
        }

        if (auto err = create_endpoint_queue(ctx.queue, endp)) {
                return err;
        }

        {
                auto &d = ctx.descriptor;
                TraceDbg("dev %04x, intf %d.%d, endp %04x{Address %#04x: %s %s[%d], MaxPacketSize %d, "
                         "Interval %d}, queue %04x%!BIN!", 
                        ptr04x(device), ctx.InterfaceNumber, ctx.AlternateSetting, ptr04x(endp),
                        d.bEndpointAddress, usbd_pipe_type_str(usb_endpoint_type(d)),
                        usb_endpoint_dir_out(d) ? "Out" : "In", usb_endpoint_num(d), 
                        d.wMaxPacketSize, d.bInterval, ptr04x(ctx.queue), WppBinary(&d, d.bLength));
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

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto intf_has_endpoint(
        _In_ USB_CONFIGURATION_DESCRIPTOR *cfg, _In_ USB_INTERFACE_DESCRIPTOR *ifd, _In_ UCHAR EndpointAddress)
{
        auto f = [] (auto, auto &epd, auto ctx)
        {
                auto addr = *static_cast<UCHAR*>(ctx);
                return epd.bEndpointAddress == addr ? STATUS_PENDING : STATUS_SUCCESS;
        };

        return usbdlib::for_each_endp(cfg, ifd, f, &EndpointAddress) == STATUS_PENDING;
}

/*
 * All endpoints to configure belongs to the same interface, so the first is used for check. 
 */
_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE)
_IRQL_requires_same_
auto interface_setting_change(
        _In_ device_ctx &dev, _In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, 
        _In_ const UDECX_ENDPOINTS_CONFIGURE_PARAMS &params)
{
        auto ifnum = params.InterfaceNumber;
        auto altnum = params.NewInterfaceSetting;

        if (dev.actconfig && params.EndpointsToConfigureCount) {

                auto ifd = usbdlib::find_next_intf(dev.actconfig, nullptr, ifnum, altnum);
                if (!ifd) {
                        Trace(TRACE_LEVEL_ERROR, "Interface %d.%d descriptor not found", ifnum, altnum);
                        return STATUS_INVALID_PARAMETER;
                }

                auto &endp = *get_endpoint_ctx(*params.EndpointsToConfigure); 

                if (!intf_has_endpoint(dev.actconfig, ifd, endp.descriptor.bEndpointAddress)) {
                        TraceDbg("Correction %d.%d -> %d.%d", ifnum, altnum, endp.InterfaceNumber, endp.AlternateSetting);
                        ifnum = endp.InterfaceNumber;
                        altnum = endp.AlternateSetting;
                }
        }

        return device::set_interface(device, request, ifnum, altnum);
}

/*
 * UDEX does not set configuration for composite devices.
 *
 * If InterfaceSettingChange is called after UdecxUsbDevicePlugOutAndDelete, WDFREQUEST will be completed 
 * with error status. In such case UDECXUSBDEVICE will not be destroyed and the driver can't be unloaded.
 */
_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE)
_IRQL_requires_same_
void endpoints_configure(
        _In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, _In_ UDECX_ENDPOINTS_CONFIGURE_PARAMS *params)
{
        NT_ASSERT(!has_urb(request));

        if (auto n = params->EndpointsToConfigureCount) {
                TraceDbg("dev %04x, EndpointsToConfigure[%lu]%!BIN!", ptr04x(device), n, 
                          WppBinary(params->EndpointsToConfigure, USHORT(n*sizeof(*params->EndpointsToConfigure))));
        }

        if (auto n = params->ReleasedEndpointsCount) {
                TraceDbg("dev %04x, ReleasedEndpoints[%lu]%!BIN!", ptr04x(device), n, 
                          WppBinary(params->ReleasedEndpoints, USHORT(n*sizeof(*params->ReleasedEndpoints))));
        }

        auto &dev = *get_device_ctx(device);
        auto st = STATUS_SUCCESS;

        if (dev.unplugged) { // UDECXUSBDEVICE can no longer be used
                TraceDbg("%!UDECX_ENDPOINTS_CONFIGURE_TYPE!: unplugged", params->ConfigureType);
        } else switch (params->ConfigureType) {
        case UdecxEndpointsConfigureTypeDeviceInitialize: // for internal use, can be called several times
                TraceDbg("DeviceInitialize");
                if (dev.actconfig && usbdlib::is_composite(dev.descriptor, *dev.actconfig)) {
                        auto cfg = dev.actconfig->bConfigurationValue;
                        st = device::set_configuration(device, request, IOCTL_INTERNAL_USBEX_CFG_INIT, cfg);
                }
                break;
        case UdecxEndpointsConfigureTypeDeviceConfigurationChange:
                st = device::set_configuration(device, request, IOCTL_INTERNAL_USBEX_CFG_CHANGE, params->NewConfigurationValue);
                break;
        case UdecxEndpointsConfigureTypeInterfaceSettingChange:
                st = interface_setting_change(dev, device, request, *params);
                break;
        case UdecxEndpointsConfigureTypeEndpointsReleasedOnly:
                TraceDbg("EndpointsReleasedOnly");
                /*
                for (ULONG i = 0; i < params->ReleasedEndpointsCount; ++i) {
                        auto endp = params->ReleasedEndpoints[i];
                        TraceDbg("dev %04x, delete released endp %04x", ptr04x(device), ptr04x(endp));
                        WdfObjectDelete(endp); // causes BSOD sometimes
                }
                */
                break;
        }

        if (st != STATUS_PENDING) {
                if (st) {
                        TraceDbg("%!STATUS!", st);
                }
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

//      cb.EvtUsbDeviceLinkPowerEntry = device_d0_entry; // required if the device supports USB remote wake
//      cb.EvtUsbDeviceLinkPowerExit = device_d0_exit;
        
        if (speed >= UdecxUsbSuperSpeed) { // required for USB 3 devices
                cb.EvtUsbDeviceSetFunctionSuspendAndWake = device_set_function_suspend_and_wake;
        }

//      cb.EvtUsbDeviceReset = device_reset; // server returns EPIPE always
        
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

        reset_alternate_setting(ctx);

        if (auto err = init_receive_usbip_header(ctx)) {
                return err;
        }

        if (auto err = device::create_queue(dev)) {
                return err;
        }

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
 * Call UdecxUsbDevicePlugOutAndDelete if UdecxUsbDevicePlugIn was successful.
 * A device will be plugged out from a hub, delete can be delayed slightly.
 * After UdecxUsbDevicePlugOutAndDelete the client driver can no longer use UDECXUSBDEVICE.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::device::plugout_and_delete(_In_ UDECXUSBDEVICE dev)
{
        PAGED_CODE();

        auto &ctx = *get_device_ctx(dev);
        static_assert(sizeof(ctx.unplugged) == sizeof(CHAR));

        if (InterlockedExchange8(PCHAR(&ctx.unplugged), true)) {
                TraceDbg("dev %04x is already unplugged", ptr04x(dev));
                return;
        }

        Trace(TRACE_LEVEL_INFORMATION, "dev %04x, port %d", ptr04x(dev), ctx.port);

        if (auto err = UdecxUsbDevicePlugOutAndDelete(dev)) { // caught BSOD on DISPATCH_LEVEL
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDevicePlugOutAndDelete(dev=%04x) %!STATUS!", ptr04x(dev), err);
        }
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::device::sched_plugout_and_delete(_In_ UDECXUSBDEVICE dev)
{
        auto func = [] (auto WorkItem)
        {
                if (auto dev = (UDECXUSBDEVICE)WdfWorkItemGetParentObject(WorkItem)) {
                        plugout_and_delete(dev);
                }
                WdfObjectDelete(WorkItem); // can be omitted
        };

        WDF_WORKITEM_CONFIG cfg;
        WDF_WORKITEM_CONFIG_INIT(&cfg, func);
        cfg.AutomaticSerialization = false;

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
        attrs.ParentObject = dev;

        WDFWORKITEM wi{};
        if (auto err = WdfWorkItemCreate(&cfg, &attrs, &wi)) {
                if (err == STATUS_DELETE_PENDING) {
                        TraceDbg("dev %04x %!STATUS!", ptr04x(dev), err);
                } else {
                        Trace(TRACE_LEVEL_ERROR, "WdfWorkItemCreate %!STATUS!", err);
                }
                return err;
        }

        WdfWorkItemEnqueue(wi);
        return STATUS_SUCCESS;
}
