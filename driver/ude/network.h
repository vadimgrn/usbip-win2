/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\pageable.h>
#include <libdrv\mdl_cpp.h>
#include <libdrv\wsk_cpp.h>
#include <libdrv\pdu.h>

#include <usbip\consts.h>

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

enum : ULONG { URB_BUF_LEN = MAXULONG }; // set mdl_size to URB.TransferBufferLength

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS make_transfer_buffer_mdl(_Out_ Mdl &mdl, _In_ ULONG mdl_size, _In_ bool mdl_chain, _In_ LOCK_OPERATION Operation, 
                                  _In_ const _URB& urb);

_IRQL_requires_max_(DISPATCH_LEVEL)
inline auto verify(_In_ const WSK_BUF &buf, _In_ bool exact)
{
	if (buf.Offset || !buf.Length) {
		return false;
	}

	auto sz = size(buf.Mdl);
	return exact ? buf.Length == sz : buf.Length <= sz;
}

} // namespace usbip
