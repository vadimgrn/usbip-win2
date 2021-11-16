#include "dbgcommon.h"
#include "strutil.h"
#include "usbip_proto.h"

const char *dbg_usbip_hdr(const struct usbip_header *hdr)
{
	static char buf[512];

	int n = libdrv_snprintf(buf, sizeof(buf), "seq:%u,%s,ep:%u,cmd:%x", 
				hdr->base.seqnum, 
				hdr->base.direction ? "in" : "out", 
				hdr->base.ep, 
				hdr->base.command);

	int rest = sizeof(buf) - n;

	switch (hdr->base.command) {
	case USBIP_CMD_SUBMIT:
		libdrv_snprintf(buf + n, rest, "(submit),tlen:%d,intv:%d",
			hdr->u.cmd_submit.transfer_buffer_length, hdr->u.cmd_submit.interval);
		break;
	case USBIP_RET_SUBMIT:
		libdrv_snprintf(buf + n, rest, "(ret_submit),alen:%u", hdr->u.ret_submit.actual_length);
		break;
	case USBIP_CMD_UNLINK:
		libdrv_snprintf(buf + n, rest, "(unlink),unlinkseq:%u", hdr->u.cmd_unlink.seqnum);
		break;
	case USBIP_RET_UNLINK:
		libdrv_snprintf(buf + n, rest, "(ret_unlink),st:%u", hdr->u.ret_unlink.status);
		break;
	case USBIP_RESET_DEV:
		libdrv_snprintf(buf + n, rest, "(reset_dev)");
		break;
	}

	return buf;
}