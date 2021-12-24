#include "vhci_vpdo_dsc.h"
#include "trace.h"
#include "vhci_vpdo_dsc.tmh"

#include "vhci.h"
#include "vhci_dev.h"
#include "usbreq.h"
#include "usbip_proto.h"
#include "vhci_irp.h"

#include <stdbool.h>

static PAGEABLE NTSTATUS req_fetch_dsc(vpdo_dev_t *vpdo, IRP *irp)
{
	PAGED_CODE();

	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
	struct urb_req *urbr = create_urbr(vpdo, irp, 0);

	if (urbr) {
		status = submit_urbr(vpdo, urbr);
		if (NT_SUCCESS(status)) {
			return STATUS_PENDING;
		}

		char buf[URB_REQ_STR_BUFSZ];
		TraceInfo(TRACE_GENERAL, "failed to submit unlink urb %s", urb_req_str(buf, sizeof(buf), urbr));

		free_urbr(urbr);
		status = STATUS_UNSUCCESSFUL;
	}

	return irp_done(irp, status);
}

PAGEABLE NTSTATUS vpdo_get_dsc_from_nodeconn(vpdo_dev_t *vpdo, IRP *irp, USB_DESCRIPTOR_REQUEST *r, ULONG *psize)
{
	PAGED_CODE();

	USB_DEFAULT_PIPE_SETUP_PACKET *setup = (USB_DEFAULT_PIPE_SETUP_PACKET*)&r->SetupPacket;
	static_assert(sizeof(*setup) == sizeof(r->SetupPacket), "assert");

	NTSTATUS status = STATUS_INVALID_PARAMETER;

	void *dsc_data = NULL;
	ULONG dsc_len = 0;

	switch (setup->wValue.HiByte) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		dsc_data = vpdo->dsc_dev;
		if (dsc_data) {
			dsc_len = vpdo->dsc_dev->bLength;
		}
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		dsc_data = vpdo->actconfig;
		if (dsc_data) {
			dsc_len = vpdo->actconfig->wTotalLength;
		}
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		status = req_fetch_dsc(vpdo, irp);
		break;
	default:
		TraceError(TRACE_GENERAL, "Unhandled %!usb_descriptor_type!", setup->wValue.HiByte);
	}

	if (dsc_data) {
		ULONG outlen = sizeof(*r) + dsc_len;
		ULONG ncopy = outlen;

		if (*psize < sizeof(*r)) {
			*psize = outlen;
			return STATUS_BUFFER_TOO_SMALL;
		}
		if (*psize < outlen) {
			ncopy = *psize - sizeof(*r);
		}
		status = STATUS_SUCCESS;
		if (ncopy > 0) {
			RtlCopyMemory(r->Data, dsc_data, ncopy);
		}
		if (ncopy == outlen) {
			*psize = outlen;
		}
	}

	return status;
}

static PAGEABLE bool is_device_serial_number(const USB_DEVICE_DESCRIPTOR *dd, int string_idx)
{
	PAGED_CODE();
	int idx = dd ? dd->iSerialNumber : 0; // not zero if has serial
	return idx && idx == string_idx;
}

static PAGEABLE PWSTR copy_wstring(const USB_STRING_DESCRIPTOR *sd, USHORT LanguageId)
{
	PAGED_CODE();

	UCHAR cch = (sd->bLength - sizeof(USB_COMMON_DESCRIPTOR))/sizeof(*sd->bString) + 1;
	PWSTR str = ExAllocatePoolWithTag(PagedPool, cch*sizeof(*str), USBIP_VHCI_POOL_TAG);

	if (str) {
		NTSTATUS st = RtlStringCchCopyNW(str, cch, sd->bString, cch - 1);
		DBG_UNREFERENCED_LOCAL_VARIABLE(st);
		NT_ASSERT(!st);

		TraceInfo(TRACE_VPDO, "Serial '%S', LanguageId %#04hx", str, LanguageId);
	} else {
		TraceError(TRACE_VPDO, "Can't allocate memory");
	}

	return str;
}

PAGEABLE void *clone(const void *src, ULONG length)
{
	void *buf = ExAllocatePoolWithTag(PagedPool, length, USBIP_VHCI_POOL_TAG);
	
	if (buf) {
		RtlCopyMemory(buf, src, length);
	} else { 
		TraceError(TRACE_VPDO, "Can't allocate %lu bytes", length);
	}

	return buf;
}

/*
 * Configuration descriptor will be saved on usb request select configuration.
 * A usb device can have several configurations, thus it's needed to cache all or none of them.
 */
PAGEABLE void cache_descriptor(vpdo_dev_t *vpdo, const struct _URB_CONTROL_DESCRIPTOR_REQUEST *r, const USB_COMMON_DESCRIPTOR *dsc)
{
	PAGED_CODE();

	NT_ASSERT(dsc->bLength > sizeof(*dsc));

	USB_STRING_DESCRIPTOR *sd = NULL;

	switch (dsc->bDescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		if (dsc->bLength == sizeof(USB_DEVICE_DESCRIPTOR) && !vpdo->dsc_dev) {
			vpdo->dsc_dev = clone(dsc, dsc->bLength);
		}
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		sd = (USB_STRING_DESCRIPTOR*)dsc;
		if (!vpdo->serial && sd->bLength >= sizeof(*sd) && is_device_serial_number(vpdo->dsc_dev, r->Index)) {
			vpdo->serial = copy_wstring(sd, r->LanguageId);
		} 
		break;
	}
}
