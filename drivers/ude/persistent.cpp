/*
 * Copyright (c) 2023-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "persistent.h"
#include "trace.h"
#include "persistent.tmh"

#include "driver.h"
#include "context.h"
#include "vhci.h"

#include <libdrv/strconv.h>
#include <resources/messages.h>

#include <ntstrsafe.h>

namespace 
{

using namespace usbip;
using namespace libdrv;

/*
 * Context space for WDFREQUEST which is used for attach attempts.
 */
struct attach_ctx
{
        ULONG location_hash; // hash(host,port,busid)
        ULONG session_id; // Terminal Server session that owns the device being (re)attached

        WDFDEVICE vhci;
        WDFTIMER timer;

        WDFMEMORY inbuf;
        WDFMEMORY outbuf;

        unsigned int retry_cnt;
        unsigned int delay;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(attach_ctx, get_attach_ctx);

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto empty(_In_ const UNICODE_STRING &s)
{
        PAGED_CODE();
        return libdrv::empty(s) || !*s.Buffer;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto reattach_req_count(_Inout_ vhci_ctx &vhci)
{
        wdf::spinlock lck(vhci.reattach_req_lock);
        return WdfCollectionGetCount(vhci.reattach_req);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto reattach_req_add(_Inout_ vhci_ctx &vhci, _In_ WDFOBJECT request)
{
        wdf::spinlock lck(vhci.reattach_req_lock);

        auto st = WdfCollectionAdd(vhci.reattach_req, request);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "%04x, WdfCollectionAdd %!STATUS!", ptr04x(request), st);
                return false;
        }

        return true;
}

/*
 * WdfCollectionRemove issues bugcheck if object is not found in collection.
 * imp_WdfCollectionRemove:WDFOBJECT XXX not in WDFCOLLECTION XXX, 0xc0000225(STATUS_NOT_FOUND)
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void reattach_req_remove(_Inout_ vhci_ctx &vhci, _In_ WDFOBJECT request)
{
        auto col = vhci.reattach_req;
        wdf::spinlock lck(vhci.reattach_req_lock);

        for (auto n = WdfCollectionGetCount(col), i = 0UL; i < n; ++i) {

                if (WdfCollectionGetItem(col, i) == request) {
                        WdfCollectionRemoveItem(col, i);
                        break;
                }
        }
}

/**
 * @param location_hash remove unconditionally if zero
 * @param session_id remove unconditionally if invalid_session_id
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto reattach_req_remove(_Inout_ vhci_ctx &vhci, _In_ ULONG location_hash, _In_ ULONG session_id = invalid_session_id)
{
        wdf::ObjectRef ref;
        auto col = vhci.reattach_req;

        wdf::spinlock lck(vhci.reattach_req_lock);

        for (auto n = WdfCollectionGetCount(col), i = 0UL; i < n; ++i) {

                auto req = WdfCollectionGetItem(col, i);
                auto r = get_attach_ctx(req);

                if (session_id != invalid_session_id && r->session_id != session_id) {
                        continue;
                }

                if (!location_hash || location_hash == r->location_hash) {
                        ref.reset(req);
                        WdfCollectionRemoveItem(col, i);
                        break;
                }
        }

        return ref;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_persistent_devices(_In_ WDFKEY key)
{
        PAGED_CODE();
        object_delete col;

        WDFCOLLECTION h;
        auto st = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &h);

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfCollectionCreate %!STATUS!", st);
                return col;
        } else {
                col.reset(h);
        }

        WDF_OBJECT_ATTRIBUTES str_attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&str_attr);
        str_attr.ParentObject = col.get();

        UNICODE_STRING value_name;
        RtlUnicodeStringInit(&value_name, persistent_devices_value_name);

        st = WdfRegistryQueryMultiString(key, &value_name, &str_attr, col.get<WDFCOLLECTION>());
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_VERBOSE, "WdfRegistryQueryMultiString('%!USTR!') %!STATUS!", &value_name, st);
                col.reset();
        }

        return col;
}

/*
 * @see set_persistent/get_persistent
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_persistent_devices(_Inout_ ULONG &cnt, _In_ ULONG max_cnt)
{
        PAGED_CODE();
        object_delete col;

        if (registry key; NT_SUCCESS(open(key, DriverRegKeyPersistentState))) {
                col = get_persistent_devices(key.get());
        }

        cnt = col ? min(WdfCollectionGetCount(col.get<WDFCOLLECTION>()), max_cnt) : 0;
        return col;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto parse_flags(_Inout_ bool &wsk_events, _Inout_ vhci::isolation &iso, _In_ const UNICODE_STRING &str)
{
        PAGED_CODE();

        if (empty(str)) {
                return STATUS_INVALID_PARAMETER;
        }

        for (USHORT i = 0; i < str.Length/sizeof(*str.Buffer); ++i) {
                if (!isdigit(str.Buffer[i])) {
                        return STATUS_INVALID_PARAMETER;
                }
        }

        ULONG val{};
        auto st = RtlUnicodeStringToInteger(&str, 10, &val);
        if (!NT_SUCCESS(st)) {
                return st;
        }

        bool once; // ignore, does not make sense for persistent
        unpack_attach_flags(once, wsk_events, iso, val);

        return STATUS_SUCCESS;
}

/*
 * @param r must be zeroed
 * @param device_str host,port,busid,serial,flags
 * @see hash_location
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto parse_device_str(_Inout_ device_attributes &r, _In_ const UNICODE_STRING &device_str)
{
        PAGED_CODE();

        if (empty(device_str)) {
                return STATUS_INVALID_PARAMETER;
        }

        UNICODE_STRING serial;
        auto tail = device_str;
        UNICODE_STRING* v[] { &r.node_name, &r.service_name, &r.busid, &serial, &tail };

        for (int i = 0; i < ARRAYSIZE(v) - 1; ++i) {
                split(*v[i], tail, tail, L',');
        }

        if (empty(r.node_name) || empty(r.service_name) || empty(r.busid)) {
                return STATUS_INVALID_PARAMETER;
        }

        auto &u8_serial = r.properties.serial;

        auto st = unicode_to_utf8(u8_serial, sizeof(u8_serial), serial);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "unicode_to_utf8('%!USTR!') %!STATUS!", &serial, st);
                return st;
        }

        st = validate_serial_number(u8_serial);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "bad serial '%!USTR!'", &serial);
                return st;
        }

        if (!empty(tail)) {
                st = parse_flags(r.properties.wsk_events, r.properties.iso_mode, tail);
                if (!NT_SUCCESS(st)) {
                        return st;
                }
        }

        return hash_location(r.location_hash, r);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool create_inbuf(
        _Inout_ WDFMEMORY &result, _Inout_ vhci::ioctl::plugin_hardware* &req, _Inout_ WDF_OBJECT_ATTRIBUTES &attr)
{
        PAGED_CODE();
        NT_ASSERT(!result);

        auto st = WdfMemoryCreate(&attr, NonPagedPoolNx, 0, sizeof(*req), &result, reinterpret_cast<PVOID*>(&req));
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreate %!STATUS!", st);
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

        const auto len = offsetof(vhci::ioctl::plugin_hardware, port) + sizeof(req->port);

        auto st = WdfMemoryCreatePreallocated(&attr, req, len, &result);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreatePreallocated %!STATUS!", st);
        }

        return result;
}

_Function_class_(EVT_WDF_REQUEST_COMPLETION_ROUTINE)
_IRQL_requires_same_
void on_plugin_hardware(
        _In_ WDFREQUEST request, _In_ WDFIOTARGET, _In_ WDF_REQUEST_COMPLETION_PARAMS*, _In_ WDFCONTEXT)
{
        object_delete ptr(request);

        auto &req = *get_attach_ctx(request);
        auto &vhci = *get_vhci_ctx(req.vhci);

        auto retry_cnt = req.retry_cnt++; // from zero

        auto st = WdfRequestGetStatus(request);
        auto failed = !NT_SUCCESS(st);
        auto retry = failed && retry_cnt < vhci.reattach_max_attempts &&
                     can_reattach(req.vhci, req.location_hash, st);

        if (auto ok = retry && !get_flag(vhci.removing); !ok) {
                auto s = !failed ? " " : // "" prints as "<NULL>"
                         !retry ? ", cannot retry" : 
                         ", vhci is being removing";

                TraceDbg("req %04x, %!STATUS!%s", ptr04x(request), st, s);
                return;
        }

        WDF_REQUEST_REUSE_PARAMS params;
        WDF_REQUEST_REUSE_PARAMS_INIT(&params, WDF_REQUEST_REUSE_NO_FLAGS, STATUS_SUCCESS);

        st = WdfRequestReuse(request, &params);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfRequestReuse(%04x) %!STATUS!", ptr04x(request), st);
                return;
        }

        auto delay = req.delay;
        req.delay = get_next_delay(delay, vhci.reattach_max_delay);

        TraceDbg("req %04x, %!STATUS!, retry #%u in %u secs.", ptr04x(request), st, retry_cnt, delay);
        NT_VERIFY(!WdfTimerStart(req.timer, WDF_REL_TIMEOUT_IN_SEC(delay))); // @see on_attach_timer

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
        _In_ WDFIOTARGET target, _In_ WDFMEMORY inbuf, _In_ WDFMEMORY outbuf, _Inout_ object_delete &req)
{
        auto request = req.get<WDFREQUEST>();
        TraceDbg("req %04x", ptr04x(request));

        auto st = WdfIoTargetFormatRequestForIoctl(target, request, vhci::ioctl::PLUGIN_HARDWARE_ONCE,
                                                   inbuf, nullptr, outbuf, nullptr);

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetFormatRequestForIoctl %!STATUS!", st);
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
 * If an EvtTimerFunc callback function running at PASSIVE_LEVEL calls WdfObjectDelete,
 * this results in deadlock. Request is a parent of a timer, the timer will be
 * implicitly deleted when the request is deleted.
 */
_Function_class_(EVT_WDF_TIMER)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void on_attach_timer(_In_ WDFTIMER timer)
{
        object_delete req(WdfTimerGetParentObject(timer));

        auto &r = *get_attach_ctx(req.get());
        auto &vhci = *get_vhci_ctx(r.vhci);

        if (get_flag(vhci.removing)) [[unlikely]] {
                TraceDbg("req %04x, vhci is being removing", ptr04x(req.get()));
        } else {
                send_plugin_hardware(vhci.target_self, r.inbuf, r.outbuf, req);
        }
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

        auto st = WdfTimerCreate(&cfg, &attr, &result);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfTimerCreate %!STATUS!", st);
        }

        return result;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto init_attach_ctx(_Inout_ vhci_ctx &vhci, _Inout_ attach_ctx &r, _In_ const device_attributes &attr)
{
        PAGED_CODE();

        r.location_hash = attr.location_hash;
        NT_ASSERT(r.location_hash);

        r.delay = vhci.reattach_first_delay;

        size_t len;
        auto &req = *static_cast<vhci::ioctl::plugin_hardware*>(WdfMemoryGetBuffer(r.inbuf, &len));
        NT_ASSERT(len == sizeof(req));

        RtlZeroMemory(&req, sizeof(req));
        req.size = sizeof(req);

        auto &props = attr.properties;
        req.wsk_events = props.wsk_events;
        req.iso_mode = props.iso_mode;

        auto st = RtlStringCbCopyNA(req.serial, sizeof(req.serial), props.serial, sizeof(props.serial));
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "RtlStringCbCopyNA('%s') %!STATUS!", props.serial, st);
                return false;
        }

        return NT_SUCCESS(fill_location(req, attr));
}

_Function_class_(EVT_WDF_OBJECT_CONTEXT_CLEANUP)
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void cleanup_attach_request(_In_ WDFOBJECT obj)
{
        PAGED_CODE();
        TraceDbg("%04x", ptr04x(obj));

        auto r = get_attach_ctx(obj);
        auto vhci = get_vhci_ctx(r->vhci);

        reattach_req_remove(*vhci, obj);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto create_attach_request(
        _In_ WDFDEVICE vhci, _In_ vhci_ctx &ctx, _In_ const device_attributes &dev, _In_ ULONG session_id)
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

        Trace(TRACE_LEVEL_INFORMATION, "%04x, %!USTR!:%!USTR!/%!USTR!, hash %lx, session %lu",
                ptr04x(req.get()), &dev.node_name, &dev.service_name, &dev.busid, dev.location_hash, session_id);

        auto &r = *get_attach_ctx(req.get());
        r.vhci = vhci;
        r.session_id = session_id; // preserve the owning session across (re)attach

        vhci::ioctl::plugin_hardware *buf{};

        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = req.get();

        auto ok = create_inbuf(r.inbuf, buf, attr) &&
                  create_outbuf(r.outbuf, buf, attr) &&
                  create_timer(r.timer, attr) &&
                  init_attach_ctx(ctx, r, dev) &&
                  reattach_req_add(ctx, req.get());

        if (!ok) {
                req.reset();
        }

        return req;
}

/*
 * WDF does not have a function for DriverRegKeySharedPersistentState yet.
 *
 * To debug persistent devices, add this line to .inf and make function to always return WdfDriverOpenParametersRegistryKey.
 * HKR, Parameters, PersistentDevices, 0x00010000, "pc,3240,3-3", "pc,3240,3-2","google.com,3240,3-1","microsoft.com,3240,3-4"
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


/*
 * There can be many reattach requests that are canceled but its timer callback has not been called yet.
 * Because of that the limit of active requests is 4x higher than the number of hub ports.
 * @see stop_attach_attempts
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::start_attach_attempts(
        _In_ WDFDEVICE vhci, _Inout_ vhci_ctx &ctx, _In_ const device_attributes &attr, _In_ ULONG session_id, _In_ bool delayed)
{
        PAGED_CODE();

        if (get_flag(ctx.removing)) {
                TraceDbg("vhci is being removing");
        } else if (auto cnt = reattach_req_count(ctx); cnt >= 4*static_cast<ULONG>(ctx.devices_cnt)) {
                Trace(TRACE_LEVEL_WARNING, "too many active attach requests, %lu", cnt);
        } else if (auto req = create_attach_request(vhci, ctx, attr, session_id); !req) {
                //
        } else if (auto &r = *get_attach_ctx(req.get()); !delayed) {
                send_plugin_hardware(ctx.target_self, r.inbuf, r.outbuf, req);
        } else {
                ++r.retry_cnt;

                enum { DELAY = 30 }; // long delay after UdecxUsbDevicePlugOutAndDelete
                NT_VERIFY(!WdfTimerStart(r.timer, WDF_REL_TIMEOUT_IN_SEC(DELAY)));

                TraceDbg("req %04x, delayed for %u secs.", ptr04x(req.get()), DELAY);
                req.release();
        }
}

/*
 * You cannot access request's context space here even when an extra reference is acquired.
 * The request can be in undefined state (completing/completed), a BSOD can occur.
 *
 * The timer callback will be fired. If it calls WdfRequestSend, STATUS_CANCELLED
 * will be immediately returned due to previously called WdfRequestCancelSentRequest.
 * WdfRequestReuse does not clear cancellation flag.
 *
 * If timer in system queue, its callback will delete cancelled request sooner or later.
 * The latter is a problem, these requests will be piling up and can cause Denial Of Service.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
int usbip::stop_attach_attempts(_Inout_ vhci_ctx &vhci, _In_ ULONG location_hash, _In_ ULONG session_id)
{
        int cnt = 0;

        while (auto req = reattach_req_remove(vhci, location_hash, session_id)) {

                ++cnt;
                auto delivered = WdfRequestCancelSentRequest(req.get<WDFREQUEST>());

                TraceDbg("hash %lx, session %lu -> req %04x, cancel request was delivered %!BOOLEAN!",
                          location_hash, session_id, ptr04x(req.get()), delivered);
        }

        return cnt;
}

/*
 * A device (re)attach travels to the driver through an IOCTL to target_self, which erases the
 * original requestor's session. The owning session is preserved in the pending attach request's
 * context, so the inbound attach handler can recover it by location_hash.
 * @see start_attach_attempts, plugin_hardware
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
ULONG usbip::find_attach_session(_Inout_ vhci_ctx &vhci, _In_ ULONG location_hash)
{
        if (!location_hash) {
                return invalid_session_id;
        }

        auto col = vhci.reattach_req;
        wdf::spinlock lck(vhci.reattach_req_lock);

        for (auto n = WdfCollectionGetCount(col), i = 0UL; i < n; ++i) {
                if (auto r = get_attach_ctx(WdfCollectionGetItem(col, i)); r->location_hash == location_hash) {
                        return r->session_id;
                }
        }

        return invalid_session_id;
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

                auto str = (WDFSTRING)WdfCollectionGetItem(col.get<WDFCOLLECTION>(), i);

                UNICODE_STRING device_str;
                WdfStringGetUnicodeString(str, &device_str);

                device_attributes attr{};
                auto st = parse_device_str(attr, device_str);

                if (!NT_SUCCESS(st)) {
                        Trace(TRACE_LEVEL_ERROR, "parse_device_str(%!USTR!) %!STATUS!", &device_str, st);
                } else {
                        // boot-time persistent devices belong to no live session; per-SID persistent state is a follow-up
                        start_attach_attempts(vhci, ctx, attr, invalid_session_id);
                }
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::fill_location(
        _Inout_ vhci::imported_device_location &r, _In_ const device_attributes &attr)
{
        PAGED_CODE();

        NT_ASSERT(attr.location_hash);
        r.location_hash = attr.location_hash;

        struct {
                char *dst;
                USHORT dst_sz;
                const UNICODE_STRING &src;
        } const v[] = {
                { r.host, sizeof(r.host), attr.node_name },
                { r.service, sizeof(r.service), attr.service_name },
                { r.busid, sizeof(r.busid), attr.busid },
        };

        for (auto &[dst, dst_sz, src]: v) {
                auto st = unicode_to_utf8(dst, dst_sz, src);
                if (!NT_SUCCESS(st)) {
                        Trace(TRACE_LEVEL_ERROR, "unicode_to_utf8('%!USTR!') %!STATUS!", &src, st);
                        return st;
                }
        }

        return STATUS_SUCCESS;
}

/*
 * Validate persistent devices multi-string buffer before writing to the registry.
 * - Enforces wide-character alignment and a maximum buffer size.
 * - Validates proper null / double-null termination of the REG_MULTI_SZ structure.
 * - Parses every entry via parse_device_str() and fill_location() to verify syntax,
 *   serial number format, attach flags, and location hash.
 * - Ensures total device count does not exceed max_devices (controller port count).
 *
 * @see set_persistent
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::validate_persistent_devices(
        _In_reads_bytes_(length) const void *buf, _In_ size_t length, _In_ ULONG max_devices)
{
        PAGED_CODE();

        if (length && !buf) {
                return STATUS_INVALID_PARAMETER;
        }

        if (length % sizeof(wchar_t)) {
                return STATUS_INVALID_PARAMETER;
        }

        if (constexpr size_t max_persistent_size = 64*1024; // arbitrary
            length > max_persistent_size) {
                Trace(TRACE_LEVEL_ERROR, "length %Iu exceeds max %Iu", length, max_persistent_size);
                return STATUS_INVALID_PARAMETER;
        }

        if (!length) {
                return STATUS_SUCCESS;
        }

        auto p = static_cast<const wchar_t*>(buf);
        auto end = p + length/sizeof(wchar_t);

        if (end[-1] != L'\0') {
                return STATUS_INVALID_PARAMETER;
        }

        ULONG count = 0;

        while (p < end && *p != L'\0') {
                auto str_start = p;
                while (p < end && *p != L'\0') {
                        ++p;
                }

                if (p >= end) {
                        return STATUS_INVALID_PARAMETER;
                }

                auto cch = p - str_start;
                if (!cch || cch*sizeof(wchar_t) > UNICODE_STRING_MAX_BYTES) {
                        return STATUS_INVALID_PARAMETER;
                }

                UNICODE_STRING device_str {
                        .Length = static_cast<USHORT>(cch*sizeof(wchar_t)),
                        .MaximumLength = static_cast<USHORT>(cch*sizeof(wchar_t)),
                        .Buffer = const_cast<wchar_t*>(str_start)
                };

                device_attributes attr{};
                auto st = parse_device_str(attr, device_str);
                if (!NT_SUCCESS(st)) {
                        Trace(TRACE_LEVEL_ERROR, "invalid device '%!USTR!' %!STATUS!", &device_str, st);
                        return st;
                }

                vhci::imported_device_location loc{};
                st = fill_location(loc, attr);
                if (!NT_SUCCESS(st)) {
                        Trace(TRACE_LEVEL_ERROR, "fill_location '%!USTR!' %!STATUS!", &device_str, st);
                        return st;
                }

                ++count;
                if (count > max_devices) {
                        Trace(TRACE_LEVEL_ERROR, "count %u exceeds max %u", count, max_devices);
                        return STATUS_INVALID_PARAMETER;
                }

                ++p; // skip the null terminator
        }

        if (p == end && count > 0) {
                return STATUS_INVALID_PARAMETER;
        }

        while (p < end) {
                if (*p != L'\0') {
                        return STATUS_INVALID_PARAMETER;
                }
                ++p;
        }

        return STATUS_SUCCESS;
}

/*
 * WdfDriverOpenParametersRegistryKey should not be used for write,
 * use WdfDriverOpenPersistentStateRegistryKey instead.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::open(_Inout_ registry &key, _In_ DRIVER_REGKEY_TYPE type, _In_ ACCESS_MASK access)
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
object_delete usbip::create_request(_In_ WDFIOTARGET target, _In_ WDF_OBJECT_ATTRIBUTES &attr)
{
        object_delete ptr;

        WDFREQUEST req;
        auto st = WdfRequestCreate(&attr, target, &req);

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfRequestCreate %!STATUS!", st);
        } else {
                ptr.reset(req);
        }

        return ptr;
}

/*
 * WskGetAddressInfo() can return STATUS_INTERNAL_ERROR(0xC00000E5), but after some delay it will succeed.
 * This can happen after reboot if dnscache(?) service is not ready yet.
 *
 * USBIP_ERROR_ST_DEV_BUSY is here because you call attach, it can fail and start attach attempts.
 * You call attach again, it can succeed, but background attach attempts will not be stopped.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool usbip::can_reattach(_In_ WDFDEVICE vhci, _In_ ULONG location_hash, _In_ NTSTATUS status)
{
        NT_ASSERT(!NT_SUCCESS(status));

        switch (as_usbip_status(status)) {
        case USBIP_ERROR_ABI:
        case USBIP_ERROR_VERSION:
        case USBIP_ERROR_PROTOCOL:
                return false; // unrecoverable errors
        case USBIP_ERROR_ST_DEV_BUSY:
                return !vhci::has_device(vhci, location_hash);
        }

        return status != STATUS_CANCELLED;
}

 /*
 * @see parse_device_str
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::hash_location(_Inout_ ULONG &hash, _In_ const device_attributes &r)
{
        PAGED_CODE();
        hash = 0;

        static_assert(sizeof(L",,") == 3*sizeof(wchar_t)); // must have space for null terminator

        auto cb64 = sizeof(L",,") + r.node_name.Length + r.service_name.Length + r.busid.Length; // see format string
        if (cb64 > UNICODE_STRING_MAX_BYTES) {
                Trace(TRACE_LEVEL_ERROR, "Location string is too long (%Iu bytes)", cb64);
                return STATUS_INVALID_PARAMETER;
        }
        auto cb = static_cast<USHORT>(cb64);

        wchar_t stack_buf[96];
        unique_ptr buf;
        wchar_t *pbuf = stack_buf;

        if (cb > sizeof(stack_buf)) {
                buf = unique_ptr(uninitialized, PagedPool, cb);
                if (!buf) {
                        Trace(TRACE_LEVEL_ERROR, "Cannot allocate %u bytes", cb);
                        return STATUS_INSUFFICIENT_RESOURCES;
                }
                pbuf = buf.get<wchar_t>();
        }

        UNICODE_STRING str { 
                .MaximumLength = cb, 
                .Buffer = pbuf
        };

        auto st = RtlUnicodeStringPrintf(&str, L"%wZ,%wZ,%wZ", &r.node_name, &r.service_name, &r.busid);
        if (!NT_SUCCESS(st)) {
                static_assert(!NT_ERROR(STATUS_BUFFER_OVERFLOW)); // is a warning
                Trace(TRACE_LEVEL_ERROR, "RtlUnicodeStringPrintf %!STATUS!", st);
                return st;
        }

        st = RtlHashUnicodeString(&str, true, HASH_STRING_ALGORITHM_DEFAULT, &hash);
        if (!NT_SUCCESS(st)) {
                Trace(TRACE_LEVEL_ERROR, "RtlHashUnicodeString('%!USTR!') %!STATUS!", &str, st);
        }
        return st;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::validate_serial_number(_In_ const char (&serial)[SERIAL_BUFSZ])
{
        auto len = is_ascii_alnum(serial, SERIAL_BUFSZ);
        return len >= 0 && len < SERIAL_BUFSZ ? USBIP_ERROR_SUCCESS : USBIP_ERROR_SERIAL_NUMBER;
}
