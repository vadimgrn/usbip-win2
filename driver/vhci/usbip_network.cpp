#include "usbip_network.h"
#include "trace.h"
#include "usbip_network.tmh"

#include "usbip_proto_op.h"

namespace
{

/*
 * @see usbip_attach.cpp, attach_device
 */
auto map_error(op_status_t st)
{
        switch (st) {
        case ST_OK:
                return ERR_NONE;
        case ST_DEV_BUSY:
                return ERR_EXIST;
        case ST_NODEV:
                return ERR_NOTEXIST;
        case ST_ERROR:
                return ERR_STATUS;
        case ST_NA:
                return ERR_INVARG;
        default:
                return ERR_GENERAL;
        }
}

} // namespace

bool usbip::send(SOCKET *sock, memory pool, void *data, ULONG len)
{
        Mdl mdl(pool, data, len);
        if (mdl.prepare(IoReadAccess)) {
                return false;
        }

        WSK_BUF buf{ mdl.get(), 0, len };

        SIZE_T actual = 0;
        auto err = send(sock, &buf, WSK_FLAG_NODELAY, actual);

        mdl.unprepare();
        return !err && actual == len;
}

bool usbip::recv(SOCKET *sock, memory pool, void *data, ULONG len)
{
        Mdl mdl(pool, data, len);
        if (mdl.prepare(IoWriteAccess)) {
                return false;
        }

        WSK_BUF buf{ mdl.get(), 0, len };

        SIZE_T actual = 0;
        auto err = receive(sock, &buf, WSK_FLAG_WAITALL, actual);

        mdl.unprepare();
        return !err && actual == len;
}

err_t usbip::recv_op_common(SOCKET *sock, UINT16 expected_code)
{
        op_common r;

        if (!recv(sock, memory::stack, &r, sizeof(r))) {
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

        auto st = static_cast<op_status_t>(r.status);
        if (st != ST_OK) {
                Trace(TRACE_LEVEL_ERROR, "Request failed: %!op_status_t!", st);
        }

        return map_error(st);
}
