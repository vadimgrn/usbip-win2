#include <initguid.h>

#include "vhci_pnp_intf.h"
#include "trace.h"
#include "vhci_pnp_intf.tmh"

#include "vhci.h"
#include "usbip_proto.h"
#include "vhci_irp.h"
#include "strutil.h"

#include <ntstrsafe.h>
#include <wdmguid.h>
#include <usbbusif.h>
#include <strmini.h>
#include <usbcamdi.h>

namespace
{

inline void ref_interface(__in PVOID Context)
{
	vdev_add_ref(static_cast<vdev_t*>(Context));
}

inline void deref_interface(__in PVOID Context)
{
	vdev_del_ref(static_cast<vdev_t*>(Context));
}

} // namespace

static BOOLEAN USB_BUSIFFN IsDeviceHighSpeed(void *context)
{
	auto vpdo = static_cast<vpdo_dev_t*>(context);
	Trace(TRACE_LEVEL_INFORMATION, "%!usb_device_speed!", vpdo->speed);
	return vpdo->speed == USB_SPEED_HIGH;
}

static NTSTATUS USB_BUSIFFN
QueryBusInformation(IN PVOID BusContext, IN ULONG Level, IN OUT PVOID BusInformationBuffer,
	IN OUT PULONG BusInformationBufferLength, OUT PULONG BusInformationActualLength)
{
	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(Level);
	UNREFERENCED_PARAMETER(BusInformationBuffer);
	UNREFERENCED_PARAMETER(BusInformationBufferLength);
	UNREFERENCED_PARAMETER(BusInformationActualLength);

	Trace(TRACE_LEVEL_INFORMATION, "Enter");
	return STATUS_UNSUCCESSFUL;
}

static NTSTATUS USB_BUSIFFN
SubmitIsoOutUrb(IN PVOID context, IN PURB urb)
{
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(urb);

	Trace(TRACE_LEVEL_INFORMATION, "Enter");
	return STATUS_UNSUCCESSFUL;
}

static NTSTATUS USB_BUSIFFN
QueryBusTime(IN PVOID context, IN OUT PULONG currentusbframe)
{
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(currentusbframe);

	Trace(TRACE_LEVEL_INFORMATION, "Enter");
	return STATUS_UNSUCCESSFUL;
}

static VOID USB_BUSIFFN
GetUSBDIVersion(IN PVOID context, IN OUT PUSBD_VERSION_INFORMATION inf, IN OUT PULONG HcdCapabilities)
{
	UNREFERENCED_PARAMETER(context);

	Trace(TRACE_LEVEL_INFORMATION, "Enter");

	*HcdCapabilities = 0;
	inf->USBDI_Version = USBDI_VERSION;
	inf->Supported_USB_Version = 0x200; // binary-coded decimal USB specification version number, USB 2.0
}

static NTSTATUS
QueryControllerType(_In_opt_ PVOID Context,
	_Out_opt_ PULONG HcdiOptionFlags,
	_Out_opt_ PUSHORT PciVendorId,
	_Out_opt_ PUSHORT PciDeviceId,
	_Out_opt_ PUCHAR PciClass,
	_Out_opt_ PUCHAR PciSubClass,
	_Out_opt_ PUCHAR PciRevisionId,
	_Out_opt_ PUCHAR PciProgIf)
{
	UNREFERENCED_PARAMETER(Context);

	if (HcdiOptionFlags) {
		*HcdiOptionFlags = 0;
	}

	if (PciVendorId) {
		*PciVendorId = 0x8086;
	}

	if (PciDeviceId) {
		*PciDeviceId = 0xa2af;
	}

	if (PciClass) {
		*PciClass = 0x0c;
	}

	if (PciSubClass) {
		*PciSubClass = 0x03;
	}

	if (PciRevisionId) {
		*PciRevisionId = 0;
	}

	if (PciProgIf) {
		*PciProgIf = 0;
	}

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS query_interface_usbdi(vpdo_dev_t *vpdo, USHORT size, USHORT version, INTERFACE *intf)
{
	PAGED_CODE();

	if (version > USB_BUSIF_USBDI_VERSION_3) {
		Trace(TRACE_LEVEL_ERROR, "Unsupported %!usb_busif_usbdi_version!", version);
		return STATUS_INVALID_PARAMETER;
	}

	const USHORT iface_size[] = {
		sizeof(USB_BUS_INTERFACE_USBDI_V0), sizeof(USB_BUS_INTERFACE_USBDI_V1),
		sizeof(USB_BUS_INTERFACE_USBDI_V2), sizeof(USB_BUS_INTERFACE_USBDI_V3)
	};

	if (size < iface_size[version]) {
		Trace(TRACE_LEVEL_ERROR, "%!usb_busif_usbdi_version!: size %d < %d", version, size, iface_size[version]);
		return STATUS_INVALID_PARAMETER;
	}

	USB_BUS_INTERFACE_USBDI_V3 *r = (USB_BUS_INTERFACE_USBDI_V3*)intf;

	switch (version) {
	case USB_BUSIF_USBDI_VERSION_3:
		r->QueryControllerType = QueryControllerType;
		r->QueryBusTimeEx = nullptr;
		/* passthrough */
	case USB_BUSIF_USBDI_VERSION_2:
		r->EnumLogEntry = nullptr;
		/* passthrough */
	case USB_BUSIF_USBDI_VERSION_1:
		r->IsDeviceHighSpeed = IsDeviceHighSpeed;
		/* passthrough */
	case USB_BUSIF_USBDI_VERSION_0:
		r->Size = iface_size[version];
		r->Version = version;
		//
		r->BusContext = vpdo;
		r->InterfaceReference = ref_interface;
		r->InterfaceDereference = deref_interface;
		//
		r->GetUSBDIVersion = GetUSBDIVersion;
		r->QueryBusTime = QueryBusTime;
		r->SubmitIsoOutUrb = SubmitIsoOutUrb;
		r->QueryBusInformation = QueryBusInformation;
		break;
	}

	Trace(TRACE_LEVEL_INFORMATION, "%!usb_busif_usbdi_version!", version);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS query_interface_usbcam(USHORT size, USHORT version, INTERFACE* intf)
{
	PAGED_CODE();

	if (version != USBCAMD_VERSION_200) {
		Trace(TRACE_LEVEL_ERROR, "Version %d != %d", version, USBCAMD_VERSION_200);
		return STATUS_INVALID_PARAMETER;
	}

	USBCAMD_INTERFACE *r = (USBCAMD_INTERFACE*)intf;
	if (size < sizeof(*r)) {
		Trace(TRACE_LEVEL_ERROR, "Size %d < %Iu", size, sizeof(*r));
		return STATUS_INVALID_PARAMETER;
	}


	Trace(TRACE_LEVEL_WARNING, "Not implemented");
	return STATUS_NOT_SUPPORTED;
}

static NTSTATUS get_location_string(void *Context, PZZWSTR *ploc_str)
{
	auto vdev = static_cast<vdev_t*>(Context);
	NTSTATUS st = STATUS_SUCCESS;

	WCHAR buf[32];
	size_t remaining = 0;

	if (vdev->type == VDEV_VPDO) {
		vpdo_dev_t *vpdo = (vpdo_dev_t*)vdev;
		st = RtlStringCchPrintfExW(buf, ARRAYSIZE(buf), nullptr, &remaining, STRSAFE_FILL_BEHIND_NULL,
						L"%s(%u)", devcodes[vdev->type], vpdo->port);
	} else {
		st = RtlStringCchCopyExW(buf, ARRAYSIZE(buf), devcodes[vdev->type],
					nullptr, &remaining, STRSAFE_FILL_BEHIND_NULL);
	}

	if (!(st == STATUS_SUCCESS && remaining >= 2)) { // string ends with L"\0\0"
		Trace(TRACE_LEVEL_ERROR, "%!STATUS!, remaining %Iu", st, remaining);
		return st;
	}

	remaining -= 2;
	size_t sz = sizeof(buf) - remaining*sizeof(*buf);

	*ploc_str = (PZZWSTR)ExAllocatePoolWithTag(PagedPool, sz, USBIP_VHCI_POOL_TAG);
	if (*ploc_str) {
		RtlCopyMemory(*ploc_str, buf, sz);
		return STATUS_SUCCESS;
	}

	Trace(TRACE_LEVEL_ERROR, "Can't allocate memory: size %Iu", sz);
	return STATUS_INSUFFICIENT_RESOURCES;
}

static NTSTATUS query_interface_location(vdev_t *vdev, USHORT size, USHORT version, INTERFACE *intf)
{
	PNP_LOCATION_INTERFACE *r = (PNP_LOCATION_INTERFACE*)intf;
	if (size < sizeof(*r)) {
		Trace(TRACE_LEVEL_ERROR, "Size %d < %Iu, version %d", size, sizeof(*r), version);
		return STATUS_INVALID_PARAMETER;
	}

	r->Size = sizeof(*r);
	r->Version = 1;
	r->Context = vdev;
	r->InterfaceReference = ref_interface;
	r->InterfaceDereference = deref_interface;

	r->GetLocationString = get_location_string;
	return STATUS_SUCCESS;
}

/*
 * On success, a bus driver sets Irp->IoStatus.Information to zero.
 * If a bus driver does not export the requested interface and therefore does not handle this IRP
 * for a child PDO, the bus driver leaves Irp->IoStatus.Status as is and completes the IRP.
 */
PAGEABLE NTSTATUS pnp_query_interface(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();

	auto irpstack = IoGetCurrentIrpStackLocation(irp);

	auto intf_type = irpstack->Parameters.QueryInterface.InterfaceType;
	USHORT size = irpstack->Parameters.QueryInterface.Size;
	USHORT version = irpstack->Parameters.QueryInterface.Version;
	INTERFACE *intf = irpstack->Parameters.QueryInterface.Interface;

	NTSTATUS status = irp->IoStatus.Status;

	if (IsEqualGUID(*intf_type, GUID_PNP_LOCATION_INTERFACE)) {
		status = query_interface_location(vdev, size, version, intf);
	} else if (IsEqualGUID(*intf_type, GUID_USBCAMD_INTERFACE)) {
		status = query_interface_usbcam(size, version, intf);
	} else if (IsEqualGUID(*intf_type, USB_BUS_INTERFACE_USBDI_GUID) && vdev->type == VDEV_VPDO) {
		status = query_interface_usbdi((vpdo_dev_t*)vdev, size, version, intf);
	}

	Trace(TRACE_LEVEL_INFORMATION, "%!GUID! -> %!STATUS!", intf_type, status);

	if (status == STATUS_SUCCESS) {
		irp->IoStatus.Information = 0;
	}

	return irp_done(irp, status);
}
