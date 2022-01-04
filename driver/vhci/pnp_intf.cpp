#include <initguid.h>

#include "pnp_intf.h"
#include "trace.h"
#include "pnp_intf.tmh"

#include "vhci.h"
#include "usbip_proto.h"
#include "irp.h"
#include "strutil.h"
#include "ch9.h"

#include <ntstrsafe.h>
#include <wdmguid.h>
#include <usbbusif.h>
#include <strmini.h>
#include <usbcamdi.h>

namespace
{

void InterfaceReference(__in PVOID Context)
{
	vdev_add_ref(static_cast<vdev_t*>(Context));
}

void InterfaceDereference(__in PVOID Context)
{
	vdev_del_ref(static_cast<vdev_t*>(Context));
}

/*
 * @return true if device is operating at high speed
 */
BOOLEAN USB_BUSIFFN IsDeviceHighSpeed(__in PVOID Context)
{
	auto vpdo = static_cast<vpdo_dev_t*>(Context);
	Trace(TRACE_LEVEL_VERBOSE, "%!usb_device_speed!", vpdo->speed);
	return vpdo->speed >= USB_SPEED_HIGH; // as Windows compares
}

/*
 * @return kilobits per second
 */
auto bus_bandwidth(usb_device_speed speed)
{
	enum 
	{
		KBS = 1000, // rounded 1024
		LOW = KBS*3/2, // 1.5 megabits per second Mbps
		FULL = 12*KBS, // 12Mbs
		HIGH = 500*KBS, // 500Mbs
		SS = 10*HIGH, // 5Gbps,  USB 3.1 Gen 1
		SSP = 2*SS // 10Gbps, USB 3.1 Gen 2
	};

	ULONG val = 0;

	switch (speed) {
	case USB_SPEED_SUPER_PLUS:
		val = SSP;
		break;
	case USB_SPEED_SUPER:
		val = SS;
		break;
	case USB_SPEED_HIGH:
	case USB_SPEED_WIRELESS:
		val = HIGH;
		break;
	case USB_SPEED_FULL:
		val = FULL;
		break;
	case USB_SPEED_LOW:
		val = LOW;
		break;
	case USB_SPEED_UNKNOWN:
		val = HIGH;
		Trace(TRACE_LEVEL_WARNING, "%!usb_device_speed!, assume USB_SPEED_HIGH", speed);
	}

	return val;
}

auto setControllerName(vpdo_dev_t *vpdo, USB_BUS_INFORMATION_LEVEL_1 &r, 
			ULONG BusInformationBufferLength, ULONG &BusInformationActualLength)
{
	r.ControllerNameUnicodeString[0] = L'\0';
	r.ControllerNameLength = 0;

	const auto &src = vpdo->usb_dev_interface;
	
	const auto name_off = offsetof(USB_BUS_INFORMATION_LEVEL_1, ControllerNameUnicodeString);
	BusInformationActualLength = static_cast<ULONG>(name_off + src.Length); // bytes

	if (BusInformationActualLength > BusInformationBufferLength) {
		return STATUS_BUFFER_TOO_SMALL;
	}

	RtlCopyMemory(r.ControllerNameUnicodeString, src.Buffer, src.Length);
	r.ControllerNameLength = src.Length;

	return STATUS_SUCCESS;
}

NTSTATUS USB_BUSIFFN QueryBusInformation(
	IN PVOID BusContext, 
	IN ULONG Level, 
	IN OUT PVOID BusInformationBuffer, 
	IN OUT PULONG BusInformationBufferLength, 
	OUT PULONG BusInformationActualLength)
{
	if (Level > 1) {
		Trace(TRACE_LEVEL_ERROR, "Unexpected Level %lu", Level);
		return STATUS_INVALID_PARAMETER;
	}
	
	auto &r = *reinterpret_cast<USB_BUS_INFORMATION_LEVEL_1*>(BusInformationBuffer);
	const ULONG sizes[] = { sizeof(USB_BUS_INFORMATION_LEVEL_0), sizeof(r) };

	if (*BusInformationBufferLength < sizes[Level]) {
		Trace(TRACE_LEVEL_ERROR, "Level %lu, BusInformationBufferLength %lu is too small", Level, *BusInformationBufferLength);
		return STATUS_BUFFER_TOO_SMALL;
	}

	auto vpdo = static_cast<vpdo_dev_t*>(BusContext);

	*BusInformationActualLength = *BusInformationBufferLength;
	NTSTATUS st = STATUS_SUCCESS;

	switch (Level) {
	case 1:
		st = setControllerName(vpdo, r, *BusInformationBufferLength, *BusInformationActualLength);
		[[fallthrough]];
	case 0:
		r.TotalBandwidth = bus_bandwidth(vpdo->speed); // bits per second (actually Kbps), available on the bus
		r.ConsumedBandwidth = 0; // is already in use, in bits per second
	}

	Trace(TRACE_LEVEL_VERBOSE, "TotalBandwidth %lu, ConsumedBandwidth %lu, ControllerName: Length %lu '%!WSTR!, "
				   "BusInformationBufferLength %lu, BusInformationActualLength %lu -> %!STATUS!",
				r.TotalBandwidth, r.ConsumedBandwidth, r.ControllerNameLength, r.ControllerNameUnicodeString, 
				*BusInformationBufferLength, *BusInformationActualLength, st);

	return st;
}

NTSTATUS USB_BUSIFFN SubmitIsoOutUrb(IN PVOID, IN URB*)
{
	TraceCall("Not supported");
	return STATUS_NOT_SUPPORTED;
}

/*
 * @return the current 32-bit USB frame number
 * If return zero, this function will be called again and again.
 */
NTSTATUS USB_BUSIFFN QueryBusTime(_In_ PVOID BusContext, _Out_opt_ ULONG *CurrentUsbFrame)
{
	auto vpdo = static_cast<vpdo_dev_t*>(BusContext);

	*CurrentUsbFrame = vpdo->current_frame_number ? vpdo->current_frame_number : 100;
	TraceCall("CurrentUsbFrame -> %lu", *CurrentUsbFrame);

	return STATUS_SUCCESS;
}

/*
 * @return the current USB 2.0 frame/micro-frame number when called for
 *         a USB device attached to a USB 2.0 host controller
 *
 * The lowest 3 bits of the returned micro-frame value will contain the current 125us
 * micro-frame, while the upper 29 bits will contain the current 1ms USB frame number.
 */
NTSTATUS USB_BUSIFFN QueryBusTimeEx(_In_opt_ PVOID BusContext, _Out_opt_ ULONG *HighSpeedFrameCounter)
{
	auto vpdo = static_cast<vpdo_dev_t*>(BusContext);

	*HighSpeedFrameCounter = (vpdo->current_frame_number ? vpdo->current_frame_number : 100)  << 3;
	TraceCall("HighSpeedFrameCounter -> %lu", *HighSpeedFrameCounter);

	return STATUS_SUCCESS;
}

VOID USB_BUSIFFN GetUSBDIVersion(IN PVOID BusContext, IN OUT PUSBD_VERSION_INFORMATION inf, IN OUT PULONG HcdCapabilities)
{
	auto vpdo = static_cast<vpdo_dev_t*>(BusContext);

	inf->USBDI_Version = USBDI_VERSION;
	inf->Supported_USB_Version = vpdo->descriptor ? vpdo->descriptor->bcdUSB : bcdUSB20;

	*HcdCapabilities = 0; // see USB_HCD_CAPS_SUPPORTS_RT_THREADS

	Trace(TRACE_LEVEL_VERBOSE, "USBDI_Version %#04lx, Supported_USB_Version %#04lx, HcdCapabilities %#04lx", 
				inf->USBDI_Version, inf->Supported_USB_Version, *HcdCapabilities);
}

UCHAR getPciProgIf(USHORT idProduct)
{
	enum hci_type_t { OHCI, UHCI, EHCI };

	switch (static_cast<hci_type_t>(idProduct)) {
	case EHCI:
		return 0x20;
	case OHCI:
		return 0x10;
	case UHCI:
		[[fallthrough]];
	default:
		return 0x00;
	}
}

NTSTATUS QueryControllerType(
	_In_opt_ PVOID BusContext,
	_Out_opt_ PULONG HcdiOptionFlags,
	_Out_opt_ PUSHORT PciVendorId,
	_Out_opt_ PUSHORT PciDeviceId,
	_Out_opt_ PUCHAR PciClass,
	_Out_opt_ PUCHAR PciSubClass,
	_Out_opt_ PUCHAR PciRevisionId,
	_Out_opt_ PUCHAR PciProgIf)
{
	auto vpdo = static_cast<vpdo_dev_t*>(BusContext);
	auto dd = vpdo->descriptor;

	if (HcdiOptionFlags) {
		*HcdiOptionFlags = 0;
	}

	if (PciVendorId) {
		*PciVendorId = dd->idVendor;
	}

	if (PciDeviceId) {
		*PciDeviceId = dd->idProduct;
	}

	if (PciClass) {
		*PciClass = PCI_CLASS_SERIAL_BUS_CTLR;
	}

	if (PciSubClass) {
		*PciSubClass = PCI_SUBCLASS_SB_USB;
	}

	if (PciRevisionId) { // FIXME: really bcdDevice?
		*PciRevisionId = static_cast<UCHAR>(dd->bcdDevice); // Device Release Number
	}

	if (PciProgIf) {
		*PciProgIf = getPciProgIf(*PciDeviceId);
	}

	Trace(TRACE_LEVEL_VERBOSE, "HcdiOptionFlags %lu, PciVendorId %#04hx, PciDeviceId %#04hx, PciClass %#02x, PciSubClass %#02x, "
				   "PciRevisionId %#02x, PciProgIf %#02x", 
					HcdiOptionFlags ? *HcdiOptionFlags : 0,
					PciVendorId ? *PciVendorId : 0,
					PciDeviceId ? *PciDeviceId : 0,
					PciClass ? *PciClass : 0,
					PciSubClass ? *PciSubClass : 0,
					PciRevisionId ? *PciRevisionId : 0, 
					PciProgIf ? *PciProgIf : 0);

	return STATUS_SUCCESS;
}

NTSTATUS USB_BUSIFFN EnumLogEntry(_In_ PVOID /*BusContext*/, _In_ ULONG DriverTag, _In_ ULONG EnumTag, _In_ ULONG P1, _In_ ULONG P2)
{
	TraceCall("DriverTag %lu, EnumTag %lu, P1 %lu, P2 %lu -> not supported", DriverTag, EnumTag, P1, P2);
	return STATUS_NOT_SUPPORTED;
}

PAGEABLE NTSTATUS query_interface_usbdi(vpdo_dev_t *vpdo, USHORT size, USHORT version, INTERFACE *intf)
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

	if (size != iface_size[version]) {
		Trace(TRACE_LEVEL_ERROR, "%!usb_busif_usbdi_version!: size %d != %d", version, size, iface_size[version]);
		return STATUS_INVALID_PARAMETER;
	}

	auto &r = *reinterpret_cast<USB_BUS_INTERFACE_USBDI_V3*>(intf);

	switch (version) {
	case USB_BUSIF_USBDI_VERSION_3:
		r.QueryBusTimeEx = QueryBusTimeEx;
		r.QueryControllerType = QueryControllerType;
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_2:
		r.EnumLogEntry = EnumLogEntry;
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_1:
		r.IsDeviceHighSpeed = IsDeviceHighSpeed;
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_0:
		r.Size = size;
		r.Version = version;
		//
		r.BusContext = vpdo;
		r.InterfaceReference = InterfaceReference;
		r.InterfaceDereference = InterfaceDereference;
		//
		r.GetUSBDIVersion = GetUSBDIVersion;
		r.QueryBusTime = QueryBusTime;
		r.SubmitIsoOutUrb = SubmitIsoOutUrb;
		r.QueryBusInformation = QueryBusInformation;
	}

	Trace(TRACE_LEVEL_VERBOSE, "%!usb_busif_usbdi_version!", version);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS query_interface_usbcam(USHORT size, USHORT version, INTERFACE* intf)
{
	PAGED_CODE();

	if (version != USBCAMD_VERSION_200) {
		Trace(TRACE_LEVEL_ERROR, "Version %d != %d", version, USBCAMD_VERSION_200);
		return STATUS_INVALID_PARAMETER;
	}

	auto &r = *reinterpret_cast<USBCAMD_INTERFACE*>(intf);
	if (size != sizeof(r)) {
		Trace(TRACE_LEVEL_ERROR, "Size %d != %Iu", size, sizeof(r));
		return STATUS_INVALID_PARAMETER;
	}


	Trace(TRACE_LEVEL_WARNING, "Not supported");
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS GetLocationString(
	_Inout_opt_ PVOID Context,
	_Outptr_
	_At_(*LocationStrings, _When_(return == 0, __drv_allocatesMem(Mem)))
	PZZWSTR *LocationStrings)
{
	auto vdev = static_cast<vdev_t*>(Context);
	NTSTATUS st = STATUS_SUCCESS;

	WCHAR buf[32];
	size_t remaining = 0;

	if (vdev->type == VDEV_VPDO) {
		auto vpdo = reinterpret_cast<vpdo_dev_t*>(vdev);
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
	auto sz = sizeof(buf) - remaining*sizeof(*buf);

	*LocationStrings = (PZZWSTR)ExAllocatePoolWithTag(PagedPool, sz, USBIP_VHCI_POOL_TAG);
	if (*LocationStrings) {
		RtlCopyMemory(*LocationStrings, buf, sz);
		return STATUS_SUCCESS;
	}

	Trace(TRACE_LEVEL_ERROR, "Can't allocate memory: size %Iu", sz);
	return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS query_interface_location(vdev_t *vdev, USHORT size, USHORT version, INTERFACE *intf)
{
	auto &r = *reinterpret_cast<PNP_LOCATION_INTERFACE*>(intf);
	if (size != sizeof(r)) {
		Trace(TRACE_LEVEL_ERROR, "Size %d != %Iu, version %d", size, sizeof(r), version);
		return STATUS_INVALID_PARAMETER;
	}

	r.Size = sizeof(r);
	r.Version = 1;
	r.Context = vdev;
	r.InterfaceReference = InterfaceReference;
	r.InterfaceDereference = InterfaceDereference;
	r.GetLocationString = GetLocationString;

	return STATUS_SUCCESS;
}

} // namespace

/*
 * On success, a bus driver sets Irp->IoStatus.Information to zero.
 * If a bus driver does not export the requested interface and therefore does not handle this IRP
 * for a child PDO, the bus driver leaves Irp->IoStatus.Status as is and completes the IRP.
 */
PAGEABLE NTSTATUS pnp_query_interface(vdev_t *vdev, IRP *irp)
{
	PAGED_CODE();

	auto irpstack = IoGetCurrentIrpStackLocation(irp);
	auto &qi = irpstack->Parameters.QueryInterface;

	auto status = irp->IoStatus.Status;

	if (IsEqualGUID(*qi.InterfaceType, GUID_PNP_LOCATION_INTERFACE)) {
		status = query_interface_location(vdev, qi.Size, qi.Version, qi.Interface);
	} else if (IsEqualGUID(*qi.InterfaceType, GUID_USBCAMD_INTERFACE)) {
		status = query_interface_usbcam(qi.Size, qi.Version, qi.Interface);
	} else if (IsEqualGUID(*qi.InterfaceType, USB_BUS_INTERFACE_USBDI_GUID) && vdev->type == VDEV_VPDO) {
		status = query_interface_usbdi((vpdo_dev_t*)vdev, qi.Size, qi.Version, qi.Interface);
	}

	if (status == STATUS_SUCCESS) {
		irp->IoStatus.Information = 0;
	}

	TraceCall("%!GUID! -> %!STATUS!", qi.InterfaceType, status);
	return irp_done(irp, status);
}
