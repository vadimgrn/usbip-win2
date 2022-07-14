/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "pageable.h"
#include "usbip_api_consts.h"
#include "mdl_cpp.h"
#include "wsk_cpp.h"
#include "pdu.h"

struct _URB;
struct usbip_header;

namespace usbip
{

using wsk::SOCKET;

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS send(_Inout_ SOCKET *sock, _In_ memory pool, _In_ void *data, _In_ ULONG len);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS recv(_Inout_ SOCKET *sock, _In_ memory pool, _Out_ void *data, _In_ ULONG len);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE err_t recv_op_common(_Inout_ SOCKET *sock, _In_ UINT16 expected_code, _Out_ op_status_t &status);

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS send_cmd(_Inout_ SOCKET *sock, _Inout_ usbip_header &hdr, _Inout_opt_ _URB *transfer_buffer = nullptr);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS make_transfer_buffer_mdl(_Out_ Mdl &mdl, _In_ LOCK_OPERATION Operation, _In_ const _URB &urb);

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto make_wsk_buf(_In_ const Mdl &mdl_hdr, _In_ const usbip_header &hdr)
{
        WSK_BUF buf{ mdl_hdr.get(), 0, get_total_size(hdr) };

        NT_ASSERT(buf.Length >= mdl_hdr.size());
        NT_ASSERT(buf.Length <= size(mdl_hdr)); // MDL for TransferBuffer can be larger than TransferBufferLength

        return buf;
}

} // namespace usbip
