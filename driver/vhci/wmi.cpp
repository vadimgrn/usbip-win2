#include "wmi.h"
#include "trace.h"
#include "wmi.tmh"

#include "vhci.h"
#include "usbip_vhci_api.h"
#include "irp.h"

#include <wmistr.h>

namespace
{

const wchar_t MOFRESOURCENAME[] = L"USBIPVhciWMI";
enum { WMI_USBIP_BUS_DRIVER_INFORMATION };

WMIGUIDREGINFO USBIPBusWmiGuidList[] = 
{
	{ &USBIP_BUS_WMI_STD_DATA_GUID, 1, 0 } // driver information
};

} // namespace


extern "C" {

WMI_QUERY_REGINFO_CALLBACK vhci_QueryWmiRegInfo;
WMI_QUERY_DATABLOCK_CALLBACK vhci_QueryWmiDataBlock;
WMI_SET_DATABLOCK_CALLBACK vhci_SetWmiDataBlock;
WMI_SET_DATAITEM_CALLBACK vhci_SetWmiDataItem;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, vhci_QueryWmiRegInfo)
#pragma alloc_text(PAGE, vhci_QueryWmiDataBlock)
#pragma alloc_text(PAGE, vhci_SetWmiDataBlock)
#pragma alloc_text(PAGE, vhci_SetWmiDataItem)
#endif

NTSTATUS vhci_SetWmiDataItem(
	__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	__in ULONG /*InstanceIndex*/, __in ULONG DataItemId, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR /*Buffer*/)
{
	PAGED_CODE();

	ULONG		requiredSize = 0;
	NTSTATUS	status;

	switch (GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		if (DataItemId == 2) {
			requiredSize = sizeof(ULONG);

			if (BufferSize < requiredSize) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			status = STATUS_SUCCESS;
		}
		else {
			status = STATUS_WMI_READ_ONLY;
		}
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	status = WmiCompleteRequest(devobj, irp, status, requiredSize, IO_NO_INCREMENT);

	return status;
}

NTSTATUS vhci_SetWmiDataBlock(
	__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	__in ULONG /*InstanceIndex*/, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR /*Buffer*/)
{
	PAGED_CODE();

	ULONG		requiredSize = 0;
	NTSTATUS	status;

	switch(GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		requiredSize = sizeof(USBIP_BUS_WMI_STD_DATA);

		if (BufferSize < requiredSize) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		status = STATUS_SUCCESS;
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
		break;
	}

	status = WmiCompleteRequest(devobj, irp, status, requiredSize, IO_NO_INCREMENT);

	return(status);
}

NTSTATUS vhci_QueryWmiDataBlock(
	__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	[[maybe_unused]] __in ULONG InstanceIndex, [[maybe_unused]] __in ULONG InstanceCount, __inout PULONG InstanceLengthArray,
	__in ULONG OutBufferSize, __out_bcount(OutBufferSize) PUCHAR Buffer)
{
	PAGED_CODE();

	vhci_dev_t *vhci = devobj_to_vhci_or_null(devobj);
	ULONG		size = 0;
	NTSTATUS	status;

	// Only ever registers 1 instance per guid
	NT_ASSERT(!InstanceIndex && InstanceCount == 1);

	switch (GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		size = sizeof (USBIP_BUS_WMI_STD_DATA);
		if (OutBufferSize < size) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		*(USBIP_BUS_WMI_STD_DATA*)Buffer = vhci->StdUSBIPBusData;
		*InstanceLengthArray = size;
		status = STATUS_SUCCESS;
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	status = WmiCompleteRequest(devobj, irp, status, size, IO_NO_INCREMENT);
	return status;
}

NTSTATUS vhci_QueryWmiRegInfo(
	__in PDEVICE_OBJECT devobj, __out ULONG *RegFlags, __out PUNICODE_STRING /*InstanceName*/,
	__out PUNICODE_STRING *RegistryPath, __out PUNICODE_STRING MofResourceName, __out PDEVICE_OBJECT *Pdo)
{
	PAGED_CODE();

	auto vdev = devobj_to_vdev(devobj);

	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &Globals.RegistryPath;
	*Pdo = vdev->pdo;
	RtlInitUnicodeString(MofResourceName, MOFRESOURCENAME);

	return STATUS_SUCCESS;
}

} // extern "C"

extern "C" PAGEABLE NTSTATUS vhci_system_control(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	PAGED_CODE();

	TraceCall("Enter irql %!irql!", KeGetCurrentIrql());

	IO_STACK_LOCATION *irpstack = IoGetCurrentIrpStackLocation(irp);

	vhci_dev_t *vhci = devobj_to_vhci_or_null(devobj);
	if (!vhci) {
		// The vpdo, just complete the request with the current status
		Trace(TRACE_LEVEL_INFORMATION, "Skip %!sysctrl!", irpstack->MinorFunction);
		return irp_done_iostatus(irp);
	}

	Trace(TRACE_LEVEL_INFORMATION, "%!sysctrl!", irpstack->MinorFunction);

	if (vhci->DevicePnPState == Deleted) {
		return irp_done(irp, STATUS_NO_SUCH_DEVICE);
	}

	SYSCTL_IRP_DISPOSITION disposition;
	NTSTATUS status = WmiSystemControl(&vhci->WmiLibInfo, devobj, irp, &disposition);
	
	switch(disposition) {
	case IrpProcessed:
		// This irp has been processed and may be completed or pending.
		break;
	case IrpNotCompleted:
		// This irp has not been completed, but has been fully processed.
		// we will complete it now
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		break;
	case IrpForward:
	case IrpNotWmi:
		// This irp is either not a WMI irp or is a WMI irp targetted
		// at a device lower in the stack.
		IoSkipCurrentIrpStackLocation(irp);
		status = IoCallDriver(vhci->devobj_lower, irp);
		break;
	default:
		// We really should never get here, but if we do just forward....
		NT_ASSERT(FALSE);
		IoSkipCurrentIrpStackLocation(irp);
		status = IoCallDriver(vhci->devobj_lower, irp);
	}

	TraceCall("Leave %!STATUS!", status);
	return status;
}

PAGEABLE NTSTATUS reg_wmi(vhci_dev_t *vhci)
{
	PAGED_CODE();

	vhci->WmiLibInfo.GuidCount = ARRAYSIZE(USBIPBusWmiGuidList);
	vhci->WmiLibInfo.GuidList = USBIPBusWmiGuidList;
	vhci->WmiLibInfo.QueryWmiRegInfo = vhci_QueryWmiRegInfo;
	vhci->WmiLibInfo.QueryWmiDataBlock = vhci_QueryWmiDataBlock;
	vhci->WmiLibInfo.SetWmiDataBlock = vhci_SetWmiDataBlock;
	vhci->WmiLibInfo.SetWmiDataItem = vhci_SetWmiDataItem;
	vhci->WmiLibInfo.ExecuteWmiMethod = nullptr;
	vhci->WmiLibInfo.WmiFunctionControl = nullptr;

	// Register with WMI
	NTSTATUS status = IoWMIRegistrationControl(to_devobj(vhci), WMIREG_ACTION_REGISTER);

	// Initialize the Std device data structure
	vhci->StdUSBIPBusData.ErrorCount = 0;

	Trace(TRACE_LEVEL_VERBOSE, "irql %!irql!, %!STATUS!", KeGetCurrentIrql(), status);
	return status;
}

PAGEABLE NTSTATUS
dereg_wmi(vhci_dev_t *vhci)
{
	PAGED_CODE();

	return IoWMIRegistrationControl(to_devobj(vhci), WMIREG_ACTION_DEREGISTER);
}
