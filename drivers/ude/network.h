/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include <libdrv\codeseg.h>
#include <libdrv\mdl_cpp.h>
#include <libdrv\wsk_cpp.h>

#include <usbip\consts.h>

struct _URB;
struct usbip_header;

namespace usbip
{

using wsk::SOCKET;

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void close_socket(_Inout_ SOCKET* &sock);

_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS send(_Inout_ SOCKET *sock, _In_ memory pool, _In_ void *data, _In_ ULONG len);

_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS recv(_Inout_ SOCKET *sock, _In_ memory pool, _Inout_ void *data, _In_ ULONG len);

_IRQL_requires_(PASSIVE_LEVEL)
PAGED ULONG recv_op_common(_Inout_ SOCKET *sock, _In_ UINT16 expected_code);

enum : ULONG { URB_BUF_LEN = MAXULONG }; // set mdl_size to URB.TransferBufferLength

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS make_transfer_buffer_mdl(_Inout_ Mdl &mdl, _In_ ULONG mdl_size, _In_ bool mdl_chain, _In_ LOCK_OPERATION Operation, 
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
