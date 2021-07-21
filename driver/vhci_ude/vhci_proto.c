#include "vhci_proto.h"
#include "vhci_driver.h"
#include "usbip_proto.h"
#include "vhci_urbr.h"
#include "usbd_helper.h"

void
set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
			    bool dir_in, pctx_ep_t ep, ULONG TransferFlags, ULONG TransferBufferLength)
{
	h->base.command = USBIP_CMD_SUBMIT;
	h->base.seqnum = seqnum;
	h->base.devid = devid;
	h->base.direction = dir_in ? USBIP_DIR_IN : USBIP_DIR_OUT;
	h->base.ep = ep ? ep->addr & 0x7f : 0;

	h->u.cmd_submit.transfer_flags = to_linux_flags(TransferFlags);
	h->u.cmd_submit.transfer_buffer_length = TransferBufferLength;
	h->u.cmd_submit.start_frame = 0;
	h->u.cmd_submit.number_of_packets = 0;
	h->u.cmd_submit.interval = ep ? ep->interval: 0;
}

void
set_cmd_unlink_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid, unsigned long seqnum_unlink)
{
	h->base.command = USBIP_CMD_UNLINK;
	h->base.seqnum = seqnum;
	h->base.devid = devid;
	h->base.direction = USBIP_DIR_OUT;
	h->base.ep = 0;
	h->u.cmd_unlink.seqnum = seqnum_unlink;
}