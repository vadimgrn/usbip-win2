#include "wmi.h"
#include "trace.h"
#include "wmi.tmh"

#include "vhci.h"
#include "usbip_vhci_api.h"
#include "irp.h"

#include <wmistr.h>

namespace
{

enum { WMI_USBIP_BUS_DRIVER_INFORMATION };

WMIGUIDREGINFO USBIPBusWmiGuidList[] = 
{
	{ &USBIP_BUS_WMI_STD_DATA_GUID, 1, 0 } // driver information
};

_Function_class_(WMI_SET_DATAITEM_CALLBACK)
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE
NTSTATUS SetWmiDataItem(
	__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	__in ULONG /*InstanceIndex*/, __in ULONG DataItemId, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR /*Buffer*/)
{
	PAGED_CODE();

	ULONG requiredSize = 0;
	NTSTATUS status{};

	switch (GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		if (DataItemId == 2) {
			requiredSize = sizeof(ULONG);
			if (BufferSize < requiredSize) {
				status = STATUS_BUFFER_TOO_SMALL;
			}
		} else {
			status = STATUS_WMI_READ_ONLY;
		}
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	return WmiCompleteRequest(devobj, irp, status, requiredSize, IO_NO_INCREMENT);
}

_Function_class_(WMI_SET_DATABLOCK_CALLBACK)
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE
NTSTATUS SetWmiDataBlock(
	__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	__in ULONG /*InstanceIndex*/, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR /*Buffer*/)
{
	PAGED_CODE();

	ULONG requiredSize = 0;
	NTSTATUS status{};

	switch(GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		requiredSize = sizeof(USBIP_BUS_WMI_STD_DATA);
		if (BufferSize < requiredSize) {
			status = STATUS_BUFFER_TOO_SMALL;
		}
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	return WmiCompleteRequest(devobj, irp, status, requiredSize, IO_NO_INCREMENT);
}

_Function_class_(WMI_QUERY_REGINFO_CALLBACK)
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE
NTSTATUS QueryWmiDataBlock(
	__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	[[maybe_unused]] __in ULONG InstanceIndex, [[maybe_unused]] __in ULONG InstanceCount, __inout PULONG InstanceLengthArray,
	__in ULONG OutBufferSize, __out_bcount(OutBufferSize) PUCHAR Buffer)
{
	PAGED_CODE();

	auto vhci = to_vhci_or_null(devobj);
	ULONG size = 0;
	NTSTATUS status{};

	// Only ever registers 1 instance per guid
	NT_ASSERT(!InstanceIndex && InstanceCount == 1);

	switch (GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		size = sizeof (USBIP_BUS_WMI_STD_DATA);
		if (OutBufferSize >= size) {
			*(USBIP_BUS_WMI_STD_DATA*)Buffer = vhci->StdUSBIPBusData;
			*InstanceLengthArray = size;
		} else {
			status = STATUS_BUFFER_TOO_SMALL;
		}
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	return WmiCompleteRequest(devobj, irp, status, size, IO_NO_INCREMENT);
}

_Function_class_(WMI_QUERY_REGINFO_CALLBACK)
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE
NTSTATUS QueryWmiRegInfo(
	__in PDEVICE_OBJECT devobj, __out ULONG *RegFlags, __out PUNICODE_STRING /*InstanceName*/,
	__out PUNICODE_STRING *RegistryPath, __out PUNICODE_STRING MofResourceName, __out PDEVICE_OBJECT *Pdo)
{
	PAGED_CODE();

	auto vdev = to_vdev(devobj);

	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &Globals.RegistryPath;
	*Pdo = vdev->pdo;
	RtlInitUnicodeString(MofResourceName, L"USBIPVhciWMI");

	return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
extern "C" PAGEABLE NTSTATUS vhci_system_control(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	PAGED_CODE();
	TraceDbg("Enter irql %!irql!", KeGetCurrentIrql());

	auto irpstack = IoGetCurrentIrpStackLocation(irp);

	auto vhci = to_vhci_or_null(devobj);
	if (!vhci) { // vpdo just complete the request with the current status
		Trace(TRACE_LEVEL_INFORMATION, "Skip %!sysctrl!", irpstack->MinorFunction);
		return CompleteRequestIoStatus(irp);
	}

	Trace(TRACE_LEVEL_INFORMATION, "%!sysctrl!", irpstack->MinorFunction);

	if (vhci->PnPState == pnp_state::Removed) {
		return CompleteRequest(irp, STATUS_NO_SUCH_DEVICE);
	}

	SYSCTL_IRP_DISPOSITION disposition;
	auto status = WmiSystemControl(&vhci->WmiLibInfo, devobj, irp, &disposition);
	
	switch(disposition) {
	case IrpProcessed: // This irp has been processed and may be completed or pending
		break;
	case IrpNotCompleted: // irp has not been completed, but has been fully processed we will complete it now
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		break;
	case IrpForward:
	case IrpNotWmi: // irp is either not a WMI irp or is a WMI irp targetted at a device lower in the stack
		IoSkipCurrentIrpStackLocation(irp);
		status = IoCallDriver(vhci->devobj_lower, irp);
		break;
	default: // should never get here
		IoSkipCurrentIrpStackLocation(irp);
		status = IoCallDriver(vhci->devobj_lower, irp);
	}

	TraceDbg("Leave %!STATUS!", status);
	return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS reg_wmi(vhci_dev_t *vhci)
{
	PAGED_CODE();
	TraceDbg("%p", vhci);

	vhci->WmiLibInfo.GuidCount = ARRAYSIZE(USBIPBusWmiGuidList);
	vhci->WmiLibInfo.GuidList = USBIPBusWmiGuidList;
	vhci->WmiLibInfo.QueryWmiRegInfo = QueryWmiRegInfo;
	vhci->WmiLibInfo.QueryWmiDataBlock = QueryWmiDataBlock;
	vhci->WmiLibInfo.SetWmiDataBlock = SetWmiDataBlock;
	vhci->WmiLibInfo.SetWmiDataItem = SetWmiDataItem;
	vhci->WmiLibInfo.ExecuteWmiMethod = nullptr;
	vhci->WmiLibInfo.WmiFunctionControl = nullptr;

	auto status = IoWMIRegistrationControl(vhci->Self, WMIREG_ACTION_REGISTER);
	vhci->StdUSBIPBusData.ErrorCount = 0;

	TraceDbg("irql %!irql!, %!STATUS!", KeGetCurrentIrql(), status);
	return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS dereg_wmi(vhci_dev_t *vhci)
{
	PAGED_CODE();
	TraceDbg("%p", vhci);
	return IoWMIRegistrationControl(vhci->Self, WMIREG_ACTION_DEREGISTER);
}
