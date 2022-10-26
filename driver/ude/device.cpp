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


/*
 * Enumeration of USB Composite Devices.
 * 
 * A) The bus driver checks the bDeviceClass, bDeviceSubClass and bDeviceProtocol fields of the device descriptor.  
 *    If these fields are zero, the device is a composite device, and the bus driver reports an extra compatible
 *    identifier (ID) of USB\COMPOSITE for the PDO.
 * 
 * B) The bus driver also reports a compatible identifier (ID) of USB\COMPOSITE,
 *    if the device meets the following requirements:
 *    1.The device class field of the device descriptor (bDeviceClass) must contain a value of zero,
 *      or the class (bDeviceClass), bDeviceSubClass, and bDeviceProtocol fields
 *      of the device descriptor must have the values 0xEF, 0x02 and 0x01 respectively, as explained
 *      in USB Interface Association Descriptor.
 *    2.The device must have multiple interfaces.
 *    3.The device must have a single configuration.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto is_composite(_In_ const device_ctx &dev, _In_ UCHAR NumInterfaces)
{
        UINT8 Class;
        UINT8 SubClass;
        UINT8 Protocol;
        UINT8 NumConfigurations;

        if (auto &dd = dev.descriptor; usbdlib::is_valid(dd)) {
                Class = dd.bDeviceClass;
                SubClass = dd.bDeviceSubClass;
                Protocol = dd.bDeviceProtocol;
                NumConfigurations = dd.bNumConfigurations;
        } else {
                auto &r = dev.ext->dev;

                Class = r.DeviceClass;
                SubClass = r.DeviceSubClass;
                Protocol = r.DeviceProtocol;
                NumConfigurations = r.NumConfigurations;
        }

        if (!(Class || SubClass || Protocol)) { // case A
                return true;
        }

        // case B

        bool ok = Class == USB_DEVICE_CLASS_RESERVED || // generic composite device
                 (Class == USB_DEVICE_CLASS_MISCELLANEOUS && // 0xEF
                  SubClass == 0x02 && // common class
                  Protocol == 0x01); // IAD composite device

        if (auto cfg = dev.actconfig) {
                NumInterfaces = cfg->bNumInterfaces;
        }

        return ok && NumConfigurations == 1 && NumInterfaces > 1;
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

        auto epd = data->EndpointDescriptor; // NULL if default control pipe is adding
        auto bEndpointAddress = epd ? epd->bEndpointAddress : UCHAR(USB_DEFAULT_ENDPOINT_ADDRESS);

        UdecxUsbEndpointInitSetEndpointAddress(data->UdecxUsbEndpointInit, bEndpointAddress);

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

        auto &dev = *get_device_ctx(device);

        auto &ctx = *get_endpoint_ctx(endp);
        ctx.device = device;

        if (epd) {
                ctx.descriptor = *epd;
                if (auto cfg = dev.actconfig) {
                        if (auto ifd = usbdlib::find_intf(cfg, *epd)) {
                                ctx.InterfaceNumber = ifd->bInterfaceNumber;
                                ctx.AlternateSetting = ifd->bAlternateSetting;
                        } else {
                                Trace(TRACE_LEVEL_ERROR, "dev %04x, interface not found for endp{Address %#x}", 
                                        ptr04x(device), epd->bEndpointAddress);
                                return STATUS_INVALID_PARAMETER;
                        }
                }
        } else {
                auto &d = ctx.descriptor;

                d.bLength = sizeof(d);
                d.bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
                NT_ASSERT(usb_default_control_pipe(d));

                dev.ep0 = endp;
        }

        if (auto err = create_endpoint_queue(ctx.queue, endp)) {
                return err;
        }

        {
                auto &d = ctx.descriptor;
                TraceDbg("dev %04x, endp %04x{Address %#02x: %s %s[%d], Interval %d, InterfaceNumber %d, "
                          "AlternateSetting %d}, queue %04x", 
                        ptr04x(device), ptr04x(endp), d.bEndpointAddress, usbd_pipe_type_str(usb_endpoint_type(d)),
                        usb_endpoint_dir_out(d) ? "Out" : "In", usb_endpoint_num(d), d.bInterval, 
                        ctx.InterfaceNumber, ctx.AlternateSetting, ptr04x(ctx.queue));
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
auto interface_setting_change(
        _In_ device_ctx &dev, _In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, 
        _In_ const UDECX_ENDPOINTS_CONFIGURE_PARAMS &params)
{
        TraceDbg("dev %04x, ToConfigure[%lu]%!BIN!", ptr04x(device), params.EndpointsToConfigureCount, 
                  WppBinary(params.EndpointsToConfigure,
                            USHORT(params.EndpointsToConfigureCount*sizeof(*params.EndpointsToConfigure))));

        auto ifnum = params.InterfaceNumber;
        auto altnum = params.NewInterfaceSetting;

        if (params.EndpointsToConfigureCount && dev.actconfig) {

                auto &endp = *get_endpoint_ctx(*params.EndpointsToConfigure);
                bool found{};

                if (auto ifd = usbdlib::find_next_intf(dev.actconfig, nullptr, ifnum, altnum)) {
                        if (ifd->bNumEndpoints) {
                                auto f = [] (auto idx, auto &epd, auto ctx)
                                {
                                        TraceDbg("#%d, Address %#x", idx, epd.bEndpointAddress);
                                        auto dsc = reinterpret_cast<USB_ENDPOINT_DESCRIPTOR*>(ctx);
                                        using usbdlib::operator==;
                                        return epd == *dsc ? STATUS_PENDING : STATUS_SUCCESS;
                                };
                                auto ret = usbdlib::for_each_endp(dev.actconfig, ifd, f, &endp.descriptor);
                                found = ret == STATUS_PENDING;
                        }
                } else {
                        Trace(TRACE_LEVEL_ERROR, "ifnum %d, altnum %d not found", ifnum, altnum);
                }

                if (!found) {
                        ifnum = endp.InterfaceNumber;
                        altnum = endp.AlternateSetting;
                        TraceDbg("InterfaceNumber %d -> %d, NewInterfaceSetting %d -> %d", 
                                  params.InterfaceNumber, ifnum, params.NewInterfaceSetting, altnum);
                }
        }

        NT_ASSERT(ifnum < ARRAYSIZE(dev.AlternateSetting));

        if (dev.AlternateSetting[ifnum] == altnum) {
                return STATUS_SUCCESS;
        }

        dev.AlternateSetting[ifnum] = altnum;
        return device::set_interface(device, request, ifnum, altnum);
}

_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE)
_IRQL_requires_same_
void endpoints_configure(
        _In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, _In_ UDECX_ENDPOINTS_CONFIGURE_PARAMS *params)
{
        NT_ASSERT(!has_urb(request));
        auto &dev = *get_device_ctx(device);

        auto st = STATUS_SUCCESS;

        if (dev.unplugged) {
                TraceDbg("Unplugged");
        } else switch (params->ConfigureType) {
        case UdecxEndpointsConfigureTypeInterfaceSettingChange:
                st = interface_setting_change(dev, device, request, *params);
                break;
        case UdecxEndpointsConfigureTypeDeviceConfigurationChange:
                st = device::set_configuration(device, request, IOCTL_INTERNAL_USBEX_CFG_CHANGE, params->NewConfigurationValue);
                break;
        case UdecxEndpointsConfigureTypeDeviceInitialize: // reserved for internal use
                TraceDbg("DeviceInitialize");
                if (is_composite(dev, 2)) { // UDEX does not set configuration for composite device
                        NT_ASSERT(!dev.actconfig);
                        st = device::set_configuration(device, request, IOCTL_INTERNAL_USBEX_CFG_INIT, UCHAR(1));
                }
                break;
        case UdecxEndpointsConfigureTypeEndpointsReleasedOnly:
                TraceDbg("dev %04x, Released[%lu]%!BIN!", ptr04x(device), params->ReleasedEndpointsCount, 
                          WppBinary(params->ReleasedEndpoints,
                                    USHORT(params->ReleasedEndpointsCount*sizeof(*params->ReleasedEndpoints))));
                /*
                for (ULONG i = 0; i < params->ReleasedEndpointsCount; ++i) {
                        auto endp = params->ReleasedEndpoints[i];
                        TraceDbg("dev %04x, delete released endp %04x", ptr04x(device), ptr04x(endp));
                        WdfObjectDelete(endp);
                }
                */
                break;
        }

        TraceDbg("%!STATUS!", st);

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

        RtlFillMemory(ctx.AlternateSetting, sizeof(ctx.AlternateSetting), -1);

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
 * Call UdecxUsbDevicePlugOutAndDelete if UdecxUsbDevicePlugIn was successful.
 * A device will be plugged out from a hub, delete can be delayed slightly.
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
