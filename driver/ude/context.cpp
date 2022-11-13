/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "context.h"
#include "trace.h"
#include "context.tmh"

#include "driver.h"

#include <libdrv\strutil.h>

 /*
  * First bit is reserved for direction of transfer (USBIP_DIR_OUT|USBIP_DIR_IN).
  * @see is_valid_seqnum
  */
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
PAGED NTSTATUS usbip::create_device_ctx_ext(_Outptr_ device_ctx_ext* &ext, _In_ const vhci::ioctl_plugin &r)
{
        PAGED_CODE();

        ext = (device_ctx_ext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*ext), POOL_TAG);
        if (!ext) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate device_ctx_ext");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        ext->busid = libdrv_strdup(POOL_FLAG_NON_PAGED, r.busid);
        if (!ext->busid) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error", r.busid);
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        struct {
                UNICODE_STRING &ustr;
                const char *ansi;
        } const v[] = {
                {ext->node_name, r.host},
                {ext->service_name, r.service},
                {ext->serial, r.serial}
        };

        for (auto &[ustr, ansi]: v) {
                if (!*ansi) {
                        // RtlInitUnicodeString(&uni, nullptr); // the same as zeroed memory
                } else if (auto err = to_unicode_str(ustr, ansi)) {
                        Trace(TRACE_LEVEL_ERROR, "to_unicode_str('%s') %!STATUS!", ansi, err);
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

        libdrv_free(ext->busid);
        RtlFreeUnicodeString(&ext->node_name);
        RtlFreeUnicodeString(&ext->service_name);
        RtlFreeUnicodeString(&ext->serial);

        ExFreePoolWithTag(ext, POOL_TAG);
}
