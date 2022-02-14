#include "internal_ioctl.h"
#include "trace.h"
#include "internal_ioctl.tmh"

#include "dbgcommon.h"
#include "irp.h"
#include "dev.h"
#include "read.h"
#include "csq.h"

namespace
{

const auto STATUS_SUBMIT_TO_SERVER = NTSTATUS(-1);

/*
* Code must be in nonpaged section if it acquires spinlock.
*/
NTSTATUS vhci_ioctl_abort_pipe(vpdo_dev_t*, USBD_PIPE_HANDLE hPipe)
{
	TraceUrb("PipeHandle %#Ix, NOT IMPEMENTED", (uintptr_t)hPipe);
	return hPipe ? STATUS_NOT_IMPLEMENTED : STATUS_INVALID_PARAMETER;
}

NTSTATUS urb_control_get_status_request(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbControlGetStatusRequest;

	TraceUrb("irp %04x -> %s: TransferBufferLength %lu (must be 2), Index %hd", 
			irp, urb_function_str(r.Hdr.Function), r.TransferBufferLength, r.Index);

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS urb_control_vendor_class_request(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbControlVendorClassRequest;
	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceUrb("irp %04x -> %s: %s, TransferBufferLength %lu, %s(%!#XBYTE!), Value %#hx, Index %#hx",
		irp, urb_function_str(r.Hdr.Function), usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags), 
		r.TransferBufferLength, brequest_str(r.Request), r.Request, r.Value, r.Index);

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS urb_control_descriptor_request(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbControlDescriptorRequest;

	TraceUrb("irp %04x -> %s: TransferBufferLength %lu(%#lx), Index %#x, %!usb_descriptor_type!, LanguageId %#04hx",
		irp, urb_function_str(r.Hdr.Function), r.TransferBufferLength, r.TransferBufferLength, 
		r.Index, r.DescriptorType, r.LanguageId);

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS urb_control_feature_request(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbControlFeatureRequest;

	TraceUrb("irp %04x -> %s: FeatureSelector %#hx, Index %#hx", 
		irp, urb_function_str(r.Hdr.Function), r.FeatureSelector, r.Index);

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS urb_select_configuration(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	char buf[SELECT_CONFIGURATION_STR_BUFSZ];
	TraceUrb("irp %04x -> %s", irp, select_configuration_str(buf, sizeof(buf), &urb->UrbSelectConfiguration));

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS urb_select_interface(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	char buf[SELECT_INTERFACE_STR_BUFSZ];
	TraceUrb("irp %04x -> %s", irp, select_interface_str(buf, sizeof(buf), &urb->UrbSelectInterface));

	return STATUS_SUBMIT_TO_SERVER;
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
NTSTATUS urb_pipe_request(vpdo_dev_t *vpdo, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbPipeRequest;
	NTSTATUS st = STATUS_NOT_SUPPORTED;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_ABORT_PIPE:
		st = vhci_ioctl_abort_pipe(vpdo, r.PipeHandle);
		break;
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
		st = STATUS_SUBMIT_TO_SERVER;
		break;
	case URB_FUNCTION_SYNC_RESET_PIPE:
	case URB_FUNCTION_SYNC_CLEAR_STALL:
	case URB_FUNCTION_CLOSE_STATIC_STREAMS:
		urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
		break;
	}

	TraceUrb("irp %04x -> %s: PipeHandle %#Ix(EndpointAddress %#02x, %!USBD_PIPE_TYPE!, Interval %d) -> %!STATUS!",
		irp,
		urb_function_str(r.Hdr.Function), 
		(uintptr_t)r.PipeHandle, 
		get_endpoint_address(r.PipeHandle), 
		get_endpoint_type(r.PipeHandle),
		get_endpoint_interval(r.PipeHandle),
		st);

	return st;
}

/*
* Can't be implemented without server's support.
* In any case the result will be irrelevant due to network latency.
* 
* See: <linux>//drivers/usb/core/usb.c, usb_get_current_frame_number. 
*/
NTSTATUS urb_get_current_frame_number(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	urb->UrbGetCurrentFrameNumber.FrameNumber = 0; // FIXME: get usb_get_current_frame_number() on Linux server
	TraceUrb("irp %04x: FrameNumber %lu", irp, urb->UrbGetCurrentFrameNumber.FrameNumber);

	urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
	return STATUS_SUCCESS;
}

NTSTATUS urb_control_transfer_any(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	static_assert(offsetof(_URB_CONTROL_TRANSFER, SetupPacket) == offsetof(_URB_CONTROL_TRANSFER_EX, SetupPacket));
	auto &r = urb->UrbControlTransferEx;

	char buf_flags[USBD_TRANSFER_FLAGS_BUFBZ];
	char buf_setup[USB_SETUP_PKT_STR_BUFBZ];

	TraceUrb("irp %04x -> PipeHandle %#Ix, %s, TransferBufferLength %lu, Timeout %lu, %s",
		irp, (uintptr_t)r.PipeHandle,
		usbd_transfer_flags(buf_flags, sizeof(buf_flags), r.TransferFlags),
		r.TransferBufferLength,
		urb->UrbHeader.Function == URB_FUNCTION_CONTROL_TRANSFER_EX ? r.Timeout : 0,
		usb_setup_pkt_str(buf_setup, sizeof(buf_setup), r.SetupPacket));

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS bulk_or_interrupt_transfer(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbBulkOrInterruptTransfer;
	const char *func = urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER_USING_CHAINED_MDL ? ", chained mdl" : ".";

	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceUrb("irp %04x -> PipeHandle %#Ix, %s, TransferBufferLength %lu%s",
		irp, (uintptr_t)r.PipeHandle,
		usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
		r.TransferBufferLength,
		func);

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS isoch_transfer(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbIsochronousTransfer;
	const char *func = urb->UrbHeader.Function == URB_FUNCTION_ISOCH_TRANSFER_USING_CHAINED_MDL ? ", chained mdl" : ".";

	char buf[USBD_TRANSFER_FLAGS_BUFBZ];

	TraceUrb("irp %04x -> PipeHandle %#Ix, %s, TransferBufferLength %lu, StartFrame %lu, NumberOfPackets %lu, ErrorCount %lu%s",
		irp, (uintptr_t)r.PipeHandle,	
		usbd_transfer_flags(buf, sizeof(buf), r.TransferFlags),
		r.TransferBufferLength, 
		r.StartFrame, 
		r.NumberOfPackets, 
		r.ErrorCount,
		func);

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS usb_function_deprecated(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	TraceUrb("irp %04x: %s not supported", irp, urb_function_str(urb->UrbHeader.Function));

	urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS get_configuration(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbControlGetConfigurationRequest;
	TraceUrb("irp %04x -> TransferBufferLength %lu (must be 1)", irp, r.TransferBufferLength);

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS get_interface(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbControlGetInterfaceRequest;
	TraceUrb("irp %04x -> TransferBufferLength %lu (must be 1), Interface %hu", irp, r.TransferBufferLength, r.Interface);

	return STATUS_SUBMIT_TO_SERVER;
}

NTSTATUS get_ms_feature_descriptor(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbOSFeatureDescriptorRequest;

	TraceUrb("irp %04x -> TransferBufferLength %lu, Recipient %d, InterfaceNumber %d, MS_PageIndex %d, MS_FeatureDescriptorIndex %d", 
		irp, r.TransferBufferLength, r.Recipient, r.InterfaceNumber, r.MS_PageIndex, r.MS_FeatureDescriptorIndex);

	return STATUS_NOT_SUPPORTED;
}

/*
 * See: <kernel>/drivers/usb/core/message.c, usb_set_isoch_delay.
 */
NTSTATUS get_isoch_pipe_transfer_path_delays(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbGetIsochPipeTransferPathDelays;

	TraceUrb("irp %04x -> PipeHandle %#Ix, MaximumSendPathDelayInMilliSeconds %lu, MaximumCompletionPathDelayInMilliSeconds %lu",
		irp, (uintptr_t)r.PipeHandle, 
		r.MaximumSendPathDelayInMilliSeconds, 
		r.MaximumCompletionPathDelayInMilliSeconds);

	return STATUS_NOT_SUPPORTED;
}

NTSTATUS open_static_streams(vpdo_dev_t*, URB *urb, UINT32 irp)
{
	auto &r = urb->UrbOpenStaticStreams;

	TraceUrb("irp %04x -> PipeHandle %#Ix, NumberOfStreams %lu, StreamInfoVersion %hu, StreamInfoSize %hu",
		irp, (uintptr_t)r.PipeHandle, r.NumberOfStreams, r.StreamInfoVersion, r.StreamInfoSize);

	return STATUS_NOT_SUPPORTED;
}

using urb_function_t = NTSTATUS(vpdo_dev_t*, URB*, UINT32);

urb_function_t* const urb_functions[] =
{
	urb_select_configuration,
	urb_select_interface,
	urb_pipe_request, // URB_FUNCTION_ABORT_PIPE

	usb_function_deprecated, // URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL
	usb_function_deprecated, // URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL

	usb_function_deprecated, // URB_FUNCTION_GET_FRAME_LENGTH
	usb_function_deprecated, // URB_FUNCTION_SET_FRAME_LENGTH
	urb_get_current_frame_number,

	urb_control_transfer_any,
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
	urb_control_transfer_any,

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

NTSTATUS usb_submit_urb(vpdo_dev_t *vpdo, IRP *irp)
{
	auto urb = (URB*)URB_FROM_IRP(irp);
	if (!urb) {
		Trace(TRACE_LEVEL_WARNING, "Null urb");
		return STATUS_INVALID_PARAMETER;
	}

	auto func = urb->UrbHeader.Function;

	if (auto handler = func < ARRAYSIZE(urb_functions) ? urb_functions[func] : nullptr) {
		auto uirp = reinterpret_cast<uintptr_t>(irp);
		auto st = handler(vpdo, urb, static_cast<UINT32>(uirp));
		return st == STATUS_SUBMIT_TO_SERVER ? submit_to_server(vpdo, irp) : st;
	}

	Trace(TRACE_LEVEL_ERROR, "%s(%#04x) has no handler (reserved?)", urb_function_str(func), func);
	return STATUS_INVALID_PARAMETER;
}

auto setup_topology_address(vpdo_dev_t *vpdo, USB_TOPOLOGY_ADDRESS &r)
{
	r.RootHubPortNumber = static_cast<USHORT>(vpdo->port);
	NT_ASSERT(r.RootHubPortNumber == vpdo->port);

	TraceUrb("RootHubPortNumber %d", r.RootHubPortNumber);
	return STATUS_SUCCESS;
}

NTSTATUS usb_get_port_status(ULONG *status)
{
	*status = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
	TraceUrb("-> PORT_ENABLED|PORT_CONNECTED"); 
	return STATUS_SUCCESS;
}

void handle_read(NTSTATUS &status, vpdo_dev_t *vpdo, IRP *read_irp, IRP *irp, bool complete)
{
	if (auto err = do_read(vpdo, read_irp, irp, false)) {
		if (complete) {
			complete_internal_ioctl(irp, err);
		} else {
			status = err;
		}
	}
}

} // namespace


NTSTATUS complete_internal_ioctl(IRP *irp, NTSTATUS status)
{
	irp->IoStatus.Information = 0;
	return CompleteRequest(irp, status);
}

NTSTATUS submit_to_server(vpdo_dev_t *vpdo, IRP *irp)
{
	auto status = STATUS_PENDING;

	{
		[[maybe_unused]] auto err = IoCsqInsertIrpEx(&vpdo->rx_irps_csq, irp, nullptr, InsertTail());
		NT_ASSERT(!err);
	}

	if (auto read_irp = IoCsqRemoveNextIrp(&vpdo->read_irp_csq, nullptr)) {

		auto ctx = as_pointer(vpdo->seqnum_payload);

		if (auto next_irp = IoCsqRemoveNextIrp(&vpdo->rx_irps_csq, ctx)) {
			handle_read(status, vpdo, read_irp, next_irp, next_irp != irp);
		} else if (ctx) { // irp with payload has cancelled, but header was already read
			auto err = abort_read_payload(vpdo, read_irp);
			CompleteRequest(read_irp, err);
		} else { // irp has cancelled
			[[maybe_unused]] auto err = IoCsqInsertIrpEx(&vpdo->read_irp_csq, read_irp, nullptr, InsertTail());
			NT_ASSERT(!err);
		}
	}

	return status;
}

extern "C" NTSTATUS vhci_internal_ioctl(__in DEVICE_OBJECT *devobj, __in IRP *Irp)
{
	auto irpStack = IoGetCurrentIrpStackLocation(Irp);
	auto ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	TraceCall("Enter irql %!irql!, %s(%#08lX)", KeGetCurrentIrql(), dbg_ioctl_code(ioctl_code), ioctl_code);

	auto vpdo = to_vpdo_or_null(devobj);
	if (!vpdo) {
		Trace(TRACE_LEVEL_WARNING, "Internal ioctl only for vpdo is allowed");
		return CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	if (vpdo->unplugged) {
		NTSTATUS st = STATUS_DEVICE_NOT_CONNECTED;
		Trace(TRACE_LEVEL_VERBOSE, "%!STATUS!", st);
		return CompleteRequest(Irp, st);
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
		status = submit_to_server(vpdo, Irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, *static_cast<USB_TOPOLOGY_ADDRESS*>(irpStack->Parameters.Others.Argument1));
		break;
	default:
		Trace(TRACE_LEVEL_WARNING, "Unhandled %s(%#08lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}

	if (status != STATUS_PENDING) {
		complete_internal_ioctl(Irp, status);
	}

	TraceCall("Leave %!STATUS!", status);
	return status;
}
