/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "persistent.h"
#include "trace.h"
#include "persistent.tmh"

#include "context.h"

#include <libdrv\strconv.h>
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
        case USBIP_ERROR_VHCI_NOT_FOUND:
        case USBIP_ERROR_DRIVER_RESPONSE:
        case USBIP_ERROR_DEVICE_INTERFACE_LIST:
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

/*
 * Exponential backoff: 2^retry_cnt, capped at max_period seconds.
 * @param retry_cnt from zero
 * @return second(s)
 */
constexpr auto get_period(_In_ unsigned int retry_cnt, _In_ unsigned int max_period)
{
        if (retry_cnt > 31) {
                return max_period;
        }

        auto timeout = 1U << retry_cnt; // 2^retry_cnt
        return timeout < max_period ? timeout : max_period;
}
static_assert(get_period(0, 300) == 1);
static_assert(get_period(1, 300) == 2);
static_assert(get_period(2, 300) == 4);
static_assert(get_period(3, 300) == 8);
static_assert(get_period(4, 300) == 16);
static_assert(get_period(5, 300) == 32);
static_assert(get_period(6, 300) == 64);
static_assert(get_period(7, 300) == 128);
static_assert(get_period(8, 300) == 256);
static_assert(get_period(9, 300) == 300);

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
        ObjectDelete devices;

        if (Registry key; NT_SUCCESS(open_parameters_key(key, KEY_QUERY_VALUE))) {
                devices = get_persistent_devices(key.get());
        }

        cnt = devices ? min(WdfCollectionGetCount(devices.get<WDFCOLLECTION>()), max_cnt) : 0;
        return devices;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto contains(_In_ WDFCOLLECTION strings, _In_ ULONG cnt, _In_ WDFSTRING key)
{
        PAGED_CODE();

        UNICODE_STRING str;
        WdfStringGetUnicodeString(key, &str);

        for (ULONG i = 0; i < cnt; ++i) {

                auto hs = (WDFSTRING)WdfCollectionGetItem(strings, i);

                UNICODE_STRING s;
                WdfStringGetUnicodeString(hs, &s);

                if (RtlEqualUnicodeString(&s, &str, true)) {
                        return true;
                }
        }

        return false;
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

        if (auto err = WdfMemoryCreate(&attr, NonPagedPoolNx, 0, sizeof(*req), &mem, reinterpret_cast<PVOID*>(&req))) {
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
 * Cannot call contains() here, it requires PASSIVE_LEVEL.
 * It will be called in timer routine.
 */
_Function_class_(EVT_WDF_REQUEST_COMPLETION_ROUTINE)
_IRQL_requires_same_
void on_plugin_hardware(
        _In_ WDFREQUEST request, _In_ WDFIOTARGET, _In_ WDF_REQUEST_COMPLETION_PARAMS*, _In_ WDFCONTEXT)
{
        auto st = WdfRequestGetStatus(request);

        auto &ctx = *get_attach_ctx(request);
        auto &vhci = *get_vhci_ctx(ctx.vhci);

        auto retry_cnt = ctx.retry_cnt++; // from zero

        auto failed = NT_ERROR(st); 
        auto retry = failed && can_retry(st) && can_retry(retry_cnt, vhci.max_attach_retries);

        if (retry && !get_flag(vhci.removing)) {
                auto secs = get_period(retry_cnt, vhci.max_attach_period);
                NT_VERIFY(!WdfTimerStart(ctx.timer, WDF_REL_TIMEOUT_IN_SEC(secs))); // @see on_attach_timer

                TraceDbg("req %04x, %!STATUS!, retry #%u in %u secs.", ptr04x(request), st, retry_cnt, secs);
        } else {
                auto s = !failed ? "" : 
                         !retry ? ", cannot retry" : 
                         ", vhci is being removing";

                TraceDbg("req %04x, %!STATUS!%s", ptr04x(request), st, s);
                WdfObjectDelete(request);
        }
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

        ULONG cnt{};
        auto persistent = get_persistent_devices(cnt, vhci.devices_cnt);

        if (auto rm = get_flag(vhci.removing); rm || !contains(persistent.get<WDFCOLLECTION>(), cnt, ctx.url)) {
                auto s = rm ? "vhci is being removing" : "is no longer persistent";
                TraceDbg("req %04x %s", ptr04x(req.get()), s);
                return;
        }

        TraceDbg("req %04x, retrying", ptr04x(req.get()));

        WDF_REQUEST_REUSE_PARAMS params;
        WDF_REQUEST_REUSE_PARAMS_INIT(&params, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);

        if (auto err = WdfRequestReuse(req.get<WDFREQUEST>(), &params)) {
                Trace(TRACE_LEVEL_ERROR, "WdfRequestReuse %!STATUS!", err);
        } else {
                send_plugin_hardware(vhci.target_self, ctx.inbuf, ctx.outbuf, req);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_attach_timer(_Inout_ WDFTIMER &timer, _In_ WDFOBJECT parent)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ExecutionLevel = WdfExecutionLevelPassive;
        attr.ParentObject = parent;

        WDF_TIMER_CONFIG cfg;
        WDF_TIMER_CONFIG_INIT(&cfg, on_attach_timer);
        cfg.TolerableDelay = TolerableDelayUnlimited;

        if (auto err = WdfTimerCreate(&cfg, &attr, &timer)) {
                Trace(TRACE_LEVEL_ERROR, "WdfTimerCreate %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}

/*
* There is no need to call WdfTimerStop here or in EVT_WDF_OBJECT_CONTEXT_CLEANUP.
* The framework stops and deletes the timer object before calling the parent's EvtCleanupCallback.
 */
_Function_class_(EVT_WDF_OBJECT_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void destroy_attach_request(_In_ WDFOBJECT obj)
{
        PAGED_CODE();
        TraceDbg("%04x", ptr04x(obj)); 

        if (auto &ctx = *get_attach_ctx(obj); ctx.url) {
                WdfObjectDereference(ctx.url);
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_attach_request(_In_ WDFDEVICE vhci, WDFIOTARGET target, _In_ WDFSTRING url)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, attach_ctx);
        attr.EvtDestroyCallback = destroy_attach_request;
        attr.ParentObject = vhci;

        auto req = create_request(target, attr);
        if (!req) {
                return req;
        }

        TraceDbg("%04x", ptr04x(req.get()));

        auto &ctx = *get_attach_ctx(req.get()); 
        ctx.vhci = vhci;

        if (auto err = create_attach_timer(ctx.timer, req.get())) {
                req.reset();
        } else {
                WdfObjectReference(url);
                ctx.url = url;
        }

        return req;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugin_persistent_device(_In_ WDFDEVICE vhci, _Inout_ vhci_ctx &vctx, _In_ WDFSTRING url)
{
        PAGED_CODE();

        UNICODE_STRING str;
        WdfStringGetUnicodeString(url, &str);

        Trace(TRACE_LEVEL_INFORMATION, "%!USTR!", &str);

        auto req = create_attach_request(vhci, vctx.target_self, url);
        if (!req) {
                return;
        }
        auto &ctx = *get_attach_ctx(req.get());

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = req.get();

        vhci::ioctl::plugin_hardware *r{};
        if (ctx.inbuf = create_inbuf(r, attr); !ctx.inbuf) {
                return;
        }

        if (auto err = parse_string(*r, str)) {
                Trace(TRACE_LEVEL_ERROR, "'%!USTR!' parse %!STATUS!", &str, err);
        } else if (ctx.outbuf = create_outbuf(r, attr); ctx.outbuf) {
                send_plugin_hardware(vctx.target_self, ctx.inbuf, ctx.outbuf, req);
        }
}

} // namespace 


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::plugin_persistent_devices(_Inout_ vhci_ctx &ctx)
{
        PAGED_CODE();
        auto vhci = get_handle(&ctx);

        ULONG cnt{};
        auto devices = get_persistent_devices(cnt, ctx.devices_cnt);

        for (ULONG i = 0; i < cnt; ++i) {
                auto url = (WDFSTRING)WdfCollectionGetItem(devices.get<WDFCOLLECTION>(), i);
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
auto usbip::create_request(_In_ WDFIOTARGET target, _In_ WDF_OBJECT_ATTRIBUTES &attr) -> ObjectDelete
{
        ObjectDelete ptr;

        if (WDFREQUEST req; auto err = WdfRequestCreate(&attr, target, &req)) {
                Trace(TRACE_LEVEL_ERROR, "WdfRequestCreate %!STATUS!", err);
        } else {
                ptr.reset(req);
        }

        return ptr;
}
