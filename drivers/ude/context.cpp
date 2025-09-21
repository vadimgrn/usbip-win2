/*
 * Copyright (c) 2022-2025 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "context.h"
#include "trace.h"
#include "context.tmh"

#include "driver.h"

#include <libdrv\strconv.h>
#include <libdrv\wsk_cpp.h>

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
        _Out_ device_ctx_ext* &ext, _In_ const vhci::ioctl::plugin_hardware &r)
{
        PAGED_CODE();

        ext = (device_ctx_ext*)ExAllocatePoolZero(NonPagedPoolNx, sizeof(*ext), pooltag);
        if (!ext) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate device_ctx_ext");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        struct {
                UNICODE_STRING *dst;
                const char *src;
                USHORT maxlen;
        } const v[] = {
                { ext->node_name(), r.host, sizeof(r.host) },
                { ext->service_name(), r.service, sizeof(r.service) },
                { ext->busid(), r.busid, sizeof(r.busid) },
        };

        for (auto &[dst, src, maxlen]: v) {
                if (!*src) {
                        // RtlInitUnicodeString(&dst, nullptr); // the same as zeroed memory
                } else if (auto err = libdrv::utf8_to_unicode(*dst, src, maxlen, PagedPool, pooltag)) {
                        Trace(TRACE_LEVEL_ERROR, "utf8_to_unicode('%s') %!STATUS!", src, err);
                        return err;
                }
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::free(_In_ device_ctx_ext *ext)
{
        PAGED_CODE();

        NT_ASSERT(ext);
        free(ext->sock);

        libdrv::FreeUnicodeString(*ext->node_name(), pooltag); // @see RtlFreeUnicodeString
        libdrv::FreeUnicodeString(*ext->service_name(), pooltag);
        libdrv::FreeUnicodeString(*ext->busid(), pooltag);

        ExFreePoolWithTag(ext, pooltag);
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
