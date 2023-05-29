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

namespace {

constexpr auto make_priority( _In_ LOCK_OPERATION operation)
{
        return NormalPagePriority | MdlMappingNoExecute | 
                (operation == IoReadAccess ? MdlMappingNoWrite : 0UL);
}

} // namespace


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
 * TransferBufferMDL is not used directly because of BSODs in random third-party drivers during "usbip detach".
 * It happens rarely, but ~1500 attach/detach loops is used to enough to get it.
 * Symptoms: read memory address 0x0000'0000'0000'0008.
 * The possible reason could be that we do not own TransferBufferMDL and it can be freed by IOManager.
 * A partial MDL made from TransferBufferMDL fixes such BSODs.
 * 
 * If use MmBuildMdlForNonPagedPool for TransferBuffer, DRIVER_VERIFIER_DETECTED_VIOLATION (c4) will happen sooner or later,
 * Arg1: 0000000000000140, Non-locked MDL constructed from either pageable or tradable memory.
 * 
 * @param mdl_size pass URB_BUF_LEN to use TransferBufferLength, real value must not be greater than TransferBufferLength
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::make_transfer_buffer_mdl(
        _Inout_ Mdl &mdl, _In_ ULONG mdl_size, _In_ LOCK_OPERATION operation, _In_ const URB &urb)
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

        void *buf{};
        bool probe_and_lock;

        if (auto head = r.TransferBufferMDL) { // preferable case because it is locked-down, can be a chain

                if (auto len = size(head); len < r.TransferBufferLength) { // must describe full buffer
                        return STATUS_BUFFER_TOO_SMALL;
                } else if (!head->Next) { // source MDL is not a chain
                        mdl = Mdl(head, 0, mdl_size);
                        return mdl ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
                } else if (buf = MmGetSystemAddressForMdlSafe(head, make_priority(operation)); !buf) {
                        return STATUS_INSUFFICIENT_RESOURCES;        
                } else { // IoBuildPartialMdl doesn't treat SourceMdl as a chain and can't be used
                        probe_and_lock = false;
                }

        } else if (buf = r.TransferBuffer; buf) { // could be allocated from paged pool
                probe_and_lock = true; // false -> DRIVER_VERIFIER_DETECTED_VIOLATION
        } else {
                Trace(TRACE_LEVEL_ERROR, "TransferBuffer and TransferBufferMDL are NULL");
                return STATUS_INVALID_PARAMETER;
        }

        NT_ASSERT(buf);
        mdl = Mdl(buf, mdl_size);

        auto st = probe_and_lock ? mdl.prepare_paged(operation) : mdl.prepare_nonpaged();
        if (st) {
                mdl.reset();
        }
        return st;
}

/*
 * wsk::close() does not free SOCKET and wsk:free() is not called here.
 * Retaining SOCKET alive solves the issue with possible send/receive calls after closing.
 * Now such calls return an error and are thread-safe (send/receive/.../close can be executed concurrently).
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::close_socket(_In_ SOCKET *sock)
{
        PAGED_CODE();

        if (!sock) {
                return false;
        }

        if (auto err = disconnect(sock)) { // close must be called anyway
                Trace(TRACE_LEVEL_ERROR, "disconnect %!STATUS!", err);
        }

        if (auto err = close(sock)) { // further calls must return STATUS_NOT_SUPPORTED
                Trace(TRACE_LEVEL_ERROR, "close %!STATUS!", err);
                return false;
        }

        NT_ASSERT(send(sock, static_cast<WSK_BUF*>(nullptr), 0, static_cast<IRP*>(nullptr)) == STATUS_NOT_SUPPORTED);
        NT_ASSERT(receive(sock, static_cast<WSK_BUF*>(nullptr), 0, static_cast<IRP*>(nullptr)) == STATUS_NOT_SUPPORTED);
        NT_ASSERT(disconnect(sock, static_cast<WSK_BUF*>(nullptr), 0) == STATUS_NOT_SUPPORTED);
        NT_ASSERT(close(sock) == STATUS_NOT_SUPPORTED);

        return true;
}
