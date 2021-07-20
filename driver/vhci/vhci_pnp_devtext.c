#include "vhci_pnp_devtext.h"
#include "vhci.h"
#include "vhci_irp.h"

static LPCWSTR vdev_descs[] = {
	L"usbip-win ROOT", L"usbip-win CPDO", L"usbip-win VHCI", L"usbip-win HPDO", L"usbip-win VHUB", L"usbip-win VPDO"
};

static LPCWSTR vdev_locinfos[] = {
	L"None", L"Root", L"Root", L"VHCI", L"VHCI", L"HPDO"
};

PAGEABLE NTSTATUS
pnp_query_device_text(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status;

	PAGED_CODE();

	status = irp->IoStatus.Status;

	switch (irpstack->Parameters.QueryDeviceText.DeviceTextType) {
	case DeviceTextDescription:
		if (!irp->IoStatus.Information) {
			irp->IoStatus.Information = (ULONG_PTR)libdrv_strdupW(vdev_descs[vdev->type]);
			status = STATUS_SUCCESS;
		}
		break;
	case DeviceTextLocationInformation:
		if (!irp->IoStatus.Information) {
			irp->IoStatus.Information = (ULONG_PTR)libdrv_strdupW(vdev_locinfos[vdev->type]);
			status = STATUS_SUCCESS;
		}
		break;
	default:
		DBGI(DBG_PNP, "unsupported device text type: %u\n", irpstack->Parameters.QueryDeviceText.DeviceTextType);
		break;
	}

	return irp_done(irp, status);
}