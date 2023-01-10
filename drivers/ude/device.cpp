/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
#include "wsk_receive.h"
#include "ioctl.h"

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

_Function_class_(EVT_WDF_DEVICE_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void NTAPI device_destroy(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto device = static_cast<UDECXUSBDEVICE>(Object);
        TraceDbg("dev %04x", ptr04x(device));

        if (auto &dev = *get_device_ctx(device); auto ptr = dev.ext) {
                free(ptr);
        }
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
        close_socket(ctx.ext->sock);

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

/*
 * Bring the device from low power state and enter working state.
 * The power request may be completed asynchronously by returning STATUS_PENDING, 
 * and then later completing it by calling UdecxUsbDeviceLinkPowerExitComplete 
 * with the actual completion code.
 */
_Function_class_(EVT_UDECX_USB_DEVICE_D0_ENTRY)
_IRQL_requires_same_
NTSTATUS d0_entry(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev)
{
        TraceDbg("vhci %04x, dev %04x", ptr04x(vhci), ptr04x(dev));
        return STATUS_SUCCESS;
}

/*
 * Send the virtual USB device to a low power state.
 * The power request may be completed asynchronously by returning STATUS_PENDING, 
 * and then later calling UdecxUsbDeviceLinkPowerExitComplete with the actual completion code.
 * 
 * @see UdecxUsbDeviceSignalWake, UdecxUsbDeviceSignalFunctionWake
 */
_Function_class_(EVT_UDECX_USB_DEVICE_D0_EXIT)
_IRQL_requires_same_
NTSTATUS d0_exit(_In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE dev, _In_ UDECX_USB_DEVICE_WAKE_SETTING WakeSetting)
{
        TraceDbg("vhci %04x, dev %04x, %!UDECX_USB_DEVICE_WAKE_SETTING!", ptr04x(vhci), ptr04x(dev), WakeSetting);
        
        switch (WakeSetting) {
        case UdecxUsbDeviceWakeDisabled:
                break;
        case UdecxUsbDeviceWakeEnabled:
                NT_ASSERT(get_device_ctx(dev)->speed() < USB_SPEED_SUPER);
                break;
        case UdecxUsbDeviceWakeNotApplicable: // SuperSpeed device
                NT_ASSERT(get_device_ctx(dev)->speed() >= USB_SPEED_SUPER);
                break;
        }

        return STATUS_SUCCESS;
}

/*
 * @see UdecxUsbDeviceSignalFunctionWake
 * @see UdecxUsbDeviceSetFunctionSuspendAndWakeComplete
 */
_Function_class_(EVT_UDECX_USB_DEVICE_SET_FUNCTION_SUSPEND_AND_WAKE)
_IRQL_requires_same_
NTSTATUS function_suspend_and_wake(
        _In_ WDFDEVICE vhci, 
        _In_ UDECXUSBDEVICE dev, 
        _In_ ULONG Interface, 
        _In_ UDECX_USB_DEVICE_FUNCTION_POWER FunctionPower)
{
        TraceDbg("vhci %04x, dev %04x, Interface %lu, %!UDECX_USB_DEVICE_FUNCTION_POWER!", 
                  ptr04x(vhci), ptr04x(dev), Interface, FunctionPower);

        NT_ASSERT(get_device_ctx(dev)->speed() >= USB_SPEED_SUPER);

        switch (FunctionPower) {
        case UdecxUsbDeviceFunctionNotSuspended:
                break;
        case UdecxUsbDeviceFunctionSuspendedCannotWake:
                break;
        case UdecxUsbDeviceFunctionSuspendedCanWake:
                break;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_endpoint_queue(_Inout_ WDFQUEUE &queue, _In_ UDECXUSBENDPOINT endpoint)
{
        PAGED_CODE();
        
        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT(&cfg, WdfIoQueueDispatchParallel); // FIXME: Sequential for EP0?
        cfg.EvtIoInternalDeviceControl = device::internal_control;

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, UDECXUSBENDPOINT);
        attrs.EvtCleanupCallback = [] (auto obj) { TraceDbg("Queue %04x cleanup", ptr04x(obj)); };
//      attrs.SynchronizationScope = WdfSynchronizationScopeQueue; // EvtIoInternalDeviceControl is used only
        attrs.ParentObject = endpoint;

        auto &endp = *get_endpoint_ctx(endpoint);
        auto &dev = *get_device_ctx(endp.device); 

        if (auto err = WdfIoQueueCreate(dev.vhci, &cfg, &attrs, &queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }
        get_endpoint(queue) = endpoint;

        UdecxUsbEndpointSetWdfIoQueue(endpoint, queue);
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

        if (auto len = data->EndpointDescriptorBufferLength) {
                NT_ASSERT(epd.bLength == len);
                NT_ASSERT(sizeof(ctx.descriptor) >= len);
                RtlCopyMemory(&ctx.descriptor, &epd, len);
        } else {
                NT_ASSERT(epd == EP0);
                static_cast<USB_ENDPOINT_DESCRIPTOR&>(ctx.descriptor) = epd;
                get_device_ctx(device)->ep0 = endp;
        }

        if (auto err = create_endpoint_queue(ctx.queue, endp)) {
                return err;
        }

        {
                auto &d = ctx.descriptor;
                TraceDbg("dev %04x, endp %04x{Address %#04x: %s %s[%d], MaxPacketSize %d, Interval %d}, queue %04x%!BIN!",
                        ptr04x(device), ptr04x(endp), d.bEndpointAddress, usbd_pipe_type_str(usb_endpoint_type(d)),
                        usb_endpoint_dir_out(d) ? "Out" : "In", usb_endpoint_num(d), d.wMaxPacketSize, 
                        d.bInterval, ptr04x(ctx.queue), WppBinary(&d, d.bLength));
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

/*
 * If InterfaceSettingChange is called after UdecxUsbDevicePlugOutAndDelete, WDFREQUEST will be completed 
 * with error status. In such case UDECXUSBDEVICE will not be destroyed and the driver can't be unloaded.
 *
 * UDEX does not call ConfigurationChange for composite devices.
 * UDEX passes wrong InterfaceNumber, NewInterfaceSetting for InterfaceSettingChange.
 * For that reasons the Upper Filter Driver is used that intercepts SELECT_CONFIGURATION, 
 * SELECT_INTERFACE URBs and notifies this driver.
 *
 * @see ude_filter/int_dev_ctrl.cpp, int_dev_ctrl
 * @see device_ioctl.cpp, control_transfer
 */
_Function_class_(EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void endpoints_configure(
        _In_ UDECXUSBDEVICE device, _In_ WDFREQUEST request, _In_ UDECX_ENDPOINTS_CONFIGURE_PARAMS *params)
{
        NT_ASSERT(!has_urb(request)); // but MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL

        if (auto n = params->EndpointsToConfigureCount) {
                TraceDbg("dev %04x, EndpointsToConfigure[%lu]%!BIN!", ptr04x(device), n, 
                          WppBinary(params->EndpointsToConfigure, USHORT(n*sizeof(*params->EndpointsToConfigure))));
        }

        if (auto n = params->ReleasedEndpointsCount) {
                TraceDbg("dev %04x, ReleasedEndpoints[%lu]%!BIN!", ptr04x(device), n, 
                          WppBinary(params->ReleasedEndpoints, USHORT(n*sizeof(*params->ReleasedEndpoints))));
        }

        if (auto &dev = *get_device_ctx(device); dev.unplugged) { // UDECXUSBDEVICE can no longer be used
                TraceDbg("dev %04x, %!UDECX_ENDPOINTS_CONFIGURE_TYPE!: unplugged", 
                          ptr04x(device), params->ConfigureType);
        } else switch (params->ConfigureType) {
        case UdecxEndpointsConfigureTypeDeviceInitialize: // for internal use, can be called several times
                TraceDbg("dev %04x, DeviceInitialize", ptr04x(device));
                break;
        case UdecxEndpointsConfigureTypeDeviceConfigurationChange:
                TraceDbg("dev %04x, NewConfigurationValue %d", ptr04x(device), params->NewConfigurationValue);
                break;
        case UdecxEndpointsConfigureTypeInterfaceSettingChange:
                TraceDbg("dev %04x, InterfaceNumber %d, NewInterfaceSetting %d", 
                          ptr04x(device), params->InterfaceNumber, params->NewInterfaceSetting);
                break;
        case UdecxEndpointsConfigureTypeEndpointsReleasedOnly:
                TraceDbg("dev %04x, EndpointsReleasedOnly", ptr04x(device)); // WdfObjectDelete(ReleasedEndpoints[i]) can cause BSOD
                break;
        }

        WdfRequestComplete(request, STATUS_SUCCESS);
}

/*
 * _UDECXUSBDEVICE_INIT* must be freed if UdecxUsbDeviceCreate fails.
 * UdecxUsbDeviceInitFree must not be called on success, Udecx does that itself.
 */
struct device_init_ptr
{
        device_init_ptr(WDFDEVICE vhci) : ptr(UdecxUsbDeviceInitAllocate(vhci)) {}

        ~device_init_ptr() 
        {
                if (ptr) {
                        UdecxUsbDeviceInitFree(ptr);
                }
        }

        device_init_ptr(const device_init_ptr&) = delete;
        device_init_ptr& operator=(const device_init_ptr&) = delete;

        explicit operator bool() const { return ptr; }
        auto operator !() const { return !ptr; }

        void release() { ptr = nullptr; }

        _UDECXUSBDEVICE_INIT *ptr{};
};

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto prepare_init(_In_ _UDECXUSBDEVICE_INIT *init, _In_ device_ctx_ext &ext)
{
        PAGED_CODE();
        NT_ASSERT(init);

        UDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS cb;
        UDECX_USB_DEVICE_CALLBACKS_INIT(&cb);

        cb.EvtUsbDeviceLinkPowerEntry = d0_entry; // required if the device supports USB remote wake
        cb.EvtUsbDeviceLinkPowerExit = d0_exit;
        
        if (ext.dev.speed >= USB_SPEED_SUPER) { // required
                cb.EvtUsbDeviceSetFunctionSuspendAndWake = function_suspend_and_wake;
        }

//      cb.EvtUsbDeviceReset = device_reset; // server returns EPIPE always
        
        cb.EvtUsbDeviceDefaultEndpointAdd = default_endpoint_add;
        cb.EvtUsbDeviceEndpointAdd = endpoint_add;
        cb.EvtUsbDeviceEndpointsConfigure = endpoints_configure;

        UdecxUsbDeviceInitSetStateChangeCallbacks(init, &cb);

        auto speed = to_udex_speed(ext.dev.speed);
        UdecxUsbDeviceInitSetSpeed(init, speed);

        UdecxUsbDeviceInitSetEndpointsType(init, UdecxEndpointTypeDynamic);
        return STATUS_SUCCESS;
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

        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::device::create(_Out_ UDECXUSBDEVICE &dev, _In_ WDFDEVICE vhci, _In_ device_ctx_ext *ext)
{
        PAGED_CODE();
        dev = WDF_NO_HANDLE;

        device_init_ptr init(vhci);
        if (auto err = init ? prepare_init(init.ptr, *ext) : STATUS_INSUFFICIENT_RESOURCES) {
                Trace(TRACE_LEVEL_ERROR, "UDECXUSBDEVICE_INIT %!STATUS!", err);
                return err;
        }

        WDF_OBJECT_ATTRIBUTES attrs;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, device_ctx);
        attrs.EvtCleanupCallback = device_cleanup;
        attrs.EvtDestroyCallback = device_destroy;
        attrs.ParentObject = vhci; // FIXME: by default?

        if (auto err = UdecxUsbDeviceCreate(&init.ptr, &attrs, &dev)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceCreate %!STATUS!", err);
                return err;
        }

        init.release();
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
