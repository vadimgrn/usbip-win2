/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "persistent.h"
#include "context.h"
#include "trace.h"
#include "persistent.tmh"

#include <libdrv\wdf_cpp.h>
#include <libdrv\strconv.h>

namespace 
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto make_collection(_In_ WDFOBJECT parent)
{
        PAGED_CODE();

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = parent;

        WDFCOLLECTION col{};
        if (auto err = WdfCollectionCreate(&attr, &col)) {
                Trace(TRACE_LEVEL_ERROR, "WdfCollectionCreate %!STATUS!", err);
        }
        
        return col;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto get_persistent_devices(_In_ WDFKEY key)
{
        PAGED_CODE();
        
        auto col = make_collection(key);
        if (!col) {
                return col;
        }

        WDF_OBJECT_ATTRIBUTES str_attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&str_attr);
        str_attr.ParentObject = col;

        UNICODE_STRING value_name;
        RtlUnicodeStringInit(&value_name, persistent_devices_value_name);

        if (auto err = WdfRegistryQueryMultiString(key, &value_name, &str_attr, col)) {
                Trace(TRACE_LEVEL_ERROR, "WdfRegistryQueryMultiString('%!USTR!') %!STATUS!", &value_name, err);
                col = WDF_NO_HANDLE; // parent will destory it
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
        wdf::ObjectDelete target;

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

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void plugin_persistent_devices(_In_ vhci_ctx &ctx)
{
        PAGED_CODE();
        auto vhci = get_device(&ctx);

        wdf::Registry key;
        if (auto err = WdfDriverOpenParametersRegistryKey(WdfGetDriver(), KEY_QUERY_VALUE, 
                                                          WDF_NO_OBJECT_ATTRIBUTES, &key)) {
                Trace(TRACE_LEVEL_ERROR, "WdfDriverOpenParametersRegistryKey %!STATUS!", err);
                return;
        }

        auto col = get_persistent_devices(key.get());
        if (!col) {
                return;
        }

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

        auto max_cnt = min(WdfCollectionGetCount(col), ARRAYSIZE(ctx.devices));

        for (ULONG i = 0; i < max_cnt && !ctx.stop_thread; ++i) {

                UNICODE_STRING str{};
                if (auto s = (WDFSTRING)WdfCollectionGetItem(col, i)) {
                        WdfStringGetUnicodeString(s, &str);
                }

                if (auto err = parse_string(req, str)) {
                        Trace(TRACE_LEVEL_ERROR, "'%!USTR!' parse %!STATUS!", &str, err);
                        continue;
                }

                Trace(TRACE_LEVEL_INFORMATION, "%s:%s/%s", req.host, req.service, req.busid);
                req.port = 0;

                if (ULONG_PTR BytesReturned; // send IOCTL to itself
                        auto err = WdfIoTargetSendIoctlSynchronously(target.get<WDFIOTARGET>(), WDF_NO_HANDLE, 
                                vhci::ioctl::PLUGIN_HARDWARE, &input, &output, nullptr, &BytesReturned)) {
                        Trace(TRACE_LEVEL_ERROR, "WdfIoTargetSendIoctlSynchronously %!STATUS!", err);
                } else {
                        NT_ASSERT(BytesReturned == outlen);
                }
        }
}

/*
 * If load_thread is set to NULL here, it is possible that this thread will be suspended 
 * while vhci_cleanup will be executed, WDFDEVICE will be destroyed, the driver will be unloaded.
 * IoCreateSystemThread is used to prevent this.
 */
_IRQL_requires_same_
_Function_class_(KSTART_ROUTINE)
PAGED void run(_In_ void *ctx)
{
        PAGED_CODE();

        auto &vhci = *static_cast<vhci_ctx*>(ctx);
        plugin_persistent_devices(vhci);

        if (auto thread = (_KTHREAD*)InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vhci.load_thread), nullptr)) {
                ObDereferenceObject(thread);
                TraceDbg("thread closed");
        } else {
                TraceDbg("thread exited");
        }
}

} // namespace 


_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::plugin_persistent_devices(_In_ vhci_ctx *vhci)
{
        PAGED_CODE();

        const auto access = THREAD_ALL_ACCESS;
        auto fdo = WdfDeviceWdmGetDeviceObject(get_device(vhci));

        if (HANDLE handle; 
            auto err = IoCreateSystemThread(fdo, &handle, access, nullptr, nullptr, nullptr, run, vhci)) {
                Trace(TRACE_LEVEL_ERROR, "PsCreateSystemThread %!STATUS!", err);
        } else {
                PVOID thread;
                NT_VERIFY(NT_SUCCESS(ObReferenceObjectByHandle(handle, access, *PsThreadType, KernelMode, 
                                                               &thread, nullptr)));

                NT_VERIFY(!InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vhci->load_thread), thread));
                NT_VERIFY(NT_SUCCESS(ZwClose(handle)));
        }
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::copy(
        _Out_ char *host, _In_ USHORT host_sz, _In_ const UNICODE_STRING &uhost,
        _Out_ char *service, _In_ USHORT service_sz, _In_ const UNICODE_STRING &uservice,
        _Out_ char *busid, _In_ USHORT busid_sz, _In_ const UNICODE_STRING &ubusid)
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
