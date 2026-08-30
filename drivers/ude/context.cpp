/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "context.h"
#include "trace.h"
#include "context.tmh"

#include "driver.h"
#include "persistent.h"

#include <libdrv/strconv.h>
#include <libdrv/wsk_cpp.h>

#include <ntstrsafe.h>

namespace
{

using namespace usbip;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto save_device_location(_Inout_ device_attributes &attr, _In_ const vhci::imported_device_location &r)
{
        PAGED_CODE();

        struct {
                UNICODE_STRING *dst;
                const char *src;
                USHORT maxlen;
        } const v[] = {
                { &attr.node_name, r.host, sizeof(r.host) },
                { &attr.service_name, r.service, sizeof(r.service) },
                { &attr.busid, r.busid, sizeof(r.busid) },
        };

        for (auto& [dst, src, maxlen]: v) {
                if (!*src) {
                        continue; // RtlInitUnicodeString(&dst, nullptr); // the same as zeroed memory
                }

                auto st = libdrv::utf8_to_unicode(*dst, src, maxlen, PagedPool, unique_ptr::pooltag);
                if (NT_ERROR(st)) {
                        Trace(TRACE_LEVEL_ERROR, "utf8_to_unicode('%s') %!STATUS!", src, st);
                        return st;
                }
        }

        return STATUS_SUCCESS;
}

_Function_class_(EVT_WDF_DEVICE_CONTEXT_DESTROY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
PAGED void destroy_device_ctx_ext(_In_ WDFOBJECT object)
{
        PAGED_CODE();

        auto mem = static_cast<WDFMEMORY>(object);
        auto &ext = get_device_ctx_ext(mem);

        Trace(TRACE_LEVEL_INFORMATION, "%!USTR!:%!USTR!/%!USTR!", ext.node_name(), ext.service_name(), ext.busid());

        free(ext.sock);
        free(ext.attr);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED auto alloc_device_ctx_ext(_Inout_ WDFMEMORY &result, _In_ WDFOBJECT parent)
{
        PAGED_CODE();
        NT_ASSERT(!result);

        WDF_OBJECT_ATTRIBUTES attr;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ParentObject = parent;
        attr.EvtDestroyCallback = destroy_device_ctx_ext;

        device_ctx_ext *ext{};
        auto st = WdfMemoryCreate(&attr, NonPagedPoolNx, 0, sizeof(*ext), &result, reinterpret_cast<PVOID*>(&ext));

        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "WdfMemoryCreate %!STATUS!", st);
        } else {
                RtlZeroMemory(ext, sizeof(*ext));
        }

        return st;
}

} // namespace


 /*
  * First bit is reserved for direction of transfer (USBIP_DIR_OUT|USBIP_DIR_IN).
  * @see is_valid_seqnum
  */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto usbip::next_seqnum(_Inout_ device_ctx &dev, _In_ bool dir_in) -> seqnum_t
{
	static_assert(!direction::out);
	static_assert(direction::in);

	auto &seqnum = dev.seqnum;
	static_assert(sizeof(seqnum) == sizeof(LONG));

	while (true) {
		if (seqnum_t num = InterlockedIncrement(reinterpret_cast<LONG*>(&seqnum)) << 1) {
			return num |= seqnum_t(dir_in);
		}
	}
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::create_device_ctx_ext(
        _Inout_ WDFMEMORY &ctx_ext, _In_ WDFOBJECT parent, _In_ const vhci::ioctl::plugin_hardware &r)
{
        PAGED_CODE();

        WDFMEMORY mem{};
        ctx_ext = mem;

        auto st = alloc_device_ctx_ext(mem, parent); 
        if (NT_ERROR(st)) {
                return st;
        }

        wdf::ObjectDelete del(mem);
        auto &ext = get_device_ctx_ext(mem);

        st = init_device_attributes(ext.attr, r);
        if (NT_ERROR(st)) {
                return st;
        }

        auto &props = ext.properties();
        props.wsk_events = r.wsk_events;

        st = RtlStringCbCopyNA(props.serial, sizeof(props.serial), r.serial, sizeof(r.serial));
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "RtlStringCbCopyNA('%s') %!STATUS!", r.serial, st);
                return st;
        }

        ctx_ext = del.release<WDFMEMORY>();
        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
UDECXUSBDEVICE usbip::get_device(_In_ WDFREQUEST Request)
{
        auto req = get_request_ctx(Request);
        auto endp = get_endpoint_ctx(req->endpoint);
        return endp->device;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool usbip::is_valid_port(_In_ const vhci_ctx &ctx, _In_ int port)
{
        return port > 0 && port <= ctx.devices_cnt;
}

/**
 * @see free
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::init_device_attributes(
        _Inout_ device_attributes &attr, _In_ const vhci::imported_device_location &loc)
{
        PAGED_CODE();

        auto st = save_device_location(attr, loc);
        if (NT_SUCCESS(st)) {
                st = hash_location(attr.location_hash, attr);
        }

        if (NT_ERROR(st)) {
                free(attr);
        }

        return st;
}

/**
 * @see init_device_attributes
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::free(_Inout_ device_attributes &r)
{
        PAGED_CODE();

        libdrv::FreeUnicodeString(r.node_name, unique_ptr::pooltag); // @see RtlFreeUnicodeString
        libdrv::FreeUnicodeString(r.service_name, unique_ptr::pooltag);
        libdrv::FreeUnicodeString(r.busid, unique_ptr::pooltag);
}
