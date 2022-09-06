#include "wmi.h"
#include "trace.h"
#include "wmi.tmh"

#include "vhci.h"
#include "irp.h"
#include "dev.h"
#include <usbip\vhci.h>

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
	_Inout_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp,
	_In_ ULONG GuidIndex,
	_In_ ULONG /*InstanceIndex*/,
	_In_ ULONG DataItemId,
	_In_ ULONG BufferSize,
	_In_reads_bytes_(BufferSize) PUCHAR /*Buffer*/)
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

	return WmiCompleteRequest(DeviceObject, Irp, status, requiredSize, IO_NO_INCREMENT);
}

_Function_class_(WMI_SET_DATABLOCK_CALLBACK)
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE
NTSTATUS SetWmiDataBlock(
	_Inout_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp,
	_In_ ULONG GuidIndex,
	_In_ ULONG /*InstanceIndex*/,
	_In_ ULONG BufferSize,
	_In_reads_bytes_(BufferSize) PUCHAR /*Buffer*/)
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

	return WmiCompleteRequest(DeviceObject, Irp, status, requiredSize, IO_NO_INCREMENT);
}

_Function_class_(WMI_QUERY_REGINFO_CALLBACK)
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE
NTSTATUS QueryWmiDataBlock(
	_Inout_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP Irp,
	_In_ ULONG GuidIndex,
	[[maybe_unused]] _In_ ULONG InstanceIndex,
	[[maybe_unused]] _In_ ULONG InstanceCount,
	_Out_writes_opt_(InstanceCount) PULONG InstanceLengthArray,
	_In_ ULONG BufferAvail,
	_Out_writes_bytes_opt_(BufferAvail) PUCHAR Buffer)
{
	PAGED_CODE();

	auto vhci = to_vhci_or_null(DeviceObject);
	ULONG size = 0;
	NTSTATUS status{};

	// Only ever registers 1 instance per guid
	NT_ASSERT(!InstanceIndex && InstanceCount == 1);

	switch (GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		size = sizeof (USBIP_BUS_WMI_STD_DATA);
		if (BufferAvail >= size) {
			*(USBIP_BUS_WMI_STD_DATA*)Buffer = vhci->StdUSBIPBusData;
			*InstanceLengthArray = size;
		} else {
			status = STATUS_BUFFER_TOO_SMALL;
		}
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	return WmiCompleteRequest(DeviceObject, Irp, status, size, IO_NO_INCREMENT);
}

_Function_class_(WMI_QUERY_REGINFO_CALLBACK)
_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE
NTSTATUS QueryWmiRegInfo(
	_Inout_ PDEVICE_OBJECT DeviceObject,
	_Inout_ PULONG RegFlags,
	_Inout_ PUNICODE_STRING /*InstanceName*/,
	_Outptr_result_maybenull_ PUNICODE_STRING *RegistryPath,
	_Inout_ PUNICODE_STRING MofResourceName,
	_Outptr_result_maybenull_ PDEVICE_OBJECT *Pdo)
{
	PAGED_CODE();

	auto vdev = to_vdev(DeviceObject);

	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &Globals.RegistryPath;
	*Pdo = vdev->pdo;
	RtlInitUnicodeString(MofResourceName, L"USBIPVhciWMI");

	return STATUS_SUCCESS;
}

} // namespace


_IRQL_requires_(PASSIVE_LEVEL)
_IRQL_requires_same_
_Function_class_(DRIVER_DISPATCH)
_Dispatch_type_(IRP_MJ_SYSTEM_CONTROL)
extern "C" PAGEABLE NTSTATUS vhci_system_control(_In_ PDEVICE_OBJECT devobj, _In_ PIRP irp)
{
	PAGED_CODE();
	TraceDbg("Enter");

	auto irpstack = IoGetCurrentIrpStackLocation(irp);

	auto vhci = to_vhci_or_null(devobj);
	if (!vhci) { // vpdo just complete the request with the current status
		Trace(TRACE_LEVEL_INFORMATION, "Skip %!sysctrl!", irpstack->MinorFunction);
		return CompleteRequestAsIs(irp);
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

	auto &r = vhci->WmiLibInfo;

	r.GuidCount = ARRAYSIZE(USBIPBusWmiGuidList);
	r.GuidList = USBIPBusWmiGuidList;
	r.QueryWmiRegInfo = QueryWmiRegInfo;
	r.QueryWmiDataBlock = QueryWmiDataBlock;
	r.SetWmiDataBlock = SetWmiDataBlock;
	r.SetWmiDataItem = SetWmiDataItem;
	r.ExecuteWmiMethod = nullptr;
	r.WmiFunctionControl = nullptr;

	auto status = IoWMIRegistrationControl(vhci->Self, WMIREG_ACTION_REGISTER);
	vhci->StdUSBIPBusData.ErrorCount = 0;

	TraceDbg("%!STATUS!", status);
	return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
PAGEABLE NTSTATUS dereg_wmi(vhci_dev_t *vhci)
{
	PAGED_CODE();
	TraceDbg("%04x", ptr4log(vhci));
	return IoWMIRegistrationControl(vhci->Self, WMIREG_ACTION_DEREGISTER);
}
