/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
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
        LIST_ENTRY entry; // head is vhci_ctx::reattach_requests
        WDFDEVICE vhci;

        WDFSTRING device_str; // host,port,busid
        WDFTIMER timer;

        WDFMEMORY inbuf;
        WDFMEMORY outbuf;

        unsigned int retry_cnt;
        unsigned int delay;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(attach_ctx, get_attach_ctx);

inline auto get_handle(_In_ attach_ctx *ctx)
{
        NT_ASSERT(ctx);
        return static_cast<WDFREQUEST>(WdfObjectContextGetObject(ctx));
}

constexpr auto empty(_In_ const UNICODE_STRING &s)
{
        return libdrv::empty(s) || !*s.Buffer;
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
                auto next = 2ULL*cur;
                delay = next < max_delay ? static_cast<unsigned int>(next) : max_delay;
        }

        return cur;
}

/*
 * Must be resident, do not use PAGED
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void push_to_cache(_Inout_ vhci_ctx &vhci, _Inout_ attach_ctx &r)
{
        NT_ASSERT(IsListEmpty(&r.entry));

        wdf::Lock lck(vhci.reattach_requests_lock);
        InsertHeadList(&vhci.reattach_requests, &r.entry);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto pop_from_cache(_Inout_ vhci_ctx &vhci)
{
        attach_ctx *r{};
        auto head = &vhci.reattach_requests;

        wdf::Lock lck(vhci.reattach_requests_lock);

        if (auto entry = RemoveHeadList(head); entry != head) {
                InitializeListHead(entry);
                r = CONTAINING_RECORD(entry, attach_ctx, entry);
        }

        return r;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void remove_from_cache(_Inout_ vhci_ctx &vhci, _Inout_ attach_ctx &r)
{
        wdf::Lock lck(vhci.reattach_requests_lock);

        RemoveEntryList(&r.entry); // works if entry was just InitializeListHead-ed
        InitializeListHead(&r.entry);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_persistent_devices(_In_ WDFKEY key)
{
        PAGED_CODE();
        ObjectDelete col;
        
        if (WDFCOLLECTION h; auto err = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &h)) {
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
        _In_ WDFREQUEST request, _In_ WDFIOTARGET, _In_ WDF_REQUEST_COMPLETION_PARAMS*, _In_ WDFCONTEXT)
{
        ObjectDelete ptr(request);

        auto &req = *get_attach_ctx(request);
        auto &vhci = *get_vhci_ctx(req.vhci);

        auto retry_cnt = req.retry_cnt++; // from zero

        auto st = WdfRequestGetStatus(request);
        auto failed = NT_ERROR(st);
        auto retry = failed && can_reattach(st) && can_retry(retry_cnt, vhci.reattach_max_attempts);

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
        _In_ WDFIOTARGET target, _In_ WDFMEMORY inbuf, _In_ WDFMEMORY outbuf, _Inout_ ObjectDelete &req)
{
        auto request = req.get<WDFREQUEST>();
        TraceDbg("req %04x", ptr04x(request));

        if (auto err = WdfIoTargetFormatRequestForIoctl(target, request,
                                vhci::ioctl::PLUGIN_HARDWARE, inbuf, nullptr, outbuf, nullptr)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetFormatRequestForIoctl %!STATUS!", err);
                return;
        }

        WdfRequestSetCompletionRoutine(request, on_plugin_hardware, WDF_NO_CONTEXT);

        if (!WdfRequestSend(request, target, WDF_NO_SEND_OPTIONS)) {
                auto err = WdfRequestGetStatus(request);
                Trace(TRACE_LEVEL_ERROR, "WdfRequestSend %!STATUS!", err);
        } else {
                req.release();
        }
}

/*
 * Release resources unique to each reuse.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void reuse_attach_ctx(_Inout_ attach_ctx &r)
{
        PAGED_CODE();

        if (auto &h = r.device_str) {
                WdfObjectDereference(h);
                h = WDF_NO_HANDLE;
        }
}

/*
 * PASSIVE_LEVEL is required to call is_persistent().
 * Removing a device from persistents allows you to stop attach attempts.
 *
 * If an EvtTimerFunc callback function running at PASSIVE_LEVEL calls WdfObjectDelete,
 * this results in deadlock. Request is a parent of a timer,
 * the timer will be implicitly deleted when the request is deleted.
 * The workaround is to cache request instead of deleting it.
 */
_Function_class_(EVT_WDF_TIMER)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void on_attach_timer(_In_ WDFTIMER timer)
{
        PAGED_CODE();
        ObjectDelete req(WdfTimerGetParentObject(timer));

        auto &r = *get_attach_ctx(req.get());
        auto &vhci = *get_vhci_ctx(r.vhci);
        auto why = "";

        if (auto rm = get_flag(vhci.removing); !rm && is_persistent(vhci, r.device_str)) {
                send_plugin_hardware(vhci.target_self, r.inbuf, r.outbuf, req);
        } else {
                why = rm ? ", vhci is being removing" : ", is no longer persistent";
        }

        if (req) { // cannot delete at PASSIVE_LEVEL
                TraceDbg("req %04x, cached%s", ptr04x(req.get()), why);
                reuse_attach_ctx(r);
                push_to_cache(vhci, r);
                req.release();
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool create_timer(_Inout_ WDFTIMER &result, _In_ WDFOBJECT parent)
{
        PAGED_CODE();
        NT_ASSERT(!result);

        WDF_TIMER_CONFIG cfg;
        WDF_TIMER_CONFIG_INIT(&cfg, on_attach_timer);
        cfg.TolerableDelay = TolerableDelayUnlimited;

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ExecutionLevel = WdfExecutionLevelPassive;
        attr.ParentObject = parent;

        if (auto err = WdfTimerCreate(&cfg, &attr, &result)) {
                Trace(TRACE_LEVEL_ERROR, "WdfTimerCreate %!STATUS!", err);
        }

        return result;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto reinit_attach_ctx(_Inout_ attach_ctx &r, _In_ WDFSTRING device_str, _In_ unsigned int delay)
{
        PAGED_CODE();

        NT_ASSERT(IsListEmpty(&r.entry));
        NT_ASSERT(r.vhci);
        NT_ASSERT(r.timer);
        NT_ASSERT(r.outbuf);

        r.retry_cnt = 0;
        r.delay = delay;

        auto &req = *static_cast<vhci::ioctl::plugin_hardware*>(WdfMemoryGetBuffer(r.inbuf, nullptr));
        RtlZeroMemory(&req, sizeof(req));

        req.size = sizeof(req);
        req.from_itself = true;

        UNICODE_STRING str;
        WdfStringGetUnicodeString(device_str, &str);

        if (auto err = parse_device_str(req, str)) {
                Trace(TRACE_LEVEL_ERROR, "'%!USTR!' parse %!STATUS!", &str, err);
                return false;
        }

        NT_ASSERT(!r.device_str); // @see reuse_attach_ctx
        r.device_str = device_str;
        WdfObjectReference(device_str);

        return true;
}

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void cleanup_attach_request(_In_ WDFOBJECT obj)
{
        PAGED_CODE();
        TraceDbg("%04x", ptr04x(obj));

        auto &r = *get_attach_ctx(obj);
        reuse_attach_ctx(r);

        auto vhci = get_vhci_ctx(r.vhci);
        remove_from_cache(*vhci, r);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_attach_request(_In_ WDFDEVICE vhci, _In_ vhci_ctx &ctx)
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
        InitializeListHead(&r.entry);
        r.vhci = vhci;

        vhci::ioctl::plugin_hardware *buf{};

        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = req.get();

        auto ok = create_inbuf(r.inbuf, buf, attr) &&
                  create_outbuf(r.outbuf, buf, attr) &&
                  create_timer(r.timer, req.get());

        if (!ok) {
                req.reset();
        }

        return req;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_attach_request(_In_ WDFDEVICE vhci, _Inout_ vhci_ctx &ctx, _In_ WDFSTRING device_str)
{
        PAGED_CODE();
        ObjectDelete req;

        if (get_flag(ctx.removing)) {
                TraceDbg("vhci is being removing");
                return req;
        }

        auto r = pop_from_cache(ctx);

        if (r) {
                req.reset(get_handle(r));
                TraceDbg("req %04x, cached", ptr04x(req.get()));
        } else if (req = create_attach_request(vhci, ctx); auto h = req.get()) {
                r = get_attach_ctx(h);
        }

        if (auto ok = r && reinit_attach_ctx(*r, device_str, ctx.reattach_first_delay); !ok) {
                req.reset();
        }

        return req;
}

/*
 * To debug persisten devices:
 * 1.Add to .inf following string
 *   HKR, Parameters, PersistentDevices, 0x00010000, "pc,3240,3-3", "pc,3240,3-2", "google.com,3240,3-1", "microsoft.com,3240,3-4"
 * 2.This function should always return WdfDriverOpenParametersRegistryKey.
 * 3.You cannot add "HKR, State, PersistentDevices ...", there will be compiler error.
 * 4.WDF does not have a function for DriverRegKeySharedPersistentState yet.
 */
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
//      case DriverRegKeySharedPersistentState: // since NTDDI_WIN10_FE
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

        if (UNICODE_STRING str; true) {
                WdfStringGetUnicodeString(device_str, &str);
                Trace(TRACE_LEVEL_INFORMATION, "%!USTR!", &str);
        }

        if (auto req = get_attach_request(vhci, ctx, device_str); !req) {
                //
        } else if (auto &r = *get_attach_ctx(req.get()); delayed) {
                ++r.retry_cnt;

                enum { DELAY = 30 }; // long delay after UdecxUsbDevicePlugOutAndDelete
                NT_VERIFY(!WdfTimerStart(r.timer, WDF_REL_TIMEOUT_IN_SEC(DELAY)));

                TraceDbg("req %04x, delayed for %u secs.", ptr04x(req.get()), DELAY);
                req.release();
        } else {
                send_plugin_hardware(ctx.target_self, r.inbuf, r.outbuf, req);
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
                auto dev_str = (WDFSTRING)WdfCollectionGetItem(col.get<WDFCOLLECTION>(), i);
                plugin_persistent_device(vhci, ctx, dev_str);
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

/*
 * WskGetAddressInfo() can return STATUS_INTERNAL_ERROR(0xC00000E5), but after some delay it will succeed.
 * This can happen after reboot if dnscache(?) service is not ready yet.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool usbip::can_reattach(_In_ NTSTATUS status)
{
        NT_ASSERT(NT_ERROR(status));

        switch (as_usbip_status(status)) {
        case USBIP_ERROR_ABI:
        case USBIP_ERROR_VERSION:
        case USBIP_ERROR_PROTOCOL:
                return false; // unrecoverable errors
        }

        return status != STATUS_CANCELLED;
}

