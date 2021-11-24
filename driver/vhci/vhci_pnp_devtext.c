#include "vhci_pnp_devtext.h"
#include "trace.h"
#include "vhci_pnp_devtext.tmh"

#include "vhci.h"
#include "vhci_irp.h"

static LPCWSTR vdev_descs[] = {
	L"usbip-win ROOT", L"usbip-win CPDO", L"usbip-win VHCI", L"usbip-win HPDO", L"usbip-win VHUB", L"usbip-win VPDO"
};

static LPCWSTR vdev_locinfos[] = {
	L"None", L"Root", L"Root", L"VHCI", L"VHCI", L"HPDO"
};

PAGEABLE NTSTATUS pnp_query_device_text(vdev_t *vdev, IRP *irp, IO_STACK_LOCATION *irpstack)
{
	PAGED_CODE();

	DEVICE_TEXT_TYPE type = irpstack->Parameters.QueryDeviceText.DeviceTextType;
	LPCWSTR str = NULL;

	switch (type) {
	case DeviceTextDescription:
		str = libdrv_strdupW(vdev_descs[vdev->type]);
		break;
	case DeviceTextLocationInformation:
		str = libdrv_strdupW(vdev_locinfos[vdev->type]);
		break;
	}

	NTSTATUS status = str ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
	irp->IoStatus.Information = (ULONG_PTR)str;

	TraceInfo(TRACE_PNP, "%!DEVICE_TEXT_TYPE!, LCID %#lx -> '%!WSTR!', %!STATUS!",
		type, irpstack->Parameters.QueryDeviceText.LocaleId, str, status);

	return irp_done(irp, status);
}