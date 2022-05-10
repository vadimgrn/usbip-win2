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

void debug(const usbip_header &hdr, const usbip::Mdl &mdl_hdr, bool send)
{
        auto pdu_sz = get_total_size(hdr);

        [[maybe_unused]] auto buf_sz = size(mdl_hdr);
        NT_ASSERT(buf_sz == sizeof(hdr) || buf_sz == pdu_sz);

        char buf[DBG_USBIP_HDR_BUFSZ];
        TraceEvents(TRACE_LEVEL_VERBOSE, FLAG_USBIP, "%s %Iu%s", 
                send ? "OUT" : "IN", pdu_sz, dbg_usbip_hdr(buf, sizeof(buf), &hdr));
}

auto make_buffer_mdl(_Out_ usbip::Mdl &mdl, _In_ URB &urb)
{
        auto err = STATUS_INVALID_PARAMETER;
        auto r = AsUrbTransfer(&urb);

        if (!r->TransferBufferLength) {
                // should never happen
        } else if (auto buf = r->TransferBuffer) {
                mdl = usbip::Mdl(usbip::memory::nonpaged, buf, r->TransferBufferLength);
                err = mdl.prepare_nonpaged();
        } else if (auto m = r->TransferBufferMDL) {
                mdl = usbip::Mdl(m);
                err = mdl.size() == r->TransferBufferLength ? STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;
        }

        if (err) {
                mdl.reset();
        }

        return err;
}

auto send_cmd(_In_ wsk::SOCKET *sock, _Inout_ URB &urb, _Inout_ usbip_header &hdr, 
        _Out_ usbip::Mdl &mdl_hdr, _Out_ usbip::Mdl &mdl_buf)
{
        mdl_hdr = usbip::Mdl(usbip::memory::stack, &hdr, sizeof(hdr));

        if (auto err = mdl_hdr.prepare(IoModifyAccess)) {
                Trace(TRACE_LEVEL_ERROR, "Prepare usbip_header %!STATUS!", err);
                return err;
        }

        if (auto err = make_buffer_mdl(mdl_buf, urb)) {
                Trace(TRACE_LEVEL_ERROR, "make_buffer_mdl %!STATUS!", err);
                return err;
        }

        if (hdr.base.direction == USBIP_DIR_OUT) {
                mdl_hdr.next(mdl_buf);
        }

        debug(hdr, mdl_hdr, true);
        byteswap_header(hdr, swap_dir::host2net);

        WSK_BUF buf{ mdl_hdr.get(), 0, size(mdl_hdr) };

        if (auto err = send(sock, &buf, WSK_FLAG_NODELAY)) {
                Trace(TRACE_LEVEL_ERROR, "Send %!STATUS!", err);
                return err;
        }

        return STATUS_SUCCESS;
}

auto recv_ret_submit_buffer(_In_ usbip::SOCKET *sock, _Inout_ _URB &urb, _Inout_ usbip_header &hdr, 
        _Inout_ usbip::Mdl &mdl_buf)
{
        auto &base = hdr.base;
        NT_ASSERT(base.command == USBIP_RET_SUBMIT);

        auto tr = AsUrbTransfer(&urb);

        {
                auto &ret = hdr.u.ret_submit;
                urb.UrbHeader.Status = ret.status ? to_windows_status(ret.status) : USBD_STATUS_SUCCESS;

                auto err = assign(tr->TransferBufferLength, ret.actual_length);
                if (err || base.direction == USBIP_DIR_OUT) { 
                        return err;
                }
        }

        NT_ASSERT(mdl_buf.size() >= tr->TransferBufferLength);
        WSK_BUF buf{ mdl_buf.get(), 0, tr->TransferBufferLength };

        if (auto err = receive(sock, &buf, WSK_FLAG_WAITALL)) {
                Trace(TRACE_LEVEL_ERROR, "Receive buffer[%Iu] %!STATUS!", buf.Length, err);
                return err;
        }

        TraceUrb("[%Iu]%!BIN!", buf.Length, WppBinary(mdl_buf.sysaddr(LowPagePriority), static_cast<USHORT>(buf.Length)));
        return STATUS_SUCCESS;
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
        return receive(sock, &buf, WSK_FLAG_WAITALL);
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

NTSTATUS usbip::send_recv_cmd(_In_ SOCKET *sock, _Inout_ _URB &urb, _Inout_ usbip_header &hdr)
{
        auto &base = hdr.base;

        auto command = base.command;
        auto seqnum = base.seqnum;
        auto direction = base.direction;

        usbip::Mdl mdl_hdr;
        usbip::Mdl mdl_buf;

        if (auto err = send_cmd(sock, urb, hdr, mdl_hdr, mdl_buf)) {
                return err;
        }

        mdl_hdr.next(nullptr);
        WSK_BUF buf{ mdl_hdr.get(), 0, mdl_hdr.size() };

        if (auto err = receive(sock, &buf, WSK_FLAG_WAITALL)) {
                Trace(TRACE_LEVEL_ERROR, "Receive usbip_header %!STATUS!", err);
                return err;
        }

        byteswap_header(hdr, swap_dir::net2host);

        base.direction = direction; // restore, always zero in server response
        debug(hdr, mdl_hdr, false);

        if (!(base.seqnum == seqnum && get_request(base.command) == command)) {
                Trace(TRACE_LEVEL_ERROR, "Unexpected command or seqnum");
                return STATUS_UNSUCCESSFUL;
        }

        return base.command == USBIP_RET_SUBMIT ? recv_ret_submit_buffer(sock, urb, hdr, mdl_buf) : STATUS_SUCCESS;
}
