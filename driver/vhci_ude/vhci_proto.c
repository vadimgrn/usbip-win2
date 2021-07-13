#include "vhci_proto.h"
#include "vhci_driver.h"
#include "usbip_proto.h"
#include "vhci_urbr.h"

#define USBDEVFS_URB_SHORT_NOT_OK	0x01
#define USBDEVFS_URB_ISO_ASAP		0x02
#define USBDEVFS_URB_NO_FSBR		0x20
#define USBDEVFS_URB_ZERO_PACKET	0x40
#define USBDEVFS_URB_NO_INTERRUPT	0x80

unsigned int
transflag(unsigned int flags)
{
	unsigned int linux_flags = 0;
	if (!(flags&USBD_SHORT_TRANSFER_OK))
		linux_flags |= USBDEVFS_URB_SHORT_NOT_OK;
	if (flags&USBD_START_ISO_TRANSFER_ASAP)
		linux_flags |= USBDEVFS_URB_ISO_ASAP;
	return linux_flags;
}

void
set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
			    unsigned int direct, pctx_ep_t ep, unsigned int flags, unsigned int len)
{
	h->base.command = USBIP_CMD_SUBMIT;
	h->base.seqnum = seqnum;
	h->base.devid = devid;
	h->base.direction = direct ? USBIP_DIR_IN : USBIP_DIR_OUT;
	h->base.ep = ep ? (ep->addr & 0x7f): 0;
	h->u.cmd_submit.transfer_flags = transflag(flags);
	h->u.cmd_submit.transfer_buffer_length = len;
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