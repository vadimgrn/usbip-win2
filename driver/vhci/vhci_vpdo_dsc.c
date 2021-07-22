#include "vhci_vpdo_dsc.h"
#include "vhci.h"
#include "vhci_dev.h"
#include "usbreq.h"
#include "usbip_proto.h"
#include "vhci_irp.h"

static NTSTATUS
req_fetch_dsc(pvpdo_dev_t vpdo, PIRP irp)
{
	struct urb_req	*urbr;
	NTSTATUS	status;

	urbr = create_urbr(vpdo, irp, 0);
	if (urbr == NULL)
		status = STATUS_INSUFFICIENT_RESOURCES;
	else {
		status = submit_urbr(vpdo, urbr);
		if (NT_SUCCESS(status))
			return STATUS_PENDING;
		else {
			DBGI(DBG_GENERAL, "failed to submit unlink urb: %s\n", dbg_urbr(urbr));
			free_urbr(urbr);
			status = STATUS_UNSUCCESSFUL;
		}
	}
	return irp_done(irp, status);
}

PAGEABLE NTSTATUS vpdo_get_dsc_from_nodeconn(vpdo_dev_t *vpdo, IRP *irp, USB_DESCRIPTOR_REQUEST *dsc_req, ULONG *psize)
{
	USB_DEFAULT_PIPE_SETUP_PACKET *setup = (USB_DEFAULT_PIPE_SETUP_PACKET*)&dsc_req->SetupPacket;
	static_assert(sizeof(*setup) == sizeof(dsc_req->SetupPacket), "assert");

	PVOID		dsc_data = NULL;
	ULONG		dsc_len = 0;
	NTSTATUS	status = STATUS_INVALID_PARAMETER;

	switch (setup->wValue.HiByte) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		dsc_data = vpdo->dsc_dev;
		if (dsc_data != NULL)
			dsc_len = sizeof(USB_DEVICE_DESCRIPTOR);
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		dsc_data = vpdo->dsc_conf;
		if (dsc_data != NULL)
			dsc_len = vpdo->dsc_conf->wTotalLength;
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		status = req_fetch_dsc(vpdo, irp);
		break;
	default:
		DBGE(DBG_GENERAL, "unhandled descriptor type: %s\n", dbg_usb_descriptor_type(setup->wValue.HiByte));
		break;
	}

	if (dsc_data) {
		ULONG	outlen = sizeof(*dsc_req) + dsc_len;
		ULONG	ncopy = outlen;

		if (*psize < sizeof(*dsc_req)) {
			*psize = outlen;
			return STATUS_BUFFER_TOO_SMALL;
		}
		if (*psize < outlen) {
			ncopy = *psize - sizeof(*dsc_req);
		}
		status = STATUS_SUCCESS;
		if (ncopy > 0)
			RtlCopyMemory(dsc_req->Data, dsc_data, ncopy);
		if (ncopy == outlen)
			*psize = outlen;
	}

	return status;
}

/*
 * need to cache a descriptor?
 * Currently, device descriptor & full configuration descriptor are cached in vpdo.
 */
static BOOLEAN
need_caching_dsc(pvpdo_dev_t vpdo, struct _URB_CONTROL_DESCRIPTOR_REQUEST* urb_cdr, PUSB_COMMON_DESCRIPTOR dsc)
{
	USB_CONFIGURATION_DESCRIPTOR *dsc_conf = NULL;

	switch (urb_cdr->DescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		if (vpdo->dsc_dev) {
			return FALSE;
		}
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		if (vpdo->dsc_conf) {
			return FALSE;
		}
		dsc_conf = (USB_CONFIGURATION_DESCRIPTOR*)dsc;
		if (urb_cdr->TransferBufferLength < dsc_conf->wTotalLength) {
			DBGI(DBG_WRITE, "ignore non-full configuration descriptor\n");
			return FALSE;
		}
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		/* string descrptor will be fetched on demand */
		return FALSE;
	default:
		return FALSE;
	}

	return TRUE;
}

void
try_to_cache_descriptor(pvpdo_dev_t vpdo, struct _URB_CONTROL_DESCRIPTOR_REQUEST* urb_cdr, PUSB_COMMON_DESCRIPTOR dsc)
{
	if (!need_caching_dsc(vpdo, urb_cdr, dsc)) {
		return;
	}

	USB_COMMON_DESCRIPTOR *dsc_new = ExAllocatePoolWithTag(PagedPool, urb_cdr->TransferBufferLength, USBIP_VHCI_POOL_TAG);
	if (!dsc_new) {
		DBGE(DBG_WRITE, "out of memory\n");
		return;
	}

	RtlCopyMemory(dsc_new, dsc, urb_cdr->TransferBufferLength);

	switch (urb_cdr->DescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		vpdo->dsc_dev = (PUSB_DEVICE_DESCRIPTOR)dsc_new;
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		vpdo->dsc_conf = (PUSB_CONFIGURATION_DESCRIPTOR)dsc_new;
		break;
	default:
		ExFreePoolWithTag(dsc_new, USBIP_VHCI_POOL_TAG);
	}
}
