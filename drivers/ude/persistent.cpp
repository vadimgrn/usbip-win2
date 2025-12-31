/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "persistent.h"
#include "trace.h"
#include "persistent.tmh"

#include "context.h"
#include "driver.h"

#include <libdrv/strconv.h>
#include <resources/messages.h>

#include <ntstrsafe.h>

namespace 
{

using namespace usbip;

/*
 * Context space for WDFREQUEST which is used for ioctl::plugin_hardware.
 */
struct attach_ctx
{
        WDFSTRING device_str; // host,port,busid
        WDFTIMER timer;
        WDFWORKITEM wi;

        WDFMEMORY inbuf;
        WDFMEMORY outbuf;

        unsigned int retry_cnt;
        unsigned int delay;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(attach_ctx, get_attach_ctx);

WDF_DECLARE_CONTEXT_TYPE(WDFREQUEST); // WdfObjectGet_WDFREQUEST
inline auto& get_request(_In_ WDFWORKITEM wi)
{
        return *WdfObjectGet_WDFREQUEST(wi);
}

constexpr auto empty(_In_ const UNICODE_STRING &s)
{
        return libdrv::empty(s) || !*s.Buffer;
}

/*
 * WskGetAddressInfo() can return STATUS_INTERNAL_ERROR(0xC00000E5), but after some delay it will succeed.
 * This can happen after reboot if dnscache(?) service is not ready yet.
 */
constexpr auto can_retry(_In_ NTSTATUS status)
{
        switch (as_usbip_status(status)) {
        case USBIP_ERROR_ABI:
        case USBIP_ERROR_VERSION:
        case USBIP_ERROR_PROTOCOL:
                return false; // unrecoverable errors
        }

        return status != STATUS_CANCELLED;
}

/*
 * @param retry_cnt from zero
 */
constexpr auto can_retry(_In_ unsigned int retry_cnt, _In_ unsigned int max_attempts)
{
        return !max_attempts || retry_cnt < max_attempts;
}
static_assert(can_retry(0, 1));
static_assert(!can_retry(1, 1));

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_delay(_Inout_ unsigned int &delay, _In_ unsigned int max_delay)
{
        PAGED_CODE();

        auto cur = delay;
        NT_ASSERT(cur && cur <= max_delay);

        if (cur != max_delay) {
                auto next = cur*2ULL;
                delay = next < max_delay ? static_cast<unsigned int>(next) : max_delay;
        }

        return cur;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_persistent_devices(_In_ WDFKEY key)
{
        PAGED_CODE();
        ObjectDelete col;
        
        if (WDFCOLLECTION h{};
            auto err = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &h)) {
                Trace(TRACE_LEVEL_ERROR, "WdfCollectionCreate %!STATUS!", err);
                return col;
        } else {
                col.reset(h);
        }

        WDF_OBJECT_ATTRIBUTES str_attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&str_attr);
        str_attr.ParentObject = col.get();

        UNICODE_STRING value_name;
        RtlUnicodeStringInit(&value_name, persistent_devices_value_name);

        if (auto err = WdfRegistryQueryMultiString(key, &value_name, &str_attr, col.get<WDFCOLLECTION>())) {
                Trace(TRACE_LEVEL_ERROR, "WdfRegistryQueryMultiString('%!USTR!') %!STATUS!", &value_name, err);
                col.reset();
        }

        return col;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_persistent_devices(_Inout_ ULONG &cnt, _In_ ULONG max_cnt)
{
        PAGED_CODE();
        ObjectDelete col;

        if (Registry key; NT_SUCCESS(open(key, DriverRegKeyPersistentState))) {
                col = get_persistent_devices(key.get());
        }

        cnt = col ? min(WdfCollectionGetCount(col.get<WDFCOLLECTION>()), max_cnt) : 0;
        return col;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto parse_device_str(_Inout_ vhci::ioctl::plugin_hardware &r, _In_ const UNICODE_STRING &str)
{
        PAGED_CODE();

        UNICODE_STRING host;
        UNICODE_STRING service;
        UNICODE_STRING busid;

        const auto sep = L',';

        libdrv::split(host, busid, str, sep);
        if (empty(host)) {
                return STATUS_INVALID_PARAMETER;
        }

        libdrv::split(service, busid, busid, sep);
        if (empty(service) || empty(busid)) {
                return STATUS_INVALID_PARAMETER;
        }

        return copy(r.host, sizeof(r.host), host, 
                    r.service, sizeof(r.service), service, 
                    r.busid, sizeof(r.busid), busid);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool create_inbuf(
        _Inout_ WDFMEMORY &result, _Inout_ vhci::ioctl::plugin_hardware* &req, _Inout_ WDF_OBJECT_ATTRIBUTES &attr)
{
        PAGED_CODE();
        NT_ASSERT(!result);

        if (auto err = WdfMemoryCreate(&attr, PagedPool, 0, sizeof(*req), &result, reinterpret_cast<PVOID*>(&req))) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreate %!STATUS!", err);
                req = nullptr;
        } else {
                RtlZeroMemory(req, sizeof(*req));
                req->size = sizeof(*req);

        }

        return req;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool create_outbuf(
        _Inout_ WDFMEMORY &result, _In_ vhci::ioctl::plugin_hardware *req, _Inout_ WDF_OBJECT_ATTRIBUTES &attr)
{
        PAGED_CODE();
        NT_ASSERT(!result);

        constexpr auto len = offsetof(vhci::ioctl::plugin_hardware, port) + sizeof(req->port);

        if (auto err = WdfMemoryCreatePreallocated(&attr, req, len, &result)) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreatePreallocated %!STATUS!", err);
        }

        return result;
}

_Function_class_(EVT_WDF_REQUEST_COMPLETION_ROUTINE)
_IRQL_requires_same_
void on_plugin_hardware(
        _In_ WDFREQUEST request, _In_ WDFIOTARGET, _In_ WDF_REQUEST_COMPLETION_PARAMS*, _In_ WDFCONTEXT context)
{
        ObjectDelete ptr(request);

        auto &req = *get_attach_ctx(request);
        auto &vhci = *get_vhci_ctx(static_cast<WDFDEVICE>(context));

        auto retry_cnt = req.retry_cnt++; // from zero

        auto st = WdfRequestGetStatus(request);
        auto failed = NT_ERROR(st); 
        auto retry = failed && can_retry(st) && can_retry(retry_cnt, vhci.reattach_max_attempts);

        if (auto ok = retry && !get_flag(vhci.removing); !ok) {

                auto s = !failed ? " " : // "" prints as "<NULL>"
                        !retry ? ", cannot retry" : 
                        ", vhci is being removing";

                TraceDbg("req %04x, %!STATUS!%s", ptr04x(request), st, s);
                return;
        }

        WDF_REQUEST_REUSE_PARAMS params;
        WDF_REQUEST_REUSE_PARAMS_INIT(&params, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);

        if (auto err = WdfRequestReuse(request, &params)) {
                Trace(TRACE_LEVEL_ERROR, "WdfRequestReuse(%04x) %!STATUS!", ptr04x(request), err);
                return;
        }

        auto delay = get_delay(req.delay, vhci.reattach_max_delay);
        NT_VERIFY(!WdfTimerStart(req.timer, WDF_REL_TIMEOUT_IN_SEC(delay))); // @see on_attach_timer

        TraceDbg("req %04x, %!STATUS!, retry #%u in %u secs.", ptr04x(request), st, retry_cnt, delay);
        ptr.release();
}

/*
 * WDF_REQUEST_SEND_OPTIONS opts;
 * WDF_REQUEST_SEND_OPTIONS_INIT(&opts, 0);
 * WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&opts, WDF_REL_TIMEOUT_IN_SEC(60));
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void send_plugin_hardware(
        _In_ WDFOBJECT vhci, _In_ WDFIOTARGET target,
        _In_ WDFMEMORY inbuf, _In_ WDFMEMORY outbuf,
        _Inout_ ObjectDelete &req)
{
        auto request = req.get<WDFREQUEST>();
        TraceDbg("req %04x", ptr04x(request));

        if (auto err = WdfIoTargetFormatRequestForIoctl(target, request,
                                vhci::ioctl::PLUGIN_HARDWARE, inbuf, nullptr, outbuf, nullptr)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetFormatRequestForIoctl %!STATUS!", err);
                return;
        }

        WdfRequestSetCompletionRoutine(request, on_plugin_hardware, vhci);

        if (!WdfRequestSend(request, target, WDF_NO_SEND_OPTIONS)) {
                auto err = WdfRequestGetStatus(request);
                Trace(TRACE_LEVEL_ERROR, "WdfRequestSend %!STATUS!", err);
        } else {
                req.release();
        }
}

/*
 * If an EvtTimerFunc callback function running at PASSIVE_LEVEL calls WdfObjectDelete,
 * this results in deadlock. Either wait for the parent to delete the timer automatically
 * when the device is removed - or, if you need to delete early, schedule a work item
 * from the timer callback to delete the timer.
 *
 * It's weird to use WDFWORKITEM instead of setting ExecutionLevel=WdfExecutionLevelPassive for timer.
 * But there is a problem: request deletion in EvtTimerFunc causes BSOD.
 * Request is a parent of a timer, the timer will be implicitly deleted when the request is deleted.
 * The workaround is to use WDFWORKITEM explicitly, it's OK to delete it from EVT_WDF_WORKITEM.
 * PASSIVE_LEVEL is required to call is_persistent().
 */
_Function_class_(EVT_WDF_TIMER)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void on_attach_timer(_In_ WDFTIMER timer)
{
        auto req = WdfTimerGetParentObject(timer);
        auto r = get_attach_ctx(req);

        WdfWorkItemEnqueue(r->wi); // @see reattach
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool create_timer(_Inout_ WDFTIMER &result, _Inout_ WDF_OBJECT_ATTRIBUTES &attr)
{
        PAGED_CODE();
        NT_ASSERT(!result);

        WDF_TIMER_CONFIG cfg;
        WDF_TIMER_CONFIG_INIT(&cfg, on_attach_timer);
        cfg.TolerableDelay = TolerableDelayUnlimited;

        if (auto err = WdfTimerCreate(&cfg, &attr, &result)) {
                Trace(TRACE_LEVEL_ERROR, "WdfTimerCreate %!STATUS!", err);
        }

        return result;
}

/*
 * Removing a device from persistents allows you to stop attach attempts.
 */ 
_Function_class_(EVT_WDF_WORKITEM)
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
PAGED void NTAPI reattach(_In_ WDFWORKITEM wi)
{
        PAGED_CODE();

        auto dev = WdfWorkItemGetParentObject(wi);
        auto &vhci = *get_vhci_ctx(dev);

        ObjectDelete req(get_request(wi));
        auto &r = *get_attach_ctx(req.get());

        if (auto rm = get_flag(vhci.removing); !rm && is_persistent(vhci, r.device_str)) {
                send_plugin_hardware(dev, vhci.target_self, r.inbuf, r.outbuf, req);
        } else {
                auto s = rm ? "vhci is being removing" : "is no longer persistent";
                TraceDbg("req %04x, %s", ptr04x(req.get()), s);
        }
}

/*
 * When your driver calls WdfWorkItemCreate, it must supply a handle
 * to either a framework device object or a framework queue object.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool create_attach_workitem(_Inout_ WDFWORKITEM &wi, _In_ WDFOBJECT parent, _In_ WDFREQUEST request)
{
        PAGED_CODE();

        NT_ASSERT(!wi);
        NT_ASSERT(parent != request);

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, WDFREQUEST);
        attr.ParentObject = parent;

        attr.EvtCleanupCallback = [] (auto obj)
        {
                auto req = get_request(static_cast<WDFWORKITEM>(obj));
                TraceDbg("cleanup, req %04x", ptr04x(req));
        };

        WDF_WORKITEM_CONFIG cfg;
        WDF_WORKITEM_CONFIG_INIT(&cfg, reattach);

        if (auto err = WdfWorkItemCreate(&cfg, &attr, &wi)) {
                Trace(TRACE_LEVEL_ERROR, "WdfWorkItemCreate %!STATUS!", err);
        } else { // init context
                get_request(wi) = request;
        }

        return wi;
}

/*
 * Calling WdfWorkItemFlush from within the WDFWORKITEM callback will lead to deadlock.
 * It happens when request is deleted in EVT_WDF_WORKITEM and WdfWorkItemFlush is called here.
 */ 
_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void cleanup_attach_request(_In_ WDFOBJECT obj)
{
        PAGED_CODE();

        TraceDbg("%04x", ptr04x(obj)); 
        auto &r = *get_attach_ctx(obj);

        if (auto h = r.wi) { // the parent is vhci
                WdfObjectDelete(h); // FIXME: double deletion if vhci deletes it first as a child
        }

        if (auto h = r.device_str) {
                WdfObjectDereference(h);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_attach_request(_In_ WDFDEVICE vhci, _In_ vhci_ctx &ctx, _In_ WDFSTRING device_str)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, attach_ctx);
        attr.EvtCleanupCallback = cleanup_attach_request;
        attr.ParentObject = vhci;

        auto req = create_request(ctx.target_self, attr);
        if (!req) {
                return req;
        }

        TraceDbg("%04x", ptr04x(req.get()));
        auto &r = *get_attach_ctx(req.get());

        vhci::ioctl::plugin_hardware *buf{};

        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = req.get();

        auto ok = create_inbuf(r.inbuf, buf, attr) &&
                  create_outbuf(r.outbuf, buf, attr) &&
                  create_timer(r.timer, attr) &&
                  create_attach_workitem(r.wi, vhci, req.get<WDFREQUEST>());

        if (!ok) {
                req.reset();
                return req;
        }

        UNICODE_STRING str;
        WdfStringGetUnicodeString(device_str, &str);

        if (auto err = parse_device_str(*buf, str)) {
                Trace(TRACE_LEVEL_ERROR, "'%!USTR!' parse %!STATUS!", &str, err);
                req.reset();
                return req;
        }

        r.delay = ctx.reattach_first_delay;

        r.device_str = device_str;
        WdfObjectReference(device_str);

        return req;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED decltype(WdfDriverOpenParametersRegistryKey) *get_function(_In_ DRIVER_REGKEY_TYPE type)
{
        PAGED_CODE();

        switch (type) {
        case DriverRegKeyParameters:
                return WdfDriverOpenParametersRegistryKey;
        case DriverRegKeyPersistentState:
                return WdfDriverOpenPersistentStateRegistryKey;
        default:
                return nullptr;
        }
}

} // namespace 


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::plugin_persistent_device(
        _In_ WDFDEVICE vhci, _Inout_ vhci_ctx &ctx, _In_ WDFSTRING device_str, _In_ bool delayed)
{
        PAGED_CODE();

        UNICODE_STRING str;
        WdfStringGetUnicodeString(device_str, &str);

        Trace(TRACE_LEVEL_INFORMATION, "%!USTR!", &str);

        if (auto req = create_attach_request(vhci, ctx, device_str); !req) {
                //
        } else if (auto &r = *get_attach_ctx(req.get()); delayed) {
                ++r.retry_cnt;

                enum { DELAY = 30 }; // long delay after UdecxUsbDevicePlugOutAndDelete
                NT_VERIFY(!WdfTimerStart(r.timer, WDF_REL_TIMEOUT_IN_SEC(DELAY)));

                TraceDbg("req %04x, delayed for %u secs.", ptr04x(req.get()), DELAY);
                req.release();
        } else {
                send_plugin_hardware(vhci, ctx.target_self, r.inbuf, r.outbuf, req);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::plugin_persistent_devices(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();
        auto &ctx = *get_vhci_ctx(vhci);

        ULONG cnt{};
        auto col = get_persistent_devices(cnt, ctx.devices_cnt);

        for (ULONG i = 0; i < cnt; ++i) {
                auto device_str = (WDFSTRING)WdfCollectionGetItem(col.get<WDFCOLLECTION>(), i);
                plugin_persistent_device(vhci, ctx, device_str);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::copy(
        _Inout_ char *host, _In_ USHORT host_sz, _In_ const UNICODE_STRING &uhost,
        _Inout_ char *service, _In_ USHORT service_sz, _In_ const UNICODE_STRING &uservice,
        _Inout_ char *busid, _In_ USHORT busid_sz, _In_ const UNICODE_STRING &ubusid)
{
        PAGED_CODE();

        struct {
                char *dst;
                USHORT dst_sz;
                const UNICODE_STRING &src;
        } const v[] = {
                {host, host_sz, uhost},
                {service, service_sz, uservice},
                {busid, busid_sz, ubusid},
        };

        for (auto &[dst, dst_sz, src]: v) {
                if (auto err = libdrv::unicode_to_utf8(dst, dst_sz, src)) {
                        Trace(TRACE_LEVEL_ERROR, "unicode_to_utf8('%!USTR!') %!STATUS!", &src, err);
                        return err;
                }
        }

        return STATUS_SUCCESS;
}

/*
 * WdfDriverOpenParametersRegistryKey should not be used for write,
 * use WdfDriverOpenPersistentStateRegistryKey instead.
 *
 * HKLM\SYSTEM\CurrentControlSet\Services\<NAME>\<KEY>:
 * "Parameters" stores immutable data.
 * "State" is used to read/write persistent data.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::open(_Inout_ Registry &key, _In_ DRIVER_REGKEY_TYPE type, _In_ ACCESS_MASK access)
{
        PAGED_CODE();
        WDFKEY h{};

        auto f = get_function(type);
        auto st = f ? f(WdfGetDriver(), access, WDF_NO_OBJECT_ATTRIBUTES, &h) : STATUS_INVALID_PARAMETER;

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "%!DRIVER_REGKEY_TYPE!, access %#lx, %!STATUS!", type, access, st);
        }

        key.reset(h);
        return st;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ObjectDelete usbip::create_request(_In_ WDFIOTARGET target, _In_ WDF_OBJECT_ATTRIBUTES &attr)
{
        ObjectDelete ptr;

        if (WDFREQUEST req; auto err = WdfRequestCreate(&attr, target, &req)) {
                Trace(TRACE_LEVEL_ERROR, "WdfRequestCreate %!STATUS!", err);
        } else {
                ptr.reset(req);
        }

        return ptr;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED ObjectDelete usbip::make_device_str(_In_ WDFOBJECT parent, _In_ const device_attributes &r)
{
        PAGED_CODE();
        ObjectDelete ptr;

        static_assert(sizeof(L",,") == 3*sizeof(wchar_t)); // must have space for null terminator
        USHORT cb = r.node_name.Length + r.service_name.Length + r.busid.Length + sizeof(L",,"); // see format string

        unique_ptr buf(libdrv::uninitialized, PagedPool, cb);
        if (!buf) {
                Trace(TRACE_LEVEL_ERROR, "Cannot allocate %d bytes", cb);
                return ptr;
        }

        UNICODE_STRING str{ 
                .MaximumLength = cb, 
                .Buffer = buf.get<wchar_t>()
        };

        if (auto err = RtlUnicodeStringPrintf(&str, L"%wZ,%wZ,%wZ", &r.node_name, &r.service_name, &r.busid)) {
                Trace(TRACE_LEVEL_ERROR, "RtlUnicodeStringPrintf %!STATUS!", err);
                return ptr;
        }

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = parent;

        if (WDFSTRING h; auto err = WdfStringCreate(&str, &attr, &h)) {
                Trace(TRACE_LEVEL_ERROR, "WdfStringCreate('%!USTR!') %!STATUS!", &str, err);
        } else {
                ptr.reset(h);
        }

        return ptr;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::is_persistent(_In_ const vhci_ctx &vhci, _In_ WDFSTRING device_str)
{
        PAGED_CODE();

        UNICODE_STRING str;
        WdfStringGetUnicodeString(device_str, &str);

        ULONG cnt{};
        auto col = get_persistent_devices(cnt, vhci.devices_cnt);

        for (ULONG i = 0; i < cnt; ++i) {
                auto hs = (WDFSTRING)WdfCollectionGetItem(col.get<WDFCOLLECTION>(), i);

                UNICODE_STRING s;
                WdfStringGetUnicodeString(hs, &s);

                if (RtlEqualUnicodeString(&s, &str, false)) {
                        return true;
                }
        }

        return false;
}
