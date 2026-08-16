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

/*
 * Context space for WDFREQUEST which is used for attach attempts.
 */
struct attach_ctx
{
        ULONG location_hash; // hash(host,port,busid)

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
        wdf::Lock(vhci.reattach_req_lock);
        return WdfCollectionGetCount(vhci.reattach_req);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto reattach_req_add(_Inout_ vhci_ctx &vhci, _In_ WDFOBJECT request)
{
        wdf::Lock lck(vhci.reattach_req_lock);

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
        wdf::Lock lck(vhci.reattach_req_lock);

        for (auto n = WdfCollectionGetCount(col), i = 0UL; i < n; ++i) {

                if (WdfCollectionGetItem(col, i) == request) {
                        WdfCollectionRemoveItem(col, i);
                        break;
                }
        }
}

/**
 * @param location_hash remove unconditionally if zero
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto reattach_req_remove(_Inout_ vhci_ctx &vhci, _In_ ULONG location_hash)
{
        wdf::ObjectRef ref;
        auto col = vhci.reattach_req;

        wdf::Lock lck(vhci.reattach_req_lock);

        for (auto n = WdfCollectionGetCount(col), i = 0UL; i < n; ++i) {

                auto req = WdfCollectionGetItem(col, i);
                
                if (!location_hash || location_hash == get_attach_ctx(req)->location_hash) {
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
        ObjectDelete col;

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
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfRegistryQueryMultiString('%!USTR!') %!STATUS!", &value_name, st);
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
        ObjectDelete col;

        if (Registry key; NT_SUCCESS(open(key, DriverRegKeyPersistentState))) {
                col = get_persistent_devices(key.get());
        }

        cnt = col ? min(WdfCollectionGetCount(col.get<WDFCOLLECTION>()), max_cnt) : 0;
        return col;
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
                libdrv::split(*v[i], tail, tail, L',');
        }

        if (empty(r.node_name) || empty(r.service_name) || empty(r.busid)) {
                return STATUS_INVALID_PARAMETER;
        }

        auto &u8_serial = r.properties.serial;

        auto st = libdrv::unicode_to_utf8(u8_serial, sizeof(u8_serial), serial);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "unicode_to_utf8('%!USTR!') %!STATUS!", &serial, st);
                return st;
        }

        st = validate_serial_number(u8_serial);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "bad serial '%!USTR!'", &serial);
                return st;
        }

        ULONG val;
        st = RtlUnicodeStringToInteger(&tail, 0, &val);

        if (NT_ERROR(st)) {
                NT_ASSERT(st == STATUS_INVALID_PARAMETER); // string is empty
                NT_ASSERT(empty(tail));
        } else {
                bool once; // ignore, does not make sence for persistent
                unpack_attach_flags(once, r.properties.wsk_events, val);
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

        constexpr auto len = __builtin_offsetof(vhci::ioctl::plugin_hardware, port) + sizeof(req->port);

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
        ObjectDelete ptr(request);

        auto &req = *get_attach_ctx(request);
        auto &vhci = *get_vhci_ctx(req.vhci);

        auto retry_cnt = req.retry_cnt++; // from zero

        auto st = WdfRequestGetStatus(request);
        auto failed = NT_ERROR(st);
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
        _In_ WDFIOTARGET target, _In_ WDFMEMORY inbuf, _In_ WDFMEMORY outbuf, _Inout_ ObjectDelete &req)
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
        ObjectDelete req(WdfTimerGetParentObject(timer));

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

        auto st = RtlStringCbCopyNA(req.serial, sizeof(req.serial), props.serial, sizeof(props.serial));
        if (NT_ERROR(st)) {
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
PAGED auto create_attach_request(_In_ WDFDEVICE vhci, _In_ vhci_ctx &ctx, _In_ const device_attributes &dev)
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

        Trace(TRACE_LEVEL_INFORMATION, "%04x, %!USTR!:%!USTR!/%!USTR!, hash %lx",
                ptr04x(req.get()), &dev.node_name, &dev.service_name, &dev.busid, dev.location_hash);

        auto &r = *get_attach_ctx(req.get());
        r.vhci = vhci;

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
        _In_ WDFDEVICE vhci, _Inout_ vhci_ctx &ctx, _In_ const device_attributes &attr, _In_ bool delayed)
{
        PAGED_CODE();

        if (get_flag(ctx.removing)) {
                TraceDbg("vhci is being removing");
        } else if (auto cnt = reattach_req_count(ctx); cnt >= 4*static_cast<ULONG>(ctx.devices_cnt)) {
                Trace(TRACE_LEVEL_WARNING, "too many active attach requests, %lu", cnt);
        } else if (auto req = create_attach_request(vhci, ctx, attr); !req) {
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
 * will be immetiately returned due to previosly called WdfRequestCancelSentRequest.
 * WdfRequestReuse does not clear cancellation flag.
 *
 * If timer in system queue, its callback will delete cancelled request sooner or later.
 * The latter is a problem, these requests will be piling up and can cause Denial Of Service.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
int usbip::stop_attach_attempts(_Inout_ vhci_ctx &vhci, _In_ ULONG location_hash)
{
        int cnt = 0;

        while (auto req = reattach_req_remove(vhci, location_hash)) {

                ++cnt;
                auto delivered = WdfRequestCancelSentRequest(req.get<WDFREQUEST>());

                TraceDbg("hash %lx -> req %04x, cancel request was delivered %!BOOLEAN!",
                          location_hash, ptr04x(req.get()), delivered);
        }

        return cnt;
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

                if (NT_ERROR(st)) {
                        Trace(TRACE_LEVEL_ERROR, "parse_device_str(%!USTR!) %!STATUS!", &device_str, st);
                } else {
                        start_attach_attempts(vhci, ctx, attr);
                }
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::fill_location(
        _Inout_ vhci::imported_device_location &r, _In_ const device_attributes &attr)
{
        PAGED_CODE();

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
                auto st = libdrv::unicode_to_utf8(dst, dst_sz, src);
                if (NT_ERROR(st)) {
                        Trace(TRACE_LEVEL_ERROR, "unicode_to_utf8('%!USTR!') %!STATUS!", &src, st);
                        return st;
                }
        }

        return STATUS_SUCCESS;
}

/*
 * WdfDriverOpenParametersRegistryKey should not be used for write,
 * use WdfDriverOpenPersistentStateRegistryKey instead.
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
 * USBIP_ERROR_ST_DEV_BUSY is here because you call attach, it can fail and start attach attemtps.
 * You call attach again, it can succeed, but background attach attempts will not be stopped.
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool usbip::can_reattach(_In_ WDFDEVICE vhci, _In_ ULONG location_hash, _In_ NTSTATUS status)
{
        NT_ASSERT(NT_ERROR(status));

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
        USHORT cb = r.node_name.Length + r.service_name.Length + r.busid.Length + sizeof(L",,"); // see format string

        unique_ptr buf(libdrv::uninitialized, PagedPool, cb);
        if (!buf) {
                Trace(TRACE_LEVEL_ERROR, "Cannot allocate %d bytes", cb);
                return USBD_STATUS_INSUFFICIENT_RESOURCES;
        }

        UNICODE_STRING str { 
                .MaximumLength = cb, 
                .Buffer = buf.get<wchar_t>()
        };

        auto st = RtlUnicodeStringPrintf(&str, L"%wZ,%wZ,%wZ", &r.node_name, &r.service_name, &r.busid);
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "RtlUnicodeStringPrintf %!STATUS!", st);
                return st;
        }

        st = RtlHashUnicodeString(&str, true, HASH_STRING_ALGORITHM_DEFAULT, &hash);
        if (NT_ERROR(st)) {
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
