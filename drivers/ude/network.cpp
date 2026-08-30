/*
 * Copyright (c) 2022-2026 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "network.h"
#include "trace.h"
#include "network.tmh"

#include "urbtransfer.h"

#include <usbip/proto.h>
#include <usbip/proto_op.h>

#include <libdrv/dbgcommon.h>
#include <libdrv/usbd_helper.h>

#include <libusbip/src/op_common.h>

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::send(_In_ SOCKET *sock, _In_ memory pool, _In_ void *data, _In_ ULONG len)
{
        PAGED_CODE();
        Mdl mdl(data, len);

        auto st = pool == memory::nonpaged ? mdl.prepare_nonpaged() : mdl.prepare_paged(IoReadAccess);
        if (NT_ERROR(st)) {
                return st;
        }

        WSK_BUF buf{ .Mdl = mdl.get(), .Length = len };
        return send(sock, &buf, WSK_FLAG_NODELAY);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED NTSTATUS usbip::recv(_In_ SOCKET *sock, _In_ memory pool, _Inout_ void *data, _In_ ULONG len)
{
        PAGED_CODE();
        Mdl mdl(data, len);

        auto st = pool == memory::nonpaged ? mdl.prepare_nonpaged() : mdl.prepare_paged(IoWriteAccess);
        if (NT_ERROR(st)) {
                return st;
        }

        WSK_BUF buf{ .Mdl = mdl.get(), .Length = len };
        return receive(sock, &buf);
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED USBIP_STATUS usbip::recv_op_common(_In_ SOCKET *sock, _In_ UINT16 expected_code)
{
        PAGED_CODE();

        op_common r{};
        auto st = recv(sock, memory::stack, &r, sizeof(r));
        if (NT_ERROR(st)) {
                Trace(TRACE_LEVEL_ERROR, "Receive %!STATUS!", st);
                return st;
        }
        byteswap(r);

	if (r.version != USBIP_VERSION) {
		Trace(TRACE_LEVEL_ERROR, "version(%#x) != expected(%#x)", r.version, USBIP_VERSION);
		return USBIP_ERROR_VERSION;
	}

        if (r.code != expected_code) {
                Trace(TRACE_LEVEL_ERROR, "code(%#x) != expected(%#x)", r.code, expected_code);
                return USBIP_ERROR_PROTOCOL;
        }

        auto op_st = static_cast<op_status_t>(r.status);
        if (op_st) {
                Trace(TRACE_LEVEL_ERROR, "code %#x, %!op_status_t!", r.code, op_st);
        }
        return op_status_error(op_st);
}

/*
 * URB must have TransferBuffer* members.
 * 
 * TransferBuffer && TransferBufferMDL can be both not NULL for bulk/int at least.
 * Microsoft documentation specifically states that a client driver can safely
 * populate both fields at the same time. If you do this, both pointers
 * must point to the exact same underlying memory buffer.
 *
 * Use TransferBufferMDL if it is present: Hardware and lower-level drivers
 * (like the HCD processing requests at DISPATCH_LEVEL) prioritize the MDL.
 * The MDL provides the safe physical memory locking required for DMA) transactions.
 * If you need a virtual pointer to inspect or write data in your code, use TransferBuffer. 
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
_IRQL_requires_same_
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

        if (auto head = r.TransferBufferMDL) { // preferable case because it is locked-down, can be a chain

                auto len = static_cast<ULONG>(size(head));

                if (len < mdl_size && (head->Next || operation == IoReadAccess)) {
                        Trace(TRACE_LEVEL_ERROR, "MDL size %lu < mdl_size(%lu)", len, mdl_size);
                        return STATUS_BUFFER_TOO_SMALL;
                } else if (head->Next) { 
                        Trace(TRACE_LEVEL_ERROR, "chained MDL mapping into linear system VA space is invalid");
                        return STATUS_NOT_SUPPORTED;
                }

                // The caller may have asked for more than this MDL covers (e.g. Mm partial-final-cluster
                // paging IO where cdrom.sys rounded URB.TransferBufferLength up to a sector). Build a partial MDL
                // on what is actually locked down; the caller is responsible for chaining a gap MDL.
                mdl = Mdl(head, 0, min(len, mdl_size));

                // The partial MDL inherits the parent's locked layout, it requires no
                // secondary pool updates. Mdl class unprepare() naturally handles locked()
                // and partial() scenarios cleanly without needing a raw flag mutation here.
                return mdl ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
        }

        if (!r.TransferBuffer) { 
                Trace(TRACE_LEVEL_ERROR, "TransferBuffer and TransferBufferMDL are NULL");
                return STATUS_INVALID_PARAMETER;
        }

        if (KeGetCurrentIrql() > APC_LEVEL) {
                Trace(TRACE_LEVEL_ERROR, "TransferBuffer: MmProbeAndLockPages cannot be called at DISPATCH_LEVEL");
                return STATUS_MUTANT_NOT_OWNED; 
        }

        mdl = Mdl(r.TransferBuffer, mdl_size);

        auto st = mdl.prepare_paged(operation); // calls MmProbeAndLockPages
        if (NT_ERROR(st)) {
                mdl.reset();
        }
        return st;
}

/*
 * wsk::close() does not free SOCKET and wsk:free() is not called here.
 * Retaining SOCKET alive solves the issue with possible send/receive calls after closing.
 * Now such calls return an error and are thread-safe (send/receive/.../close can be executed concurrently).
 * 
 * Can be called several times.
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED bool usbip::close_socket(_In_ SOCKET *sock)
{
        PAGED_CODE();

        if (!sock) {
                return false;
        }

        auto st = disconnect(sock);
        if (NT_ERROR(st)) { // close must be called anyway
                Trace(TRACE_LEVEL_ERROR, "disconnect %!STATUS!", st);
        }

        st = close(sock);
        if (NT_ERROR(st)) { // further calls must return STATUS_NOT_SUPPORTED
                Trace(TRACE_LEVEL_ERROR, "close %!STATUS!", st);
                return false;
        }

        NT_ASSERT(send(sock, static_cast<WSK_BUF*>(nullptr), 0, static_cast<IRP*>(nullptr)) == STATUS_NOT_SUPPORTED);
        NT_ASSERT(receive(sock, static_cast<WSK_BUF*>(nullptr), 0, static_cast<IRP*>(nullptr)) == STATUS_NOT_SUPPORTED);
        NT_ASSERT(disconnect(sock, static_cast<WSK_BUF*>(nullptr), 0) == STATUS_NOT_SUPPORTED);
        NT_ASSERT(close(sock) == STATUS_NOT_SUPPORTED);

        return true;
}

/**
 * Advanced network drivers sometimes set SO_RCVBUF to exactly 0.
 * Purpose: This bypasses the intermediate Ancillary Function Driver (AFD.sys) kernel copying buffers entirely.
 * Requirement: You should only do this if your WSK driver is actively utilizing standard,
 * asynchronous WskReceive IRP pipelines instead of event callbacks. By bypassing the internal kernel buffer,
 * the network card will directly copy incoming packet payloads into the explicit MDL (Memory Descriptor List)
 * memory buffers provided by your pending IRPs.
 *
 * The Problem:
 * When SO_RCVBUF is 0, TCP cannot slide its window or acknowledge packets unless an explicit WSK
 * receive IRP is actively pending. During the precise fraction of a millisecond where your driver
 * is executing step 2 (processing the header and parsing the URB), there is no pending receive IRP.
 * Because AFD.sys has no buffer to absorb incoming stream data during that window, the local TCP stack
 * will freeze its advertised window size to 0, choking the network sender and stalling the connection
 * pipeline.
 * 
 * @param size pass zero to bypasses AFD.sys copying completely
 */
_IRQL_requires_same_
_IRQL_requires_max_(APC_LEVEL)
PAGED NTSTATUS usbip::set_recvbuf_size(_In_ SOCKET *sock, _In_ ULONG size)
{
        PAGED_CODE();
        return control(sock, WskSetOption, SO_RCVBUF, SOL_SOCKET,
                       sizeof(size), &size, 0, nullptr, nullptr, true, nullptr);
}
