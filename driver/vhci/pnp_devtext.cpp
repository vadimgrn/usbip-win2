#include "pnp_devtext.h"
#include "trace.h"
#include "pnp_devtext.tmh"

#include "vhci.h"
#include "irp.h"
#include "strutil.h"

namespace
{

const LPCWSTR vdev_descs[] = {
	L"usbip-win ROOT", L"usbip-win CPDO", L"usbip-win VHCI", L"usbip-win HPDO", L"usbip-win VHUB", L"usbip-win VPDO"
};

const LPCWSTR vdev_locinfos[] = {
	L"None", L"Root", L"Root", L"VHCI", L"VHCI", L"HPDO"
};

} // namespace


/*
 * If a bus driver returns information in response to this IRP, 
 * it allocates a NULL-terminated Unicode string from paged memory. 
 */
PAGEABLE NTSTATUS pnp_query_device_text(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();

	auto irpstack = IoGetCurrentIrpStackLocation(irp);

	auto type = irpstack->Parameters.QueryDeviceText.DeviceTextType;
	auto str = (LPCWSTR)irp->IoStatus.Information;

	if (str) {
		Trace(TRACE_LEVEL_WARNING, "%!DEVICE_TEXT_TYPE!, LCID %#lx -> pre-filled '%!WSTR!', %!STATUS!",
			type, irpstack->Parameters.QueryDeviceText.LocaleId, str, irp->IoStatus.Status);

		return irp_done_iostatus(irp);
	}

	switch (type) {
	case DeviceTextDescription:
		str = libdrv_strdupW(PagedPool, vdev_descs[vdev->type]);
		break;
	case DeviceTextLocationInformation:
		str = libdrv_strdupW(PagedPool, vdev_locinfos[vdev->type]);
		break;
	}

	NTSTATUS status = str ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
	irp->IoStatus.Information = (ULONG_PTR)str;

	Trace(TRACE_LEVEL_INFORMATION, "%!DEVICE_TEXT_TYPE!, LCID %#lx -> '%!WSTR!', %!STATUS!",
		type, irpstack->Parameters.QueryDeviceText.LocaleId, str, status);

	return irp_done(irp, status);
}