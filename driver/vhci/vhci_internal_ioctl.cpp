#include "dbgcommon.h"
#include "trace.h"
#include "vhci_internal_ioctl.tmh"

#include "usbreq.h"
#include "vhci_irp.h"

const NTSTATUS STATUS_SUBMIT_URBR_IRP = -1L;

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

/*
* Code must be in nonpaged section if it acquires spinlock.
*/
static NTSTATUS vhci_ioctl_abort_pipe(vpdo_dev_t *vpdo, USBD_PIPE_HANDLE hPipe)
{
	TraceUrb("PipeHandle %#Ix", (uintptr_t)hPipe);

	if (!hPipe) {
		return STATUS_INVALID_PARAMETER;
	}

	KIRQL oldirql;
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	// remove all URBRs of the aborted pipe
	for (LIST_ENTRY *le = vpdo->head_urbr.Flink; le != &vpdo->head_urbr; ) {
		struct urb_req	*urbr_local = CONTAINING_RECORD(le, struct urb_req, list_all);
		le = le->Flink;

		if (!is_port_urbr(urbr_local->irp, hPipe)) {
			continue;
		}

		{
			char buf[URB_REQ_STR_BUFSZ];
			TraceVerbose(FLAG_GENERAL, "aborted urbr removed %s", urb_req_str(buf, sizeof(buf), urbr_local));
		}

		if (urbr_local->irp) {
			PIRP irp = urbr_local->irp;

			KIRQL oldirql_cancel;
			IoAcquireCancelSpinLock(&oldirql_cancel);
			BOOLEAN	valid_irp = IoSetCancelRoutine(irp, nullptr) != nullptr;
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
	
	TraceUrb("%s: TransferBufferLength %lu (must be 2), Index %hd", 
		urb_function_str(r->Hdr.Function), r->TransferBufferLength, r->Index);
	
	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_control_vendor_class_request(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	
	struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST *r = &urb->UrbControlVendorClassRequest;
	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceUrb("%s: %s, TransferBufferLength %lu, %s(%!#XBYTE!), Value %#hx, Index %#hx",
			urb_function_str(r->Hdr.Function), usbd_transfer_flags(buf, sizeof(buf), r->TransferFlags), 
			r->TransferBufferLength, brequest_str(r->Request), r->Request, r->Value, r->Index);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_control_descriptor_request(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	
	struct _URB_CONTROL_DESCRIPTOR_REQUEST *r = &urb->UrbControlDescriptorRequest;

	TraceUrb("%s: TransferBufferLength %lu(%#lx), Index %#x, %!usb_descriptor_type!, LanguageId %#04hx",
					urb_function_str(r->Hdr.Function), r->TransferBufferLength, r->TransferBufferLength, 
					r->Index, r->DescriptorType, r->LanguageId);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_control_feature_request(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	
	struct _URB_CONTROL_FEATURE_REQUEST *r = &urb->UrbControlFeatureRequest;

	TraceUrb("%s: FeatureSelector %#hx, Index %#hx", 
			urb_function_str(r->Hdr.Function), r->FeatureSelector, r->Index);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_select_configuration(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	char buf[SELECT_CONFIGURATION_STR_BUFSZ];
	TraceUrb("%s", select_configuration_str(buf, sizeof(buf), &urb->UrbSelectConfiguration));

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS urb_select_interface(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	char buf[SELECT_INTERFACE_STR_BUFSZ];
	TraceUrb("%s", select_interface_str(buf, sizeof(buf), &urb->UrbSelectInterface));

	return STATUS_SUBMIT_URBR_IRP;
}

/*
 * URB_FUNCTION_SYNC_CLEAR_STALL must issue USB_REQ_CLEAR_FEATURE, USB_ENDPOINT_HALT.
 * URB_FUNCTION_SYNC_RESET_PIPE must call usb_reset_endpoint.
 * 
 * Linux server catches control transfer USB_REQ_CLEAR_FEATURE/USB_ENDPOINT_HALT and calls usb_clear_halt.
 * There is no way to distinguish these two operations without modifications on server's side.
 * It can be implemented by passing extra parameter
 * a) wValue=1 to clear halt 
 * b) wValue=2 to call usb_reset_endpoint
 * 
 * See: <linux>/drivers/usb/usbip/stub_rx.c, is_clear_halt_cmd
 * <linux>/drivers/usb/core/message.c, usb_clear_halt, usb_reset_endpoint
 */
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
		urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
		break;
	}

	TraceUrb("%s: PipeHandle %#Ix(EndpointAddress %#02x, %!USBD_PIPE_TYPE!, Interval %d) -> %!STATUS!",
				urb_function_str(r->Hdr.Function), 
				(uintptr_t)r->PipeHandle, 
				get_endpoint_address(r->PipeHandle), 
				get_endpoint_type(r->PipeHandle),
				get_endpoint_interval(r->PipeHandle),
				st);

	return st;
}

/*
 * Can't be implemented without server's support.
 * In any case the result will not be relevant due to network latency.
 * 
 * See: <linux>//drivers/usb/core/usb.c, usb_get_current_frame_number. 
 */
static NTSTATUS urb_get_current_frame_number(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	UNREFERENCED_PARAMETER(urb);

	TraceUrb("Not supported");
	urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;

	return STATUS_NOT_SUPPORTED;
}

static NTSTATUS urb_control_transfer(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER *r = &urb->UrbControlTransfer;

	char buf_flags[USBD_TRANSFER_FLAGS_BUFBZ];
	char buf_setup[USB_SETUP_PKT_STR_BUFBZ];

	TraceUrb("PipeHandle %#Ix, %s, TransferBufferLength %lu, %s",
			(uintptr_t)r->PipeHandle, 
			usbd_transfer_flags(buf_flags, sizeof(buf_flags), r->TransferFlags),
			r->TransferBufferLength,
			usb_setup_pkt_str(buf_setup, sizeof(buf_setup), r->SetupPacket));

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS bulk_or_interrupt_transfer(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_BULK_OR_INTERRUPT_TRANSFER *r = &urb->UrbBulkOrInterruptTransfer;
	const char *func = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL ? ", chained mdl" : ".";

	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceUrb("PipeHandle %#Ix, %s, TransferBufferLength %lu%s",
			(uintptr_t)r->PipeHandle,
			usbd_transfer_flags(buf, sizeof(buf), r->TransferFlags),
			r->TransferBufferLength,
			func);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS isoch_transfer(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_ISOCH_TRANSFER *r = &urb->UrbIsochronousTransfer;
	const char *func = urb->UrbHeader.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? ", chained mdl" : ".";

	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceUrb("PipeHandle %#Ix, %s, TransferBufferLength %lu, StartFrame %lu, NumberOfPackets %lu, ErrorCount %lu%s",
			(uintptr_t)r->PipeHandle,	
			usbd_transfer_flags(buf, sizeof(buf), r->TransferFlags),
			r->TransferBufferLength, 
			r->StartFrame, 
			r->NumberOfPackets, 
			r->ErrorCount,
			func);

	return STATUS_SUBMIT_URBR_IRP;
}
static NTSTATUS urb_control_transfer_ex(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_TRANSFER_EX* r = &urb->UrbControlTransferEx;

	char buf_flags[USBD_TRANSFER_FLAGS_BUFBZ];
	char buf_setup[USB_SETUP_PKT_STR_BUFBZ];

	TraceUrb("PipeHandle %#Ix, %s, TransferBufferLength %lu, Timeout %lu, %s",
			(uintptr_t)r->PipeHandle,
			usbd_transfer_flags(buf_flags, sizeof(buf_flags), r->TransferFlags),
			r->TransferBufferLength,
			r->Timeout,
			usb_setup_pkt_str(buf_setup, sizeof(buf_setup), r->SetupPacket));

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS usb_function_deprecated(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	TraceUrb("%s not supported", urb_function_str(urb->UrbHeader.Function));

	urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
	return STATUS_NOT_SUPPORTED;
}

static NTSTATUS get_configuration(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_GET_CONFIGURATION_REQUEST *r = &urb->UrbControlGetConfigurationRequest;
	TraceUrb("TransferBufferLength %lu (must be 1)", r->TransferBufferLength);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS get_interface(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_CONTROL_GET_INTERFACE_REQUEST *r = &urb->UrbControlGetInterfaceRequest;
	TraceUrb("TransferBufferLength %lu (must be 1), Interface %hu", r->TransferBufferLength, r->Interface);

	return STATUS_SUBMIT_URBR_IRP;
}

static NTSTATUS get_ms_feature_descriptor(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST *r = &urb->UrbOSFeatureDescriptorRequest;

	TraceUrb("TransferBufferLength %lu, Recipient %d, InterfaceNumber %d, MS_PageIndex %d, MS_FeatureDescriptorIndex %d", 
			r->TransferBufferLength, r->Recipient, r->InterfaceNumber, r->MS_PageIndex, r->MS_FeatureDescriptorIndex);

	return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS get_isoch_pipe_transfer_path_delays(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS *r = &urb->UrbGetIsochPipeTransferPathDelays;

	TraceUrb("PipeHandle %#Ix, MaximumSendPathDelayInMilliSeconds %lu, MaximumCompletionPathDelayInMilliSeconds %lu",
				(uintptr_t)r->PipeHandle, 
				r->MaximumSendPathDelayInMilliSeconds, 
				r->MaximumCompletionPathDelayInMilliSeconds);

	return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS open_static_streams(vpdo_dev_t *vpdo, URB *urb)
{
	UNREFERENCED_PARAMETER(vpdo);

	struct _URB_OPEN_STATIC_STREAMS *r = &urb->UrbOpenStaticStreams;

	TraceUrb("PipeHandle %#Ix, NumberOfStreams %lu, StreamInfoVersion %hu, StreamInfoSize %hu",
				(uintptr_t)r->PipeHandle, r->NumberOfStreams, r->StreamInfoVersion, r->StreamInfoSize);

	return STATUS_NOT_IMPLEMENTED;
}

typedef NTSTATUS (*urb_function_t)(vpdo_dev_t*, URB*);

static const urb_function_t urb_functions[] =
{
	urb_select_configuration,
	urb_select_interface,
	urb_pipe_request, // URB_FUNCTION_ABORT_PIPE

	usb_function_deprecated, // URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL
	usb_function_deprecated, // URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL

	usb_function_deprecated, // URB_FUNCTION_GET_FRAME_LENGTH
	usb_function_deprecated, // URB_FUNCTION_SET_FRAME_LENGTH
	urb_get_current_frame_number,

	urb_control_transfer,
	bulk_or_interrupt_transfer,
	isoch_transfer,

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

	nullptr, // URB_FUNCTION_RESERVED_0X0016          

	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_DEVICE
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_INTERFACE
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_ENDPOINT

	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_DEVICE 
	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_INTERFACE
	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_ENDPOINT

	nullptr, // URB_FUNCTION_RESERVE_0X001D

	urb_pipe_request, // URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL

	urb_control_vendor_class_request, // URB_FUNCTION_CLASS_OTHER
	urb_control_vendor_class_request, // URB_FUNCTION_VENDOR_OTHER

	urb_control_get_status_request, // URB_FUNCTION_GET_STATUS_FROM_OTHER

	urb_control_feature_request, // URB_FUNCTION_SET_FEATURE_TO_OTHER
	urb_control_feature_request, // URB_FUNCTION_CLEAR_FEATURE_TO_OTHER

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT

	get_configuration, // URB_FUNCTION_GET_CONFIGURATION
	get_interface, // URB_FUNCTION_GET_INTERFACE

	urb_control_descriptor_request, // URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE
	urb_control_descriptor_request, // URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE

	get_ms_feature_descriptor,

	nullptr, // URB_FUNCTION_RESERVE_0X002B
	nullptr, // URB_FUNCTION_RESERVE_0X002C
	nullptr, // URB_FUNCTION_RESERVE_0X002D
	nullptr, // URB_FUNCTION_RESERVE_0X002E
	nullptr, // URB_FUNCTION_RESERVE_0X002F

	urb_pipe_request, // URB_FUNCTION_SYNC_RESET_PIPE
	urb_pipe_request, // URB_FUNCTION_SYNC_CLEAR_STALL
	urb_control_transfer_ex,

	nullptr, // URB_FUNCTION_RESERVE_0X0033
	nullptr, // URB_FUNCTION_RESERVE_0X0034                  

	open_static_streams,
	urb_pipe_request, // URB_FUNCTION_CLOSE_STATIC_STREAMS
	bulk_or_interrupt_transfer, // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL
	isoch_transfer, // URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL

	nullptr, // 0x0039
	nullptr, // 0x003A        
	nullptr, // 0x003B
	nullptr, // 0x003C        

	get_isoch_pipe_transfer_path_delays // URB_FUNCTION_GET_ISOCH_PIPE_TRANSFER_PATH_DELAYS
};

static NTSTATUS usb_submit_urb(vpdo_dev_t *vpdo, IRP *irp)
{
	auto urb = (URB*)URB_FROM_IRP(irp);
	if (!urb) {
		TraceError(FLAG_GENERAL, "null urb");
		return STATUS_INVALID_PARAMETER;
	}

	USHORT func = urb->UrbHeader.Function;
	urb_function_t pfunc = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr;

	if (pfunc) {
		NTSTATUS st = pfunc(vpdo, urb);
		return st == STATUS_SUBMIT_URBR_IRP ? submit_urbr_irp(vpdo, irp) : st;
	}

	TraceError(FLAG_GENERAL, "%s(%#04x) has no handler (reserved?)", urb_function_str(func), func);
	return STATUS_INVALID_PARAMETER;
}

static NTSTATUS setup_topology_address(vpdo_dev_t *vpdo, USB_TOPOLOGY_ADDRESS *r)
{
	r->RootHubPortNumber = (USHORT)vpdo->port;
	TraceVerbose(FLAG_GENERAL, "RootHubPortNumber %d", r->RootHubPortNumber);
	return STATUS_SUCCESS;
}

static NTSTATUS usb_get_port_status(ULONG *status)
{
	*status = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
	TraceVerbose(FLAG_GENERAL, "-> PORT_ENABLED|PORT_CONNECTED"); 
	return STATUS_SUCCESS;
}

extern "C" NTSTATUS vhci_internal_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	IO_STACK_LOCATION *irpStack = IoGetCurrentIrpStackLocation(Irp);
	ULONG ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	TraceVerbose(FLAG_GENERAL, "Enter irql %!irql!, %s(%#08lX)", KeGetCurrentIrql(), dbg_ioctl_code(ioctl_code), ioctl_code);

	vpdo_dev_t *vpdo = devobj_to_vpdo_or_null(devobj);
	if (!vpdo) {
		TraceWarning(FLAG_GENERAL, "internal ioctl only for vpdo is allowed");
		return irp_done(Irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	if (!vpdo->plugged) {
		NTSTATUS st = STATUS_DEVICE_NOT_CONNECTED;
		TraceVerbose(FLAG_GENERAL, "%!STATUS!", st);
		return irp_done(Irp, st);
	}

	NTSTATUS status = STATUS_NOT_SUPPORTED;

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = usb_submit_urb(vpdo, Irp);
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = usb_get_port_status(static_cast<ULONG*>(irpStack->Parameters.Others.Argument1));
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = submit_urbr_irp(vpdo, Irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, static_cast<USB_TOPOLOGY_ADDRESS*>(irpStack->Parameters.Others.Argument1));
		break;
	default:
		TraceWarning(FLAG_GENERAL, "Unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	if (status != STATUS_PENDING) {
		Irp->IoStatus.Information = 0;
		irp_done(Irp, status);
	}

	TraceVerbose(FLAG_GENERAL, "Leave %!STATUS!", status);
	return status;
}
