/*
 * Copyright (C) 2022 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "network.h"
#include "trace.h"
#include "network.tmh"

#include "urbtransfer.h"

#include <usbip\proto.h>
#include <usbip\proto_op.h>

#include <libdrv\dbgcommon.h>
#include <libdrv\usbd_helper.h>

#include <libusbip/src/op_common.h>

_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::send(_Inout_ SOCKET *sock, _In_ memory pool, _In_ void *data, _In_ ULONG len)
{
        PAGED_CODE();

        Mdl mdl(data, len);
        if (auto err = pool == memory::nonpaged ? mdl.prepare_nonpaged() : mdl.prepare_paged(IoReadAccess)) {
                return err;
        }

        WSK_BUF buf{ .Mdl = mdl.get(), .Length = len };
        return send(sock, &buf, WSK_FLAG_NODELAY);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::recv(_Inout_ SOCKET *sock, _In_ memory pool, _Inout_ void *data, _In_ ULONG len)
{
        PAGED_CODE();

        Mdl mdl(data, len);
        if (auto err = pool == memory::nonpaged ? mdl.prepare_nonpaged() : mdl.prepare_paged(IoWriteAccess)) {
                return err;
        }

        WSK_BUF buf{ .Mdl = mdl.get(), .Length = len };
        return receive(sock, &buf);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED USBIP_STATUS usbip::recv_op_common(_Inout_ SOCKET *sock, _In_ UINT16 expected_code)
{
        PAGED_CODE();

        op_common r{};
        if (auto err = recv(sock, memory::stack, &r, sizeof(r))) {
                Trace(TRACE_LEVEL_ERROR, "Receive %!STATUS!", err);
                return USBIP_ERROR_NETWORK;
        }
	PACK_OP_COMMON(false, &r);

	if (r.version != USBIP_VERSION) {
		Trace(TRACE_LEVEL_ERROR, "version(%#x) != expected(%#x)", r.version, USBIP_VERSION);
		return USBIP_ERROR_VERSION;
	}

        if (r.code != expected_code) {
                Trace(TRACE_LEVEL_ERROR, "code(%#x) != expected(%#x)", r.code, expected_code);
                return USBIP_ERROR_PROTOCOL;
        }

        auto st = static_cast<op_status_t>(r.status);
        if (st) {
                Trace(TRACE_LEVEL_ERROR, "code %#x, %!op_status_t!", r.code, st);
        }
        return op_status_error(st);
}

/*
 * URB must have TransferBuffer* members.
 * TransferBuffer && TransferBufferMDL can be both not NULL for bulk/int at least.
 * 
 * TransferBufferMDL can be a chain and have size greater than mdl_size. 
 * If attach tail to this MDL (as for isoch transfer), new MDL must be used 
 * to describe a buffer with required mdl_size.
 * 
 * If use MmBuildMdlForNonPagedPool for TransferBuffer, DRIVER_VERIFIER_DETECTED_VIOLATION (c4) will happen sooner or later,
 * Arg1: 0000000000000140, Non-locked MDL constructed from either pageable or tradable memory.
 * 
 * @param mdl_size pass URB_BUF_LEN to use TransferBufferLength, real value must not be greater than TransferBufferLength
 * @param mdl_chain tail will be attached to this mdl
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::make_transfer_buffer_mdl(
        _Inout_ Mdl &mdl, _In_ ULONG mdl_size, _In_ bool mdl_chain, _In_ LOCK_OPERATION Operation, _In_ const URB &urb)
{
        NT_ASSERT(!mdl);
        auto &r = AsUrbTransfer(urb);

        if (mdl_size == URB_BUF_LEN) {
                mdl_size = r.TransferBufferLength;
        } else if (mdl_size > r.TransferBufferLength) {
                return STATUS_INVALID_PARAMETER;
        }
        
        if (!mdl_size) {
                return STATUS_SUCCESS;
        }

        auto make = [&mdl, mdl_size, Operation] (auto buf, auto probe_and_lock)
        {
                mdl = Mdl(buf, mdl_size);
                auto err = probe_and_lock ? mdl.prepare_paged(Operation) : mdl.prepare_nonpaged();
                if (err) {
                        mdl.reset();
                }
                return err;
        };

        if (r.TransferBufferMDL) {
                // preferable case because it is locked-down
        } else if (auto buf = r.TransferBuffer) { // could be allocated from paged pool
                return make(buf, true); // false -> DRIVER_VERIFIER_DETECTED_VIOLATION
        } else {
                Trace(TRACE_LEVEL_ERROR, "TransferBuffer and TransferBufferMDL are NULL");
                return STATUS_INVALID_PARAMETER;
        }

        auto head = r.TransferBufferMDL;
        auto len = size(head); // can be a chain

        auto st = STATUS_SUCCESS;

        if (len < r.TransferBufferLength) { // must describe full buffer
                st = STATUS_BUFFER_TOO_SMALL;
        } else if (len == mdl_size || (len > mdl_size && !mdl_chain)) { // WSK_BUF.Length will cut extra length
                NT_VERIFY(mdl = Mdl(head));
        } else if (!head->Next) { // build partial MDL
                mdl = Mdl(head, 0, mdl_size);
        } else if (auto buf = MmGetSystemAddressForMdlSafe(head, NormalPagePriority | MdlMappingNoExecute)) {
                // IoBuildPartialMdl doesn't treat SourceMdl as a chain and can't be used
                st = make(buf, false); // if use MmGetMdlVirtualAddress(head) -> IRQL_NOT_LESS_OR_EQUAL
        }

        if (!mdl && NT_SUCCESS(st)) {
                st = STATUS_INSUFFICIENT_RESOURCES;
        }

        return st;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::close_socket(_Inout_ SOCKET* &sock)
{
        PAGED_CODE();

        auto socket = (SOCKET*)InterlockedExchangePointer(reinterpret_cast<PVOID*>(&sock), nullptr);
        if (!socket) {
                return false;
        }

        if (auto err = disconnect(socket)) { // close must be called anyway
                Trace(TRACE_LEVEL_ERROR, "disconnect %!STATUS!", err);
        }

        if (auto err = close(socket)) {
                Trace(TRACE_LEVEL_ERROR, "close %!STATUS!", err);
                return false;
        }

        return true;
}

