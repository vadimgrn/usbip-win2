/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "usbip_api_consts.h"
#include "mdl_cpp.h"
#include "wsk_cpp.h"

struct _URB;
struct usbip_header;

namespace usbip
{

using wsk::SOCKET;

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS send(_Inout_ SOCKET *sock, _In_ memory pool, _In_ void *data, _In_ ULONG len);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS recv(_Inout_ SOCKET *sock, _In_ memory pool, _Out_ void *data, _In_ ULONG len);

_IRQL_requires_(PASSIVE_LEVEL)
err_t recv_op_common(_Inout_ SOCKET *sock, _In_ UINT16 expected_code, _Out_ op_status_t &status);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS send_cmd(_Inout_ SOCKET *sock, _Inout_ usbip_header &hdr, _Inout_opt_ _URB *transfer_buffer = nullptr);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS make_transfer_buffer_mdl(_Out_ Mdl &mdl, _In_ LOCK_OPERATION Operation, _In_ const _URB &urb);

} // namespace usbip
