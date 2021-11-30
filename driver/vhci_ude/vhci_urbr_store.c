#include "vhci_urbr_store.h"
#include "vhci_driver.h"
#include "vhci_urbr_store.tmh"

#include <usbip_proto.h>
#include "vhci_urbr.h"
#include "vhci_proto.h"

#include "vhci_urbr_store_control.h"
#include "vhci_urbr_store_vendor.h"
#include "vhci_urbr_store_bulk.h"
#include "vhci_urbr_store_iso.h"
#include "vhci_urbr_store_status.h"
#include "vhci_urbr_store_dscr.h"
#include "vhci_urbr_store_select.h"
#include "vhci_urbr_store_reset.h"

NTSTATUS
store_urbr_partial(WDFREQUEST req_read, purb_req_t urbr)
{
	PURB	urb;
	USHORT		urbfunc;
	WDF_REQUEST_PARAMETERS	params;
	NTSTATUS	status;

	char buf[DBG_URBR_BUFSZ];
	TraceInfo(TRACE_READ, "Enter: urbr: %s", dbg_urbr(buf, sizeof(buf), urbr));

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(urbr->req, &params);

	urb = (PURB)params.Parameters.Others.Arg1;
	urbfunc = urb->UrbHeader.Function;

	switch (urbfunc) {
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = store_urbr_control_transfer_partial(req_read, urbr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = store_urbr_control_transfer_ex_partial(req_read, urbr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urbr_vendor_class_partial(req_read, urbr);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urbr_bulk_partial(req_read, urbr);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urbr_iso_partial(req_read, urbr);
		break;
	default:
		TraceWarning(TRACE_READ, "unexpected partial urbr: %s", urb_function_str(urbfunc));
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	TraceInfo(TRACE_READ, "Leave: %!STATUS!", status);

	return status;
}

static NTSTATUS
store_urbr_urb(WDFREQUEST req_read, purb_req_t urbr)
{
	USHORT		urb_func;
	NTSTATUS	status;

	urb_func = urbr->u.urb.urb->UrbHeader.Function;

	char buf[DBG_URBR_BUFSZ];
	TraceInfo(TRACE_READ, "%s", dbg_urbr(buf, sizeof(buf), urbr));

	switch (urb_func) {
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		status = store_urbr_get_status(req_read, urbr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		status = store_urbr_dscr_dev(req_read, urbr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
		status = store_urbr_dscr_intf(req_read, urbr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = store_urbr_control_transfer(req_read, urbr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = store_urbr_control_transfer_ex(req_read, urbr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urbr_vendor_class(req_read, urbr);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urbr_bulk(req_read, urbr);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urbr_iso(req_read, urbr);
		break;
#if 0
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
		status = store_urb_reset_pipe(irp, urb, urbr);
		break;
#endif
	default:
		WdfRequestSetInformation(req_read, 0);
		TraceError(TRACE_READ, "unhandled %s", urb_function_str(urb_func));
		status = STATUS_INVALID_PARAMETER;
	}

	return status;
}

static NTSTATUS
store_cancelled_urbr(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header	*hdr;

	TraceInfo(TRACE_READ, "Enter");

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_INVALID_PARAMETER;

	set_cmd_unlink_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, urbr->u.seq_num_unlink);

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr(WDFREQUEST req_read, purb_req_t urbr)
{
	char buf[DBG_URBR_BUFSZ];
	TraceInfo(TRACE_READ, "urbr: %s", dbg_urbr(buf, sizeof(buf), urbr));

	switch (urbr->type) {
	case URBR_TYPE_URB:
		return store_urbr_urb(req_read, urbr);
	case URBR_TYPE_UNLINK:
		return store_cancelled_urbr(req_read, urbr);
	case URBR_TYPE_SELECT_CONF:
		return store_urbr_select_config(req_read, urbr);
	case URBR_TYPE_SELECT_INTF:
		return store_urbr_select_interface(req_read, urbr);
	case URBR_TYPE_RESET_PIPE:
		return store_urbr_reset_pipe(req_read, urbr);
		break;
	default:
		TraceError(TRACE_READ, "unknown type: %d", urbr->type);
		return STATUS_UNSUCCESSFUL;
	}
}
