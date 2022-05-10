#include "usbip_network.h"
#include "trace.h"
#include "usbip_network.tmh"

#include "usbip_proto_op.h"

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

err_t usbip::recv_op_common(SOCKET *sock, UINT16 expected_code, op_status_t &status)
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
