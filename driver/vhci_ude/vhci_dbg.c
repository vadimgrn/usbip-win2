#include "vhci_dbg.h"
#include "dbgcommon.h"
#include "strutil.h"
#include "usbip_vhci_api.h"

#include <ntstrsafe.h>
#include <usbdi.h>
#include <usbspec.h>

const char *dbg_usb_setup_packet(char *buf, unsigned int len, const void *packet)
{
	const USB_DEFAULT_PIPE_SETUP_PACKET *pkt = packet;
	
	NTSTATUS st = RtlStringCbPrintfA(buf, len, 
			"[bmRequestType %#04x, bRequest %#04x, wValue %#06hx, wIndex %#06hx, wLength %hu]",
			pkt->bmRequestType, pkt->bRequest, pkt->wValue, pkt->wIndex, pkt->wLength);

	return st != STATUS_INVALID_PARAMETER ? buf : "dbg_usb_setup_packet: invalid parameter";
}

const char *dbg_urbr(char* buf, unsigned int len, const urb_req_t *urbr)
{
	if (!urbr) {
		return "[null]";
	}

	NTSTATUS st = STATUS_SUCCESS;
	
	switch (urbr->type) {
	case URBR_TYPE_URB:
		st = RtlStringCbPrintfA(buf, len, "[URB, seq_num %lu, cancelable %d]", urbr->seq_num, 
					urbr->u.urb.urb ? urbr->u.urb.cancelable : 0);
		break;
	case URBR_TYPE_UNLINK:
		st = RtlStringCbPrintfA(buf, len, "[UNLINK, seq_num %lu, seq_num_unlink %lu]", urbr->seq_num, urbr->u.seq_num_unlink);
		break;
	case URBR_TYPE_SELECT_CONF:
		st = RtlStringCbPrintfA(buf, len, "[SELECT_CONF, seq_num %lu, value %d]", urbr->seq_num, urbr->u.conf_value);
		break;
	case URBR_TYPE_SELECT_INTF:
		st = RtlStringCbPrintfA(buf, len, "[SELECT_INTF, seq_num %lu, ifnum %d, altnum %d]", urbr->seq_num, urbr->u.intf.intf_num, urbr->u.intf.alt_setting);
		break;
	case URBR_TYPE_RESET_PIPE:
		st = RtlStringCbPrintfA(buf, len, "[RESET_PIPE, seq_num %lu, bEndpointAddress %#04x]", urbr->seq_num, 
					urbr->ep ? urbr->ep->addr : 0);
		break;
	default:
		st = RtlStringCbPrintfA(buf, len, "[?, seq_num %lu]", urbr->seq_num);
	}
	
	return st != STATUS_INVALID_PARAMETER ? buf : "dbg_urbr: invalid parameter";
}
