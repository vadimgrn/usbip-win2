#include <initguid.h>

#include "vhci_pnp_intf.h"
#include "trace.h"
#include "vhci_pnp_intf.tmh"

#include "usbip_proto.h"
#include "vhci_irp.h"
#include "strutil.h"

static VOID
ref_interface(__in PVOID Context)
{
	pvdev_t	vdev = (pvdev_t)Context;
	vdev_add_ref((pvdev_t)vdev);
}

static VOID
deref_interface(__in PVOID Context)
{
	pvdev_t	vdev = (pvdev_t)Context;
	vdev_del_ref((pvdev_t)vdev);
}

BOOLEAN USB_BUSIFFN
IsDeviceHighSpeed(PVOID context)
{
	pvpdo_dev_t	vpdo = context;
	TraceInfo(TRACE_GENERAL, "IsDeviceHighSpeed called, it is %d\n", vpdo->speed);
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

	TraceInfo(TRACE_GENERAL, "QueryBusInformation called\n");
	return STATUS_UNSUCCESSFUL;
}

static NTSTATUS USB_BUSIFFN
SubmitIsoOutUrb(IN PVOID context, IN PURB urb)
{
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(urb);

	TraceInfo(TRACE_GENERAL, "SubmitIsoOutUrb called\n");
	return STATUS_UNSUCCESSFUL;
}

static NTSTATUS USB_BUSIFFN
QueryBusTime(IN PVOID context, IN OUT PULONG currentusbframe)
{
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(currentusbframe);

	TraceInfo(TRACE_GENERAL, "QueryBusTime called\n");
	return STATUS_UNSUCCESSFUL;
}

static VOID USB_BUSIFFN
GetUSBDIVersion(IN PVOID context, IN OUT PUSBD_VERSION_INFORMATION inf, IN OUT PULONG HcdCapabilities)
{
	UNREFERENCED_PARAMETER(context);

	TraceInfo(TRACE_GENERAL, "GetUSBDIVersion called\n");

	*HcdCapabilities = 0;
	inf->USBDI_Version = 0x600; /* Windows 8 */
	inf->Supported_USB_Version = 0x200; /* USB 2.0 */
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

	if (HcdiOptionFlags != NULL)
		* HcdiOptionFlags = 0;
	if (PciVendorId != NULL)
		* PciVendorId = 0x8086;
	if (PciDeviceId != NULL)
		* PciDeviceId = 0xa2af;
	if (PciClass != NULL)
		* PciClass = 0x0c;
	if (PciSubClass != NULL)
		* PciSubClass = 0x03;
	if (PciRevisionId != NULL)
		* PciRevisionId = 0;
	if (PciProgIf != NULL)
		* PciProgIf = 0;

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
query_interface_usbdi(pvpdo_dev_t vpdo, USHORT size, USHORT version, PINTERFACE intf)
{
	USB_BUS_INTERFACE_USBDI_V3	*bus_intf = (USB_BUS_INTERFACE_USBDI_V3 *)intf;
	unsigned int valid_size[4] = {
		sizeof(USB_BUS_INTERFACE_USBDI_V0), sizeof(USB_BUS_INTERFACE_USBDI_V1),
		sizeof(USB_BUS_INTERFACE_USBDI_V2), sizeof(USB_BUS_INTERFACE_USBDI_V3)
	};

	PAGED_CODE();

	if (version > USB_BUSIF_USBDI_VERSION_3) {
		TraceWarning(TRACE_GENERAL, "vpdo: unsupported usbdi interface version: %d", version);
		return STATUS_INVALID_PARAMETER;
	}
	if (size < valid_size[version]) {
		TraceWarning(TRACE_GENERAL, "vpdo: unsupported usbdi interface version: %d", version);
		return STATUS_INVALID_PARAMETER;
	}

	bus_intf->Size = (USHORT)valid_size[version];

	switch (version) {
	case USB_BUSIF_USBDI_VERSION_3:
		bus_intf->QueryControllerType = QueryControllerType;
		bus_intf->QueryBusTimeEx = NULL;
		/* passthrough */
	case USB_BUSIF_USBDI_VERSION_2:
		bus_intf->EnumLogEntry = NULL;
		/* passthrough */
	case USB_BUSIF_USBDI_VERSION_1:
		bus_intf->IsDeviceHighSpeed = IsDeviceHighSpeed;
		/* passthrough */
	case USB_BUSIF_USBDI_VERSION_0:
		bus_intf->QueryBusInformation = QueryBusInformation;
		bus_intf->SubmitIsoOutUrb = SubmitIsoOutUrb;
		bus_intf->QueryBusTime = QueryBusTime;
		bus_intf->GetUSBDIVersion = GetUSBDIVersion;
		bus_intf->InterfaceReference = ref_interface;
		bus_intf->InterfaceDereference = deref_interface;
		bus_intf->BusContext = vpdo;
		break;
	default:
		TraceError(TRACE_GENERAL, "never go here\n");
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

static NTSTATUS
get_location_string(PVOID Context, PZZWSTR *ploc_str)
{
	pvdev_t vdev = (pvdev_t)Context;
	int	len;

	switch (vdev->type) {
	case VDEV_VPDO:
		len = libdrv_asprintfW(ploc_str, L"%s(%u)t", devcodes[vdev->type], ((pvpdo_dev_t)vdev)->port);
		break;
	default:
		len = libdrv_asprintfW(ploc_str, L"%st", devcodes[vdev->type]);
		break;
	}
	(*ploc_str)[len - 1] = L'\0';

	return STATUS_SUCCESS;
}

static NTSTATUS
query_interface_location(pvdev_t vdev, USHORT size, USHORT version, PINTERFACE intf)
{
	PNP_LOCATION_INTERFACE		*intf_loc = (PNP_LOCATION_INTERFACE *)intf;

	UNREFERENCED_PARAMETER(version);

	if (size < sizeof(PNP_LOCATION_INTERFACE)) {
		TraceWarning(TRACE_GENERAL, "unsupported pnp location interface version: %d", version);
		return STATUS_INVALID_PARAMETER;
	}

	intf_loc->Size = sizeof(PNP_LOCATION_INTERFACE);
	intf_loc->Version = 1;
	intf_loc->GetLocationString = get_location_string;
	intf_loc->Context = (PVOID)vdev;
	intf_loc->InterfaceReference = ref_interface;
	intf_loc->InterfaceDereference = deref_interface;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
pnp_query_interface(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	PAGED_CODE();

	const GUID *intf_type = irpstack->Parameters.QueryInterface.InterfaceType;
	USHORT size = irpstack->Parameters.QueryInterface.Size;
	USHORT version = irpstack->Parameters.QueryInterface.Version;
	INTERFACE *intf = irpstack->Parameters.QueryInterface.Interface;

	NTSTATUS status = STATUS_NOT_SUPPORTED;

	if (IsEqualGUID(intf_type, &GUID_PNP_LOCATION_INTERFACE)) {
		status = query_interface_location(vdev, size, version, intf);
	} else if (IsEqualGUID(intf_type, &USB_BUS_INTERFACE_USBDI_GUID) && vdev->type == VDEV_VPDO) {
		status = query_interface_usbdi((vpdo_dev_t*)vdev, size, version, intf);
	} else {
		TraceWarning(TRACE_GENERAL, "Query unknown interface %!GUID!\n", intf_type);
	}

	return irp_done(irp, status);
}