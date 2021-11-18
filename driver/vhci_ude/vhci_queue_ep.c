#include "vhci_queue_ep.h"
#include "vhci_driver.h"
#include "vhci_queue_ep.tmh"

static VOID
internal_device_control(_In_ WDFQUEUE queue, _In_ WDFREQUEST req,
	_In_ size_t outlen, _In_ size_t inlen, _In_ ULONG ioctl_code)
{
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	UNREFERENCED_PARAMETER(inlen);
	UNREFERENCED_PARAMETER(outlen);

	TraceInfo(TRACE_EP, "Enter");

	if (ioctl_code != IOCTL_INTERNAL_USB_SUBMIT_URB) {
		TraceError(TRACE_EP, "unexpected %s(%#010lX)", dbg_ioctl_code(ioctl_code), ioctl_code);
	}
	else {
		pctx_ep_t	ep = *TO_PEP(queue);
		if (ep->vusb->invalid)
			status = STATUS_DEVICE_DOES_NOT_EXIST;
		else
			status = submit_req_urb(*TO_PEP(queue), req);
	}

	if (status != STATUS_PENDING)
		UdecxUrbCompleteWithNtStatus(req, status);

	TraceInfo(TRACE_EP, "Leave: %!STATUS!", status);
}

static VOID
io_default_ep(_In_ WDFQUEUE queue, _In_ WDFREQUEST req)
{
	UNREFERENCED_PARAMETER(queue);
	UNREFERENCED_PARAMETER(req);

	TraceError(TRACE_EP, "unexpected io default callback");
}

WDFQUEUE
create_queue_ep(pctx_ep_t ep)
{
	WDFQUEUE	queue;
	WDF_IO_QUEUE_CONFIG	conf;
	WDF_OBJECT_ATTRIBUTES	attrs;
	NTSTATUS	status;

	WDF_IO_QUEUE_CONFIG_INIT(&conf, WdfIoQueueDispatchParallel);
	conf.EvtIoInternalDeviceControl = internal_device_control;
	conf.EvtIoDefault = io_default_ep;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, pctx_ep_t);
	attrs.ParentObject = ep->ude_ep;
	attrs.SynchronizationScope = WdfSynchronizationScopeQueue;

	status = WdfIoQueueCreate(ep->vusb->vhci->hdev, &conf, &attrs, &queue);
	if (NT_ERROR(status)) {
		TraceError(TRACE_EP, "failed to create queue: %!STATUS!", status);
		return NULL;
	}

	*TO_PEP(queue) = ep;

	return queue;
}
