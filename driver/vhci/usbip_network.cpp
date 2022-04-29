#include "usbip_network.h"
#include <wdm.h>
#include "trace.h"
#include "usbip_network.tmh"

#include "usbip_proto_op.h"
#include "wsk_utils.h"
#include "mdl_cpp.h"

bool usbip_net_send(wsk::SOCKET *sock, void *data, ULONG len)
{
        usbip::Mdl mdl(usbip::memory_pool::stack, data, len);
        if (!mdl.prepare(IoReadAccess)) {
                return false;
        }

        WSK_BUF buf{ mdl.get(), 0, mdl.size() };

        SIZE_T actual = 0;
        auto err = send(sock, &buf, WSK_FLAG_NODELAY, actual);

        mdl.unprepare();
        return !err && actual == len;
}

bool usbip_net_recv(wsk::SOCKET *sock, void *data, ULONG len)
{
        usbip::Mdl mdl(usbip::memory_pool::stack, data, len);
        if (!mdl.prepare(IoWriteAccess)) {
                return false;
        }

        WSK_BUF buf{ mdl.get(), 0, mdl.size() };

        SIZE_T actual = 0;
        auto err = receive(sock, &buf, WSK_FLAG_WAITALL, actual);

        mdl.unprepare();
        return !err && actual == len;
}

bool usbip_net_recv_op_common(wsk::SOCKET *sock, UINT16 response_code, UINT32 *status)
{
        op_common r;

        if (!usbip_net_recv(sock, &r, sizeof(r))) {
                return false;
        }

	PACK_OP_COMMON(0, &r);

	if (r.version != USBIP_VERSION) {
		Trace(TRACE_LEVEL_ERROR, "Version %#x != %#x", r.version, USBIP_VERSION);
		return false;
	}

        if (r.code != response_code) {
                Trace(TRACE_LEVEL_ERROR, "Code %#x != %#x", r.code, response_code);
                return false;
        }

        if (status) {
                *status = r.status;
        }

        if (r.status != ST_OK) {
                Trace(TRACE_LEVEL_ERROR, "Request failed: status %d", r.status);
                return false;
	}

        return true;
}
