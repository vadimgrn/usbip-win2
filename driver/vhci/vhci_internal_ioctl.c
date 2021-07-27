#include "vhci_internal_ioctl.h"
#include "vhci_dbg.h"
#include "usbreq.h"

NTSTATUS vhci_ioctl_abort_pipe(vpdo_dev_t *vpdo, USBD_PIPE_HANDLE hPipe)
{
	if (!hPipe) {
		DBGI(DBG_IOCTL, "%s: empty pipe handle\n", __func__);
		return STATUS_INVALID_PARAMETER;
	}

	DBGI(DBG_IOCTL, "%s: PipeHandle %p\n", __func__, hPipe);

	KIRQL oldirql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	// remove all URBRs of the aborted pipe
	for (LIST_ENTRY *le = vpdo->head_urbr.Flink; le != &vpdo->head_urbr;) {
		struct urb_req	*urbr_local = CONTAINING_RECORD(le, struct urb_req, list_all);
		le = le->Flink;

		if (!is_port_urbr(urbr_local, hPipe)) {
			continue;
		}

		DBGI(DBG_IOCTL, "aborted urbr removed: %s\n", dbg_urbr(urbr_local));

		if (urbr_local->irp) {
			PIRP	irp = urbr_local->irp;

			KIRQL oldirql_cancel;
			IoAcquireCancelSpinLock(&oldirql_cancel);
			BOOLEAN	valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
			IoReleaseCancelSpinLock(oldirql_cancel);

			if (valid_irp) {
				irp->IoStatus.Status = STATUS_CANCELLED;
				irp->IoStatus.Information = 0;
				IoCompleteRequest(irp, IO_NO_INCREMENT);
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

static NTSTATUS
submit_urbr_irp(pvpdo_dev_t vpdo, PIRP irp)
{
	struct urb_req	*urbr;
	NTSTATUS	status;

	urbr = create_urbr(vpdo, irp, 0);
	if (urbr == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	status = submit_urbr(vpdo, urbr);
	if (NT_ERROR(status))
		free_urbr(urbr);
	return status;
}

static NTSTATUS
process_irp_urb_req(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	if (urb == NULL) {
		DBGE(DBG_IOCTL, "process_irp_urb_req: null urb\n");
		return STATUS_INVALID_PARAMETER;
	}

	DBGI(DBG_IOCTL, "process_irp_urb_req: function: %s\n", dbg_urbfunc(urb->UrbHeader.Function));

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
	default:
		DBGW(DBG_IOCTL, "process_irp_urb_req: unhandled function: %s: len: %d\n",
			dbg_urbfunc(urb->UrbHeader.Function), urb->UrbHeader.Length);
		return STATUS_INVALID_PARAMETER;
	}
}

static NTSTATUS
setup_topology_address(pvpdo_dev_t vpdo, PIO_STACK_LOCATION irpStack)
{
	PUSB_TOPOLOGY_ADDRESS	topoaddr;

	topoaddr = (PUSB_TOPOLOGY_ADDRESS)irpStack->Parameters.Others.Argument1;
	topoaddr->RootHubPortNumber = (USHORT)vpdo->port;
	return STATUS_SUCCESS;
}

NTSTATUS
vhci_internal_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	PIO_STACK_LOCATION      irpStack;
	NTSTATUS		status;
	pvpdo_dev_t	vpdo;
	ULONG			ioctl_code;

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_internal_ioctl: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	DBGI(DBG_IOCTL, "ioctl code: %s\n", dbg_vhci_ioctl_code(ioctl_code));

	if (!IS_DEVOBJ_VPDO(devobj)) {
		DBGW(DBG_IOCTL, "internal ioctl only for vpdo is allowed\n");
		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	vpdo = (pvpdo_dev_t)devobj->DeviceExtension;

	if (!vpdo->plugged) {
		DBGW(DBG_IOCTL, "device is not connected\n");
		Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_DEVICE_NOT_CONNECTED;
	}

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = process_irp_urb_req(vpdo, Irp, (PURB)irpStack->Parameters.Others.Argument1);
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = STATUS_SUCCESS;
		*(unsigned long *)irpStack->Parameters.Others.Argument1 = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = submit_urbr_irp(vpdo, Irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, irpStack);
		break;
	default:
		DBGE(DBG_IOCTL, "unhandled internal ioctl: %s\n", dbg_vhci_ioctl_code(ioctl_code));
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	if (status != STATUS_PENDING) {
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_internal_ioctl: Leave: %s\n", dbg_ntstatus(status));
	return status;
}