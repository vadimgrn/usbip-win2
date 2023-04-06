/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "context.h"
#include "trace.h"
#include "context.tmh"

#include "driver.h"
#include <libdrv\strconv.h>

 /*
  * First bit is reserved for direction of transfer (USBIP_DIR_OUT|USBIP_DIR_IN).
  * @see is_valid_seqnum
  */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
seqnum_t usbip::next_seqnum(_Inout_ device_ctx &dev, _In_ bool dir_in)
{
	static_assert(!USBIP_DIR_OUT);
	static_assert(USBIP_DIR_IN);

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

        ext = (device_ctx_ext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*ext), pooltag);
        if (!ext) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate device_ctx_ext");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        struct {
                UNICODE_STRING &dst;
                const char *src;
        } const v[] = {
                {ext->node_name, r.host},
                {ext->service_name, r.service},
                {ext->busid, r.busid},
        };

        for (auto &[dst, src]: v) {
                if (!*src) {
                        // RtlInitUnicodeString(&dst, nullptr); // the same as zeroed memory
                } else if (auto err = libdrv::utf8_to_unicode(dst, src)) {
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
        NT_ASSERT(!ext->sock);

        RtlFreeUnicodeString(&ext->node_name);
        RtlFreeUnicodeString(&ext->service_name);
        RtlFreeUnicodeString(&ext->busid);

        ExFreePoolWithTag(ext, pooltag);
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
auto usbip::find_endpoint(_In_ const device_ctx &dev, _In_ USBD_PIPE_HANDLE PipeHandle) -> endpoint_ctx* 
{
        NT_ASSERT(PipeHandle);

        auto ep0 = get_endpoint_ctx(dev.ep0);
        auto head = &ep0->entry;

        for (auto entry = head->Flink; entry != head; entry = entry->Flink) {
                auto endp = CONTAINING_RECORD(entry, endpoint_ctx, entry);
                if (endp->PipeHandle == PipeHandle) {
                        return endp;
                }
        }

        TraceDbg("PipeHandle %04x not found", ptr04x(PipeHandle));
        return nullptr;
}
