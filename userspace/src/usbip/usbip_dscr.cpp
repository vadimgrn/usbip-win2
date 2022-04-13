#include "usbip_dscr.h"
#include "usbip_proto.h"
#include "usbip_network.h"

#include <usbspec.h>

/* sufficient large enough seq used to avoid conflict with normal vhci operation */
static unsigned	seqnum = 0x7ffffff;

static int fetch_descriptor(SOCKET sockfd, UINT8 dscr_type, unsigned int devid, void *dscr, USHORT dscr_size)
{
	struct usbip_header uhdr = {0};

	uhdr.base.command = htonl(USBIP_CMD_SUBMIT);
	uhdr.base.seqnum = seqnum++;
	uhdr.base.direction = htonl(USBIP_DIR_IN);
	uhdr.base.devid = htonl(devid);

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = (USB_DEFAULT_PIPE_SETUP_PACKET*)uhdr.u.cmd_submit.setup; // get_submit_setup(&uhdr);
	static_assert(sizeof(*setup) == sizeof(uhdr.u.cmd_submit.setup), "assert");

	setup->bmRequestType.s.Dir = BMREQUEST_DEVICE_TO_HOST;
	setup->bmRequestType.s.Type = BMREQUEST_STANDARD;
	setup->bmRequestType.s.Recipient = BMREQUEST_TO_DEVICE;

	setup->bRequest = USB_REQUEST_GET_DESCRIPTOR;
	setup->wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(dscr_type, 0); // FIXME: index
	setup->wIndex.W = 0; // zero or Language ID
	setup->wLength = dscr_size;

	uhdr.u.cmd_submit.transfer_buffer_length = htonl(dscr_size);

	if (usbip_net_send(sockfd, &uhdr, sizeof(uhdr)) < 0) {
		dbg("fetch_descriptor: failed to send usbip header\n");
		return -1;
	}
	if (usbip_net_recv(sockfd, &uhdr, sizeof(uhdr)) < 0) {
		dbg("fetch_descriptor: failed to recv usbip header\n");
		return -1;
	}
	if (uhdr.u.ret_submit.status != 0) {
		dbg("fetch_descriptor: command submit error: %d\n", uhdr.u.ret_submit.status);
		return -1;
	}

	INT32 alen = ntohl(uhdr.u.ret_submit.actual_length);
	if (alen < dscr_size) {
		err("fetch_descriptor: too short response: actual length: %d\n", alen);
		return -1;
	}

	if (usbip_net_recv(sockfd, dscr, alen) < 0) {
		err("fetch_descriptor: failed to recv usbip payload\n");
		return -1;
	}
	
	return 0;
}

int fetch_device_descriptor(SOCKET sockfd, unsigned int devid, USB_DEVICE_DESCRIPTOR *dd)
{
	return fetch_descriptor(sockfd, USB_DEVICE_DESCRIPTOR_TYPE, devid, dd, sizeof(*dd));
}

int fetch_conf_descriptor(SOCKET sockfd, unsigned int devid, USB_CONFIGURATION_DESCRIPTOR *cfgd, USHORT *wTotalLength)
{
	USB_CONFIGURATION_DESCRIPTOR hdr = {0};
	if (fetch_descriptor(sockfd, USB_CONFIGURATION_DESCRIPTOR_TYPE, devid, &hdr, sizeof(hdr)) < 0) {
		return -1;
	}

	if (!cfgd) {
		*wTotalLength = hdr.wTotalLength;
		return 0;
	}

	if (*wTotalLength < hdr.wTotalLength) {
		err("fetch_conf_descriptor: too small descriptor buffer\n");
		return -1;
	}

	*wTotalLength = hdr.wTotalLength;
	return fetch_descriptor(sockfd, USB_CONFIGURATION_DESCRIPTOR_TYPE, devid, cfgd, *wTotalLength);
}
