/*
 * Copyright (C) 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#include "query_interface.h"
#include "trace.h"
#include "query_interface.tmh"

#include "driver.h"
#include "device.h"

namespace
{

using namespace usbip;

inline auto& get_filter_ext(_In_ void *BusContext)
{
	return *static_cast<filter_ext*>(BusContext);
}

_Function_class_(PINTERFACE_REFERENCE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void USB_BUSIFFN InterfaceReference(_In_ void *BusContext)
{
	auto &f = get_filter_ext(BusContext);
	TraceDbg("%04x", ptr04x(f.self));
}

_Function_class_(PINTERFACE_DEREFERENCE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void USB_BUSIFFN InterfaceDereference(_In_ void *BusContext)
{
	auto &f = get_filter_ext(BusContext);
	TraceDbg("%04x", ptr04x(f.self));
}

_Function_class_(PUSB_BUSIFFN_SUBMIT_ISO_OUT_URB)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN SubmitIsoOutUrb(_In_ void *BusContext, _In_ URB*)
{
	auto &f = get_filter_ext(BusContext);
	Trace(TRACE_LEVEL_ERROR, "%04x, STATUS_NOT_SUPPORTED", ptr04x(f.self));
	return STATUS_NOT_SUPPORTED;
}

_Function_class_(PUSB_BUSIFFN_ENUM_LOG_ENTRY)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS USB_BUSIFFN EnumLogEntry(
	[[maybe_unused]] _In_ PVOID BusContext,
	[[maybe_unused]] _In_ ULONG DriverTag,
	[[maybe_unused]] _In_ ULONG EnumTag,
	[[maybe_unused]] _In_ ULONG P1,
	[[maybe_unused]] _In_ ULONG P2)
{
	auto &f = get_filter_ext(BusContext);
	TraceDbg("%04x, STATUS_NOT_SUPPORTED", ptr04x(f.self));
	return STATUS_NOT_SUPPORTED;
}

/*
 * If return zero, QueryBusTime() will be called again and again.
 * @return the current 32-bit USB frame number
 */
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusTime(
	_In_ void *BusContext, 
	_Out_opt_ ULONG *CurrentUsbFrame)
{
	if (!CurrentUsbFrame) {
		return STATUS_INVALID_PARAMETER;
	}

	auto &val = *CurrentUsbFrame;

	auto &fltr = get_filter_ext(BusContext);
	val = fltr.device.start_frame;

	if (!val) {
		val = USBD_ISO_START_FRAME_RANGE/10;
	}

	// TraceFlood("%lu", val); // too often
	return STATUS_SUCCESS;
}

/*
 * @return the current USB 2.0 frame/micro-frame number when called for
 *         a USB device attached to a USB 2.0 host controller
 *
 * The lowest 3 bits of the returned micro-frame value will contain the current 125us
 * micro-frame, while the upper 29 bits will contain the current 1ms USB frame number.
 */
_Function_class_(PUSB_BUSIFFN_QUERY_BUS_TIME_EX)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusTimeEx(
	_In_opt_ PVOID BusContext,
	_Out_opt_ PULONG HighSpeedFrameCounter)
{
	auto st = QueryBusTime(BusContext, HighSpeedFrameCounter);
	if (NT_SUCCESS(st)) {
		*HighSpeedFrameCounter <<= 3;
	}
	return st;
}

_Function_class_(PUSB_BUSIFFN_QUERY_BUS_INFORMATION)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryBusInformation(
	_In_ void *BusContext,
	_In_ ULONG Level,
	_Inout_ PVOID BusInformationBuffer,
	_Out_ PULONG BusInformationBufferLength, // really _Inout_
	_Out_opt_ PULONG BusInformationActualLength)
{
	auto &fltr = get_filter_ext(BusContext);
	auto &bi = fltr.device.usbdi.bus_info;

	auto name_len = bi.level_1.ControllerNameLength; // bytes
	if (!name_len) { // setQueryBusInformation failed
		return STATUS_UNSUCCESSFUL;
	}

	ULONG min_len{};

	switch (Level) {
	case 1:
		min_len = sizeof(bi.level_1);
		break;
	case 0:
		min_len = sizeof(_USB_BUS_INFORMATION_LEVEL_0);
		break;
	default:
		Trace(TRACE_LEVEL_ERROR, "Level %lu, NOT_SUPPORTED", Level);
		return STATUS_NOT_SUPPORTED;
	}

	if (*BusInformationBufferLength < min_len) {
		Trace(TRACE_LEVEL_ERROR, "dev %04x, Level %lu, BusInformationBufferLength %lu, BUFFER_TOO_SMALL", 
			                  ptr04x(fltr.self), Level, *BusInformationBufferLength);
		return STATUS_BUFFER_TOO_SMALL;
	}

	ULONG actual_len;

	if (Level) {
		const ULONG name_offset = offsetof(_USB_BUS_INFORMATION_LEVEL_1, ControllerNameUnicodeString);
		actual_len = min(name_offset + name_len, *BusInformationBufferLength);
	} else {
		actual_len = min_len;
	}

	RtlCopyMemory(BusInformationBuffer, &bi, actual_len);
	*BusInformationBufferLength = actual_len;

	if (BusInformationActualLength) {
		*BusInformationActualLength = actual_len;
	}

	return STATUS_SUCCESS;
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void setQueryBusInformation(_Inout_ filter_ext &fltr, _Inout_ _USB_BUS_INTERFACE_USBDI_V3 &r)
{
	PAGED_CODE();

	const ULONG level = 1; // highest
	auto &bi = fltr.device.usbdi.bus_info;

	ULONG len = sizeof(bi);
	ULONG actual_len{};

	auto st = r.QueryBusInformation(r.BusContext, level, &bi, &len, &actual_len);

	if (NT_ERROR(st)) {
		bi.level_1.ControllerNameLength = 0;
		Trace(TRACE_LEVEL_ERROR, "QueryBusInformation(Level=%lu), Length %lu, ActualLength %lu, %!STATUS!", 
					  level, len, actual_len, st);
		return;
	}

	auto name_len = static_cast<USHORT>(bi.level_1.ControllerNameLength); // bytes

	const ULONG name_offset = offsetof(_USB_BUS_INFORMATION_LEVEL_1, ControllerNameUnicodeString);
	auto remaining_len = actual_len - name_offset;

	if (remaining_len < name_len) { // usbdi_bus_info cannot accomodate this name
		bi.level_1.ControllerNameLength = 0;
		Trace(TRACE_LEVEL_ERROR, "BusInformationActualLength %lu, ControllerNameLength %lu", 
			                  actual_len, name_len);
		return;
	}

	UNICODE_STRING name {
		.Length = name_len,
		.MaximumLength = name_len,
		.Buffer = bi.level_1.ControllerNameUnicodeString
	};

	TraceDbg("Level %lu, TotalBandwidth %lu, ConsumedBandwidth %lu, ControllerName '%!USTR!'", 
		  level, bi.level_1.TotalBandwidth, bi.level_1.ConsumedBandwidth, &name);
}

_Function_class_(PUSB_BUSIFFN_GETUSBDI_VERSION)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
void USB_BUSIFFN GetUSBDIVersion(
	_In_ void *BusContext,
	_Out_opt_ USBD_VERSION_INFORMATION *VersionInformation,
	_Out_opt_ ULONG *HcdCapabilities)
{
	auto &fltr = get_filter_ext(BusContext);
	auto &v = fltr.device.usbdi.version;

	if (auto &vi = v.VersionInformation; VersionInformation) {
		VersionInformation->USBDI_Version = vi.USBDI_Version;
		VersionInformation->Supported_USB_Version = vi.Supported_USB_Version;
	}

	if (HcdCapabilities) {
		*HcdCapabilities = v.HcdCapabilities; // @see USB_HCD_CAPS_SUPPORTS_RT_THREADS
	}
}

_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void setGetUSBDIVersion(_Inout_ filter_ext &fltr, _Inout_ _USB_BUS_INTERFACE_USBDI_V3 &r)
{
	PAGED_CODE();

	auto &v = fltr.device.usbdi.version;
	auto &vi = v.VersionInformation;

	r.GetUSBDIVersion(r.BusContext, &vi, &v.HcdCapabilities);

	TraceDbg("dev %04x, USBDI_Version %#lx, Supported_USB_Version %#lx, HcdCapabilities %#lx",
		  ptr04x(fltr.self), vi.USBDI_Version, vi.Supported_USB_Version, v.HcdCapabilities);
}

_Function_class_(PUSB_BUSIFFN_IS_DEVICE_HIGH_SPEED)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ BOOLEAN USB_BUSIFFN IsDeviceHighSpeed(_In_opt_ void *BusContext)
{
	auto &fltr = get_filter_ext(BusContext);
	return fltr.device.usbdi.IsDeviceHighSpeed;
}

_Function_class_(PUSB_BUSIFFN_QUERY_CONTROLLER_TYPE)
_IRQL_requires_same_
_IRQL_requires_max_(DISPATCH_LEVEL)
_Must_inspect_result_ NTSTATUS USB_BUSIFFN QueryControllerType(
	_In_opt_ PVOID BusContext,
	_Out_opt_ PULONG HcdiOptionFlags,
	_Out_opt_ PUSHORT PciVendorId,
	_Out_opt_ PUSHORT PciDeviceId,
	_Out_opt_ PUCHAR PciClass,
	_Out_opt_ PUCHAR PciSubClass,
	_Out_opt_ PUCHAR PciRevisionId,
	_Out_opt_ PUCHAR PciProgIf)
{
	auto &fltr = get_filter_ext(BusContext);
	auto &r = fltr.device.usbdi.controller_type;

	if (!r.PciClass) { // setQueryControllerType failed
		return STATUS_UNSUCCESSFUL;
	}

	if (HcdiOptionFlags) {
		*HcdiOptionFlags = r.HcdiOptionFlags;
	}

	if (PciVendorId) {
		*PciVendorId = r.PciVendorId;
	}

	if (PciDeviceId) {
		*PciDeviceId = r.PciDeviceId;
	}

	if (PciClass) {
		*PciClass = r.PciClass;
	}

	if (PciSubClass) {
		*PciSubClass = r.PciSubClass;
	}

	if (PciRevisionId) {
		*PciRevisionId = r.PciRevisionId;
	}

	if (PciProgIf) {
		*PciProgIf = r.PciProgIf;
	}

	return STATUS_SUCCESS;
}

/*
 * PciClass is typically set to PCI_CLASS_SERIAL_BUS_CTLR (0x0C).
 * PciSubClass is typically set to PCI_SUBCLASS_SB_USB (0x03).
 * 
 * PciProgif is typically set to one of the following values:
 * 0x00 - Universal Host Controller Interface (UHCI)
 * 0x10 - Open Host Controller Interface (OHCI)
 * 0x20 - Enhanced Host Controller Interface (EHCI)
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void setQueryControllerType(_Inout_ filter_ext &fltr, _Inout_ _USB_BUS_INTERFACE_USBDI_V3 &r)
{
	PAGED_CODE();
	auto &d = fltr.device.usbdi.controller_type;

	auto st = r.QueryControllerType(r.BusContext, 
					&d.HcdiOptionFlags,
					&d.PciVendorId, &d.PciDeviceId,
					&d.PciClass, &d.PciSubClass, &d.PciRevisionId,
					&d.PciProgIf);

	if (NT_ERROR(st)) {
		Trace(TRACE_LEVEL_ERROR, "dev %04x, %!STATUS!", ptr04x(fltr.self), st);
		d.PciClass = 0;
		return;
	}

	TraceDbg("dev %04x, HcdiOptionFlags %#lx, PciVendorId %#04hx, PciDeviceId %#04hx, "
		 "PciClass %#02x, PciSubClass %#02x, PciRevisionId %#02x, PciProgIf %#02x", 
		  ptr04x(fltr.self), d.HcdiOptionFlags, d.PciVendorId, d.PciDeviceId,
		  d.PciClass, d.PciSubClass, d.PciRevisionId, 
		  d.PciProgIf);

	NT_ASSERT(d.PciClass);
}

} // namespace


/*
 * The first implementation allocated usbdi_ctx and InterfaceDereference freed it when 
 * reference count dropped to zero. But Driver Verifier sometimes catched memory leak
 * for usbdi_ctx because InterfaceDereference was not called.
 * 
 * struct usbdi_ctx
 * {
 *	LONG ref_count;
 *	filter_ext *self;
 *	_USB_BUS_INTERFACE_USBDI_V3 v3; // copy of original interface
 * };
 */
_IRQL_requires_same_
_IRQL_requires_(PASSIVE_LEVEL)
PAGED void usbip::query_interface(_Inout_ filter_ext &fltr, _Inout_ _USB_BUS_INTERFACE_USBDI_V3 &r)
{
	PAGED_CODE();

	switch (auto &di = fltr.device.usbdi; r.Version) {
	case USB_BUSIF_USBDI_VERSION_3:
		if (auto &ct = di.controller_type; !ct.PciClass) { // first time
			setQueryControllerType(fltr, r); // can fail, do not check ct.PciClass
		}
		r.QueryBusTimeEx = QueryBusTimeEx;
		r.QueryControllerType = QueryControllerType;
	case USB_BUSIF_USBDI_VERSION_2:
		r.EnumLogEntry = EnumLogEntry;
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_1:
		di.IsDeviceHighSpeed = r.IsDeviceHighSpeed(r.BusContext);
		r.IsDeviceHighSpeed = IsDeviceHighSpeed;
		[[fallthrough]];
	case USB_BUSIF_USBDI_VERSION_0:
		if (auto &vi = di.version.VersionInformation; !vi.USBDI_Version) { // first time
			setGetUSBDIVersion(fltr, r);
			setQueryBusInformation(fltr, r);
			NT_ASSERT(vi.USBDI_Version);
		}
		r.GetUSBDIVersion = GetUSBDIVersion;
		r.QueryBusTime = QueryBusTime;
		r.SubmitIsoOutUrb = SubmitIsoOutUrb;
		r.QueryBusInformation = QueryBusInformation;
		[[fallthrough]];
	default: // INTERFACE
		r.InterfaceDereference(r.BusContext); // release
		r.InterfaceDereference = InterfaceDereference;

		r.InterfaceReference = InterfaceReference;
		r.BusContext = &fltr;
	}
}
