#include "usbip_network.h"
#include "trace.h"
#include "usbip_network.tmh"

#include "dev.h"
#include "pdu.h"
#include "dbgcommon.h"
#include "urbtransfer.h"
#include "usbip_proto.h"
#include "usbip_proto_op.h"
#include "usbd_helper.h"

namespace
{

auto assign(ULONG &TransferBufferLength, int actual_length)
{
        bool ok = actual_length >= 0 && (ULONG)actual_length <= TransferBufferLength;
        TransferBufferLength = ok ? actual_length : 0;

        return ok ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

UINT32 get_request(UINT32 response)
{
        switch (static_cast<usbip_request_type>(response)) {
        case USBIP_RET_SUBMIT:
                return USBIP_CMD_SUBMIT;
        case USBIP_RET_UNLINK:
                return USBIP_CMD_UNLINK;
        default:
                return 0;
        }
}

auto recv_ret_submit(_In_ usbip::SOCKET *sock, _Inout_ URB &urb, _Inout_ usbip_header &hdr, _Inout_ usbip::Mdl &mdl_buf)
{
        auto &base = hdr.base;
        NT_ASSERT(base.command == USBIP_RET_SUBMIT);

        auto tr = AsUrbTransfer(&urb);

        {
                auto &ret = hdr.u.ret_submit;
                urb.UrbHeader.Status = ret.status ? to_windows_status(ret.status) : USBD_STATUS_SUCCESS;

                auto err = assign(tr->TransferBufferLength, ret.actual_length);
                if (err || base.direction == USBIP_DIR_OUT || !tr->TransferBufferLength) { 
                        return err;
                }
        }

        NT_ASSERT(mdl_buf.size() >= tr->TransferBufferLength);
        WSK_BUF buf{ mdl_buf.get(), 0, tr->TransferBufferLength };

        if (auto err = receive(sock, &buf)) {
                Trace(TRACE_LEVEL_ERROR, "Receive buffer[%Iu] %!STATUS!", buf.Length, err);
                return err;
        }

        TraceUrb("[%Iu]%!BIN!", buf.Length, WppBinary(mdl_buf.sysaddr(LowPagePriority), static_cast<USHORT>(buf.Length)));
        return STATUS_SUCCESS;
}

/*
 * URB must have TransferBuffer* members.
 */
auto make_transfer_buffer_mdl(_Out_ usbip::Mdl &mdl, _In_ const URB &urb)
{
        auto func = urb.UrbHeader.Function;
        bool use_mdl = func == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL ||
                       func == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL;

        auto err = STATUS_SUCCESS;
        auto r = AsUrbTransfer(&urb);

        if (!r->TransferBufferLength) {
                NT_ASSERT(!mdl);
        } else if (auto buf = use_mdl ? nullptr : r->TransferBuffer) {
                mdl = usbip::Mdl(usbip::memory::nonpaged, buf, r->TransferBufferLength);
                err = mdl.prepare_nonpaged();
        } else if (auto m = r->TransferBufferMDL) {
                mdl = usbip::Mdl(m);
                err = mdl.size() == r->TransferBufferLength ? STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;
        } else {
                Trace(TRACE_LEVEL_ERROR, "TransferBuffer and TransferBufferMDL are NULL");
                err = STATUS_INVALID_PARAMETER;
        }

        if (err) {
                mdl.reset();
        }

        return err;
}

} // namespace


NTSTATUS usbip::send(SOCKET *sock, memory pool, void *data, ULONG len)
{
        Mdl mdl(pool, data, len);
        if (auto err = mdl.prepare(IoReadAccess)) {
                return err;
        }

        WSK_BUF buf{ mdl.get(), 0, len };
        return send(sock, &buf, WSK_FLAG_NODELAY);
}

NTSTATUS usbip::recv(SOCKET *sock, memory pool, void *data, ULONG len)
{
        Mdl mdl(pool, data, len);
        if (auto err = mdl.prepare(IoWriteAccess)) {
                return err;
        }

        WSK_BUF buf{ mdl.get(), 0, len };
        return receive(sock, &buf);
}

err_t usbip::recv_op_common(_In_ SOCKET *sock, _In_ UINT16 expected_code, _Out_ op_status_t &status)
{
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

NTSTATUS usbip::send_cmd(_In_ SOCKET *sock, _Inout_ usbip_header &hdr, _Inout_opt_ const URB *transfer_buffer)
{
        usbip::Mdl mdl_hdr(memory::stack, &hdr, sizeof(hdr));

        if (auto err = mdl_hdr.prepare(IoReadAccess)) {
                Trace(TRACE_LEVEL_ERROR, "Prepare usbip_header %!STATUS!", err);
                return err;
        }

        usbip::Mdl mdl_buf;

        if (transfer_buffer && is_transfer_direction_out(&hdr)) { // TransferFlags can have wrong direction
                if (auto err = make_transfer_buffer_mdl(mdl_buf, *transfer_buffer)) {
                        Trace(TRACE_LEVEL_ERROR, "make_buffer_mdl %!STATUS!", err);
                        return err;
                }
                mdl_hdr.next(mdl_buf);
        }

        WSK_BUF buf{ mdl_hdr.get(), 0, size(mdl_hdr) };

        {
                auto pdu_sz = get_total_size(hdr);
                NT_ASSERT(buf.Length == sizeof(hdr) || buf.Length == pdu_sz);

                char str[DBG_USBIP_HDR_BUFSZ];
                TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "OUT %Iu%s", pdu_sz, dbg_usbip_hdr(str, sizeof(str), &hdr));
        }

        byteswap_header(hdr, swap_dir::host2net);

        if (auto err = send(sock, &buf, WSK_FLAG_NODELAY)) {
                Trace(TRACE_LEVEL_ERROR, "Send %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}
