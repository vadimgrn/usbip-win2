/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "vhci.h"
#include "trace.h"
#include "vhci.tmh"

#include "driver.h"
#include "device.h"
#include "vhci_ioctl.h"
#include "persistent.h"

#include <libdrv/wdm_cpp.h>
#include <libdrv/pair.h>

#include <ntstrsafe.h>
#include <usbdlib.h>
#include <usbiodef.h>

namespace
{

using namespace usbip;

/*
 * WDF calls the callback at PASSIVE_LEVEL if object's handle type is WDFDEVICE.
 */
_Function_class_(EVT_WDF_DEVICE_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void vhci_cleanup(_In_ WDFOBJECT object)
{
        PAGED_CODE();
        TraceDbg("%04x", ptr04x(object));

        auto vhci = static_cast<WDFDEVICE>(object);
        auto &ctx = *get_vhci_ctx(vhci);

        set_flag(ctx.removing); // used to set in EVT_WDF_DEVICE_QUERY_REMOVE

        if (auto t = ctx.target_self) {
                WdfIoTargetClose(t);
        }

        unique_ptr(ctx.devices); // destroy
        ctx.devices = nullptr;

        ctx.devices_cnt = 0;
        ctx.usb2_ports = 0;
}

_Function_class_(EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void NTAPI canceled_on_queue(_In_ WDFQUEUE, _In_ WDFREQUEST request)
{
        TraceDbg("read request %04x", ptr04x(request));
        WdfRequestComplete(request, STATUS_CANCELLED);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_read_queue(_Out_ WDFQUEUE &queue, _In_ WDF_OBJECT_ATTRIBUTES &attr, _In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        WDF_IO_QUEUE_CONFIG cfg;
        WDF_IO_QUEUE_CONFIG_INIT(&cfg, WdfIoQueueDispatchManual);
        cfg.PowerManaged = WdfFalse;
        cfg.EvtIoCanceledOnQueue = canceled_on_queue;

        if (auto err = WdfIoQueueCreate(vhci, &cfg, &attr, &queue)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueCreate %!STATUS!", err);
                return err;
        }

        TraceDbg("vhci %04x, queue %04x", ptr04x(vhci), ptr04x(queue));
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto query_usb_ports_cnt(_In_ int def_cnt)
{
        PAGED_CODE();

        struct {
                enum { usb3, usb2 };
                int cnt[2];
        } v {def_cnt, def_cnt};

        Registry key;
        if (auto err = open_parameters_key(key, KEY_QUERY_VALUE)) {
                return v;
        }

        struct {
                const wchar_t *name;
                int &value;
        } const params[] = {
                { L"NumberOfUsb20Ports", v.cnt[v.usb2] },
                { L"NumberOfUsb30Ports", v.cnt[v.usb3] },
        };

        for (auto& [name, value]: params) {

                UNICODE_STRING value_name;
                NT_VERIFY(!RtlUnicodeStringInit(&value_name, name));

                if (ULONG val{}; auto err = WdfRegistryQueryULong(key.get(), &value_name, &val)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfRegistryQueryULong(%!USTR!) %!STATUS!", &value_name, err);
                } else {
                        value = val;
                }
        }

        return v;
}

/*
 * Ports number cannot be zero and total ports number cannot exceed 255.
 * @see userspace/usbip/usbip.cpp, MAX_HUB_PORTS
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void set_usb_ports_cnt(_Inout_ int &usb2_ports, _Inout_ int &usb3_ports)
{
        PAGED_CODE();

        enum { MIN_PORTS = 1, DEF_PORTS = 30, MAX_PORTS = 254, MAX_TOTAL_PORTS };
        auto v = query_usb_ports_cnt(DEF_PORTS);

        for (int total = 0; auto &n: v.cnt) {

                n = min(MAX_PORTS, max(MIN_PORTS, n));

                if (total + n > MAX_TOTAL_PORTS) {
                        n = MAX_TOTAL_PORTS - total;
                }

                NT_ASSERT(n >= MIN_PORTS);
                NT_ASSERT(n <= MAX_PORTS);

                total += n;
                NT_ASSERT(total <= MAX_TOTAL_PORTS);
        }

        usb2_ports = v.cnt[v.usb2];
        usb3_ports = v.cnt[v.usb3];
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto alloc_devices(_Inout_ vhci_ctx &vhci)
{
        PAGED_CODE();

        int usb2_ports{};
        int usb3_ports{};
        set_usb_ports_cnt(usb2_ports, usb3_ports);

        auto n = usb2_ports + usb3_ports;
        NT_ASSERT(n > 0);

        unique_ptr ptr(NonPagedPoolNx, n*sizeof(*vhci.devices));
        if (!ptr) {
                Trace(TRACE_LEVEL_ERROR, "Cannot allocate array UDECXUSBDEVICE[%d]", n);
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        vhci.usb2_ports = usb2_ports;
        vhci.devices_cnt = n;
        vhci.devices = ptr.release<UDECXUSBDEVICE>();

        Trace(TRACE_LEVEL_INFORMATION, "usb2 ports %d, UDECXUSBDEVICE[%d]", vhci.usb2_ports, vhci.devices_cnt);
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_target_self(_Out_ WDFIOTARGET &target, _In_ WDF_OBJECT_ATTRIBUTES &attr, _In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        if (auto err = WdfIoTargetCreate(vhci, &attr, &target)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetCreate %!STATUS!", err);
                return err;
        }

        auto fdo = WdfDeviceWdmGetDeviceObject(vhci);

        WDF_IO_TARGET_OPEN_PARAMS params;
        WDF_IO_TARGET_OPEN_PARAMS_INIT_EXISTING_DEVICE(&params, fdo);

        if (auto err = WdfIoTargetOpen(target, &params)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetOpen %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void init_constants(
        _Inout_ unsigned int &max_tries, _Inout_ unsigned int &init_delay, _Inout_ unsigned int &max_delay)
{
        PAGED_CODE();

        enum {
                HOUR = 60*60, DAY = 24*HOUR, // seconds
                DEF_INIT_DELAY = 15, DEF_MAX_DELAY = HOUR,
                MIN_DELAY = 1, MAX_DELAY = DAY 
        };

        Registry key; 
        if (NT_ERROR(open_parameters_key(key, KEY_QUERY_VALUE))) {
                init_delay = DEF_INIT_DELAY;
                max_delay = DEF_MAX_DELAY;
                return;
        }

        struct {
                const wchar_t *name;
                unsigned int &val;
        } const v[] {
                { L"ReattachMaxTries", max_tries },
                { L"ReattachInitDelay", init_delay },
                { L"ReattachMaxDelay", max_delay },
        };

        for (auto& [name, value]: v) {

                UNICODE_STRING value_name;
                RtlUnicodeStringInit(&value_name, name);

                if (ULONG val = 0; auto err = WdfRegistryQueryULong(key.get<WDFKEY>(), &value_name, &val)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfRegistryQueryULong('%!USTR!') %!STATUS!", &value_name, err);
                } else {
                        value = static_cast<unsigned int>(val);
                }
        }
        
        if (!init_delay) {
                init_delay = DEF_INIT_DELAY;
        }

        if (!max_delay) {
                max_delay = DEF_MAX_DELAY;
        }

        if (init_delay > max_delay) {
                swap(init_delay, max_delay);
        }

        if (init_delay < MIN_DELAY) {
                init_delay = MIN_DELAY;
        }

        if (max_delay > MAX_DELAY) {
                max_delay = MAX_DELAY;
        }

        TraceDbg("%S=%u, %S=%u, %S=%u", v[0].name, max_tries, v[1].name, init_delay, v[2].name, max_delay);

        NT_ASSERT(init_delay >= MIN_DELAY);
        NT_ASSERT(max_delay <= MAX_DELAY);
        NT_ASSERT(init_delay <= max_delay);
}

using init_func_t = NTSTATUS(WDFDEVICE);

_Function_class_(init_func_t)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto init_context(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        auto &ctx = *get_vhci_ctx(vhci);
        InitializeListHead(&ctx.fileobjects);

        if (auto err = alloc_devices(ctx)) {
                return err;
        }

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = vhci;

        if (auto err = WdfSpinLockCreate(&attr, &ctx.devices_lock)) {
                Trace(TRACE_LEVEL_ERROR, "WdfSpinLockCreate %!STATUS!", err);
                return err;
        }

        if (auto err = WdfWaitLockCreate(&attr, &ctx.events_lock)) {
                Trace(TRACE_LEVEL_ERROR, "WdfWaitLockCreate %!STATUS!", err);
                return err;
        }

        if (auto err = create_target_self(ctx.target_self, attr, vhci)) {
                return err;
        }

        if (auto err = create_read_queue(ctx.reads, attr, vhci)) {
                return err;
        }

        init_constants(ctx.reattach_max_tries, ctx.reattach_init_delay, ctx.reattach_max_delay);
        return STATUS_SUCCESS;
}

_Function_class_(init_func_t)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_interfaces(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        const GUID* v[] = {
                &GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
                &vhci::GUID_DEVINTERFACE_USB_HOST_CONTROLLER
        };

        for (auto guid: v) {
                if (auto err = WdfDeviceCreateDeviceInterface(vhci, guid, nullptr)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfDeviceCreateDeviceInterface(%!GUID!) %!STATUS!", guid, err);
                        return err;
                }
        }

        return STATUS_SUCCESS;
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
        const GUID* supported[] = {
                &GUID_USB_CAPABILITY_CHAINED_MDLS, 
                &GUID_USB_CAPABILITY_SELECTIVE_SUSPEND, // class extension reports it as supported without invoking the callback
//              &GUID_USB_CAPABILITY_FUNCTION_SUSPEND,
                &GUID_USB_CAPABILITY_DEVICE_CONNECTION_HIGH_SPEED_COMPATIBLE, 
                &GUID_USB_CAPABILITY_DEVICE_CONNECTION_SUPER_SPEED_COMPATIBLE 
        };

        auto st = STATUS_NOT_SUPPORTED;

        for (auto i: supported) {
                if (*i == *CapabilityType) {
                        st = STATUS_SUCCESS;
                        break;
                }
        }

        *ResultLength = 0;
        return st;
}

/*
 * If TargetState is WdfPowerDeviceD3Final, you should assume that the system is being turned off, 
 * the device is about to be removed, or a resource rebalance is in progress.
 * 
 * Cannot be used for actions that are done in EVT_WDF_DEVICE_QUERY_REMOVE 
 * because if the device is in D1-3 state, this callback will not be called again. 
 * The second reason is that if something (app, driver) holds a reference to WDFDEVICE, 
 * EVT_WDF_DEVICE_D0_EXIT(WdfPowerDeviceD3Final) will not be called.
 */
_Function_class_(EVT_WDF_DEVICE_D0_EXIT)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED NTSTATUS NTAPI vhci_d0_exit(_In_ WDFDEVICE, _In_ WDF_POWER_DEVICE_STATE TargetState)
{
        PAGED_CODE();
        TraceDbg("TargetState %!WDF_POWER_DEVICE_STATE!", TargetState);
        return STATUS_SUCCESS;
}

/*
 * You should not make this callback function pageable.
 */
_Function_class_(EVT_WDF_DEVICE_D0_ENTRY)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
/*PAGED*/ NTSTATUS NTAPI vhci_d0_entry(_In_ WDFDEVICE, _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
        PAGED_CODE();
        TraceDbg("PreviousState %!WDF_POWER_DEVICE_STATE!", PreviousState);
        return STATUS_SUCCESS;
}

/*
 * Do not call WdfIoQueuePurgeSynchronously from the following queue object event callback functions,
 * regardless of the queue with which the event callback function is associated:
 * EvtIoDefault, EvtIoDeviceControl, EvtIoInternalDeviceControl, EvtIoRead, EvtIoWrite.
 */
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED void purge_read_queue(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        auto &ctx = *get_vhci_ctx(vhci);
        TraceDbg("%04x", ptr04x(ctx.reads));

        wdf::WaitLock lck(ctx.events_lock);
        WdfIoQueuePurgeSynchronously(ctx.reads);
}

/* 
 * This callback determines whether a specified device can be stopped and removed.
 * The framework does not synchronize the EvtDeviceQueryRemove callback function 
 * with other PnP and power management callback functions.
 * 
 * VHCI device will not be removed until all FILEOBJECT-s will be closed.
 * The uninstaller will block on the command that removes VHCI device node.
 * Cancelling read requests forces apps to close handle of VHCI device.
 * 
 * FIXME: can be called several times (if IRP_MN_CANCEL_REMOVE_DEVICE was issued?).
 */
_Function_class_(EVT_WDF_DEVICE_QUERY_REMOVE)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED NTSTATUS vhci_query_remove(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();
        TraceDbg("%04x", ptr04x(vhci));
        
        if (auto ctx = get_vhci_ctx(vhci); true) {
                set_flag(ctx->removing);
        }

        vhci::detach_all_devices(vhci, true);
        purge_read_queue(vhci); // detach notifications may not be received

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto create_collection(_Out_ WDFCOLLECTION &result, _In_ WDFOBJECT parent)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = parent;

        return WdfCollectionCreate(&attr, &result);
}

_Function_class_(EVT_WDF_DEVICE_FILE_CREATE)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED void device_file_create(_In_ WDFDEVICE vhci, _In_ WDFREQUEST request, _In_ WDFFILEOBJECT fileobj)
{
        PAGED_CODE();
        TraceDbg("vhci %04x, fobj %04x", ptr04x(vhci), ptr04x(fileobj));

        auto &fobj = *get_fileobject_ctx(fileobj);
        InitializeListHead(&fobj.entry);

        auto st = create_collection(fobj.events, fileobj);

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfCollectionCreate %!STATUS!", st);
        } else if (auto v = get_vhci_ctx(vhci)) {
                wdf::WaitLock lck(v->events_lock);
                InsertTailList(&v->fileobjects, &fobj.entry);
        }

        WdfRequestComplete(request, st);
}

_Function_class_(EVT_WDF_FILE_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED void file_cleanup(_In_ WDFFILEOBJECT fileobj)
{
        PAGED_CODE();
        TraceDbg("fobj %04x", ptr04x(fileobj));

        auto &fobj = *get_fileobject_ctx(fileobj); 
        auto vhci = WdfFileObjectGetDevice(fileobj);
        auto &ctx = *get_vhci_ctx(vhci);

        wdf::WaitLock lck(ctx.events_lock);

        RemoveEntryList(&fobj.entry);
        InitializeListHead(&fobj.entry);
        
        if (fobj.process_events) {
                --ctx.events_subscribers;
        }
}

/*
 * Drivers for USB devices must not specify IdleCanWakeFromS0. 
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto initialize(_Inout_ WDFDEVICE_INIT *init)
{
        PAGED_CODE();

        {
                WDF_PNPPOWER_EVENT_CALLBACKS cb;
                WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&cb);

                cb.EvtDeviceD0Exit = vhci_d0_exit;
                cb.EvtDeviceD0Entry = vhci_d0_entry;
                cb.EvtDeviceQueryRemove = vhci_query_remove;

                WdfDeviceInitSetPnpPowerEventCallbacks(init, &cb);
        }

        {
                WDF_REMOVE_LOCK_OPTIONS opts;
                WDF_REMOVE_LOCK_OPTIONS_INIT(&opts, WDF_REMOVE_LOCK_OPTION_ACQUIRE_FOR_IO);
                WdfDeviceInitSetRemoveLockOptions(init, &opts);
        }

        {
                WDF_OBJECT_ATTRIBUTES attr;
                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, request_ctx);
                WdfDeviceInitSetRequestAttributes(init, &attr);
        }

        {
                WDF_FILEOBJECT_CONFIG cfg;
                WDF_FILEOBJECT_CONFIG_INIT(&cfg, device_file_create, WDF_NO_EVENT_CALLBACK, file_cleanup);
                cfg.FileObjectClass = WdfFileObjectWdfCanUseFsContext;

                WDF_OBJECT_ATTRIBUTES attr;
                WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, fileobject_ctx);

                WdfDeviceInitSetFileObjectConfig(init, &cfg, &attr);
        }

        WdfDeviceInitSetCharacteristics(init, FILE_AUTOGENERATED_DEVICE_NAME, true);

        if (auto err = WdfDeviceInitAssignSDDLString(init, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDeviceInitAssignSDDLString %!STATUS!", err);
                return err;
        }

        if (auto err = UdecxInitializeWdfDeviceInit(init)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxInitializeWdfDeviceInit %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}

_Function_class_(init_func_t)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto add_usbdevice_emulation(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();
        auto &ctx = *get_vhci_ctx(vhci);

        UDECX_WDF_DEVICE_CONFIG cfg;
        UDECX_WDF_DEVICE_CONFIG_INIT(&cfg, query_usb_capability);

        cfg.NumberOfUsb20Ports = static_cast<USHORT>(ctx.usb2_ports);
        cfg.NumberOfUsb30Ports = static_cast<USHORT>(ctx.devices_cnt - ctx.usb2_ports);

        NT_ASSERT(cfg.NumberOfUsb20Ports + cfg.NumberOfUsb30Ports == ctx.devices_cnt);

        if (auto err = UdecxWdfDeviceAddUsbDeviceEmulation(vhci, &cfg)) {
                Trace(TRACE_LEVEL_ERROR, "UdecxWdfDeviceAddUsbDeviceEmulation %!STATUS!", err);
                return err;
        }

        Trace(TRACE_LEVEL_INFORMATION, "NumberOfUsb20Ports %d, NumberOfUsb30Ports %d", 
                                        cfg.NumberOfUsb20Ports, cfg.NumberOfUsb30Ports);

        return STATUS_SUCCESS;
}

_Function_class_(init_func_t)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto configure(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();

        {
                WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idle_settings;
                WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idle_settings, IdleCannotWakeFromS0);

                if (auto err = WdfDeviceAssignS0IdleSettings(vhci, &idle_settings)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfDeviceAssignS0IdleSettings %!STATUS!", err);
                        return err;
                }
        }

/*
        {
                WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS wake;
                WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS_INIT(&wake);
                wake.

                if (auto err = WdfDeviceAssignSxWakeSettings(vhci, &wake)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfDeviceAssignSxWakeSettings %!STATUS!", err);
                        return err;
                }
        }

        {
                WDF_DEVICE_POWER_CAPABILITIES caps;
                WDF_DEVICE_POWER_CAPABILITIES_INIT(&caps);
                WdfDeviceSetPowerCapabilities(vhci, &caps);
        }

        {
                WDF_DEVICE_PNP_CAPABILITIES caps;
                WDF_DEVICE_PNP_CAPABILITIES_INIT(&caps);
                WdfDeviceSetPnpCapabilities(vhci, &caps);
        }
*/
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_vhci(_Out_ WDFDEVICE &vhci, _In_ WDFDEVICE_INIT *init)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr; // default parent (WDFDRIVER) is OK
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, vhci_ctx);
        attr.EvtCleanupCallback = vhci_cleanup;

        if (auto err = WdfDeviceCreate(&init, &attr, &vhci)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDeviceCreate %!STATUS!", err);
                return err;
        }

        init_func_t* const functions[] { init_context, configure, create_interfaces, 
                                         add_usbdevice_emulation, vhci::create_queues };

        for (auto f: functions) {
                if (auto err = f(vhci)) {
                        return err;
                }
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto get_port_range(_In_ const vhci_ctx &vhci, _In_ usb_device_speed speed)
{
        struct{ int begin;  int end; } r;

        if (speed < USB_SPEED_SUPER) {
                r.begin = 0;
                r.end = vhci.usb2_ports;
        } else {
                r.begin = vhci.usb2_ports;
                r.end = vhci.devices_cnt;
        }

        return r;
}

_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED auto make_device_state(
        _In_ WDFOBJECT parent, _In_ const device_attributes &dev, _In_ int port, _In_ vhci::state state)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.EvtDestroyCallback = [] (auto p) { TraceDbg("destroy %04x", ptr04x(p)); };
        attr.ParentObject = parent;

        WDFMEMORY mem{};
        vhci::device_state *r{};
        if (auto err = WdfMemoryCreate(&attr, PagedPool, 0, sizeof(*r), &mem, reinterpret_cast<PVOID*>(&r))) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreate %!STATUS!", err);
                return mem;
        }

        RtlZeroMemory(r, sizeof(*r));
        r->size = sizeof(*r);
        r->state = state;

        if (auto err = fill(*r, dev, port)) {
                WdfObjectDelete(mem);
                mem = WDF_NO_HANDLE;
        }

        TraceDbg("%04x", ptr04x(mem));
        return mem;
}

/*
 * vhci_ctx::events_lock must be acquired.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void process_event(
        _In_ WDFQUEUE queue, _Inout_ fileobject_ctx &fobj, _In_ WDFMEMORY evt, _In_ ULONG max_events)
{
        PAGED_CODE();

        auto fileobj = get_handle(&fobj);
        WDFREQUEST request{};

        switch (auto st = WdfIoQueueRetrieveRequestByFileObject(queue, fileobj, &request)) {
        case STATUS_SUCCESS:
                NT_ASSERT(!WdfCollectionGetCount(fobj.events));
                vhci::complete_read(request, evt);
                break;
        case STATUS_NO_MORE_ENTRIES:
                if (auto err = WdfCollectionAdd(fobj.events, evt)) { // append and increment reference count
                        Trace(TRACE_LEVEL_ERROR, "WdfCollectionAdd %!STATUS!", err);
                } else if (auto cnt = WdfCollectionGetCount(fobj.events); cnt > max_events) {
                        auto head = WdfCollectionGetFirstItem(fobj.events);
                        WdfCollectionRemove(fobj.events, head); // decrements reference count

                        TraceDbg("fobj %04x, drop %04x[0], add %04x[%lu]",
                                  ptr04x(fileobj), ptr04x(head), ptr04x(evt), --cnt - 1);
                } else {
                        TraceDbg("fobj %04x, add %04x[%lu]", ptr04x(fileobj), ptr04x(evt), cnt - 1);
                }
                break;
        default:
                Trace(TRACE_LEVEL_ERROR, "WdfIoQueueRetrieveRequestByFileObject %!STATUS!", st);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void process_event(_In_ vhci_ctx &vhci, _In_ WDFMEMORY evt)
{
        PAGED_CODE();

        int cnt = 0;
        wdf::WaitLock lck(vhci.events_lock);

        for (auto head = &vhci.fileobjects, entry = head->Flink; entry != head; entry = entry->Flink) {
                auto &fobj = *CONTAINING_RECORD(entry, fileobject_ctx, entry);
                if (fobj.process_events) {
                        process_event(vhci.reads, fobj, evt, 2*vhci.devices_cnt);
                        ++cnt;
                }
        }

        NT_ASSERT(cnt == vhci.events_subscribers);
}

} // namespace


/*
 * usb2.0 devices don't work in usb3.x ports, and visa versa, tested.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
int usbip::vhci::claim_roothub_port(_In_ UDECXUSBDEVICE device)
{
        auto &dev = *get_device_ctx(device);
        auto &vhci = *get_vhci_ctx(dev.vhci); 

        NT_ASSERT(!dev.port);
        int port = 0;

        auto [begin, end] = get_port_range(vhci, dev.speed());

        wdf::Lock lck(vhci.devices_lock); // function must be resident, do not use PAGED

        for (auto i = begin; i < end; ++i) {
                NT_ASSERT(i < vhci.devices_cnt);

                if (auto &handle = vhci.devices[i]; !handle) {
                        WdfObjectReference(handle = device);
                        
                        port = i + 1;
                        NT_ASSERT(is_valid_port(vhci, port));

                        dev.port = port;
                        break;
                }
        }

        lck.release();
        return port;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
int usbip::vhci::reclaim_roothub_port(_In_ UDECXUSBDEVICE device)
{
        auto &dev = *get_device_ctx(device);
        auto &vhci = *get_vhci_ctx(dev.vhci); 

        int portnum = 0;

        wdf::Lock lck(vhci.devices_lock); 
        if (auto &port = dev.port) {
                NT_ASSERT(is_valid_port(vhci, port));
                portnum = port;

                auto &handle = vhci.devices[port - 1];
                NT_ASSERT(handle == device);

                handle = WDF_NO_HANDLE;
                port = 0;
        }
        lck.release(); // explicit call to satisfy code analyzer and get rid of warning C28166

        if (portnum) {
                WdfObjectDereference(device);
        }
        
        return portnum;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
wdf::ObjectRef usbip::vhci::get_device(_In_ WDFDEVICE vhci, _In_ int port)
{
        auto &ctx = *get_vhci_ctx(vhci);

        wdf::ObjectRef ptr;
        if (!is_valid_port(ctx, port)) {
                return ptr;
        }

        wdf::Lock lck(ctx.devices_lock); 
        if (auto handle = ctx.devices[port - 1]) {
                NT_ASSERT(get_device_ctx(handle)->port == port);
                ptr.reset(handle); // adds reference
        }
        lck.release(); // explicit call to satisfy code analyzer and get rid of warning C28166

        return ptr;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::vhci::detach_all_devices(_In_ WDFDEVICE vhci, _In_ bool async)
{
        PAGED_CODE();

        TraceDbg("%04x", ptr04x(vhci));
        auto &ctx = *get_vhci_ctx(vhci);

        for (int port = 1; port <= ctx.devices_cnt; ++port) {
 
                if (auto dev = get_device(vhci, port); auto hdev = dev.get<UDECXUSBDEVICE>()) {
                        if (async) {
                                device::async_detach_and_delete(hdev);
                        } else {
                                device::detach_and_delete(hdev);
                        }
                }
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::vhci::fill(_Out_ imported_device &dev, _In_ const device_attributes &r, _In_ int port)
{
        PAGED_CODE();

//      imported_device_location
        dev.port = port;
        if (auto err = copy(dev.host, sizeof(dev.host), r.node_name, 
                            dev.service, sizeof(dev.service), r.service_name,  
                            dev.busid, sizeof(dev.busid), r.busid)) {
                return err;
        }
//
        static_cast<imported_device_properties&>(dev) = r.properties;
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::vhci::complete_read(_In_ WDFREQUEST request, _In_ WDFMEMORY evt)
{
        PAGED_CODE();

        device_state *dst{};
        auto dst_sz = sizeof(*dst);

        auto st = WdfRequestRetrieveOutputBuffer(request, dst_sz, reinterpret_cast<PVOID*>(&dst), nullptr);
        
        if (NT_SUCCESS(st)) {
                size_t size{};
                *dst = *reinterpret_cast<device_state*>(WdfMemoryGetBuffer(evt, &size));
                NT_ASSERT(size == dst_sz);
        } else {
                Trace(TRACE_LEVEL_ERROR, "WdfRequestRetrieveOutputBuffer %!STATUS!", st);
                dst_sz = 0;
        }

        TraceDbg("fobj %04x, req %04x, device_state %04x, %!STATUS!", ptr04x(WdfRequestGetFileObject(request)), 
                  ptr04x(request), ptr04x(evt), st);

        WdfRequestCompleteWithInformation(request, st, dst_sz);
}

/*
 * Each WDFMEMORY object is shared between FILEOBJECT-s, thus parent is set to WDFDEVICE.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::vhci::device_state_changed(
        _In_ WDFDEVICE vhci, _In_ const device_attributes &attr, _In_ int port, _In_ state state)
{
        PAGED_CODE();

        auto &ctx = *get_vhci_ctx(vhci);
        auto subscribers = ctx.events_subscribers;

        TraceDbg("%!USTR!:%!USTR!/%!USTR!, port %d, %!vhci_state!, subscribers %d", 
                  &attr.node_name, &attr.service_name, &attr.busid, port, int(state), subscribers);

        if (!subscribers) {
                wdf::WaitLock lck(ctx.events_lock);
                if (!ctx.events_subscribers) {
                        return; // don't create device_state unnecessarily
                }
        }

        if (auto evt = make_device_state(vhci, attr, port, state)) {
                process_event(ctx, evt);
                WdfObjectDelete(evt); // will be deleted after its reference count becomes zero
        } else {
                Trace(TRACE_LEVEL_ERROR, "Failed to create state '%!vhci_state!'", int(state));
        }
}

/*
 * Drivers cannot call WdfObjectDelete to delete WDFDEVICE.
 * WdfObjectDelete: Attempt to Delete an Object Which does not allow WdfDeleteObject, STATUS_CANNOT_DELETE.
 */
_Function_class_(EVT_WDF_DRIVER_DEVICE_ADD)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::DeviceAdd(_In_ WDFDRIVER, _Inout_ WDFDEVICE_INIT *init)
{
        PAGED_CODE();

        if (auto err = initialize(init)) {
                return err;
        }

        WDFDEVICE vhci{};
        if (auto err = create_vhci(vhci, init)) { 
                return err; // the framework handles deletion of WDFDEVICE
        }

        Trace(TRACE_LEVEL_INFORMATION, "vhci %04x", ptr04x(vhci));
        plugin_persistent_devices(vhci);

        return STATUS_SUCCESS;
}
