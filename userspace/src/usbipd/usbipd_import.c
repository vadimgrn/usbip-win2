#include "usbipd.h"

#include "usbip_network.h"
#include "usbipd_stub.h"
#include "usbip_setupdi.h"
#include "start_xfer.h"

static int
export_device(devno_t devno, SOCKET sockfd)
{
	HANDLE hdev = open_stub_dev(devno);
	if (hdev == INVALID_HANDLE_VALUE) {
		dbg("cannot open devno: %hhu", devno);
		return ERR_NOTEXIST;
	}

	int err = start_xfer(hdev, sockfd);
	CloseHandle(hdev);

	if (err) {
		dbg("failed to lauch xfer process: error: %lx", GetLastError());
		return ERR_GENERAL;
	}

	return 0;
}

int
recv_request_import(SOCKET sockfd)
{
	struct op_import_request req;
	struct usbip_usb_device	udev;
	devno_t	devno;
	int rc;

	memset(&req, 0, sizeof(req));

	rc = usbip_net_recv(sockfd, &req, sizeof(req));
	if (rc < 0) {
		dbg("usbip_net_recv failed: import request");
		return -1;
	}
	PACK_OP_IMPORT_REQUEST(0, &req);

	devno = get_devno_from_busid(req.busid);
	if (devno == 0) {
		dbg("invalid bus id: %s", req.busid);
		usbip_net_send_op_common(sockfd, OP_REP_IMPORT, ST_NODEV);
		return -1;
	}

	usbip_net_set_keepalive(sockfd);

	/* should set TCP_NODELAY for usbip */
	usbip_net_set_nodelay(sockfd);

	/* export device needs a TCP/IP socket descriptor */
	rc = export_device(devno, sockfd);
	if (rc < 0) {
		dbg("failed to export device: %s, err:%d", req.busid, rc);
		usbip_net_send_op_common(sockfd, OP_REP_IMPORT, ST_NA);
		return -1;
	}

	rc = usbip_net_send_op_common(sockfd, OP_REP_IMPORT, ST_OK);
	if (rc < 0) {
		dbg("usbip_net_send_op_common failed: %#0x", OP_REP_IMPORT);
		return -1;
	}

	build_udev(devno, &udev);
	usbip_net_pack_usb_device(1, &udev);

	rc = usbip_net_send(sockfd, &udev, sizeof(udev));
	if (rc < 0) {
		dbg("usbip_net_send failed: devinfo");
		return -1;
	}

	dbg("import request busid %s: complete", req.busid);

	return 0;
}