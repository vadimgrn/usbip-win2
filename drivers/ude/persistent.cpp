/*
 * Copyright (c) 2023-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "persistent.h"
#include "trace.h"
#include "persistent.tmh"

#include "context.h"

#include <libdrv\strconv.h>
#include <libdrv\wait_timeout.h>
#include <resources/messages.h>

#include <ntstrsafe.h>

namespace 
{

using namespace usbip;

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

constexpr auto empty(_In_ const UNICODE_STRING &s)
{
        return libdrv::empty(s) || !*s.Buffer;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto parse_string(_Out_ vhci::ioctl::plugin_hardware &r, _In_ const UNICODE_STRING &str)
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

/*
 * Target is self. 
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto make_target(_In_ WDFDEVICE vhci)
{
        PAGED_CODE();
        ObjectDelete target;

        if (WDFIOTARGET t; auto err = WdfIoTargetCreate(vhci, WDF_NO_OBJECT_ATTRIBUTES, &t)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetCreate %!STATUS!", err);
                return target;
        } else {
                target.reset(t);
        }

        auto fdo = WdfDeviceWdmGetDeviceObject(vhci);

        WDF_IO_TARGET_OPEN_PARAMS params;
        WDF_IO_TARGET_OPEN_PARAMS_INIT_EXISTING_DEVICE(&params, fdo);

        if (auto err = WdfIoTargetOpen(target.get<WDFIOTARGET>(), &params)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetOpen %!STATUS!", err);
                target.reset();
        }

        return target;
}

constexpr auto get_delay(_In_ ULONG attempt, _In_ ULONG cnt)
{
        NT_ASSERT(cnt);
        enum { UNIT = 10, MAX_DELAY = 30*60 }; // seconds
        return attempt > 1 ? min(UNIT*attempt/cnt, MAX_DELAY) : 0; // first two attempts without a delay
}

_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED auto sleep(_Inout_ vhci_ctx &ctx, _In_ ULONG seconds)
{
        PAGED_CODE();
        auto timeout = make_timeout(seconds*wdm::second, wdm::period::relative);

        switch (auto st = KeWaitForSingleObject(&ctx.attach_thread_stop, Executive, KernelMode, false, &timeout)) {
        case STATUS_SUCCESS:
                TraceDbg("thread stop requested");
                return false;
        case STATUS_TIMEOUT:
                break;
        default:
                Trace(TRACE_LEVEL_ERROR, "KeWaitForSingleObject %!STATUS!", st);
        }

        return true;
}

/*
 * WskGetAddressInfo() can return STATUS_INTERNAL_ERROR(0xC00000E5), but after some delay it will succeed.
 * This can happen after reboot if dnscache(?) service is not ready yet.
 */
constexpr auto can_retry(_In_ NTSTATUS status)
{
        switch (as_usbip_status(status)) {
        case USBIP_ERROR_VERSION:
        case USBIP_ERROR_PROTOCOL:
        case USBIP_ERROR_ABI:
        // @see op_status_t
        case USBIP_ERROR_ST_NA: 
        case USBIP_ERROR_ST_DEV_BUSY:
        case USBIP_ERROR_ST_DEV_ERR:
        case USBIP_ERROR_ST_NODEV:
        case USBIP_ERROR_ST_ERROR:
                return false;
        default:
                return true;
        }
}

/*
 * @return true - do not try to connect to this device again
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto plugin_hardware(
        _In_ const UNICODE_STRING &line, 
        _In_ WDFIOTARGET target,
        _Inout_ vhci::ioctl::plugin_hardware &req,
        _Inout_ WDF_MEMORY_DESCRIPTOR &input,
        _Inout_ WDF_MEMORY_DESCRIPTOR &output,
        [[maybe_unused]] _In_ const ULONG outlen)
{
        PAGED_CODE();

        if (auto err = parse_string(req, line)) {
                Trace(TRACE_LEVEL_ERROR, "'%!USTR!' parse %!STATUS!", &line, err);
                return true; // remove malformed string
        }

        Trace(TRACE_LEVEL_INFORMATION, "%s:%s/%s", req.host, req.service, req.busid);
        req.port = 0;

        if (ULONG_PTR BytesReturned; // send IOCTL to itself
            auto err = WdfIoTargetSendIoctlSynchronously(target, WDF_NO_HANDLE, vhci::ioctl::PLUGIN_HARDWARE, 
                                                         &input, &output, nullptr, &BytesReturned)) {
                Trace(TRACE_LEVEL_ERROR, "WdfIoTargetSendIoctlSynchronously %!STATUS!", err);
                return !can_retry(err);
        } else {
                NT_ASSERT(BytesReturned == outlen);
                return true;
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto contains(_In_ WDFCOLLECTION col, _In_ const UNICODE_STRING &str)
{
        PAGED_CODE();
        
        for (ULONG i = 0, cnt = WdfCollectionGetCount(col); i < cnt; ++i) {
                auto item = (WDFSTRING)WdfCollectionGetItem(col, i);

                UNICODE_STRING s{};
                WdfStringGetUnicodeString(item, &s);
                        
                if (RtlEqualUnicodeString(&s, &str, true)) {
                        return true;
                }
        }

        return false;
}

/*
 * Remove from collection A items absent in collection B.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void intersection(_In_ WDFCOLLECTION a, _In_ WDFCOLLECTION b)
{
        PAGED_CODE();

        for (ULONG i = 0, cnt = WdfCollectionGetCount(a); i < cnt; ) {
                auto item = (WDFSTRING)WdfCollectionGetItem(a, i);

                UNICODE_STRING s{};
                WdfStringGetUnicodeString(item, &s);

                if (contains(b, s)) {
                        ++i;
                } else {
                        TraceDbg("exclude %!USTR!", &s);
                        WdfCollectionRemoveItem(a, i);
                        --cnt;
                }
        }
}

/*
 * Refreshing allows to remove devices that constantly fail to attach.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED ULONG get_count(_In_ WDFCOLLECTION col, _In_ WDFKEY key, _In_ bool refresh)
{
        PAGED_CODE();

        if (!refresh) {
                //
        } else if (auto newcol = get_persistent_devices(key)) {
                intersection(col, newcol.get<WDFCOLLECTION>());
        } else {
                return 0;
        }

        return min(WdfCollectionGetCount(col), ARRAYSIZE(vhci_ctx::devices));
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugin_persistent_devices(_In_ vhci_ctx &ctx)
{
        PAGED_CODE();

        Registry key;
        if (auto err = open_parameters_key(key, KEY_QUERY_VALUE)) {
                return;
        }

        auto devices = get_persistent_devices(key.get());
        if (!(devices && WdfCollectionGetCount(devices.get<WDFCOLLECTION>()))) {
                return;
        }

        auto vhci = get_handle(&ctx);

        auto target = make_target(vhci);
        if (!target) {
                return;
        }

        vhci::ioctl::plugin_hardware req{{ .size = sizeof(req) }};

        WDF_MEMORY_DESCRIPTOR input;
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&input, &req, sizeof(req));

        constexpr auto outlen = offsetof(vhci::ioctl::plugin_hardware, port) + sizeof(req.port);

        WDF_MEMORY_DESCRIPTOR output;
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&output, &req, outlen);

        for (ULONG attempt = 0; true; ++attempt) {

                auto cnt = get_count(devices.get<WDFCOLLECTION>(), key.get(), attempt);
                if (!cnt) {
                        break;
                }

                if (auto secs = get_delay(attempt, cnt)) {
                        TraceDbg("attempt #%lu, %lu device(s), sleep %lu sec.", attempt, cnt, secs);
                        if (!sleep(ctx, secs)) {
                                break;
                        }
                }

                for (ULONG i = 0; i < cnt && sleep(ctx, 0); ) {
                        UNICODE_STRING str{};
                        if (auto s = (WDFSTRING)WdfCollectionGetItem(devices.get<WDFCOLLECTION>(), i)) {
                                WdfStringGetUnicodeString(s, &str);
                        }

                        if (plugin_hardware(str, target.get<WDFIOTARGET>(), req, input, output, outlen)) {
                                TraceDbg("exclude %!USTR!", &str);
                                WdfCollectionRemoveItem(devices.get<WDFCOLLECTION>(), i);
                                --cnt;
                        } else {
                                ++i;
                        }
                }
        }
}

/*
 * If attach_thread is set to NULL here, it is possible that this thread will be suspended 
 * while vhci_cleanup will be executed, WDFDEVICE will be destroyed, the driver will be unloaded.
 * IoCreateSystemThread is used to prevent this.
 */
_IRQL_requires_same_
_Function_class_(KSTART_ROUTINE)
PAGED void persistent_devices_thread(_In_ void *ctx)
{
        PAGED_CODE();
        KeSetPriorityThread(KeGetCurrentThread(), LOW_PRIORITY + 1);

        auto &vhci = *static_cast<vhci_ctx*>(ctx);
        plugin_persistent_devices(vhci);

        if (auto thread = (_KTHREAD*)InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vhci.attach_thread), nullptr)) {
                ObDereferenceObject(thread);
                TraceDbg("dereference");
        } else {
                TraceDbg("exit");
        }
}

} // namespace 


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::plugin_persistent_devices(_In_ vhci_ctx *vhci)
{
        PAGED_CODE();

        const auto access = THREAD_ALL_ACCESS;
        auto fdo = WdfDeviceWdmGetDeviceObject(get_handle(vhci));

        if (HANDLE handle; 
            auto err = IoCreateSystemThread(fdo, &handle, access, nullptr, nullptr, 
                                            nullptr, persistent_devices_thread, vhci)) {
                Trace(TRACE_LEVEL_ERROR, "IoCreateSystemThread %!STATUS!", err);
        } else {
                PVOID thread;
                NT_VERIFY(NT_SUCCESS(ObReferenceObjectByHandle(handle, access, *PsThreadType, KernelMode, 
                                                               &thread, nullptr)));

                NT_VERIFY(NT_SUCCESS(ZwClose(handle)));
                NT_VERIFY(!InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vhci->attach_thread), thread));
                TraceDbg("thread launched");
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
        if (st) {
                Trace(TRACE_LEVEL_ERROR, "WdfDriverOpenParametersRegistryKey(DesiredAccess=%lu) %!STATUS!", 
                                          DesiredAccess, st);
        }

        key.reset(k);
        return st;
}
