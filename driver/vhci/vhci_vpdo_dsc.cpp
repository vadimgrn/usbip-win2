#include "vhci_vpdo_dsc.h"
#include "trace.h"
#include "vhci_vpdo_dsc.tmh"

#include "vhci.h"
#include "vhci_dev.h"
#include "usbreq.h"
#include "usbip_proto.h"
#include "vhci_irp.h"

#include <ntstrsafe.h>

namespace
{

PAGEABLE NTSTATUS req_fetch_dsc(vpdo_dev_t *vpdo, IRP *irp)
{
	PAGED_CODE();

	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
	auto urbr = create_urbr(vpdo, irp, 0);

	if (urbr) {
		status = submit_urbr(vpdo, urbr);
		if (NT_SUCCESS(status)) {
			return STATUS_PENDING;
		}

		Trace(TRACE_LEVEL_ERROR, "Failed to submit fetch descriptor URB");

		free_urbr(urbr);
		status = STATUS_UNSUCCESSFUL;
	}

	return irp_done(irp, status);
}

PAGEABLE bool is_device_serial_number(const USB_DEVICE_DESCRIPTOR *dd, int string_idx)
{
	PAGED_CODE();
	int idx = dd ? dd->iSerialNumber : 0; // not zero if has serial
	return idx && idx == string_idx;
}

PAGEABLE auto copy_wstring(const USB_STRING_DESCRIPTOR *sd, USHORT LanguageId)
{
	PAGED_CODE();

	UCHAR cch = (sd->bLength - sizeof(USB_COMMON_DESCRIPTOR))/sizeof(*sd->bString) + 1;
	PWSTR str = (PWSTR)ExAllocatePoolWithTag(PagedPool, cch*sizeof(*str), USBIP_VHCI_POOL_TAG);

	if (str) {
		[[maybe_unused]] auto st = RtlStringCchCopyNW(str, cch, sd->bString, cch - 1);
		NT_ASSERT(!st);
		Trace(TRACE_LEVEL_INFORMATION, "Serial '%S', LanguageId %#04hx", str, LanguageId);
	} else {
		Trace(TRACE_LEVEL_ERROR, "Can't allocate memory");
	}

	return str;
}

PAGEABLE auto clone(const void *src, ULONG length)
{
	void *buf = ExAllocatePoolWithTag(PagedPool, length, USBIP_VHCI_POOL_TAG);

	if (buf) {
		RtlCopyMemory(buf, src, length);
	} else { 
		Trace(TRACE_LEVEL_ERROR, "Can't allocate %lu bytes", length);
	}

	return buf;
}

} // namespace

PAGEABLE NTSTATUS vpdo_get_dsc_from_nodeconn(vpdo_dev_t *vpdo, IRP *irp, USB_DESCRIPTOR_REQUEST *r, ULONG *psize)
{
	PAGED_CODE();

	auto setup = (USB_DEFAULT_PIPE_SETUP_PACKET*)&r->SetupPacket;
	static_assert(sizeof(*setup) == sizeof(r->SetupPacket), "assert");

	void *dsc_data = nullptr;
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
	}

	if (!dsc_data) {
		return req_fetch_dsc(vpdo, irp);
	}

	ULONG outlen = sizeof(*r) + dsc_len;

	if (*psize < sizeof(*r)) {
		*psize = outlen;
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto ncopy = *psize < outlen ? *psize - sizeof(*r) : outlen;
	if (ncopy) {
		RtlCopyMemory(r->Data, dsc_data, ncopy);
	}

	if (ncopy == outlen) {
		*psize = outlen;
	}

	return STATUS_SUCCESS;
}

/*
 * Configuration descriptor will be saved on usb request select configuration.
 * A usb device can have several configurations, thus it's needed to cache all or none of them.
 */
PAGEABLE void cache_descriptor(vpdo_dev_t *vpdo, const struct _URB_CONTROL_DESCRIPTOR_REQUEST *r, const USB_COMMON_DESCRIPTOR *dsc)
{
	PAGED_CODE();

	NT_ASSERT(dsc->bLength > sizeof(*dsc));

	USB_STRING_DESCRIPTOR *sd = nullptr;

	switch (dsc->bDescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		if (dsc->bLength == sizeof(USB_DEVICE_DESCRIPTOR) && !vpdo->dsc_dev) {
			vpdo->dsc_dev = (USB_DEVICE_DESCRIPTOR*)clone(dsc, dsc->bLength);
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
