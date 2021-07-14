#include "vhci_proto.h"
#include "vhci_driver.h"
#include "usbip_proto.h"
#include "vhci_urbr.h"

/*
 * <uapi/linux/usbdevice_fs.h>
 */
enum { // bits
	USBDEVFS_URB_SHORT_NOT_OK = 0x01,
	USBDEVFS_URB_ISO_ASAP = 0x02
};

static UINT32
to_usbdevfs_flags(ULONG TransferFlags)
{
	UINT32 flags = 0;

	if (TransferFlags & USBD_START_ISO_TRANSFER_ASAP) {
		flags |= USBDEVFS_URB_ISO_ASAP;
	} else if (IsTransferDirectionIn(TransferFlags) && !(TransferFlags & USBD_SHORT_TRANSFER_OK)) {
		flags |= USBDEVFS_URB_SHORT_NOT_OK;
	}

	return flags;
}

void
set_cmd_submit_usbip_header(struct usbip_header *h, unsigned long seqnum, unsigned int devid,
			    bool dir_in, pctx_ep_t ep, ULONG TransferFlags, unsigned int len)
{
	h->base.command = USBIP_CMD_SUBMIT;
	h->base.seqnum = seqnum;
	h->base.devid = devid;
	h->base.direction = dir_in ? USBIP_DIR_IN : USBIP_DIR_OUT;
	h->base.ep = ep ? (ep->addr & 0x7f): 0;
	h->u.cmd_submit.transfer_flags = to_usbdevfs_flags(TransferFlags);
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