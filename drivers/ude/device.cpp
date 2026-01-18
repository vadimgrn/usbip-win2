/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "device.h"
#include "trace.h"
#include "device.tmh"

#include "driver.h"
#include "ioctl.h"
#include "vhci.h"
#include "network.h"
#include "persistent.h"
#include "wsk_receive.h"
#include "device_ioctl.h"
#include "request_list.h"
#include "endpoint_list.h"

#include <libdrv/dbgcommon.h>
#include <libdrv/wait_timeout.h>

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
                return UdecxUsbLowSpeed;
        case USB_SPEED_UNKNOWN:
        default:
                return UdecxUsbHighSpeed; // FIXME
        }
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void device_cleanup(_In_ WDFOBJECT Object)
{
        PAGED_CODE();

        auto device = static_cast<UDECXUSBDEVICE>(Object);
        auto &dev = *get_device_ctx(device);

        Trace(TRACE_LEVEL_INFORMATION, "dev %04x, cancelable(%!UINT64!) / sent(%!UINT64!) requests",
                ptr04x(device), dev.cancelable_requests, dev.sent_requests);

        NT_VERIFY(!device::detach(device, false)); // receive thread never calls EVT_WDF_DEVICE_CONTEXT_CLEANUP

        if (auto &h = dev.ctx_ext) { // the parent is vhci controller
                WdfObjectDelete(h);
                h = WDF_NO_HANDLE;
        }

        // all resources must be freed
        NT_ASSERT(IsListEmpty(&dev.requests));
        NT_ASSERT(get_flag(dev.unplugged));
        NT_ASSERT(!dev.port);
        NT_ASSERT(!dev.recv_thread);
}

/*
 * @return object_reference defer dereference of receive thread if it executes this function
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto recv_thread_join(_In_ UDECXUSBDEVICE device, _Inout_ device_ctx &dev)
{
        PAGED_CODE();
        wdm::object_reference thread(InterlockedExchangePointer(reinterpret_cast<PVOID*>(&dev.recv_thread), nullptr), false);

        if (!thread) {
                TraceDbg("dev %04x, was not created", ptr04x(device));
                return thread;
        } else if (thread.get() == KeGetCurrentThread()) { // called by receive thread
                thread.set_defer_delete();
                return thread;
        }

        NT_ASSERT(get_flag(dev.unplugged)); // thread checks it
        TraceDbg("dev %04x", ptr04x(device));

        if (auto timeout = make_timeout(1*wdm::minute, wdm::period::relative);
            auto err = KeWaitForSingleObject(thread.get(), Executive, KernelMode, false, &timeout)) {
                Trace(TRACE_LEVEL_ERROR, "dev %04x, KeWaitForSingleObject %!STATUS!", ptr04x(device), err);
        } else {
                TraceDbg("dev %04x, joined", ptr04x(device));
        }

        thread.reset(); // it's safe to dereference(delete) now
        return thread;
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

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void NTAPI endpoint_cleanup(_In_ WDFOBJECT object)
{
        PAGED_CODE();

        auto endpoint = static_cast<UDECXUSBENDPOINT>(object);
        auto &endp = *get_endpoint_ctx(endpoint);
        auto &d = endp.descriptor;

        TraceDbg("endp %04x{Address %#x: %s %s[%d]}, PipeHandle %04x",
                  ptr04x(endpoint), d.bEndpointAddress, usbd_pipe_type_str(usb_endpoint_type(d)),
                  usb_endpoint_dir_out(d) ? "Out" : "In", usb_endpoint_num(d), ptr04x(endp.PipeHandle));

        remove_endpoint_list(endp);
}

/*
 * FIXME: UDE never(?) call this callback for stalled endpoints.
 */
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

        while (auto request = device::remove_request(dev, endpoint)) {
                device::send_cmd_unlink_and_cancel(endp.device, request);
        }

        auto purge_complete = [] ([[maybe_unused]] auto queue, auto ctx) // EVT_WDF_IO_QUEUE_STATE
        { 
                auto endpoint = static_cast<UDECXUSBENDPOINT>(ctx);
                NT_ASSERT(get_endpoint(queue) == endpoint);

                UdecxUsbEndpointPurgeComplete(endpoint);
        };

        WdfIoQueuePurge(endp.queue, purge_complete, endpoint);
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

/*
 * UDE can call EvtIoInternalDeviceControl concurrently for different queues of the same device.
 * I've caught concurrent CTRL and BULK transfer for a flash drive. 
 * FIXME: can UDE reorder requests?
 *
 * WdfSynchronizationScopeDevice can't be used to serialize calls of WskSend because 
 * it can be called concurrently from UDECX_USB_ENDPOINT_CALLBACKS.EvtUsbEndpointPurge.
 * If set SynchronizationScopeDevice for UDECXUSBENDPOINT, UdecxUsbEndpointCreate 
 * will return STATUS_WDF_SYNCHRONIZATION_SCOPE_INVALID. For these reasons,
 * explicit WDFSPINLOCK device_ctx.send_lock is used to serialize WskSend calls.
 * 
 * Using power-managed queues for I/O requests that require the device to be in its working state, 
 * and using queues that are not power-managed for all other requests.
 * Virtual device does not require power. For that reason all queues are not power-managed. 
 * @see Power Management for I/O Queues
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_endpoint_queue(
        _Inout_ WDFQUEUE &queue, _In_ UDECXUSBENDPOINT endpoint, _In_ WDF_IO_QUEUE_DISPATCH_TYPE dispatch_type)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT(&cfg, dispatch_type);
        cfg.PowerManaged = WdfFalse;
        cfg.EvtIoInternalDeviceControl = device::internal_control;

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, UDECXUSBENDPOINT);
        attr.EvtCleanupCallback = [] (auto obj) { TraceDbg("Queue %04x cleanup", ptr04x(obj)); };
//      attr.SynchronizationScope = WdfSynchronizationScopeQueue; // EvtIoInternalDeviceControl is used only
        attr.ParentObject = endpoint;

        auto &endp = *get_endpoint_ctx(endpoint);
        auto &dev = *get_device_ctx(endp.device); 

        if (auto err = WdfIoQueueCreate(dev.vhci, &cfg, &attr, &queue)) {
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
        
        auto &dev = *get_device_ctx(device);
        if (get_flag(dev.unplugged)) {
                TraceDbg("dev %04x, unplugged", ptr04x(device));
                return STATUS_DEVICE_NOT_CONNECTED;
        }

        auto &epd = data->EndpointDescriptor ? *data->EndpointDescriptor : EP0;
        UdecxUsbEndpointInitSetEndpointAddress(data->UdecxUsbEndpointInit, epd.bEndpointAddress);

        UDECX_USB_ENDPOINT_CALLBACKS cb;
        UDECX_USB_ENDPOINT_CALLBACKS_INIT(&cb, endpoint_reset);
        cb.EvtUsbEndpointStart = endpoint_start; // does not work without it
        cb.EvtUsbEndpointPurge = endpoint_purge;

        UdecxUsbEndpointInitSetCallbacks(data->UdecxUsbEndpointInit, &cb);

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, endpoint_ctx);
        attr.EvtCleanupCallback = endpoint_cleanup;
        attr.ParentObject = device;

        UDECXUSBENDPOINT endpoint;
        if (auto err = UdecxUsbEndpointCreate(&data->UdecxUsbEndpointInit, &attr, &endpoint)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbEndpointCreate %!STATUS!", err);
                return err;
        }

        auto &endp = *get_endpoint_ctx(endpoint);

        endp.device = device;
        InitializeListHead(&endp.entry);

        if (auto len = data->EndpointDescriptorBufferLength) {
                NT_ASSERT(epd.bLength == len);
                NT_ASSERT(sizeof(endp.descriptor) >= len);
                RtlCopyMemory(&endp.descriptor, &epd, len);
                insert_endpoint_list(endp);
        } else { // default control pipe always added first
                NT_ASSERT(epd == EP0);
                static_cast<USB_ENDPOINT_DESCRIPTOR&>(endp.descriptor) = epd;
                dev.ext().ep0_added = true;
                dev.ep0 = endpoint;
        }

        if (auto dispatch = usb_endpoint_type(epd) == UsbdPipeTypeControl ?
                            WdfIoQueueDispatchSequential : WdfIoQueueDispatchParallel;
            auto err = create_endpoint_queue(endp.queue, endpoint, dispatch)) {
                return err;
        }

        {
                auto &d = endp.descriptor;
                TraceDbg("dev %04x, endp %04x{Length %d, Address %#04x{%s %s[%d]}, Attributes %#x, MaxPacketSize %#x, "
                         "Interval %d, Audio{Refresh %#x, SynchAddress %#x}}, queue %04x%!BIN!",
                        ptr04x(device), ptr04x(endpoint), d.bLength, d.bEndpointAddress, 
                        usbd_pipe_type_str(usb_endpoint_type(d)), usb_endpoint_dir_out(d) ? "Out" : "In", 
                        usb_endpoint_num(d), d.bmAttributes, d.wMaxPacketSize, d.bInterval, d.bRefresh,
                        d.bSynchAddress, ptr04x(endp.queue), WppBinary(&d, d.bLength));
        }

        return STATUS_SUCCESS;
}

_Function_class_(EVT_UDECX_USB_DEVICE_DEFAULT_ENDPOINT_ADD)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS default_endpoint_add(_In_ UDECXUSBDEVICE dev, _In_ _UDECXUSBENDPOINT_INIT *init)
{
        PAGED_CODE();
        UDECX_USB_ENDPOINT_INIT_AND_METADATA data{ .UdecxUsbEndpointInit = init };
        return endpoint_add(dev, &data);
}

/*
 * WdfRequestGetIoQueue(request) returns queue that does not belong to the device (not its EP0 or others).
 * get_endpoint(WdfRequestGetIoQueue(request)) causes BSOD.
 *
 * Completing a request with an error status after UdecxUsbDevicePlugOutAndDelete causes 
 * hanging of the driver during the unloading because UDE will not destroy UDECXUSBDEVICE.
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

        switch (params->ConfigureType) {
        case UdecxEndpointsConfigureTypeDeviceInitialize: // for internal use, can be called several times
                TraceDbg("dev %04x, DeviceInitialize", ptr04x(device));
                break;
        case UdecxEndpointsConfigureTypeDeviceConfigurationChange:
                TraceDbg("dev %04x, ConfigurationValue %d", ptr04x(device), params->NewConfigurationValue);
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

        _UDECXUSBDEVICE_INIT *ptr{};
};

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto prepare_init(_In_ _UDECXUSBDEVICE_INIT *init, _In_ usb_device_speed speed)
{
        PAGED_CODE();
        NT_ASSERT(init);

        UDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS cb;
        UDECX_USB_DEVICE_CALLBACKS_INIT(&cb);

        cb.EvtUsbDeviceLinkPowerEntry = d0_entry; // required if the device supports USB remote wake
        cb.EvtUsbDeviceLinkPowerExit = d0_exit;
        
        if (speed >= USB_SPEED_SUPER) { // required
                cb.EvtUsbDeviceSetFunctionSuspendAndWake = function_suspend_and_wake;
        }

//      cb.EvtUsbDeviceReset = device_reset; // server returns EPIPE always
        
        cb.EvtUsbDeviceDefaultEndpointAdd = default_endpoint_add;
        cb.EvtUsbDeviceEndpointAdd = endpoint_add;
        cb.EvtUsbDeviceEndpointsConfigure = endpoints_configure;

        UdecxUsbDeviceInitSetStateChangeCallbacks(init, &cb);

        auto udex_speed = to_udex_speed(speed);
        TraceDbg("%!usb_device_speed! -> %!UDECX_USB_DEVICE_SPEED!", speed, udex_speed);

        UdecxUsbDeviceInitSetSpeed(init, udex_speed);
        UdecxUsbDeviceInitSetEndpointsType(init, UdecxEndpointTypeDynamic);

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_spin_lock(_Out_ WDFSPINLOCK *handle, _In_ WDFOBJECT parent)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = parent;

        if (auto err = WdfSpinLockCreate(&attr, handle)) {
                Trace(TRACE_LEVEL_ERROR, "WdfSpinLockCreate %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto init_device(_In_ UDECXUSBDEVICE device, _Inout_ device_ctx &dev)
{
        PAGED_CODE();

        WDFSPINLOCK *v[] = {
                &dev.send_lock,
                &dev.endpoint_list_lock,
                &dev.requests_lock,
        };

        for (auto i: v) {
                if (auto err = create_spin_lock(i, device)) {
                        return err;
                }
        }

        InitializeListHead(&dev.requests);
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto create_detach_request_inbuf(_In_ WDF_OBJECT_ATTRIBUTES &attr, _In_ int port)
{
        WDFMEMORY mem{}; 
        
        if (vhci::ioctl::plugout_hardware *req{};
            auto err = WdfMemoryCreate(&attr, PagedPool, 0, sizeof(*req), &mem, reinterpret_cast<PVOID*>(&req))) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreate %!STATUS!", err);
        } else {
                RtlZeroMemory(req, sizeof(*req));
                req->size = sizeof(*req);
                req->port = port;
        }

        return mem;
}

/*
 * Call UdecxUsbDevicePlugOutAndDelete if UdecxUsbDevicePlugIn was successful.
 * After UdecxUsbDevicePlugOutAndDelete the client driver can no longer use UDECXUSBDEVICE.
 * 
 * If call UdecxUsbDevicePlugOutAndDelete shortly after UdecxUsbDevicePlugIn,
 * it is highly likely it will not delete UDECXUSBDEVICE immediately. In such case 
 * UDECXUSBDEVICE.EvtCleanupCallback will be called during the deletion of VHCI device.
 * 
 * This only happens if UdecxUsbDevicePlugOutAndDelete was called when endpoints 
 * were not created and EVT_UDECX_USB_DEVICE_ENDPOINTS_CONFIGURE was not called.
 * In such case WdfObjectDelete will destroy the device immediately and without a BSOD.
 * 
 * We have to close a socket and free a port in detach() instead of device_cleanup, 
 * otherwise next plugin_hardware will fail with ST_DEV_BUSY because the socket is still connected.
 *
 * @param ext its WDFMEMORY must be WdfObjectReference-d before the call
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugout_and_delete(
        _In_ WDFDEVICE vhci, _In_ UDECXUSBDEVICE device, _In_ const device_ctx_ext &ext, _In_ int port)
{
        PAGED_CODE();
        device_state_changed(vhci, ext.attr, port, vhci::state::unplugging);

        if (auto err = UdecxUsbDevicePlugOutAndDelete(device)) {
                Trace(TRACE_LEVEL_ERROR, "dev %04x, UdecxUsbDevicePlugOutAndDelete %!STATUS!", ptr04x(device), err);
                return;
        }
        // device and device_ctx may already be destroyed, do not use

        auto force_delete = !ext.ep0_added;
        Trace(TRACE_LEVEL_INFORMATION, "dev %04x, done, force delete %d", ptr04x(device), force_delete);

        device_state_changed(vhci, ext.attr, port, vhci::state::unplugged);

        if (force_delete) { // FIXME: may cause BSOD if something changes in future UDE releases
                WdfObjectDelete(device);
        }
}

} // namespace


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::device::create(_Out_ UDECXUSBDEVICE &device, _In_ WDFDEVICE vhci, _In_ WDFMEMORY ctx_ext)
{
        PAGED_CODE();

        device = WDF_NO_HANDLE;
        auto &ext = get_device_ctx_ext(ctx_ext);

        device_init_ptr init(vhci);

        if (auto &prop = ext.properties(); 
            auto err = init ? prepare_init(init.ptr, prop.speed) : STATUS_INSUFFICIENT_RESOURCES) {
                Trace(TRACE_LEVEL_ERROR, "UDECXUSBDEVICE_INIT %!STATUS!", err);
                return err;
        }

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, device_ctx);
        attr.EvtCleanupCallback = device_cleanup;
        attr.ParentObject = vhci;

        if (auto err = UdecxUsbDeviceCreate(&init.ptr, &attr, &device)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxUsbDeviceCreate %!STATUS!", err);
                return err;
        }

        NT_ASSERT(!init); // zeroed by UdecxUsbDeviceCreate
        auto &ctx = *get_device_ctx(device);

        ctx.vhci = vhci;
        ctx.ctx_ext = ctx_ext;
        ext.ctx = &ctx;

        if (auto err = init_device(device, ctx)) {
                return err;
        }

        Trace(TRACE_LEVEL_INFORMATION, "dev %04x", ptr04x(device));
        return STATUS_SUCCESS;
}

/*
 * ObDereferenceObject must be called as soon as it is done with this thread
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::device::recv_thread_start(_In_ UDECXUSBDEVICE device)
{
        PAGED_CODE();
        const auto access = THREAD_ALL_ACCESS;

        HANDLE handle{};
        if (auto err = PsCreateSystemThread(&handle, access, nullptr, nullptr, nullptr, recv_thread_function, device)) {
                Trace(TRACE_LEVEL_ERROR, "PsCreateSystemThread %!STATUS!", err);
                return err;
        }

        auto dev = get_device_ctx(device);

        PVOID thread{};
        NT_VERIFY(NT_SUCCESS(ObReferenceObjectByHandle(handle, access, *PsThreadType, KernelMode, &thread, nullptr)));

        NT_VERIFY(!InterlockedExchangePointer(reinterpret_cast<PVOID*>(&dev->recv_thread), thread));
        NT_VERIFY(NT_SUCCESS(ZwClose(handle)));

        TraceDbg("dev %04x", ptr04x(device));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void usbip::device::async_detach_and_delete(_In_ UDECXUSBDEVICE device, _In_ bool reattach)
{
        auto &dev = *get_device_ctx(device);
        auto &ctx = *get_vhci_ctx(dev.vhci);

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = dev.vhci;

        auto req_ptr = create_request(ctx.target_self, attr);
        if (!req_ptr) {
                return;
        }
        auto request = req_ptr.get<WDFREQUEST>();

        TraceDbg("req %04x, port %d, reattach %!bool!", ptr04x(request), dev.port, reattach);

        attr.ParentObject = request;
        auto inbuf = create_detach_request_inbuf(attr, dev.port);
        if (!inbuf) {
                return;
        }

        auto code = reattach ? vhci::ioctl::PLUGOUT_HARDWARE_AND_REATTACH : vhci::ioctl::PLUGOUT_HARDWARE;

        if (auto err = WdfIoTargetFormatRequestForIoctl(ctx.target_self, request, code,
                                                        inbuf, nullptr, WDF_NO_HANDLE, nullptr)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetFormatRequestForIoctl %!STATUS!", err);
                return;
        }

        auto completion = [] (auto request, auto, auto, auto ctx)
        {
                auto st = WdfRequestGetStatus(request);
                auto port = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
                TraceDbg("req %04x, port %d, %!STATUS!", ptr04x(request), port, st);

                WdfObjectDelete(request);
        };

        auto context = reinterpret_cast<void*>(static_cast<intptr_t>(dev.port));
        WdfRequestSetCompletionRoutine(request, completion, context);

        if (!WdfRequestSend(request, ctx.target_self, WDF_NO_SEND_OPTIONS)) {
                auto err = WdfRequestGetStatus(request);
                Trace(TRACE_LEVEL_ERROR, "WdfRequestSend %!STATUS!", err);
        } else {
                req_ptr.release(); // will be deleted in the completion routine
        }
}

/*
 * Must be called from EvtIoDeviceControl of default queue only.
 * The default queue has WdfIoQueueDispatchSequential and WdfExecutionLevelPassive.
 * If you need to call it from somewhere else, call async_detach.
 *
 * @see vhci_ioctl.cpp, device_control.
 *
 * Call UdecxUsbDevicePlugOutAndDelete if UdecxUsbDevicePlugIn was successful.
 * After UdecxUsbDevicePlugOutAndDelete the client driver can no longer use UDECXUSBDEVICE.
 * 
 * FIXME:
 * If call UdecxUsbDevicePlugOutAndDelete shortly after UdecxUsbDevicePlugIn,
 * it is highly likely it will not delete UDECXUSBDEVICE immediately.
 * In such case UDECXUSBDEVICE.EvtCleanupCallback will be called during the deletion of VHCI device only.
 * The reason is unknown, there are no any clarifications in API documentation about that.
 * 
 * Therefore we have to close a socket and free a port here instead of device_cleanup, 
 * otherwise next plugin_hardware will fail with ST_DEV_BUSY because the socket is still connected.
 *
 * FIXME: inability to delete UDECXUSBDEVICE causes leak of resources, drivers' memory consumption 
 * raises over the time. Driver Verifier cannot detect it because all will be freed on driver unload.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED wdm::object_reference usbip::device::detach(
        _In_ UDECXUSBDEVICE device, _In_ bool plugout_and_delete, _In_ bool reattach)
{
        PAGED_CODE();
        wdm::object_reference thread;

        auto &dev = *get_device_ctx(device);
        auto vhci = dev.vhci;

        if (set_flag(dev.unplugged)) {
                TraceDbg("dev %04x, already unplugged", ptr04x(device));
                return thread;
        }

        if (close_socket(dev.sock())) {
                Trace(TRACE_LEVEL_INFORMATION, "dev %04x, connection closed", ptr04x(device));
                device_state_changed(dev, vhci::state::disconnected);
        }

        thread = recv_thread_join(device, dev);

        auto port = vhci::reclaim_roothub_port(device);
        if (port) {
                Trace(TRACE_LEVEL_INFORMATION, "port %d released", port);
        }

        auto &ext = get_device_ctx_ext(dev.ctx_ext);
        wdf::ObjectRef ref(dev.ctx_ext); // prevent its destruction after UdecxUsbDevicePlugOutAndDelete

        if (plugout_and_delete) {
                ::plugout_and_delete(vhci, device, ext, port);
        }

        if (reattach) {
                auto ctx = get_vhci_ctx(vhci);
                auto delayed = plugout_and_delete;
                start_attach_attempts(vhci, *ctx, ext.attr, delayed);
        }

        return thread;
}
