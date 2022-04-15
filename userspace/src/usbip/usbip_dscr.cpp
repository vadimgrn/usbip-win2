#include "usbip_dscr.h"
#include "usbip_network.h"

namespace
{

int fetch_descriptor(SOCKET sockfd, seqnum_t &seqnum, UINT8 dscr_type, UINT8 dscr_idx, unsigned int devid, void *dscr, USHORT dscr_size)
{
        usbip_header hdr{};

        hdr.base.command = htonl(USBIP_CMD_SUBMIT);
        hdr.base.seqnum = ++seqnum;
        hdr.base.direction = htonl(USBIP_DIR_IN);
        hdr.base.devid = htonl(devid);

        auto &r = *reinterpret_cast<USB_DEFAULT_PIPE_SETUP_PACKET*>(hdr.u.cmd_submit.setup);
        static_assert(sizeof(r) == sizeof(hdr.u.cmd_submit.setup));

        r.bmRequestType.s.Dir = BMREQUEST_DEVICE_TO_HOST;
        r.bmRequestType.s.Type = BMREQUEST_STANDARD;
        r.bmRequestType.s.Recipient = BMREQUEST_TO_DEVICE;

        r.bRequest = USB_REQUEST_GET_DESCRIPTOR;
        r.wValue.W = USB_DESCRIPTOR_MAKE_TYPE_AND_INDEX(dscr_type, dscr_idx);
        r.wIndex.W = 0; // Language ID, relevant for USB_STRING_DESCRIPTOR_TYPE only
        r.wLength = dscr_size;

        hdr.u.cmd_submit.transfer_buffer_length = htonl(dscr_size);

        if (usbip_net_send(sockfd, &hdr, sizeof(hdr)) < 0) {
                dbg("%s: failed to send usbip header\n", __func__);
                return -1;
        }
        if (usbip_net_recv(sockfd, &hdr, sizeof(hdr)) < 0) {
                dbg("%s: failed to recv usbip header\n", __func__);
                return -1;
        }
        if (auto st = hdr.u.ret_submit.status) {
                dbg("%s: command submit error: %d\n", __func__, st);
                return -1;
        }

        auto alen = ntohl(hdr.u.ret_submit.actual_length);
        if (alen != dscr_size) {
                err("%s: actual length %d != %d\n", __func__, alen, dscr_size);
                return -1;
        }

        if (usbip_net_recv(sockfd, dscr, dscr_size) < 0) {
                err("%s: failed to recv usbip payload\n", __func__);
                return -1;
        }

        return 0;
}

} // namespace


int fetch_device_descriptor(SOCKET sockfd, seqnum_t &seqnum, unsigned int devid, USB_DEVICE_DESCRIPTOR &dd)
{
	return fetch_descriptor(sockfd, seqnum, USB_DEVICE_DESCRIPTOR_TYPE, 0, devid, &dd, sizeof(dd));
}

int fetch_conf_descriptor(SOCKET sockfd, seqnum_t &seqnum, unsigned int devid, USB_CONFIGURATION_DESCRIPTOR *cfgd, USHORT &wTotalLength)
{
        const UINT8 dscr_idx = -1;

        if (!cfgd) {
	        USB_CONFIGURATION_DESCRIPTOR hdr{};
	        if (fetch_descriptor(sockfd, seqnum, USB_CONFIGURATION_DESCRIPTOR_TYPE, dscr_idx, devid, &hdr, sizeof(hdr)) < 0) {
		        return -1;
	        }
                wTotalLength = hdr.wTotalLength;
                assert(wTotalLength > sizeof(hdr));
                return 0;
	}

	if (wTotalLength <= sizeof(*cfgd)) {
		err("%s: too small descriptor buffer\n", __func__);
		return -1;
	}

	return fetch_descriptor(sockfd, seqnum, USB_CONFIGURATION_DESCRIPTOR_TYPE, dscr_idx, devid, cfgd, wTotalLength);
}
