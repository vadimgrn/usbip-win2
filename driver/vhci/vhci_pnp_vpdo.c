#include "vhci_pnp_vpdo.h"
#include "vhci_irp.h"

// IRP_MN_DEVICE_ENUMERATED is included by default since Windows 7.
#if WINVER<0x0701
#define IRP_MN_DEVICE_ENUMERATED 0x19
#endif

PAGEABLE BOOLEAN
process_pnp_vpdo(pvpdo_dev_t vpdo, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status;

	PAGED_CODE();

	// NB: Because we are a bus enumerator, we have no one to whom we could
	// defer these irps.  Therefore we do not pass them down but merely
	// return them.
	switch (irpstack->MinorFunction) {
	case IRP_MN_DEVICE_USAGE_NOTIFICATION:
		// OPTIONAL for bus drivers.
		// This bus drivers any of the bus's descendants
		// (child device, child of a child device, etc.) do not
		// contain a memory file namely paging file, dump file,
		// or hibernation file. So we  fail this Irp.
		status = STATUS_UNSUCCESSFUL;
		break;
	case IRP_MN_EJECT:
		// For the device to be ejected, the device must be in the D3
		// device power state (off) and must be unlocked
		// (if the device supports locking). Any driver that returns success
		// for this IRP must wait until the device has been ejected before
		// completing the IRP.

		vhub_mark_unplugged_vpdo(vhub_from_vpdo(vpdo), vpdo);

		status = STATUS_SUCCESS;
		break;
	case IRP_MN_DEVICE_ENUMERATED:
		//
		// This request notifies bus drivers that a device object exists and
		// that it has been fully enumerated by the plug and play manager.
		//
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_PNP_DEVICE_STATE:
		irp->IoStatus.Information = 0;
		status = irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
	case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
		/* not handled */
		status = irp->IoStatus.Status;
		break;
	default:
		return FALSE;
	}

	irp_done(irp, status);
	return TRUE;
}