/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <usbip/proto.h>
#include <libdrv/wdf_cpp.h>

namespace usbip
{

struct device_ctx;
struct wsk_context;

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
bool validate(_Inout_ header &hdr);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFREQUEST ret_command(_In_ const header &hdr, _Inout_ device_ctx &dev);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS ret_submit(_Inout_ wsk_context &ctx);

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void complete(_In_ WDFREQUEST request, _In_ NTSTATUS status);

/*
 * ret_submit() set URB.UrbHeader.Status, atomic_complete set IRP.IoStatus.Status
 */
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
inline void complete(_In_ WDFREQUEST request)
{
        complete(request, WdfRequestGetStatus(request));
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto& get_ret_submit(_In_ const header &hdr)
{
        NT_ASSERT(hdr.command == RET_SUBMIT);
        return hdr.ret_submit;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto check(_In_ ULONG TransferBufferLength, _In_ int actual_length)
{
        return  actual_length >= 0 && static_cast<ULONG>(actual_length) <= TransferBufferLength ? 
                STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;
}

_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
constexpr auto assign(_Inout_ ULONG &TransferBufferLength, _In_ int actual_length)
{
        auto err = check(TransferBufferLength, actual_length);
        TransferBufferLength = err ? 0 : actual_length;
        return err;
}

} // namespace usbip
