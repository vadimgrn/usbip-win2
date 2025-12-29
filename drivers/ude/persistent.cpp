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
        WDFDEVICE vhci;
        WDFSTRING url; // host,port,busid

        WDFTIMER timer;
        WDFMEMORY inbuf;
        WDFMEMORY outbuf;

        unsigned int retry_cnt;
        unsigned int delay;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(attach_ctx, get_attach_ctx);

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
constexpr auto can_retry(_In_ unsigned int retry_cnt, _In_ unsigned int max_retries)
{
        return !max_retries || retry_cnt < max_retries;
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

        if (Registry key; NT_SUCCESS(open_parameters_key(key, KEY_QUERY_VALUE))) {
                col = get_persistent_devices(key.get());
        }

        cnt = col ? min(WdfCollectionGetCount(col.get<WDFCOLLECTION>()), max_cnt) : 0;
        return col;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto parse_string(_Inout_ vhci::ioctl::plugin_hardware &r, _In_ const UNICODE_STRING &str)
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
PAGED auto create_inbuf(_Inout_ vhci::ioctl::plugin_hardware* &req, _Inout_ WDF_OBJECT_ATTRIBUTES &attr)
{
        PAGED_CODE();
        WDFMEMORY mem{};

        if (auto err = WdfMemoryCreate(&attr, PagedPool, 0, sizeof(*req), &mem, reinterpret_cast<PVOID*>(&req))) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreate %!STATUS!", err);
                req = nullptr;
        } else {
                RtlZeroMemory(req, sizeof(*req));
                req->size = sizeof(*req);

        }

        return mem;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_outbuf(_In_ vhci::ioctl::plugin_hardware *req, _Inout_ WDF_OBJECT_ATTRIBUTES &attr)
{
        PAGED_CODE();
        
        constexpr auto len = offsetof(vhci::ioctl::plugin_hardware, port) + sizeof(req->port);
        WDFMEMORY mem{};

        if (auto err = WdfMemoryCreatePreallocated(&attr, req, len, &mem)) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreatePreallocated %!STATUS!", err);
        }

        return mem;
}

/*
 * Cannot call is_persistent() at DISPATCH_LEVEL,
 * it will be called in timer routine.
 */
_Function_class_(EVT_WDF_REQUEST_COMPLETION_ROUTINE)
_IRQL_requires_same_
void on_plugin_hardware(
        _In_ WDFREQUEST request, _In_ WDFIOTARGET, _In_ WDF_REQUEST_COMPLETION_PARAMS*, _In_ WDFCONTEXT)
{
        ObjectDelete ptr(request);
        auto st = WdfRequestGetStatus(request);

        auto &ctx = *get_attach_ctx(request);
        auto &vhci = *get_vhci_ctx(ctx.vhci);

        auto retry_cnt = ctx.retry_cnt++; // from zero

        auto failed = NT_ERROR(st); 
        auto retry = failed && can_retry(st) && can_retry(retry_cnt, vhci.reattach_max_tries);

        if (!retry || get_flag(vhci.removing)) {

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

        auto secs = get_delay(ctx.delay, vhci.reattach_max_delay);
        NT_VERIFY(!WdfTimerStart(ctx.timer, WDF_REL_TIMEOUT_IN_SEC(secs))); // @see on_attach_timer

        TraceDbg("req %04x, %!STATUS!, retry #%u in %u secs.", ptr04x(request), st, retry_cnt, secs);
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
        _In_ WDFIOTARGET target, _In_ WDFMEMORY inbuf, _In_ WDFMEMORY outbuf,  _Inout_ ObjectDelete &req)
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
 * Removing a device from persistents allows you to stop attach attempts.
 */ 
_Function_class_(EVT_WDF_TIMER)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void on_attach_timer(_In_ WDFTIMER timer)
{
        PAGED_CODE();
        ObjectDelete req(WdfTimerGetParentObject(timer));

        auto &ctx = *get_attach_ctx(req.get());
        auto &vhci = *get_vhci_ctx(ctx.vhci);

        if (auto rm = get_flag(vhci.removing); rm || !is_persistent(vhci, ctx.url)) {
                auto s = rm ? "vhci is being removing" : "is no longer persistent";
                TraceDbg("req %04x, %s", ptr04x(req.get()), s);
        } else {
                TraceDbg("req %04x, retrying", ptr04x(req.get()));
                send_plugin_hardware(vhci.target_self, ctx.inbuf, ctx.outbuf, req);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_attach_timer(_In_ WDFOBJECT parent)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ExecutionLevel = WdfExecutionLevelPassive;
        attr.ParentObject = parent;

        WDF_TIMER_CONFIG cfg;
        WDF_TIMER_CONFIG_INIT(&cfg, on_attach_timer);
        cfg.TolerableDelay = TolerableDelayUnlimited;

        WDFTIMER timer{};
        if (auto err = WdfTimerCreate(&cfg, &attr, &timer)) {
                Trace(TRACE_LEVEL_ERROR, "WdfTimerCreate %!STATUS!", err);
        }

        return timer;
}

/*
 * There is no need to call WdfTimerStop here.
 * The framework stops and deletes the timer object 
 * before calling the parent's EvtCleanupCallback.
 */
_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void cleanup_attach_request(_In_ WDFOBJECT obj)
{
        PAGED_CODE();
        TraceDbg("%04x", ptr04x(obj)); 

        if (auto &ctx = *get_attach_ctx(obj); ctx.url) {
                WdfObjectDereference(ctx.url);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_attach_request(_In_ WDFDEVICE vhci, _In_ vhci_ctx &ctx, _In_ WDFSTRING url)
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

        r.inbuf = create_inbuf(buf, attr);
        r.outbuf = create_outbuf(buf, attr);
        r.timer = create_attach_timer(req.get());

        if (auto ok = r.inbuf && r.outbuf && r.timer; !ok) {
                req.reset();
                return req;
        }

        UNICODE_STRING str;
        WdfStringGetUnicodeString(url, &str);

        if (auto err = parse_string(*buf, str)) {
                Trace(TRACE_LEVEL_ERROR, "'%!USTR!' parse %!STATUS!", &str, err);
                req.reset();
                return req;
        }

        r.vhci = vhci;
        r.delay = ctx.reattach_init_delay;

        r.url = url;
        WdfObjectReference(url);

        return req;
}

} // namespace 


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::plugin_persistent_device(
        _In_ WDFDEVICE vhci, _Inout_ vhci_ctx &ctx, _In_ WDFSTRING url, _In_ bool after_delay)
{
        PAGED_CODE();

        UNICODE_STRING str;
        WdfStringGetUnicodeString(url, &str);

        Trace(TRACE_LEVEL_INFORMATION, "%!USTR!", &str);

        if (auto req = create_attach_request(vhci, ctx, url); !req) {
                //
        } else if (auto &r = *get_attach_ctx(req.get()); after_delay) {
                enum { DELAY = 30 };
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
                auto url = (WDFSTRING)WdfCollectionGetItem(col.get<WDFCOLLECTION>(), i);
                plugin_persistent_device(vhci, ctx, url);
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

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::open_parameters_key(_Out_ Registry &key, _In_ ACCESS_MASK DesiredAccess)
{
        PAGED_CODE();

        WDFKEY k{}; 
        auto st = WdfDriverOpenParametersRegistryKey(WdfGetDriver(), DesiredAccess, WDF_NO_OBJECT_ATTRIBUTES, &k);

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDriverOpenParametersRegistryKey(DesiredAccess=%lu) %!STATUS!", DesiredAccess, st);
        }

        key.reset(k);
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
PAGED ObjectDelete usbip::make_device_url(_In_ WDFOBJECT parent, _In_ const device_attributes &r)
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
PAGED bool usbip::is_persistent(_In_ const vhci_ctx &vhci, _In_ WDFSTRING url)
{
        PAGED_CODE();

        UNICODE_STRING str;
        WdfStringGetUnicodeString(url, &str);

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
