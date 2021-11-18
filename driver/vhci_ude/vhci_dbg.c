#include "vhci_dbg.h"

#include <ntstrsafe.h>

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
