/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "context.h"
#include "trace.h"
#include "context.tmh"

#include "driver.h"
#include <libdrv\strutil.h>

namespace
{

/*
 * RtlFreeUnicodeString must be used to release memory.
 * @see RtlUTF8StringToUnicodeString
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto to_unicode_str(_Out_ UNICODE_STRING &dst, _In_ const char *ansi)
{
        PAGED_CODE();

        ANSI_STRING s;
        RtlInitAnsiString(&s, ansi);

        return RtlAnsiStringToUnicodeString(&dst, &s, true);
}

} // namespace


 /*
  * First bit is reserved for direction of transfer (USBIP_DIR_OUT|USBIP_DIR_IN).
  * @see is_valid_seqnum
  */
_IRQL_requires_max_(DISPATCH_LEVEL)
seqnum_t usbip::next_seqnum(_Inout_ device_ctx &udev, _In_ bool dir_in)
{
	static_assert(!USBIP_DIR_OUT);
	static_assert(USBIP_DIR_IN);

	auto &seqnum = udev.data->seqnum;
	static_assert(sizeof(seqnum) == sizeof(LONG));

	while (true) {
		if (seqnum_t num = InterlockedIncrement(reinterpret_cast<LONG*>(&seqnum)) << 1) {
			return num |= seqnum_t(dir_in);
		}
	}
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::create(_Outptr_ device_ctx_data* &data, _In_ const vhci::ioctl_plugin &r)
{
        PAGED_CODE();

        data = (device_ctx_data*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*data), POOL_TAG);
        if (!data) {
                Trace(TRACE_LEVEL_ERROR, "Can't allocate device_ctx_data");
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        data->busid = libdrv_strdup(POOL_FLAG_NON_PAGED, r.busid);
        if (!data->busid) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error", r.busid);
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (auto err = to_unicode_str(data->node_name, r.host)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.host, err);
                return err;
        }

        if (auto err = to_unicode_str(data->service_name, r.service)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.service, err);
                return err;
        }

        if (!*r.serial) {
                // RtlInitUnicodeString(&ctx.serial, nullptr);
        } else if (auto err = to_unicode_str(data->serial, r.serial)) {
                Trace(TRACE_LEVEL_ERROR, "Copy '%s' error %!STATUS!", r.serial, err);
                return err;
        }

        return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE void usbip::free(_In_ device_ctx_data *data)
{
        PAGED_CODE();

        if (!data) {
                return;
        }

        NT_ASSERT(!data->sock);
        libdrv_free(data->busid);

        RtlFreeUnicodeString(&data->node_name);
        RtlFreeUnicodeString(&data->service_name);
        RtlFreeUnicodeString(&data->serial);

        ExFreePoolWithTag(data, POOL_TAG);
}
