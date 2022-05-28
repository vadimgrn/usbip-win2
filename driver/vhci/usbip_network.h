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

NTSTATUS send(SOCKET *sock, memory pool, void *data, ULONG len);
NTSTATUS recv(SOCKET *sock, memory pool, void *data, ULONG len);

err_t recv_op_common(_In_ SOCKET *sock, _In_ UINT16 expected_code, _Out_ op_status_t &status);
NTSTATUS send_cmd(_In_ SOCKET *sock, _Inout_ usbip_header &hdr, _Inout_opt_ const _URB *transfer_buffer = nullptr);

NTSTATUS make_transfer_buffer_mdl(_Out_ Mdl &mdl, _In_ const _URB &urb);

} // namespace usbip
