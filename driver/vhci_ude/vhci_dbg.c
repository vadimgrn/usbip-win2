#include "vhci_dbg.h"
#include "dbgcommon.h"

#include <ntstrsafe.h>
#include <usbdi.h>
#include <usbspec.h>

#include "strutil.h"
#include "usbip_vhci_api.h"

/*
 * WPP call requires both a debug message buffer and the length at the same time.
 * Thus, WPP macros reference global variables, which are manipluated via dbg_xxxx().
 */
enum { NAMECODE_BUF_MAX = 128 };

char buf_dbg_urbr[NAMECODE_BUF_MAX];
unsigned int len_dbg_urbr;

const char *dbg_usb_setup_packet(char *buf, unsigned int len, const void *packet)
{
	const USB_DEFAULT_PIPE_SETUP_PACKET *pkt = packet;
	
	NTSTATUS st = RtlStringCbPrintfA(buf, len, 
			"rqtype:%02x,req:%02x,wIndex:%hu,wLength:%hu,wValue:%hu",
			pkt->bmRequestType, pkt->bRequest, pkt->wIndex, pkt->wLength, pkt->wValue);

	return st == STATUS_SUCCESS ? buf : "dbg_usb_setup_packet error";
}

const char *dbg_urbr(const urb_req_t *urbr)
{
	if (!urbr) {
		len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, sizeof(buf_dbg_urbr), "[null]");
	} else {
		switch (urbr->type) {
		case URBR_TYPE_URB:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, sizeof(buf_dbg_urbr), "[urb,seq:%u,!%urb_function%]", urbr->seq_num, urbr->u.urb.urb->UrbHeader.Function);
			break;
		case URBR_TYPE_UNLINK:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, sizeof(buf_dbg_urbr), "[ulk,seq:%u,%u]", urbr->seq_num, urbr->u.seq_num_unlink);
			break;
		case URBR_TYPE_SELECT_CONF:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, sizeof(buf_dbg_urbr), "[slc,seq:%u,%hhu]", urbr->seq_num, urbr->u.conf_value);
			break;
		case URBR_TYPE_SELECT_INTF:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, sizeof(buf_dbg_urbr), "[sli,seq:%u,%hhu,%hhu]", urbr->seq_num, urbr->u.intf.intf_num, urbr->u.intf.alt_setting);
			break;
		case URBR_TYPE_RESET_PIPE:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, sizeof(buf_dbg_urbr), "[rst,seq:%u,%hhu]", urbr->seq_num, urbr->ep->addr);
			break;
		default:
			len_dbg_urbr = libdrv_snprintf(buf_dbg_urbr, sizeof(buf_dbg_urbr), "[unk:seq:%u]", urbr->seq_num);			break;
		}
	}
	
	++len_dbg_urbr;
	return buf_dbg_urbr;
}
