#include "vhci_internal_ioctl.h"
#include "dbgcommon.h"
#include "trace.h"
#include "vhci_internal_ioctl.tmh"

#include "usbreq.h"
#include "vhci_irp.h"

#include <ntstrsafe.h>

const NTSTATUS STATUS_SUBMIT_URBR_IRP = -1L;

enum { USBD_INTERFACE_STR_BUFSZ = 768 };

static const char *usbd_interface_str(char *buf, size_t len, const USBD_INTERFACE_INFORMATION *r, int cnt)
{
	NTSTATUS st = STATUS_SUCCESS;

	NTSTRSAFE_PSTR end = NULL;
	size_t remaining = 0;

	for (int i = 0; i < cnt && st == STATUS_SUCCESS; ++i) {

		st = RtlStringCbPrintfExA(buf, len, &end, &remaining, STRSAFE_NULL_ON_FAILURE,
			"%sInterface: Length %hu, InterfaceNumber %hhu, AlternateSetting %hhu, "
			"Class %#02hhx, SubClass %#02hhx, Protocol %#02hhx, InterfaceHandle %#p, NumberOfPipes %lu", 
			i ? "; " : "",
			r->Length, 
			r->InterfaceNumber,
			r->AlternateSetting,
			r->Class,
			r->SubClass,
			r->Protocol,
			r->InterfaceHandle,
			r->NumberOfPipes);

		for (ULONG j = 0; j < r->NumberOfPipes && st == STATUS_SUCCESS; ++j) {

			const USBD_PIPE_INFORMATION *p = r->Pipes + j;

			st = RtlStringCbPrintfExA(end, remaining, &end, &remaining, STRSAFE_NULL_ON_FAILURE,
				", Pipes[%lu](MaximumPacketSize %hu, EndpointAddress %#02hhx(%s#%d), Interval %hhu, %s, "
				"PipeHandle %#p, MaximumTransferSize %lu, PipeFlags %#lx)",
				j,
				p->MaximumPacketSize,
				p->EndpointAddress,
				USB_ENDPOINT_DIRECTION_IN(p->EndpointAddress) ? "In" : "Out",
				p->EndpointAddress & USB_ENDPOINT_ADDRESS_MASK,
				p->Interval,
				usbd_pipe_type_str(p->PipeType),
				p->PipeHandle,
				p->MaximumTransferSize,
				p->PipeFlags);
		}

		r = (const USBD_INTERFACE_INFORMATION*)((char*)r + r->Length);
	}

	return buf && *buf ? buf : "usbd_interface_str error"; 
}

static NTSTATUS submit_urbr_irp(vpdo_dev_t* vpdo, IRP* irp)
{
	struct urb_req* urbr = create_urbr(vpdo, irp, 0);
	if (!urbr) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	NTSTATUS status = submit_urbr(vpdo, urbr);
	if (NT_ERROR(status)) {
		free_urbr(urbr);
	}

	return status;
}

NTSTATUS vhci_ioctl_abort_pipe(vpdo_dev_t *vpdo, USBD_PIPE_HANDLE hPipe)
{
	TraceInfo(TRACE_IOCTL, "PipeHandle %!HANDLE!", hPipe);

	if (!hPipe) {
		return STATUS_INVALID_PARAMETER;
	}

	KIRQL oldirql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	// remove all URBRs of the aborted pipe
	for (LIST_ENTRY *le = vpdo->head_urbr.Flink; le != &vpdo->head_urbr;) {
		struct urb_req	*urbr_local = CONTAINING_RECORD(le, struct urb_req, list_all);
		le = le->Flink;

		if (!is_port_urbr(urbr_local->irp, hPipe)) {
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

static NTSTATUS urb_control_get_status_request(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_GET_STATUS_REQUEST *r = &urb->UrbControlGetStatusRequest;
	
	TraceVerbose(TRACE_IOCTL, "%s: TransferBufferLength %lu, Index %hd", 
		urb_function_str(r->Hdr.Function), r->TransferBufferLength, r->Index);
	
	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_control_vendor_class_request(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *r = &urb->UrbControlVendorClassRequest;
	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceVerbose(TRACE_IOCTL, "%s: %s, TransferBufferLength %lu, %s(%!#XBYTE!), Value %#hx, Index %#hx",
		urb_function_str(r->Hdr.Function), usbd_transfer_flags(buf, sizeof(buf), r->TransferFlags), 
		r->TransferBufferLength, brequest_str(r->Request), r->Request, r->Value, r->Index);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_control_descriptor_request(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	
	struct _URB_CONTROL_DESCRIPTOR_REQUEST *r = &urb->UrbControlDescriptorRequest;

	TraceVerbose(TRACE_IOCTL, "%s: TransferBufferLength %lu, Index %!UBYTE!, %!usb_descriptor_type!, LanguageId %#04hx",
		urb_function_str(r->Hdr.Function), r->TransferBufferLength, r->Index, r->DescriptorType, r->LanguageId);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_control_feature_request(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	
	struct _URB_CONTROL_FEATURE_REQUEST *r = &urb->UrbControlFeatureRequest;

	TraceVerbose(TRACE_IOCTL, "%s: FeatureSelector %#hx, Index %#hx", 
		urb_function_str(r->Hdr.Function), r->FeatureSelector, r->Index);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_select_configuration(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	
	struct _URB_SELECT_CONFIGURATION *r = &urb->UrbSelectConfiguration;
	USB_CONFIGURATION_DESCRIPTOR *cd = r->ConfigurationDescriptor;

	if (cd) {
		char buf[USBD_INTERFACE_STR_BUFSZ];

		TraceVerbose(TRACE_IOCTL, "ConfigurationHandle %!HANDLE!, "
			"ConfigurationDescriptor(bLength %!UBYTE!, %!usb_descriptor_type!, wTotalLength %hu, bNumInterfaces %!UBYTE!, "
			"bConfigurationValue %!UBYTE!, iConfiguration %!UBYTE!, bmAttributes %!#XBYTE!, MaxPower %!UBYTE!), %s",
			r->ConfigurationHandle,
			cd->bLength,
			cd->bDescriptorType,
			cd->wTotalLength,
			cd->bNumInterfaces,
			cd->bConfigurationValue,
			cd->iConfiguration,
			cd->bmAttributes,
			cd->MaxPower,
			usbd_interface_str(buf, sizeof(buf), &r->Interface, cd->bNumInterfaces));
	} else {
		TraceVerbose(TRACE_IOCTL, "ConfigurationHandle %!HANDLE!, ConfigurationDescriptor NULL (unconfigured)",  
					r->ConfigurationHandle);
	}

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_select_interface(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	
	struct _URB_SELECT_INTERFACE *r = &urb->UrbSelectInterface;
	char buf[USBD_INTERFACE_STR_BUFSZ];

	TraceVerbose(TRACE_IOCTL, "ConfigurationHandle %!HANDLE!, %s", r->ConfigurationHandle, 
			usbd_interface_str(buf, sizeof(buf), &r->Interface, 1));

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_pipe_request(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_PIPE_REQUEST *r = &urb->UrbPipeRequest;
	NTSTATUS st = STATUS_NOT_SUPPORTED;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_ABORT_PIPE:
		st = vhci_ioctl_abort_pipe(vpdo, r->PipeHandle);
		break;
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
		st = STATUS_SUBMIT_URBR_IRP;
		break;
	case URB_FUNCTION_SYNC_RESET_PIPE:
	case URB_FUNCTION_SYNC_CLEAR_STALL:
	case URB_FUNCTION_CLOSE_STATIC_STREAMS:
		break;
	}

	TraceVerbose(TRACE_IOCTL, "%s: PipeHandle %!HANDLE! -> %!STATUS!",
		urb_function_str(r->Hdr.Function), r->PipeHandle, st);

	return st;
}

static NTSTATUS urb_get_current_frame_number(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	urb->UrbGetCurrentFrameNumber.FrameNumber = 0;
	TraceVerbose(TRACE_IOCTL, "FrameNumber %lu", urb->UrbGetCurrentFrameNumber.FrameNumber);

	return STATUS_SUCCESS;
}

static NTSTATUS urb_control_transfer(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer;

	char buf_flags[USBD_TRANSFER_FLAGS_BUFBZ];
	char buf_setup[DBG_USB_SETUP_BUFBZ];

	TraceVerbose(TRACE_IOCTL, "PipeHandle %!HANDLE!, %s, TransferBufferLength %lu, %s",
		r->PipeHandle, 
		usbd_transfer_flags(buf_flags, sizeof(buf_flags), r->TransferFlags),
		r->TransferBufferLength,
		dbg_usb_setup_packet(buf_setup, sizeof(buf_setup), r->SetupPacket));

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_bulk_or_interrupt_transfer(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_BULK_OR_INTERRUPT_TRANSFER *r = &urb->UrbBulkOrInterruptTransfer;
	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceVerbose(TRACE_IOCTL, "PipeHandle %!HANDLE!, %s, TransferBufferLength %lu",
		r->PipeHandle,
		usbd_transfer_flags(buf, sizeof(buf), r->TransferFlags),
		r->TransferBufferLength);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_isoch_transfer(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceVerbose(TRACE_IOCTL, "PipeHandle %!HANDLE!, %s, TransferBufferLength %lu, StartFrame %lu, NumberOfPackets %lu, ErrorCount %lu",
		r->PipeHandle,
		usbd_transfer_flags(buf, sizeof(buf), r->TransferFlags),
		r->TransferBufferLength, r->StartFrame, r->NumberOfPackets, r->ErrorCount);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_control_transfer_ex(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER_EX* r = &urb->UrbControlTransferEx;

	char buf_flags[USBD_TRANSFER_FLAGS_BUFBZ];
	char buf_setup[DBG_USB_SETUP_BUFBZ];

	TraceVerbose(TRACE_IOCTL, "PipeHandle %!HANDLE!, %s, TransferBufferLength %lu, Timeout %lu, %s",
		r->PipeHandle,
		usbd_transfer_flags(buf_flags, sizeof(buf_flags), r->TransferFlags),
		r->TransferBufferLength,
		r->Timeout,
		dbg_usb_setup_packet(buf_setup, sizeof(buf_setup), r->SetupPacket));

	return STATUS_SUBMIT_URBR_IRP;
}

typedef NTSTATUS (*urb_function_t)(vpdo_dev_t*, URB*);

static const urb_function_t urb_functions[] =
{
	urb_select_configuration,
	urb_select_interface,
	urb_pipe_request, // URB_FUNCTION_ABORT_PIPE

	NULL, // URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL
	NULL, // URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL

	NULL, // URB_FUNCTION_GET_FRAME_LENGTH
	NULL, // URB_FUNCTION_SET_FRAME_LENGTH
	urb_get_current_frame_number,

	urb_control_transfer,
	urb_bulk_or_interrupt_transfer,
	urb_isoch_transfer,

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE

	urb_control_feature_request, // URB_FUNCTION_SET_FEATURE_TO_DEVICE
	urb_control_feature_request, // URB_FUNCTION_SET_FEATURE_TO_INTERFACE
	urb_control_feature_request, // URB_FUNCTION_SET_FEATURE_TO_ENDPOINT

	urb_control_feature_request, // URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE
	urb_control_feature_request, // URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE
	urb_control_feature_request, // URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT

	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_DEVICE
	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_INTERFACE
	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_ENDPOINT

	NULL, // URB_FUNCTION_RESERVED_0X0016          

	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_DEVICE
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_INTERFACE
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_ENDPOINT

	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_DEVICE 
	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_INTERFACE
	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_ENDPOINT

	NULL, // URB_FUNCTION_RESERVE_0X001D

	urb_pipe_request, // URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL

	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_OTHER
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_OTHER

	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_OTHER

	urb_control_feature_request, // URB_FUNCTION_SET_FEATURE_TO_OTHER
	urb_control_feature_request, // URB_FUNCTION_CLEAR_FEATURE_TO_OTHER

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT

	NULL, // URB_FUNCTION_GET_CONFIGURATION
	NULL, // URB_FUNCTION_GET_INTERFACE

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE

	NULL, // URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR

	NULL, // URB_FUNCTION_RESERVE_0X002B
	NULL, // URB_FUNCTION_RESERVE_0X002C
	NULL, // URB_FUNCTION_RESERVE_0X002D
	NULL, // URB_FUNCTION_RESERVE_0X002E
	NULL, // URB_FUNCTION_RESERVE_0X002F

	urb_pipe_request, // URB_FUNCTION_SYNC_RESET_PIPE
	urb_pipe_request, // URB_FUNCTION_SYNC_CLEAR_STALL
	urb_control_transfer_ex,

	NULL, // URB_FUNCTION_RESERVE_0X0033
	NULL, // URB_FUNCTION_RESERVE_0X0034                  

	NULL, // URB_FUNCTION_OPEN_STATIC_STREAMS
	urb_pipe_request, // URB_FUNCTION_CLOSE_STATIC_STREAMS
	NULL, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
	NULL, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

	NULL, // 0x0039
	NULL, // 0x003A        
	NULL, // 0x003B        
	NULL, // 0x003C        

	NULL // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
};

static NTSTATUS process_irp_urb_req(vpdo_dev_t *vpdo, IRP *irp)
{
	URB *urb = URB_FROM_IRP(irp);
	if (!urb) {
		TraceError(TRACE_IOCTL, "null urb");
		return STATUS_INVALID_PARAMETER;
	}

	USHORT func = urb->UrbHeader.Function;
	urb_function_t pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : NULL;

	if (pfunc) {
		NTSTATUS st = pfunc(vpdo, urb);
		return st == STATUS_SUBMIT_URBR_IRP ? submit_urbr_irp(vpdo, irp) : st;
	}

	TraceWarning(TRACE_IOCTL, "Not implemented/supported %s(%#04x), Length %d", 
		urb_function_str(func), func, urb->UrbHeader.Length);

	return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
setup_topology_address(pvpdo_dev_t vpdo, PIO_STACK_LOCATION irpStack)
{
	USB_TOPOLOGY_ADDRESS *r = irpStack->Parameters.Others.Argument1;
	r->RootHubPortNumber = (USHORT)vpdo->port;

	TraceVerbose(TRACE_IOCTL, "RootHubPortNumber %d", r->RootHubPortNumber);
	return STATUS_SUCCESS;
}

NTSTATUS vhci_internal_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	IO_STACK_LOCATION *irpStack = IoGetCurrentIrpStackLocation(Irp);
	ULONG ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	TraceInfo(TRACE_IOCTL, "irp %p, irql %!irql!, %s(%#010lX)", 
		Irp, KeGetCurrentIrql(), dbg_ioctl_code(ioctl_code), ioctl_code);

	vpdo_dev_t *vpdo = devobj_to_vpdo(devobj);

	if (vpdo->common.type != VDEV_VPDO) {
		TraceWarning(TRACE_IOCTL, "internal ioctl only for vpdo is allowed");
		return irp_done(Irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	if (!vpdo->plugged) {
		NTSTATUS st = STATUS_DEVICE_NOT_CONNECTED;
		TraceInfo(TRACE_IOCTL, "%!STATUS!", st);
		return irp_done(Irp, st);
	}

	NTSTATUS status = STATUS_SUCCESS;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = process_irp_urb_req(vpdo, Irp);
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		*(ULONG*)irpStack->Parameters.Others.Argument1 = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = submit_urbr_irp(vpdo, Irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, irpStack);
		break;
	default:
		TraceWarning(TRACE_IOCTL, "Unhandled %s(%#010lX), irp %p", dbg_ioctl_code(ioctl_code), ioctl_code, Irp);
		status = STATUS_NOT_SUPPORTED;
	}

	if (status != STATUS_PENDING) {
		Irp->IoStatus.Information = 0;
		irp_done(Irp, status);
	}

	TraceInfo(TRACE_IOCTL, "irp %p -> %!STATUS!", Irp, status);
	return status;
}
