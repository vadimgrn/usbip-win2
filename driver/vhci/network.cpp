/*
 * Copyright (C) 2022 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "network.h"
#include "trace.h"
#include "network.tmh"

#include "dev.h"
#include "dbgcommon.h"
#include "urbtransfer.h"
#include "usbip_proto.h"
#include "usbip_proto_op.h"
#include "usbd_helper.h"
#include "irp.h"

namespace
{

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE auto recv_ret_submit(_Inout_ usbip::SOCKET *sock, _Inout_ URB &urb, _Inout_ usbip_header &hdr, _Inout_ usbip::Mdl &mdl_buf)
{
        PAGED_CODE();

        auto &base = hdr.base;
        NT_ASSERT(base.command == USBIP_RET_SUBMIT);

        auto &tr = AsUrbTransfer(urb);

        {
                auto &ret = hdr.u.ret_submit;
                urb.UrbHeader.Status = ret.status ? to_windows_status(ret.status) : USBD_STATUS_SUCCESS;

                auto err = usbip::assign(tr.TransferBufferLength, ret.actual_length);
                if (err || base.direction == USBIP_DIR_OUT || !tr.TransferBufferLength) { 
                        return err;
                }
        }

        NT_ASSERT(mdl_buf.size() >= tr.TransferBufferLength);
        WSK_BUF buf{ mdl_buf.get(), 0, tr.TransferBufferLength };

        if (auto err = receive(sock, &buf)) {
                Trace(TRACE_LEVEL_ERROR, "Receive buffer[%Iu] %!STATUS!", buf.Length, err);
                return err;
        }

        TraceDbg("[%Iu]%!BIN!", buf.Length, WppBinary(mdl_buf.sysaddr(LowPagePriority), static_cast<USHORT>(buf.Length)));
        return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::send(_Inout_ SOCKET *sock, _In_ memory pool, _In_ void *data, _In_ ULONG len)
{
        PAGED_CODE();

        Mdl mdl(pool, data, len);
        if (auto err = mdl.prepare(IoReadAccess)) {
                return err;
        }

        WSK_BUF buf{ mdl.get(), 0, len };
        return send(sock, &buf, WSK_FLAG_NODELAY);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::recv(_Inout_ SOCKET *sock, _In_ memory pool, _Out_ void *data, _In_ ULONG len)
{
        PAGED_CODE();

        Mdl mdl(pool, data, len);
        if (auto err = mdl.prepare(IoWriteAccess)) {
                return err;
        }

        WSK_BUF buf{ mdl.get(), 0, len };
        return receive(sock, &buf);
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE err_t usbip::recv_op_common(_Inout_ SOCKET *sock, _In_ UINT16 expected_code, _Out_ op_status_t &status)
{
        PAGED_CODE();
        op_common r;

        if (auto err = recv(sock, memory::stack, &r, sizeof(r))) {
                Trace(TRACE_LEVEL_ERROR, "Receive %!STATUS!", err);
                return ERR_NETWORK;
        }

	PACK_OP_COMMON(0, &r);

	if (r.version != USBIP_VERSION) {
		Trace(TRACE_LEVEL_ERROR, "Version(%#x) != expected(%#x)", r.version, USBIP_VERSION);
		return ERR_VERSION;
	}

        if (r.code != expected_code) {
                Trace(TRACE_LEVEL_ERROR, "Code(%#x) != expected(%#x)", r.code, expected_code);
                return ERR_PROTOCOL;
        }

        status = static_cast<op_status_t>(r.status);
        return ERR_NONE;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS usbip::send_cmd(_Inout_ SOCKET *sock, _Inout_ usbip_header &hdr, _Inout_opt_ URB *transfer_buffer)
{
        PAGED_CODE();

        usbip::Mdl mdl_hdr(memory::stack, &hdr, sizeof(hdr));

        if (auto err = mdl_hdr.prepare_paged(IoReadAccess)) {
                Trace(TRACE_LEVEL_ERROR, "prepare_paged %!STATUS!", err);
                return err;
        }

        usbip::Mdl mdl_buf;

        if (transfer_buffer && is_transfer_direction_out(hdr)) { // TransferFlags can have wrong direction
                if (auto err = make_transfer_buffer_mdl(mdl_buf, IoReadAccess, *transfer_buffer)) {
                        Trace(TRACE_LEVEL_ERROR, "make_transfer_buffer_mdl %!STATUS!", err);
                        return err;
                }
                mdl_hdr.next(mdl_buf);
        }

        auto buf = make_wsk_buf(mdl_hdr, hdr);

        {
                char str[DBG_USBIP_HDR_BUFSZ];
                TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "OUT %Iu%s", buf.Length, dbg_usbip_hdr(str, sizeof(str), &hdr, true));
        }

        byteswap_header(hdr, swap_dir::host2net);

        if (auto err = send(sock, &buf, WSK_FLAG_NODELAY)) {
                Trace(TRACE_LEVEL_ERROR, "Send %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}

/*
 * URB must have TransferBuffer* members.
 * TransferBuffer && TransferBufferMDL can be both not NULL for bulk/int at least.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::make_transfer_buffer_mdl(_Out_ Mdl &mdl, _In_ LOCK_OPERATION Operation, _In_ const URB &urb)
{
        auto err = STATUS_SUCCESS;
        auto &r = AsUrbTransfer(urb);

        if (!r.TransferBufferLength) {
                NT_ASSERT(!mdl);
        } else if (auto m = r.TransferBufferMDL) {
                mdl = Mdl(m);
                err = mdl.size() >= r.TransferBufferLength ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
        } else if (auto buf = r.TransferBuffer) {
                mdl = Mdl(memory::paged, buf, r.TransferBufferLength); // unknown it is paged or not
                err = mdl.prepare_paged(Operation);
        } else {
                Trace(TRACE_LEVEL_ERROR, "TransferBuffer and TransferBufferMDL are NULL");
                err = STATUS_INVALID_PARAMETER;
        }

        if (err) {
                mdl.reset();
        }

        return err;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS usbip::assign(_Inout_ ULONG &TransferBufferLength, _In_ int actual_length)
{
        if (actual_length >= 0 && ULONG(actual_length) <= TransferBufferLength) {
                TransferBufferLength = actual_length;
                return STATUS_SUCCESS;
        }

        return STATUS_INVALID_BUFFER_SIZE;
}
