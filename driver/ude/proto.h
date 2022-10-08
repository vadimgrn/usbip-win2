#pragma once

#include <wdm.h>
#include <usbip\proto.h>

namespace usbip
{

struct device_ctx;
struct endpoint_ctx;

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS set_cmd_submit_usbip_header(
	_Out_ usbip_header &hdr, _Inout_ device_ctx &dev, _In_ const endpoint_ctx &endp,
	_In_ ULONG TransferFlags, _In_ ULONG TransferBufferLength = 0, _In_opt_ const bool *setup_dir_out = nullptr);

_IRQL_requires_max_(DISPATCH_LEVEL)
void set_cmd_unlink_usbip_header(_Out_ usbip_header &hdr, _Inout_ device_ctx &dev, _In_ seqnum_t seqnum_unlink);

} // namespace usbip
