#include "vhci_internal_ioctl.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_internal_ioctl.tmh"

#include "usbreq.h"
#include "vhci_irp.h"

NTSTATUS vhci_ioctl_abort_pipe(vpdo_dev_t *vpdo, USBD_PIPE_HANDLE hPipe)
{
	if (!hPipe) {
		TraceInfo(TRACE_IOCTL, "empty pipe handle");
		return STATUS_INVALID_PARAMETER;
	}

	TraceInfo(TRACE_IOCTL, "PipeHandle %!HANDLE!", hPipe);

	KIRQL oldirql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	// remove all URBRs of the aborted pipe
	for (LIST_ENTRY *le = vpdo->head_urbr.Flink; le != &vpdo->head_urbr;) {
		struct urb_req	*urbr_local = CONTAINING_RECORD(le, struct urb_req, list_all);
		le = le->Flink;

		if (!is_port_urbr(urbr_local, hPipe)) {
			continue;
		}

		{
			char buf[DBG_URBR_BUFSZ];
			TraceInfo(TRACE_IOCTL, "aborted urbr removed: %s", dbg_urbr(buf, sizeof(buf), urbr_local));
		}

		if (urbr_local->irp) {
			PIRP	irp = urbr_local->irp;

			KIRQL oldirql_cancel;
			IoAcquireCancelSpinLock(&oldirql_cancel);
			BOOLEAN	valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
			IoReleaseCancelSpinLock(oldirql_cancel);

			if (valid_irp) {
				irp->IoStatus.Information = 0;
				irp_done(irp, STATUS_CANCELLED);
			}
		}
		RemoveEntryListInit(&urbr_local->list_state);
		RemoveEntryListInit(&urbr_local->list_all);
		free_urbr(urbr_local);
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_get_frame(pvpdo_dev_t vpdo, PURB urb)
{
	struct _URB_GET_CURRENT_FRAME_NUMBER	*urb_get = &urb->UrbGetCurrentFrameNumber;
	UNREFERENCED_PARAMETER(vpdo);

	urb_get->FrameNumber = 0;
	return STATUS_SUCCESS;
}

static NTSTATUS submit_urbr_irp(vpdo_dev_t *vpdo, IRP *irp)
{
	struct urb_req *urbr = create_urbr(vpdo, irp, 0);
	if (!urbr) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	NTSTATUS status = submit_urbr(vpdo, urbr);
	if (NT_ERROR(status)) {
		free_urbr(urbr);
	}

	return status;
}

static NTSTATUS process_irp_urb_req(vpdo_dev_t *vpdo, IRP *irp, URB *urb)
{
	if (!urb) {
		TraceError(TRACE_IOCTL, "null urb");
		return STATUS_INVALID_PARAMETER;
	}

	TraceInfo(TRACE_IOCTL, "%!urb_function!", urb->UrbHeader.Function);

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_ABORT_PIPE:
		return vhci_ioctl_abort_pipe(vpdo, urb->UrbPipeRequest.PipeHandle);
	case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
		return process_urb_get_frame(vpdo, urb);
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
	case URB_FUNCTION_SELECT_CONFIGURATION:
	case URB_FUNCTION_ISOCH_TRANSFER:
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
	case URB_FUNCTION_SELECT_INTERFACE:
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
	case URB_FUNCTION_CONTROL_TRANSFER:
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		return submit_urbr_irp(vpdo, irp);
	}

	TraceWarning(TRACE_IOCTL, "unhandled %!urb_function!, len %d",
			urb->UrbHeader.Function, urb->UrbHeader.Length);
	
	return STATUS_INVALID_PARAMETER;
}

static NTSTATUS
setup_topology_address(pvpdo_dev_t vpdo, PIO_STACK_LOCATION irpStack)
{
	PUSB_TOPOLOGY_ADDRESS	topoaddr;

	topoaddr = (PUSB_TOPOLOGY_ADDRESS)irpStack->Parameters.Others.Argument1;
	topoaddr->RootHubPortNumber = (USHORT)vpdo->port;
	return STATUS_SUCCESS;
}

NTSTATUS vhci_internal_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	IO_STACK_LOCATION *irpStack = IoGetCurrentIrpStackLocation(Irp);
	ULONG ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	TraceInfo(TRACE_IOCTL, "%s(%#010lX), irp %p", dbg_ioctl_code(ioctl_code), ioctl_code, Irp);

	vpdo_dev_t *vpdo = devobj_to_vpdo(devobj);

	if (vpdo->common.type != VDEV_VPDO) {
		TraceError(TRACE_IOCTL, "internal ioctl only for vpdo is allowed");
		return irp_done(Irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	if (!vpdo->plugged) {
		NTSTATUS st = STATUS_DEVICE_NOT_CONNECTED;
		TraceInfo(TRACE_IOCTL, "%!STATUS!", st);
		return irp_done(Irp, st);
	}

	NTSTATUS status = STATUS_INVALID_PARAMETER;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = process_irp_urb_req(vpdo, Irp, (URB*)irpStack->Parameters.Others.Argument1);
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		*(ULONG*)irpStack->Parameters.Others.Argument1 = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
		status = STATUS_SUCCESS;
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = submit_urbr_irp(vpdo, Irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, irpStack);
		break;
	default:
		TraceWarning(TRACE_IOCTL, "Unhandled %s(%#010lX), irp %p", dbg_ioctl_code(ioctl_code), ioctl_code, Irp);
	}

	if (status != STATUS_PENDING) {
		Irp->IoStatus.Information = 0;
		irp_done(Irp, status);
	}

	TraceInfo(TRACE_IOCTL, "%!STATUS!, irp %p", status, Irp);
	return status;
}