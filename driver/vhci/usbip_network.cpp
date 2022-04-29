#include "usbip_network.h"
#include "trace.h"
#include "usbip_network.tmh"

#include "usbip_proto_op.h"
#include "mdl_cpp.h"

bool usbip_net_send_op_common(wsk::SOCKET *sock, UINT16 code, UINT32 status)
{
        op_common r{ USBIP_VERSION, code, status };
	PACK_OP_COMMON(1, &r);

        usbip::Mdl mdl(&r, sizeof(r));
        if (!mdl.lock()) {
                return false;
        }

        WSK_BUF buf{ mdl.get(), mdl.offset(), mdl.size() };

        SIZE_T actual = 0;
        auto err = send(sock, &buf, WSK_FLAG_NODELAY, actual);

        mdl.unlock();
        return !err && actual == buf.Length ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

bool usbip_net_recv_op_common(wsk::SOCKET *sock, UINT16 &code, UINT32 &status)
{
        op_common r{};

        usbip::Mdl mdl(&r, sizeof(r));
        if (!mdl.lock()) {
                return false;
        }

        WSK_BUF buf{ mdl.get(), mdl.offset(), mdl.size() };

        SIZE_T actual = 0;
        auto err = receive(sock, &buf, WSK_FLAG_WAITALL, actual);

        mdl.unlock();
        if (err || actual != buf.Length) {
                return false;
        }

	PACK_OP_COMMON(0, &r);

	if (r.version != USBIP_VERSION) {
		Trace(TRACE_LEVEL_ERROR, "Version mismatch: %#x != %#x", r.version, USBIP_VERSION);
		return false;
	}

	if (r.code != code) {
                Trace(TRACE_LEVEL_ERROR, "Unexpected pdu %#0x for %#0x", r.code, code);
		return false;
	}

        code = r.code;
        status = r.status;

	if (r.status != ST_OK) {
                Trace(TRACE_LEVEL_ERROR, "Request failed: status: %d", r.status);
		return false;
	}

	return true;
}
